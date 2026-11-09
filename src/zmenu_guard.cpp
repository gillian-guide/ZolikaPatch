// The loader's ZMenuIV guard + IVMenuAPI registration (see zmenu_guard.hpp).
// Windows-only at heart; the non-Windows fallback reports the module absent so
// the portable test build links.
#include "zpatch/zmenu_guard.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "zpatch/hook_bodies_win.hpp"

namespace zpatch {
namespace {

// The two toggles the original registers (sub_100523C0's tail). The original
// passes the raw module addresses of its getter/setter callbacks; the rebuilt
// loader passes its own ABI bodies' addresses (rebased in the live module).
const MenuRegistration kMenuRegistrations[] = {
    {"PREF_DEATHMUSIC", L"Death Music", "MENU_DISPLAY_ON_OFF", "PREF_MUSIC_VOLUME",
     1, 0, reinterpret_cast<std::uintptr_t>(&abi_menu_death_get),
     reinterpret_cast<std::uintptr_t>(&abi_menu_death_set)},
    {"PREF_FOCUSLOSS", L"Pause Game On Focus Loss", "MENU_DISPLAY_ON_OFF", "PREF_VSYNC",
     1, 0, reinterpret_cast<std::uintptr_t>(&abi_menu_focus_get),
     reinterpret_cast<std::uintptr_t>(&abi_menu_focus_set)},
};

}  // namespace

const MenuRegistration* menu_registrations(std::size_t& count) noexcept {
  count = sizeof(kMenuRegistrations) / sizeof(kMenuRegistrations[0]);
  return kMenuRegistrations;
}

#if defined(_WIN32)

ZMenuGuardResult zmenu_version_guard(std::uint32_t& version) noexcept {
  version = 0;
  const HMODULE module = GetModuleHandleA(kZMenuModuleName);
  if (!module) return ZMenuGuardResult::module_absent;
  using GetZMenuVersionFn = int(__cdecl*)(std::uint32_t*, std::uint32_t*);
  const auto get_version = reinterpret_cast<GetZMenuVersionFn>(
      reinterpret_cast<void*>(GetProcAddress(module, kZMenuProcName)));
  if (!get_version) return ZMenuGuardResult::export_absent;
  version = 0;
  std::uint32_t second = 0;
  (void)get_version(&version, &second);  // the original ignores the result
  return zmenu_version_accepted(version) ? ZMenuGuardResult::version_ok
                                         : ZMenuGuardResult::version_rejected;
}

std::size_t register_menu_options_win() noexcept {
  const HMODULE menu = GetModuleHandleA("IVMenuAPI.asi");
  if (!menu) return 0;
  using AddOptionFn = int(__cdecl*)(const char*, const wchar_t*, const char*,
                                    const char*, int, int, int, int);
  const auto add =
      reinterpret_cast<AddOptionFn>(
          reinterpret_cast<void*>(GetProcAddress(menu, "AddIVMenuOption")));
  if (!add) return 0;
  std::size_t count = 0;
  std::size_t total = 0;
  const MenuRegistration* rows = menu_registrations(count);
  for (std::size_t i = 0; i < count; ++i) {
    const MenuRegistration& row = rows[i];
    const int result = add(row.family, row.title, row.display, row.pref, row.a, row.b,
                           static_cast<int>(row.getter), static_cast<int>(row.setter));
    if (result) ++total;
  }
  return total;
}

#else

ZMenuGuardResult zmenu_version_guard(std::uint32_t& version) noexcept {
  version = 0;
  return ZMenuGuardResult::module_absent;
}

std::size_t register_menu_options_win() noexcept { return 0; }

#endif

}  // namespace zpatch
