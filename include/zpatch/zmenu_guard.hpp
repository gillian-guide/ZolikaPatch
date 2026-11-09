#pragma once
// The original loader's ZMenuIV compatibility guard (sub_10052300, called
// unconditionally at 0x1005527B) and the IVMenuAPI registration tail
// (sub_100522B0 called from sub_100523C0 at 0x10055C0C / 0x10055C31).
//
// sub_10052300: GetModuleHandleA("ZMenuIV.asi") -> GetProcAddress
// ("GetZMenuVersion") -> call it with two out pointers (cdecl: the original
// does `push p2; push p1; call eax; add esp, 8`; the result is ignored) -> when
// the first out value is below 0x35DD1, MessageBoxA(NULL, text, "", 0x10) and
// TerminateProcess(GetCurrentProcess(), 0).
//
// sub_100522B0(family, title, pref, getter, setter): GetModuleHandleA
// ("IVMenuAPI.asi") -> GetProcAddress("AddIVMenuOption") -> call with
// (family, title, "MENU_DISPLAY_ON_OFF", pref, 1, 0, getter, setter).

#include <cstddef>
#include <cstdint>

namespace zpatch {

constexpr std::uint32_t kZMenuMinVersion = 0x35DD1u;
constexpr const char* kZMenuModuleName = "ZMenuIV.asi";
constexpr const char* kZMenuProcName = "GetZMenuVersion";
constexpr const char* kZMenuRejectText =
    "Incompatible/outdated version of ZMenu detected! The game will now exit.";
// The original passes `Caption` (0x10063CD6), which is an empty string.
constexpr const char* kZMenuRejectCaption = "";
constexpr std::uint32_t kZMenuRejectFlags = 0x10u;  // MB_ICONHAND
constexpr std::uint32_t kZMenuTerminateExitCode = 0u;

enum class ZMenuGuardResult : std::uint8_t {
  module_absent,   // ZMenuIV.asi is not loaded
  export_absent,   // no GetZMenuVersion export
  version_ok,      // the export reported >= kZMenuMinVersion
  version_rejected  // below the minimum: the original exits
};

// Runs the guard's decision logic (module lookup, export lookup, call).
// `version` receives the first out value when the export ran.
ZMenuGuardResult zmenu_version_guard(std::uint32_t& version) noexcept;

// True when a reported version passes the guard (>= 0x35DD1).
constexpr bool zmenu_version_accepted(std::uint32_t version) noexcept {
  return version >= kZMenuMinVersion;
}

// One row of the loader's AddIVMenuOption registration. `getter`/`setter` are
// module-relative and resolved in register_menu_options_win.
struct MenuRegistration final {
  const char* family;
  const wchar_t* title;
  const char* display;
  const char* pref;
  int a;
  int b;
  std::uintptr_t getter;
  std::uintptr_t setter;
};

const MenuRegistration* menu_registrations(std::size_t& count) noexcept;

// The original's registration tail: when IVMenuAPI.asi is loaded and exports
// AddIVMenuOption, register every row of menu_registrations() through it.
// Returns the number of successful calls.
std::size_t register_menu_options_win() noexcept;

}  // namespace zpatch
