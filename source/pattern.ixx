module;

#include <common.hxx>

export module pattern;

import common;
import patch;

// Signature matching over the game image, in the shape the arms use:
//
//     if (auto pattern = Pattern("74 2B 57 E8 ?? ?? ?? ?? 8B BE A8 0E 00 00"); pattern.size() > 0)
//         injector::WriteMemory<uint8_t>(MatchAddress(pattern, 0), 0xEB, true);
//
// The recovered text goes in verbatim; `patch.ixx` owns the scan itself (image
// range, variadic build variants, recovered `??` normalisation) so there is one
// implementation of it in the tree.
export hook::pattern Pattern(std::string_view recovered)
{
    return find_pattern(recovered);
}

// The address of a site relative to the first match, which is how the recovered
// rows describe their anchors. A miss is the caller's to check: an arm tests
// `pattern.size()` before it addresses anything.
export template <typename T = uint8_t>
T* MatchAddress(hook::pattern& pattern, ptrdiff_t offset)
{
    return pattern.get(0).get<T>(offset);
}
