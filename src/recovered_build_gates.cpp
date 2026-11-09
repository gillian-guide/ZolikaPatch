#include "zpatch/recovered_build_gates.hpp"
namespace zpatch {
namespace {
constexpr OptionBuildGate kGates[] = {
#include "recovered_build_gates.inc"
};
bool equals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
  return true;
}
}
const OptionBuildGate* recovered_build_gates(std::size_t& count) noexcept {
  count = sizeof(kGates) / sizeof(kGates[0]);
  return kGates;
}
bool build_allows_option(GameBuild build, std::string_view option, bool& known) noexcept {
  std::size_t count = 0;
  const auto* gates = recovered_build_gates(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (!equals(gates[i].option, option)) continue;
    known = true;
    const auto ordinal = static_cast<unsigned>(build);
    return ordinal < 32u && (gates[i].build_mask & (1u << ordinal)) != 0;
  }
  known = false;
  return true;
}
}  // namespace zpatch
