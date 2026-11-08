#include <common.hxx>
#include <algorithm>
#include <cstdio>
#include <cstring>

import bodies;
import common;
import config;
import game;
import loader;
import options;
import shared;
import native;
import stability;
import vehicles;
import useless_checks;
import graphics;
import launcher;
import loading;
import audio;
import misc;
import ooc;

// The write-set dump: load a copy of a game image into this process, apply the
// image's own relocations to it (which is what the game's loader does), run the
// same load pipeline the ASI runs, and print every byte it changed as
// `<RVA> <byte>` in RVA order. That is the parity artefact -- it is what the
// per-build baselines and the original's oracle are compared against.
//
// The copy is laid out by RVA in a 64 KiB-aligned allocation, not at the
// preferred base: nothing here needs 0x400000, so the tool stays 32-bit like the
// ASI. The allocation's alignment matters: the loader's delta is always a
// multiple of the 64 KiB allocation granularity, so keeping the dump's delta the
// same keeps the relocation's effect on the low two bytes of a dword identical
// to the game's. The relocation pass moves every absolute operand the image
// carries by `hostBase - OptionalHeader.ImageBase`, so from then on a captured
// pattern operand read out of the buffer is a live address in *this* process --
// exactly what it is in the game. An arm's identity cast is therefore right in
// both places, and the dump can no longer reward a per-arm rebase that would be
// wrong at run time.

namespace
{
    struct Image
    {
        uint8_t* bytes{};                  // the copy the arms run against, 64 KiB-aligned
        size_t size{};
        uintptr_t imageBase{};             // the image's own preferred base
        std::vector<uint8_t> unrelocated;  // the copy before the .reloc pass
        std::vector<uint8_t> pristine;     // the copy after it, before any arm ran
        std::vector<size_t> fixupFields;   // the first byte of every HIGHLOW fixup
        uint32_t delta{};                  // hostBase - ImageBase, the loader's delta

        ~Image()
        {
            if (bytes != nullptr)
                VirtualFree(bytes, 0, MEM_RELEASE);
        }
        Image() = default;
        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;
    };

    // Is `rva` inside a section the image marks writable? Used by the relocated
    // operand check: an arm that writes through a captured operand has to land in a
    // writable word of the image, or the survey has matched the wrong instruction.
    bool WritableRva(const Image& image, size_t rva)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.bytes);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.bytes + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            if (rva < section->VirtualAddress ||
                rva - section->VirtualAddress >= section->Misc.VirtualSize)
                continue;
            return (section->Characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        }
        return false;
    }

    // The range a signature scan covers, from game.ixx's ImageScanRange: the image
    // base to the end of the last executable section. In this buffer the offset past
    // the first byte is the RVA, so the range needs no base arithmetic.
    std::pair<size_t, size_t> ExecutableRange(const Image& image)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.bytes);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.bytes + dos->e_lfanew);
        size_t end = 0;
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            const auto length = section->SizeOfRawData != 0 ? section->SizeOfRawData : section->Misc.VirtualSize;
            if ((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0)
                end = section->VirtualAddress + length;
        }
        if (end == 0)
            end = image.size;
        return { 0, std::min(end, image.size) };
    }

    // The check that would have caught the double-rebase: an arm captures a dword
    // out of the image and casts it to a pointer. The image as loaded holds live
    // addresses, so after this loader's relocation that dword must be exactly
    // `hostBase + RVA`. Measured at IncreaseVehicleModelLimit's anchor, whose
    // `mov ecx, imm32` at match+77 names the model-limit object: the unrelocated
    // dword is a preferred-base VA, the relocated one must differ from it by the
    // loader's delta, and the object it names must be a writable word of the buffer.
    // With the relocation step removed the dword is still the preferred VA and this
    // fails loudly instead of the dump measuring the arm against an address that is
    // not in this process.
    bool VerifyRelocatedOperand(const Image& image, const std::vector<uint8_t>& unrelocated)
    {
        // The recovered 1.0.8.0 anchor is build-agnostic: it is the allocator's
        // model-bound loop, and every supported image carries it once in .text.
        static constexpr uint8_t kAnchor[] = { 0x83, 0xC7, 0x01, 0x81, 0xFF, 0x18, 0x79, 0x00, 0x00, 0x7C, 0xD2 };
        const auto [begin, end] = ExecutableRange(image);
        const auto* first = image.bytes + begin;
        const auto* last = image.bytes + end;
        const auto* found = std::search(first, last, std::begin(kAnchor), std::end(kAnchor));
        if (found == last)
        {
            std::printf("relocation check: IncreaseVehicleModelLimit's anchor is absent; skipped\n");
            return true;
        }

        const size_t anchor = static_cast<size_t>(found - image.bytes);
        const size_t field = anchor + 77;
        if (field + sizeof(uint32_t) > image.size)
        {
            std::printf("relocation check: FAILED: the operand field at RVA %zX is outside the buffer\n", field);
            return false;
        }

        uint32_t preferred{};
        uint32_t relocated{};
        std::memcpy(&preferred, unrelocated.data() + field, sizeof(preferred));
        std::memcpy(&relocated, image.bytes + field, sizeof(relocated));

        if (preferred < image.imageBase || preferred - image.imageBase >= image.size)
        {
            std::printf("relocation check: FAILED: the unrelocated operand at RVA %zX is %08X, "
                        "not a preferred-base VA of this image (base %zX)\n",
                field, preferred, static_cast<uint32_t>(image.imageBase));
            return false;
        }

        const auto expected = static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(image.bytes) + (preferred - static_cast<uint32_t>(image.imageBase)));
        if (relocated != expected)
        {
            std::printf("relocation check: FAILED: the operand at RVA %zX is %08X, expected %08X "
                        "(host base %08X, image base %08X); the .reloc pass did not run\n",
                field, relocated, expected, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image.bytes)),
                static_cast<uint32_t>(image.imageBase));
            return false;
        }

        const auto target = static_cast<size_t>(relocated - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image.bytes)));
        if (target + sizeof(uint32_t) > image.size || !WritableRva(image, target))
        {
            std::printf("relocation check: FAILED: the relocated operand %08X (RVA %zX) is not a "
                        "writable dword of the buffer\n", relocated, target);
            return false;
        }

        std::printf("relocation check: RVA %zX operand %08X -> %08X (writable RVA %zX): OK\n",
            field, preferred, relocated, target);
        return true;
    }

    // Applies the image's `.reloc` entries with the delta the Windows loader would
    // use: the buffer sits at a 64 KiB-aligned heap address, so every preferred-base
    // VA baked into the image moves by `hostBase - OptionalHeader.ImageBase` and the
    // relocation's effect on each dword is the same in kind as the game's. A file
    // with no `.reloc` cannot be modelled (nothing would tell a relocated copy from
    // a preferred-base one), and a fixup outside the buffer is a corrupt image, so
    // both are refused loudly rather than skipped.
    bool ApplyRelocations(Image& image, const std::vector<uint8_t>& unrelocated, const std::string& name)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.bytes);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image.bytes + dos->e_lfanew);
        const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (directory.VirtualAddress == 0 || directory.Size == 0)
        {
            std::printf("%s: refused: the image has no .reloc directory, so a copy at another base "
                        "cannot be modelled\n", name.c_str());
            return false;
        }
        if (static_cast<size_t>(directory.VirtualAddress) + directory.Size > image.size)
        {
            std::printf("%s: refused: the .reloc directory (RVA %zX, %zX byte(s)) runs past the "
                        "image buffer (%zX)\n",
                name.c_str(), static_cast<size_t>(directory.VirtualAddress),
                static_cast<size_t>(directory.Size), image.size);
            return false;
        }

        const auto hostBase = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(image.bytes));
        const auto delta = static_cast<uint32_t>(hostBase - static_cast<uint32_t>(image.imageBase));
        if ((delta & 0xFFFFu) != 0)
        {
            std::printf("%s: refused: the mapped copy is not 64 KiB-aligned with the image base, "
                        "so the delta is not a loader delta (%08X)\n", name.c_str(), delta);
            return false;
        }

        auto* bytes = image.bytes;
        image.fixupFields.clear();
        image.delta = delta;
        const size_t end = static_cast<size_t>(directory.VirtualAddress) + directory.Size;
        size_t applied = 0;
        size_t offset = directory.VirtualAddress;
        while (offset + sizeof(IMAGE_BASE_RELOCATION) <= end)
        {
            const auto* block = reinterpret_cast<const IMAGE_BASE_RELOCATION*>(bytes + offset);
            if (block->SizeOfBlock == 0)
                break;
            if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) || offset + block->SizeOfBlock > end)
            {
                std::printf("%s: refused: malformed .reloc block at RVA %zX (%zX byte(s))\n",
                    name.c_str(), offset, static_cast<size_t>(block->SizeOfBlock));
                return false;
            }

            const auto count =
                (block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(uint16_t);
            const auto* entries = reinterpret_cast<const uint16_t*>(block + 1);
            for (size_t index = 0; index < count; ++index)
            {
                const auto type = entries[index] >> 12;
                const size_t rva = block->VirtualAddress + (entries[index] & 0x0FFF);
                if (type == IMAGE_REL_BASED_ABSOLUTE)
                    continue;
                if (type != IMAGE_REL_BASED_HIGHLOW)
                {
                    std::printf("%s: refused: relocation type %u at RVA %zX (a 32-bit image only "
                                "has HIGHLOW entries here)\n", name.c_str(), type, rva);
                    return false;
                }
                if (rva + sizeof(uint32_t) > image.size)
                {
                    std::printf("%s: refused: a HIGHLOW fixup at RVA %zX lands outside the "
                                "%zX-byte image buffer\n", name.c_str(), rva, image.size);
                    return false;
                }

                uint32_t value{};
                std::memcpy(&value, bytes + rva, sizeof(value));
                value += delta;
                std::memcpy(bytes + rva, &value, sizeof(value));
                image.fixupFields.push_back(rva);
                ++applied;
            }
            offset += block->SizeOfBlock;
        }

        if (applied == 0)
        {
            std::printf("%s: refused: the .reloc directory holds no HIGHLOW fixup\n", name.c_str());
            return false;
        }
        std::sort(image.fixupFields.begin(), image.fixupFields.end());

        std::printf("%s: %zu HIGHLOW fixup(s) applied, delta %08X\n", name.c_str(), applied, delta);
        return VerifyRelocatedOperand(image, unrelocated);
    }

    // Copies the headers and every section to its virtual address, leaving the
    // rest zeroed, which is what the loader maps, then relocates the copy.
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
        image.imageBase = nt->OptionalHeader.ImageBase;
        // VirtualAlloc's result is 64 KiB-aligned, which is what makes the delta a
        // loader delta (see ApplyRelocations).
        image.bytes = static_cast<uint8_t*>(
            VirtualAlloc(nullptr, image.size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (image.bytes == nullptr)
        {
            std::printf("%s: cannot allocate the %zX-byte image buffer\n", path.string().c_str(), image.size);
            return false;
        }
        std::memset(image.bytes, 0, image.size);

        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            const auto count = std::min<size_t>(section->SizeOfRawData, image.size - section->VirtualAddress);
            if (section->PointerToRawData + count <= raw.size() && count <= image.size)
                std::memcpy(image.bytes + section->VirtualAddress, raw.data() + section->PointerToRawData, count);
        }
        // The headers are part of the image too.
        const auto headers = std::min<size_t>(nt->OptionalHeader.SizeOfHeaders, raw.size());
        std::memcpy(image.bytes, raw.data(), headers);

        // The unrelocated copy is what the relocated-operand check measures against,
        // and it is also the coordinate system the baselines speak: a row is a byte
        // the mod changed relative to the original image.
        std::vector<uint8_t> unrelocated(image.bytes, image.bytes + image.size);
        if (!ApplyRelocations(image, unrelocated, path.string()))
            return false;

        image.unrelocated = std::move(unrelocated);
        image.pristine.assign(image.bytes, image.bytes + image.size);
        return true;
    }

    // The dump speaks the baselines' convention: a row is a byte the mod changed,
    // expressed in the *original* image's coordinate system.
    //
    //  * A byte outside a fixup is compared against the unrelocated copy (which is
    //    what the image held there).
    //  * A byte in the low half of a fixup dword is also compared against the
    //    unrelocated copy: the delta is 64 KiB-aligned, so relocation does not move
    //    those bytes and anything that differs is the mod's.
    //  * A byte in the high half of a fixup dword is compared against the relocated
    //    pristine copy: relocation does move it, so a difference means the mod wrote
    //    there, not the loader.
    //  * A substituted published pointer is a stand-in for an arm's own pointer
    //    write, so its four bytes are always compared against the original image.
    void Dump(const Image& image, std::FILE* out, std::string_view prefix,
        const std::vector<size_t>& substituted)
    {
        std::vector<bool> placeholder(image.size, false);
        for (const auto rva : substituted)
            for (size_t index = 0; index < sizeof(uint32_t) && rva + index < image.size; ++index)
                placeholder[rva + index] = true;

        for (size_t rva = 0; rva < image.size; ++rva)
        {
            if (placeholder[rva])
            {
                if (image.bytes[rva] != image.unrelocated[rva])
                    std::fprintf(out, "%s%06zX %02X\n", prefix.data(), rva, image.bytes[rva]);
                continue;
            }

            // The fixup dword that covers this byte, if any. Its high half is the
            // half the 64 KiB-aligned delta moves: a high byte that still equals the
            // relocated pristine byte was moved by the loader, not written by the
            // mod, so it is not a row. Anything else the mod wrote is expressed
            // against the original image, exactly like every other byte.
            bool high = false;
            const auto next = std::upper_bound(image.fixupFields.begin(), image.fixupFields.end(), rva);
            if (next != image.fixupFields.begin())
            {
                const auto field = *(next - 1);
                high = rva < field + sizeof(uint32_t) && rva - field >= 2;
            }
            if (high && image.bytes[rva] == image.pristine[rva])
                continue;

            if (image.bytes[rva] != image.unrelocated[rva])
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
    // A published pointer is a *host* address, so it cannot equal the original's.
    // The baselines record one placeholder for every published module address
    // (0xD0000000), and only the arm knows which words those are: a scan for
    // "four bytes that look like an address in this process" cannot tell a pointer
    // from a displacement plus its neighbouring code, and rewriting the wrong
    // window silently destroys a site (it ate RemoveRGSCLoginRequirement's E9 and
    // then seven other keys' bytes). So the arms register their slots and the dump
    // rewrites exactly those. Returns the RVAs it substituted.
    std::vector<size_t> CanonicalisePublishedPointers(Image& image, uintptr_t hostBase)
    {
        static constexpr uint32_t kBaselinePlaceholder = 0xD0000000u;
        std::vector<size_t> substituted;

        // Two kinds of registered slot. A published *module* address is a host
        // pointer, so the slot holds our own layout and the baseline's placeholder
        // stands in for it. A published *game image* address is a real value in the
        // original too, but the reference dump recorded the same placeholder for it,
        // so the dump substitutes there as well - the arm still writes the true
        // address, and only the tool speaks the baseline's convention.
        for (const auto slot : Bodies::Published())
        {
            if (slot < hostBase || slot + sizeof(uint32_t) > hostBase + image.size)
                continue;

            const auto rva = static_cast<size_t>(slot - hostBase);
            // The registry is process-global, so a slot registered by an earlier
            // cell is still listed here. Only a slot this run actually wrote is a
            // published pointer in this cell: the loop restores the image from the
            // pristine copy before every arm, so an untouched slot still reads its
            // original bytes and must be left alone.
            if (std::memcmp(image.bytes + rva, image.pristine.data() + rva, sizeof(uint32_t)) == 0)
                continue;

            std::memcpy(image.bytes + rva, &kBaselinePlaceholder, sizeof(kBaselinePlaceholder));
            substituted.push_back(rva);
        }

        // A game-image address slot is an RVA in the image the dump is holding, so
        // its slot address is the host base plus that RVA - the same arithmetic as
        // above, and again only when this run actually wrote it.
        for (const auto slot : Bodies::PublishedImage())
        {
            if (slot < hostBase || slot + sizeof(uint32_t) > hostBase + image.size)
                continue;

            const auto rva = static_cast<size_t>(slot - hostBase);
            if (std::memcmp(image.bytes + rva, image.pristine.data() + rva, sizeof(uint32_t)) == 0)
                continue;

            std::memcpy(image.bytes + rva, &kBaselinePlaceholder, sizeof(kBaselinePlaceholder));
            substituted.push_back(rva);
        }
        return substituted;
    }
}

int main(int argc, char** argv)
{
    // A faulting cell is data, not an interruption: a fault here must not raise a
    // Windows "Application Error" dialog on the machine that is running the harness
    // (the 1.0.0.0 cell faults on purpose, and a scan that writes through a stale
    // pointer must fail loudly in the log, not in a message box).
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    std::filesystem::path executable = "..\\game_refs\\GTAIV.exe";
    std::string build = "9";
    std::filesystem::path output;
    std::filesystem::path ini = "ZolikaPatch.ini";
    bool perOption = false;
    bool oocSelfTest = false;

    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        if (argument == "--exe" && index + 1 < argc)
            executable = argv[++index];
        else if (argument == "--build" && index + 1 < argc)
            build = argv[++index];
        else if (argument == "--per-option")
            perOption = true;
        else if (argument == "--ooc-selftest")
            oocSelfTest = true;
        else if (output.empty())
            output = argv[index];
        else
            ini = argv[index];
    }

    // The payload path only runs when the game calls the wrapper the arm installs,
    // so this is the one way the rebuilt decoder and archive builder execute
    // offline: decode the pinned resource and check both results against the
    // fingerprints the recovered bytes carry (source/ooc.ixx, docs/parity.md).
    if (oocSelfTest)
    {
        const char* failure = OocSelfCheck();
        std::printf("ooc selftest: %s\n", failure == nullptr ? "PASS" : failure);
        return failure == nullptr ? 0 : 1;
    }

    if (output.empty())
    {
        std::printf("usage: zp_dump [--exe <image>] [--build <compact>] [--per-option] <out> [ini]\n"
                    "       zp_dump --ooc-selftest\n");
        return 2;
    }

    Image image;
    if (!Load(executable, image))
        return 1;

    auto& game = Game::Get();
    game.AttachImage(reinterpret_cast<uintptr_t>(image.bytes), image.size, executable, {});
    Loader::Current() = FromId(static_cast<uint32_t>(std::atoi(build.c_str())));
    if (!std::filesystem::exists(ini))
        std::printf("no ini at %s: every option keeps its default\n", ini.string().c_str());
    Config::Ini() = Feather::Load(ini);

    std::FILE* out = std::fopen(output.string().c_str(), "wb");
    if (out == nullptr)
        return 1;

    Shared::Register();
    // The same layer registration the ASI performs: it only resolves a pointer
    // and publishes body addresses, so it changes no byte of the image.
    Native::Register();
    RegisterStability();
    RegisterVehicles();
    RegisterUselessChecks();
    RegisterGraphics();
    RegisterLauncher();
    RegisterLoading();
    RegisterAudio();
    RegisterMisc();
    RegisterOoc();

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
            std::memcpy(image.bytes, image.pristine.data(), image.size);
            Loader::Run();
            Bodies::Canonicalise(image.bytes, image.pristine.data(), image.size,
                reinterpret_cast<uintptr_t>(image.bytes), image.imageBase);
            const auto substituted =
                CanonicalisePublishedPointers(image, reinterpret_cast<uintptr_t>(image.bytes));
            Dump(image, out, std::string(option.key) + " ", substituted);
        }
    }
    else
    {
        Loader::Run();
        Bodies::Canonicalise(image.bytes, image.pristine.data(), image.size,
            reinterpret_cast<uintptr_t>(image.bytes), image.imageBase);
        const auto substituted =
            CanonicalisePublishedPointers(image, reinterpret_cast<uintptr_t>(image.bytes));
        Dump(image, out, "", substituted);
        std::printf("%s: %zu byte(s) changed, build %s\n", output.string().c_str(), Changed(image), build.c_str());
    }

    std::fclose(out);
    std::printf("%s: %zu byte(s) changed, build %s\n", output.string().c_str(), Changed(image), build.c_str());
    return 0;
}
