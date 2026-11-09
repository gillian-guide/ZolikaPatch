#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "zpatch/game_image.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// A recovered signature-relative jump install: the handler scans for `signature`,
// adds `offset` to the match, writes E9 + rel32, and the rel32 targets a plugin
// trampoline (`destination`). The literal op table records only the fixed E9 byte
// for these, so this completes them (E9 + the rel32 the handler computes).
// See tools/extract_sig_jumps.py.
struct RecoveredSigJump final {
  std::string_view option;
  std::string_view signature;
  std::int32_t offset;        // site = match + offset
  std::uint32_t destination;  // absolute jump target (plugin trampoline VA)
};

const RecoveredSigJump* recovered_sig_jumps(std::size_t& count) noexcept;

// Applies one recovered signature jump to a caller-owned mapped image:
// scans for the signature, then writes E9 + rel32 (using `module_base_va` for the
// site's virtual address, as the handler does with the live module base).
Status apply_sig_jump(MappedImage image, std::uintptr_t module_base_va,
                      const RecoveredSigJump& jump, std::byte*& written_at) noexcept;

// Applies every recovered signature jump whose option passes the recovered build
// gate for `build_ordinal`. Returns the number applied via `applied`.
Status apply_recovered_sig_jumps(MappedImage image, std::uintptr_t module_base_va,
                                 std::uint8_t build_ordinal,
                                 std::size_t& applied) noexcept;

}  // namespace zpatch
