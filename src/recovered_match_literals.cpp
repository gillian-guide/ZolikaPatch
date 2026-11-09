#include "zpatch/recovered_match_literals.hpp"
#include "zpatch/patch_catalog.hpp"
#include "zpatch/pattern.hpp"

#include <string_view>
#include <vector>

namespace zpatch {
namespace {
constexpr RecoveredMatchLiteral kMatchLiterals[] = {
#include "recovered_match_literals.inc"
};
}  // namespace

const RecoveredMatchLiteral* recovered_match_literals(std::size_t& count) noexcept {
  count = sizeof(kMatchLiterals) / sizeof(kMatchLiterals[0]);
  return kMatchLiterals;
}

Status apply_recovered_match_literals(MappedImage image, std::uint8_t build_ordinal,
                                      std::size_t& written) noexcept {
  written = 0;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  std::string_view cached_signature;
  std::vector<const std::byte*> matches;
  for (const auto& op : kMatchLiterals) {
    if (!recovered_feature_allows_build(op.option, build_ordinal)) continue;
    if (op.width == 0 || op.width > 4) continue;
    if (op.signature != cached_signature) {
      cached_signature = op.signature;
      find_all_patterns({image.base, image.size}, op.signature, matches);
    }
    const auto apply_one = [&](const std::byte* match) -> bool {
      const auto match_offset = static_cast<std::ptrdiff_t>(match - image.base);
      const auto site_offset = match_offset + static_cast<std::ptrdiff_t>(op.offset);
      if (site_offset < 0 ||
          static_cast<std::size_t>(site_offset) + op.width > image.size)
        return false;
      std::byte* site = image.base + site_offset;
      for (std::uint32_t b = 0; b < op.width; ++b)
        site[b] = static_cast<std::byte>((op.value >> (8u * b)) & 0xffu);
      ++written;
      return true;
    };
    if (op.match_index < 0) {
      for (const auto* m : matches) apply_one(m);
    } else if (static_cast<std::size_t>(op.match_index) < matches.size()) {
      apply_one(matches[static_cast<std::size_t>(op.match_index)]);
    }
  }
  return {};
}

}  // namespace zpatch
