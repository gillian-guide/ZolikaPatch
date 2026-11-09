#pragma once
// Recovered module-rva installs, per game build.
//
// Some handlers patch the game with writes that the literal op table cannot
// express: E9/E8 jumps whose displacement is computed at run time from the
// module base, NOP fills over fixed ranges, and slot publishes that store a
// game address into a plugin global. They were recovered from the handlers'
// decompiled build branches (see tools/extract_rva_installs.py); every row
// carries the branch's build ordinal and is applied only on that build.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "zpatch/config.hpp"
#include "zpatch/game_image.hpp"
#include "zpatch/patch.hpp"
#include "zpatch/runtime.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

struct RvaInstall final {
  std::string_view option;
  std::uint32_t site_rva;    // 0 for slot publishes
  std::uint8_t opcode;       // 0xE9 / 0xE8 / 0x90 (fill)
  std::uint8_t kind;         // 0 = jump (game rva) 1 = jump (module va) 2 = fill 3 = slot
  std::uint8_t build;        // the original's build ordinal; 0 = any build
  std::uint32_t operand;     // destination rva / module VA / fill length / slot rva
  std::uint32_t reserved;
  std::string_view slot_name;  // kind 3: the original global's name
};

const RvaInstall* recovered_rva_installs(std::size_t& count) noexcept;

struct RvaInstallReport final {
  std::size_t jumps{};
  std::size_t fills{};
  std::size_t slots{};
  std::size_t skipped_no_destination{};  // module destination without a body
  std::size_t skipped_no_slot{};         // slot without a rebased global
  std::size_t gated_out{};               // the option is disabled in the INI
  std::size_t anchor_missing{};          // the handler's first-scan anchor is absent
};

// Applies the recovered installs whose build matches `build` (a row with
// build 0 applies to every build). Kind-1 destinations resolve through
// `resolve`; a zero result skips that install. Kind-3 slots write
// `game_module_base + operand` into the rebased global when one exists
// (rebased_slot_for), else they are skipped. Options disabled in the INI are
// never applied.
Status apply_recovered_rva_installs(const ConfigDocument& config, MappedImage image,
                                    std::uintptr_t game_module_base, ProtectFn protect,
                                    AppliedWriteLog* log, RuntimeDestinationResolver resolve,
                                    void* resolver_user, std::uint8_t build,
                                    RvaInstallReport& report) noexcept;

// True when an install for this build covers the given module rva (the literal
// op table's rva row for the same site must then be skipped).
bool rva_install_covers(std::uint32_t site_rva, std::uint8_t build) noexcept;

}  // namespace zpatch
