#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "zpatch/config.hpp"
#include "zpatch/game.hpp"
#include "zpatch/game_image.hpp"
#include "zpatch/option_flags.hpp"
#include "zpatch/patch.hpp"
#include "zpatch/recovered_apply.hpp"
#include "zpatch/status.hpp"
#include "zpatch/vehicle_tables.hpp"

namespace zpatch {

// The virtual address the mapped image represents (the value GetModuleHandleA(0)
// returns for the game module). Defaults to GTAIV.exe's recorded image base, which
// is what the recovered RVA-jump arithmetic assumes.
constexpr std::uintptr_t kDefaultGameModuleBase = 0x400000u;


struct RuntimeState final {
  ConfigDocument config;
  VehicleLightTables vehicle_lights;
  OptionFlagState option_flags;
  bool initialized{};
  bool patches_applied{};
};

// Loads configuration and the recovered option flags. Performs no memory writes.
Status initialize(std::string_view config_path) noexcept;
Status initialize() noexcept;
void shutdown() noexcept;

// Logs every Options-section key (raw value, arm default when absent, enabled
// decision, build gate) and the totals through zpatch/log.hpp, so a live run
// records the exact configuration it applied.
void log_config_summary(GameBuild build) noexcept;
RuntimeState runtime_state();

// The original loader's unconditional preamble: it resolves a fixed set of game
// pointers with signature scans for every run (not behind any option) and
// rewrites its ten hash salts only when the build probe misses. The rebuilt
// loader performs the same work against its own globals, so the option bodies
// find the same state the original would have left. Returns the number of
// pointers resolved (7 on 1.0.8.0).
struct LoaderPreambleReport final {
  std::size_t resolved{};
  std::size_t missing{};
};
LoaderPreambleReport apply_loader_preamble(MappedImage image) noexcept;

// The original's menu integration (AddOptionsInMenu + IVMenuAPI.asi): when
// available, the loader also installs RestoreDeathMusic's hooks and registers
// the two runtime toggles. The rebuilt loader calls this before applying the
// recovered patches so the RestoreDeathMusic rows run in that case too.
void set_menu_integration_available(bool available) noexcept;

// The loader's effective option gate: `option_enabled` plus the original's
// cross-option rules (RestoreDeathMusic falls through to installed when the
// IVMenuAPI integration is available; the shared CarDynamicShadowFix installer
// runs the CarParts hook when CarDynamicShadowFix == 1 and CarPartsShadowFix
// != 1). Also handles the internal spelling "CarPartsShadowFix@standalone"
// (the sub_10007E40 EB write, standalone arm only). Every install table uses
// this gate so the rules apply uniformly.
bool ini_option_enabled(const ConfigDocument& config, std::string_view option) noexcept;

struct RuntimePatchReport final {
  std::size_t candidates{};        // recovered operations considered
  std::size_t applied{};           // written this call
  std::size_t not_found{};         // signature absent in the supplied image
  std::size_t gated_out{};         // option disabled or build-gated
  std::size_t jump_opcode_pending{};  // bare jump-opcode rows whose install was not recovered
  std::size_t no_destination{};       // installs skipped: no reimplemented body for the target
  std::size_t module_address_pending{};  // writes skipped: value points into the original module
  std::size_t build_mismatch{};       // rva rows skipped: recovered from another build's branch
  std::size_t suppressed{};           // rows skipped by the curated op corrections
  std::size_t anchor_missing{};       // rows skipped: the option handler's first-scan anchor is absent
  std::size_t useless_checks_applied{};  // RemoveUselessChecks' table-driven NOPs (finding N5)
  std::vector<RecoveredApplyOutcome> outcomes;
};

// One applied write, with the bytes that were overwritten so it can be undone.
struct AppliedWrite final {
  std::string_view option;
  std::byte* address{};
  std::size_t length{};
  std::byte original[64]{};        // preimage captured at write time
};

// The ordered log of writes made by the most recent apply_recovered_patches call.
// Cleared at the start of every apply, so it always describes exactly what that
// call changed. Callers copy it before mapping is invalidated.
using AppliedWriteLog = std::vector<AppliedWrite>;

// Overrides the destination of a recovered jump install. Returning 0 keeps the
// recovered original destination (evidence application); a non-zero value
// installs that address instead - a rebuilt ASI passes default_hook_resolver so
// the jumps target its own reimplemented bodies.
using RuntimeDestinationResolver = std::uintptr_t (*)(std::uint32_t reference_va, void* user);

// Applies the recovered patch operations and records the preimage of every write,
// so the same call can later be rolled back with rollback_recovered_patches.
//
// Gating is identical to apply_recovered_patches: each operation's option must be
// enabled and the recovered per-build gate must allow the build. `log` receives
// one entry per successful write, in application order. When `resolve` is given,
// jump installs target the resolved address for their reference destination
// instead of the recovered original VA.
Status apply_recovered_patches_logged(GameBuild build, MappedImage image, ProtectFn protect,
                                      RuntimePatchReport& report,
                                      AppliedWriteLog& log,
                                      std::uintptr_t game_module_base = kDefaultGameModuleBase,
                                      RuntimeDestinationResolver resolve = nullptr,
                                      void* resolver_user = nullptr) noexcept;

// Rolls back writes previously recorded in `log`. Writes are undone in reverse
// order (last applied first). Each restore goes through `protect` so page
// protection is honoured exactly as on apply. Successful entries are removed from
// `log`; entries whose restore failed are kept so the caller can retry or report.
// Returns the number of restored writes via `restored`.
Status rollback_recovered_patches(ProtectFn protect, AppliedWriteLog& log,
                                  std::size_t& restored) noexcept;


// Applies the recovered patch operations to a caller-owned mapped module image.
//
// Gating uses the RECOVERED evidence only: each operation's option must be
// enabled in the loaded configuration, and the recovered per-build gate must
// allow the build. The write itself goes through `apply_mapped_operation`, so no
// OS call, page protection change or process-memory access happens here - the
// caller is responsible for mapping, protection and the real module.
//
// `patches_applied` in the runtime state becomes true when at least one write
// succeeds. This is a real application path against supplied memory; it is NOT a
// claim that the mod works in a live GTA IV process.
Status apply_recovered_patches(GameBuild build, MappedImage image, ProtectFn protect,
                               RuntimePatchReport& report) noexcept;

}  // namespace zpatch
