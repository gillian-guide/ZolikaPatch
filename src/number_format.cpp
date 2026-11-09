#include "zpatch/number_format.hpp"

namespace zpatch {

std::uint32_t divide_by_ten_magic(std::uint32_t value) noexcept {
  // `mov eax,0xcccccccd; mul ecx; shr edx,3` == unsigned / 10 for 32-bit input.
  return static_cast<std::uint32_t>(
      (static_cast<std::uint64_t>(value) * 0xcccccccdull) >> 35);
}

std::size_t decimal_digits_lsd_first(std::uint32_t value, char* out,
                                     std::size_t capacity) noexcept {
  if (!out || capacity == 0) return 0;
  std::size_t n = 0;
  do {
    if (n >= capacity) return 0;      // caller's buffer too small
    out[n++] = static_cast<char>('0' + (value % 10u));
    value = divide_by_ten_magic(value);
  } while (value != 0);
  return n;
}

std::string format_unsigned(std::uint32_t value) {
  char digits[16];
  const std::size_t n = decimal_digits_lsd_first(value, digits, sizeof(digits));
  std::string out;
  out.reserve(n);
  for (std::size_t i = n; i-- > 0;) out.push_back(digits[i]);
  return out;
}

std::string format_signed(std::int32_t value) {
  if (value < 0) {
    // Negate in unsigned space so INT32_MIN is handled exactly.
    const std::uint32_t magnitude = 0u - static_cast<std::uint32_t>(value);
    return "-" + format_unsigned(magnitude);
  }
  return format_unsigned(static_cast<std::uint32_t>(value));
}

}  // namespace zpatch
