#include "zpatch/game_image.hpp"
#include "zpatch/pattern.hpp"
#include <cstring>

namespace zpatch {
namespace {
bool parse_hex_bytes(std::string_view text, std::byte* out, std::size_t capacity,
                     std::size_t& written) noexcept {
  written = 0;
  std::size_t cursor = 0;
  const auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  while (cursor < text.size()) {
    const auto space = text.find(' ', cursor);
    const auto token = text.substr(cursor, space == std::string_view::npos
        ? std::string_view::npos : space - cursor);
    if (token.size() != 2 || written >= capacity) return false;
    const int high = nibble(token[0]), low = nibble(token[1]);
    if (high < 0 || low < 0) return false;
    out[written++] = static_cast<std::byte>((high << 4) | low);
    if (space == std::string_view::npos) break;
    cursor = space + 1;
  }
  return written > 0;
}
}
Status restore_mapped_bytes(std::byte* site, const std::byte* original,
                            std::size_t length) noexcept {
  if (site == nullptr || original == nullptr || length == 0)
    return {StatusCode::invalid_input, "invalid restore input"};
  std::memcpy(site, original, length);
  return {};
}
Status apply_mapped_operation(MappedImage image, std::string_view signature,
                              std::int32_t offset, std::uint32_t module_rva,
                              std::string_view replacement,
                              std::byte*& written_at, std::size_t& written) noexcept {
  return apply_mapped_operation_capture(image, signature, offset, module_rva,
                                        replacement, written_at, written, nullptr, 0, nullptr);
}
Status apply_mapped_operation_capture(MappedImage image, std::string_view signature,
                                      std::int32_t offset, std::uint32_t module_rva,
                                      std::string_view replacement,
                                      std::byte*& written_at, std::size_t& written,
                                      std::byte* original_out,
                                      std::size_t original_capacity,
                                      ProtectFn protect) noexcept {
  written_at = nullptr; written = 0;
  if (!image.base || image.size == 0)
    return {StatusCode::invalid_input, "invalid mapped image"};
  std::byte bytes[64];
  std::size_t length = 0;
  if (!parse_hex_bytes(replacement, bytes, sizeof(bytes), length))
    return {StatusCode::invalid_input, "invalid replacement text"};
  std::byte* site = nullptr;
  if (module_rva != 0) {
    if (module_rva + length > image.size)
      return {StatusCode::invalid_input, "rva outside image"};
    site = image.base + module_rva;
  } else {
    Match match;
    const auto found = find_pattern({image.base, image.size}, signature, match);
    if (!found) return found;
    const auto match_offset = static_cast<std::ptrdiff_t>(match.address - image.base);
    const auto site_offset = match_offset + static_cast<std::ptrdiff_t>(offset);
    if (site_offset < 0 || static_cast<std::size_t>(site_offset) + length > image.size)
      return {StatusCode::invalid_input, "write outside image"};
    site = image.base + site_offset;
  }
  if (original_out != nullptr) {
    if (original_capacity < length)
      return {StatusCode::invalid_input, "undo buffer too small"};
    std::memcpy(original_out, site, length);   // capture the preimage first
  }
  // The original handlers VirtualProtect every site before storing; live code
  // pages are read-only, so an unprotected store would fault.
  std::uint32_t previous = 0;
  if (protect != nullptr) {
    const auto protect_status = protect(site, length, 0x40u, &previous);
    if (!protect_status) return protect_status;
  }
  std::memcpy(site, bytes, length);
  if (protect != nullptr) (void)protect(site, length, previous, &previous);
  written_at = site;
  written = length;
  return {};
}
}  // namespace zpatch
