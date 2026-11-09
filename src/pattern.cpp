#include "zpatch/pattern.hpp"

#include <vector>
#include <cstdint>

namespace zpatch {
namespace {
// Mirrors the observed strtoul(text, nullptr, 16) token conversion: optional
// whitespace and sign, optional 0x prefix, then hex digits; no digits yields 0.
// The result is truncated to its low byte and wildcards are exact "?"/"??".
int hex_digit(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
bool parse_hex_token(std::string_view token, int& value, bool& wildcard) noexcept {
  if (token == "?" || token == "??") { wildcard = true; value = -1; return true; }
  wildcard = false;
  std::size_t i = 0;
  while (i < token.size() && (token[i] == ' ' || token[i] == '\t' ||
         token[i] == '\r' || token[i] == '\n' || token[i] == '\f' || token[i] == '\v')) ++i;
  bool negative = false;
  if (i < token.size() && (token[i] == '+' || token[i] == '-')) { negative = token[i] == '-'; ++i; }
  if (i + 1 < token.size() && token[i] == '0' && (token[i + 1] == 'x' || token[i + 1] == 'X')) i += 2;
  std::uint32_t accumulated = 0;
  bool any = false;
  for (; i < token.size(); ++i) {
    const int digit = hex_digit(token[i]);
    if (digit < 0) break;
    any = true;
    accumulated = accumulated * 16u + static_cast<std::uint32_t>(digit);
  }
  std::uint32_t result = negative ? 0u - accumulated : accumulated;
  value = static_cast<int>(result & 0xffu);
  (void)any;
  return true;
}
}  // namespace


namespace {
Status parse_pattern(std::string_view signature, std::vector<int>& pattern) noexcept {
  pattern.clear();
  std::size_t i = 0;
  while (i < signature.size()) {
    while (i < signature.size() &&
           (signature[i] == ' ' || signature[i] == '\t' ||
            signature[i] == '\r' || signature[i] == '\n')) ++i;
    if (i == signature.size()) break;
    const auto start = i;
    while (i < signature.size() && signature[i] != ' ' &&
           signature[i] != '\t' && signature[i] != '\r' &&
           signature[i] != '\n') ++i;
    const auto token = signature.substr(start, i - start);
    int value = 0; bool wildcard = false;
    if (!parse_hex_token(token, value, wildcard))
      return {StatusCode::invalid_input, "invalid signature token"};
    pattern.push_back(wildcard ? -1 : value);
  }
  if (pattern.empty()) return {StatusCode::invalid_input, "empty signature"};
  return {};
}
}

std::size_t find_all_patterns(ModuleView module, std::string_view signature,
                              std::vector<const std::byte*>& output) noexcept {
  output.clear();
  if ((!module.base && module.size != 0) || signature.empty()) return 0;
  std::vector<int> pattern;
  if (!parse_pattern(signature, pattern)) return 0;
  if (pattern.size() > module.size) return 0;
  for (std::size_t pos = 0; pos <= module.size - pattern.size(); ++pos) {
    bool matched = true;
    for (std::size_t n = 0; n < pattern.size(); ++n) {
      if (pattern[n] >= 0 && std::to_integer<unsigned char>(module.base[pos + n]) !=
                                 static_cast<unsigned>(pattern[n])) {
        matched = false; break;
      }
    }
    if (matched) output.push_back(module.base + pos);
  }
  return output.size();
}

Status find_pattern(ModuleView module, std::string_view signature,
                    Match& output) noexcept {
  output = {};
  if ((!module.base && module.size != 0) || signature.empty())
    return {StatusCode::invalid_input, "empty or invalid scan input"};
  try {
    std::vector<int> pattern;
    if (!parse_pattern(signature, pattern))
      return {StatusCode::invalid_input, "invalid signature token"};
    if (pattern.size() > module.size) return {StatusCode::not_found, "pattern not found"};
    for (std::size_t pos = 0; pos <= module.size - pattern.size(); ++pos) {
      bool matched = true;
      for (std::size_t n = 0; n < pattern.size(); ++n) {
        if (pattern[n] >= 0 && std::to_integer<unsigned char>(module.base[pos + n]) !=
                                   static_cast<unsigned>(pattern[n])) {
          matched = false; break;
        }
      }
      if (matched) { output.address = module.base + pos; return {}; }
    }
    return {StatusCode::not_found, "pattern not found"};
  } catch (...) {
    return {StatusCode::invalid_input, "signature allocation failed"};
  }
}

}  // namespace zpatch
