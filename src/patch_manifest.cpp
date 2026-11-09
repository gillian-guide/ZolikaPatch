#include "zpatch/patch_manifest.hpp"
#include <cstring>

namespace zpatch {
namespace {
bool build_enabled(std::uint32_t mask, GameBuild build) noexcept {
  const auto index = static_cast<unsigned>(build);
  return index < 32u && (mask & (1u << index)) != 0;
}
bool option_enabled(std::string_view enabled_options, std::string_view key) noexcept {
  if (key.empty()) return true;
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
Status run_manifest(ModuleView module, GameBuild build, const PatchManifestView& manifest,
                    std::string_view enabled_options, ProtectFn protect,
                    std::vector<PatchOutcome>& output) noexcept {
  output.clear();
  if ((!module.base && module.size) || (!manifest.sites && manifest.size))
    return {StatusCode::invalid_input, "invalid manifest input"};
  for (std::size_t i = 0; i < manifest.size; ++i) {
    const auto& site = manifest.sites[i];
    PatchOutcome outcome{site.name, {}};
    if (!build_enabled(site.required_builds, build)) {
      outcome.status = {StatusCode::unsupported_platform, "build not enabled"};
      output.push_back(outcome); continue;
    }
    if (!option_enabled(enabled_options, site.options)) {
      outcome.status = {StatusCode::not_reconstructed, "option gate disabled"};
      output.push_back(outcome); continue;
    }
    Match match;
    const auto found = find_pattern(module, site.signature, match);
    if (!found) { outcome.status = found; output.push_back(outcome); continue; }
    auto* site_address = const_cast<std::byte*>(match.address) + site.signature_offset;
    const auto offset = static_cast<std::size_t>(site_address - module.base);
    outcome.status = apply_protected_patch(build, build, const_cast<std::byte*>(module.base),
                                           offset, site.expected, site.replacement, protect);
    if (outcome.status) {
      outcome.address = site_address;
      outcome.expected = site.expected;
      outcome.replacement = site.replacement;
    }
    output.push_back(outcome);
  }
  return {};
}
}  // namespace

Status apply_patch_manifest(ModuleView module, GameBuild build, const PatchManifestView& manifest,
                            std::string_view enabled_options, ProtectFn protect,
                            std::vector<PatchOutcome>& output) noexcept {
  return run_manifest(module, build, manifest, enabled_options, protect, output);
}
Status rollback_patch_outcomes(GameBuild build, ProtectFn protect,
                               std::vector<PatchOutcome>& applied) noexcept {
  if (!protect) return {StatusCode::invalid_input, "invalid rollback input"};
  for (auto it = applied.rbegin(); it != applied.rend(); ++it) {
    if (!it->address) continue;
    it->status = apply_protected_patch(build, build, it->address, 0,
                                       it->replacement, it->expected, protect);
  }
  return {};
}
}  // namespace zpatch
