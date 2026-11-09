#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "zpatch/game_image.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// A recovered signature-relative literal store that the primary op table did not
// carry: scan for `signature`, then write `value` (`width` bytes, little-endian)
// at match + offset. Each entry was confirmed by reading the handler disassembly
// (scan call -> site computation -> VirtualProtect -> store); see
// tools/extract_literal_stores.py.
struct RecoveredLiteralStore final {
  std::string_view option;
  std::string_view signature;
  std::int32_t offset;
  std::uint32_t width;
  std::uint32_t value;
};

const RecoveredLiteralStore* recovered_literal_stores(std::size_t& count) noexcept;

// Applies one recovered literal store to a caller-owned mapped image.
Status apply_literal_store(MappedImage image, const RecoveredLiteralStore& op,
                           std::byte*& written_at) noexcept;

// Applies every recovered literal store whose option passes the recovered build
// gate for `build_ordinal`. Returns the number applied via `applied`.
Status apply_recovered_literal_stores(MappedImage image, std::uint8_t build_ordinal,
                                      std::size_t& applied) noexcept;

}  // namespace zpatch
