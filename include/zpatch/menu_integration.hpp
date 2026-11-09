#pragma once
#include <cstddef>

namespace zpatch {

// Recovered IVMenuAPI integration (FUN_10052380). ZolikaPatch does not link the
// third-party IVMenuAPI.asi; when the AddOptionsInMenu option is enabled it
// probes for the module, else loads it once, caching the attempt in a flag.
// See evidence/menu_integration.json.
enum class MenuProbeResult { disabled, already_loaded, loaded, unavailable };

// Models the recovered decision given caller-supplied OS facts, so the logic is
// testable without touching the real module list or loader.
MenuProbeResult probe_menu_api(bool option_enabled, bool module_present,
                               bool attempt_already_made, bool load_succeeded) noexcept;

// Tracks the recovered one-shot attempt flag across calls.
struct MenuIntegrationState final {
  bool option_enabled{true};      // AddOptionsInMenu default is 1
  bool module_present{};
  bool attempt_made{};
  std::size_t load_attempts{};
};

// One probe using the recovered state machine. `load` supplies the loader result
// for the attempt (ignored when it will not be made). Updates the attempt flag
// exactly as the original does.
MenuProbeResult menu_integration_probe(MenuIntegrationState& state, bool load_succeeded) noexcept;

}  // namespace zpatch
