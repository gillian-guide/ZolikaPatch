#include "zpatch/menu_integration.hpp"

namespace zpatch {

MenuProbeResult probe_menu_api(bool option_enabled, bool module_present,
                               bool attempt_already_made, bool load_succeeded) noexcept {
  if (!option_enabled) return MenuProbeResult::disabled;   // flag clear -> return 0
  if (module_present) return MenuProbeResult::already_loaded;
  if (!attempt_already_made && load_succeeded) return MenuProbeResult::loaded;
  return MenuProbeResult::unavailable;
}

MenuProbeResult menu_integration_probe(MenuIntegrationState& state, bool load_succeeded) noexcept {
  if (!state.option_enabled) return MenuProbeResult::disabled;
  if (state.module_present) return MenuProbeResult::already_loaded;
  if (!state.attempt_made) {
    ++state.load_attempts;
    if (load_succeeded) { state.module_present = true; return MenuProbeResult::loaded; }
    state.attempt_made = true;      // DAT_101a843c = 1, so it is not retried
    return MenuProbeResult::unavailable;
  }
  return MenuProbeResult::unavailable;
}

}  // namespace zpatch
