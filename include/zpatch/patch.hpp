#pragma once

#include <cstddef>

#include "zpatch/game.hpp"
#include "zpatch/status.hpp"

namespace zpatch {
struct MutableMemoryView final { std::byte* base{}; std::size_t size{}; };
struct ByteView final { const std::byte* data{}; std::size_t size{}; };

// Guarded, bounded byte replacement for caller-owned storage. The write occurs
// only when the explicit, known build matches and the entire expected sequence matches. This API does not change page
// protection, scan processes/modules, or expose unguarded game-memory writes.
Status apply_guarded_patch(GameBuild actual_build, GameBuild required_build,
                           MutableMemoryView target, std::size_t offset,
                           ByteView expected, ByteView replacement) noexcept;
// Rollback is equally guarded: bytes must still equal replacement.
Status rollback_guarded_patch(GameBuild actual_build, GameBuild required_build,
                              MutableMemoryView target, std::size_t offset,
                              ByteView expected, ByteView replacement) noexcept;
// FUN_10001680: fill a caller-owned range with 0x90. No page-protection change;
// the caller owns any executable-memory protection concerns.
Status fill_nop(MutableMemoryView target, std::size_t offset,
                std::size_t length) noexcept;

// Relative JMP patch corresponding to the installer near 0x10004110: write E9
// followed by a 32-bit little-endian displacement from the patch site. Only
// caller-owned storage is modified; displacement is computed as target-site-minus-
// origin plus the five-byte instruction length, matching the observed installer.
Status install_relative_jump(MutableMemoryView target, std::size_t offset,
                             const std::byte* destination) noexcept;
// Protected single-value writes corresponding to FUN_1005ad60 (1 byte),
// FUN_1005adb0 (2 bytes) and FUN_1005b650 (4 bytes). Each sets PAGE_EXECUTE_
// READWRITE (0x40) around the store, then restores the previous protection.
// Page-protection calls are supplied by the caller so this stays testable and
// never touches its own executable pages.
using ProtectFn = Status (*)(void* address, std::size_t size, std::uint32_t new_protect,
                             std::uint32_t* previous);
Status protected_write(std::byte* address, const std::byte* value, std::size_t size,
                       ProtectFn protect) noexcept;

// Guarded application that writes through the recovered protection-toggle path.
// Same build and expected-byte gates as apply_guarded_patch, but each byte goes
// through the caller-supplied protection callback (as FUN_1005ad60 would).
Status apply_protected_patch(GameBuild actual_build, GameBuild required_build,
                             std::byte* target, std::size_t offset,
                             ByteView expected, ByteView replacement,
                             ProtectFn protect) noexcept;

}  // namespace zpatch
