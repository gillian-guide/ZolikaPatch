#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "zpatch/game_image.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// A recovered Nth-match literal write: the handler collects ALL matches of
// `signature` (multi-hit collector 0x1005afa0) and writes `value` (`width` bytes,
// little-endian) at matches[match_index] + offset, or at EVERY match when
// match_index is -1 (the handler loops over the vector). The single-hit literal
// op table cannot express this (one signature, several sites). See
// tools/extract_match_literals.py.
struct RecoveredMatchLiteral final {
  std::string_view option;
  std::string_view signature;
  std::int32_t match_index;   // >=0 specific match; -1 = every match
  std::int32_t offset;
  std::uint32_t width;
  std::uint32_t value;
};

const RecoveredMatchLiteral* recovered_match_literals(std::size_t& count) noexcept;

// Applies every recovered Nth-match literal whose option passes the recovered
// build gate for `build_ordinal`. Returns the number of written sites via
// `written`.
Status apply_recovered_match_literals(MappedImage image, std::uint8_t build_ordinal,
                                      std::size_t& written) noexcept;

}  // namespace zpatch
