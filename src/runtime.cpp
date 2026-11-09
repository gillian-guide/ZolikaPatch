#include "zpatch/runtime.hpp"
#include "zpatch/useless_checks.hpp"
#include "zpatch/recovered_build_gates.hpp"
#include "zpatch/recovered_rva_jumps.hpp"
#include "zpatch/recovered_sig_jumps.hpp"
#include "zpatch/recovered_indirect_writes.hpp"
#include "zpatch/recovered_match_jumps.hpp"
#include "zpatch/recovered_match_literals.hpp"
#include "zpatch/recovered_literal_stores.hpp"
#include "zpatch/patch_catalog.hpp"
#include "zpatch/rebased_values.hpp"
#include "zpatch/op_corrections.hpp"
#include "zpatch/log.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/rva_installs.hpp"
#include "zpatch/game.hpp"

#include <cstring>
#include <cstdlib>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace zpatch {
namespace {
std::mutex mutex;
RuntimeState state;
// True when AddOptionsInMenu is on and IVMenuAPI.asi could be loaded (the
// loader sets this before applying patches; see set_menu_integration_available).
bool g_menu_integration_available = false;

// The original's FusionFix probe (sub_10003E20): returns true when
// GTAIV.EFLC.FusionFix.asi is loaded and its signature bytes do not match the
// known build. The checked bytes belong to the FusionFix module, not the game.
bool fusionfix_gate_closed() noexcept {
#if defined(_WIN32)
  const HMODULE module = GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi");
  if (module == nullptr) return false;
  const auto* image = reinterpret_cast<const std::byte*>(module);
  return compatibility_module_lacks_signature(image, 0x20D7u + 4u);
#else
  return false;
#endif
}

// NewFilesCompatibility's handler early-out (10051939/10051948): with GTAC.dll
// or JacksIVFixes.asi loaded it exits before any patch.
bool newfiles_conflict_modules_present() noexcept {
#if defined(_WIN32)
  return GetModuleHandleA("GTAC.dll") != nullptr ||
         GetModuleHandleA("JacksIVFixes.asi") != nullptr;
#else
  return false;
#endif
}

// Every original handler VirtualProtects its site before storing; live code
// pages are read-only, so the inline write paths protect around their stores
// exactly like the handler does (the mapped test image is writable and passes a
// counting protect that performs no OS call).
Status protected_write(ProtectFn protect, std::byte* site, const std::byte* bytes,
                       std::size_t length) noexcept {
  std::uint32_t previous = 0;
  if (protect != nullptr) {
    const auto status = protect(site, length, 0x40u, &previous);
    if (!status) return status;
  }
  std::memcpy(site, bytes, length);
  if (protect != nullptr) (void)protect(site, length, previous, &previous);
  return {};
}

// Resolves a jump-install destination through the caller's resolver.
//
// Two modes:
//  - no resolver: the recovered reference VA is installed (evidence application
//    against a mapped image of the original module);
//  - resolver present: a zero result means the reimplementation has no body for
//    that destination, so the install must be SKIPPED - never fall back to the
//    reference VA, which does not exist in the rebuilt module (installing it
//    sends the game to a dangling address).
std::uint32_t resolve_destination(RuntimeDestinationResolver resolve, void* user,
                                  std::uint32_t reference_va, bool& skip) noexcept {
  skip = false;
  if (!resolve) return reference_va;
  const std::uintptr_t resolved = resolve(reference_va, user);
  if (!resolved) {
    skip = true;
    return 0;
  }
  return static_cast<std::uint32_t>(resolved);
}

// The original loader applies an option's patches only when the INI contains the
// key with a non-zero value; absent keys are not applied at all.
// Shim bodies are withheld until each resume's register/flag dependencies are
// verified against the original body's disassembly: a wrong shim crashes the
// game during the savegame load. Both the match-jump and sig-jump installers
// consult this list.
bool shim_withheld(std::uint32_t destination_va) noexcept {
  constexpr std::uint32_t kWithheld[] = {
      0x10036ee0u,  // BlackscreenFix
      0x1003d7b0u,  // PathNodeCrashFix
      0x1003d870u,  // PlaneCrashFix
      0x10050490u,  // WantedLevelCrashFix
      0x10036e90u,  // GroupHackFix
      0x10046750u,  // SMPA60Fix
      0x10005d80u,  // CollisionDeadlockFix
  };
  for (const std::uint32_t withheld : kWithheld) {
    if (withheld == destination_va) return true;
  }
  return false;
}

bool ini_option_enabled_impl(const ConfigDocument& config, std::string_view option) noexcept {
  // The original's loader runs RestoreDeathMusic's installer when the INI key
  // is 1 *or* when the IVMenuAPI integration is available (the menu toggle then
  // drives the flag at runtime; the installer itself stays flag-gated).
  if (option == "RestoreDeathMusic" && g_menu_integration_available) return true;
  // The shared CarDynamicShadowFix installer calls sub_10006910 (the CarParts
  // hook) when CarDynamicShadowFix == 1 and CarPartsShadowFix != 1 (its own arm
  // covers CarPartsShadowFix == 1). The CarParts rows therefore apply in both
  // cases; both arm reads default to "1", so an absent key counts as
  // CarPartsShadowFix == 1 (the standalone arm fires).
  if (option == "CarPartsShadowFix") {
    if (option_enabled(config, option)) return true;
    return option_enabled(config, "CarDynamicShadowFix");
  }
  // sub_10007E40's `83 F8 04 C6 44 24 0E 00 75 0F`+8 EB write belongs to the
  // CarPartsShadowFix standalone arm only (the loader runs sub_10006910 *and*
  // sub_10007E40 for key==1); the shared CarDynamic installer calls only
  // sub_10006910. The internal spelling keeps that write out of the
  // CarDynamicShadowFix != CarPartsShadowFix case.
  if (option == "CarPartsShadowFix@standalone")
    return option_enabled(config, "CarPartsShadowFix");
  // CarDynamicShadowFix's C/D/E branch (old builds): the 6-byte NOP fill at
  // C+0x3E only runs when pattern E is missing. E exists only on 1.0.0.4, so the
  // internal spelling carries a build gate (1000/1040) instead of a runtime scan.
  if (option == "CarDynamicShadowFix@noE")
    return option_enabled(config, "CarDynamicShadowFix");
  // (The former CarDynamicShadowFix@noFCHS group was removed 2026-10-15: its
  // three "shadow-buffer reduction" rows were misattributed to sub_1000B790,
  // which is a path helper with no patch stores. Those sites belong to
  // sub_1000E870, whose restores run only when ForceCarHeadlightShadows == 1;
  // they are now ForceCarHeadlightShadows sig_writes. See HANDOFF session 15.)
  // MiscFixes: the loader's arm calls sub_10023DD0 only after ORing the
  // XLivelessAddon.asi presence into byte_10159B84 (0x10052DF3); the handler
  // itself skips every write when that flag is set (`if (!byte_10159B84)`).
  if (option == "MiscFixes" && zpatch_xliveless_present != 0u) return false;
  // SkipIntro/SkipMenu/BorderlessWindowed skip their install when the FusionFix
  // probe reports a loaded GTAIV.EFLC.FusionFix.asi with non-matching bytes
  // (0x1005563F / 0x100556D1 / 0x10055836).
  if (option == "SkipIntro" || option == "SkipMenu" || option == "BorderlessWindowed") {
    if (!option_enabled(config, option)) return false;
    return !fusionfix_gate_closed();
  }
  // NewFilesCompatibility's handler exits before any patch when GTAC.dll or
  // JacksIVFixes.asi is loaded (10051939/10051948).
  if (option == "NewFilesCompatibility") {
    if (!option_enabled(config, option)) return false;
    return !newfiles_conflict_modules_present();
  }
  // DualVehicleHeadlights: the handler installs one E9 on every build; the
  // dword_1015BE04 1..6 check only selects the destination body. The two
  // internal spellings share the INI key and are separated by their build gates.
  if (option == "DualVehicleHeadlights@old" || option == "DualVehicleHeadlights@new")
    return option_enabled(config, "DualVehicleHeadlights");
  return option_enabled(config, option);
}

std::uint32_t read_u32(const std::byte* address) noexcept {
  std::uint32_t value = 0;
  std::memcpy(&value, address, sizeof(value));
  return value;
}
}  // namespace

bool ini_option_enabled(const ConfigDocument& config, std::string_view option) noexcept {
  return ini_option_enabled_impl(config, option);
}

void set_menu_integration_available(bool available) noexcept {
  g_menu_integration_available = available;
}

Status initialize(std::string_view config_path) noexcept {
  ConfigDocument loaded;
  const auto status = load_config(config_path, loaded);
  if (!status) return status;
  VehicleLightTables tables;
  const auto table_status = load_vehicle_light_tables(loaded, tables);
  if (!table_status) return table_status;
  // The original's loader fills its two 512-slot vehicle light tables from the
  // [PopupLightAngles] / [HeadlightConeOffsets] INI sections before any option
  // runs (unconditionally). Publish the same values into the rebased arrays the
  // live bodies read.
  for (std::size_t i = 0; i < tables.popup_angles.size(); ++i) {
    const VehicleLightValue& popup = tables.popup_angles[i];
    zpatch_popup_hashes[i] = popup.populated ? popup.model_hash : 0u;
    zpatch_popup_angles[i] = popup.populated ? popup.value : 0.0f;
    const VehicleLightValue& offset = tables.headlight_offsets[i];
    zpatch_fchs_hashes[i] = offset.populated ? offset.model_hash : 0u;
    zpatch_fchs_offsets[i] = offset.populated ? offset.value : 0.0f;
  }
  OptionFlagState flags;
  const auto flag_status = apply_option_flags(loaded, flags);
  if (!flag_status) return flag_status;
  // The option globals the live hook bodies read, exactly like the original's
  // loader: each arm reads its key with its own default (substituted on a miss)
  // and *raises* the global to 1 only when the parsed value is exactly 1 - it
  // never clears one. The moving-parts trio's arms default to "1"; the rest
  // default to "0". The globals are zero-initialised (BSS) in the original, so
  // a key that is off leaves the flag at 0.
  if (option_enabled(loaded, "RestoreDeathMusic")) zpatch_flag_death_music = 1u;
  if (option_enabled(loaded, "Patch4RemoveModChecks")) zpatch_flag_patch4_modchecks = 1u;
  if (option_enabled(loaded, "RemoveGFWLUpdatePopup")) zpatch_flag_gfwl_popup = 1u;
  // DebugPrintSpawns is read *inside* the FreezeCarFix arm (original
  // 0x10053FAB..0x10053FE0: movzx ecx,[0x10159C15]; cmp esi,1; cmove ecx,1;
  // mov [0x10159C15],cl; call 0x10037C70). With FreezeCarFix off the original
  // never touches the byte, so neither do we. The store keeps the seed when the
  // key is off and raises to 1 when it is exactly 1.
  if (option_enabled(loaded, "FreezeCarFix")) {
    if (option_enabled(loaded, "DebugPrintSpawns")) zpatch_flag_debug_spawns = 1u;
  }
  if (option_enabled(loaded, "DoNotPauseOnMinimize")) zpatch_flag_donotpause = 1u;
  log_printf("phase=flags option=DebugPrintSpawns value=%u freeze_car_fix=%u",
             static_cast<unsigned>(zpatch_flag_debug_spawns),
             option_enabled(loaded, "FreezeCarFix") ? 1u : 0u);
  // The trio's shared consumer reads one flag per option (the loader arms set
  // them before calling the shared installer).
  if (option_enabled(loaded, "MovingSteeringWheelSupport")) zpatch_flag_steering = 1u;
  if (option_enabled(loaded, "MovingPlaneParts")) zpatch_flag_plane = 1u;
  if (option_enabled(loaded, "ShakingEngineBlock")) zpatch_flag_engine = 1u;
  if (const ConfigEntry* dat = loaded.find(kOptionsSection, "OverrideGTADatPathTo"))
    set_override_gta_dat_path(dat->value);
  // The original's loader enables the Patch6and7RemoveModChecks install when
  // either that key or the undocumented companion key Patch7RemoveModChecks
  // parses to 1 (`if (v369 == 1 || Patch7RemoveModChecks == 1) sub_10044710()`).
  // Patch7RemoveModChecks is read by the loader but absent from the shipped INI;
  // mirror the OR by aliasing it onto the gating key.
  {
    const auto* six_seven = loaded.find(kOptionsSection, "Patch6and7RemoveModChecks");
    const auto* seven = loaded.find(kOptionsSection, "Patch7RemoveModChecks");
    const bool six_seven_on =
        six_seven && std::strtol(six_seven->value.c_str(), nullptr, 10) == 1;
    const bool seven_on = seven && std::strtol(seven->value.c_str(), nullptr, 10) == 1;
    if (seven_on && !six_seven_on) {
      // Lookup is last-value-wins, so appending the alias is enough.
      ConfigEntry alias;
      alias.section = std::string(kOptionsSection);
      alias.key.assign("Patch6and7RemoveModChecks");
      alias.value.assign("1");
      loaded.entries.push_back(std::move(alias));
      log_printf("config: Patch7RemoveModChecks=1 -> enabling the 6&7 install set");
    }
  }
  std::lock_guard<std::mutex> lock(mutex);
  state.vehicle_lights = tables;
  state.option_flags = flags;
  state.config = std::move(loaded);
  state.initialized = true;
  state.patches_applied = false;
  return {};
}
Status initialize() noexcept { return initialize(kDefaultConfigFile); }

void log_config_summary(GameBuild build) noexcept {
  ConfigDocument config;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.initialized) return;
    config = state.config;
  }
  std::size_t enabled = 0;
  std::size_t total = 0;
  for (const ConfigEntry& entry : config.entries) {
    if (entry.section != kOptionsSection) continue;
    ++total;
    const bool on = option_enabled(config, entry.key);
    if (on) ++enabled;
    bool known = false;
    const bool build_ok = build_allows_option(build, entry.key, known);
    log_printf("config phase=config key=%s raw=\"%s\" source=ini enabled=%u "
               "arm_default=\"%s\" build=%s",
               entry.key.c_str(), entry.value.c_str(), on ? 1u : 0u,
               option_arm_default(entry.key) ? option_arm_default(entry.key) : "-",
               known ? (build_ok ? "ok" : "denied") : "ungated");
  }
  // Options absent from the file that still enable through their arm default
  // (the original's map getter substitutes the arm default on a miss).
  std::size_t arm_count = 0;
  const OptionArmDefault* arms = option_arm_defaults(arm_count);
  for (std::size_t i = 0; i < arm_count; ++i) {
    if (config.find(kOptionsSection, arms[i].key) != nullptr) continue;
    const bool on = option_enabled(config, arms[i].key);
    if (on) ++enabled;
    bool known = false;
    const bool build_ok = build_allows_option(build, arms[i].key, known);
    log_printf("config phase=config key=%s raw=<absent> source=default effective=\"%s\" "
               "enabled=%u build=%s",
               arms[i].key, arms[i].value, on ? 1u : 0u,
               known ? (build_ok ? "ok" : "denied") : "ungated");
  }
  log_summary("config-summary phase=config keys=%zu enabled=%zu log_level=%d", total,
              enabled, log_level());
}
void shutdown() noexcept { std::lock_guard<std::mutex> lock(mutex); state = {}; }
RuntimeState runtime_state() { std::lock_guard<std::mutex> lock(mutex); return state; }

// The original loader's unconditional preamble (ZolikaPatch 0x100523c0, before
// the first INI read): seven signature scans whose results it stores in its own
// globals for the option bodies to use, plus a build probe that rewrites its
// ten hash salts only when the probe misses. It runs on every launch, no option
// gates it. The rebuilt loader performs the same scans against its own globals.
LoaderPreambleReport apply_loader_preamble(MappedImage image) noexcept {
  LoaderPreambleReport report;
  if (!image.base || image.size == 0) return report;
  const ModuleView view{image.base, image.size};
  const auto resolve = [&](std::string_view signature, const std::byte*& match) {
    Match found;
    if (!find_pattern(view, signature, found)) return false;
    match = found.address;
    return true;
  };
  const std::byte* match = nullptr;
  // 08 0B C2 8B F0 E8 (game 0x625300): the native-script handle resolver; the
  // original reads the call displacement at +6 (dword_1015BDE0).
  if (resolve("08 0B C2 8B F0 E8", match)) {
    zpatch_driveby_resolver = read_u32(match + 6) +
                              static_cast<std::uint32_t>(
                                  reinterpret_cast<std::uintptr_t>(match)) +
                              10u;
    ++report.resolved;
  } else {
    ++report.missing;
    log_printf("skip phase=preamble name=dword_1015BDE0 reason=signature-absent");
  }
  // 85 F6 74 30 ... (game 0xB191FA): a game function used by
  // BadPlayerModelCrashFix (dword_10159C74 = match - 42).
  if (resolve("85 F6 74 30 8B 16 8B 42 0C 8B CE FF D0 3C 02 75 23", match)) {
    zpatch_pre_fn_59c74 =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(match)) - 42u;
    ++report.resolved;
  } else {
    ++report.missing;
    log_printf("skip phase=preamble name=dword_10159C74 reason=signature-absent");
  }
  // 8B 44 24 04 ... (game 0x8DD2D0): the shared model-index lookup
  // (dword_10159C0C). The option-gated slot publishes write the same value;
  // the original resolves it here for every run.
  if (resolve("8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E", match)) {
    zpatch_model_lookup = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(match));
    ++report.resolved;
  } else {
    ++report.missing;
    log_printf("skip phase=preamble name=dword_10159C0C reason=signature-absent");
  }
  // 85 C0 C6 44 24 0C ... (game 0x49EC13): the ped-model object array pointer
  // (dword_1015AD78 = *(match-4)).
  if (resolve("85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07", match)) {
    zpatch_pre_ped_array = read_u32(match - 4);
    ++report.resolved;
  } else {
    ++report.missing;
    log_printf("skip phase=preamble name=dword_1015AD78 reason=signature-absent");
  }
  // 56 8B F1 8B 06 ... (first of two hits, game 0x715390): a game function used
  // by the model lookups (dword_10159B88 = match).
  if (resolve("56 8B F1 8B 06 8B 90 A0 00 00 00 57 FF D2 85 C0 74 1C", match)) {
    zpatch_pre_fn_59b88 = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(match));
    ++report.resolved;
  } else {
    ++report.missing;
    log_printf("skip phase=preamble name=dword_10159B88 reason=signature-absent");
  }
  // 0F 10 5E 14 ... (game 0x50B8F0): dword_1015D63C = match - 48.
  if (resolve("0F 10 5E 14 F3 0F 10 66 18 F3 0F 10 4C 24 0C F3", match)) {
    zpatch_pre_fn_5d63c =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(match)) - 48u;
    ++report.resolved;
  } else {
    ++report.missing;
    log_printf("skip phase=preamble name=dword_1015D63C reason=signature-absent");
  }
  // 0F 10 16 F3 0F 10 5E 04 ... (game 0x50BA00): dword_10159CDC = match - 48.
  if (resolve("0F 10 16 F3 0F 10 5E 04 F3 0F 10 4C 24 0C F3 0F 10 66 08 F2 0F 5A C0",
              match)) {
    zpatch_pre_fn_59cdc =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(match)) - 48u;
    ++report.resolved;
  } else {
    ++report.missing;
    log_printf("skip phase=preamble name=dword_10159CDC reason=signature-absent");
  }
  // The build probe: when `68 ? ? ? ? 68 A9 54 14 51 E8` is absent the game's
  // hardcoded hash salts are unknown and the original rewrites its ten salt
  // dwords with the recovered fallback set. On 1.0.8.0 the probe hits (the
  // game's salts already match) and nothing is written.
  if (!resolve("68 ? ? ? ? 68 A9 54 14 51 E8", match)) {
    zpatch_salt_28 = 0x931F73A2u;
    zpatch_salt_3c = 0xA4EA0691u;
    zpatch_salt_40 = 0xC9DBDA90u;
    zpatch_salt_50 = 0x6B86410Eu;
    zpatch_salt_54 = 0x6DCD115Au;
    zpatch_salt_5c = 0x1CC648EAu;
    zpatch_salt_a8 = 0x7F6A60D3u;
    zpatch_salt_b8 = 0x62BFDB37u;
    zpatch_salt_c8 = 0x406B4B20u;
    zpatch_salt_d4 = 0x09399528u;
    log_printf("skip phase=preamble name=salt-probe reason=signature-absent "
               "fallback=applied");
  } else {
    log_verbose("phase=preamble name=salt-probe hits=1 salts=game (no rewrite)");
  }
  log_printf("phase=preamble resolved=%zu missing=%zu", report.resolved, report.missing);
  return report;
}

Status apply_recovered_patches(GameBuild build, MappedImage image, ProtectFn protect,
                               RuntimePatchReport& report) noexcept {
  AppliedWriteLog discard;
  return apply_recovered_patches_logged(build, image, protect, report, discard);
}
Status apply_recovered_patches_logged(GameBuild build, MappedImage image, ProtectFn protect,
                                      RuntimePatchReport& report,
                                      AppliedWriteLog& log,
                                      std::uintptr_t game_module_base,
                                      RuntimeDestinationResolver resolve,
                                      void* resolver_user) noexcept {
  report = {};
  log.clear();
  if (!image.base || image.size == 0 || !protect)
    return {StatusCode::invalid_input, "invalid patch application input"};

  // Snapshot the configuration and flags so the decision is made without holding
  // the runtime lock across the writes.
  ConfigDocument config;
  OptionFlagState flags;
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (!state.initialized) return {StatusCode::invalid_input, "runtime not initialized"};
    config = state.config;
    flags = state.option_flags;
  }

  std::size_t count = 0;
  const auto* ops = recovered_patch_ops(count);
  std::size_t rva_count = 0;
  const auto* rva_jumps = recovered_rva_jumps(rva_count);
  std::size_t sig_count = 0;
  const auto* sig_jumps = recovered_sig_jumps(sig_count);
  std::size_t match_count = 0;
  const auto* match_jumps = recovered_match_jumps(match_count);

  // A literal op row whose site is also described by a recovered jump install is
  // the first byte of that same write (the jump extractor cross-validated 28/28
  // offsets against the literal table). Applying the literal row first would
  // overwrite the site's signature and hide it from the jump installer, so the
  // jump installer owns the write instead.
  const auto covered_by_jump = [&](const RecoveredPatchOp& op) {
    for (std::size_t k = 0; k < sig_count; ++k) {
      if (sig_jumps[k].option == op.option && sig_jumps[k].signature == op.signature &&
          sig_jumps[k].offset == op.offset)
        return true;
    }
    for (std::size_t k = 0; k < match_count; ++k) {
      if (match_jumps[k].option == op.option && match_jumps[k].signature == op.signature &&
          match_jumps[k].offset == op.offset)
        return true;
    }
    return false;
  };

  std::size_t applied = 0;
  // RemoveUselessChecks' table-driven writes run FIRST: the original's loader
  // arm (0x100541E4/0x1005421E) precedes the per-option handlers (e.g.
  // MiscFixes at 0x10055A19), so its NOPs can break a later handler's scan and
  // the port must scan the already-NOPped image to match (finding N5). The rows
  // were recovered from the original's executed writes
  // (tools/extract_useless_checks.py).
  {
    UselessCheckReport useless_report;
    const auto status = apply_recovered_useless_checks(
        config, image, game_module_base, protect, &log,
        static_cast<std::uint8_t>(build), useless_report);
    if (!status) return status;
    applied += useless_report.applied;
    report.candidates += useless_report.applied;
    report.gated_out += useless_report.gated_out;
    report.useless_checks_applied += useless_report.applied;
  }
  // One line per decision: every skip carries its reason, every apply its
  // signature/match/offset/opcode so the log reconstructs the load without the
  // tables. `phase=` tags let tools filter by stage.
  const auto log_skip = [](const char* phase, std::string_view option, const char* reason) {
    log_printf("skip phase=%s option=%.*s reason=%s", phase,
               static_cast<int>(option.size()), option.data(), reason);
  };
  // The original handlers scan once and write every site relative to that single
  // match; a row's write can sit inside its own signature (HighFPSSpeedupFix's
  // +3 NOP pair overwrites signature bytes 3-4), so re-scanning per row would
  // miss the remaining rows. Reuse the first match per signature for the rest of
  // this pass, exactly like the handler's `ebx`.
  struct OpMatchCacheEntry final {
    std::string_view signature;
    const std::byte* match;
  };
  std::vector<OpMatchCacheEntry> op_match_cache;

  for (std::size_t i = 0; i < count; ++i) {
    const auto& op = ops[i];
    // Gate 1: the option must be enabled in the loaded INI, exactly like the
    // original loader (absent keys are not applied at all). Bundled options
    // (e.g. MiscFixes) gate their whole recovered row set on the single key.
    if (!ini_option_enabled(config, op.option)) {
      ++report.gated_out;
      log_skip("ops", op.option, "option-off");
      continue;
    }
    const std::string_view effective_option = op.option;
    // Gate 2: the recovered per-build branch must allow this build.
    bool known = false;
    const bool allowed = build_allows_option(build, op.option, known);
    (void)known;
    if (!allowed) { ++report.gated_out; log_skip("ops", op.option, "build"); continue; }
    // Gate 2b: the option handler's first-scan anchor (finding N1). The
    // original handlers wrap their whole write set in `if (result)` around
    // their first scan; a miss means no row of this option may apply, whatever
    // signature it lands on.
    if (!op_anchor_ok(image, op.option)) {
      ++report.anchor_missing;
      log_skip("ops", op.option, "handler-anchor-absent");
      continue;
    }
    // Gate 3: a module-rva row writes blindly at base + rva, so it may only be
    // applied when the handler's own build branch produced it for this build.
    if (op.module_rva != 0 &&
        !rva_build_allows(op.handler, op.module_rva, static_cast<std::uint8_t>(build))) {
      ++report.build_mismatch;
      log_printf("skip phase=ops option=%.*s reason=rva-build-mismatch handler=%08X rva=%08X",
                 static_cast<int>(op.option.size()), op.option.data(),
                 static_cast<unsigned>(op.handler), op.module_rva);
      continue;
    }
    // A recovered full install (jump/fill) owns its site; the bare op row for
    // the same rva must not also run.
    if (op.module_rva != 0 &&
        rva_install_covers(op.module_rva, static_cast<std::uint8_t>(build))) {
      log_skip("ops", op.option, "covered-by-rva-install");
      continue;
    }
    // Curated corrections: rows the old extractor mis-split or mis-attributed
    // (they corrupted the game when applied - the savefile crash).
    if (op_row_suppressed(op.handler, op.signature, op.offset, op.replacement)) {
      ++report.suppressed;
      log_printf("skip phase=ops option=%.*s reason=curated-suppression signature=\"%.*s\"",
                 static_cast<int>(op.option.size()), op.option.data(),
                 static_cast<int>(op.signature.size()), op.signature.data());
      continue;
    }
    if (covered_by_jump(op)) {  // the jump installer writes this site
      log_skip("ops", op.option, "covered-by-jump");
      continue;
    }
    // A lone E9/E8 row is only the first byte of an install the original
    // handler writes together with its displacement. Writing it alone would
    // send the game to a garbage address, so only the jump installers may
    // write these sites; rows whose install was not recovered are skipped and
    // reported instead of half-applied.
    if (op.length == 1u && (op.replacement == "E9" || op.replacement == "E8" ||
                            op.replacement == "e9" || op.replacement == "e8")) {
      bool covered = false;
      if (op.module_rva != 0) {
        for (std::size_t k = 0; k < rva_count; ++k) {
          if (rva_jumps[k].module_rva == op.module_rva) { covered = true; break; }
        }
      }
      if (!covered) {
        ++report.jump_opcode_pending;
        log_skip("ops", op.option, "jump-opcode-pending");
        continue;
      }
      log_skip("ops", op.option, "covered-by-rva-jump");
      continue;  // the rva jump installer writes the full jump
    }
    std::byte* where = nullptr;
    std::size_t wrote = 0;
    AppliedWrite entry;
    entry.option = effective_option;
    std::uint32_t effective_rva = op.module_rva;
    if (effective_rva == 0) {
      for (const auto& cached : op_match_cache) {
        if (cached.signature == op.signature) {
          // module_rva mode writes at base+rva without applying the row offset,
          // so fold the offset into the cached match's rva.
          const auto site_offset = static_cast<std::ptrdiff_t>(cached.match - image.base) +
                                   static_cast<std::ptrdiff_t>(op.offset);
          if (site_offset > 0) effective_rva = static_cast<std::uint32_t>(site_offset);
          break;
        }
      }
    }
    const auto status = apply_mapped_operation_capture(
        image, op.signature, op.offset, effective_rva, op.replacement, where, wrote,
        entry.original, sizeof(entry.original), protect);
    if (status && op.module_rva == 0 && effective_rva == 0 && where != nullptr)
      op_match_cache.push_back({op.signature, where - op.offset});
    RecoveredApplyOutcome outcome{effective_option, status, where, wrote};
    if (!status) {
      if (status.code == StatusCode::not_found) {
        ++report.not_found;
        log_printf("skip phase=ops option=%.*s reason=signature-absent signature=\"%.*s\" "
                   "offset=%d",
                   static_cast<int>(op.option.size()), op.option.data(),
                   static_cast<int>(op.signature.size()), op.signature.data(), op.offset);
      } else {
        log_printf("skip phase=ops option=%.*s reason=write-failed detail=%s",
                   static_cast<int>(op.option.size()), op.option.data(),
                   status.detail.data());
      }
      report.outcomes.push_back(outcome);
      continue;
    }
    report.outcomes.push_back(outcome);
    entry.address = where;
    entry.length = wrote;
    log.push_back(entry);
    ++applied;
    log_verbose("apply phase=ops option=%.*s rva=%08zX len=%zu opcode=%s offset=%d "
                "signature=\"%.*s\"",
                static_cast<int>(op.option.size()), op.option.data(),
                static_cast<std::size_t>(where - image.base), wrote, op.replacement.data(),
                op.offset, static_cast<int>(op.signature.size()), op.signature.data());
  }
  // Recovered build-9 full installs (jumps with runtime displacements, NOP
  // fills, slot publishes). These supersede the bare op rows for their sites.
  log_printf("phase=ops applied=%zu candidates=%zu", applied, count);
  {
    RvaInstallReport install_report;
    const auto status = apply_recovered_rva_installs(
        config, image, game_module_base, protect, &log, resolve, resolver_user,
        static_cast<std::uint8_t>(build), install_report);
    if (!status) return status;
    applied += install_report.jumps + install_report.fills + install_report.slots;
    report.candidates += install_report.jumps + install_report.fills + install_report.slots;
    report.no_destination += install_report.skipped_no_destination;
    report.module_address_pending += install_report.skipped_no_slot;
    report.gated_out += install_report.gated_out;
    report.anchor_missing += install_report.anchor_missing;
  }
  // (The RemoveUselessChecks table-driven writes ran first; see above.)
  log_printf("phase=rva-installs done");

  // The handlers' real writes for the rows the corrections suppressed: full
  // E9/E8 installs with module or match-relative destinations.
  {
    // Slot publishes run before the sig installs: the original handler reads a
    // site's original displacement for the resume address (SkipMenu's
    // *(match+24) + match + 28) *before* it overwrites the site. Publishing
    // after the install would capture our own body as the resume and the body
    // would tail-jump to itself forever (the live CPU-spinning black screen).
    SlotInstallReport slot_report;
    const auto slot_status = apply_slot_installs(config, image, build, protect, &log, slot_report);
    if (!slot_status) return slot_status;
    report.candidates += slot_report.applied;
    report.module_address_pending += slot_report.skipped_no_slot;
    report.gated_out += slot_report.gated_out;
    report.anchor_missing += slot_report.anchor_missing;
    log_printf("phase=slots applied=%zu", slot_report.applied);

    SigInstallReport sig_report;
    const auto status = apply_sig_installs(config, image, build, game_module_base, protect, &log,
                                           resolve, resolver_user, sig_report);
    if (!status) return status;
    applied += sig_report.applied;
    report.candidates += sig_report.applied;
    report.no_destination += sig_report.skipped_no_destination;
    report.gated_out += sig_report.gated_out;
    report.anchor_missing += sig_report.anchor_missing;
    log_printf("phase=sig-installs applied=%zu", sig_report.applied);

    // Recovered signature-relative jumps whose destinations now have live
    // bodies. GATED OFF until each shim body is verified against the original
    // body's *disassembly*: the decompiled C view hides register/flag
    // dependencies of the resumes (e.g. a resume that begins with a conditional
    // jump consuming flags set by instructions the shim replaced), and a wrong
    // shim crashes the game during the savegame load. Re-enable per verified
    // body.
    constexpr bool kEnableShimInstalls = false;
    if (kEnableShimInstalls) {
      SigJumpReport sig_jump_report;
      const auto sig_jump_status = apply_sig_jump_installs(
          config, image, game_module_base, protect, &log, resolve, resolver_user,
          sig_jump_report);
      if (!sig_jump_status) return sig_jump_status;
      applied += sig_jump_report.applied;
      report.candidates += sig_jump_report.applied;
      report.no_destination += sig_jump_report.skipped_no_destination;
      report.gated_out += sig_jump_report.gated_out;
      report.anchor_missing += sig_jump_report.anchor_missing;
    }

    // Corrected plain byte writes (mis-offset op rows replaced by the
    // handler's real site, e.g. InfiniteCarBottomDistance's EB at +3).
    SigWriteReport sig_write_report;
    const auto sig_write_status =
        apply_sig_writes(config, image, build, protect, &log, sig_write_report);
    if (!sig_write_status) return sig_write_status;
    applied += sig_write_report.applied;
    report.candidates += sig_write_report.applied;
    report.gated_out += sig_write_report.gated_out;
    report.anchor_missing += sig_write_report.anchor_missing;
    log_printf("phase=sig-writes applied=%zu", sig_write_report.applied);
  }

  std::string_view cached_match_signature;
  std::vector<const std::byte*> cached_matches;
  // Module-RVA relative-jump installs: the literal op table cannot carry these
  // because their rel32 is computed from the game module base at run time. They
  // are gated identically and their preimage is captured so rollback covers them.
  for (std::size_t i = 0; i < rva_count; ++i) {
    const auto& jump = rva_jumps[i];
    ++report.candidates;
    if (!ini_option_enabled(config, jump.option)) {
      ++report.gated_out;
      log_skip("rva-jumps", jump.option, "option-off");
      continue;
    }
    if (!recovered_feature_allows_build(jump.option, static_cast<std::uint8_t>(build))) {
      ++report.gated_out;
      log_skip("rva-jumps", jump.option, "build");
      continue;
    }
    if (!op_anchor_ok(image, jump.option)) {
      ++report.anchor_missing;
      log_skip("rva-jumps", jump.option, "handler-anchor-absent");
      continue;
    }
    if (!rva_build_allows(jump.handler, jump.module_rva, static_cast<std::uint8_t>(build))) {
      ++report.build_mismatch;
      log_printf("skip phase=rva-jumps option=%.*s reason=rva-build-mismatch rva=%08X",
                 static_cast<int>(jump.option.size()), jump.option.data(), jump.module_rva);
      continue;
    }
    if (jump.module_rva + 5u > image.size) {
      ++report.not_found;
      log_printf("skip phase=rva-jumps option=%.*s reason=site-out-of-bounds rva=%08X",
                 static_cast<int>(jump.option.size()), jump.option.data(), jump.module_rva);
      continue;
    }
    AppliedWrite entry;
    entry.option = jump.option;
    entry.address = image.base + jump.module_rva;
    entry.length = 5u;
    std::memcpy(entry.original, entry.address, 5u);   // preimage for rollback
    bool skip = false;
    RecoveredRvaJump effective = jump;
    effective.destination = resolve_destination(resolve, resolver_user, jump.destination, skip);
    if (skip) {
      ++report.no_destination;
      log_printf("skip phase=rva-jumps option=%.*s reason=no-destination dest=%08X",
                 static_cast<int>(jump.option.size()), jump.option.data(), jump.destination);
      continue;
    }
    std::byte* where = nullptr;
    const auto status = apply_rva_jump(image, game_module_base, effective, where, protect);
    if (!status) {
      ++report.not_found;
      log_printf("skip phase=rva-jumps option=%.*s reason=write-failed detail=%s",
                 static_cast<int>(jump.option.size()), jump.option.data(),
                 status.detail.data());
      continue;
    }
    log.push_back(entry);
    ++applied;
    log_verbose("apply phase=rva-jumps option=%.*s rva=%08X dest=%08X", 
                static_cast<int>(jump.option.size()), jump.option.data(), jump.module_rva,
                effective.destination);
  }

  // Signature-relative jump installs: the literal table stored only the fixed E9
  // byte for these, dropping the rel32 the handler computes. The runtime scans,
  // captures the true preimage and writes the full E9 + rel32 itself (the
  // standalone apply_sig_jump cannot hand back the site before the write).
  for (std::size_t i = 0; i < sig_count; ++i) {
    const auto& jump = sig_jumps[i];
    ++report.candidates;
    if (!ini_option_enabled(config, jump.option)) {
      ++report.gated_out;
      log_skip("sig-jumps", jump.option, "option-off");
      continue;
    }
    if (!recovered_feature_allows_build(jump.option, static_cast<std::uint8_t>(build))) {
      ++report.gated_out;
      log_skip("sig-jumps", jump.option, "build");
      continue;
    }
    if (!op_anchor_ok(image, jump.option)) {
      ++report.anchor_missing;
      log_skip("sig-jumps", jump.option, "handler-anchor-absent");
      continue;
    }
    Match match;
    if (!find_pattern({image.base, image.size}, jump.signature, match)) {
      log_printf("skip phase=sig-jumps option=%.*s reason=signature-absent signature=\"%.*s\"",
                 static_cast<int>(jump.option.size()), jump.option.data(),
                 static_cast<int>(jump.signature.size()), jump.signature.data());
      continue;                                  // signature absent this build
    }
    const auto match_offset = static_cast<std::ptrdiff_t>(match.address - image.base);
    const auto site_offset = match_offset + static_cast<std::ptrdiff_t>(jump.offset);
    if (site_offset < 0 || static_cast<std::size_t>(site_offset) + 5u > image.size) {
      log_printf("skip phase=sig-jumps option=%.*s reason=site-out-of-bounds offset=%d",
                 static_cast<int>(jump.option.size()), jump.option.data(), jump.offset);
      continue;
    }
    std::byte* site = image.base + site_offset;
    AppliedWrite entry;
    entry.option = jump.option;
    entry.address = site;
    entry.length = 5u;
    std::memcpy(entry.original, site, 5u);        // preimage for rollback
    bool skip = false;
    const auto destination =
        resolve_destination(resolve, resolver_user, jump.destination, skip);
    if (skip) {
      ++report.no_destination;
      log_printf("skip phase=sig-jumps option=%.*s reason=no-destination dest=%08X",
                 static_cast<int>(jump.option.size()), jump.option.data(), jump.destination);
      continue;
    }
    const auto origin = game_module_base + static_cast<std::uintptr_t>(site_offset);
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::uintptr_t>(destination) - (origin + 5u));
    std::byte bytes[5];
    bytes[0] = std::byte{0xE9};
    std::memcpy(bytes + 1, &displacement, sizeof(displacement));
    const auto status = protected_write(protect, site, bytes, 5u);
    if (!status) return status;
    log.push_back(entry);
    ++applied;
    log_verbose("apply phase=sig-jumps option=%.*s rva=%08zX dest=%08zX signature=\"%.*s\"",
                static_cast<int>(jump.option.size()), jump.option.data(),
                static_cast<std::size_t>(site_offset),
                static_cast<std::size_t>(destination),
                static_cast<int>(jump.signature.size()), jump.signature.data());
  }

  // Indirect-literal writes: the target is a pointer the match names, so they are
  // a distinct op kind. Gated identically; preimage captured for rollback.
  std::size_t indirect_count = 0;
  const auto* indirect = recovered_indirect_writes(indirect_count);
  for (std::size_t i = 0; i < indirect_count; ++i) {
    const auto& op = indirect[i];
    ++report.candidates;
    if (!ini_option_enabled(config, op.option)) {
      ++report.gated_out;
      log_skip("indirect", op.option, "option-off");
      continue;
    }
    if (!recovered_feature_allows_build(op.option, static_cast<std::uint8_t>(build))) {
      ++report.gated_out;
      log_skip("indirect", op.option, "build");
      continue;
    }
    if (!op_anchor_ok(image, op.option)) {
      ++report.anchor_missing;
      log_skip("indirect", op.option, "handler-anchor-absent");
      continue;
    }
    std::byte* where = nullptr;
    const auto status = apply_indirect_write(image, game_module_base, op, where, protect);
    if (!status) {
      log_printf("skip phase=indirect option=%.*s reason=signature-or-target-absent "
                 "signature=\"%.*s\"",
                 static_cast<int>(op.option.size()), op.option.data(),
                 static_cast<int>(op.signature.size()), op.signature.data());
      continue;                       // signature/target absent this build
    }
    AppliedWrite entry;
    entry.option = op.option;
    entry.address = where;
    entry.length = op.width;
    if (op.width > sizeof(entry.original)) continue;
    std::memcpy(entry.original, where, op.width);
    log.push_back(entry);
    ++applied;
  }

  // Nth-match jump installs (multi-hit collector): the same signature maps to
  // several sites, so the match index is part of the op. Applied and logged like
  // the others; rollback restores them in reverse order.
  for (std::size_t i = 0; i < match_count; ++i) {
    const auto& op = match_jumps[i];
    ++report.candidates;
    if (!ini_option_enabled(config, op.option)) {
      ++report.gated_out;
      log_skip("match-jumps", op.option, "option-off");
      continue;
    }
    if (!recovered_feature_allows_build(op.option, static_cast<std::uint8_t>(build))) {
      ++report.gated_out;
      log_skip("match-jumps", op.option, "build");
      continue;
    }
    if (!op_anchor_ok(image, op.option)) {
      ++report.anchor_missing;
      log_skip("match-jumps", op.option, "handler-anchor-absent");
      continue;
    }
    if (shim_withheld(op.destination)) {
      ++report.no_destination;
      log_printf("skip phase=match-jumps option=%.*s reason=body-withheld dest=%08X",
                 static_cast<int>(op.option.size()), op.option.data(), op.destination);
      continue;
    }
    if (op.signature != cached_match_signature) {
      cached_match_signature = op.signature;
      find_all_patterns({image.base, image.size}, op.signature, cached_matches);
    }
    if (op.match_index >= cached_matches.size()) {
      log_printf("skip phase=match-jumps option=%.*s reason=match-index-absent index=%d "
                 "hits=%zu",
                 static_cast<int>(op.option.size()), op.option.data(), op.match_index,
                 cached_matches.size());
      continue;
    }
    const auto match_offset =
        static_cast<std::ptrdiff_t>(cached_matches[op.match_index] - image.base);
    const auto site_offset = match_offset + static_cast<std::ptrdiff_t>(op.offset);
    if (site_offset < 0 || static_cast<std::size_t>(site_offset) + 5u > image.size)
      continue;
    std::byte* site = image.base + site_offset;
    AppliedWrite entry;
    entry.option = op.option;
    entry.address = site;
    entry.length = 5u;
    std::memcpy(entry.original, site, 5u);
    bool skip = false;
    const auto destination = resolve_destination(resolve, resolver_user, op.destination, skip);
    if (skip) {
      ++report.no_destination;
      log_printf("skip phase=match-jumps option=%.*s reason=no-destination dest=%08X",
                 static_cast<int>(op.option.size()), op.option.data(), op.destination);
      continue;
    }
    const auto origin = game_module_base + static_cast<std::uintptr_t>(site_offset);
    const auto displacement = static_cast<std::int32_t>(
        static_cast<std::uintptr_t>(destination) - (origin + 5u));
    std::byte bytes[5];
    bytes[0] = static_cast<std::byte>(op.opcode & 0xffu);
    std::memcpy(bytes + 1, &displacement, sizeof(displacement));
    const auto status = protected_write(protect, site, bytes, 5u);
    if (!status) {
      log_printf("skip phase=match-jumps option=%.*s reason=write-failed detail=%s",
                 static_cast<int>(op.option.size()), op.option.data(), status.detail.data());
      continue;
    }
    log.push_back(entry);
    ++applied;
    log_verbose("apply phase=match-jumps option=%.*s rva=%08zX dest=%08zX index=%d",
                static_cast<int>(op.option.size()), op.option.data(),
                static_cast<std::size_t>(site_offset),
                static_cast<std::size_t>(destination), op.match_index);
  }

  // Nth-match literal writes (multi-hit collector, non-jump): the handler loops
  // over the match vector and writes a literal at each (or one) match. Gated
  // identically; each written site is logged for rollback.
  std::size_t match_literal_count = 0;
  const auto* match_literals = recovered_match_literals(match_literal_count);
  for (std::size_t i = 0; i < match_literal_count; ++i) {
    const auto& op = match_literals[i];
    ++report.candidates;
    if (!ini_option_enabled(config, op.option)) {
      ++report.gated_out;
      log_skip("match-literals", op.option, "option-off");
      continue;
    }
    if (!recovered_feature_allows_build(op.option, static_cast<std::uint8_t>(build))) {
      ++report.gated_out;
      log_skip("match-literals", op.option, "build");
      continue;
    }
    if (!op_anchor_ok(image, op.option)) {
      ++report.anchor_missing;
      log_skip("match-literals", op.option, "handler-anchor-absent");
      continue;
    }
    if (op.width == 0 || op.width > sizeof(AppliedWrite::original)) continue;
    // A 32-bit value inside the original module's image is a pointer into the
    // original ASI's own data (e.g. StartupTimeFix's timing global). Rebase it
    // to our equivalent global when one is registered; otherwise skip the write
    // (a dangling pointer would corrupt the game).
    std::uint32_t value = op.value;
    if (op.width == 4u && op.value >= 0x10000000u && op.value <= 0x101AFFFFu) {
      const std::uintptr_t rebased = rebased_value_for(op.option);
      if (!rebased) {
        ++report.module_address_pending;
        log_printf("skip phase=match-literals option=%.*s reason=module-value-unmapped "
                   "value=%08X (N3 class)",
                   static_cast<int>(op.option.size()), op.option.data(), op.value);
        continue;
      }
      value = static_cast<std::uint32_t>(rebased);
    }
    if (op.signature != cached_match_signature) {
      cached_match_signature = op.signature;
      find_all_patterns({image.base, image.size}, op.signature, cached_matches);
    }
    const auto write_site = [&](const std::byte* match) {
      const auto match_off = static_cast<std::ptrdiff_t>(match - image.base);
      const auto site_off = match_off + static_cast<std::ptrdiff_t>(op.offset);
      if (site_off < 0 || static_cast<std::size_t>(site_off) + op.width > image.size) {
        log_printf("skip phase=match-literals option=%.*s reason=site-out-of-bounds",
                   static_cast<int>(op.option.size()), op.option.data());
        return;
      }
      std::byte* site = image.base + site_off;
      AppliedWrite entry;
      entry.option = op.option;
      entry.address = site;
      entry.length = op.width;
      std::memcpy(entry.original, site, op.width);
      std::byte bytes[sizeof(AppliedWrite::original)];
      for (std::uint32_t b = 0; b < op.width; ++b)
        bytes[b] = static_cast<std::byte>((value >> (8u * b)) & 0xffu);
      const auto status = protected_write(protect, site, bytes, op.width);
      if (!status) {
        log_printf("skip phase=match-literals option=%.*s reason=write-failed detail=%s",
                   static_cast<int>(op.option.size()), op.option.data(), status.detail.data());
        return;
      }
      log.push_back(entry);
      ++applied;
      log_verbose("apply phase=match-literals option=%.*s rva=%08zX value=%08X",
                  static_cast<int>(op.option.size()), op.option.data(),
                  static_cast<std::size_t>(site_off), value);
    };
    if (op.match_index < 0) {
      for (const auto* m : cached_matches) write_site(m);
    } else if (static_cast<std::size_t>(op.match_index) < cached_matches.size()) {
      write_site(cached_matches[static_cast<std::size_t>(op.match_index)]);
    } else {
      log_printf("skip phase=match-literals option=%.*s reason=match-index-absent index=%d "
                 "hits=%zu",
                 static_cast<int>(op.option.size()), op.option.data(), op.match_index,
                 cached_matches.size());
    }
  }

  // Signature-relative literal stores the op table missed (NoLODLightHeightCutoff,
  // NoLoadingSleep, IncreasePtrNodes, IncreaseVehicleStructLimit, VSyncFix, ...).
  // A 32-bit value inside the original module's range is a pointer into the
  // original ASI's data; rebase it to our equivalent global, or skip when none is
  // registered (a dangling pointer would corrupt the game).
  constexpr bool kEnableLiteralStores = true;  // MP bisect done: culprit was RemoveBoundingBoxCulling (INI-off)
  std::size_t literal_count = 0;
  const auto* literal_stores =
      kEnableLiteralStores ? recovered_literal_stores(literal_count) : nullptr;
  for (std::size_t i = 0; i < literal_count; ++i) {
    const auto& op = literal_stores[i];
    if (!ini_option_enabled(config, op.option)) {
      ++report.gated_out;
      log_skip("literal-stores", op.option, "option-off");
      continue;
    }
    if (!recovered_feature_allows_build(op.option, static_cast<std::uint8_t>(build))) {
      ++report.gated_out;
      log_skip("literal-stores", op.option, "build");
      continue;
    }
    if (!op_anchor_ok(image, op.option)) {
      ++report.anchor_missing;
      log_skip("literal-stores", op.option, "handler-anchor-absent");
      continue;
    }
    if (op.width == 0 || op.width > sizeof(AppliedWrite::original)) continue;
    std::uint32_t value = op.value;
    // Any 32-bit literal inside the original module's image (0x10000000 + its
    // ~0x1B0000 size) is a pointer into the original ASI's data; the game image
    // lives far below 0x10000000, so the range is unambiguous.
    if (op.width == 4u && op.value >= 0x10000000u && op.value <= 0x101AFFFFu) {
      const std::uintptr_t rebased = rebased_value_for(op.option);
      if (!rebased) {
        ++report.module_address_pending;
        log_printf("skip phase=literal-stores option=%.*s reason=module-value-unmapped "
                   "value=%08X (N3 class)",
                   static_cast<int>(op.option.size()), op.option.data(), op.value);
        continue;
      }
      value = static_cast<std::uint32_t>(rebased);
    }
    Match match;
    if (!find_pattern({image.base, image.size}, op.signature, match)) {
      log_printf("skip phase=literal-stores option=%.*s reason=signature-absent "
                 "signature=\"%.*s\"",
                 static_cast<int>(op.option.size()), op.option.data(),
                 static_cast<int>(op.signature.size()), op.signature.data());
      continue;
    }
    const auto match_offset = static_cast<std::ptrdiff_t>(match.address - image.base);
    const auto site_offset = match_offset + static_cast<std::ptrdiff_t>(op.offset);
    if (site_offset < 0 || static_cast<std::size_t>(site_offset) + op.width > image.size)
      continue;
    std::byte* site = image.base + site_offset;
    AppliedWrite entry;
    entry.option = op.option;
    entry.address = site;
    entry.length = op.width;
    std::memcpy(entry.original, site, op.width);
    std::byte bytes[sizeof(AppliedWrite::original)];
    for (std::uint32_t b = 0; b < op.width; ++b)
      bytes[b] = static_cast<std::byte>((value >> (8u * b)) & 0xffu);
    const auto status = protected_write(protect, site, bytes, op.width);
    if (!status) {
      log_printf("skip phase=literal-stores option=%.*s reason=write-failed detail=%s",
                 static_cast<int>(op.option.size()), op.option.data(), status.detail.data());
      continue;
    }
    log.push_back(entry);
    ++applied;
    log_verbose("apply phase=literal-stores option=%.*s rva=%08zX value=%08X",
                static_cast<int>(op.option.size()), op.option.data(),
                static_cast<std::size_t>(site_offset), value);
  }

  // Candidates are every row the pipeline considered: the recovered op/jump
  // tables plus the curated corrections (sig installs, slot publishes, sig
  // writes), so applied + not_found + gated_out never exceeds the candidate
  // count even when a curated row is gated out.
  std::size_t curated_sig = 0;
  sig_installs(curated_sig);
  std::size_t curated_slots = 0;
  slot_installs(curated_slots);
  std::size_t curated_writes = 0;
  sig_writes(curated_writes);
  report.candidates = count + rva_count + sig_count + indirect_count + match_count +
                      match_literal_count + literal_count + curated_sig + curated_slots +
                      curated_writes;
  report.applied = applied;

  // Full trace of the live pass: every write with its bytes, then every op-row
  // that did not apply with its reason.
  for (const AppliedWrite& entry : log) {
    const auto va = game_module_base + static_cast<std::uintptr_t>(entry.address - image.base);
    std::string option(entry.option);
    log_write("write", option.c_str(), va, entry.address, entry.length);
  }
  for (const RecoveredApplyOutcome& outcome : report.outcomes) {
    if (outcome.status) continue;
    std::string option(outcome.option);
    log_printf("skip phase=ops-write option=%s reason=%s", option.c_str(),
               outcome.status.detail.data());
  }
  log_summary("phase=summary applied=%zu candidates=%zu not_found=%zu gated=%zu "
              "jump_opcode_pending=%zu no_destination=%zu module_pending=%zu "
              "build_mismatch=%zu suppressed=%zu anchor_missing=%zu useless_checks=%zu "
              "writes=%zu",
              report.applied, report.candidates, report.not_found, report.gated_out,
              report.jump_opcode_pending, report.no_destination,
              report.module_address_pending, report.build_mismatch, report.suppressed,
              report.anchor_missing, report.useless_checks_applied, log.size());
  log_flush();
  if (applied) {
    std::lock_guard<std::mutex> lock(mutex);
    state.patches_applied = true;
  }
  log_printf("phase=apply applied=%zu", applied);
  return {};
}
Status rollback_recovered_patches(ProtectFn protect, AppliedWriteLog& log,
                                  std::size_t& restored) noexcept {
  restored = 0;
  if (!protect) return {StatusCode::invalid_input, "invalid rollback input"};
  AppliedWriteLog remaining;
  // Undo in reverse application order so overlapping sites unwind correctly.
  for (auto it = log.rbegin(); it != log.rend(); ++it) {
    if (!it->address || it->length == 0 || it->length > sizeof(it->original)) {
      remaining.push_back(*it);
      continue;
    }
    std::uint32_t previous = 0;
    const auto protected_status = protect(it->address, it->length, 0x40u, &previous);
    if (!protected_status) { remaining.push_back(*it); continue; }
    const auto status = restore_mapped_bytes(it->address, it->original, it->length);
    (void)protect(it->address, it->length, previous, &previous);
    if (!status) { remaining.push_back(*it); continue; }
    ++restored;
  }
  // Reverse back so the retained entries keep their original application order.
  log.assign(remaining.rbegin(), remaining.rend());
  if (restored) {
    std::lock_guard<std::mutex> lock(mutex);
    state.patches_applied = false;
  }
  return {};
}
}  // namespace zpatch
