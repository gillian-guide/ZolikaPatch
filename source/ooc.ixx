module;

#include <common.hxx>

// Declares the /GS cookie the transcribed wrapper's prologue reads and the check
// its epilogue calls (`__security_cookie`, `__security_check_cookie` in
// vcruntime.h), the `va_list` the rebuilt formatter takes, and the CRT calls the
// decoder and the archive builder make.
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

export module ooc;

import bodies;
import common;
import loader;
import patch;

// OutOfCommissionFix, the one key whose payload is data rather than code: the
// original embeds the compressed `finale2.sco` script resource in its own image,
// decodes it at run time, registers the decoded table as a fake pak entry, wraps
// it in an in-memory archive and mounts that archive through the game's own
// device register. The arm is then two writes: an `E9` that widens the game's
// script-resource magic check to accept the archive's `0x86723DFF`, and an `E8`
// that sends the game's device lookup through the mounting wrapper first.
//
// Recovered VAs (IDA `ZolikaPatch.asi.i64`):
//
//   0x1004C3F0  the key's handler: scan `A9 39 44 24 18`, publish match+0x11 as
//               the branch body's resume and `E9` the match's +7; scan
//               `E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56`, publish the target
//               of the call at match-0x33 (the game's device register) and the
//               original target of the call at the match, then retarget that call
//               to the wrapper. Compiler output (EH4 frame, /GS, std::string
//               temporaries, the string builder's digit loops), so it is rebuilt
//               below rather than transcribed and has no proof row.
//   0x1004B150  the decoder's opcode step.      rebuilt, no proof row
//   0x1004B570  the decode driver + Adler-32.   rebuilt, no proof row
//   0x1004B750  the init: decode, register, build the archive, publish the size.
//   0x1004ACE0  the in-memory archive builder.  rebuilt, no proof row
//   0x1004C350  the wrapper the second `E8` is retargeted to.  transcribed, proved
//   0x1004C3C0  the branch body the `E9` jumps to.             transcribed, proved
//
// Every rebuilt piece is compiler output for a plain C function with no register
// contract: the decoder is a switch over the opcode byte, the archive builder is
// a memset plus a sequence of memcpys, and the handler is the original's own
// loader-shaped scan/publish/write code. They are rebuilt in C++ with the
// original's arithmetic, bounds and call order, so their behaviour is the same
// and there is nothing instruction-level to compare.

// The recovered resource, verbatim. source/ooc_finale2.inc carries the pinned
// image SHA-256, the source VA and length, the extraction recipe and the decoded
// table's own hashes.
static const uint8_t kOocResource[] = {
#include "ooc_finale2.inc"
};
static_assert(sizeof(kOocResource) == 163796,
    "the recovered resource is the original's 163,796 bytes at 0x100B6798");

// The original's own state, in C linkage because the transcribed bodies name the
// slots directly (the original keeps them in its .bss beside its bodies).
extern "C"
{
    uint32_t zpatch_ooc_resume = 0;    // dword_10158034: the branch body's resume
    uint32_t zpatch_ooc_mount = 0;     // dword_10159BF8: the game's device register
    uint32_t zpatch_ooc_original = 0;  // dword_10159C04: the retargeted call's target
    uint32_t zpatch_ooc_state = 0;     // dword_10159BFC: the archive's byte count
}

// The two halves of the mount string the wrapper formats: "me" + "mor" + "y" +
// ':' + '$' + <archive> + ',' + <bytes> + ',' + '0' + ':' + "" is
// "memory:$<archive>,<bytes>,0:", which is the name the game's device register
// takes for a block that already sits in memory. The format and its one string
// argument are the original's own text (0x1012EB58 and 0x1012EB0C).
extern "C"
{
    const char zpatch_ooc_mount_format[] = "me%sy%c%c%p,%d,%c%c%s";
    const char zpatch_ooc_mount_prefix[] = "mor";
}

// The bounded formatter's entry, published by source/stability.ixx (sub_10001280).
extern "C"
{
    extern uint32_t zpatch_format_entry;
}

namespace
{
    // sub_1004B750's malloc argument and sub_1004B570's output bound
    // (dword_10196374 = begin + 0x2BD50): the decoded table's exact size.
    constexpr size_t kOocTableSize = 0x2BD50;  // 179,536
    // The archive's granularity: the payload starts at 0x800 and every entry is
    // padded to it (sub_1004ACE0, sub_1004ABB0).
    constexpr size_t kOocPageSize = 0x800;
    // The name the one fake-pak entry carries, and the archive's own magic -- the
    // value the branch body compares the script resource against.
    constexpr char kOocName[] = "finale2.sco";
    constexpr uint32_t kOocArchiveMagic = 0x86723DFFu;

    // The original's three output globals as one record: dword_101A842C (begin),
    // dword_101A8430 (cursor) and dword_10196374 (end). The source's lower bound,
    // dword_101887D4, is the resource's base - 16 and is constant-true against a
    // stream that starts at the base, so there is no source test to reproduce.
    struct OocStream
    {
        uint8_t* begin;
        uint8_t* cursor;
        uint8_t* end;
    };

    // One literal run of sub_1004B150. A run that would end past the output's end
    // advances the cursor by its length without copying, which is the original's
    // fall-through after its bound test (`dword_101A8430 = v29`).
    void OocLiteral(OocStream& stream, size_t from, size_t count)
    {
        const auto cursor = reinterpret_cast<uintptr_t>(stream.cursor);
        if (cursor + count > reinterpret_cast<uintptr_t>(stream.end))
        {
            stream.cursor = reinterpret_cast<uint8_t*>(cursor + count);
            return;
        }
        std::memcpy(stream.cursor, kOocResource + from, count);
        stream.cursor += count;
    }

    // One back-reference of sub_1004B150. The copy is byte by byte because the
    // original's `do { *out++ = *in++ } while (--count)` lets an overlapping run
    // propagate; a run whose source would start before the buffer sets the cursor
    // to `end + 1`, the original's `dword_10196374 + 1`, which no later test can
    // satisfy.
    void OocBackReference(OocStream& stream, size_t distance, size_t count)
    {
        const auto cursor = reinterpret_cast<uintptr_t>(stream.cursor);
        if (cursor + count > reinterpret_cast<uintptr_t>(stream.end))
        {
            stream.cursor = reinterpret_cast<uint8_t*>(cursor + count);
            return;
        }
        const auto from = cursor - distance;
        if (from < reinterpret_cast<uintptr_t>(stream.begin))
        {
            stream.cursor = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(stream.end) + 1);
            return;
        }
        const auto* in = reinterpret_cast<const uint8_t*>(from);
        for (size_t index = 0; index < count; ++index)
            stream.cursor[index] = in[index];
        stream.cursor += count;
    }

    // sub_1004B150, one opcode: the four literal forms, the five back-reference
    // forms and the no-progress opcode that stops the driver. Returns the next
    // opcode's offset; an opcode that does not advance is the stop signal. The
    // resource is read unguarded, exactly as the original reads its own .rdata --
    // what keeps `pc` inside it is the pinned resource's own `05 FA` terminator and
    // its exact output length, which the driver checks and the self-check proves.
    size_t OocStep(OocStream& stream, size_t pc)
    {
        const uint8_t op = kOocResource[pc];

        if (op < 0x20)
        {
            if (op >= 0x18)  // 3-byte distance biased by 1572863, 1-byte length
            {
                const size_t length = kOocResource[pc + 3] + 1u;
                const size_t distance = kOocResource[pc + 2] + 256u * kOocResource[pc + 1] +
                                        65536u * op - 1572863u;
                OocBackReference(stream, distance, length);
                return pc + 4;
            }
            if (op >= 0x10)  // 3-byte distance biased by 0xFFFFF, 2-byte length
            {
                const size_t length = (kOocResource[pc + 3] << 8) + kOocResource[pc + 4] + 1u;
                const size_t distance = kOocResource[pc + 2] + 256u * kOocResource[pc + 1] +
                                        65536u * op - 0xFFFFFu;
                OocBackReference(stream, distance, length);
                return pc + 5;
            }
            if (op >= 0x08)  // literal: 1-byte count + 2047
            {
                const size_t count = 256u * op + kOocResource[pc + 1] - 2047u;
                OocLiteral(stream, pc + 2, count);
                return pc + 2 + count;
            }
            switch (op)
            {
            case 7:  // literal: 2-byte count + 1
            {
                const size_t count = 256u * kOocResource[pc + 1] + kOocResource[pc + 2] + 1u;
                OocLiteral(stream, pc + 3, count);
                return pc + 3 + count;
            }
            case 6:  // 3-byte distance + 1, 1-byte length
            {
                const size_t length = kOocResource[pc + 4] + 1u;
                const size_t distance = kOocResource[pc + 3] + 256u * kOocResource[pc + 2] +
                                        65536u * kOocResource[pc + 1] + 1u;
                OocBackReference(stream, distance, length);
                return pc + 5;
            }
            case 4:  // 3-byte distance + 1, 2-byte length
            {
                const size_t length = (kOocResource[pc + 4] << 8) + kOocResource[pc + 5] + 1u;
                const size_t distance = kOocResource[pc + 3] + 256u * kOocResource[pc + 2] +
                                        65536u * kOocResource[pc + 1] + 1u;
                OocBackReference(stream, distance, length);
                return pc + 6;
            }
            default:  // 0, 1, 2, 3, 5: no progress, which is the driver's stop
                return pc;
            }
        }

        if (op < 0x80)
        {
            if (op >= 0x40)  // 2-byte distance biased by 0x3FFF, 1-byte length
            {
                const size_t length = kOocResource[pc + 2] + 1u;
                const size_t distance = 256u * op + kOocResource[pc + 1] - 0x3FFFu;
                OocBackReference(stream, distance, length);
                return pc + 3;
            }
            // A short literal: the opcode itself is the count, biased by 31, so the
            // whole run is one byte of opcode plus count bytes.
            const size_t count = op - 31u;
            OocLiteral(stream, pc + 1, count);
            return pc + count + 1;
        }

        // The shortest back-reference: the opcode is the length, biased by 127, and
        // the distance is one byte.
        OocBackReference(stream, kOocResource[pc + 1] + 1u, op - 127u);
        return pc + 2;
    }

    // The resource's Adler-32 trailer, computed the way the original's inlined
    // zlib routine does: `a` seeded 1, `b` seeded 0, both reduced modulo 0xFFF1
    // every 5552 bytes so neither accumulator can wrap.
    uint32_t OocAdler32(const uint8_t* data, size_t size)
    {
        constexpr uint32_t kModulus = 0xFFF1u;
        constexpr size_t kBlock = 5552;
        uint32_t a = 1u;
        uint32_t b = 0u;
        for (size_t start = 0; start < size; start += kBlock)
        {
            const size_t end = (start + kBlock < size) ? start + kBlock : size;
            for (size_t index = start; index < end; ++index)
            {
                a += data[index];
                b += a;
            }
            a %= kModulus;
            b %= kModulus;
        }
        return (b << 16) | a;
    }

    // sub_1004B570: decode until the no-progress opcode, then require the `05 FA`
    // terminator, the exact output length and the trailer. The original answers
    // 179536 on success and 0 otherwise -- and sub_1004B750 discards that answer,
    // so a resource that failed its own checksum is registered and mounted as it
    // decoded, exactly as here.
    bool OocDecode(uint8_t* table)
    {
        OocStream stream{ table, table, table + kOocTableSize };
        size_t pc = OocStep(stream, 0);
        if (pc != 0)
        {
            while (reinterpret_cast<uintptr_t>(stream.cursor) <= reinterpret_cast<uintptr_t>(stream.end))
            {
                const size_t previous = pc;
                pc = OocStep(stream, pc);
                if (pc == previous)
                    break;
            }
            // The original leaves through a second door when the cursor ran past the
            // output: that path answers 0 without ever looking at the terminator.
            if (reinterpret_cast<uintptr_t>(stream.cursor) > reinterpret_cast<uintptr_t>(stream.end))
                return false;
        }
        if (kOocResource[pc] != 0x05 || kOocResource[pc + 1] != 0xFA)
            return false;
        if (reinterpret_cast<uintptr_t>(stream.cursor) != reinterpret_cast<uintptr_t>(stream.end))
            return false;
        const uint32_t trailer = (static_cast<uint32_t>(kOocResource[pc + 2]) << 24) |
                                 (static_cast<uint32_t>(kOocResource[pc + 3]) << 16) |
                                 (static_cast<uint32_t>(kOocResource[pc + 4]) << 8) |
                                 static_cast<uint32_t>(kOocResource[pc + 5]);
        return OocAdler32(table, kOocTableSize) == trailer;
    }

    // One entry of the original's fake-pak vector (a `std::vector` at 0x1016E43C
    // with its three pointers at 0x1016E43C/40/44). The name is a std::string
    // there; the only name ever registered is the eleven-character "finale2.sco",
    // which fits its 16-byte small-string buffer, so the entry keeps that buffer
    // as a fixed array and the layout stays the 44 bytes the builder walks.
    struct OocPakEntry
    {
        char name[16];           // +0x00 the std::string's small-string buffer
        uint32_t name_size;      // +0x10
        uint32_t name_capacity;  // +0x14
        uint32_t file_size;      // +0x18
        uint32_t stored_offset;  // +0x1C
        uint32_t stored_size;    // +0x20
        uint32_t padding;        // +0x24
        uint32_t data;           // +0x28
    };
    static_assert(sizeof(OocPakEntry) == 44, "the original's fake-pak entry is 44 bytes");

    std::vector<OocPakEntry>& OocPakTable()
    {
        static std::vector<OocPakEntry> entries;
        return entries;
    }

    // The archive's 20-byte header and its 16-byte per-entry record
    // (sub_1004AB70, sub_1004AB80, sub_1004ABB0, sub_1004AB90 and the stores
    // sub_1004ACE0 makes into them).
    struct OocHeader
    {
        uint32_t magic;
        uint32_t version;
        uint32_t entries;
        uint32_t names;
        uint16_t page;
        uint16_t opcode;
    };
    static_assert(sizeof(OocHeader) == 20, "the archive header is 20 bytes");

    struct OocRecord
    {
        uint32_t stored_size;
        uint32_t stored_offset;
        uint32_t index;
        uint16_t pages;
        uint16_t padding;  // low 11 bits the pad length, bit 0x20 the offset flag
    };
    static_assert(sizeof(OocRecord) == 16, "the archive record is 16 bytes");

    // sub_1004ABB0: the pages a payload occupies, rounding up.
    uint32_t OocPageCountFor(uint32_t size)
    {
        return size / static_cast<uint32_t>(kOocPageSize) +
               (size % static_cast<uint32_t>(kOocPageSize) != 0u ? 1u : 0u);
    }

    // sub_1004B750's push_back: the name's fields, the payload's size at +0x18 and
    // the table at +0x28. The three offsets the builder recomputes stay zero.
    void OocRegister(uint8_t* table)
    {
        OocPakEntry entry{};
        std::memcpy(entry.name, kOocName, sizeof(kOocName));
        entry.name_size = static_cast<uint32_t>(sizeof(kOocName) - 1);
        entry.name_capacity = static_cast<uint32_t>(sizeof(entry.name) - 1);
        entry.file_size = static_cast<uint32_t>(kOocTableSize);
        entry.data = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(table));
        OocPakTable().push_back(entry);
    }

    // sub_1004ACE0: the in-memory archive. Header, one record per entry, the names,
    // then at 0x800 the payloads with each entry's zero padding to the next page.
    // The builder consumes the payload it was handed (the original frees each
    // entry's block inside the payload loop) and returns a malloc'd archive the
    // caller owns and never frees -- the original leaks it on every call, because
    // the init it runs from re-runs every time.
    uint8_t* OocBuildArchive(uint32_t& final_offset)
    {
        auto& entries = OocPakTable();

        // The original's classification of what to store: a payload of 16 bytes or
        // less, or one that does not start with the compressed-resource dword
        // 0x05435352 ("RSC" + 5), is stored whole at offset 0. This resource starts
        // with "scr" + 0x0E, so it takes that branch.
        for (auto& entry : entries)
        {
            if (entry.file_size <= 0x10u)
            {
                entry.stored_offset = 0;
                entry.stored_size = entry.file_size;
                continue;
            }
            const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(entry.data));
            if (words[0] == 0x05435352u)
            {
                entry.stored_offset = words[1];
                entry.stored_size = words[2];
            }
            else
            {
                entry.stored_offset = 0;
                entry.stored_size = entry.file_size;
            }
        }

        uint32_t pages = 0;
        uint32_t names = 0;
        for (const auto& entry : entries)
        {
            pages += OocPageCountFor(entry.file_size);
            names += static_cast<uint32_t>(std::strlen(entry.name) + 1);
        }
        const uint32_t total = pages * static_cast<uint32_t>(kOocPageSize) +
                               static_cast<uint32_t>(kOocPageSize);

        // No null check, like the original's: a failed allocation faults in the
        // memset below.
        auto* archive = static_cast<uint8_t*>(std::malloc(total));
        std::memset(archive, 0, total);

        uint32_t offset = 0;
        const auto append = [&](const void* source, size_t bytes)
        {
            std::memcpy(archive + offset, source, bytes);
            offset += static_cast<uint32_t>(bytes);
        };

        // The header's last two words are the original's fixed 16 and 0xE9.
        const OocHeader header{ kOocArchiveMagic, 3u, static_cast<uint32_t>(entries.size()),
                                names + 16u * static_cast<uint32_t>(entries.size()), 0x10u, 0xE9u };
        append(&header, sizeof(header));

        // The record's index field is the entry's first page, 1-based: the original
        // accumulates each entry's page count into `v9` and stores `v9 + 1`, not the
        // entry's ordinal.
        uint32_t pages_before = 0;
        for (auto& entry : entries)
        {
            OocRecord record{};
            record.stored_size = entry.stored_size;
            record.stored_offset = entry.stored_offset;
            record.index = pages_before + 1u;
            record.pages = static_cast<uint16_t>(OocPageCountFor(entry.file_size));
            const uint32_t remainder = entry.file_size % static_cast<uint32_t>(kOocPageSize);
            if (remainder != 0u)
            {
                entry.padding = static_cast<uint32_t>(kOocPageSize) - remainder;
                record.padding = static_cast<uint16_t>(entry.padding);
                if (entry.stored_offset != 0u)
                    record.padding = static_cast<uint16_t>(record.padding | 0x2000u);
            }
            append(&record, sizeof(record));
            pages_before += record.pages;
        }

        for (const auto& entry : entries)
            append(entry.name, std::strlen(entry.name) + 1);

        // The payload region starts at the first page, whatever the header and the
        // names took (the original resets its offset dword here).
        offset = static_cast<uint32_t>(kOocPageSize);
        for (auto& entry : entries)
        {
            append(reinterpret_cast<const void*>(static_cast<uintptr_t>(entry.data)), entry.file_size);
            std::free(reinterpret_cast<void*>(static_cast<uintptr_t>(entry.data)));
            entry.data = 0;
            if (entry.padding != 0u)
            {
                auto* zeros = static_cast<uint8_t*>(std::malloc(entry.padding));
                std::memset(zeros, 0, entry.padding);
                append(zeros, entry.padding);
                std::free(zeros);
            }
        }

        final_offset = offset;
        return archive;
    }

    // sub_1004B750: decode, register the entry, build the archive, publish the byte
    // count the mount string carries and return the archive. Its one-shot guard
    // dword_10159C00 is read and never written anywhere in the original, so every
    // call re-runs all of this and leaks the previous archive: kept 1:1.
    uint32_t OocInitialise()
    {
        // Unchecked, like the original: the decode writes through the block whether
        // or not the allocation succeeded.
        auto* table = static_cast<uint8_t*>(std::malloc(kOocTableSize));
        static_cast<void>(OocDecode(table));  // the original discards the answer
        OocRegister(table);
        uint32_t final_offset = 0;
        uint8_t* archive = OocBuildArchive(final_offset);
        OocPakTable().clear();  // dword_1016E440 = dword_1016E43C
        zpatch_ooc_state = final_offset;
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(archive));
    }

    // sub_10001280, recovered VA 0x10001280: the bounded formatter, transcribed once
    // in source/stability.ixx and proved there instruction for instruction
    // (docs/rebuilt.md). A module interface cannot export a naked function, so this
    // six-byte forwarder is what the wrapper's `call` names: the original's call is
    // an immediate too, and the forwarder enters the body with the stack and the
    // registers untouched.
    static __declspec(naked) void OocFormat()
    {
        __asm
        {
            jmp DWORD PTR [zpatch_format_entry]
        }
    }

    // fnv1a over a recovered block, for the self-check below only: it is the
    // fingerprint the recovered data's own header records.
    uint32_t OocFnv1a(const uint8_t* data, size_t size)
    {
        uint32_t hash = 2166136261u;
        for (size_t index = 0; index < size; ++index)
        {
            hash ^= data[index];
            hash *= 16777619u;
        }
        return hash;
    }
}

// The offline proof of the three rebuilt transforms: decode the recovered
// resource, register the table, build the archive, and compare each result with
// the fingerprint the recovered bytes must produce. Answers nullptr on success and
// a short description of the first difference otherwise.
//
// The shipped ASI never calls this. The payload path only runs when the game
// calls the wrapper at 0xB70733, which no offline tool can reach, so the decoder
// and the builder would otherwise be unexecuted code; `zp_dump --ooc-selftest`
// runs them against the pinned data instead (docs/parity.md). The two fnv1a
// values are recomputed from `game_refs/ZolikaPatch_original.asi.off` by the
// recipe in source/ooc_finale2.inc's header.
export const char* OocSelfCheck()
{
    auto* table = static_cast<uint8_t*>(std::malloc(kOocTableSize));
    if (table == nullptr)
        return "the decoded table's allocation failed";
    if (!OocDecode(table))
    {
        std::free(table);
        return "the resource did not decode: terminator, output length or Adler-32 trailer";
    }
    const uint32_t table_hash = OocFnv1a(table, kOocTableSize);
    OocRegister(table);
    uint32_t final_offset = 0;
    uint8_t* archive = OocBuildArchive(final_offset);
    OocPakTable().clear();

    const char* failure = nullptr;
    if (table_hash != 0xC883859Fu)
        failure = "the decoded table is not the recovered 179,536 bytes";
    else if (final_offset != 182272u)
        failure = "the archive is not the recovered 182,272 bytes";
    else if (OocFnv1a(archive, final_offset) != 0xB5AF7B10u)
        failure = "the archive is not the recovered layout";
    else if ((archive[16] | (archive[17] << 8)) != 0x10u ||
             (archive[18] | (archive[19] << 8)) != 0xE9u)
        failure = "the archive header's page and opcode words are not 16 and 0xE9";
    std::free(archive);
    return failure;
}

// sub_1004C3C0, recovered VA 0x1004C3C0 (7 instructions): the branch body the E9
// at game 0xA22E97 jumps to. The five bytes it replaces are `cmp [esp+30h], eax`
// plus the first byte of the `movq xmm0, [esp+30h]` that follows, so it replays
// both. If the value is the game's own script magic the flags are that compare's;
// if not, it compares against the archive's 0x86723DFF, and the game's `je` two
// instructions after the resume consumes whichever flags are left. That is what
// makes the archive's resource acceptable to the check the arm widens.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void OocBranchBody()
{
    __asm
    {
        cmp DWORD PTR [esp + 30h], eax
        movq xmm0, QWORD PTR [esp + 30h]
        jne ooc_branch_check
        jmp DWORD PTR [zpatch_ooc_resume]
    ooc_branch_check:
        mov eax, 86723DFFh
        cmp DWORD PTR [esp + 30h], eax
        jmp DWORD PTR [zpatch_ooc_resume]
    }
}

// sub_1004C350, recovered VA 0x1004C350 (41 instructions to its `ret`): the
// wrapper the E8 at game 0xB70733 is retargeted to. It runs the init, formats the
// mount string from the archive's address and byte count, registers it with the
// game's device register (the call at match-0x33 the arm published), and then
// calls the function the site's E8 named before the retarget. The /GS frame is the
// original's: the same cookie, the same 0x44-byte buffer, the same 0x38-byte
// cleanup after the two calls.
//
// Naked and internal: an external-linkage naked function is written to the module
// and every importer fails (C2488).
static __declspec(naked) void OocWrapBody()
{
    __asm
    {
        push ebp
        mov ebp, esp
        sub esp, 44h
        mov eax, DWORD PTR [__security_cookie]
        xor eax, ebp
        mov DWORD PTR [ebp - 4], eax
        push esi
        mov esi, DWORD PTR [ebp + 8]
        call OocInitialise
        test eax, eax
        je ooc_wrap_original
        push 0
        push 3Ah
        push 30h
        push DWORD PTR [zpatch_ooc_state]
        push eax
        push 24h
        push 3Ah
        push OFFSET zpatch_ooc_mount_prefix
        push OFFSET zpatch_ooc_mount_format
        lea eax, [ebp - 44h]
        push 40h
        push eax
        call OocFormat
        push -1
        lea eax, [ebp - 44h]
        push 1
        push eax
        call DWORD PTR [zpatch_ooc_mount]
        add esp, 38h
    ooc_wrap_original:
        push DWORD PTR [ebp + 0Ch]
        push esi
        call DWORD PTR [zpatch_ooc_original]
        mov ecx, DWORD PTR [ebp - 4]
        add esp, 8
        xor ecx, ebp
        pop esi
        call __security_check_cookie
        mov esp, ebp
        pop ebp
        ret
    }
}

namespace
{
    // The two anchors, as the original handler's own scan strings name them. The
    // first is five bytes starting inside the `mov eax, <magic>` that precedes the
    // resource compare; the second is the device-lookup call the wrapper replaces.
    constexpr std::string_view kOocBranchSignature = "A9 39 44 24 18";
    constexpr std::string_view kOocWrapperSignature =
        "E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56";

    // sub_10001670: the target a `call` at `site` names (`site + 5 + disp32`).
    uint32_t CallTarget(const std::byte* site)
    {
        int32_t displacement{};
        std::memcpy(&displacement, site + 1, sizeof(displacement));
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site) + 5 + displacement);
    }

    // The publish tier reads what the install tier overwrites: the resume is two
    // instructions past the five bytes the E9 covers, and the wrapper replays the
    // call at the site, so its original target has to be read before the retarget.
    void PublishOutOfCommissionFix()
    {
        if (!Loader::Enabled("OutOfCommissionFix"))
            return;

        auto pattern = find_pattern(kOocBranchSignature);
        if (!pattern.empty())
        {
            zpatch_ooc_resume = static_cast<uint32_t>(
                reinterpret_cast<uintptr_t>(pattern.get_first<std::byte>(0)) + 0x11);
        }

        pattern = find_pattern(kOocWrapperSignature);
        if (!pattern.empty())
        {
            const auto* match = pattern.get_first<std::byte>(0);
            zpatch_ooc_mount = CallTarget(match - 0x33);
            zpatch_ooc_original = CallTarget(match);
        }
    }

    // The install tier: the E9 at the first anchor's +7 and the E8 retarget at the
    // second anchor, which is the original handler's write set on every build its
    // scan hits.
    void InstallOutOfCommissionFix()
    {
        if (!Loader::Enabled("OutOfCommissionFix"))
            return;

        auto pattern = find_pattern(kOocBranchSignature);
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&OocBranchBody), 0x1004C3C0u);
            injector::MakeJMP(pattern.get_first(7), reinterpret_cast<void*>(&OocBranchBody), true);
        }

        pattern = find_pattern(kOocWrapperSignature);
        if (!pattern.empty())
        {
            Bodies::Register(reinterpret_cast<uintptr_t>(&OocWrapBody), 0x1004C350u);
            injector::MakeCALL(pattern.get_first(0), reinterpret_cast<void*>(&OocWrapBody), true);
        }
    }
}

// Registered like every other arm: the publish tier reads the two sites the
// install tier overwrites, and the install tier owns the writes.
export void RegisterOoc()
{
    Loader::PublishEvent() += []
    {
        PublishOutOfCommissionFix();
    };

    Loader::InstallEvent() += []
    {
        InstallOutOfCommissionFix();
    };
}
