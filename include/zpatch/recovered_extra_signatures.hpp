#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include <cstddef>

#include "zpatch/game_image.hpp"
namespace zpatch {
// Per-option patch signatures recovered from the handlers' pattern evidence for
// the options the primary signature extractor could not see (its coverage gap,
// not a behavioural gap). Every signature here was re-checked against GTA IV
// 1.0.8.0 with the recovered matcher semantics before being committed; see
// tools/extract_extra_signatures.py.
struct ExtraSignature final {
  std::string_view option;
  std::string_view signature;      // whitespace-separated hex tokens, ? wildcards
  std::uint32_t token_count{};
  std::string_view source_function;  // function the pattern was recovered from
};
const ExtraSignature* recovered_extra_signatures(std::size_t& count) noexcept;

struct ExtraSignatureHit final {
  std::string_view option;
  std::string_view signature;
  bool found{};
  std::byte* address{};      // first match inside the supplied image
};

// Scans a caller-owned mapped image for every recovered extra signature using the
// recovered matcher semantics. This is the mod's own first step for those options
// (locate the site); the write payload for them was not recovered, so nothing is
// written. Returns the number of signatures that were found.
std::size_t scan_extra_signatures(MappedImage image,
                                 std::vector<ExtraSignatureHit>& output) noexcept;
}  // namespace zpatch
