#include "zpatch/recovered_indirect_writes.hpp"
#include "zpatch/patch_catalog.hpp"

#include <cstring>

namespace zpatch {
namespace {
constexpr RecoveredIndirectWrite kIndirect[] = {
#include "recovered_indirect_writes.inc"
};
}  // namespace

const RecoveredIndirectWrite* recovered_indirect_writes(std::size_t& count) noexcept {
  count = sizeof(kIndirect) / sizeof(kIndirect[0]);
  return kIndirect;
}

Status apply_indirect_write(MappedImage image, std::uintptr_t module_base_va,
                            const RecoveredIndirectWrite& op,
                            std::byte*& written_at, ProtectFn protect) noexcept {
  written_at = nullptr;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  if (op.width == 0 || op.width > 4)
    return {StatusCode::invalid_input, "unsupported width"};
  Match match;
  const auto found = find_pattern({image.base, image.size}, op.signature, match);
  if (!found) return found;
  const auto match_offset = static_cast<std::ptrdiff_t>(match.address - image.base);
  const auto pointer_offset = match_offset + static_cast<std::ptrdiff_t>(op.pointer_offset);
  if (pointer_offset < 0 ||
      static_cast<std::size_t>(pointer_offset) + sizeof(std::uint32_t) > image.size)
    return {StatusCode::invalid_input, "pointer word outside image"};
  std::uint32_t pointer = 0;
  std::memcpy(&pointer, image.base + pointer_offset, sizeof(pointer));
  // The pointer is a game virtual address; map it back into the image.
  if (pointer < module_base_va) return {StatusCode::invalid_input, "pointer below module base"};
  const auto target_offset = static_cast<std::size_t>(pointer - module_base_va);
  if (target_offset + op.width > image.size)
    return {StatusCode::invalid_input, "write target outside image"};
  std::byte* target = image.base + target_offset;
  std::byte bytes[4];
  for (std::uint32_t b = 0; b < op.width; ++b)
    bytes[b] = static_cast<std::byte>((op.value >> (8u * b)) & 0xffu);
  // The original handler VirtualProtects the target first; live code pages are
  // read-only, so an unprotected store would fault.
  std::uint32_t previous = 0;
  if (protect != nullptr) {
    const auto protect_status = protect(target, op.width, 0x40u, &previous);
    if (!protect_status) return protect_status;
  }
  std::memcpy(target, bytes, op.width);
  if (protect != nullptr) (void)protect(target, op.width, previous, &previous);
  written_at = target;
  return {};
}

Status apply_recovered_indirect_writes(MappedImage image, std::uintptr_t module_base_va,
                                       std::uint8_t build_ordinal,
                                       std::size_t& applied) noexcept {
  applied = 0;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  for (const auto& op : kIndirect) {
    if (!recovered_feature_allows_build(op.option, build_ordinal)) continue;
    std::byte* where = nullptr;
    const auto status = apply_indirect_write(image, module_base_va, op, where);
    if (!status) continue;                      // signature absent: skip this build
    ++applied;
  }
  return {};
}

}  // namespace zpatch
