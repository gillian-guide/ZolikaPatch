#pragma once
// Reimplemented Windows bodies for the SkipLauncher hook installs. The
// resolver feeds apply_skip_launcher; the ROM message-window service starts
// lazily from rom_reset (the original 0x10004570 -> 0x100043D0 path).

#include <cstdint>

#include "zpatch/skip_launcher.hpp"

#if defined(_WIN32)

namespace zpatch {

// Records the CreateFile IAT slot captured from the game (0 disables the
// forwarding and the hook calls CreateFileA directly).
void skip_launcher_win_set_create_file_slot(std::uint32_t slot_va) noexcept;

// Resolves a SkipHook to the address of the reimplemented body. Returns 0 for
// unknown hooks.
std::uintptr_t skip_launcher_win_resolve(SkipHook hook, void* user) noexcept;

}  // namespace zpatch

#endif  // _WIN32
