#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace zpatch {
// A loader-preamble signature resolution recovered from the original ASI.
// kind 0: `signature` is a site resolver: it is matched and the result is stored
//         at `global` (only when the match is non-zero).
// kind 1: `signature` is a *build probe*, not a resolver. It matches game code
//         that embeds the hash constant 0x511454a9. When the probe MISSES, the
//         ten fallback hash salts (`value` written to `global`) are applied; when
//         it HITS (as on GTA IV 1.0.8.0, verified), the game's own hardcoded
//         salts already match and the fallback set is NOT written.
// The `value` of a kind-1 entry is a hash constant, not a pointer.
struct RecoveredPreambleEntry final {
  std::string_view signature;
  std::uint32_t global;
  std::uint32_t value;
  std::uint32_t kind;
};
const RecoveredPreambleEntry* recovered_preamble_entries(std::size_t& count) noexcept;
}  // namespace zpatch
