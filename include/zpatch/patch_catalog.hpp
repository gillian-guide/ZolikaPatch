#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace zpatch {

struct FeatureEvidence final {
  std::string_view ini_key;
  std::string_view observed_default;
};

struct FeatureCatalogView final {
  const FeatureEvidence* data{};
  std::size_t size{};
};

// One recovered [Options] key, with everything the reconstruction knows about it.
//
// The catalog is evidence-derived: each row records the recovered loader handler
// address, whether per-option signatures were recovered, the recovered build-gate
// mask (== gate; bit i == ordinal i may apply; -1 means no gate was recovered and
// the option is treated as unconditional), how many signature and module-RVA
// write operations belong to it, whether its signatures were resolved against
// GTA IV 1.0.8.0, and the recovered classification.
//
// When a gate was recovered the mask is a POSITIVE gate: bit i set means the
// recovered `==` branch for ordinal i applies (a `!=`-only gate is expressed as
// its complement). When has_build_gate is false the mask is all-ones and the
// option is unconditional. Presence in this catalog still does not imply
// recovered patch behaviour.
struct RecoveredFeature final {
  std::string_view ini_key;
  std::string_view observed_default;
  std::uint32_t handler_va{};        // 0 when no handler was recovered
  bool has_signature{};
  bool has_build_gate{};
  std::uint32_t build_mask{0xFFFFFFFFu};   // meaningful only when has_build_gate
  std::uint32_t signature_ops{};
  std::uint32_t rva_ops{};
  bool resolved_on_1080{};
  std::string_view classification;   // signature-patch / flag-only / function-call-only / ...
};

struct RecoveredFeatureCatalogView final {
  const RecoveredFeature* data{};
  std::size_t size{};
};

// Catalog only: presence here does not imply recovered patch behavior.
FeatureCatalogView observed_feature_catalog() noexcept;

// The full recovered catalog: 98 [Options] keys with their recovered metadata.
RecoveredFeatureCatalogView recovered_feature_catalog() noexcept;

// Looks up `key` (case-sensitive, as in the INI). Returns nullptr when unknown.
const RecoveredFeature* find_recovered_feature(std::string_view key) noexcept;

// True when the recovered per-option gate allows `build` (ordinal i is build i).
// An unknown key, or a key with no recovered gate, returns true.
bool recovered_feature_allows_build(std::string_view key, std::uint8_t ordinal) noexcept;

}  // namespace zpatch
