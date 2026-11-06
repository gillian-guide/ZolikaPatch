#include <common.hxx>
#include <cstdio>

import common;
import game;
import native;
import patch;

// The salt rule, pinned on the real game images (files, not a running game).
//
// The original loader preamble scans the game image for
// `68 ? ? ? ? 68 A9 54 14 51 E8` and, on a hit, jumps over its ten salt stores;
// on a miss it writes the fallback set. Native::ResolveSalts() is the layer's
// implementation of that rule, so this program loads each image the same way
// zp_dump does, attaches it to the Game context, calls ResolveSalts() and checks
// the ten slots against the values recovered from the ASI. The three images are
// the two probe-miss builds (1.0.0.0, 1.0.4.0) and 1.0.8.0, where the probe hits.
//
//     zp_salt_check [game_refs]

namespace
{
    struct Slot
    {
        uint32_t reference;  // the ASI slot VA
        uint32_t shipped;    // the ASI's `.data` value
        uint32_t fallback;   // the miss path's store at 0x10052E1E..0x10052E78
    };

    constexpr Slot kSlots[] =
    {
        { 0x10158054u, 0x511454A9u, 0x6B86410Eu },
        { 0x101580C8u, 0x570F5725u, 0x406B4B20u },
        { 0x10158050u, 0x3B0C6738u, 0x6DCD115Au },
        { 0x101580B8u, 0x4E61480Au, 0x62BFDB37u },
        { 0x10158028u, 0x6A6B4F18u, 0x931F73A2u },
        { 0x101580A8u, 0x79A95BF9u, 0x7F6A60D3u },
        { 0x10158040u, 0x4BF36A32u, 0xC9DBDA90u },
        { 0x1015803Cu, 0x022B2DA9u, 0xA4EA0691u },
        { 0x101580D4u, 0x75E005F1u, 0x09399528u },
        { 0x1015805Cu, 0x62E319C6u, 0x1CC648EAu },
    };

    struct Case
    {
        const char* image;
        bool probe;  // whether the probe has a hit on this build
    };

    // The recovered probe result per build: 1.0.8.0 hits at game VA 0xB3878C;
    // 1.0.0.0 and 1.0.4.0 have no hit, so the preamble's stores run.
    constexpr Case kCases[] =
    {
        { "GTAIV.exe", true },        // 1.0.8.0
        { "GTAIV_1000.exe", false },  // 1.0.0.0
        { "GTAIV_1040.exe", false },  // 1.0.4.0
    };

    int checks = 0;
    int failures = 0;

    void Check(bool condition, const char* what, const std::string& detail = {})
    {
        ++checks;
        if (condition)
        {
            std::printf("PASS %s\n", what);
            return;
        }
        ++failures;
        std::printf("FAIL %s", what);
        if (!detail.empty())
            std::printf(" (%s)", detail.c_str());
        std::printf("\n");
    }

    // The same mapping dump_main.cpp uses: the headers and every section at its
    // virtual address, the rest zeroed. The scan range is then the one the ASI
    // sees in the game.
    bool Load(const std::filesystem::path& path, std::vector<uint8_t>& bytes, size_t& size)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return false;

        std::vector<uint8_t> raw((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (raw.size() < sizeof(IMAGE_DOS_HEADER))
            return false;

        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(raw.data() + dos->e_lfanew);
        size = nt->OptionalHeader.SizeOfImage;
        bytes.assign(size, 0);

        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index, ++section)
        {
            const auto count = std::min<size_t>(section->SizeOfRawData, size - section->VirtualAddress);
            if (section->PointerToRawData + count <= raw.size())
                std::memcpy(bytes.data() + section->VirtualAddress, raw.data() + section->PointerToRawData, count);
        }
        const auto headers = std::min<size_t>(nt->OptionalHeader.SizeOfHeaders, raw.size());
        std::memcpy(bytes.data(), raw.data(), headers);
        return true;
    }
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const std::filesystem::path gameRefs = argc > 1 ? argv[1] : std::filesystem::path("..\\game_refs");

    for (const auto& test : kCases)
    {
        const auto path = gameRefs / test.image;
        std::vector<uint8_t> bytes;
        size_t size = 0;
        if (!Load(path, bytes, size))
        {
            std::printf("SKIP %s: cannot read %s\n", test.image, path.string().c_str());
            continue;
        }

        Game::Get().AttachImage(reinterpret_cast<uintptr_t>(bytes.data()), size, path, {});
        Native::ResolveSalts();

        // The probe itself, through the layer's scan: a hit is the reason the
        // shipped values survive, so the test pins the branch as well as the
        // slots.
        const bool probe = !find_pattern("68 ? ? ? ? 68 A9 54 14 51 E8").empty();
        Check(probe == test.probe, (std::string(test.image) + ": the probe " +
            (test.probe ? "hits" : "misses")).c_str());

        const char* which = test.probe ? "shipped" : "fallback";
        for (const auto& slot : kSlots)
        {
            const uint32_t expected = test.probe ? slot.shipped : slot.fallback;
            const uint32_t actual = Native::Salt(slot.reference);
            char what[160];
            std::snprintf(what, sizeof(what), "%s: dword_%08X is the %s value", test.image,
                slot.reference, which);
            char detail[96];
            std::snprintf(detail, sizeof(detail), "0x%08X, expected 0x%08X", actual, expected);
            Check(actual == expected, what, detail);
        }
    }

    std::printf("%d check(s), %d failure(s)\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
