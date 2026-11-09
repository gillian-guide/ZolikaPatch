#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "zpatch/game.hpp"
namespace zpatch {
// Build-mask gate recovered from an option handler's positive equality checks.
// Bit i corresponds to GameBuild ordinal i (v1060=7, v1070=8, v1080=9, ...).
struct OptionBuildGate final {
  std::string_view option;
  std::uint32_t build_mask;
};
const OptionBuildGate* recovered_build_gates(std::size_t& count) noexcept;
// WARNING (subagent-verified): recovered gates are NOT a proven enable/disable
// predicate. For at least one handler (DualVehicleHeadlights, 0x10006770) the
// build comparison selects which *trampoline target* is written while the
// signature scan runs unconditionally. Callers must treat a gate as the branch
// that was recovered for that build, not as applicability. Conversely, some
// handlers gate by `!=` (e.g. VSyncFix excludes 1..5/9/13).
//
// true when the option has a recovered branch for `build`; `known` reports
// whether any gate was recovered for the option at all.
bool build_allows_option(GameBuild build, std::string_view option, bool& known) noexcept;
}  // namespace zpatch
