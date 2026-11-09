#include "zpatch/recovered_literal_stores.hpp"
#include "zpatch/patch_catalog.hpp"
#include "zpatch/pattern.hpp"

#include <cstring>

namespace zpatch {
namespace {
constexpr RecoveredLiteralStore kStores[] = {
#include "recovered_literal_stores.inc"
};
}  // namespace

const RecoveredLiteralStore* recovered_literal_stores(std::size_t& count) noexcept {
  count = sizeof(kStores) / sizeof(kStores[0]);
  return kStores;
}

Status apply_literal_store(MappedImage image, const RecoveredLiteralStore& op,
                           std::byte*& written_at) noexcept {
  written_at = nullptr;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  if (op.width == 0 || op.width > 4)
    return {StatusCode::invalid_input, "unsupported width"};
  Match match;
  const auto found = find_pattern({image.base, image.size}, op.signature, match);
  if (!found) return found;
  const auto match_offset = static_cast<std::ptrdiff_t>(match.address - image.base);
  const auto site_offset = match_offset + static_cast<std::ptrdiff_t>(op.offset);
  if (site_offset < 0 || static_cast<std::size_t>(site_offset) + op.width > image.size)
    return {StatusCode::invalid_input, "write outside image"};
  std::byte* site = image.base + site_offset;
  for (std::uint32_t b = 0; b < op.width; ++b)
    site[b] = static_cast<std::byte>((op.value >> (8u * b)) & 0xffu);
  written_at = site;
  return {};
}

Status apply_recovered_literal_stores(MappedImage image, std::uint8_t build_ordinal,
                                      std::size_t& applied) noexcept {
  applied = 0;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  for (const auto& op : kStores) {
    if (!recovered_feature_allows_build(op.option, build_ordinal)) continue;
    std::byte* where = nullptr;
    const auto status = apply_literal_store(image, op, where);
    if (!status) continue;                       // signature absent this build
    ++applied;
  }
  return {};
}

}  // namespace zpatch
