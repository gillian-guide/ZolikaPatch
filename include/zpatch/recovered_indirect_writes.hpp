#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "zpatch/game_image.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// A recovered indirect-literal write: the handler scans for `signature`, reads a
// pointer from the dword at `match + pointer_offset`, and writes `value`
// (`width` bytes, little-endian) through that pointer, inside a VirtualProtect
// bracket. The literal op table cannot express the indirection and the jump tables
// cannot express a non-jump write, so this is its own op kind.
// See tools/extract_indirect_writes.py.
struct RecoveredIndirectWrite final {
  std::string_view option;
  std::string_view signature;
  std::int32_t pointer_offset;   // pointer = *(uint32*)(match + pointer_offset)
  std::uint32_t width;           // 1/2/4 bytes
  std::uint32_t value;           // written little-endian at the pointer
};

const RecoveredIndirectWrite* recovered_indirect_writes(std::size_t& count) noexcept;

// Applies one recovered indirect write to a caller-owned mapped image: scans for
// the signature, dereferences the pointer word, and writes the value. The target
// must be inside the image (the indirect pointer is otherwise out of this
// reconstruction's reach). `module_base_va` is the value GetModuleHandleA(0)
// returns for the game module.
Status apply_indirect_write(MappedImage image, std::uintptr_t module_base_va,
                            const RecoveredIndirectWrite& op,
                            std::byte*& written_at, ProtectFn protect = nullptr) noexcept;

// Applies every recovered indirect write whose option passes the recovered build
// gate for `build_ordinal`. Returns the number applied via `applied`.
Status apply_recovered_indirect_writes(MappedImage image, std::uintptr_t module_base_va,
                                       std::uint8_t build_ordinal,
                                       std::size_t& applied) noexcept;

}  // namespace zpatch
