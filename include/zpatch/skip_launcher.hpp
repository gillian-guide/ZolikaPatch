#pragma once
// SkipLauncher: the DRM/launcher bypass recovered from the original handler at
// 0x10045ce0 (see ZolikaPatch_analysis evidence and
// evidence/skip_launcher_ops.json). The handler patches ~25 sites in the game
// module found by signature, neutralising the launcher/SecuROM checks so the
// game can start without PlayGTAIV or XLivelessAddon (the INI documents it as
// "from XLivelessAddon but with some additional improvements").
//
// Provenance: the feature is ZolikaPatch's fork of XLivelessAddon
// (GTAmodding/XLivelessAddon, which carries no license). Everything here is
// written from the decompilation of ZolikaPatch's own handlers - XLivelessAddon
// was consulted only to understand what the recovered patches do; no code was
// copied from it. See CREDITS.md at the repository root.
//
// Two multi-hit collectors patch the first two matches of one pattern and the
// last two matches of another with "mov al,1; ret; nop". One entry installs a
// jump (33 C0 E9 rel32) to the match of another entry.
//
// Nothing here touches a live process: the caller owns the mapped image and the
// page-protection callback. Writes are appended to the runtime write log when
// one is supplied, so rollback restores the original bytes.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "zpatch/game_image.hpp"
#include "zpatch/patch.hpp"
#include "zpatch/runtime.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// A single store relative to a signature match. Exactly one of `bytes` /
// `fill` is used; `jump_to_entry` (when not kNoJumpTarget) writes a rel32 whose
// target is that entry's match, for the entry that installs `33 C0 E9 rel32`.
struct SkipLauncherWrite final {
  std::int32_t offset;
  std::uint16_t length;
  const std::uint8_t* bytes;  // null when fill is used
  std::uint8_t fill;          // e.g. 0x90 for NOP fills
  std::uint32_t jump_to_entry;
};

inline constexpr std::uint32_t kNoJumpTarget = 0xFFFFFFFFu;

struct SkipLauncherEntry final {
  std::string_view signature;
  const SkipLauncherWrite* writes;
  std::size_t write_count;
};

struct SkipLauncherCollector final {
  enum class Target : std::uint8_t { first_two, last_two };
  std::string_view pattern;
  Target target;
  const std::uint8_t* bytes;
  std::uint16_t length;
};

// Reimplemented hook bodies the installers jump to. The original installs E8/E9
// jumps from the game into its own module; a rebuilt ASI resolves these to its
// own bodies.
enum class SkipHook : std::uint8_t {
  dfa_init,           // 0x10004780: return 1
  create_file,        // 0x10004820: CreateFileA through the captured slot
  close_handle,       // 0x10004790: CloseHandle with an exception guard
  set_file_pointer,   // 0x10004760
  get_file_size_ex,   // 0x10004720
  read_file,          // 0x10004740
  rom_reset,          // 0x10004570: ROM window setup + object reset
  rom_set,            // 0x10004610: *(u32*)(a1+32) = *(u32*)(a1+28); return 1
};

using SkipHookResolver = std::uintptr_t (*)(SkipHook hook, void* user);

struct SkipLauncherInstall final {
  std::string_view signature;
  std::int32_t offset;
  std::uint8_t opcode;  // 0xE8 call or 0xE9 jump
  SkipHook hook;
};

struct SkipLauncherCollectedTarget final {
  std::string_view target_signature;
  std::uint32_t index;  // collected match index the jump is installed at
};

struct SkipLauncherCollectedInstall final {
  std::string_view collector;
  const SkipLauncherCollectedTarget* targets;
  std::size_t target_count;
};

// Generated tables (src/recovered_skip_launcher.inc).
const SkipLauncherEntry* skip_launcher_entries(std::size_t& count) noexcept;
const SkipLauncherCollector* skip_launcher_collectors(std::size_t& count) noexcept;
const SkipLauncherInstall* skip_launcher_dfa_installs(std::size_t& count) noexcept;
const SkipLauncherInstall* skip_launcher_rom_installs(std::size_t& count) noexcept;
const SkipLauncherCollectedInstall* skip_launcher_dfa_collected(std::size_t& count) noexcept;

// The entry whose match feeds the jump install (the original reads the second
// entry's match into its jump-target slot).
inline constexpr std::uint32_t kSkipLauncherJumpTargetEntry = 1;

struct SkipLauncherReport final {
  std::size_t entries_found{};
  std::size_t entries_missing{};
  std::size_t writes{};
  std::size_t collectors_found{};
  std::size_t collector_writes{};
  bool jump_installed{};
  std::size_t dfa_installs{};       // hook jumps written by the DFA installer
  std::size_t dfa_missing{};        // DFA signatures absent this build
  std::size_t rom_installs{};       // hook jumps written by the ROM installer
  std::size_t rom_missing{};
  std::size_t collected_installs{}; // DFA collected-site jumps
  std::uint32_t create_file_slot{}; // captured IAT slot VA (0 when absent)
};

// The three installable stages, matching the original's call structure:
// 0x10045CE0 runs the main writes and then the DFA installer (0x10004850)
// inside its XLivelessAddon guard, while the ROM service (0x10004620) is
// called after it unconditionally; the MiscFixes handler calls the DFA
// installer again on its own (10024018). The DFA installer carries its own
// guards (XLivelessAddon flag, GTAC.dll, EFLC builds, a one-shot flag).
enum class SkipLauncherStage : std::uint8_t {
  main = 1,
  dfa = 2,
  rom = 4,
  all = 7,
};
constexpr SkipLauncherStage operator|(SkipLauncherStage a, SkipLauncherStage b) noexcept {
  return static_cast<SkipLauncherStage>(static_cast<std::uint8_t>(a) |
                                        static_cast<std::uint8_t>(b));
}
constexpr bool has_stage(SkipLauncherStage set, SkipLauncherStage stage) noexcept {
  return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(stage)) != 0;
}

// Applies the recovered SkipLauncher patches selected by `stages` to `image`.
// The DFA and ROM hook installs additionally need a hook resolver (without one
// those stages are skipped and counted as missing). `log` (optional) receives
// the preimages for rollback; long fills are recorded in 64-byte chunks,
// matching the runtime log's entry size. Missing signatures are reported, never
// guessed - exactly like the original handlers.
Status apply_skip_launcher(MappedImage image, ProtectFn protect, AppliedWriteLog* log,
                           SkipHookResolver resolve, void* resolver_user,
                           SkipLauncherReport& report,
                           SkipLauncherStage stages = SkipLauncherStage::all) noexcept;

}  // namespace zpatch
