#include "zpatch/patch.hpp"

#include <cstring>
#include <limits>
#include <cstdint>

namespace zpatch {
namespace {
Status validate(MutableMemoryView target, std::size_t offset,
                ByteView current, ByteView desired) noexcept {
  if ((!target.base && target.size) || (!current.data && current.size) ||
      (!desired.data && desired.size) || current.size == 0 ||
      current.size != desired.size)
    return {StatusCode::invalid_input, "invalid guarded patch input"};
  if (offset > target.size || current.size > target.size - offset)
    return {StatusCode::invalid_input, "guarded patch outside target"};
  if (std::memcmp(target.base + offset, current.data, current.size) != 0)
    return {StatusCode::evidence_mismatch, "expected bytes do not match"};
  return {};
}
Status replace(MutableMemoryView target, std::size_t offset,
               ByteView current, ByteView desired) noexcept {
  const auto status = validate(target, offset, current, desired);
  if (!status) return status;
  std::memcpy(target.base + offset, desired.data, desired.size);
  return {};
}
}  // namespace
Status protected_write(std::byte* address, const std::byte* value, std::size_t size,
                       ProtectFn protect) noexcept {
  if (!address || !value || !protect || (size != 1 && size != 2 && size != 4))
    return {StatusCode::invalid_input, "invalid protected write input"};
  std::uint32_t previous = 0;
  auto status = protect(address, size, 0x40u, &previous);
  if (!status) return status;
  std::memcpy(address, value, size);
  // Restore is best-effort; the store already happened.
  (void)protect(address, size, previous, &previous);
  return {};
}
Status fill_nop(MutableMemoryView target, std::size_t offset,
                std::size_t length) noexcept {
  if ((!target.base && target.size) || length == 0)
    return {StatusCode::invalid_input, "invalid NOP fill input"};
  if (offset > target.size || length > target.size - offset)
    return {StatusCode::invalid_input, "NOP fill outside target"};
  std::memset(target.base + offset, 0x90, length);
  return {};
}
Status install_relative_jump(MutableMemoryView target, std::size_t offset,
                             const std::byte* destination) noexcept {
  if ((!target.base && target.size) || !destination)
    return {StatusCode::invalid_input, "invalid jump patch input"};
  constexpr std::size_t kInstructionSize = 5;
  if (offset > target.size || kInstructionSize > target.size - offset)
    return {StatusCode::invalid_input, "jump patch outside target"};
  const auto site = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(target.base + offset));
  const auto destination_address = static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(destination));
  // Match the observed 32-bit encoding: dest - (site + 5), truncated to 32 bits.
  const auto displacement = destination_address - (site + static_cast<std::int64_t>(kInstructionSize));
  target.base[offset] = std::byte{0xe9};
  const auto value = static_cast<std::uint32_t>(static_cast<std::int32_t>(displacement));
  for (int i = 0; i < 4; ++i)
    target.base[offset + 1 + static_cast<std::size_t>(i)] =
        static_cast<std::byte>((value >> (8 * i)) & 0xffu);
  return {};
}
Status apply_protected_patch(GameBuild actual_build, GameBuild required_build,
                             std::byte* target, std::size_t offset,
                             ByteView expected, ByteView replacement,
                             ProtectFn protect) noexcept {
  if (actual_build == GameBuild::unknown || actual_build != required_build)
    return {StatusCode::evidence_mismatch, "game build does not match"};
  if (!target || !protect || (!expected.data && expected.size) ||
      (!replacement.data && replacement.size))
    return {StatusCode::invalid_input, "invalid protected patch input"};
  if (expected.size == 0 || expected.size != replacement.size)
    return {StatusCode::invalid_input, "invalid protected patch input"};
  if (std::memcmp(target + offset, expected.data, expected.size) != 0)
    return {StatusCode::evidence_mismatch, "expected bytes do not match"};
  for (std::size_t i = 0; i < replacement.size; ++i) {
    const auto status = protected_write(target + offset + i, replacement.data + i, 1, protect);
    if (!status) return status;
  }
  return {};
}
Status apply_guarded_patch(GameBuild actual_build, GameBuild required_build,
                           MutableMemoryView target, std::size_t offset,
                           ByteView expected, ByteView replacement) noexcept {
  if (actual_build == GameBuild::unknown || actual_build != required_build)
    return {StatusCode::evidence_mismatch, "game build does not match"};
  return replace(target, offset, expected, replacement);
}
Status rollback_guarded_patch(GameBuild actual_build, GameBuild required_build,
                              MutableMemoryView target, std::size_t offset,
                              ByteView expected, ByteView replacement) noexcept {
  if (actual_build == GameBuild::unknown || actual_build != required_build)
    return {StatusCode::evidence_mismatch, "game build does not match"};
  return replace(target, offset, replacement, expected);
}
}  // namespace zpatch
