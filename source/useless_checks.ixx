module;

#include <common.hxx>

export module useless_checks;

import common;
import game;
import loader;

// RemoveUselessChecks, as the original handler sub_10044BD0 does it: a per-build
// list of game RVAs written unconditionally (the original verifies nothing),
// only on the builds it gated. Nineteen/two/two single writes first, then the
// build's RVA tables (2230/45/56 on 1.0.8.0, 2288 on 1.0.7.0, 2466 on 1.0.4.0).
//
// The rows are recovered data: data/useless_checks.inc, generated from
// game_refs/ZolikaPatch_original.asi.off and pinned by the SHA-256 hashes in its
// own header. This module is the consumer only.
namespace
{
    // One single write: `length` bytes of `byte` at game base + rva.
    struct UselessCheckSingle
    {
        uint32_t rva;
        uint8_t length;
        uint8_t byte;
    };

    #include "../data/useless_checks.inc"

    static_assert(std::size(kUselessBuild9Nop5) == 2230u);
    static_assert(std::size(kUselessBuild9MovAl1) == 45u);
    static_assert(std::size(kUselessBuild9XorAlAl) == 56u);
    static_assert(std::size(kUselessBuild8Nop5) == 2288u);
    static_assert(std::size(kUselessBuild5Nop5) == 2466u);
    static_assert(std::size(kUselessSingles9) == 19u);
    static_assert(std::size(kUselessSingles8) == 2u);
    static_assert(std::size(kUselessSingles5) == 2u);

    constexpr uint8_t kNop = 0x90;
    constexpr std::array<uint8_t, 5> kMovAl1 = { 0xB0, 0x01, kNop, kNop, kNop };
    constexpr std::array<uint8_t, 5> kXorAlAl = { 0x32, 0xC0, kNop, kNop, kNop };

    // The singles come first, and a NOP run uses MakeNOP while every other single
    // is a single byte store.
    void SweepSingles(uintptr_t base, std::span<const UselessCheckSingle> rows)
    {
        for (const auto& row : rows)
        {
            if (row.byte == kNop)
                injector::MakeNOP(base + row.rva, row.length, true);
            else
                injector::WriteMemory<uint8_t>(base + row.rva, row.byte, true);
        }
    }

    // One protected five-byte write per site.
    void SweepNop5(uintptr_t base, std::span<const uint32_t> rows)
    {
        constexpr std::array<uint8_t, 5> kNop5 = { kNop, kNop, kNop, kNop, kNop };
        for (const auto rva : rows)
        {
            injector::WriteMemoryRaw(base + rva, const_cast<uint8_t*>(kNop5.data()), kNop5.size(), true);
        }
    }

    // The 45/56 tables protect each byte separately: this must not collapse into
    // one five-byte write, or the write set differs.
    void SweepBytes(uintptr_t base, std::span<const uint32_t> rows, std::span<const uint8_t> bytes)
    {
        for (const auto rva : rows)
        {
            for (size_t index = 0; index < bytes.size(); ++index)
                injector::WriteMemory<uint8_t>(base + rva + index, bytes[index], true);
        }
    }

    struct BuildSweep final
    {
        Build build;
        std::span<const UselessCheckSingle> singles;
        std::span<const uint32_t> nop5;
        std::span<const uint32_t> first;
        std::span<const uint8_t> firstPattern;
        std::span<const uint32_t> second;
        std::span<const uint8_t> secondPattern;
    };

    constexpr BuildSweep kSweeps[] = {
        { Build::v1080, kUselessSingles9, kUselessBuild9Nop5, kUselessBuild9MovAl1, kMovAl1, kUselessBuild9XorAlAl, kXorAlAl },
        { Build::v1070, kUselessSingles8, kUselessBuild8Nop5, {}, {}, {}, {} },
        { Build::v1040, kUselessSingles5, kUselessBuild5Nop5, {}, {}, {}, {} },
    };
}

export namespace UselessChecks
{
    void Install()
    {
        if (!Loader::Enabled("RemoveUselessChecks"))
            return;

        const auto base = Game::Get().ImageBase();
        for (const auto& sweep : kSweeps)
        {
            if (sweep.build != Loader::Current())
                continue;

            SweepSingles(base, sweep.singles);
            SweepNop5(base, sweep.nop5);
            SweepBytes(base, sweep.first, sweep.firstPattern);
            SweepBytes(base, sweep.second, sweep.secondPattern);
            return;
        }
    }
}

// Registered like every other arm: the install tier owns the gate.
export void RegisterUselessChecks()
{
    Loader::InstallEvent() += [] { UselessChecks::Install(); };
}
