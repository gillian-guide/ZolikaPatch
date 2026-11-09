#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "zpatch/status.hpp"

namespace zpatch {

struct ModuleView final {
  const std::byte* base{};
  std::size_t size{};
};

struct Match final { const std::byte* address{}; };

// Scans a bounded memory view. Tokens are split on ASCII whitespace and each is
// converted like strtoul(token, nullptr, 16); only '?' and '??' are wildcards.
// Consequently single digits, 0x prefixes, signs and non-hex tokens all reduce
// to a low-byte value (non-hex yields 0) rather than being rejected.
Status find_pattern(ModuleView module, std::string_view signature, Match& output) noexcept;

// Finds every match of `signature` in `module` (in ascending address order). Used
// by the Nth-match installs, whose handlers locate all matches with the multi-hit
// collector 0x1005afa0. Returns the number found.
std::size_t find_all_patterns(ModuleView module, std::string_view signature,
                              std::vector<const std::byte*>& output) noexcept;

// This API only reads the supplied view. No binary-writing/patch API is exposed.
}  // namespace zpatch
