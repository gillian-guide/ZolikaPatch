#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace zpatch {
// One literal write operation recovered from an option handler: the handler
// locates `signature`, then writes `replacement` at `signature + offset`.
struct RecoveredPatchOp final {
  std::string_view option;
  std::string_view signature;      // empty for module-relative (rva) operations
  std::int32_t offset;             // signed: handlers patch before the match too
  std::uint32_t length;
  std::string_view replacement;    // space-separated hex bytes
  std::uint32_t module_rva;        // nonzero for DAT_1015ad70-relative writes
  std::uint32_t handler;           // handler VA the row was recovered from
};
const RecoveredPatchOp* recovered_patch_ops(std::size_t& count) noexcept;

// Per-build attribution of a module-rva row (handler + rva). Returns false when
// the row belongs to another build's branch and must not be applied, or when the
// row has no attribution at all (never blind-write without evidence).
bool rva_build_allows(std::uint32_t handler, std::uint32_t rva,
                      std::uint8_t build) noexcept;
}  // namespace zpatch
