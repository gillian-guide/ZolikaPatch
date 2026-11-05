module;

#include <common.hxx>

export module pattern;

import common;
import game;

// Signature matching over the game image, in the shape the arms use:
//
//     if (auto pattern = Pattern("74 2B 57 E8 ?? ?? ?? ?? 8B BE A8 0E 00 00"); pattern.size() > 0)
//         injector::WriteMemory<uint8_t>(MatchAddress(pattern, 0), 0xEB, true);
//
// The recovered strings are written the way IDA prints them (`??` wildcards) and
// the scanner wants one `?` per wildcard byte, so the normalisation lives here
// and nowhere else: an arm keeps the recovered text verbatim.
namespace
{
    std::string Normalise(std::string_view recovered)
    {
        std::string result;
        size_t offset = 0;
        while (offset < recovered.size())
        {
            while (offset < recovered.size() && std::isspace(static_cast<unsigned char>(recovered[offset])))
                ++offset;

            const auto start = offset;
            while (offset < recovered.size() && !std::isspace(static_cast<unsigned char>(recovered[offset])))
                ++offset;
            if (start == offset)
                break;

            if (!result.empty())
                result.push_back(' ');

            const auto token = recovered.substr(start, offset - start);
            if (token.find_first_not_of('?') == std::string_view::npos)
            {
                result.push_back('?');
                continue;
            }
            for (const auto character : token)
                result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
        }
        return result;
    }
}

// The matches of one recovered signature inside the game image. The image is the
// load context's, so the caller passes only the signature.
export hook::pattern Pattern(std::string_view recovered)
{
    const auto [begin, end] = ImageScanRange();
    return hook::pattern(begin, end, Normalise(recovered));
}

// The address of a site relative to the first match, which is how the recovered
// rows describe their anchors. A miss is the caller's to check: the arms test
// `pattern.size()` before they address anything.
export template <typename T = uint8_t>
T* MatchAddress(hook::pattern& pattern, ptrdiff_t offset)
{
    return pattern.get(0).get<T>(offset);
}
