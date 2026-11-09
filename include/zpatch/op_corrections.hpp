#pragma once
// Curated corrections for mis-extracted op rows.
//
// The Ghidra-era extractor mis-split several handlers' multi-write patches
// (wrong offsets, split operands, rows from the wrong expression). Applying
// those rows corrupted the game - the ImprovedShaderStreaming savefile crash.
// evidence/op_corrections.json documents each correction (verified against the
// handler decompilation by tools/apply_op_corrections.py) and this module
// exposes:
//   - suppressions: op rows that must never be applied;
//   - sig installs: the handler's real writes (full E9/E8 with a module or
//     match-relative destination).

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "zpatch/config.hpp"
#include "zpatch/game_image.hpp"
#include "zpatch/patch.hpp"
#include "zpatch/runtime.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

struct OpSuppression final {
  std::uint32_t handler;
  std::string_view signature;
  std::int32_t offset;
  std::string_view values;  // uppercase, space separated
};

struct SigInstall final {
  std::string_view option;
  std::string_view signature;
  std::int32_t site_offset;
  std::uint8_t opcode;     // 0xE9 / 0xE8
  std::uint8_t dest_kind;  // 0 = module VA (resolved), 1 = match-relative,
                           // 2 = match_branch: the original 6-byte conditional
                           //     branch's target (match + site_offset + 6 + rel32);
                           // 3 = rebased_global: write rebased_value_for(option)
                           //     as a 4-byte value at match + site_offset;
                           // 4 = literal_byte: write the single byte `dest` at
                           //     match + site_offset (no displacement);
                           // 5 = plugin_data_ptr: write the ADDRESS named by
                           //     dest_symbol (rebased_symbol_for) as a 4-byte
                           //     value at match + site_offset
  std::uint32_t dest;
  std::uint8_t site_deref;  // 1 = the site is *(match + site_offset) (a pointer)
  std::string_view dest_symbol;  // dest_kind 5 only
  // dest_kind 6 = literal_bytes: write the byte string `bytes` at
  // match + site_offset inside the same patch-set pass (MouseFix's in-pattern
  // opcode rewrites: 90 90 / 66 0F 6E C0). Written atomically with the set so
  // the scan happens once, before any byte inside the pattern is overwritten.
  std::string_view bytes;
};

const OpSuppression* op_suppressions(std::size_t& count) noexcept;
const SigInstall* sig_installs(std::size_t& count) noexcept;

// A handler's first-scan anchor. The original handlers scan one "guard"
// signature first and wrap their whole write set in `if (result)`: FreezeCarFix
// (0x10037C70), SMPA60Fix (0x100467A0) and NewSavesCompatibility (0x10052200)
// jump to their epilogue when that scan misses, so none of their rows (whatever
// signature they land on) may apply. The rebuilt passes run the rows in table
// order across several stages, so the anchor is consulted per row: a miss means
// the original handler would have left the image untouched (finding N1:
// 36 over-applied install sites on 1000/1004/1040).
struct OpAnchor final {
  std::string_view option;
  std::string_view signature;
};

const OpAnchor* op_anchors(std::size_t& count) noexcept;

// True when the option has no anchor, or the anchor signature is found in the
// image (the original's first scan hit, so its writes proceed).
bool op_anchor_ok(MappedImage image, std::string_view option) noexcept;

// A plain byte write at a signature match offset (the corrected form of a
// mis-offset op-table row, e.g. InfiniteCarBottomDistance's EB at +3).
// `fallback_signature` (optional) reproduces the original's two-scan sequence
// (CutsceneFixes chain A: the hidden worker scans the first pattern, and only
// when it has no match resets the text and scans the second): it is scanned
// only when the primary signature produced no match, and the write then lands
// on its match.
struct SigWrite final {
  std::string_view option;
  std::string_view signature;
  std::int32_t site_offset;
  std::string_view bytes;  // uppercase, space separated
  std::uint32_t match_index{0};
  std::string_view fallback_signature;  // empty = no fallback
};

const SigWrite* sig_writes(std::size_t& count) noexcept;

struct SigJumpReport final {
  std::size_t applied{};
  std::size_t missing{};
  std::size_t skipped_no_destination{};
  std::size_t gated_out{};
  std::size_t anchor_missing{};  // the option's handler first-scan anchor is absent
};

// Installs the recovered signature-relative jumps (E9 at match + offset) to
// their resolved destinations. Runs after the slot publishes so a body's
// published resume/value globals still hold the game's original addresses.
Status apply_sig_jump_installs(const ConfigDocument& config, MappedImage image,
                               std::uintptr_t game_module_base, ProtectFn protect,
                               AppliedWriteLog* log, RuntimeDestinationResolver resolve,
                               void* resolver_user, SigJumpReport& report) noexcept;

struct SigWriteReport final {
  std::size_t applied{};
  std::size_t missing{};
  std::size_t gated_out{};  // the option is disabled in the INI
  std::size_t anchor_missing{};  // the option's handler first-scan anchor is absent
};

Status apply_sig_writes(const ConfigDocument& config, MappedImage image, GameBuild build,
                        ProtectFn protect, AppliedWriteLog* log, SigWriteReport& report) noexcept;

// A recovered slot publish anchored on a signature match: the handler stores
// the match address (kind 0) or `*(match + field_offset) + match + add` (kind 1)
// into one of its globals. The rebuilt module writes the same value into its
// rebased global. `match_index` selects among the matches the handler's
// multi-hit collector (0x1005afa0) found (0 = first; vector-based handlers that
// use vector[1] set 1). kind 3 ("rva") publishes `image base + add` with no
// signature (the handler's `dword_1015AD70 + const` form); `build` gates it to a
// specific game build (0 = any) because the constants differ per build.
struct SlotInstall final {
  std::string_view option;
  std::string_view signature;
  std::string_view slot_name;
  std::uint8_t kind;        // 0 = match, 1 = match_field, 2 = call_target_deref,
                            // 3 = rva, 4 = ptr_site_field, 5 = match_deref
  std::int32_t field_offset;      // may be negative (a field before the match)
  std::int32_t add;               // may be negative
  std::int32_t site_field_offset; // kind 4: read the site pointer at match + this
  std::uint32_t match_index;  // 0-based index into the match list
  std::uint8_t build;         // 0 = any build, else the required GameBuild value
};

const SlotInstall* slot_installs(std::size_t& count) noexcept;

struct SlotInstallReport final {
  std::size_t applied{};
  std::size_t missing{};
  std::size_t skipped_no_slot{};  // the global has not been rebased yet
  std::size_t gated_out{};        // the option is disabled in the INI
  std::size_t anchor_missing{};   // the option's handler first-scan anchor is absent
};

Status apply_slot_installs(const ConfigDocument& config, MappedImage image, GameBuild build,
                           ProtectFn protect, AppliedWriteLog* log,
                           SlotInstallReport& report) noexcept;

// True when the op row (handler, signature, offset, replacement) is suppressed.
bool op_row_suppressed(std::uint32_t handler, std::string_view signature, std::int32_t offset,
                       std::string_view replacement) noexcept;

struct SigInstallReport final {
  std::size_t applied{};
  std::size_t missing{};            // signature not found
  std::size_t skipped_no_destination{};  // module destination without a body
  std::size_t gated_out{};          // the option is disabled in the INI
  std::size_t anchor_missing{};     // the option's handler first-scan anchor is absent
};

// Applies the sig installs: scans each signature, writes the opcode and its
// displacement at match + site_offset (module destinations resolve through
// `resolve`; a zero result skips the install). Entries whose option is disabled
// in the INI are never written.
Status apply_sig_installs(const ConfigDocument& config, MappedImage image, GameBuild build,
                          std::uintptr_t game_module_base, ProtectFn protect,
                          AppliedWriteLog* log, RuntimeDestinationResolver resolve,
                          void* resolver_user, SigInstallReport& report) noexcept;

}  // namespace zpatch
