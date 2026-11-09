#pragma once
#include <cstddef>
#include <cstdint>
#include "zpatch/patch.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// A minimal in-memory view of the game image as it looks when mapped, so
// recovered patch operations (signature-relative and module-RVA) can be applied
// to a caller-owned mapped copy. This adapter performs no OS calls: mapping,
// protection and writing are the caller's responsibility.
struct MappedImage final {
  std::byte* base{};        // start of the mapped image (image base)
  std::size_t size{};
};

// Restores a captured preimage to `site`, mirroring apply_mapped_operation_capture.
// The write is bounded by the supplied length; no verification of the current
// bytes is performed (so rollback is the exact inverse of apply).
Status restore_mapped_bytes(std::byte* site, const std::byte* original,
                            std::size_t length) noexcept;

// Applies one recovered write operation to a mapped image:
//  - signature ops (module_rva == 0): scan for `signature`, compute
//    match + offset (offset may be negative) and write `replacement`.
//  - rva ops (module_rva != 0): write `replacement` at base + module_rva.
// Expected-byte verification is intentionally NOT performed, matching the
// original handlers. Returns a status describing what happened.
Status apply_mapped_operation(MappedImage image, std::string_view signature,
                              std::int32_t offset, std::uint32_t module_rva,
                              std::string_view replacement,
                              std::byte*& written_at, std::size_t& written) noexcept;

// As above, but when `original_out` is non-null the bytes that were overwritten
// are copied into it first (up to `original_capacity`, at least `written` bytes).
// This is the preimage a rollback needs; the original mod never records one, so
// this is an explicit reconstruction aid, not borrowed behaviour. When `protect`
// is supplied the page is made writable around the store, exactly like every
// original handler's VirtualProtect call (live code pages are read-only); the
// test harness passes nullptr over its writable mapping.
Status apply_mapped_operation_capture(MappedImage image, std::string_view signature,
                                      std::int32_t offset, std::uint32_t module_rva,
                                      std::string_view replacement,
                                      std::byte*& written_at, std::size_t& written,
                                      std::byte* original_out,
                                      std::size_t original_capacity,
                                      ProtectFn protect = nullptr) noexcept;

}  // namespace zpatch
