module;

#include <common.hxx>

export module bodies;

import common;

// Transcribed hook bodies and their recovered addresses.
//
// The shipped ASI jumps to the body's own code; the offline tools cannot, because
// the body sits in a 32-bit module and the dump runs in a tool of its own. So a
// branch that points at a registered body is rewritten, before the write set is
// compared, to the displacement the original ASI has at that site: the body's
// recovered VA. Without this the dump's bytes would depend on where the tool
// happened to place the body, and could never match a baseline.
export namespace Bodies
{
    struct Entry
    {
        uintptr_t address;   // where the body lives in this process
        uint32_t reference;  // the VA it has in the original ASI
    };

    inline std::vector<Entry>& Registry()
    {
        static std::vector<Entry> entries;
        return entries;
    }

    inline void Register(uintptr_t address, uint32_t reference)
    {
        Registry().push_back({ address, reference });
    }

    inline std::span<const Entry> All()
    {
        return Registry();
    }

    inline bool ReferenceFor(uintptr_t address, uint32_t& reference)
    {
        for (const auto& entry : Registry())
        {
            if (entry.address == address)
            {
                reference = entry.reference;
                return true;
            }
        }
        return false;
    }

    // Rewrites every E8/E9 the pass wrote whose target is a registered body. A
    // branch the pass did not write is left alone (its byte still equals the
    // pristine one).
    // `hostBase` is where the image buffer sits in this process -- what the
    // injector wrote its displacement against. `imageBase` is the image's own
    // preferred base, which is the coordinate system a baseline is expressed in.
    // Matching the target needs the first; emitting the displacement needs the
    // second, and using one for both is off by hostBase - imageBase.
    inline size_t Canonicalise(uint8_t* bytes, const uint8_t* pristine, size_t size,
                               uintptr_t hostBase, uintptr_t imageBase)
    {
        size_t rewritten = 0;
        for (size_t offset = 0; offset + 5 <= size; ++offset)
        {
            const uint8_t opcode = bytes[offset];
            if ((opcode != 0xE8 && opcode != 0xE9) || pristine[offset] == opcode)
                continue;

            int32_t displacement{};
            std::memcpy(&displacement, bytes + offset + 1, sizeof(displacement));
            const auto site = static_cast<uintptr_t>(hostBase + offset);
            const auto target = static_cast<uintptr_t>(static_cast<intptr_t>(site + 5 + displacement));

            uint32_t reference{};
            if (!ReferenceFor(target, reference))
                continue;

            const auto imageSite = static_cast<uintptr_t>(imageBase + offset);
            const auto canonical = static_cast<int32_t>(static_cast<intptr_t>(reference) - static_cast<intptr_t>(imageSite + 5));
            std::memcpy(bytes + offset + 1, &canonical, sizeof(canonical));
            ++rewritten;
        }
        return rewritten;
    }
}
