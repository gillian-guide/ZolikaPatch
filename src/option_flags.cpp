#include "zpatch/option_flags.hpp"
#include <array>
#include <cstddef>

namespace zpatch {
namespace {
// Confirmed by bounded-control-flow mapping plus independent read-site
// cross-validation (FUN_10004c70 and the loader's own setne stores).
constexpr OptionFlagBinding kBindings[] = {
  {"MovingSteeringWheelSupport", 0x1015ad40u, true},
  {"ShakingEngineBlock",         0x1015ad41u, true},
  {"MovingPlaneParts",           0x10159c17u, true},
  {"AddOptionsInMenu",           0x10158038u, true},
  {"DebugPrintSpawns",           0x10159c15u, true},
  {"Patch4RemoveModChecks",      0x10159c14u, true},
  {"RemoveGFWLUpdatePopup",      0x10159b87u, true},
  {"DoNotPauseOnMinimize",       0x1015ad43u, true},
  {"RestoreDeathMusic",          0x10159b86u, true},
  {"DrivebyFOVFix",              0x1015bdf4u, true},
};
std::size_t slot_for(std::uint32_t global_address) noexcept {
  // Exact lookup over the recovered bindings: no hashing, so no collisions.
  // Returns kSlotCount when the address is not a known binding.
  std::size_t count = 0;
  const auto* bindings = confirmed_option_flags(count);
  for (std::size_t i = 0; i < count; ++i)
    if (bindings[i].global_address == global_address) return i;
  return kSlotCount;
}
}
const OptionFlagBinding* confirmed_option_flags(std::size_t& count) noexcept {
  count = sizeof(kBindings) / sizeof(kBindings[0]);
  return kBindings;
}
Status apply_option_flags(const ConfigDocument& config, OptionFlagState& state) noexcept {
  OptionFlagState next;
  std::size_t count = 0;
  const auto* bindings = confirmed_option_flags(count);
  for (std::size_t i = 0; i < count; ++i) {
    // The loader arms parse the (possibly default-substituted) value and set the
    // flag only when it is exactly 1; an absent key uses the arm default.
    next.flags[i] = option_enabled(config, bindings[i].ini_key) ? 1 : 0;
    if (config.find(kOptionsSection, bindings[i].ini_key)) ++next.bindings_applied;
  }
  state = next;
  return {};
}
bool option_flag_enabled(const OptionFlagState& state, std::uint32_t global_address) noexcept {
  const auto slot = slot_for(global_address);
  return slot < kSlotCount && state.flags[slot] != 0;
}
}  // namespace zpatch
