#include "zpatch/recovered_apply.hpp"
#include "zpatch/recovered_build_gates.hpp"
#include <cstdint>

namespace zpatch {
namespace {
bool option_enabled(std::string_view enabled_options, std::string_view key) noexcept {
  std::size_t start = 0;
  while (start <= enabled_options.size()) {
    const auto end = enabled_options.find(',', start);
    const auto token = enabled_options.substr(
        start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (token == key) return true;
    if (end == std::string_view::npos) break;
    start = end + 1;
  }
  return false;
}
bool parse_replacement(std::string_view text, std::byte* out, std::size_t capacity,
                       std::size_t& written) noexcept {
  written = 0;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto space = text.find(' ', cursor);
    const auto token = text.substr(
        cursor, space == std::string_view::npos ? std::string_view::npos : space - cursor);
    if (token.size() != 2 || written >= capacity) return false;
    const auto nibble = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    };
    const int high = nibble(token[0]), low = nibble(token[1]);
    if (high < 0 || low < 0) return false;
    out[written++] = static_cast<std::byte>((high << 4) | low);
    if (space == std::string_view::npos) break;
    cursor = space + 1;
  }
  return written > 0;
}
}
Status apply_recovered_operations_for_build(ModuleView module, GameBuild build,
                                            std::string_view enabled_options,
                                            ProtectFn protect,
                                            std::vector<RecoveredApplyOutcome>& output) noexcept {
  std::vector<RecoveredApplyOutcome> all;
  const auto status = apply_recovered_operations(module, enabled_options, protect, all);
  output.clear();
  for (auto& outcome : all) {
    bool known = false;
    if (!build_allows_option(build, outcome.option, known)) continue;  // gated out
    output.push_back(outcome);
  }
  return status;
}
Status apply_recovered_operations(ModuleView module, std::string_view enabled_options,
                                  ProtectFn protect,
                                  std::vector<RecoveredApplyOutcome>& output) noexcept {
  output.clear();
  if ((!module.base && module.size) || !protect)
    return {StatusCode::invalid_input, "invalid recovered apply input"};
  std::size_t count = 0;
  const auto* ops = recovered_patch_ops(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto& op = ops[i];
    if (!option_enabled(enabled_options, op.option)) continue;
    RecoveredApplyOutcome outcome{op.option, {}, nullptr, 0};
    std::byte* site = nullptr;
    if (op.module_rva != 0) {
      if (op.module_rva + op.length > module.size) {
        outcome.status = {StatusCode::invalid_input, "rva outside module"};
        output.push_back(outcome); continue;
      }
      site = const_cast<std::byte*>(module.base) + op.module_rva;
    } else {
      Match match;
      const auto found = find_pattern(module, op.signature, match);
      if (!found) { outcome.status = found; output.push_back(outcome); continue; }
      if (op.offset < 0 &&
          static_cast<std::size_t>(-op.offset) > static_cast<std::size_t>(match.address - module.base)) {
        outcome.status = {StatusCode::invalid_input, "negative offset before module"};
        output.push_back(outcome); continue;
      }
      site = const_cast<std::byte*>(match.address) + op.offset;
    }
    std::byte replacement[64];
    std::size_t written = 0;
    if (!parse_replacement(op.replacement, replacement, sizeof(replacement), written)) {
      outcome.status = {StatusCode::invalid_input, "bad replacement text"};
      output.push_back(outcome); continue;
    }
    // The original handler writes without verifying site contents; reproduce that.
    std::uint32_t previous = 0;
    const auto protected_status = protect(site, written, 0x40u, &previous);
    if (!protected_status) { outcome.status = protected_status; output.push_back(outcome); continue; }
    for (std::size_t b = 0; b < written; ++b) site[b] = replacement[b];
    (void)protect(site, written, previous, &previous);
    outcome.address = site;
    outcome.bytes_written = written;
    output.push_back(outcome);
  }
  return {};
}
}  // namespace zpatch
