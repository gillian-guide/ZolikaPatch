#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace zpatch {

// Recovered decimal formatting primitive.
//
// FUN_10024600 (75,906 bytes, 22,782 instructions) is a compiler-generated bulk
// numeric formatter: 442 inlined MSVC divide-by-10 sequences
// (`mov eax,0xcccccccd; mul; shr edx,3`) with ASCII digit constants (0x30 '0',
// 0x38 '8', 0x34 '4', 0x20 ' ') inside a 0x5bf0-byte stack frame. Only three
// string-address pushes exist, so it is not literal-driven - it converts many
// numeric values to decimal text. See
// ZolikaPatch_decompiled/checkpoints/fun_10024600/characterization.json.
//
// This exposes the underlying primitive the composition proves, as a tested
// function rather than a 76 KB reconstruction.

// Unsigned decimal, exactly the digits an inlined divide-by-10 loop produces.
std::string format_unsigned(std::uint32_t value);

// Signed decimal with a leading '-'.
std::string format_signed(std::int32_t value);

// The recovered digit loop, exposed for testing: fills `out` with the decimal
// digits of `value` in least-significant-first order and returns the count.
// This is the exact inner loop the 442 inlined copies implement.
std::size_t decimal_digits_lsd_first(std::uint32_t value, char* out,
                                     std::size_t capacity) noexcept;

// The MSVC magic-constant division the compiler inlined: value / 10.
std::uint32_t divide_by_ten_magic(std::uint32_t value) noexcept;

}  // namespace zpatch
