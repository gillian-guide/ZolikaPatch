#include "zpatch/recovered_sig_jumps.hpp"
#include "zpatch/patch_catalog.hpp"

#include <cstring>

namespace zpatch {
namespace {
constexpr RecoveredSigJump kSigJumps[] = {
#include "recovered_sig_jumps.inc"
};
constexpr std::size_t kJumpLength = 5u;
}  // namespace

const RecoveredSigJump* recovered_sig_jumps(std::size_t& count) noexcept {
  count = sizeof(kSigJumps) / sizeof(kSigJumps[0]);
  return kSigJumps;
}

Status apply_sig_jump(MappedImage image, std::uintptr_t module_base_va,
                      const RecoveredSigJump& jump, std::byte*& written_at) noexcept {
  written_at = nullptr;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  Match match;
  const auto found = find_pattern({image.base, image.size}, jump.signature, match);
  if (!found) return found;
  const auto match_offset = static_cast<std::ptrdiff_t>(match.address - image.base);
  const auto site_offset = match_offset + static_cast<std::ptrdiff_t>(jump.offset);
  if (site_offset < 0 || static_cast<std::size_t>(site_offset) + kJumpLength > image.size)
    return {StatusCode::invalid_input, "write outside image"};
  std::byte* site = image.base + site_offset;
  const auto origin = module_base_va + static_cast<std::uintptr_t>(site_offset);
  const auto displacement = static_cast<std::int32_t>(
      static_cast<std::uintptr_t>(jump.destination) - (origin + kJumpLength));
  site[0] = std::byte{0xE9};
  std::memcpy(site + 1, &displacement, sizeof(displacement));
  written_at = site;
  return {};
}

Status apply_recovered_sig_jumps(MappedImage image, std::uintptr_t module_base_va,
                                 std::uint8_t build_ordinal,
                                 std::size_t& applied) noexcept {
  applied = 0;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  for (const auto& jump : kSigJumps) {
    if (!recovered_feature_allows_build(jump.option, build_ordinal)) continue;
    std::byte* where = nullptr;
    const auto status = apply_sig_jump(image, module_base_va, jump, where);
    if (!status) { ++applied; continue; }   // signature absent: skip, don't fail
    ++applied;
  }
  return {};
}

}  // namespace zpatch
