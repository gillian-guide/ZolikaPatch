#pragma once
#include <cstddef>
#include <cstdint>
#include "zpatch/game_image.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// A recovered module-RVA relative-jump install: the handler writes the opcode
// (E9 jmp or E8 call) + rel32 at the game's module base + `module_rva`, targeting
// `destination` (a plugin trampoline). The displacement cannot be stored as
// literal bytes because it is computed from the runtime module base, which is why
// these ops are separate from the literal-write table. See tools/extract_rva_jumps.py.
struct RecoveredRvaJump final {
  std::string_view option;
  std::uint32_t module_rva;      // site = module_base + module_rva
  std::uint8_t opcode;           // 0xE9 jmp / 0xE8 call
  std::uint32_t destination;     // absolute jump target (plugin trampoline VA)
  std::uint32_t handler;         // handler the op was recovered from
};

const RecoveredRvaJump* recovered_rva_jumps(std::size_t& count) noexcept;

// Applies one recovered RVA jump to a caller-owned mapped image.
//
// `module_base_va` is the virtual address the mapped image represents (the value
// GetModuleHandleA(0) returns - the *game* module, not the buffer pointer). The
// handler computes the site as game_base + rva and the displacement as
// destination - (game_base + rva) - 5, so the file offset of the write comes from
// the buffer while the displacement arithmetic uses module_base_va. This keeps the
// output byte-identical to the live handler on any host (32- or 64-bit, any heap
// address), instead of depending on where the test's buffer happens to be.
// Applies one recovered RVA jump: opcode + rel32 at module_base_va + module_rva.
// When `protect` is supplied the code page is made writable around the store,
// exactly like the original handler's VirtualProtect call.
Status apply_rva_jump(MappedImage image, std::uintptr_t module_base_va,
                      const RecoveredRvaJump& jump, std::byte*& written_at,
                      ProtectFn protect = nullptr) noexcept;

// Applies every recovered RVA jump whose option passes the recovered build gate
// for `build_ordinal`. Returns the number applied via `applied`.
Status apply_recovered_rva_jumps(MappedImage image, std::uintptr_t module_base_va,
                                 std::uint8_t build_ordinal,
                                 std::size_t& applied) noexcept;

}  // namespace zpatch
