#pragma once
// ABI-exact Windows bodies for the recovered hook destinations, used by the
// live apply path. Each body mirrors the original trampoline's calling
// convention and register contract instruction-for-instruction (verified from
// the disassembly; see the reference VAs in the comments). The portable
// equivalents in hook_bodies.hpp stay the unit-tested behavioural models; this
// layer exists so the game can jump/call into the rebuilt module safely.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "zpatch/status.hpp"

#if defined(_WIN32)

namespace zpatch {

// Maps a recovered reference destination VA (e.g. 0x10005590) to its ABI-exact
// body. Returns 0 when the destination has no live-ready body yet, in which
// case the installer skips that jump and reports it.
std::uintptr_t hook_bodies_win_resolve(std::uint32_t reference_va, void* user) noexcept;

// Number of reference VAs with a live-ready body (for reporting).
std::size_t hook_bodies_win_count() noexcept;

// Debug crash probe. When armed for one option (from the [Debug] CrashProbe INI
// key), the resolver serves a probe body for that option's destination instead
// of the normal one. The probe body forces the crash condition the fix guards
// against (a null object, the 0xDDDDDDDD / 0x1FFFDF sentinels, a bad blackscreen
// value) and then runs the normal body, so the fix's recovery path is exercised
// live. With the fix disabled the forced condition should fault, which is the
// differential proof. Off by default; returns true when the option was
// recognized. The probe logs each hit via zpatch_probe_hit.
bool hook_bodies_win_set_probe(std::string_view option) noexcept;

// The AddOptionsInMenu integration (original loader tail): when IVMenuAPI.asi is
// loaded and AddOptionsInMenu is on, the loader registers the Death Music and
// Pause On Focus Loss toggles through AddIVMenuOption and installs the
// RestoreDeathMusic hooks even with the INI key off. The callbacks read/write
// the same option flags the INI sets.
extern "C" void zpatch_menu_prime_focus_loss() noexcept;
extern "C" std::uint8_t abi_menu_death_get() noexcept;
extern "C" void abi_menu_death_set(std::uint32_t, std::uint32_t) noexcept;
extern "C" std::uint8_t abi_menu_focus_get() noexcept;
extern "C" void abi_menu_focus_set(std::uint32_t, std::uint32_t) noexcept;

// LocalSettingsPath: captures the CWD and arms the four-byte call slot the
// retargeted game call goes through. The loader calls this before installing
// when the option is enabled (the original has a static constructor for it).
extern "C" void zpatch_local_settings_capture_cwd() noexcept;

// OutOfCommissionFix offline self-test: decodes the embedded "finale2.sco"
// blob, checks the OOC Adler-32 trailer, builds the memory-mount RPF archive and
// verifies the golden layout/hash. Used by the native tests; no game needed.
extern "C" bool zpatch_ooc_selftest() noexcept;

// Fault probes for the de-guarded helpers (fault-inclusive policy, 2026-10-16).
// Each named probe forces the condition the removed fault-avoidance guard used
// to swallow and then runs the actual de-guarded code, which must fault
// (the process dies at the instruction the original faults at; see the
// PRESERVED-FAULT catalogue in docs/phase4/fix_report.md). Returns 1 when the
// name is unknown and did not run; never returns for a known probe whose fault
// fires. Kinds: fchs-null-lookup, fchs-null-object, garage-null-lookup,
// gfwl-zero-base, focus-loss-null-site, vehicle-component-null-registry.
extern "C" int zpatch_fault_probe(const char* name) noexcept;

// NewMemorySystem: the reimplemented CRageAllocator methods (vtable[2]/[3]/[18]
// of the original 0x100B5038) and the vtable the E8 body writes into the game's
// allocator singleton. The other entries are disassembly-transcribed stubs.
extern "C" void* __stdcall zpatch_nms_alloc(std::uint32_t size, std::uint32_t align,
                                            std::uint32_t flags) noexcept;
extern "C" void __stdcall zpatch_nms_free(void* ptr) noexcept;
extern "C" std::uint8_t __stdcall zpatch_nms_contains(std::uint32_t key) noexcept;
extern "C" std::uintptr_t zpatch_nms_vtable[24];

}  // namespace zpatch

#endif  // _WIN32
