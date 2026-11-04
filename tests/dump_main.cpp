#include <common.hxx>
#include <cstdio>

import bodies;
import common;
import config;
import game;
import loader;
import options;
import stability;
import vehicles;
import useless_checks;

// The write-set dump: load a copy of a game image into this process, run the
// same load pipeline the ASI runs, and print every byte it changed as
// `<RVA> <byte>` in RVA order. That is the parity artefact -- it is what the
// per-build baselines and the original's oracle are compared against.
//
// The image is a heap buffer laid out by RVA, not a mapping at the preferred
// base: nothing here needs 0x400000, so the tool stays 32-bit like the ASI and
// the arms see the same addresses they would see in the game.

namespace
{
    struct Image
    {
        std::vector<uint8_t> bytes;
        std::vector<uint8_t> pristine;
        size_t size{};
    };

    // Copies the headers and every section to its virtual address, leaving the
    // rest zeroed, which is what the loader maps.
    bool Load(const std::filesystem::path& path, Image& image)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            std::printf("cannot open %s\n", path.string().c_str());
            return false;
        }
        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (raw.size() < sizeof(IMAGE_DOS_HEADER))
            return false;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(raw.data() + dos->e_lfanew);
        image.size = nt->OptionalHeader.SizeOfImage;
        image.bytes.assign(image.size, 0);

        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            const auto count = std::min<size_t>(section->SizeOfRawData, image.size - section->VirtualAddress);
            if (section->PointerToRawData + count <= raw.size() && count <= image.size)
                std::memcpy(image.bytes.data() + section->VirtualAddress, raw.data() + section->PointerToRawData, count);
        }
        // The headers are part of the image too.
        const auto headers = std::min<size_t>(nt->OptionalHeader.SizeOfHeaders, raw.size());
        std::memcpy(image.bytes.data(), raw.data(), headers);

        image.pristine = image.bytes;
        return true;
    }

    void Dump(const Image& image, std::FILE* out, std::string_view prefix)
    {
        for (size_t rva = 0; rva < image.size; ++rva)
        {
            if (image.bytes[rva] != image.pristine[rva])
                std::fprintf(out, "%s%06zX %02X\n", prefix.data(), rva, image.bytes[rva]);
        }
    }

    size_t Changed(const Image& image)
    {
        size_t changed = 0;
        for (size_t rva = 0; rva < image.size; ++rva)
            changed += image.bytes[rva] != image.pristine[rva] ? 1 : 0;
        return changed;
    }
}

int main(int argc, char** argv)
{
    std::filesystem::path executable = "..\\game_refs\\GTAIV.exe";
    std::string build = "9";
    std::filesystem::path output;
    std::filesystem::path ini = "ZolikaPatch.ini";
    bool perOption = false;

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--exe" && index + 1 < argc)
            executable = argv[++index];
        else if (argument == "--build" && index + 1 < argc)
            build = argv[++index];
        else if (argument == "--per-option")
            perOption = true;
        else if (output.empty())
            output = argv[index];
        else
            ini = argv[index];
    }
    if (output.empty())
    {
        std::printf("usage: zp_dump [--exe <image>] [--build <compact>] [--per-option] <out> [ini]\n");
        return 2;
    }

    Image image;
    if (!Load(executable, image))
        return 1;

    auto& game = Game::Get();
    game.AttachImage(reinterpret_cast<uintptr_t>(image.bytes.data()), image.size, executable, {});
    Loader::Current() = FromId(static_cast<uint32_t>(std::atoi(build.c_str())));
    if (!std::filesystem::exists(ini))
        std::printf("no ini at %s: every option keeps its default\n", ini.string().c_str());
    Config::Ini() = Feather::Load(ini);

    std::FILE* out = std::fopen(output.string().c_str(), "wb");
    if (out == nullptr)
        return 1;

    RegisterStability();
    RegisterVehicles();
    RegisterUselessChecks();

    if (perOption)
    {
        for (const auto& option : Catalogue::All)
        {
            // Every key off, this one on -- the arm-isolation oracle's document.
            // Leaving the others absent would read their defaults, which for
            // most keys is "on", and every other arm would write into this cell.
            std::string text = "[Options]\n";
            for (const auto& other : Catalogue::All)
                text += std::string(other.key) + "=0\n";
            text += std::string(option.key) + "=1\n";

            Config::Ini() = Feather::Parse(text);
            image.bytes = image.pristine;
            Loader::Run();
            Bodies::Canonicalise(image.bytes.data(), image.pristine.data(), image.size, reinterpret_cast<uintptr_t>(image.bytes.data()));
            Dump(image, out, std::string(option.key) + " ");
        }
    }
    else
    {
        Loader::Run();
            Bodies::Canonicalise(image.bytes.data(), image.pristine.data(), image.size, reinterpret_cast<uintptr_t>(image.bytes.data()));
        Dump(image, out, "");
        std::printf("%s: %zu byte(s) changed, build %s\n", output.string().c_str(), Changed(image), build.c_str());
    }

    std::fclose(out);
    std::printf("%s: %zu byte(s) changed, build %s\n", output.string().c_str(), Changed(image), build.c_str());
    return 0;
}
