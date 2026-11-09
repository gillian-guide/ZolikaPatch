#include "zpatch/recovered_match_jumps.hpp"
#include "zpatch/patch_catalog.hpp"
#include "zpatch/pattern.hpp"

#include <cstring>
#include <vector>

namespace zpatch {
namespace {
constexpr RecoveredMatchJump kMatchJumps[] = {
#include "recovered_match_jumps.inc"
};
constexpr std::size_t kJumpLength = 5u;
}  // namespace

const RecoveredMatchJump* recovered_match_jumps(std::size_t& count) noexcept {
  count = sizeof(kMatchJumps) / sizeof(kMatchJumps[0]);
  return kMatchJumps;
}

Status apply_recovered_match_jumps(MappedImage image, std::uintptr_t module_base_va,
                                   std::uint8_t build_ordinal,
                                   std::size_t& applied) noexcept {
  applied = 0;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  // Cache the match list per signature so a shared pattern is scanned once.
  std::string_view cached_signature;
  std::vector<const std::byte*> matches;
  for (const auto& op : kMatchJumps) {
    if (!recovered_feature_allows_build(op.option, build_ordinal)) continue;
    if (op.signature != cached_signature) {
      cached_signature = op.signature;
      find_all_patterns({image.base, image.size}, op.signature, matches);
    }
    if (op.match_index >= matches.size()) continue;   // not all matches present
    const auto match_offset =
        static_cast<std::ptrdiff_t>(matches[op.match_index] - image.base);
    const auto site_offset = match_offset + static_cast<std::ptrdiff_t>(op.offset);
    if (site_offset < 0 ||
        static_cast<std::size_t>(site_offset) + kJumpLength > image.size)
      continue;
    std::byte* site = image.base + site_offset;
    const auto origin = module_base_va + static_cast<std::uintptr_t>(site_offset);
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::uintptr_t>(op.destination) - (origin + kJumpLength));
    site[0] = static_cast<std::byte>(op.opcode & 0xffu);
    std::memcpy(site + 1, &displacement, sizeof(displacement));
    ++applied;
  }
  return {};
}

}  // namespace zpatch
