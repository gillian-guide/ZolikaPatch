#include "zpatch/recovered_rva_jumps.hpp"
#include "zpatch/patch_catalog.hpp"
#include "zpatch/recovered_ops.hpp"

#include <cstring>

namespace zpatch {
namespace {
constexpr RecoveredRvaJump kRvaJumps[] = {
#include "recovered_rva_jumps.inc"
};
constexpr std::size_t kJumpLength = 5u;
}  // namespace

const RecoveredRvaJump* recovered_rva_jumps(std::size_t& count) noexcept {
  count = sizeof(kRvaJumps) / sizeof(kRvaJumps[0]);
  return kRvaJumps;
}

Status apply_rva_jump(MappedImage image, std::uintptr_t module_base_va,
                      const RecoveredRvaJump& jump, std::byte*& written_at,
                      ProtectFn protect) noexcept {
  written_at = nullptr;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  if (jump.module_rva + kJumpLength > image.size)
    return {StatusCode::invalid_input, "rva outside image"};
  std::byte* site = image.base + jump.module_rva;
  // The handler derives the displacement from the live game module base, not from
  // the buffer address, so the arithmetic uses module_base_va.
  const auto origin = module_base_va + jump.module_rva;
  const auto displacement = static_cast<std::int32_t>(
      static_cast<std::uintptr_t>(jump.destination) - (origin + kJumpLength));
  std::byte bytes[kJumpLength];
  bytes[0] = static_cast<std::byte>(jump.opcode);
  std::memcpy(bytes + 1, &displacement, sizeof(displacement));
  // The original handler VirtualProtects the site first; live code pages are
  // read-only, so an unprotected store would fault.
  std::uint32_t previous = 0;
  if (protect != nullptr) {
    const auto protect_status = protect(site, kJumpLength, 0x40u, &previous);
    if (!protect_status) return protect_status;
  }
  std::memcpy(site, bytes, kJumpLength);
  if (protect != nullptr) (void)protect(site, kJumpLength, previous, &previous);
  written_at = site;
  return {};
}

Status apply_recovered_rva_jumps(MappedImage image, std::uintptr_t module_base_va,
                                 std::uint8_t build_ordinal,
                                 std::size_t& applied) noexcept {
  applied = 0;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  for (const auto& jump : kRvaJumps) {
    // Gate exactly as the live apply path does: the feature mask plus the
    // per-(handler, rva) build attribution (a row from another build's branch
    // would patch the wrong code at that RVA).
    if (!recovered_feature_allows_build(jump.option, build_ordinal)) continue;
    if (!rva_build_allows(jump.handler, jump.module_rva, build_ordinal)) continue;
    std::byte* where = nullptr;
    const auto status = apply_rva_jump(image, module_base_va, jump, where);
    if (!status) return status;
    ++applied;
  }
  return {};
}

}  // namespace zpatch
