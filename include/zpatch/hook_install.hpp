#pragma once
// Installer side of the reimplemented hook bodies: writes the recovered E9/E8
// jump installs into a caller-owned mapped game image, but resolves each
// destination through the reimplemented body table instead of the original
// plugin trampoline VAs.
//
// The original handlers write `E9 + rel32` (or `E8 + rel32` for the two
// BorderlessWindowed call installs) at the recovered site and let the rel32
// point into their own module. A rebuilt ASI does the same with its own bodies;
// because the displacement is computed from the runtime module base, nothing
// here depends on where either module is loaded.
//
// Nothing in this module touches a live process: the caller owns the image and
// supplies the page-protection callback.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "zpatch/game.hpp"
#include "zpatch/game_image.hpp"
#include "zpatch/patch.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// One recovered reference destination and the reimplemented body replacing it.
// Generated from evidence/hook_reimplementation_map.json by
// tools/ida/generate_hook_targets_inc.py.
struct RecoveredHookTarget final {
  std::uint32_t reference_va;      // original destination VA (e.g. 0x10005590)
  std::uintptr_t (*address)();     // address of the reimplemented body
  const char* option;              // first attributed option, for reporting
};

const RecoveredHookTarget* recovered_hook_targets(std::size_t& count) noexcept;

// Looks up a reference destination. Returns 0 when it has no reimplementation.
std::uintptr_t resolve_recovered_hook_target(std::uint32_t reference_va) noexcept;

// Resolves the address to install in place of a reference destination.
// Supplied by the caller so tests can substitute a synthetic layout; the
// default resolver reads recovered_hook_targets.
using HookDestinationResolver = std::uintptr_t (*)(std::uint32_t reference_va, void* user);
std::uintptr_t default_hook_resolver(std::uint32_t reference_va, void* user) noexcept;

enum class HookInstallStatus : std::uint8_t {
  installed,
  not_found,       // the site signature did not resolve
  gated_out,       // the recovered build gate excludes this build
  no_destination,  // the resolver had no reimplementation for the destination
  write_failed,    // the protection callback rejected the write
};

struct HookInstallOutcome final {
  std::string_view option;
  std::uint32_t reference_va{};
  std::uintptr_t resolved_va{};
  std::byte* site{};
  HookInstallStatus status{HookInstallStatus::not_found};
};

struct HookInstallReport final {
  std::size_t candidates{};
  std::size_t installed{};
  std::size_t not_found{};
  std::size_t gated_out{};
  std::size_t no_destination{};
  std::size_t write_failed{};
  std::vector<HookInstallOutcome> outcomes;
};

// Installs the recovered signature-relative E9 jumps (recovered_sig_jumps.hpp).
// `game_module_base` is the VA the mapped image represents (GetModuleHandleA(0)),
// used only for the rel32 arithmetic.
Status install_recovered_sig_jumps(GameBuild build, MappedImage image,
                                   std::uintptr_t game_module_base, ProtectFn protect,
                                   HookDestinationResolver resolve, void* resolver_user,
                                   HookInstallReport& report) noexcept;

// Installs the recovered module-RVA E9 jumps (recovered_rva_jumps.hpp): the site
// is image base + module_rva and needs no scan.
Status install_recovered_rva_jumps(GameBuild build, MappedImage image,
                                   std::uintptr_t game_module_base, ProtectFn protect,
                                   HookDestinationResolver resolve, void* resolver_user,
                                   HookInstallReport& report) noexcept;

// Installs the recovered Nth-match jumps (recovered_match_jumps.hpp), honoring
// the recovered opcode (E9 jump or E8 call) and match index.
Status install_recovered_match_jumps(GameBuild build, MappedImage image,
                                     std::uintptr_t game_module_base, ProtectFn protect,
                                     HookDestinationResolver resolve, void* resolver_user,
                                     HookInstallReport& report) noexcept;

// Convenience wrapper running all three installers into one report.
Status install_recovered_hooks(GameBuild build, MappedImage image,
                               std::uintptr_t game_module_base, ProtectFn protect,
                               HookDestinationResolver resolve, void* resolver_user,
                               HookInstallReport& report) noexcept;

}  // namespace zpatch
