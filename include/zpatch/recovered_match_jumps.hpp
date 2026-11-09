#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include "zpatch/game_image.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// A recovered Nth-match jump install: the handler collects ALL matches of
// `signature` (multi-hit collector 0x1005afa0) and writes E9 + rel32 at
// matches[match_index] + offset, targeting a plugin trampoline. Unlike the
// single-hit signature jump, the same signature maps to several sites, so the
// match index is part of the operation. See tools/extract_match_jumps.py.
struct RecoveredMatchJump final {
  std::string_view option;
  std::string_view signature;
  std::uint32_t match_index;   // 0-based index into the match list
  std::int32_t offset;         // site = matches[match_index] + offset
  std::uint32_t destination;   // absolute target (plugin trampoline VA)
  std::uint32_t opcode;        // 0xE9 = relative jmp, 0xE8 = relative call
};

const RecoveredMatchJump* recovered_match_jumps(std::size_t& count) noexcept;

// Applies every recovered Nth-match jump whose option passes the recovered build
// gate for `build_ordinal`. Scans for all matches once per unique signature,
// writes E9 + rel32 (using `module_base_va` for the site VA) and reports each
// written site. Returns the number applied via `applied`.
Status apply_recovered_match_jumps(MappedImage image, std::uintptr_t module_base_va,
                                   std::uint8_t build_ordinal,
                                   std::size_t& applied) noexcept;

// As above, but also appends one AppliedWrite (address + preimage) per written
// site to `log`, so the runtime apply/rollback path can undo them. `log` is the
// runtime's AppliedWriteLog (declared in runtime.hpp); kept as a template-free
// callback signature to avoid a header cycle - the caller passes a small functor.
struct MatchWriteRecord final { std::byte* address; std::uint32_t length; std::byte original[8]; };
Status apply_recovered_match_jumps_recorded(MappedImage image, std::uintptr_t module_base_va,
                                            std::uint8_t build_ordinal,
                                            std::size_t& applied,
                                            std::vector<MatchWriteRecord>& records) noexcept;

}  // namespace zpatch
