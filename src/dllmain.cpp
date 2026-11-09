#if !defined(_WIN32)
#error "The ASI entry point is Windows-only"
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <tlhelp32.h>

#include "zpatch/game.hpp"
#include "zpatch/hook_bodies_win.hpp"
#include "zpatch/log.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/rebased_values.hpp"
#include "zpatch/runtime.hpp"
#include "zpatch/skip_launcher.hpp"
#include "zpatch/skip_launcher_win.hpp"
#include "zpatch/zmenu_guard.hpp"

#if defined(_M_IX86) && defined(_MSC_VER)
// Provided by the static CRT because hook_bodies_win.cpp uses a
// __declspec(thread) anchor; the loader assigns it before DllMain runs.
#endif

namespace {

zpatch::Status virtual_protect(void* address, std::size_t size, std::uint32_t new_protect,
                               std::uint32_t* previous) noexcept {
  DWORD old_protect = 0;
  if (!VirtualProtect(address, size, new_protect, &old_protect))
    return {zpatch::StatusCode::io_error, "VirtualProtect failed"};
  if (previous) *previous = old_protect;
  return {};
}

// The DFA installer's own guards (sub_10004850 at 1000487B-100048CD): the
// XLivelessAddon flag, GTAC.dll, the EFLC builds (build id 0xA..0xD) and a
// process-wide one-shot flag ([0x10159C16]).
void log_line(const char* format, ...);

bool dfa_install_allowed(zpatch::GameBuild build) {
  if (zpatch::zpatch_xliveless_present != 0u) return false;
  if (GetModuleHandleA("GTAC.dll") != nullptr) return false;
  switch (build) {
    case zpatch::GameBuild::v1100:
    case zpatch::GameBuild::v1110:
    case zpatch::GameBuild::v1120:
    case zpatch::GameBuild::v1130:
      return false;
    default:
      break;
  }
  return true;
}

// One-shot mirror of the original's [0x10159C16]: SkipLauncher's chain
// (0x10045CE0 -> 0x10004850) and the MiscFixes handler (10024018) both call the
// DFA installer; the second call is a no-op.
bool g_dfa_installed = false;

// Applies only the DFA installer stage under its own guards.
void apply_dfa_stage_if_allowed(zpatch::GameBuild build, const zpatch::MappedImage& image,
                                const char* source) {
  if (!dfa_install_allowed(build) || g_dfa_installed) return;
  g_dfa_installed = true;
  zpatch::SkipLauncherReport report;
  (void)zpatch::apply_skip_launcher(image, &virtual_protect, nullptr,
                                    &zpatch::skip_launcher_win_resolve, nullptr, report,
                                    zpatch::SkipLauncherStage::dfa);
  zpatch::skip_launcher_win_set_create_file_slot(report.create_file_slot);
  log_line("DFA (%s): installs=%zu missing=%zu collected=%zu slot=%#x", source,
           report.dfa_installs, report.dfa_missing, report.collected_installs,
           report.create_file_slot);
}

// Mirrors the original handler's load-time application of the SkipLauncher
// bypass (0x10045ce0 + 0x10004850 + 0x10004620): patch the game module found by
// signature so the game starts without the launcher/DRM shims, including the
// DFA file-API hooks and the ROM message-window service.
zpatch::SkipLauncherReport apply_skip_launcher_if_enabled(zpatch::GameBuild build) {
  zpatch::SkipLauncherReport report;
  const auto state = zpatch::runtime_state();
  if (!state.initialized) return report;
  if (!zpatch::ini_option_enabled(state.config, "SkipLauncher")) return report;

  auto* module = reinterpret_cast<std::byte*>(GetModuleHandleA(nullptr));
  if (!module) return report;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return report;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
      module + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return report;

  // 0x10045CE0's XLivelessAddon guard covers the main writes and the DFA
  // installer; the ROM service (0x10004620) is called after it unconditionally.
  // 0x10004850 adds the GTAC/build/one-shot guards.
  auto stages = zpatch::SkipLauncherStage::rom;
  if (zpatch::zpatch_xliveless_present == 0u)
    stages = stages | zpatch::SkipLauncherStage::main;
  if (dfa_install_allowed(build) && !g_dfa_installed) {
    stages = stages | zpatch::SkipLauncherStage::dfa;
    g_dfa_installed = true;
  }
  const zpatch::MappedImage image{module, nt->OptionalHeader.SizeOfImage};
  (void)zpatch::apply_skip_launcher(image, &virtual_protect, nullptr,
                                    &zpatch::skip_launcher_win_resolve, nullptr, report,
                                    stages);
  zpatch::skip_launcher_win_set_create_file_slot(report.create_file_slot);
  return report;
}

char g_log_path[MAX_PATH]{};
char g_ini_path[MAX_PATH]{};
char g_module_path[MAX_PATH]{};

void log_line(const char* format, ...) {
  if (!g_log_path[0]) return;
  char buffer[1024];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  zpatch::log_printf("%s", buffer);
}

// djb2 over the module file, so every run identifies the exact build that was
// loaded even when several copies sit in the game folder.
std::uint32_t hash_module_file(const char* path) {
  std::FILE* file = std::fopen(path, "rb");
  if (!file) return 0;
  std::uint32_t hash = 5381;
  unsigned char chunk[4096];
  std::size_t read = 0;
  while ((read = std::fread(chunk, 1, sizeof(chunk), file)) != 0) {
    for (std::size_t i = 0; i < read; ++i) hash = hash * 33u + chunk[i];
  }
  std::fclose(file);
  return hash;
}

// Derives the log and INI paths from the ASI's own location, so both work
// regardless of the process working directory.
void resolve_paths(HMODULE module) {
  char path[MAX_PATH]{};
  const DWORD length = GetModuleFileNameA(module, path, MAX_PATH);
  if (!length || length >= MAX_PATH) return;
  char* slash = path + length;
  while (slash != path && slash[-1] != '\\' && slash[-1] != '/') --slash;
  const std::size_t prefix = static_cast<std::size_t>(slash - path);
  const auto copy_name = [&](const char* name, char* destination) {
    const std::size_t name_length = std::strlen(name) + 1;
    if (prefix + name_length > MAX_PATH) return;
    std::memcpy(destination, path, prefix);
    std::memcpy(destination + prefix, name, name_length);
  };
  copy_name("ZolikaPatch.log", g_log_path);
  copy_name("ZolikaPatch.ini", g_ini_path);
  if (length < MAX_PATH) std::memcpy(g_module_path, path, length + 1);
}

// Opens the trace log and records the build identity first, so every later
// line can be attributed to an exact binary.
void open_log() {
  if (!g_log_path[0]) return;
  zpatch::log_open(g_log_path);
  zpatch::log_printf("phase=build-identity compiled=%s %s module=%s size-hash=%08X", __DATE__,
                     __TIME__,
                     g_module_path, hash_module_file(g_module_path));
  zpatch::log_printf("phase=attach version=7.65-rebuild");
  zpatch::log_printf("phase=process exe=%s", []() -> const char* {
    static char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    return path;
  }());
}

// LaunchWithPlayGTAIV (original sub_100441B0): when a process named exactly
// "PlayGTAIV.exe" exists, rewrite the two in-image launcher-name strings
// ("LaunchGTAIV.exe") to "PlayGTAIV.exe" so a non-SecuROM launcher renamed to
// PlayGTAIV can start patch 7/8 (and 4/5/8) builds. The original's process scan
// is CreateToolhelp32Snapshot/Process32First/Next plus a strcmp of the exe name
// (allowing the first entry to match too); its per-build RVA pairs are below.
bool find_play_gtaiv_running() {
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return false;
  // The original uses the ANSI PROCESSENTRY32 + strcmp; the wide variant is the
  // same scan with the same exact-name compare.
  PROCESSENTRY32W entry{};
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      if (std::wcscmp(L"PlayGTAIV.exe", entry.szExeFile) == 0) {
        CloseHandle(snapshot);
        return true;
      }
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return false;
}

void apply_launch_with_play_gtaiv(zpatch::GameBuild build, std::byte* module_base,
                                  const zpatch::ConfigDocument& config) {
  if (!zpatch::ini_option_enabled(config, "LaunchWithPlayGTAIV")) return;
  if (!find_play_gtaiv_running()) {
    log_line("LaunchWithPlayGTAIV: PlayGTAIV.exe not running; no patches");
    return;
  }
  std::uint32_t first = 0;
  std::uint32_t second = 0;
  const std::uint32_t id = static_cast<std::uint32_t>(build);
  if (id == static_cast<std::uint32_t>(zpatch::GameBuild::v1000)) {
    first = 10421308u;
    second = 10025484u;
  } else if (id == static_cast<std::uint32_t>(zpatch::GameBuild::v1040)) {
    first = 10396036u;
    second = 10344608u;
  } else if (id == static_cast<std::uint32_t>(zpatch::GameBuild::v1060)) {
    first = 10456896u;
    second = 11129048u;
  } else if (id == static_cast<std::uint32_t>(zpatch::GameBuild::v1070)) {
    first = 10461040u;
    second = 11133208u;
  } else if (id == static_cast<std::uint32_t>(zpatch::GameBuild::v1080)) {
    first = 10815736u;
    second = 10855908u;
  } else {
    return;  // the original patches only these builds
  }
  // Two 8-byte blocks per site: "Play"+"GTAI" then "V.ex" then the leftover
  // `e\0e\0`; written verbatim from the original's four dwords.
  const std::uint32_t block[4] = {2036427856u, 1229018183u, 2019896918u, 6619237u};
  for (const std::uint32_t site : {first, second}) {
    auto* address = module_base + site;
    DWORD previous = 0;
    if (!VirtualProtect(address, 16, PAGE_EXECUTE_READWRITE, &previous)) continue;
    std::memcpy(address, block, 16);
    DWORD ignored = 0;
    VirtualProtect(address, 16, previous, &ignored);
    log_line("LaunchWithPlayGTAIV: patched launcher name at rva=%#x", site);
  }
}

// The original's IVMenuAPI gate (sub_10052380): AddOptionsInMenu on and
// IVMenuAPI.asi loadable (the failed LoadLibrary is remembered once).
bool menu_integration_available() {
  const auto state = zpatch::runtime_state();
  if (!zpatch::ini_option_enabled(state.config, "AddOptionsInMenu")) return false;
  if (GetModuleHandleA("IVMenuAPI.asi")) return true;
  static bool load_failed = false;
  if (load_failed) return false;
  if (LoadLibraryA("IVMenuAPI.asi")) return true;
  load_failed = true;
  log_line("IVMenuAPI.asi not present; menu options not registered");
  return false;
}

// The original loader tail (sub_100522B0): register the two runtime toggles.
void register_menu_options() {
  const std::size_t calls = zpatch::register_menu_options_win();
  if (calls) log_line("IVMenuAPI: registered Death Music and Pause On Focus Loss toggles");
}

// Runs the whole loader synchronously in DllMain, exactly like the original
// (DllMain -> 0x100523c0). A worker thread loses the race against the DRM's
// import-time code modification, so the signatures no longer match by the time
// it scans; FusionFix shows the same discipline (no deferred init for the
// load-time work).
// The ZMenuIV compatibility check the original loader calls unconditionally at
// 1005527B (sub_10052300): when ZMenuIV.asi exports GetZMenuVersion and its
// version is below 0x35DD1, show the incompatibility box and terminate. The
// decision lives in zmenu_guard.cpp so the harnesses exercise the same code.
void enforce_zmenu_version() {
  std::uint32_t version = 0;
  const zpatch::ZMenuGuardResult result = zpatch::zmenu_version_guard(version);
  switch (result) {
    case zpatch::ZMenuGuardResult::module_absent:
      return;
    case zpatch::ZMenuGuardResult::export_absent:
      log_line("phase=zmenu result=export-absent module=ZMenuIV.asi");
      return;
    case zpatch::ZMenuGuardResult::version_ok:
      log_line("phase=zmenu result=accept version=%#x min=%#x", version,
             zpatch::kZMenuMinVersion);
      return;
    case zpatch::ZMenuGuardResult::version_rejected:
      break;
  }
  log_line("phase=zmenu result=reject version=%#x min=%#x box=\"%s\" flags=%#x "
             "terminate-code=%u",
             version, zpatch::kZMenuMinVersion, zpatch::kZMenuRejectText,
             zpatch::kZMenuRejectFlags, zpatch::kZMenuTerminateExitCode);
  MessageBoxA(nullptr, zpatch::kZMenuRejectText, zpatch::kZMenuRejectCaption,
              zpatch::kZMenuRejectFlags);
  TerminateProcess(GetCurrentProcess(), zpatch::kZMenuTerminateExitCode);
}

void run_loader() {
  log_line("phase=loader start ini=%s", g_ini_path);
  if (!g_ini_path[0]) return;
  const zpatch::Status status = zpatch::initialize(g_ini_path);
  // sub_10052300 follows config-object initialization and is unconditional;
  // an initialize status failure must not suppress this compatibility check.
  enforce_zmenu_version();
  log_line("phase=initialize result=%s", status ? "ok" : status.detail.data());
  if (!status) {
    log_line("phase=loader done (initialize failed)");
    zpatch::log_flush();
    return;
  }
  // Non-original infrastructure: [Debug] LogLevel selects the trace verbosity
  // (0 off, 1 summary, 2 decisions default, 3 verbose). Documented in
  // docs/LOGGING.md; the original has no such key.
  {
    const auto state = zpatch::runtime_state();
    const auto* level = state.config.find("Debug", "LogLevel");
    int level_value = 2;
    if (level && !level->value.empty()) level_value = std::atoi(level->value.c_str());
    zpatch::log_set_level(level_value);
    log_line("config phase=log key=LogLevel raw=\"%s\" source=%s level=%d",
             level ? level->value.c_str() : "<absent>", level ? "ini" : "default",
             zpatch::log_level());
  }
  // The build identity and module image the load-time installers need. An
  // unrecognized game version installs nothing (every recovered write is
  // build-specific).
  std::uint32_t compact_version = 0;
  zpatch::GameBuild build = zpatch::GameBuild::unknown;
  (void)zpatch::query_current_game_version(compact_version, build);
  log_line("phase=build compact=%u id=%u name=%s", compact_version,
            static_cast<unsigned>(build), zpatch::game_build_name(build));
  zpatch::log_config_summary(build);
  {
    // Debug crash probe (off by default): [Debug] CrashProbe=<OptionName> arms a
    // one-shot fault injection so a fix's recovery path can be exercised live.
    {
      const auto state = zpatch::runtime_state();
      const auto* probe = state.config.find("Debug", "CrashProbe");
      if (probe && !probe->value.empty() && probe->value != "0")
        (void)zpatch::hook_bodies_win_set_probe(probe->value);
    }
    auto* module_base = reinterpret_cast<std::byte*>(GetModuleHandleA(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module_base);
    const auto* nt = dos && dos->e_magic == IMAGE_DOS_SIGNATURE
                         ? reinterpret_cast<const IMAGE_NT_HEADERS*>(module_base + dos->e_lfanew)
                         : nullptr;
    // MiscFixes' loader arm (0x10052DF3) runs before SkipLauncher's
    // (0x10055504) and calls the one-shot DFA installer (10024018) under the
    // same guards; the SkipLauncher chain's own call is then a no-op.
    if (nt && nt->Signature == IMAGE_NT_SIGNATURE &&
        zpatch::ini_option_enabled(zpatch::runtime_state().config, "MiscFixes")) {
      const zpatch::MappedImage dfa_image{module_base, nt->OptionalHeader.SizeOfImage};
      apply_dfa_stage_if_allowed(build, dfa_image, "MiscFixes");
    }
    const zpatch::SkipLauncherReport report = apply_skip_launcher_if_enabled(build);
    log_line("phase=skip-launcher entries_found=%zu entries_missing=%zu writes=%zu "
             "collectors_found=%zu collector_writes=%zu jump=%s",
             report.entries_found, report.entries_missing, report.writes,
             report.collectors_found, report.collector_writes,
             report.jump_installed ? "yes" : "no");
    log_line("phase=skip-launcher-dfa installs=%zu missing=%zu collected=%zu slot=%#x",
             report.dfa_installs, report.dfa_missing, report.collected_installs,
             report.create_file_slot);
    log_line("phase=skip-launcher-rom installs=%zu missing=%zu", report.rom_installs,
             report.rom_missing);

    // FixPCIDKickbug (original loader arm): delete the GFWL CD-key value so
    // keygen users stop kicking each other on join. The arm's default is "1",
    // so an absent key still runs it; the value must parse to exactly 1.
    {
      const auto state = zpatch::runtime_state();
      if (zpatch::ini_option_enabled(state.config, "FixPCIDKickbug")) {
        using DeleteValueFn = LONG(WINAPI*)(HKEY, LPCWSTR, LPCWSTR);
        HMODULE shlwapi = LoadLibraryW(L"shlwapi.dll");
        if (shlwapi) {
          const auto delete_value = reinterpret_cast<DeleteValueFn>(
              GetProcAddress(shlwapi, "SHDeleteValueW"));
          if (delete_value) {
            const LONG rc = delete_value(HKEY_CURRENT_USER,
                                         L"Software\\Classes\\Software\\Microsoft\\XLive",
                                         L"PCID");
            log_line("FixPCIDKickbug: SHDeleteValueW rc=%ld", rc);
          }
        }
      }
    }

    // General option patches: the original applies every enabled option's
    // operations in the same synchronous pass. Destinations resolve to
    // ABI-exact bodies; hooks without a live-ready body are skipped and
    // counted in the report.
    if (build == zpatch::GameBuild::unknown) {
      // An unrecognized game version must install nothing: every recovered write
      // is build-specific, so applying it blindly could corrupt a build the
      // original never touched.
      log_line("phase=patches skipped reason=unrecognized-build");
    } else if (nt && nt->Signature == IMAGE_NT_SIGNATURE) {
      const zpatch::MappedImage game_image{module_base, nt->OptionalHeader.SizeOfImage};
      // The original's menu integration: with IVMenuAPI.asi available the
      // RestoreDeathMusic hooks install (flag-gated) and the DoNotPause
      // installer runs so the menu can toggle both at runtime.
      const bool menu = menu_integration_available();
      zpatch::set_menu_integration_available(menu);
      if (menu) register_menu_options();
      const auto state = zpatch::runtime_state();
      if (menu || zpatch::ini_option_enabled(state.config, "DoNotPauseOnMinimize"))
        zpatch::zpatch_menu_prime_focus_loss();
      // LocalSettingsPath's handler captures the CWD and arms its call slot
      // before installing (the original has a static constructor for the slot).
      if (zpatch::ini_option_enabled(state.config, "LocalSettingsPath"))
        zpatch::zpatch_local_settings_capture_cwd();
      // The original loader resolves its game pointers and reads the two
      // vehicle-light INI sections before any option runs (unconditionally).
      // Nothing here writes game code; the resolved values only feed the hook
      // bodies' globals.
      const auto preamble = zpatch::apply_loader_preamble(game_image);
      log_line("phase=preamble resolved=%zu missing=%zu", preamble.resolved, preamble.missing);
      // CarDecalsFix's arm double-load guard: the original shows this box when
      // its scan finds no match (a second instance/loader already patched the
      // site) and would otherwise leave the game unpatched.
      {
        const auto state = zpatch::runtime_state();
        if (zpatch::ini_option_enabled(state.config, "CarDecalsFix")) {
          zpatch::Match decals_match;
          const zpatch::ModuleView decals_view{game_image.base, game_image.size};
          if (!zpatch::find_pattern(
                  decals_view,
                  "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00",
                  decals_match)) {
            log_line("CarDecalsFix: pattern absent (already patched?); showing double-load warning");
            MessageBoxA(0,
                        "ZPatch was loaded twice! Make sure that you don't have multiple ASI "
                        "loaders installed! (e.g. xliveless + dsound, xliveless + dinput8, "
                        "dsound + dinput8, etc)",
                        "ZPatch Error", 0);
          }
        }
      }
      zpatch::RuntimePatchReport patch_report;
      zpatch::AppliedWriteLog write_log;
      log_line("phase=patches start build=%u hooks_ready=%zu", static_cast<unsigned>(build),
               zpatch::hook_bodies_win_count());
      const auto patch_status = zpatch::apply_recovered_patches_logged(
          build, game_image, &virtual_protect, patch_report, write_log,
          reinterpret_cast<std::uintptr_t>(module_base), &zpatch::hook_bodies_win_resolve,
          nullptr);
      log_line("phase=patches result=%s candidates=%zu applied=%zu not_found=%zu gated=%zu "
               "jump_opcode_pending=%zu no_destination=%zu module_address_pending=%zu "
               "build_mismatch=%zu suppressed=%zu anchor_missing=%zu",
               patch_status ? "ok" : patch_status.detail.data(), patch_report.candidates,
               patch_report.applied, patch_report.not_found, patch_report.gated_out,
               patch_report.jump_opcode_pending, patch_report.no_destination,
               patch_report.module_address_pending, patch_report.build_mismatch,
               patch_report.suppressed, patch_report.anchor_missing);
      // LaunchWithPlayGTAIV (loader arm, INI-gated): rewrite the in-image
      // launcher-name strings when PlayGTAIV.exe is running.
      apply_launch_with_play_gtaiv(build, module_base, state.config);
    }
  }
  log_line("phase=loader done");
  zpatch::log_flush();
}

// The original DllMain's unconditional pre-loader checks (0x10055c70): an old
// dsound.dll ASI loader (exact size 0x16600) triggers a warning box, and a
// compact version in [1201, 12999] (Complete Edition and newer) opens the
// downgrade page and terminates the process - the original refuses to run
// there. Both run before the loader and are not INI-gated.
void apply_dllmain_checks(std::uint32_t compact_version) {
  if (HMODULE dsound = GetModuleHandleW(L"dsound.dll")) {
    wchar_t path[MAX_PATH]{};
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (GetModuleFileNameW(dsound, path, MAX_PATH) &&
        GetFileAttributesExW(path, GetFileExInfoStandard, &info)) {
      const unsigned long long size =
          (static_cast<unsigned long long>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
      if (size == 91648ull) {
        log_line("dsound: old dsound.dll ASI loader detected (size 91648)");
        MessageBoxA(0,
                    "Old dsound.dll detected! It's strongly recommended to use Ultimate ASI "
                    "Loader as either dinput8.dll or xlive.dll to load this mod.",
                    "ZPatch Warning", 0);
      }
    }
  }
  if (compact_version - 1201u <= 0x2E16u) {
    log_line("version guard: unsupported version %u; opening downgrade page and terminating",
             compact_version);
    std::system("start https://zolika1351.pages.dev/mods/ivpatch/downgrading");
    Sleep(500);
    TerminateProcess(GetCurrentProcess(), 0x8000FFFFu);
  }
}

// The original loader's module captures (0x100523c0 preamble and DllMain):
// xlive.dll's base for the GFWL patches, XLivelessAddon.asi's presence, and
// CitizenIV.exe's presence for the MP crash-method path. All unconditional.
void capture_loader_modules() {
  zpatch::zpatch_xlive_base = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(GetModuleHandleA("xlive.dll")));
  zpatch::zpatch_xliveless_present =
      GetModuleHandleA("XLivelessAddon.asi") != nullptr ? 1u : 0u;
  if (GetModuleHandleA("CitizenIV.exe")) zpatch::zpatch_citizen_present = 1u;
  log_line("phase=modules xlive=%08X xliveless=%u citizeniv=%u", zpatch::zpatch_xlive_base,
           zpatch::zpatch_xliveless_present, zpatch::zpatch_citizen_present);
}
}  // namespace

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    resolve_paths(module);
    open_log();
    std::uint32_t compact_version = 0;
    zpatch::GameBuild build = zpatch::GameBuild::unknown;
    (void)zpatch::query_current_game_version(compact_version, build);
    apply_dllmain_checks(compact_version);
    capture_loader_modules();
    run_loader();
  }
  // Process teardown reclaims state; avoid locks and runtime work in DllMain.
  return TRUE;
}
