// ABI-exact Windows bodies for recovered hook destinations. Every convention
// and argument layout below was read off the original trampoline's disassembly
// (addresses in the comments); where a C declaration cannot express the
// contract (register arguments, callee-popped frames) the body is a naked
// function with the original instructions.
//
// The portable models in hook_bodies.cpp remain the tested reference for
// behaviour; this file is the live layer the game actually enters.
#include "zpatch/hook_bodies_win.hpp"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlwapi.h>

#include <cstddef>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <xmmintrin.h>
#include <cstring>
#include <new>
#include <string>
#include <unordered_set>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

#include "zpatch/game.hpp"
#include "zpatch/log.hpp"
#include "zpatch/model_catalog.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/rebased_values.hpp"
#if defined(_MSC_VER) && !defined(_WIN64)
#include "zpatch/driveby_seh.hpp"
#endif

#if defined(ZP_PROBE_TRACE)
// Diagnostic build only (tools/build_asi_probe_trace.cmd): the tracing wrappers
// below (same anonymous namespace as the real bodies) call these loggers. Not
// compiled into the shipped ASI.
extern "C" {
void zpatch_ptrace_probe_in(std::uint32_t, std::uint32_t);
void zpatch_ptrace_probe_out(std::uint32_t, std::uint32_t, std::uint32_t);
void zpatch_ptrace_guard_in(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
void zpatch_ptrace_guard_out(std::uint32_t, std::uint32_t);
}
#endif

// The original ASI's compressed "finale2.sco" resource (OutOfCommissionFix's
// payload); generated from game_refs/ZolikaPatch_original.asi.off.
#include "recovered_ooc_pak.inc"

// Debug crash-probe state. The probe bodies reference these from inline asm, so
// they need C linkage at global scope (the inline assembler rejects a function
// symbol that is only declared inside a namespace). Inert unless
// hook_bodies_win_set_probe arms a probe from the [Debug] CrashProbe INI key.
extern "C" {
std::uintptr_t zpatch_probe_target = 0;  // the normal body the probe falls through to
std::uint32_t zpatch_probe_hits = 0;
// Fault-probe pseudo garage object (the garage lookup reads its hash bytes).
std::uint8_t zpatch_probe_garage_object[128] = {};
void zpatch_probe_hit() noexcept {
  ++zpatch_probe_hits;
  if (zpatch_probe_hits <= 5 || (zpatch_probe_hits % 1000u) == 0)
    zpatch::log_printf("CrashProbe: fired (hit #%u)", zpatch_probe_hits);
}

// PlayerAsPedComponentsFix helper (original sub_1003D930): look up the "player"
// model index and store 1 in zpatch_player_ped_computed when the object's model
// (+0x2e) matches, else 0. The shim stores the object (eax) first. C linkage so
// the shim's inline asm can call it by name.
static void zpatch_cookie_tail() noexcept;  // defined with the cookie shim

std::int32_t abi_player_ped_helper() noexcept {
  const std::uint32_t hash = zpatch::game_hash("player");
  std::int32_t index = -1;
  const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
      static_cast<std::uintptr_t>(zpatch::zpatch_model_lookup));
  if (lookup == nullptr) {
    // Fault-preserving: the original's 0x1003D960 `call dword_10159C0C` faults
    // (guard removed 2026-10-16).
    zpatch::log_printf("guard-removed option=PlayerAsPedComponentsFix lookup=null "
                       "fault=0x1003D960");
  }
  zpatch::zpatch_player_ped_computed = 0;  // original writes 0 before the lookup
  lookup(hash, &index);
  const auto* object = reinterpret_cast<const std::uint8_t*>(
      static_cast<std::uintptr_t>(zpatch::zpatch_player_ped_object));
  const std::int16_t model = *reinterpret_cast<const std::int16_t*>(object + 0x2e);
  zpatch::zpatch_player_ped_computed = (index == model) ? 1u : 0u;
  // Original tail: EDX=1 (the cmove source), ECX=cookie via the /GS check, and
  // EAX = the object's model (the movsx eax,[eax+2eh] result the resume sees).
#if defined(_MSC_VER) && !defined(_WIN64)
  __asm { mov edx, 1 }
#endif
  zpatch_cookie_tail();
  return model;
}

// EmissiveLerpFix has no compiled helpers: the original tramp (sub_1001AF20)
// is pure integer math and its SSE tail must run in a register-exact body that
// touches only xmm0 (any compiled helper may clobber xmm1..xmm7, which the game
// keeps live across the site). The body below inlines the integer math and
// emits the cvtsi2ss/mulss bytes directly.

// SuperLODFix helper (original sub_100469A0): looks up the model index for the
// hash 0xAE4B15D6 via the handler-published lookup (dword_101580B4) and reports
// whether the object's model (+0x2e) matches. The object is the single stack
// argument the shim pushes.
bool abi_super_lod_helper(const void* object) noexcept {
  std::int32_t index = -1;
  const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
      static_cast<std::uintptr_t>(zpatch::zpatch_super_lod_lookup));
  if (lookup == nullptr) {
    // Fault-preserving: sub_100469A0's `call dword_101580B4` faults (guard
    // removed 2026-10-16).
    zpatch::log_printf("guard-removed option=SuperLODFix lookup=null fault=0x100469B9");
  }
  lookup(0xAE4B15D6u, &index);
  const auto* o = static_cast<const std::uint8_t*>(object);
  const std::int16_t model = *reinterpret_cast<const std::int16_t*>(o + 0x2e);
  return model == index;
  zpatch_cookie_tail();
}

// HighFPSBikePhysicsFix helper (original sub_1001EF30): if the stored value is
// below the 0.006666666665 constant, clamp it to 0x3BDA740E. SSE in the
// original, so this is a compiled helper.
void abi_bike_physics_helper() noexcept {
  float value = 0.0f;
  std::memcpy(&value, &zpatch::zpatch_bike_value, sizeof(value));
  if (0.006666666665 > static_cast<double>(value))
    zpatch::zpatch_bike_value = 0x3BDA740Eu;
}

// PopupHeadlightsFlag checker (original sub_10044310). Called from the shim
// between pushad/popad, so GPR clobbering is fine. The original touches only
// xmm0 (the angle copy); this reimplementation keeps the float copy through an
// integer memcpy so no SSE register is needed. Semantics (verified against the
// disassembly):
//  - angle := pi/2, flag := 0 before the lookups;
//  - first lookup: hash 0xEB9F21D3 (the "popuplights" IDE hash constant; NOT
//    game_hash("popuplights")), out param initialized to -1 every call, and the
//    raw dword compare means a failed lookup leaving -1 matches a model of -1;
//  - walk zpatch_popuplights_models (the IDE-collected +popuplights list),
//    stopping at the first zero entry;
//  - on a match, scan all 512 [PopupLightAngles] slots and copy the last
//    angle whose hash equals the matched model hash.
void abi_popup_check() noexcept {
  using LookupFn = int(__cdecl*)(std::uint32_t, std::int32_t*);
  const auto lookup = reinterpret_cast<LookupFn>(
      static_cast<std::uintptr_t>(zpatch::zpatch_model_lookup));
  zpatch::zpatch_popup_angle = 0x3FC90FDBu;  // dword_101580CC: pi/2 float bits
  zpatch::zpatch_popup_flag = 0;             // dword_1015BE24
  std::int32_t out = -1;
  if (lookup == nullptr) {
    // Fault-preserving: sub_10044310's `call dword_10159C0C` faults (guard
    // removed 2026-10-16).
    zpatch::log_printf("guard-removed option=PopupHeadlightsFlag lookup=null "
                       "fault=0x10044344");
  }
  lookup(0xEB9F21D3u, &out);
  zpatch::zpatch_popup_index = static_cast<std::uint32_t>(out);  // dword_1015BE1C
  if (out == static_cast<std::int32_t>(zpatch::zpatch_popup_model)) {
    zpatch::zpatch_popup_flag = 1;
    return;
  }
  for (std::uint32_t i = 0; i < 512; ++i) {
    const std::uint32_t hash = zpatch::zpatch_popuplights_models[i];
    if (hash == 0) return;
    out = -1;
    lookup(hash, &out);
    if (out != static_cast<std::int32_t>(zpatch::zpatch_popup_model)) continue;
    zpatch::zpatch_popup_flag = 1;
    for (std::uint32_t k = 0; k < 512; ++k) {
      if (zpatch::zpatch_popup_hashes[k] != 0 &&
          zpatch::zpatch_popup_hashes[k] == hash) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &zpatch::zpatch_popup_angles[k], sizeof(bits));
        zpatch::zpatch_popup_angle = bits;
      }
    }
    return;
  }
}

// ForceCarHeadlightShadows checker (original sub_1001AD20, called from the shim
// between pushad/popad, so GPR clobbering is fine; the original clobbers xmm0
// via movss, which this integer-copy form avoids). Semantics verified against
// the disassembly: default 2.0f; walk the [HeadlightConeOffsets] hash table
// (dword_1015ADB8) from entry 0, stopping at the first zero hash; resolve each
// hash through the game's model lookup and, on the first entry whose index
// equals the object's signed 16-bit model id at +0x2E, publish that entry's
// float offset (dword_1015B5C0) into dword_101580C0.
//
// Fault-preserving (2026-10-16): the original calls the lookup pointer and
// dereferences the object unconditionally (0x1001AD5C `call dword_10159C0C`,
// 0x1001AD62 `movsx eax, word [v2+2Eh]`); the rebuilt helper's null checks
// were fault-avoidance and are removed. Only reachable when the loader's
// preamble missed (lookup == 0, e.g. a build without the pattern) or the shim
// is called with a null object - both fault in the original.
void abi_fchs_check() noexcept {
  const std::uint32_t* const hashes = zpatch::zpatch_fchs_hashes;
  const float* const offsets = zpatch::zpatch_fchs_offsets;
  std::uint32_t hash = hashes[0];
  zpatch::zpatch_fchs_offset = 0x40000000u;  // 2.0f, as the original
  if (hash == 0u) return;
  const std::uint32_t object = zpatch::zpatch_fchs_param;
  const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
      static_cast<std::uintptr_t>(zpatch::zpatch_model_lookup));
  if (lookup == nullptr) {
    // Fault-preserving: the null check was removed; the original's
    // 0x1001AD5C `call dword_10159C0C` faults here.
    zpatch::log_printf("guard-removed option=ForceCarHeadlightShadows lookup=null "
               "fault=0x1001AD5C (call dword_10159C0C)");
  } else if (object == 0u) {
    zpatch::log_printf("guard-removed option=ForceCarHeadlightShadows object=null "
               "fault=0x1001AD62 (movsx eax, word [v2+2Eh])");
  }
  const auto* base =
      reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(object));
  for (std::uint32_t i = 0;;) {
    std::int32_t index = -1;
    lookup(hash, &index);
    const std::int16_t model = *reinterpret_cast<const std::int16_t*>(base + 0x2E);
    if (static_cast<std::int32_t>(model) == index) {
      float value = 0.0f;
      std::memcpy(&value, &offsets[i], sizeof(value));
      std::memcpy(&zpatch::zpatch_fchs_offset, &value, sizeof(value));
      return;
    }
    ++i;
    if (i == 512u) return;  // the original walks into adjacent .data instead; layout-specific, unreproducible
    hash = hashes[i];
    if (hash == 0u) return;
  }
}

// The original's native-scripting "call function by hash" layer (sub_100038E0
// and the sub_1005Bxxx wrappers). The game resolver (dword_1015BDE0, game
// 0x626AF0 on 1.0.8.0) receives the hash in ECX/ESI and the call block in EDX,
// and returns the native handler in EAX; the handler is then called cdecl with one argument: the 0xA4-byte call
// block. The block's +0 m_pReturn and +8 m_pArgs both point at the argument
// array at +0x60 (arg 0 doubles as the return slot), +4 is the argument count,
// +0xC/+0x10 are the output-record counters the cleanup copies out of the
// records at +0x20. The original returns the resolver result ALWAYS as (char)1
// (`mov al,1`) and skips the call when the handler is null.
extern "C" std::uint32_t zpatch_script_resolve(std::uint32_t hash);
extern "C" std::uint32_t zpatch_script_resolve_with_block(std::uint32_t hash,
                                                           void* block);
extern "C" std::uint32_t __cdecl zpatch_script_resolve_seh(std::uint32_t hash,
                                                            std::uint32_t edx_arg);
#if defined(_MSC_VER)
// The MSVC inline assembler needs the global visible by its unmangled name at
// this scope (the declaration lives in namespace zpatch).
using zpatch::zpatch_driveby_resolver;
__declspec(naked) std::uint32_t zpatch_script_resolve(std::uint32_t) {
  __asm {
    push edx
    push dword ptr [esp + 8]
    call zpatch_script_resolve_seh
    add  esp, 8
    ret
  }
}
__declspec(naked) std::uint32_t zpatch_script_resolve_with_block(std::uint32_t,
                                                                  void*) {
  __asm {
    push esi
    mov  esi, dword ptr [esp + 8]
    mov  ecx, esi
    mov  edx, dword ptr [esp + 0Ch]
    mov  eax, DWORD PTR [zpatch_driveby_resolver]
    call eax
    pop  esi
    ret
  }
}
#elif defined(__i386__)
__attribute__((naked)) std::uint32_t zpatch_script_resolve(std::uint32_t) {
  __asm__ __volatile__(
      "pushl %esi\n"
      "movl 8(%esp), %esi\n"
      "movl %esi, %ecx\n"
      "movl zpatch_driveby_resolver, %eax\n"
      "call *%eax\n"
      "popl %esi\n"
      "ret\n");
}
__attribute__((naked)) std::uint32_t zpatch_script_resolve_with_block(std::uint32_t,
                                                                       void*) {
  __asm__ __volatile__(
      "pushl %esi\n"
      "movl 8(%esp), %esi\n"
      "movl %esi, %ecx\n"
      "movl 12(%esp), %edx\n"
      "movl zpatch_driveby_resolver, %eax\n"
      "call *%eax\n"
      "popl %esi\n"
      "ret\n");
}
#else
#if defined(__GNUC__)
__attribute__((noinline))
#endif
std::uint32_t zpatch_script_resolve(std::uint32_t) {
  volatile std::uint32_t result = 0;
  return result;
}
#if defined(__GNUC__)
__attribute__((noinline))
#endif
std::uint32_t zpatch_script_resolve_with_block(std::uint32_t, void*) {
  volatile std::uint32_t result = 0;
  return result;
}
#endif

#if defined(_MSC_VER) && !defined(_WIN64)
static std::uint32_t __cdecl zpatch_resolver_call(std::uint32_t hash,
                                                  std::uint32_t edx_arg) {
  return zpatch_script_resolve_with_block(
      hash, reinterpret_cast<void*>(static_cast<std::uintptr_t>(edx_arg)));
}

extern "C" __declspec(noinline) std::uint32_t __cdecl
zpatch_script_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg) {
  return zpatch::detail::driveby_resolve_seh(&zpatch_resolver_call, hash, edx_arg);
}

extern "C" __declspec(noinline) std::uint32_t __cdecl
zpatch_driveby_resolve_seh(std::uint32_t hash, std::uint32_t edx_arg) {
  return zpatch_script_resolve_seh(hash, edx_arg);
}
#endif

struct zpatch_native_block final {
  std::uint32_t m_p_return;   // +0x00 = &args
  std::uint32_t m_arg_count;  // +0x04
  std::uint32_t m_p_args;     // +0x08 = &args
  std::uint32_t m_out_count;  // +0x0C
  std::uint32_t m_out_ptrs[4];  // +0x10
  struct record final { std::uint32_t v[3]; std::uint32_t padding; };
  record m_records[4];        // +0x20 (sub_100038C0 zeroes v[0..2] of each)
  std::uint32_t args[17];     // +0x60 (arg 0 is the return slot)
};

extern "C" char zpatch_script_call_by_hash(std::uint32_t hash, void* block) noexcept {
  const auto handler = reinterpret_cast<void(__cdecl*)(void*)>(
      static_cast<std::uintptr_t>(zpatch_script_resolve_with_block(hash, block)));
  if (handler) handler(block);
  return 1;
}

// One builder covers every wrapper: the sub_1005DE40/DE80 cleanup loops are
// identical (copy out each pending record, then return args[0]; DE80 returns
// only its low byte).
extern "C" std::uint32_t zpatch_script_call(std::uint32_t hash, std::uint32_t nargs,
                                            const std::uint32_t* args) noexcept {
  zpatch_native_block block;
  std::memset(&block, 0, 0xA0);
  for (auto& record : block.m_records) {
    record.v[0] = 0;
    record.v[1] = 0;
    record.v[2] = 0;
  }
  const auto base = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(block.args));
  block.m_p_return = base;
  block.m_p_args = base;
  block.m_arg_count = nargs;
  for (std::uint32_t i = 0; i < nargs && i < 17u; ++i) block.args[i] = args[i];
  zpatch_script_call_by_hash(hash, &block);
  while (block.m_out_count != 0u) {
    --block.m_out_count;
    auto* destination = reinterpret_cast<std::uint32_t*>(
        static_cast<std::uintptr_t>(block.m_out_ptrs[block.m_out_count]));
    const auto& source = block.m_records[block.m_out_count];
    destination[0] = source.v[0];
    destination[1] = source.v[1];
    destination[2] = source.v[2];
  }
  return block.args[0];
}

extern "C" std::uint8_t zpatch_script_call_bool(std::uint32_t hash, std::uint32_t nargs,
                                                const std::uint32_t* args) noexcept {
  return static_cast<std::uint8_t>(zpatch_script_call(hash, nargs, args));
}

// The MP-only diagnostic name lookup of sub_10003F00 walks the original ASI's
// own 38455-entry model-name table (0x100DE770). The generated catalog retains
// its original order and is checked against the oracle before extraction;
// single-player never reaches this path (the caller is gated on GTAC.dll).
extern "C" const char* zpatch_bpm_model_name(std::uint32_t hash) noexcept {
  // sub_10003F00: scan the original's 38455-entry model-name table in order and
  // return the first name whose game_hash matches; "**UNKNOWN**" on a miss.
  return zpatch::model_name_from_hash(hash).data();
}

// FreezeCarFix / DebugPrintSpawns helper shims (custom register conventions of
// the original's helpers). The probe body calls these by name.
extern "C" const char* zpatch_spawns_name_ecx();  // sub_10003F00 twin, arg in ECX
#if defined(_MSC_VER)
__declspec(naked) const char* zpatch_spawns_name_ecx() {
  __asm {
    push ecx
    call zpatch_bpm_model_name
    add  esp, 4
    ret
  }
}
#else
const char* zpatch_spawns_name_ecx() { return "**UNKNOWN**"; }
#endif

extern "C" const char* zpatch_spawns_who_edx();  // sub_1005B290 twin, arg in EDX
namespace {
const char* zpatch_spawns_who_c(std::uint32_t id) noexcept {
  // The original reads the native hash from [0x101580C8] (zpatch_salt_c8);
  // the loader's probe-miss fallback rewrites that slot on 1000/1010/1040.
  return reinterpret_cast<const char*>(static_cast<std::uintptr_t>(
      zpatch_script_call(zpatch::zpatch_salt_c8, 1, &id)));
}
}  // namespace
#if defined(_MSC_VER)
__declspec(naked) const char* zpatch_spawns_who_edx() {
  __asm {
    push edx
    call zpatch_spawns_who_c
    add  esp, 4
    ret
  }
}
#else
const char* zpatch_spawns_who_edx() { return ""; }
#endif

extern "C" void zpatch_spawns_print_edx();  // sub_1005B530 twin, arg in EDX
namespace {
void zpatch_spawns_print_c(const char* text) noexcept {
  const std::uint32_t arg = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(text));
  zpatch_script_call(0x79023CFCu, 1, &arg);
}
}  // namespace
#if defined(_MSC_VER)
__declspec(naked) void zpatch_spawns_print_edx() {
  __asm {
    push edx
    call zpatch_spawns_print_c
    add  esp, 4
    ret
  }
}
#else
void zpatch_spawns_print_edx() {}
#endif

// sub_10001280: the original's bounded formatter (buf, size, fmt, ...).
extern "C" int zpatch_spawns_sprintf(char* buffer, std::size_t size,
                                     const char* format, ...) {
  va_list args;
  va_start(args, format);
#if defined(_MSC_VER)
  const int result = _vsnprintf_s(buffer, size, _TRUNCATE, format, args);
#else
  const int result = std::vsnprintf(buffer, size, format, args);
#endif
  va_end(args);
  return result;
}

// sub_1005FE7B: the cookie check (cookie in ECX).  This is the LAST call in
// the probe's return path, so it must preserve EAX: the original is
// `cmp ecx, [0x1015800C]; jne fail; ret` and leaves the model index in EAX
// untouched.  Returning the cookie from a C helper (the old implementation)
// overwrote EAX with the process cookie on every probe call.
extern "C" void zpatch_spawns_cookie_check_ecx();
#if defined(_MSC_VER)
extern "C" std::uintptr_t __security_cookie;
__declspec(naked) void zpatch_spawns_cookie_check_ecx() {
  __asm {
    cmp ecx, dword ptr [__security_cookie]
    jne spawns_cookie_fail
    ret
  spawns_cookie_fail:
    mov ecx, 2
    int 29h
  }
}
#else
void zpatch_spawns_cookie_check_ecx() {}
#endif

// The original helpers compiled with /GS end with
//   mov ecx,[ebp-N]; xor ecx,ebp; call <cookie check>; ...
// and the shim resumes *without* restoring EFLAGS (pushal/popal do not), so the
// flags at the resume are the ones from that `cmp ecx,[__security_cookie]`.
// A C replacement must end the same way or the resume sees different flags.
static void zpatch_cookie_tail() noexcept {
#if defined(_MSC_VER) && !defined(_WIN64)
  __asm {
    mov ecx, dword ptr [__security_cookie]
    call zpatch_spawns_cookie_check_ecx
  }
#else
  zpatch_spawns_cookie_check_ecx();
#endif
}

// BadPlayerModelCrashFix's check (original sub_10037AC0): if the object for the
// current model index is not a ped (class id != 6), fall back to the
// "m_y_multiplayer" model so the game does not use an invalid model. Called
// between pushad/popad from the trampoline, so GPR clobbering is fine.
extern "C" void abi_bpm_check() noexcept {
  const auto* models = reinterpret_cast<void* const*>(
      static_cast<std::uintptr_t>(zpatch::zpatch_pre_ped_array));
  void* object = models[zpatch::zpatch_bpm_model_index];
  if (object == nullptr) return;
  void** vtable = *reinterpret_cast<void***>(object);
  // sub_10037AC0 leaves ECX holding the models-table base loaded from
  // dword_1015AD78; it does not reload ECX with `object` before `call eax`.
  // Preserve that original register contract (odd as it looks).
  const char class_id = reinterpret_cast<char(__thiscall*)(void*)>(vtable[3])(
      const_cast<void*>(reinterpret_cast<const void*>(models)));
  if (class_id == 6) return;
  if (GetModuleHandleW(L"GTAC.dll") != nullptr) {
    const std::uint32_t model_hash =
        *reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(object) + 0x3C);
    const char* model_name = zpatch_bpm_model_name(model_hash);
    std::uint32_t entity = zpatch::zpatch_bpm_obj;
    const char* who = reinterpret_cast<const char*>(static_cast<std::uintptr_t>(
        zpatch_script_call(zpatch::zpatch_salt_c8, 1, &entity)));
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s used a non-ped model (%s)", who,
                  model_name);
    const std::uint32_t text = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(buffer));
    zpatch_script_call(0x79023CFCu, 1, &text);
  }
  const std::uint32_t hash = zpatch::game_hash("m_y_multiplayer");
  if (zpatch_script_call_bool(zpatch::zpatch_salt_b8, 1, &hash) == 0) {
    reinterpret_cast<void(__cdecl*)(std::uint32_t, std::uint32_t)>(
        static_cast<std::uintptr_t>(zpatch::zpatch_pre_fn_59c74))(hash, 0);
    zpatch_script_call(zpatch::zpatch_salt_40, 0, nullptr);
  }
  std::int32_t new_index = -1;
  reinterpret_cast<void(__cdecl*)(std::uint32_t, std::int32_t*)>(
      static_cast<std::uintptr_t>(zpatch::zpatch_model_lookup))(hash, &new_index);
  zpatch::zpatch_bpm_model_index = static_cast<std::uint32_t>(new_index);
}

// RestoreDeathMusic (original sub_10050A50): the death-music gate. Gated on the
// option flag byte_10159B86; asks the game's native layer whether death music
// should play, using the shipped hash-salt script calls (0-arg action, the
// 2-arg player query writing its result into a local, then the two booleans).
extern "C" std::uint8_t abi_death_music_enabled() noexcept {
  if (!zpatch::zpatch_flag_death_music) return 0;
  const std::uint32_t action = zpatch_script_call(zpatch::zpatch_salt_5c, 0, nullptr);
  std::uint32_t queried = 0;
  const std::uint32_t query_args[2] = {action,
                                       static_cast<std::uint32_t>(
                                           reinterpret_cast<std::uintptr_t>(&queried))};
  zpatch_script_call(zpatch::zpatch_salt_54, 2, query_args);
  if (zpatch_script_call_bool(zpatch::zpatch_salt_28, 1, &queried) != 0) return 1;
  if (zpatch_script_call_bool(zpatch::zpatch_salt_a8, 0, nullptr) != 0 &&
      zpatch_script_call_bool(zpatch::zpatch_salt_50, 0, nullptr) != 0)
    return 1;
  return 0;
}

// RestoreDeathMusic (original sub_10050AD0): when death music should play,
// resolve "6A 06 68 ? ? ? ? 8D 8C" once and randomize the push immediate at
// match+1 to a track number 1..5 (rand() % 5 + 1, exactly like the original).
extern "C" char abi_death_music_install() noexcept {
  if (!abi_death_music_enabled()) return 0;
  if (zpatch::zpatch_death_music_site == 0) {
    const auto* module = reinterpret_cast<const std::byte*>(GetModuleHandleA(nullptr));
    if (module) {
      const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
      if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            module + dos->e_lfanew);
        if (nt->Signature == IMAGE_NT_SIGNATURE) {
          zpatch::Match match;
          const zpatch::ModuleView view{module, nt->OptionalHeader.SizeOfImage};
          if (zpatch::find_pattern(view, "6A 06 68 ? ? ? ? 8D 8C", match))
            zpatch::zpatch_death_music_site = static_cast<std::uint32_t>(
                reinterpret_cast<std::uintptr_t>(match.address));
        }
      }
    }
  }
  if (zpatch::zpatch_death_music_site != 0) {
    auto* site = reinterpret_cast<std::uint8_t*>(
        static_cast<std::uintptr_t>(zpatch::zpatch_death_music_site) + 1);
    DWORD previous = 0;
    if (VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &previous)) {
      *site = static_cast<std::uint8_t>(std::rand() % 5 + 1);
      DWORD ignored = 0;
      VirtualProtect(site, 1, previous, &ignored);
    }
  }
  return 1;
}

// The NewSavesCompatibility converter (original sub_10051B30): parse the save's
// 0x110-byte header (magic "SAVE" at +0xC), walk 18 chunks of 9-byte header +
// payload (the payload is read and discarded, exactly like the original), and
// record which chunks have the new-format sizes; then patch three game sites.
namespace {
// Byte/word/dword stores through VirtualProtect. Fault-preserving: like every
// original handler these ignore the VirtualProtect result and write anyway, so
// an unmapped/foreign target faults at the store exactly as the original does
// (the earlier `if (!VirtualProtect(...)) return;` form was fault-avoidance -
// it silently skipped the write the original performs).
void wp_byte(void* address, std::uint8_t value) noexcept {
  DWORD previous = 0;
  VirtualProtect(address, 1, PAGE_EXECUTE_READWRITE, &previous);
  *reinterpret_cast<std::uint8_t*>(address) = value;
  DWORD ignored = 0;
  VirtualProtect(address, 1, previous, &ignored);
}
void wp_dword(void* address, std::uint32_t value) noexcept {
  DWORD previous = 0;
  VirtualProtect(address, 4, PAGE_EXECUTE_READWRITE, &previous);
  std::memcpy(address, &value, 4);
  DWORD ignored = 0;
  VirtualProtect(address, 4, previous, &ignored);
}
void wp_word(void* address, std::uint16_t value) noexcept {
  DWORD previous = 0;
  VirtualProtect(address, 2, PAGE_EXECUTE_READWRITE, &previous);
  std::memcpy(address, &value, 2);
  DWORD ignored = 0;
  VirtualProtect(address, 2, previous, &ignored);
}
const std::byte* scan_game(std::string_view signature) noexcept {
  const auto* module = reinterpret_cast<const std::byte*>(GetModuleHandleA(nullptr));
  if (!module) return nullptr;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
  zpatch::Match match;
  const zpatch::ModuleView view{module, nt->OptionalHeader.SizeOfImage};
  if (!zpatch::find_pattern(view, signature, match)) return nullptr;
  return match.address;
}
}  // namespace

// The original's path/name helpers call the secure CRT (`strcpy_s`/`wcscpy_s`)
// through its IAT. The rebuild links a static CRT; keeping a volatile function
// pointer preserves a distinct secure-copy call, while the differential
// coalesces its adjacent byte stores with the original's imported bulk write.
namespace {
using ZpStrcpyS = int(__cdecl*)(char*, std::size_t, const char*);
volatile ZpStrcpyS zpatch_secure_strcpy_s =
    static_cast<ZpStrcpyS>(&strcpy_s);
}  // namespace

// The AddOptionsInMenu integration's four callbacks (originals sub_10050A30/40
// and sub_1001EDB0/C0). The focus-loss setter also re-asserts the game branch
// sub_1001ECB0 patches, using the pre-patch word captured by
// zpatch_menu_prime_focus_loss (the original saves it in its own dword the
// first time the installer runs).
namespace {
std::uint8_t* g_focus_loss_site = nullptr;
std::uint16_t g_focus_loss_original = 0;
}  // namespace

extern "C" void zpatch_menu_prime_focus_loss() noexcept {
  if (g_focus_loss_site != nullptr) return;
  const std::byte* found =
      scan_game("75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15");
  if (found == nullptr) return;
  g_focus_loss_site = const_cast<std::uint8_t*>(
      reinterpret_cast<const std::uint8_t*>(found));
  std::memcpy(&g_focus_loss_original, g_focus_loss_site, 2);
}

extern "C" std::uint8_t abi_menu_death_get() noexcept {
  return zpatch::zpatch_flag_death_music;
}
extern "C" void abi_menu_death_set(std::uint32_t, std::uint32_t value) noexcept {
  zpatch::zpatch_flag_death_music = value != 0 ? 1u : 0u;
}
extern "C" std::uint8_t abi_menu_focus_get() noexcept {
  return zpatch::zpatch_flag_donotpause == 0 ? 1u : 0u;
}
extern "C" void abi_menu_focus_set(std::uint32_t, std::uint32_t value) noexcept {
  zpatch::zpatch_flag_donotpause = value == 0 ? 1u : 0u;
  // Fault-preserving (2026-10-16): the original callback (sub_1001EDC0) writes
  // its saved 2-byte focus site unconditionally; a failed prime scan leaves
  // that saved site null and the original faults at 0. The rebuilt setter's
  // null check was fault-avoidance and is removed.
  if (g_focus_loss_site == nullptr) {
    zpatch::log_printf("guard-removed option=AddOptionsInMenu focus_site=null "
               "fault=sub_1001EDC0 (unconditional write through the saved site)");
  }
  wp_word(g_focus_loss_site,
          zpatch::zpatch_flag_donotpause != 0 ? 0x9090u : g_focus_loss_original);
}

// LocalSettingsPath: the CWD captured at load, the "\GTA IV" folder suffix the
// game's path helper is retargeted to push, the four-byte call slot the game
// calls through (original off_10158064 = sub_10051320) and the five game path
// pointers the detour rewrites.
extern "C" int __stdcall zpatch_local_copy_saves(std::uint32_t, std::uint32_t,
                                                 std::uint32_t, std::uint32_t,
                                                 char* dest) noexcept {
  if (dest != nullptr)
    std::snprintf(dest, 0x104u, "%s\\saves", zpatch::zpatch_local_ansi_cwd);
  return 0;
}

// Captures the CWD and fills the call slot the retargeted game call goes
// through (the original's static constructor pointed off_10158064 at
// sub_10051320).
extern "C" void zpatch_local_settings_capture_cwd() noexcept {
  GetCurrentDirectoryA(0x104u, zpatch::zpatch_local_ansi_cwd);
  GetCurrentDirectoryW(0x104u, zpatch::zpatch_local_wide_cwd);
  zpatch::zpatch_local_saves_slot = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(&zpatch_local_copy_saves));
}

// The detours' helper (original sub_10050D10): rewrites the game's four
// just-computed settings/save path buffers to the relative "<...>\saves\<episode>\"
// form the original builds (the first ANSI destination is hardcoded "GTA IV",
// the other three follow the episode global). Called between pushad/popad.
// The original builds std::strings and copies them with secure CRT calls. The
// rebuild writes the same fixed bytes with bounded loops (no snprintf: the
// static CRT's formatter needs DllMain state and produced a different wide
// string under the differential).
namespace {
void zpatch_build_ansi_path(char* out, const char* name) noexcept {
  const char prefix[] = "\\saves\\";
  std::size_t o = 0;
  for (const char* s = prefix; *s; ++s) out[o++] = *s;
  for (const char* s = name; *s; ++s) out[o++] = *s;
  out[o++] = '\\';
  out[o] = '\0';
}
void zpatch_build_wide_path(wchar_t* out, const wchar_t* name) noexcept {
  const wchar_t prefix[] = L"\\saves\\";
  std::size_t o = 0;
  for (const wchar_t* s = prefix; *s; ++s) out[o++] = *s;
  for (const wchar_t* s = name; *s; ++s) out[o++] = *s;
  out[o++] = L'\\';
  out[o] = 0;
}
// The original copies with strcpy_s/wcscpy_s (cap 0x200). The rebuilt helper
// writes the same bytes without the static CRT: its secure-copy path produced
// an empty string under the differential (the static CRT's internal copy did
// not see the caller's stack buffer), and the memory effect is what the
// original performs.
void zpatch_copy_path(char* dst, std::size_t cap, const char* src) noexcept {
  std::size_t n = 0;
  while (n + 1 < cap && src[n] != '\0') {
    dst[n] = src[n];
    ++n;
  }
  dst[n] = '\0';
}
void zpatch_copy_wpath(wchar_t* dst, std::size_t cap, const wchar_t* src) noexcept {
  std::size_t n = 0;
  while (n + 1 < cap && src[n] != 0) {
    dst[n] = src[n];
    ++n;
  }
  dst[n] = 0;
}
}  // namespace

extern "C" void zpatch_local_settings_step() noexcept {
  const int episode = zpatch::zpatch_local_episode_ptr
                          ? *reinterpret_cast<const int*>(
                                static_cast<std::uintptr_t>(
                                    zpatch::zpatch_local_episode_ptr))
                          : 0;
  const char* ansi_name = "GTA IV";
  const wchar_t* wide_name = L"GTA IV";
  if (episode == 1) {
    ansi_name = "TLaD";
    wide_name = L"TLaD";
  } else if (episode == 2) {
    ansi_name = "TBoGT";
    wide_name = L"TBoGT";
  }
  char ansi_iv[16];
  char ansi_episode[32];
  wchar_t wide_episode[32];
  // sub_10050D10 copies the literal "\saves\GTA IV\" into the first buffer,
  // independent of the episode.
  zpatch_build_ansi_path(ansi_iv, "GTA IV");
  zpatch_build_ansi_path(ansi_episode, ansi_name);
  zpatch_build_wide_path(wide_episode, wide_name);
  if (zpatch::zpatch_local_path_18)
    zpatch_copy_path(reinterpret_cast<char*>(static_cast<std::uintptr_t>(
                         zpatch::zpatch_local_path_18)),
                     0x200u, ansi_iv);
  if (zpatch::zpatch_local_path_08)
    zpatch_copy_path(reinterpret_cast<char*>(static_cast<std::uintptr_t>(
                         zpatch::zpatch_local_path_08)),
                     0x200u, ansi_episode);
  if (zpatch::zpatch_local_path_1c)
    zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<std::uintptr_t>(
                          zpatch::zpatch_local_path_1c)),
                      0x200u, wide_episode);
  if (zpatch::zpatch_local_path_10)
    zpatch_copy_wpath(reinterpret_cast<wchar_t*>(static_cast<std::uintptr_t>(
                          zpatch::zpatch_local_path_10)),
                      0x200u, wide_episode);
  zpatch_cookie_tail();
}

// GarageModelFix helpers (originals sub_10046B90 / sub_10046BD0). The first
// stashes the vehicle's model hash across four scattered bytes of the garage
// object (skipped in MP); the second rebuilds it, resolves it through the
// shared model lookup and repairs the garage model when the resolved index is
// out of range. The originals take (object in ECX, vehicle in EDX); unmangled
// naked wrappers forward to compiled __cdecl implementations.
namespace {
void zpatch_garage_store_c(std::uint8_t* object, const std::uint8_t* vehicle) noexcept {
  if (zpatch::zpatch_citizen_present) return;
  const std::int16_t model =
      *reinterpret_cast<const std::int16_t*>(vehicle + 46);
  const auto base = static_cast<std::uintptr_t>(zpatch::zpatch_pre_ped_array);
  const std::uint32_t entry = *reinterpret_cast<const std::uint32_t*>(
      base + 4u * static_cast<std::uint32_t>(model));
  const std::uint32_t hash = *reinterpret_cast<const std::uint32_t*>(
      reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(entry)) + 60);
  object[64] = static_cast<std::uint8_t>(hash >> 8);
  object[19] = static_cast<std::uint8_t>(hash);
  object[65] = static_cast<std::uint8_t>(hash >> 16);
  object[70] = static_cast<std::uint8_t>(hash >> 24);
}
std::int32_t zpatch_garage_lookup_c(std::uint8_t* object) noexcept {
  std::uint32_t hash = object[19];
  hash |= (static_cast<std::uint32_t>(object[64]) |
           ((static_cast<std::uint32_t>(object[65]) |
             (static_cast<std::uint32_t>(object[70]) << 8))
            << 8))
          << 8;
  const std::int32_t original = static_cast<std::int32_t>(object[19]);
  if (hash == 0u) return original;
  using LookupFn = int(__cdecl*)(std::uint32_t, std::int32_t*);
  // The original calls dword_10159C0C unconditionally (sub_10046BD0 at
  // 0x10046C10 / 0x10046C4A); the rebuilt helper's null checks were
  // fault-avoidance and are removed (fault preserved).
  const auto lookup = reinterpret_cast<LookupFn>(
      static_cast<std::uintptr_t>(zpatch::zpatch_model_lookup));
  if (lookup == nullptr) {
    zpatch::log_printf("guard-removed option=GarageModelFix lookup=null "
               "fault=0x10046C10 (call dword_10159C0C)");
  }
  std::int32_t out = -1;
  lookup(hash, &out);
  if (out <= 0 || out >= 32000) {
    out = -1;
    lookup(0xC703DB5Fu, &out);
    *reinterpret_cast<std::int16_t*>(object + 16) = static_cast<std::int16_t>(out);
    *reinterpret_cast<std::uint16_t*>(object + 48) &= 0xFFFEu;
    return 65534;
  }
  *reinterpret_cast<std::int16_t*>(object + 16) = static_cast<std::int16_t>(out);
  return out;
}
}  // namespace
extern "C" void zpatch_garage_store_model();
#if defined(_MSC_VER)
__declspec(naked) void zpatch_garage_store_model() {
  using zpatch::zpatch_citizen_present;
  using zpatch::zpatch_pre_ped_array;
  __asm {
    cmp  BYTE PTR [zpatch_citizen_present], 0
    push esi
    mov  esi, ecx
    jne  garage_done
    movsx edx, WORD PTR [edx + 0x2e]
    mov  eax, DWORD PTR [zpatch_pre_ped_array]
    mov  eax, DWORD PTR [eax + edx * 4]
    mov  eax, DWORD PTR [eax + 0x3c]
    mov  ecx, eax
    shr  ecx, 8
    mov  BYTE PTR [esi + 0x40], cl
    mov  ecx, eax
    shr  ecx, 0x10
    mov  BYTE PTR [esi + 0x13], al
    shr  eax, 0x18
    mov  BYTE PTR [esi + 0x41], cl
    mov  BYTE PTR [esi + 0x46], al
  garage_done:
    pop  esi
    ret
  }
}
#else
void zpatch_garage_store_model() {}
#endif
extern "C" std::int32_t zpatch_garage_lookup_model();
#if defined(_MSC_VER)
__declspec(naked) std::int32_t zpatch_garage_lookup_model() {
  __asm {
    push ecx
    call zpatch_garage_lookup_c
    add  esp, 4
    push eax
    mov ecx, dword ptr [__security_cookie]
    call zpatch_spawns_cookie_check_ecx
    pop eax
    ret
  }
}
#else
std::int32_t zpatch_garage_lookup_model() { return 0; }
#endif

// MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts.
// abi_vehicle_component is the original sub_10003F40: a component-name lookup in
// the model registry (entries at registry[0], stride 0xE0, count at +0x14,
// name at entry+0, bone id at entry+0x14). abi_steer_body is the shared consumer
// (original sub_10004C70), called between pushad/popad by the two detours.
extern "C" const void* abi_vehicle_component(const void* registry,
                                             const char* name) noexcept {
  // Fault-preserving (2026-10-16): the original (sub_10003F40) dereferences
  // the registry, the entry table and each candidate name unconditionally
  // (`stricmp(*(char**)(*(DWORD*)a1 + i), a2)`); the rebuilt helper's null
  // checks were fault-avoidance and are removed. A null registry/name now
  // faults exactly where the original does.
  if (registry == nullptr || name == nullptr) {
    zpatch::log_printf("guard-removed option=MovingSteeringWheelSupport/Parts registry-or-name=null "
               "fault=sub_10003F40 (unconditional dereference)");
  }
  const auto* entries = *reinterpret_cast<const std::uint8_t* const*>(registry);
  const auto count = *reinterpret_cast<const std::uint16_t*>(
      static_cast<const std::uint8_t*>(registry) + 0x14);
  for (std::uint16_t i = 0; i < count; ++i) {
    const auto* entry = entries + static_cast<std::size_t>(i) * 0xE0u;
    const char* candidate = *reinterpret_cast<const char* const*>(entry);
    if (_stricmp(candidate, name) == 0) return entry;
  }
  return nullptr;
}

extern "C" std::uint32_t abi_steer_body() noexcept {
  using zpatch::zpatch_engine_descending;
  using zpatch::zpatch_engine_last_timestep;
  using zpatch::zpatch_engine_phase;
  using zpatch::zpatch_flag_engine;
  using zpatch::zpatch_flag_plane;
  using zpatch::zpatch_flag_steering;
  using zpatch::zpatch_model_lookup;
  using zpatch::zpatch_pre_fn_59b88;
  using zpatch::zpatch_pre_fn_59cdc;
  using zpatch::zpatch_pre_fn_5d63c;
  using zpatch::zpatch_pre_ped_array;
  using zpatch::zpatch_salt_3c;
  using zpatch::zpatch_steer_vtable_fn;
  using zpatch::zpatch_steer_vehicle;
  using zpatch::zpatch_wheelangle_angles;
  using zpatch::zpatch_wheelangle_models;

  auto* const vehicle = reinterpret_cast<std::uint8_t*>(
      static_cast<std::uintptr_t>(zpatch_steer_vehicle));

#if defined(_MSC_VER) && !defined(_WIN64)
  using BoneFn = std::uint32_t*(__thiscall*)(void*, std::uint32_t);
  using SetFn = void(__thiscall*)(std::uint32_t*, float);
  using PartFn = void(__thiscall*)(void*, int, int, float, int, int, int);
#else
  using BoneFn = std::uint32_t*(*)(void*, std::uint32_t);
  using SetFn = void(*)(std::uint32_t*, float);
  using PartFn = void(*)(void*, int, int, float, int, int, int);
#endif
  const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
      static_cast<std::uintptr_t>(zpatch_model_lookup));
  const auto get_bone = reinterpret_cast<BoneFn>(
      static_cast<std::uintptr_t>(zpatch_pre_fn_59b88));
  const auto set_a = reinterpret_cast<SetFn>(
      static_cast<std::uintptr_t>(zpatch_pre_fn_5d63c));
  const auto set_b = reinterpret_cast<SetFn>(
      static_cast<std::uintptr_t>(zpatch_pre_fn_59cdc));
  const auto part_call = reinterpret_cast<PartFn>(
      static_cast<std::uintptr_t>(zpatch_steer_vtable_fn));

  // sub_10003980: read the current script float (resolver hash salt_3c).
  const auto script_float = [&]() -> float {
    const std::uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
    const std::uint32_t offset =
        *reinterpret_cast<const std::uint32_t*>(
            static_cast<std::uintptr_t>(ctx) + 0xE);
    const std::uint32_t value = *reinterpret_cast<const std::uint32_t*>(
        static_cast<std::uintptr_t>(ctx) + offset + 0x18);
    return *reinterpret_cast<const float*>(static_cast<std::uintptr_t>(value) + 8);
  };
  const auto model_of = [vehicle]() {
    return *reinterpret_cast<const std::int16_t*>(vehicle + 0x2E);
  };
  const auto identity = [](std::uint32_t* m) {
    m[0] = 0x3F800000u;
    m[1] = 0;
    m[2] = 0;
    m[4] = 0;
    m[5] = 0x3F800000u;
    m[6] = 0;
    m[8] = 0;
    m[9] = 0;
    m[10] = 0x3F800000u;
  };
  const auto bone_matrix = [&](std::int16_t model, const char* name) -> std::uint32_t* {
    const auto base = static_cast<std::uintptr_t>(zpatch_pre_ped_array);
    const std::uint32_t ped = *reinterpret_cast<const std::uint32_t*>(
        base + 4u * static_cast<std::uint32_t>(model));
    const std::uint32_t p1 = *reinterpret_cast<const std::uint32_t*>(
        static_cast<std::uintptr_t>(ped) + 8);
    const std::uint32_t p2 = *reinterpret_cast<const std::uint32_t*>(
        static_cast<std::uintptr_t>(p1) + 0xB4);
    const std::uint32_t registry = *reinterpret_cast<const std::uint32_t*>(
        static_cast<std::uintptr_t>(p2) + 0xC);
    const auto* entry = static_cast<const std::uint8_t*>(abi_vehicle_component(
        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(registry)), name));
    if (entry == nullptr) return nullptr;  // the original's own `if (v6)` check
    if (get_bone == nullptr) {
      // Fault-preserving: sub_10004C70's 0x10004D4A `call dword_10159B88` faults
      // (guard removed 2026-10-16).
      zpatch::log_printf("guard-removed option=MovingSteeringWheelSupport get_bone=null "
                         "fault=0x10004D4A");
    }
    const std::uint32_t bone =
        *reinterpret_cast<const std::uint16_t*>(entry + 0x14);
    return get_bone(vehicle, bone);
  };

  if (zpatch_flag_steering) {
    const std::int16_t model = model_of();
    const float steering = *reinterpret_cast<const float*>(vehicle + 0x10D8);
    const std::uint32_t lock_owner = *reinterpret_cast<const std::uint32_t*>(
        vehicle + 0xE18);
    const float lock = *reinterpret_cast<const float*>(
        static_cast<std::uintptr_t>(lock_owner) + 0x84);
    const float turn = static_cast<float>(static_cast<double>(steering / lock) *
                                          -3.141592653589793);
    float tilt = 0.0f;
    for (int i = 0; i < 512; ++i) {
      const std::uint32_t hash = zpatch_wheelangle_models[i];
      if (hash == 0u) break;
      std::int32_t resolved = -1;
      lookup(hash, &resolved);  // sub_10004C70 0x10004CE7: unconditional
      if (resolved == model) {
        tilt = static_cast<float>(
            static_cast<double>(zpatch_wheelangle_angles[i]) *
            3.141592653589793 / 180.0);
        break;
      }
    }
    if (model != -1) {
      std::uint32_t* matrix = bone_matrix(model, "steeringwheel");
      if (matrix != nullptr) {
        identity(matrix);
        if (set_a) set_a(matrix, tilt);
        if (set_b) set_b(matrix, turn);
      }
    }
  }
  if (zpatch_flag_engine) {
    const float timestep = script_float();
    if (zpatch_engine_last_timestep != timestep) {
      const double step = zpatch_engine_descending ? -0.5 : 0.5;
      const float current = script_float();
      const float phase = static_cast<float>(static_cast<double>(current) * step +
                                             static_cast<double>(zpatch_engine_phase));
      zpatch_engine_phase = phase;
      std::uint8_t direction = zpatch_engine_descending;
      if (static_cast<double>(phase) > 0.01) direction = 1;
      if (static_cast<double>(phase) < -0.01) direction = 0;
      zpatch_engine_descending = direction;
    }
    const std::int16_t model = model_of();
    if (model != -1) {
      std::uint32_t* matrix = bone_matrix(model, "engineblock");
      if (matrix != nullptr) {
        identity(matrix);
        const std::uint32_t flags =
            *reinterpret_cast<const std::uint32_t*>(vehicle + 0xF64);
        float scale = 0.0f;
        if ((flags & 8u) != 0u) {
          const float rpm = *reinterpret_cast<const float*>(vehicle + 0x10C8);
          scale = static_cast<float>(static_cast<double>(rpm) * 0.75 + 0.25);
        }
        if ((flags & 0x10u) != 0u) scale = 1.0f;
        if (set_b) set_b(matrix, scale * zpatch_engine_phase);
      }
    }
  }
  if (zpatch_flag_plane &&
      *reinterpret_cast<const std::uint32_t*>(vehicle + 0x1354) == 5u &&
      part_call != nullptr) {
    const float travel = *reinterpret_cast<const float*>(vehicle + 0x1F3C);
    const float lift = script_float() * travel * 100.0f;
    part_call(vehicle, 89, 4, lift, 0, 0, 0);
    part_call(vehicle, 91, 4, script_float() * travel * 100.0f, 0, 0, 0);
    part_call(vehicle, 90, 4, script_float() * travel * 100.0f, 0, 0, 0);
    part_call(vehicle, 92, 4, script_float() * travel * 100.0f, 0, 0, 0);
    const float pitch = *reinterpret_cast<const float*>(vehicle + 0x1F08);
    const float roll = *reinterpret_cast<const float*>(vehicle + 0x1F04);
    const float heave = *reinterpret_cast<const float*>(vehicle + 0x1F00);
    part_call(vehicle, 96, 3,
              static_cast<float>(static_cast<double>(pitch) * 0.5), 1, 0, 0);
    part_call(vehicle, 97, 3,
              static_cast<float>(static_cast<double>(pitch) * -0.5), 1, 0, 0);
    part_call(vehicle, 94, 3,
              static_cast<float>(static_cast<double>(roll) * -0.5), 1, 0, 0);
    part_call(vehicle, 95, 3,
              static_cast<float>(static_cast<double>(roll) * -0.5), 1, 0, 0);
    part_call(vehicle, 93, 5,
              static_cast<float>(static_cast<double>(heave) * 0.5), 1, 0, 0);
  }
  // Unconditional tail: snapshot the last script timestep and return its slot.
  const std::uint32_t ctx = zpatch_script_resolve(zpatch_salt_3c);
  const std::uint32_t offset = *reinterpret_cast<const std::uint32_t*>(
      static_cast<std::uintptr_t>(ctx) + 0xE);
  const std::uint32_t value_ptr = *reinterpret_cast<const std::uint32_t*>(
      static_cast<std::uintptr_t>(ctx) + offset + 0x18);
  const float last = *reinterpret_cast<const float*>(
      static_cast<std::uintptr_t>(value_ptr) + 8);
  zpatch_engine_last_timestep = last;
  return value_ptr;
}

// The shared flag-gated helper of the Patch4RemoveModChecks /
// RemoveGFWLUpdatePopup handler (original sub_10005340): when Patch4 is on,
// zero the dword the matched game code points at (match+30); when the GFWL
// popup removal is on, store 0x10000E83 over the four xlive.dll update-window
// flag slots. Called between pushad/popad from abi_gfwl_stub.
namespace {
// sub_10005340's patch4 arm and sub_10050587's speedup arm guard their one-time
// pattern scans with the MSVC thread-safe-static protocol:
//   if ((int)guard > (int)*(TLS[_tls_index] + 4)) {
//       _Init_thread_header(&guard);
//       if (guard == -1) { scan; cache; _Init_thread_footer(&guard); }
//   }
// The guard/cache are ASI globals. The epoch at *(TLS[...]+4) is loader/CRT
// state: it is not reproducible across modules and stays non-negative when the
// ASI is mapped without the CRT's TLS init, which made the first call take the
// "already initialized" path and leave the cached clock null (live AV at
// clock-51). Keep the original's one-time semantics with an explicit tri-state
// guard instead: 0 = uninitialised, -1 = initialising, 1 = done.
bool zpatch_tls_static_header(std::uint32_t& guard) noexcept {
#if defined(_M_IX86) && defined(_MSC_VER)
  for (;;) {
    const LONG observed = InterlockedCompareExchange(
        reinterpret_cast<volatile LONG*>(&guard), -1, 0);
    if (observed == 0) return true;
    if (observed == -1) {
      Sleep(1);  // another thread owns the one-time scan
      continue;
    }
    return false;
  }
#else
  if (guard == 0) {
    guard = 0xFFFFFFFFu;
    return true;
  }
  return false;
#endif
}

void zpatch_tls_static_footer(std::uint32_t& guard) noexcept {
#if defined(_M_IX86) && defined(_MSC_VER)
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&guard), 1);
#else
  guard = 1;
#endif
}
}  // namespace

extern "C" void abi_gfwl_patch() noexcept {
  if (zpatch::zpatch_flag_patch4_modchecks) {
    if (zpatch_tls_static_header(zpatch::zpatch_gfwl_scan_guard)) {
      // _Init_thread_header claimed the guard; scan then publish the epoch.
      const std::byte* found =
          scan_game("33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57");
      zpatch::zpatch_gfwl_site = found == nullptr
                                     ? 0u
                                     : static_cast<std::uint32_t>(
                                           reinterpret_cast<std::uintptr_t>(found));
      zpatch_tls_static_footer(zpatch::zpatch_gfwl_scan_guard);
    }
    if (zpatch::zpatch_gfwl_site != 0) {
      std::uint32_t target = 0;
      std::memcpy(&target,
                  reinterpret_cast<const void*>(
                      static_cast<std::uintptr_t>(zpatch::zpatch_gfwl_site) + 30u),
                  4);
      // Fault-preserving: the original VirtualProtects and zeroes
      // *(site+30) without a null check (sub_10005340 at 0x10005427).
      wp_dword(reinterpret_cast<void*>(target), 0);
    }
  }
  if (zpatch::zpatch_flag_gfwl_popup) {
    // Fault-preserving (2026-10-16): the original (sub_10005340 at 0x10005421
    // ff.) writes dword_10159C10 + 0x66F770/.. with no base check; with
    // xlive.dll absent the captured base is 0, so the original's
    // VirtualProtect/`*v2 = 0x10000E83` touch the low 0x66F770 page and
    // fault. The rebuilt helper's `base != 0` early-out was fault-avoidance
    // and is removed.
    const auto base = static_cast<std::uintptr_t>(zpatch::zpatch_xlive_base);
    if (base == 0) {
      zpatch::log_printf("guard-removed option=RemoveGFWLUpdatePopup xlive_base=0 "
                 "fault=0x1000542D (write to low 0x66F770 page)");
    }
    wp_dword(reinterpret_cast<void*>(base + 6748016u), 0x10000E83u);
    wp_dword(reinterpret_cast<void*>(base + 6748020u), 0x10000E83u);
    wp_dword(reinterpret_cast<void*>(base + 7034320u), 0x10000E83u);
    wp_dword(reinterpret_cast<void*>(base + 7034352u), 0x10000E83u);
  }
}

extern "C" std::uint8_t abi_new_saves_read_hook(void*, std::uint32_t size) noexcept {
  std::uint8_t scratch[3704];
  if (zpatch::zpatch_new_saves_loaddata == 0) return 0;
  using LoadFn = std::uint8_t(__cdecl*)(void*, std::uint32_t);
  return reinterpret_cast<LoadFn>(
      static_cast<std::uintptr_t>(zpatch::zpatch_new_saves_loaddata))(scratch, size);
}

extern "C" std::int32_t abi_new_saves_p3_hook() noexcept {
  std::uint8_t buffer[12];
  using LoadFn = std::int32_t(__cdecl*)(void*, std::uint32_t);
  const auto load = reinterpret_cast<LoadFn>(
      static_cast<std::uintptr_t>(zpatch::zpatch_new_saves_loaddata));
  if (zpatch::zpatch_new_saves_flag13 != 0 && load)
    static_cast<void>(load(buffer, 12));
  using OrigFn = std::int32_t(__cdecl*)();
  const std::int32_t result = reinterpret_cast<OrigFn>(
      static_cast<std::uintptr_t>(zpatch::zpatch_new_saves_p3_orig))();
  if (zpatch::zpatch_new_saves_flag13 != 0 && load) return load(buffer, 12);
  return result;
}

extern "C" void abi_new_saves_convert(const char* path) noexcept {
  std::FILE* file = std::fopen(path, "rb");
  if (!file) return;
  std::uint8_t header[0x110];
  std::fread(header, 0x110, 1, file);  // return ignored, like the original
  std::uint32_t magic = 0;
  std::memcpy(&magic, header + 0x0C, 4);
  if (magic != 0x45564153u) {  // "SAVE"
    std::fclose(file);
    return;
  }
  for (int i = 0; i < 18; ++i) {
    std::uint8_t chunk[9];
    std::fread(chunk, 9, 1, file);
    std::uint32_t size = 0;
    std::memcpy(&size, chunk + 5, 4);
    zpatch::zpatch_new_saves_chunk_len = size;                  // dword_1015AD90
    zpatch::zpatch_new_saves_element_count = size - 9u;         // dword_1015ADA0
    void* payload = std::malloc(zpatch::zpatch_new_saves_element_count);
    std::fread(payload, 1, zpatch::zpatch_new_saves_element_count, file);
    std::free(payload);
    if (i == 9) zpatch::zpatch_new_saves_flag9 = size == 19525u ? 1u : 0u;
    if (i == 16) zpatch::zpatch_new_saves_flag16 = size == 3703u ? 1u : 0u;
    if (i == 13) zpatch::zpatch_new_saves_flag13 = size == 5953u ? 1u : 0u;
  }
  std::fclose(file);

  // P1 (game 0x872185): force the 0x3C-byte read path for new saves.
  static const std::byte* p1 = nullptr;
  if (p1 == nullptr) p1 = scan_game("74 37 80 3D ? ? ? ? 00 75 2E 8D 84 ? ? ? ? 00 6A 1E");
  if (p1 != nullptr)
    wp_byte(const_cast<std::byte*>(p1),
            zpatch::zpatch_new_saves_flag9 != 0 ? 0xEB : 0x74);

  // P2 (game 0x7D8C50): route the oversized read through the plugin's scratch
  // buffer, resize the read, and force/NOP the branch per the save format.
  static const std::byte* p2 = nullptr;
  if (p2 == nullptr) p2 = scan_game("68 BE 0C 00 00 50 E8");
  if (p2 != nullptr) {
    auto* site = const_cast<std::byte*>(p2);
    std::int32_t original_rel = 0;
    std::memcpy(&original_rel, site + 7, 4);
    const auto original_target = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(site) + 11u +
        static_cast<std::uint32_t>(original_rel));
    wp_byte(site + 6, 0xE8);
    const auto hook = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&abi_new_saves_read_hook));
    wp_dword(site + 7, hook - static_cast<std::uint32_t>(
                                  reinterpret_cast<std::uintptr_t>(site + 6)) - 5u);
    if (zpatch::zpatch_new_saves_loaddata == 0)
      zpatch::zpatch_new_saves_loaddata = original_target;
    wp_dword(site + 1, zpatch::zpatch_new_saves_flag16 != 0 ? 3694u : 3262u);
    wp_word(site - 5, zpatch::zpatch_new_saves_flag16 != 0 ? 0x9090u : 0x2675u);
  }

  // P3 (game 0x8DA090): route the second LoadData call through the plugin body
  // that consumes the extra 12-byte block for new saves, and repoint the
  // shared LoadData slot at the function's own later call target.
  static const std::byte* p3 = nullptr;
  if (p3 == nullptr) p3 = scan_game("56 E8 ? ? ? ? E8 ? ? ? ? A1 ? ? ? ? 8D 0C 85 00 00 00 00");
  if (p3 != nullptr) {
    auto* site = const_cast<std::byte*>(p3);
    std::int32_t original_rel = 0;
    std::memcpy(&original_rel, site + 7, 4);
    const auto original_target = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(site) + 11u +
        static_cast<std::uint32_t>(original_rel));
    wp_byte(site + 6, 0xE8);
    const auto hook = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&abi_new_saves_p3_hook));
    wp_dword(site + 7, hook - static_cast<std::uint32_t>(
                                  reinterpret_cast<std::uintptr_t>(site + 6)) - 5u);
    if (zpatch::zpatch_new_saves_p3_orig == 0)
      zpatch::zpatch_new_saves_p3_orig = original_target;
    std::int32_t third_rel = 0;
    std::memcpy(&third_rel, site + 30, 4);
    zpatch::zpatch_new_saves_loaddata = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(site) + 30u +
        static_cast<std::uint32_t>(third_rel) + 4u);
  }
}

// The prepare step of the tramp (original sub_10051FD0): when the handler
// captured the game's save-name buffer, make sure the savegame directory
// exists (game function resolved from its signature) and convert
// "<dir>\\<save name>".
extern "C" void abi_new_saves_prepare() noexcept {
  const auto* name = reinterpret_cast<const char*>(
      static_cast<std::uintptr_t>(zpatch::zpatch_new_saves_src));
  if (name == nullptr) return;
  static const std::byte* get_dir = nullptr;
  if (get_dir == nullptr)
    get_dir = scan_game("55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28");
  if (get_dir == nullptr) return;
  char directory[312]{};
  char suffix = 0;
  using GetDirFn = std::uint8_t(__cdecl*)(int, char*, char*);
  if (reinterpret_cast<GetDirFn>(const_cast<std::byte*>(get_dir))(0, directory, &suffix) == 0)
    return;
  char path[560];
  std::snprintf(path, sizeof(path), "%s\\%s", directory, name);
  abi_new_saves_convert(path);
}
}  // extern "C"
namespace zpatch {
namespace {

// The spawn probe's GetModuleHandleW call goes through our own slot (MSVC's
// inline assembler cannot name the @-decorated import pointer); fill it from a
// file-scope initializer, which runs before DllMain can install the hooks.
struct SpawnsGetModuleHandleInit final {
  SpawnsGetModuleHandleInit() noexcept {
    zpatch_spawns_getmodulehandlew = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&GetModuleHandleW));
  }
};
[[maybe_unused]] SpawnsGetModuleHandleInit g_spawns_getmodulehandle_init;

// CutsceneFixes chain B's VirtualProtect slot: the original body loads the IAT
// entry directly, but MSVC's inline assembler cannot name the @-decorated
// import pointer. The body loads this slot instead (same value).
std::uint32_t zpatch_cutscene_virtualprotect = 0;
struct CutsceneVirtualProtectInit final {
  CutsceneVirtualProtectInit() noexcept {
    zpatch_cutscene_virtualprotect = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(&VirtualProtect));
  }
};
[[maybe_unused]] CutsceneVirtualProtectInit g_cutscene_virtualprotect_init;

// The two game functions the DLC discovery routine calls (published as RVAs by
// the LoadDLCs handler: dword_1015804C register, dword_101580D0 apply). The
// thiscall/stdcall keywords are MSVC-only; the ASI is built with VS x86.
#if defined(_MSC_VER) && !defined(_WIN64)
using DlcRegisterFn = void(__stdcall*)(int id, char* request, int flag);
using DlcApplyFn = void(__thiscall*)(void* object, char* request);
#else
using DlcRegisterFn = void (*)(int id, char* request, int flag);
using DlcApplyFn = void (*)(void* object, char* request);
#endif

// @exact 0x10005590: instruction-for-instruction transcription of the original.
// 0x10005590 AimProjectileCrashFix. __thiscall (this in ecx), three stack
// arguments, callee pops 12 bytes. MSVC rejects __thiscall on free functions,
// so the original instruction sequence is reproduced directly.
#if defined(_MSC_VER)
__declspec(naked) void abi_aim_projectile_crash_fix() {
  __asm {
    cmp ecx, 0
    je skip
    mov eax, [esp + 8]
    mov [ecx + 0x20], eax
    mov eax, [esp + 0xc]
    mov [ecx + 0x24], eax
    neg eax
    sbb eax, eax
    and eax, [esp + 4]
    mov [ecx + 0x1c], eax
  skip:
    ret 12
  }
}
#else
__attribute__((naked)) void abi_aim_projectile_crash_fix() {
  __asm__ __volatile__(
      "cmpl $0, %ecx\n"
      "je 1f\n"
      "movl 0x8(%esp), %eax\n"
      "movl %eax, 0x20(%ecx)\n"
      "movl 0xc(%esp), %eax\n"
      "movl %eax, 0x24(%ecx)\n"
      "negl %eax\n"
      "sbbl %eax, %eax\n"
      "andl 0x4(%esp), %eax\n"
      "movl %eax, 0x1c(%ecx)\n"
      "1:\n"
      "ret $12\n");
}
#endif

// @exact 0x100057f0: matches the original's xor eax,eax / ret.
// 0x100057f0 BetterMPSync: xor eax,eax / ret.
int __cdecl abi_better_mp_sync() { return 0; }

// @exact 0x10005ed0: matches the original's mov eax,13h / ret.
// 0x10005ed0 CombinedRadioFreezeFix: mov eax,13h / ret.
int __cdecl abi_combined_radio_freeze_fix() { return 19; }

// 0x10003e50 AllowHashesForIDE: the game's model-name hash function, replaced so
// that "hash:<number>" literals are accepted (see game_hash). The original is
// __cdecl(char*) -> unsigned int; this wrapper keeps that ABI.
unsigned int __cdecl abi_game_hash(const char* text) {
  return game_hash(text ? std::string_view{text} : std::string_view{});
}

// 0x10050870 PedModelsInCutscenesFix: replaces the game's "does this object use
// the player model" check (the original calls a game helper and compares
// +0x2e). The replacement looks up the model index for the "player" hash and
// compares it with this->model. The original is __thiscall(this) -> bool in al;
// __fastcall puts `this` in ecx and returns bool in al the same way.
bool __fastcall abi_ped_models_in_cutscenes_check(const void* self, void*) {
  const std::uint32_t hash = game_hash("player");
  std::int32_t index = -1;
  const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
      static_cast<std::uintptr_t>(zpatch_model_lookup));
  if (lookup == nullptr) {
    // Fault-preserving: the replacement mirrors the original's unconditional
    // `call dword_10159C0C` (guard removed 2026-10-16).
    zpatch::log_printf("guard-removed option=PedModelsInCutscenesFix lookup=null "
                       "fault=original model lookup call");
  }
  lookup(hash, &index);
  const auto* o = static_cast<const std::uint8_t*>(self);
  const std::int16_t model = *reinterpret_cast<const std::int16_t*>(o + 0x2e);
  return model == index;
}

// 0x10045a30 / 0x10045a80 ImprovedShaderStreaming: compiled replacements for the
// two "find shader entry" game functions the shader-streaming handler E8s into.
// The originals are __thiscall(this, query) -> matched entry or 0, with a single
// callee-popped stack argument, so __fastcall(self, edx, query) reproduces the
// ecx=this / stack-arg / ret 4 contract. Each walks the vector at this+8 (count
// at this+0xc), asks each non-null entry's vtable[+0x10]() for its name, and
// returns the first entry whose name passes the filter:
//   * 0x10045a30 (sub_100459E0): name[4]=='h', or name[4]=='p' && name[5]=='e';
//   * 0x10045a80 (sub_10045A10): name[4]=='v'.
using ShaderEntryNameFn = const void*(__fastcall*)(void* entry, int);
using ShaderStreamFilterFn = int(__cdecl*)(const void* query, const void* name);
int __cdecl abi_shader_stream_filter_hpe(const void*, const void* name) {
  const auto* p = static_cast<const unsigned char*>(name);
  if (!p) return 2;
  if (p[4] == 'h') return 0;
  if (p[4] == 'p' && p[5] == 'e') return 0;
  return 2;
}
int __cdecl abi_shader_stream_filter_v(const void*, const void* name) {
  const auto* p = static_cast<const unsigned char*>(name);
  return (p && p[4] == 'v') ? 0 : 2;
}
namespace {
void* shader_streaming_find(void* self, const void* query, ShaderStreamFilterFn filter) {
  auto* base = static_cast<unsigned char*>(self);
  const unsigned count = *reinterpret_cast<const unsigned short*>(base + 0xc);
  auto** array = *reinterpret_cast<void***>(base + 8);
  for (unsigned i = 0; i < count; ++i) {
    void* entry = array[i];
    if (!entry) continue;
    auto* vt = *reinterpret_cast<void* const* const*>(entry);
    const auto name_fn = reinterpret_cast<ShaderEntryNameFn>(vt[0x10 / sizeof(void*)]);
    const void* name = name_fn(entry, 0);
    if (filter(query, name) == 0) return entry;
  }
  return nullptr;
}
}  // namespace
void* __fastcall abi_improved_shader_streaming_find_hpe(void* self, int, const void* query) {
  return shader_streaming_find(self, query, &abi_shader_stream_filter_hpe);
}
void* __fastcall abi_improved_shader_streaming_find_v(void* self, int, const void* query) {
  return shader_streaming_find(self, query, &abi_shader_stream_filter_v);
}

// 0x10023480 LoadDLCs: mov al,1 / ret 8 (__stdcall, two arguments). The
// original sets only AL; the upper 24 bits of EAX are preserved, so a plain
// `return 1` (mov eax,1) is observable (the runtime differential caught
// 0xFFFFFF01 vs 0x00000001).
#if defined(_MSC_VER)
// @exact 0x10023480 - hand-written transcription, byte-compared.
__declspec(naked) int __stdcall abi_load_dlcs_dummy(int, int) {
  __asm {
    mov al, 1
    ret 8
  }
}
#else
int __stdcall abi_load_dlcs_dummy(int, int) { return 1; }
#endif

// 0x10038550 RemoveRGSCLoginRequirement: mov al,1 / ret (no arguments), AL only.
#if defined(_MSC_VER)
// @exact 0x10038550 - hand-written transcription, byte-compared.
__declspec(naked) int __cdecl abi_rgsc_login_removed() {
  __asm {
    mov al, 1
    ret
  }
}
#else
int __cdecl abi_rgsc_login_removed() { return 1; }
#endif

// @exact 0x10038570: matches the original's xor eax,eax / ret 8.
// 0x10038570 SavegameFix: xor eax,eax / ret 8 (__stdcall, two arguments).
int __stdcall abi_savegame_fix_null(int, int) { return 0; }

// 0x10045580 SavegameFix close: push [ebp+8] / call CloseHandle / xor eax,eax /
// ret 4 (__stdcall, one argument).
int __stdcall abi_savegame_fix_close(void* handle) {
  if (handle) CloseHandle(handle);
  return 0;
}

// 0x100455e0 SavegameFix mutex: if (!out) return 1;
// *out = CreateMutexA(0,0,0); return 0; __stdcall, two arguments.
int __stdcall abi_savegame_fix_mutex(void*, void** out_handle) {
  if (!out_handle) return 1;
  *out_handle = CreateMutexA(nullptr, FALSE, nullptr);
  return 0;
}

// Crash-fix shims, transcribed byte-faithfully from the original bodies'
// disassembly. The decompiled C hid real work in almost all of them (argument
// pushes, register setup, flags consumed by the resumed code), so these are
// 1:1 instruction sequences ending in a tail jump to the published resume.
// Registers the resumed code depends on (al, dl, flags) must survive intact.

// @exact 0x1003d7b0: instruction-for-instruction transcription of the original.
// 0x1003d7b0 PathNodeCrashFix: eax = node index; the sentinel is 0x1FFFDF.
// Otherwise dl = [eax+0x1F] and its low bit feed the failure resume.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_path_node_shim() {
  __asm {
    cmp eax, 0x1FFFDF
    je ok
    mov dl, BYTE PTR [eax + 0x1F]
    test dl, 1
    jmp DWORD PTR [zpatch_path_node_resume_fail]
  ok:
    jmp DWORD PTR [zpatch_path_node_resume_ok]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_path_node_shim() {
  __asm__ __volatile__("cmpl $0x1FFFDF, %eax\n"
                       "je 1f\n"
                       "movb 0x1F(%eax), %dl\n"
                       "testb $1, %dl\n"
                       "jmp *zpatch_path_node_resume_fail\n"
                       "1:\n"
                       "jmp *zpatch_path_node_resume_ok\n");
}
#else
[[maybe_unused]] void abi_path_node_shim() {}
#endif

// @exact 0x1003d870: instruction-for-instruction transcription of the original.
// 0x1003d870 PlaneCrashFix: eax = object; null takes the null resume. The
// normal path stores bl at +0x26 and loads eax from [[esi]+0xF4].
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_plane_crash_shim() {
  __asm {
    cmp eax, 0
    je null_path
    mov BYTE PTR [eax + 0x26], bl
    mov edx, DWORD PTR [esi]
    mov eax, DWORD PTR [edx + 0xF4]
    jmp DWORD PTR [zpatch_plane_crash_resume_b]
  null_path:
    jmp DWORD PTR [zpatch_plane_crash_resume_a]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_plane_crash_shim() {
  __asm__ __volatile__("cmpl $0, %eax\n"
                       "je 1f\n"
                       "movb %bl, 0x26(%eax)\n"
                       "movl (%esi), %edx\n"
                       "movl 0xF4(%edx), %eax\n"
                       "jmp *zpatch_plane_crash_resume_b\n"
                       "1:\n"
                       "jmp *zpatch_plane_crash_resume_a\n");
}
#else
[[maybe_unused]] void abi_plane_crash_shim() {}
#endif

// @exact 0x10050490: instruction-for-instruction transcription of the original.
// 0x10050490 WantedLevelCrashFix: ecx = object; null returns 0. The normal
// path calls the vtable slot +0x128 and the resume consumes al and the flags
// of the following test.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_wanted_level_shim() {
  __asm {
    cmp ecx, 0
    je null_path
    mov eax, DWORD PTR [ecx]
    mov edx, DWORD PTR [eax + 0x128]
    call edx
    test al, al
    jmp DWORD PTR [zpatch_wanted_level_resume]
  null_path:
    pop ebx
    ret
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_wanted_level_shim() {
  __asm__ __volatile__("cmpl $0, %ecx\n"
                       "je 1f\n"
                       "movl (%ecx), %eax\n"
                       "movl 0x128(%eax), %edx\n"
                       "call *%edx\n"
                       "testb %al, %al\n"
                       "jmp *zpatch_wanted_level_resume\n"
                       "1:\n"
                       "popl %ebx\n"
                       "ret\n");
}
#else
[[maybe_unused]] void abi_wanted_level_shim() {}
#endif

// @exact 0x10036e90: instruction-for-instruction transcription of the original.
// 0x10036e90 GroupHackFix: eax = 0, ecx += 4, edx = 7, then the resume's
// first instruction tests the flags of [ecx] against 0.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_group_hack_shim() {
  __asm {
    xor eax, eax
    add ecx, 4
    lea edx, [eax + 7]
    cmp ecx, 0xC
    je null_path
    cmp DWORD PTR [ecx], 0
    jmp DWORD PTR [zpatch_group_hack_resume]
  null_path:
    ret
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_group_hack_shim() {
  __asm__ __volatile__("xorl %eax, %eax\n"
                       "addl $4, %ecx\n"
                       "leal 7(%eax), %edx\n"
                       "cmpl $0xC, %ecx\n"
                       "je 1f\n"
                       "cmpl $0, (%ecx)\n"
                       "jmp *zpatch_group_hack_resume\n"
                       "1:\n"
                       "ret\n");
}
#else
[[maybe_unused]] void abi_group_hack_shim() {}
#endif

// @exact 0x10046750: instruction-for-instruction transcription of the original.
// 0x10046750 SMPA60Fix: ecx = this; the sentinel 0xDDDDDDDD tails into the
// match+28 resume. The normal path pushes five arguments for the match+14
// resume before tailing into it.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_smpa60_shim() {
  __asm {
    cmp ecx, 0xDDDDDDDD
    je sentinel
    mov ecx, DWORD PTR [eax + 0x70]
    add eax, 0x40
    push eax
    push ecx
    lea edx, [ecx + 0x10]
    push edx
    push ecx
    push ecx
    jmp DWORD PTR [zpatch_smpa60_resume_b]
  sentinel:
    jmp DWORD PTR [zpatch_smpa60_resume_a]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_smpa60_shim() {
  __asm__ __volatile__("cmpl $0xDDDDDDDD, %ecx\n"
                       "je 1f\n"
                       "movl 0x70(%eax), %ecx\n"
                       "addl $0x40, %eax\n"
                       "pushl %eax\n"
                       "pushl %ecx\n"
                       "leal 0x10(%ecx), %edx\n"
                       "pushl %edx\n"
                       "pushl %ecx\n"
                       "pushl %ecx\n"
                       "jmp *zpatch_smpa60_resume_b\n"
                       "1:\n"
                       "jmp *zpatch_smpa60_resume_a\n");
}
#else
[[maybe_unused]] void abi_smpa60_shim() {}
#endif

// @exact 0x10046780: instruction-for-instruction transcription of the original.
// 0x10046780 SMPA60Fix tail shim: if the +0x14 field is a valid pointer (not
// null, not the 0xCDCDCDCD heap marker) copy [eax+0x10] into [ptr+0x10], then
// tail-jump to the resume the handler publishes (match+136, dword_10159C6C).
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_smpa60_tail_shim() {
  __asm {
    mov ecx, DWORD PTR [eax + 0x14]
    test ecx, ecx
    je skip
    cmp ecx, 0xCDCDCDCD
    je skip
    mov edx, DWORD PTR [eax + 0x10]
    mov DWORD PTR [ecx + 0x10], edx
  skip:
    jmp DWORD PTR [zpatch_smpa60_resume_c]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_smpa60_tail_shim() {
  __asm__ __volatile__("movl 0x14(%eax), %ecx\n"
                       "testl %ecx, %ecx\n"
                       "je 1f\n"
                       "cmpl $0xCDCDCDCD, %ecx\n"
                       "je 1f\n"
                       "movl 0x10(%eax), %edx\n"
                       "movl %edx, 0x10(%ecx)\n"
                       "1:\n"
                       "jmp *zpatch_smpa60_resume_c\n");
}
#else
[[maybe_unused]] void abi_smpa60_tail_shim() {}
#endif

// @exact 0x10036eb0: instruction-for-instruction transcription of the original.
// 0x10036eb0 FreezeGunFix: eax = object; if [eax+0x40] is a valid handle (not
// the 0xCDCDCDCD heap marker, not -1, not 0) tail-jump into the original path
// (push edi then the resume), else return 0. The null path pops esi and does
// ret 8 because the site sits just after the enclosing function's push esi.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_freeze_gun_shim() {
  __asm {
    mov ecx, DWORD PTR [eax + 0x38]
    test ecx, ecx
    mov esi, DWORD PTR [eax + 0x40]
    cmp esi, 0xCDCDCDCD
    je freeze_gun_null
    cmp esi, -1
    je freeze_gun_null
    cmp esi, 0
    je freeze_gun_null
    push edi
    jmp DWORD PTR [zpatch_freeze_gun_resume]
  freeze_gun_null:
    xor eax, eax
    pop esi
    ret 8
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_freeze_gun_shim() {
  __asm__ __volatile__("movl 0x38(%eax), %ecx\n"
                       "testl %ecx, %ecx\n"
                       "movl 0x40(%eax), %esi\n"
                       "cmpl $0xCDCDCDCD, %esi\n"
                       "je 1f\n"
                       "cmpl $-1, %esi\n"
                       "je 1f\n"
                       "cmpl $0, %esi\n"
                       "je 1f\n"
                       "pushl %edi\n"
                       "jmp *zpatch_freeze_gun_resume\n"
                       "1:\n"
                       "xorl %eax, %eax\n"
                       "popl %esi\n"
                       "ret $8\n");
}
#else
[[maybe_unused]] void abi_freeze_gun_shim() {}
#endif

// @exact 0x1003d9a0: instruction-for-instruction transcription of the original.
// 0x1003d9a0 PlayerAsPedComponentsFix: save eax, run the helper (which stores
// 1/0 in zpatch_player_ped_computed), restore, then push the result and ecx and
// tail-jump to the resume (match+14).
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_player_ped_components_shim() {
  __asm {
    pushad
    mov DWORD PTR [zpatch_player_ped_object], eax
    call abi_player_ped_helper
    popad
    push DWORD PTR [zpatch_player_ped_computed]
    push ecx
    jmp DWORD PTR [zpatch_player_ped_resume]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_player_ped_components_shim() {
  __asm__ __volatile__("pushal\n"
                       "movl %eax, zpatch_player_ped_object\n"
                       "call abi_player_ped_helper\n"
                       "popal\n"
                       "pushl zpatch_player_ped_computed\n"
                       "pushl %ecx\n"
                       "jmp *zpatch_player_ped_resume\n");
}
#else
[[maybe_unused]] void abi_player_ped_components_shim() {}
#endif

// 0x1001af60 EmissiveLerpFix: register-exact transcription of the original
// tramp. It saves ecx/edx, runs sub_1001AF20 (inlined below: the integer state
// machine over dword_10159CC0/CCC), then does the SSE tail and tail-jumps to
// the resume (match-12, game 0x7F8BAD).
//
// The tail must match the original bit-for-bit:
//   cvtsi2ss xmm0, edx        -> MERGES: keeps xmm0[127:32] from the caller
//   mulss    xmm0, [0x10159cc4]  -> multiplier is the slot's BYTES (match-16,
//                                  an address read as float), not a dereference
//   leave xmm1..xmm7 untouched  (the game keeps them live across the site)
// A compiled helper cannot be used: it may clobber xmm1..xmm7, and _mm_set_ss
// zeroes xmm0's upper bits. MSVC inline asm cannot spell cvtsi2ss/mulss, so the
// four-byte encodings are emitted. Not byte-exact (the helper is inlined), so
// verify-bodies reports this body as INFO.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_emissive_lerp_shim() {
  __asm {
    push eax
    mov DWORD PTR [zpatch_emissive_ecx], ecx
    mov DWORD PTR [zpatch_emissive_edx], edx
    // --- inlined sub_1001AF20 (0x1001af20) ---
    mov ecx, DWORD PTR [zpatch_emissive_ecx]
    xor edx, edx
    mov eax, DWORD PTR [zpatch_emissive_edx]
    shl ecx, 4
    sub ecx, DWORD PTR [zpatch_emissive_ecx]
    mov DWORD PTR [zpatch_emissive_edx], 0
    lea eax, [eax + ecx*4]
    mov ecx, 0Ch
    add eax, -405
    cmp eax, 2EDh
    __emit 0Fh
    __emit 46h
    __emit 0D1h                  // cmovbe edx, ecx
    mov DWORD PTR [zpatch_emissive_ecx], edx
    // --- reload ecx/edx exactly like the original tramp ---
    mov ecx, DWORD PTR [zpatch_emissive_ecx]
    mov edx, DWORD PTR [zpatch_emissive_edx]
    __emit 0F3h
    __emit 0Fh
    __emit 2Ah
    __emit 0C2h                  // cvtsi2ss xmm0, edx (merge upper bits)
    mov eax, OFFSET zpatch_emissive_const
    __emit 0F3h
    __emit 0Fh
    __emit 59h
    __emit 00h                   // mulss xmm0, DWORD PTR [eax]
    pop eax
    jmp DWORD PTR [zpatch_emissive_resume]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_emissive_lerp_shim() {
  __asm__ __volatile__(
      "pushl %eax\n"
      "movl %ecx, zpatch_emissive_ecx\n"
      "movl %edx, zpatch_emissive_edx\n"
      "movl zpatch_emissive_ecx, %ecx\n"
      "xorl %edx, %edx\n"
      "movl zpatch_emissive_edx, %eax\n"
      "shll $4, %ecx\n"
      "subl zpatch_emissive_ecx, %ecx\n"
      "movl $0, zpatch_emissive_edx\n"
      "leal (%eax,%ecx,4), %eax\n"
      "movl $12, %ecx\n"
      "addl $-405, %eax\n"
      "cmpl $0x2ed, %eax\n"
      ".byte 0x0f, 0x46, 0xd1\n"    /* cmovbe edx, ecx */
      "movl %edx, zpatch_emissive_ecx\n"
      "movl zpatch_emissive_ecx, %ecx\n"
      "movl zpatch_emissive_edx, %edx\n"
      ".byte 0xf3, 0x0f, 0x2a, 0xc2\n" /* cvtsi2ss xmm0, edx */
      "movl $zpatch_emissive_const, %eax\n"
      ".byte 0xf3, 0x0f, 0x59, 0x00\n" /* mulss xmm0, [eax] */
      "popl %eax\n"
      "jmp *zpatch_emissive_resume\n");
}
#else
[[maybe_unused]] void abi_emissive_lerp_shim() {}
#endif

// 0x10006880 CarPartsShadowFix: the original is
//   if (edi) return dword_1015BDC8(); else return dword_1015BDC0();
// i.e. a NAKED tail-jump through one of two handler-published game resume
// addresses (match+2 / match+0x45). Keep it naked so edi and the rest of the
// register state reach the resume untouched.
// @exact 0x100068A0 CarPartsShadowFix second shim (installed by the shared
// installer's other slot writers, not by 0x10006910): calls the published
// thunk [dword_1015B5B8] twice with (esi, 2|0, ebx, ebx, edi), pops the 20
// bytes of arguments and tail-jumps to [dword_1015BDCC]. The NOPs are in the
// original bytes (the thunk was assembled from a byte template).
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_carparts_shadow_2() {
  __asm {
    push esi
    nop
    push 2
    nop
    push ebx
    nop
    push ebx
    nop
    push edi
    nop
    call dword ptr [zpatch_carparts_shadow_fn]
    push esi
    nop
    push 0
    nop
    push ebx
    nop
    push ebx
    nop
    push edi
    nop
    call dword ptr [zpatch_carparts_shadow_fn]
    nop
    add  esp, 14h
    nop
    jmp  dword ptr [zpatch_carparts_resume_2]
    __emit 0CCh   // body delimiter for verify_hook_bodies
  }
}
#else
[[maybe_unused]] void abi_carparts_shadow_2() {}
#endif

// @exact 0x100068D0 CarDynamicShadowFix's second shadow-submission body (the
// pattern-B variant, unused on 1.0.8.0 where pattern A is the one found): same
// double call through [dword_1015B5B8] with (esi, 2, 0, 0, edi) / (esi, 0, 0,
// 0, edi), 20 bytes of arguments popped, tail-jump to [dword_1015BDCC].
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_carparts_shadow_2b() {
  __asm {
    push esi
    nop
    push 2
    nop
    push 0
    nop
    push 0
    nop
    push edi
    nop
    call dword ptr [zpatch_carparts_shadow_fn]
    nop
    push esi
    nop
    push 0
    nop
    push 0
    nop
    push 0
    nop
    push edi
    nop
    call dword ptr [zpatch_carparts_shadow_fn]
    nop
    add  esp, 14h
    nop
    jmp  dword ptr [zpatch_carparts_resume_2]
    __emit 0CCh   // body delimiter for verify_hook_bodies
  }
}
#else
[[maybe_unused]] void abi_carparts_shadow_2b() {}
#endif

// @exact 0x10006880 CarPartsShadowFix shim. The original disassembly is (the
// NOPs are in the original bytes):
//   cmp edi, 0 / nop / je else / nop
//   movzx ebx, word ptr [edi+1Ch] / nop / xor esi, esi / nop
//   test ebx, ebx / nop
//   jmp dword ptr [dword_1015BDC8]        ; resume = match+2
// else: nop / jmp dword ptr [dword_1015BDC0]   ; resume = match+0x45
// The EBX/ESI setup and its flags are consumed by the resumed game code - the
// session-6 port omitted them (the decompiled C hides them) and crashed the
// game right before world rendering after a save load.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_carparts_shadow() {
  __asm {
    cmp  edi, 0
    nop
    je   carparts_else
    nop
    movzx ebx, word ptr [edi + 1Ch]
    nop
    xor  esi, esi
    nop
    test ebx, ebx
    nop
    jmp  dword ptr [zpatch_carparts_resume_if]
  carparts_else:
    nop
    jmp  dword ptr [zpatch_carparts_resume_else]
    __emit 0CCh   // body delimiter for verify_hook_bodies
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_carparts_shadow() {
  __asm__ __volatile__(
      "cmpl $0, %edi\n"
      "nop\n"
      "je 1f\n"
      "nop\n"
      "movzwl 0x1c(%edi), %ebx\n"
      "nop\n"
      "xorl %esi, %esi\n"
      "nop\n"
      "testl %ebx, %ebx\n"
      "nop\n"
      "jmp *zpatch_carparts_resume_if\n"
      "1:\n"
      "nop\n"
      "jmp *zpatch_carparts_resume_else\n");
}
#else
[[maybe_unused]] void abi_carparts_shadow() {}
#endif

// @exact 0x10044530 PopupHeadlightsFlag (site pattern "0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1",
// live on 1.0.8.0 at game VA 0x00A53A64). The original disassembly is:
//   movsx eax, word ptr [esi+2Eh]        ; entry EAX is dead
//   pushad
//   mov   dword_1015BE20, eax
//   call  sub_10044310                   ; fills index/flag/angle globals
//   popad                                ; restores ECX/EDX/ESI/...; EAX = model index
//   cmp   dword_1015BE24, 1              ; ZF=1 -> resume A (fall-through path)
//   je    resume_a
//   cmp   eax, dword_1015BE1C            ; flags for resume B (invariantly ZF=0)
//   jmp   dword_1015D62C                 ; resume B = site+0xA (75->EB skip path)
// resume_a:
//   jmp   dword_1015D628                 ; resume A = site+0xC
// pushad/popal is essential: the helper may clobber every GPR, and the resumes
// consume ECX/EDX/ESI exactly as the site left them.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_popup_headlights() {
  __asm {
    movsx eax, word ptr [esi + 2Eh]
    pushad
    mov   dword ptr [zpatch_popup_model], eax
    call  abi_popup_check
    popad
    cmp   dword ptr [zpatch_popup_flag], 1
    je    popup_resume_a
    cmp   eax, dword ptr [zpatch_popup_index]
    jmp   dword ptr [zpatch_popup_resume_b]
  popup_resume_a:
    jmp   dword ptr [zpatch_popup_resume_a]
  }
}
// @exact 0x10044560: the EDX twin for the second site pattern (absent on 1.0.8.0).
[[maybe_unused]] __declspec(naked) void abi_popup_headlights_edx() {
  __asm {
    movsx edx, word ptr [esi + 2Eh]
    pushad
    mov   dword ptr [zpatch_popup_model], edx
    call  abi_popup_check
    popad
    cmp   dword ptr [zpatch_popup_flag], 1
    je    popup_edx_resume_a
    cmp   edx, dword ptr [zpatch_popup_index]
    jmp   dword ptr [zpatch_popup_resume_b]
  popup_edx_resume_a:
    jmp   dword ptr [zpatch_popup_resume_a]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_popup_headlights() {
  __asm__ __volatile__(
      "movsx 0x2e(%esi), %eax\n"
      "pushal\n"
      "movl %eax, zpatch_popup_model\n"
      "call abi_popup_check\n"
      "popal\n"
      "cmpl $1, zpatch_popup_flag\n"
      "je 1f\n"
      "cmpl zpatch_popup_index, %eax\n"
      "jmp *zpatch_popup_resume_b\n"
      "1:\n"
      "jmp *zpatch_popup_resume_a\n");
}
[[maybe_unused]] __attribute__((naked)) void abi_popup_headlights_edx() {
  __asm__ __volatile__(
      "movsx 0x2e(%esi), %edx\n"
      "pushal\n"
      "movl %edx, zpatch_popup_model\n"
      "call abi_popup_check\n"
      "popal\n"
      "cmpl $1, zpatch_popup_flag\n"
      "je 1f\n"
      "cmpl zpatch_popup_index, %edx\n"
      "jmp *zpatch_popup_resume_b\n"
      "1:\n"
      "jmp *zpatch_popup_resume_a\n");
}
#else
[[maybe_unused]] void abi_popup_headlights() {}
[[maybe_unused]] void abi_popup_headlights_edx() {}
#endif

// @exact 0x1001ADB0 ForceCarHeadlightShadows hook thunk. The original installer E9s
// the game function at pattern "55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B"
// (game 0x8F7590, resume = match+12) and its body is:
//   mov  dword_1015BDD0, ecx          ; ECX = a1 (object)
//   pushal
//   call sub_1001AD20                 ; per-model offset check
//   popal
//   push ebp / mov ebp,esp / and esp,-16 / sub esp,0xD4   ; replayed prologue
//   jmp  dword_1015B5BC               ; resume, entry ECX/EDX restored by popal
// The replay is essential: those 12 bytes belong to the function the E9 hook
// replaced, so the resume must run with the frame already established.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_fchs_shim() {
  __asm {
    mov  dword ptr [zpatch_fchs_param], ecx
    pushad
    call abi_fchs_check
    popad
    push ebp
    mov  ebp, esp
    and  esp, 0FFFFFFF0h
    sub  esp, 0D4h
    jmp  dword ptr [zpatch_fchs_resume]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_fchs_shim() {
  __asm__ __volatile__(
      "movl %ecx, zpatch_fchs_param\n"
      "pushal\n"
      "call abi_fchs_check\n"
      "popal\n"
      "pushl %ebp\n"
      "movl %esp, %ebp\n"
      "andl $0xfffffff0, %esp\n"
      "subl $0xd4, %esp\n"
      "jmp *zpatch_fchs_resume\n");
}
#else
[[maybe_unused]] void abi_fchs_shim() {}
#endif

// DualVehicleHeadlights (handler 0x10006770, E9 at the game match): the bodies
// 0x100064F0 (new builds) / 0x10006630 (builds 1..6) duplicate the headlight-cone
// draw call: they save every GPR (the game's custom convention passes arguments
// in eax/edx/ecx/ebx/ebp/edi/esi plus stack floats), draw light slot 1 and slot 2
// through the handler-published game pointers, restore the GPRs and tail-jump to
// the published continuation (match+338). The two bodies differ only in the
// float-argument order of the first draw. Register-exact transcription; the
// helper sets the two constants the original's sub_100064D0 sets.
#if defined(_MSC_VER)
extern "C" __declspec(naked) void abi_dual_headlights_init() {
  __asm {
    mov DWORD PTR [zpatch_dual_light_a], 1
    mov DWORD PTR [zpatch_dual_light_b], 2
    ret
    __emit 0CCh   // body delimiter for verify_hook_bodies
  }
}
// @exact 0x100064F0
extern "C" __declspec(naked) void abi_dual_headlights_new() {
  __asm {
    pushad
    call abi_dual_headlights_init
    popad
    mov DWORD PTR [zpatch_dual_r_eax], eax
    mov DWORD PTR [zpatch_dual_r_ebx], ebx
    mov DWORD PTR [zpatch_dual_r_ecx], ecx
    mov DWORD PTR [zpatch_dual_r_edx], edx
    mov DWORD PTR [zpatch_dual_r_esi], esi
    mov DWORD PTR [zpatch_dual_r_edi], edi
    mov DWORD PTR [zpatch_dual_r_ebp], ebp
    mov DWORD PTR [zpatch_dual_r_esp], esp
    push 0
    call DWORD PTR [zpatch_dual_bd0]
    add esp, 4
    push eax
    mov ecx, edi
    call DWORD PTR [zpatch_dual_be4]
    push eax
    mov eax, DWORD PTR [esp + 30h]
    fld DWORD PTR [esp + 1Ch]
    push 1
    push DWORD PTR [zpatch_dual_light_a]
    mov ecx, DWORD PTR [ebp + 24h]
    sub esp, 10h
    fstp DWORD PTR [esp + 0Ch]
    add eax, 30h
    fld DWORD PTR [esp + 38h]
    fstp DWORD PTR [esp + 8]
    fld DWORD PTR [esp + 30h]
    fstp DWORD PTR [esp + 4]
    fld DWORD PTR [esp + 2Ch]
    fstp DWORD PTR [esp]
    push eax
    push ecx
    movsx edx, BYTE PTR [edi + 40h]
    mov esi, DWORD PTR [edi + 48h]
    mov edi, DWORD PTR [zpatch_dual_bcc]
    lea ecx, [esp + 54h]
    call DWORD PTR [zpatch_dual_bd4]
    add esp, 24h
    mov eax, DWORD PTR [zpatch_dual_r_eax]
    mov ebx, DWORD PTR [zpatch_dual_r_ebx]
    mov ecx, DWORD PTR [zpatch_dual_r_ecx]
    mov edx, DWORD PTR [zpatch_dual_r_edx]
    mov esi, DWORD PTR [zpatch_dual_r_esi]
    mov edi, DWORD PTR [zpatch_dual_r_edi]
    mov ebp, DWORD PTR [zpatch_dual_r_ebp]
    mov esp, DWORD PTR [zpatch_dual_r_esp]
    push 0
    call DWORD PTR [zpatch_dual_bd0]
    add esp, 4
    push eax
    mov ecx, edi
    call DWORD PTR [zpatch_dual_be4]
    fld DWORD PTR [esp + 18h]
    mov ecx, DWORD PTR [esp + 28h]
    push eax
    push 1
    push DWORD PTR [zpatch_dual_light_b]
    mov eax, DWORD PTR [ebp + 24h]
    sub esp, 10h
    fstp DWORD PTR [esp + 0Ch]
    add ecx, 30h
    fld DWORD PTR [esp + 38h]
    fstp DWORD PTR [esp + 8]
    fld DWORD PTR [esp + 30h]
    fstp DWORD PTR [esp + 4]
    fld DWORD PTR [esp + 2Ch]
    fstp DWORD PTR [esp]
    push ecx
    push eax
    movsx edx, BYTE PTR [edi + 40h]
    mov esi, DWORD PTR [edi + 48h]
    mov edi, DWORD PTR [zpatch_dual_bcc]
    lea ecx, [esp + 54h]
    call DWORD PTR [zpatch_dual_bd4]
    add esp, 24h
    jmp DWORD PTR [zpatch_dual_be0]
    __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
  }
}
// @exact 0x10006630
extern "C" __declspec(naked) void abi_dual_headlights_old() {
  __asm {
    pushad
    call abi_dual_headlights_init
    popad
    mov DWORD PTR [zpatch_dual_r_eax], eax
    mov DWORD PTR [zpatch_dual_r_ebx], ebx
    mov DWORD PTR [zpatch_dual_r_ecx], ecx
    mov DWORD PTR [zpatch_dual_r_edx], edx
    mov DWORD PTR [zpatch_dual_r_esi], esi
    mov DWORD PTR [zpatch_dual_r_edi], edi
    mov DWORD PTR [zpatch_dual_r_ebp], ebp
    mov DWORD PTR [zpatch_dual_r_esp], esp
    push 0
    call DWORD PTR [zpatch_dual_bd0]
    add esp, 4
    push eax
    mov ecx, edi
    call DWORD PTR [zpatch_dual_be4]
    push eax
    mov eax, DWORD PTR [esp + 30h]
    fld DWORD PTR [esp + 20h]
    push 1
    push DWORD PTR [zpatch_dual_light_a]
    mov ecx, DWORD PTR [ebp + 24h]
    sub esp, 10h
    fstp DWORD PTR [esp + 0Ch]
    add eax, 30h
    fld DWORD PTR [esp + 30h]
    fstp DWORD PTR [esp + 8]
    fld DWORD PTR [esp + 2Ch]
    fstp DWORD PTR [esp + 4]
    fld DWORD PTR [esp + 34h]
    fstp DWORD PTR [esp]
    push eax
    push ecx
    movsx edx, BYTE PTR [edi + 40h]
    mov esi, DWORD PTR [edi + 48h]
    mov edi, DWORD PTR [zpatch_dual_bcc]
    lea ecx, [esp + 54h]
    call DWORD PTR [zpatch_dual_bd4]
    add esp, 24h
    mov eax, DWORD PTR [zpatch_dual_r_eax]
    mov ebx, DWORD PTR [zpatch_dual_r_ebx]
    mov ecx, DWORD PTR [zpatch_dual_r_ecx]
    mov edx, DWORD PTR [zpatch_dual_r_edx]
    mov esi, DWORD PTR [zpatch_dual_r_esi]
    mov edi, DWORD PTR [zpatch_dual_r_edi]
    mov ebp, DWORD PTR [zpatch_dual_r_ebp]
    mov esp, DWORD PTR [zpatch_dual_r_esp]
    push 0
    call DWORD PTR [zpatch_dual_bd0]
    add esp, 4
    push eax
    mov ecx, edi
    call DWORD PTR [zpatch_dual_be4]
    fld DWORD PTR [esp + 1Ch]
    mov ecx, DWORD PTR [esp + 28h]
    push eax
    push 1
    push DWORD PTR [zpatch_dual_light_b]
    mov eax, DWORD PTR [ebp + 24h]
    sub esp, 10h
    fstp DWORD PTR [esp + 0Ch]
    add ecx, 30h
    fld DWORD PTR [esp + 30h]
    fstp DWORD PTR [esp + 8]
    fld DWORD PTR [esp + 2Ch]
    fstp DWORD PTR [esp + 4]
    fld DWORD PTR [esp + 34h]
    fstp DWORD PTR [esp]
    push ecx
    push eax
    movsx edx, BYTE PTR [edi + 40h]
    mov esi, DWORD PTR [edi + 48h]
    mov edi, DWORD PTR [zpatch_dual_bcc]
    lea ecx, [esp + 54h]
    call DWORD PTR [zpatch_dual_bd4]
    add esp, 24h
    jmp DWORD PTR [zpatch_dual_be0]
    __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
  }
}
#else
// 32-bit GNU hosts are not a shipped target; the MSVC ASI is what the resolver
// serves (keep the symbols so the resolver table compiles).
extern "C" void abi_dual_headlights_init() {}
extern "C" void abi_dual_headlights_new() {}
extern "C" void abi_dual_headlights_old() {}
#endif

// @exact 0x100512F0 LocalSettingsPath detour: entered via E9 at the path-setup
// function's epilogue (game 0x856EE6, swallowing `pop edi; pop esi; pop ebx;
// ret` plus one int3 pad byte), runs the path-rewrite helper between
// pushad/popad and replays the swallowed epilogue.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_local_settings_detour() {
  __asm {
    pushad
    call zpatch_local_settings_step
    popad
    pop edi
    pop esi
    pop ebx
    ret
    __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after ret)
  }
}
// @exact 0x10051300: fallback-build detour (pattern absent on 1.0.8.0); same
// helper, then re-issues the original 5-arg call through ESI and the two-pop
// epilogue.
[[maybe_unused]] __declspec(naked) void abi_local_settings_fallback_detour() {
  __asm {
    pushad
    call zpatch_local_settings_step
    popad
    push 0
    push 0
    push 1Ch
    push 0
    call esi
    pop esi
    pop edi
    ret
    __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after ret)
  }
}
#else
[[maybe_unused]] void abi_local_settings_detour() {}
[[maybe_unused]] void abi_local_settings_fallback_detour() {}
#endif

// @exact 0x100051B0 Trio primary detour: the installer E9s the game site at
// match+2 (game 0xA538A0); the body caches ESI (the vehicle), runs the shared
// consumer between pushad/popad, calls vfunc 296 (al = its result) and
// tail-jumps to the resume (match+16, the `jne` after the replayed test).
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_steer_detour_eax() {
  __asm {
    mov  dword ptr [zpatch_steer_vehicle], esi
    pushad
    call abi_steer_body
    popad
    mov  edx, dword ptr [esi]
    mov  eax, dword ptr [edx + 128h]
    mov  ecx, esi
    call eax
    test al, al
    jmp  dword ptr [zpatch_steer_resume]
  }
}
// @exact 0x100051E0 Fallback-build detour (pattern absent on 1.0.8.0): same,
// with the vfunc fetched through EAX/EDX instead.
[[maybe_unused]] __declspec(naked) void abi_steer_detour_edx() {
  __asm {
    mov  dword ptr [zpatch_steer_vehicle], esi
    pushad
    call abi_steer_body
    popad
    mov  eax, dword ptr [esi]
    mov  edx, dword ptr [eax + 128h]
    mov  ecx, esi
    call edx
    test al, al
    jmp  dword ptr [zpatch_steer_resume]
  }
}
#else
[[maybe_unused]] void abi_steer_detour_eax() {}
[[maybe_unused]] void abi_steer_detour_edx() {}
#endif

// @exact 0x10046C80 GarageModelFix body A (installed at game 0xC68BB0):
// pushal; ecx=object(esi), edx=vehicle(edi); call the store helper; popal;
// replay `mov edx,[edi+0x20]; fld dword [edx+0x34]` and tail-jump to the
// resume (match+6). The NOPs are not present here - the original body is
// straight-line.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_garage_body_a() {
  __asm {
    pushad
    mov  ecx, esi
    mov  edx, edi
    call zpatch_garage_store_model
    popad
    mov  edx, dword ptr [edi + 20h]
    fld  dword ptr [edx + 34h]
    jmp  dword ptr [zpatch_garage_resume_a]
    __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
  }
}
// @exact 0x10046CA0 GarageModelFix body B (installed at game 0x9659C3):
// pushal; call the lookup helper (object in ECX); popal; replay
// `movzx ecx,[esi]; push eax; push ecx` and tail-jump to the resume (match+5).
[[maybe_unused]] __declspec(naked) void abi_garage_body_b() {
  __asm {
    pushad
    call zpatch_garage_lookup_model
    popad
    movzx ecx, word ptr [esi]
    push eax
    push ecx
    jmp  dword ptr [zpatch_garage_resume_b]
    __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
  }
}
#else
[[maybe_unused]] void abi_garage_body_a() {}
[[maybe_unused]] void abi_garage_body_b() {}
#endif

// ---------------------------------------------------------------------------
// FreezeCarFix / DebugPrintSpawns bodies (original sub_100372F0 guard, the
// ten factory-dispatch thunks and the sub_10037510 spawn probe; the installer
// 0x10037C70 hooks them and the FreezeCarFix loader arm gates the install).
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
// @exact 0x100372F0
[[maybe_unused]] __declspec(naked) void abi_spawns_guard() {
  __asm {
    push ebp
    mov ebp, esp
    cmp dword ptr [zpatch_spawns_skip], 0
    jne spawns_guard_taken
    push dword ptr [ebp + 10h]
    mov eax, dword ptr [zpatch_spawns_orig_b]
    push dword ptr [ebp + 0Ch]
    push dword ptr [ebp + 8]
    call eax
    mov dword ptr [zpatch_spawns_skip], 0
    pop ebp
    ret 0Ch
  spawns_guard_taken:
    xor eax, eax
    mov dword ptr [zpatch_spawns_skip], eax
    pop ebp
    ret 0Ch
    __emit 0CCh
  }
}

// ---------------------------------------------------------------------------
// 7. the ten factory-dispatch thunks (P3 sites; selector + captured byte)
//    NOTE case 1 (+0x3D) and case 2 (+0x56) both set selector 3 in the
//    original bytes - kept 1:1.
// ---------------------------------------------------------------------------

// @exact 0x10037330
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0x24() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 5
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case0_resume]
    __emit 0CCh
  }
}

// @exact 0x100373F0
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0x3D() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 3
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case1_resume]
    __emit 0CCh
  }
}

// @exact 0x10037420
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0x56() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 3
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case2_resume]
    __emit 0CCh
  }
}

// @exact 0x10037390
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0x6F() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 0
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case3_resume]
    __emit 0CCh
  }
}

// @exact 0x10037450
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0x88() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 4
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case4_resume]
    __emit 0CCh
  }
}

// @exact 0x10037480
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0x9E() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 6
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case5_resume]
    __emit 0CCh
  }
}

// @exact 0x100374E0
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0xB4() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 7
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case6_resume]
    __emit 0CCh
  }
}

// @exact 0x10037360
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0xCA() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 1
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case7_resume]
    __emit 0CCh
  }
}

// @exact 0x100373C0
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0xE0() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 2
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case8_resume]
    __emit 0CCh
  }
}

// @exact 0x100374B0
[[maybe_unused]] __declspec(naked) void abi_spawns_case_0xF6() {
  __asm {
    push ebp
    mov ebp, esp
    mov ecx, dword ptr [ebp + 0Ch]
    movsx eax, cl
    mov dword ptr [zpatch_spawns_kind], 8
    mov dword ptr [zpatch_spawns_class], eax
    mov dword ptr [ebp + 0Ch], ecx
    pop ebp
    jmp dword ptr [zpatch_spawns_case9_resume]
    __emit 0CCh
  }
}


// ---------------------------------------------------------------------------
// 8. sub_10037510 - the probe (P1/P4/P5/P6/P7 sites); byte-exact naked body
//    394 instructions, 0x10037510..0x10037ABC. ecx at the [vtable+0xC] call
//    is the selector, exactly like the original - do not "fix" it.
// ---------------------------------------------------------------------------
// @exact 0x10037510
[[maybe_unused]] __declspec(naked) void abi_spawns_probe() {
  __asm {

    push ebp
    mov ebp, esp
    sub esp, 4Ch
    mov eax, dword ptr [__security_cookie]
    xor eax, ebp
    mov dword ptr [ebp - 8], eax
    push esi
    mov esi, dword ptr [zpatch_pre_ped_array]
    xor eax, eax
    push edi
    mov edi, dword ptr [ebp + 8]
    __emit 0Fh
    __emit 1Fh
    __emit 00h
L_10037530:
    mov ecx, dword ptr [esi + eax*4]
    test ecx, ecx
    je L_10037540
    cmp dword ptr [ecx + 3Ch], edi
    je L_100375FA
L_10037540:
    inc eax
    cmp eax, 7918h
    jl L_10037530
    or eax, 0FFFFFFFFh
L_1003754B:
    mov ecx, dword ptr [zpatch_spawns_kind]
    cwde
    mov dword ptr [zpatch_spawns_kind], 0FFFFFFFFh
    mov esi, dword ptr [esi + eax*4]
    cmp eax, -1
    je L_10037AA2
    test esi, esi
    je L_10037AA2
    cmp esi, 0Ch
    je L_10037AA2
    cmp esi, 0CDCDCDCDh
    je L_10037AA2
    mov eax, dword ptr [esi]
    test eax, eax
    je L_10037AA2
    cmp eax, 0Ch
    je L_10037AA2
    cmp eax, 0CDCDCDCDh
    je L_10037AA2
    test ecx, ecx
    jne L_10037635
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 5
    jne L_10037602
    cmp dword ptr [esi + 6Ch], 0
    jne L_10037602
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_car
    jmp L_10037A8C
L_100375FA:
    movzx eax, ax
    jmp L_1003754B
L_10037602:
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_nonspawned_car
    jmp L_10037A27
L_10037635:
    cmp ecx, 1
    jne L_100376C0
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 5
    jne L_1003768D
    cmp dword ptr [esi + 6Ch], 4
    jne L_1003768D
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_heli
    jmp L_10037A8C
L_1003768D:
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_nonspawned_heli
    jmp L_10037A27
L_100376C0:
    cmp ecx, 2
    jne L_1003774B
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 5
    jne L_10037718
    cmp dword ptr [esi + 6Ch], 2
    jne L_10037718
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_boat
    jmp L_10037A8C
L_10037718:
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_nonspawned_boat
    jmp L_10037A27
L_1003774B:
    cmp ecx, 6
    jne L_100377D6
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 5
    jne L_100377A3
    cmp dword ptr [esi + 6Ch], 1
    jne L_100377A3
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_bike
    jmp L_10037A8C
L_100377A3:
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_nonspawned_bike
    jmp L_10037A27
L_100377D6:
    cmp ecx, 7
    jne L_10037861
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 5
    jne L_1003782E
    cmp dword ptr [esi + 6Ch], 3
    jne L_1003782E
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_train
    jmp L_10037A8C
L_1003782E:
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_nonspawned_train
    jmp L_10037A27
L_10037861:
    cmp ecx, 8
    jne L_100378EC
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 5
    jne L_100378B9
    cmp dword ptr [esi + 6Ch], 5
    jne L_100378B9
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_plane
    jmp L_10037A8C
L_100378B9:
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_nonspawned_plane
    jmp L_10037A27
L_100378EC:
    cmp ecx, 3
    jne L_1003796D
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 6
    je L_1003792D
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_nonspawned_ped
    jmp L_10037A27
L_1003792D:
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_ped
    jmp L_10037A8C
L_1003796D:
    cmp ecx, 4
    jne L_100379EB
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 5
    jne L_100379AB
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_vehicle_object
    jmp L_10037A27
L_100379AB:
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_object
    jmp L_10037A8C
L_100379EB:
    cmp ecx, 5
    jne L_10037AA2
    mov eax, dword ptr [eax + 0Ch]
    call eax
    cmp al, 6
    je L_10037A59
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037A3D
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_used_nonped
L_10037A27:
    lea eax, [ebp - 4Ch]
    push 40h
    push eax
    call zpatch_spawns_sprintf
    add esp, 14h
    lea edx, [ebp - 4Ch]
    call zpatch_spawns_print_edx
L_10037A3D:
    mov dword ptr [zpatch_spawns_skip], 1
    xor eax, eax
    pop edi
    pop esi
    mov ecx, dword ptr [ebp - 8]
    xor ecx, ebp
    call zpatch_spawns_cookie_check_ecx
    mov esp, ebp
    pop ebp
    ret
L_10037A59:
    cmp byte ptr [zpatch_flag_debug_spawns], 0
    je L_10037AA2
    push OFFSET zpatch_str_gtac
    call dword ptr [zpatch_spawns_getmodulehandlew]
    test eax, eax
    je L_10037AA2
    mov ecx, edi
    call zpatch_spawns_name_ecx
    mov edx, dword ptr [zpatch_spawns_class]
    mov esi, eax
    call zpatch_spawns_who_edx
    push esi
    push eax
    push OFFSET zpatch_fmt_spawned_generic
L_10037A8C:
    lea eax, [ebp - 4Ch]
    push 40h
    push eax
    call zpatch_spawns_sprintf
    add esp, 14h
    lea edx, [ebp - 4Ch]
    call zpatch_spawns_print_edx
L_10037AA2:
    mov eax, dword ptr [zpatch_spawns_orig_a]
    push edi
    call eax
    mov ecx, dword ptr [ebp - 8]
    add esp, 4
    xor ecx, ebp
    pop edi
    pop esi
    call zpatch_spawns_cookie_check_ecx
    mov esp, ebp
    pop ebp
    ret
    __emit 0CCh
  }
}

#else
[[maybe_unused]] void abi_spawns_guard() {}
[[maybe_unused]] void abi_spawns_case_0x24() {}
[[maybe_unused]] void abi_spawns_case_0x3D() {}
[[maybe_unused]] void abi_spawns_case_0x56() {}
[[maybe_unused]] void abi_spawns_case_0x6F() {}
[[maybe_unused]] void abi_spawns_case_0x88() {}
[[maybe_unused]] void abi_spawns_case_0x9E() {}
[[maybe_unused]] void abi_spawns_case_0xB4() {}
[[maybe_unused]] void abi_spawns_case_0xCA() {}
[[maybe_unused]] void abi_spawns_case_0xE0() {}
[[maybe_unused]] void abi_spawns_case_0xF6() {}
[[maybe_unused]] void abi_spawns_probe() {}
#endif

// @exact 0x1001B2E0 CarDecalsFix: the original scans the game for
// "8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00" (unique on
// 1.0.8.0, game 0x6F7F50) with its multi-hit collector, patches the FIRST match
// with an E9 to this function (and shows the "loaded twice" box when there is
// no match), so this replaces the game function's entry:
//   a2 = [esp+8]; if ((a2[0x28] & 0x3C0) == 0x80) { a1 = [esp+4];
//      if (a1[0x38]) return a1[0x34]; } else { a1 = [esp+4]; }
//   return a1[0x30];   (__stdcall, ret 8)
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_car_decals_fix() {
  __asm {
    mov eax, dword ptr [esp + 8]
    mov ecx, dword ptr [eax + 28h]
    and ecx, 3C0h
    cmp ecx, 80h
    jne car_decals_other
    mov eax, dword ptr [esp + 4]
    cmp dword ptr [eax + 38h], 0
    je  car_decals_load
    mov eax, dword ptr [eax + 34h]
    ret 8
  car_decals_other:
    mov eax, dword ptr [esp + 4]
  car_decals_load:
    mov eax, dword ptr [eax + 30h]
    ret 8
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_car_decals_fix() {
  __asm__ __volatile__(
      "movl 0x8(%esp), %eax\n"
      "movl 0x28(%eax), %ecx\n"
      "andl $0x3c0, %ecx\n"
      "cmpl $0x80, %ecx\n"
      "jne 1f\n"
      "movl 0x4(%esp), %eax\n"
      "cmpl $0, 0x38(%eax)\n"
      "je 2f\n"
      "movl 0x34(%eax), %eax\n"
      "ret $8\n"
      "1:\n"
      "movl 0x4(%esp), %eax\n"
      "2:\n"
      "movl 0x30(%eax), %eax\n"
      "ret $8\n");
}
#else
[[maybe_unused]] void abi_car_decals_fix() {}
#endif

// @exact 0x100054E0 Patch4RemoveModChecks / RemoveGFWLUpdatePopup stub: run the
// flag-gated patch helper between pushad/popad, then replay the original
// function's first two instructions (its 64-bit subtraction of ECX:ESI from
// EDX:EAX into the caller's stack slots) and tail-jump to the real
// continuation (the handler publishes match+12).
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_gfwl_stub() {
  __asm {
    pushad
    call abi_gfwl_patch
    popad
    sub  eax, ecx
    sbb  edx, esi
    mov  dword ptr [esp + 0Ch], eax
    mov  dword ptr [esp + 10h], edx
    jmp  dword ptr [zpatch_gfwl_resume]
    __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_gfwl_stub() {
  __asm__ __volatile__(
      "pushal\n"
      "call abi_gfwl_patch\n"
      "popal\n"
      "subl %ecx, %eax\n"
      "sbbl %esi, %edx\n"
      "movl %eax, 0xc(%esp)\n"
      "movl %edx, 0x10(%esp)\n"
      "jmp *zpatch_gfwl_resume\n");
}
#else
[[maybe_unused]] void abi_gfwl_stub() {}
#endif

// @exact 0x10037BA0 BadPlayerModelCrashFix trampoline. The handler E9s the game
// function at pattern "83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C" (game 0x7CF803,
// 3 bytes into the function) and publishes match+0xA as the resume. The body:
//   capture [ebp+0xC] (entity index) and [ebp+0x10] (model index) into the
//   check's globals; pushal/call/popal; write the possibly-corrected model
//   index back into the [ebp+0x10] argument slot; replay the overwritten
//   prologue (and esp,0xF0; sub esp,0x54; xorps xmm0,xmm0; push ebx) and
//   tail-jump to the resume. EBP is the game function's live frame.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_bad_player_model() {
  __asm {
    push edi
    mov  edi, dword ptr [ebp + 0Ch]
    mov  dword ptr [zpatch_bpm_obj], edi
    mov  edi, dword ptr [ebp + 10h]
    mov  dword ptr [zpatch_bpm_model_index], edi
    pushad
    call abi_bpm_check
    popad
    mov  edi, dword ptr [zpatch_bpm_model_index]
    mov  dword ptr [ebp + 10h], edi
    pop  edi
    and  esp, 0FFFFFFF0h
    sub  esp, 54h
    xorps xmm0, xmm0
    push ebx
    jmp  dword ptr [zpatch_bpm_resume]
    __emit 0CCh   // body delimiter for verify_hook_bodies (unreachable after jmp)
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_bad_player_model() {
  __asm__ __volatile__(
      "pushl %edi\n"
      "movl 0xc(%ebp), %edi\n"
      "movl %edi, zpatch_bpm_obj\n"
      "movl 0x10(%ebp), %edi\n"
      "movl %edi, zpatch_bpm_model_index\n"
      "pushal\n"
      "call abi_bpm_check\n"
      "popal\n"
      "movl zpatch_bpm_model_index, %edi\n"
      "movl %edi, 0x10(%ebp)\n"
      "popl %edi\n"
      "andl $0xfffffff0, %esp\n"
      "subl $0x54, %esp\n"
      "xorps %xmm0, %xmm0\n"
      "pushl %ebx\n"
      "jmp *zpatch_bpm_resume\n");
}
#else
[[maybe_unused]] void abi_bad_player_model() {}
#endif

// @exact 0x100521E0 NewSavesCompatibility tramp. The handler E9s the savegame-load
// entry at "89 84 24 E0 01 00 00 8B 45 08 53 33 DB" (game 0x85B093) so that
// this tramp runs the converter first, then replicates the overwritten
// instruction (the stack-cookie store), the next two instructions and the tail
// jump to the handler-published resume (match+13). Entry is by JMP, so the
// [esp+0x1E0] store targets the same game frame slot the original instruction
// would have written.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_new_saves_compat() {
  __asm {
    pushad
    call abi_new_saves_prepare
    popad
    mov  dword ptr [esp + 1E0h], eax
    mov  eax, dword ptr [ebp + 8]
    push ebx
    xor  ebx, ebx
    jmp  dword ptr [zpatch_new_saves_resume]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_new_saves_compat() {
  __asm__ __volatile__(
      "pushal\n"
      "call abi_new_saves_prepare\n"
      "popal\n"
      "movl %eax, 0x1e0(%esp)\n"
      "movl 0x8(%ebp), %eax\n"
      "pushl %ebx\n"
      "xorl %ebx, %ebx\n"
      "jmp *zpatch_new_saves_resume\n");
}
#else
[[maybe_unused]] void abi_new_saves_compat() {}
#endif

// 0x100469e0 SuperLODFix: not byte-exact (calls a compiled helper). The fail
// path resumes via dword_101580AC, the ok path via dword_101580B0; both are
// handler-published base+rva values (build-gated).
#if defined(_MSC_VER)
// @exact 0x100469E0 - hand-written transcription, byte-compared.
[[maybe_unused]] __declspec(naked) void abi_super_lod_fix_shim() {
  __asm {
    jmp slod_main
  slod_fail:
    jmp DWORD PTR [zpatch_super_lod_fail]
  slod_main:
    push eax
    push ecx
    push esi
    call abi_super_lod_helper
    add esp, 4
    pop ecx
    test al, al
    pop eax
    jne slod_ok
    test byte ptr [eax + 0x72], 0x10
    je slod_fail
  slod_ok:
    jmp DWORD PTR [zpatch_super_lod_ok]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_super_lod_fix_shim() {
  __asm__ __volatile__("jmp 2f\n"
                       "1:\n"
                       "jmp *zpatch_super_lod_fail\n"
                       "2:\n"
                       "pushl %eax\n"
                       "pushl %ecx\n"
                       "pushl %esi\n"
                       "call abi_super_lod_helper\n"
                       "addl $4, %esp\n"
                       "popl %ecx\n"
                       "testb %al, %al\n"
                       "popl %eax\n"
                       "jne 3f\n"
                       "testb $0x10, 0x72(%eax)\n"
                       "je 1b\n"
                       "3:\n"
                       "jmp *zpatch_super_lod_ok\n");
}
#else
[[maybe_unused]] void abi_super_lod_fix_shim() {}
#endif

// 0x1004aab0 VRAMFix: stashes two of the game function's arguments, calls the
// verdict helper (original sub_1004AA80) under pushad/popad, then reproduces
// the original tail (`fldcw [esp]; add esp,0x1c; ret`), returning INT_MAX when
// the verdict is set. The E9 site replaced that tail, so this must be a naked
// body.
#if defined(_MSC_VER)
extern "C" __declspec(naked) void abi_vram_fix_helper() {
  __asm {
    mov eax, DWORD PTR [zpatch_vram_arg_b]
    mov BYTE PTR [zpatch_vram_flag], 0
    test eax, eax
    js vram_h_set
    cmp DWORD PTR [zpatch_vram_arg_a], 0
    jne vram_h_set
    cmp eax, 3E800000h
    jge vram_h_done
  vram_h_set:
    mov BYTE PTR [zpatch_vram_flag], 1
  vram_h_done:
    ret
  }
}
// @exact 0x1004AAB0 - hand-written transcription, byte-compared.
[[maybe_unused]] __declspec(naked) void abi_vram_fix_shim() {
  __asm {
    mov eax, DWORD PTR [esp + 0x14]
    mov DWORD PTR [zpatch_vram_arg_a], eax
    mov eax, DWORD PTR [esp + 0x10]
    mov DWORD PTR [zpatch_vram_arg_b], eax
    pushad
    call abi_vram_fix_helper
    popad
    cmp BYTE PTR [zpatch_vram_flag], 0
    je vram_skip
    mov eax, 7FFFFFFFh
  vram_skip:
    fldcw WORD PTR [esp]
    add esp, 1Ch
    ret
  }
}
#elif defined(__i386__)
extern "C" __attribute__((naked)) void abi_vram_fix_helper() {
  __asm__ __volatile__(
      "movl zpatch_vram_arg_b, %eax\n"
      "movb $0, zpatch_vram_flag\n"
      "testl %eax, %eax\n"
      "js 1f\n"
      "cmpl $0, zpatch_vram_arg_a\n"
      "jne 1f\n"
      "cmpl $0x3e800000, %eax\n"
      "jge 2f\n"
      "1:\n"
      "movb $1, zpatch_vram_flag\n"
      "2:\n"
      "ret\n");
}
[[maybe_unused]] __attribute__((naked)) void abi_vram_fix_shim() {
  __asm__ __volatile__(
      "movl 0x14(%esp), %eax\n"
      "movl %eax, zpatch_vram_arg_a\n"
      "movl 0x10(%esp), %eax\n"
      "movl %eax, zpatch_vram_arg_b\n"
      "pushal\n"
      "call abi_vram_fix_helper\n"
      "popal\n"
      "cmpb $0, zpatch_vram_flag\n"
      "je 3f\n"
      "movl $0x7fffffff, %eax\n"
      "3:\n"
      "fldcw (%esp)\n"
      "addl $0x1c, %esp\n"
      "ret\n");
}
#else
[[maybe_unused]] void abi_vram_fix_helper() {}
[[maybe_unused]] void abi_vram_fix_shim() {}
#endif

// One-shot diagnostic for DrivebyFOVFix: scans the resolver's registry table
// (game_base+0x150FDD0/count 0x150FDD4, build-9 RVAs) for the key 0x9399528 and
// logs the resolver/arg/result. The wrapper is naked so it can be called from
// the shim without disturbing the live xmm1..xmm7 (only xmm0 is scratch).
extern "C" void abi_driveby_fov_note_impl() {
  static bool logged = false;
  if (logged) return;
  logged = true;
  const std::uintptr_t game_base =
      static_cast<std::uintptr_t>(zpatch_driveby_resume) - 5u - 0x85031Fu;  // build-9 sig RVA
  std::uint32_t count = 0;
  std::uint32_t table = 0;
  std::uint32_t found = 0;
  std::uint32_t idx = 0;
  if (game_base > 0x10000u && game_base < 0x80000000u) {
    count = *reinterpret_cast<const std::uint32_t*>(game_base + 0x150FDD4u);
    table = *reinterpret_cast<const std::uint32_t*>(game_base + 0x150FDD0u);
    if (table > 0x10000u && count > 0u && count < 0x100000u) {
      for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t key = *reinterpret_cast<const std::uint32_t*>(
            static_cast<std::uintptr_t>(table) + i * 8u);
        if (key == 0x9399528u) {
          found = 1;
          idx = i;
          break;
        }
      }
    }
  }
  log_printf(
      "DrivebyFOVFix diag: resolver=%08X result=%08X gb=%08X count=%u table=%08X "
      "key_found=%u idx=%u",
      zpatch_driveby_resolver, zpatch_driveby_last_result, static_cast<unsigned>(game_base), count,
      table, found, idx);
}
#if defined(_MSC_VER)
extern "C" __declspec(naked) void abi_driveby_fov_note() {
  __asm {
    sub esp, 70h
    __emit 0Fh
    __emit 11h
    __emit 4Ch
    __emit 24h
    __emit 00h                    // movups [esp], xmm1
    __emit 0Fh
    __emit 11h
    __emit 54h
    __emit 24h
    __emit 10h                    // movups [esp+0x10], xmm2
    __emit 0Fh
    __emit 11h
    __emit 5Ch
    __emit 24h
    __emit 20h                    // movups [esp+0x20], xmm3
    __emit 0Fh
    __emit 11h
    __emit 64h
    __emit 24h
    __emit 30h                    // movups [esp+0x30], xmm4
    __emit 0Fh
    __emit 11h
    __emit 6Ch
    __emit 24h
    __emit 40h                    // movups [esp+0x40], xmm5
    __emit 0Fh
    __emit 11h
    __emit 74h
    __emit 24h
    __emit 50h                    // movups [esp+0x50], xmm6
    __emit 0Fh
    __emit 11h
    __emit 7Ch
    __emit 24h
    __emit 60h                    // movups [esp+0x60], xmm7
    call abi_driveby_fov_note_impl
    __emit 0Fh
    __emit 10h
    __emit 4Ch
    __emit 24h
    __emit 00h                    // movups xmm1, [esp]
    __emit 0Fh
    __emit 10h
    __emit 54h
    __emit 24h
    __emit 10h                    // movups xmm2, [esp+0x10]
    __emit 0Fh
    __emit 10h
    __emit 5Ch
    __emit 24h
    __emit 20h                    // movups xmm3, [esp+0x20]
    __emit 0Fh
    __emit 10h
    __emit 64h
    __emit 24h
    __emit 30h                    // movups xmm4, [esp+0x30]
    __emit 0Fh
    __emit 10h
    __emit 6Ch
    __emit 24h
    __emit 40h                    // movups xmm5, [esp+0x40]
    __emit 0Fh
    __emit 10h
    __emit 74h
    __emit 24h
    __emit 50h                    // movups xmm6, [esp+0x50]
    __emit 0Fh
    __emit 10h
    __emit 7Ch
    __emit 24h
    __emit 60h                    // movups xmm7, [esp+0x60]
    add esp, 70h
    ret
  }
}
#else
extern "C" void abi_driveby_fov_note() {}
#endif

// 0x100064b0 DrivebyFOVFix: computes the drive-by FOV once and feeds it to the
// resume as xmm0. The original body is `pushal; call sub_10006450; popal;
// movss xmm0,[0x101580c4]; jmp [0x1015bdf4]`; sub_10006450 resolves the handle
// dword_101580D4 (the loader's salt 0x09399528 on 1.0.8.0) through the
// loader-published game resolver (dword_1015BDE0, key in ESI, result in EAX),
// walks two structures, caches the entry, and reads the float at +0x60 into
// dword_101580C4. The whole body is reproduced in naked asm so only xmm0
// changes (xmm1/xmm2 are live across the site and the resume consumes them).
// Registered live (2026-10-13): the arm installs unconditionally when the
// pattern matches, and the pattern exists on 1.0.8.0 (1 hit) - the earlier
// "not installed on 1080" note was wrong.
//
// Fault-preserving (2026-10-16): the guards on the resolver result and the
// cached entry were fault-avoidance; the original dereferences the resolver
// result unconditionally (0x1000646E `mov ecx,[eax+0Eh]`) and dereferences the
// cached entry unconditionally (0x10006490 `mov eax,[eax]`). The 45.0f default
// is the original's own first instruction (0x10006457); it stays. The resolver
// is wrapped in sub_10003980-equivalent catch-all SEH: an exception returns
// zero, then the original null-handle dereference faults at 0x1000646E.
#if defined(_MSC_VER)
// sub_10006450, transcribed 1:1 (guards removed), kept as a separate function
// so the shim's `call` reproduces the original stack shape (the original shim
// is `pushal; call sub_10006450; popal; movss; jmp resume`).
extern "C" __declspec(naked) void abi_driveby_fov_helper() {
  __asm {
    cmp BYTE PTR [zpatch_driveby_done], 0
    mov DWORD PTR [zpatch_driveby_fov], 42340000h  // 45.0f default
    jne drv_have
    mov eax, DWORD PTR [zpatch_salt_d4]
    push edx
    push eax
    call zpatch_driveby_resolve_seh
    add esp, 8
    mov ecx, DWORD PTR [eax + 0Eh]    // AVs when the handle did not resolve
    add eax, 0Eh
    add ecx, eax
    mov BYTE PTR [zpatch_driveby_done], 1
    mov eax, DWORD PTR [ecx + 0Fh]    // AVs on a garbage chain
    mov eax, DWORD PTR [eax + ecx + 14h]
    mov DWORD PTR [zpatch_driveby_cached], eax
    jmp drv_read
  drv_have:
    mov eax, DWORD PTR [zpatch_driveby_cached]
  drv_read:
    mov eax, DWORD PTR [eax]          // AVs when the cached entry is null
    test eax, eax
    je drv_done
    mov edx, OFFSET zpatch_driveby_fov
    __emit 0F3h
    __emit 0Fh
    __emit 10h
    __emit 40h
    __emit 60h                   // movss xmm0, DWORD PTR [eax + 0x60]
    __emit 0F3h
    __emit 0Fh
    __emit 11h
    __emit 02h                   // movss DWORD PTR [edx], xmm0
  drv_done:
    ret
  }
}
// 0x100064b0: the original is only `pushal; call sub_10006450; popal;
// movss xmm0,[0x101580C4]; jmp [0x1015bdf4]`. Every GPR is restored before the
// SSE load, and EAX is used as the scratch base (pushed/popped) so no register
// the resume consumes is clobbered.
[[maybe_unused]] __declspec(naked) void abi_driveby_fov_shim() {
  __asm {
    pushad
    call abi_driveby_fov_helper
    popad
    push eax
    mov eax, OFFSET zpatch_driveby_fov
    __emit 0F3h
    __emit 0Fh
    __emit 10h
    __emit 00h                   // movss xmm0, DWORD PTR [eax]
    pop eax
    jmp DWORD PTR [zpatch_driveby_resume]
  }
}
#else
[[maybe_unused]] void abi_driveby_fov_helper() {}
[[maybe_unused]] void abi_driveby_fov_shim() {}
#endif

namespace {
// "%s_sign_%d" without the static CRT: the CRT's formatter needs DllMain
// state the emulator does not have, and the original only ever formats this
// one pattern (sub_10001280 with the literal template). Produces the same bytes.
int zpatch_nolivery_format(char* out, std::size_t cap, const char* name,
                            int number) noexcept {
  std::size_t o = 0;
  const auto put = [&](char c) { if (o + 1 < cap) out[o++] = c; };
  for (const char* s = name; s && *s; ++s) put(*s);
  static const char kSuffix[] = "_sign_";
  for (const char* s = kSuffix; *s; ++s) put(*s);
  char digits[12];
  int d = 0;
  unsigned v = number < 0 ? 0u - static_cast<unsigned>(number)
                          : static_cast<unsigned>(number);
  do { digits[d++] = static_cast<char>('0' + v % 10u); v /= 10u; } while (v);
  if (number < 0) put('-');
  while (d > 0) put(digits[--d]);
  if (cap) out[o < cap ? o : cap - 1] = 0;
  return static_cast<int>(o);
}
}  // namespace

// NoLiveryLimit helpers (sub_1003D220 / sub_1003D2F0 / sub_1003D270). The bodies
// are naked shims that stash their register inputs in globals, call a compiled
// helper, then tail-jump to the handler-published resume. The count helper
// calls the handler-published function as `__thiscall(this, hash)`.
extern "C" void abi_nolivery_hash_sign() {  // sub_1003D220
  char buffer[64];
  zpatch_nolivery_format(
      buffer, sizeof(buffer),
      reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_name)),
      static_cast<int>(zpatch_nolivery_index) + 1);
  zpatch_nolivery_hash = game_hash(buffer);
  zpatch_cookie_tail();
}
extern "C" void abi_nolivery_copy_name() {  // sub_1003D2F0
  // Original: push [bufptr]; push 0x10; push [dest]; call [strcpy_s]; add esp,0xc;
  // ret. It reads both globals unconditionally (no null guard) and has no /GS
  // cookie tail (it is a leaf that only calls the CRT).
  char* const dst = reinterpret_cast<char*>(static_cast<std::uintptr_t>(zpatch_nolivery_dest));
  const char* const src =
      reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_bufptr));
  zpatch_secure_strcpy_s(dst, 0x10u, src);
}
extern "C" void abi_nolivery_count_loop() {  // sub_1003D270
  using LookupFn = int(__fastcall*)(void* self, int, std::uint32_t hash);
  const auto fn = reinterpret_cast<LookupFn>(static_cast<std::uintptr_t>(zpatch_nolivery_fn));
  void* const self = reinterpret_cast<void*>(static_cast<std::uintptr_t>(zpatch_nolivery_arg));
  const char* const name =
      reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_nolivery_bufptr));
  zpatch_nolivery_count = 0;
  for (std::uint32_t i = 1;; ++i) {
    char buffer[64];
    zpatch_nolivery_format(buffer, sizeof(buffer), name, static_cast<int>(i));
    const std::uint32_t hash = game_hash(buffer);
    const int result = fn ? fn(self, 0, hash) : 0;
    if (result == 0) break;
    zpatch_nolivery_count = i;
  }
  zpatch_cookie_tail();
}
#if defined(_MSC_VER)
// @exact 0x1003d310: name = &ebx[0x13c], index = eax; resume with (hash, movsx ebx[0x48]).
extern "C" __declspec(naked) void abi_nolivery_a() {
  __asm {
    pushad
    lea ecx, [ebx + 13Ch]
    mov DWORD PTR [zpatch_nolivery_name], ecx
    mov DWORD PTR [zpatch_nolivery_index], eax
    call abi_nolivery_hash_sign
    popad
    mov ecx, DWORD PTR [zpatch_nolivery_hash]
    movsx edx, WORD PTR [ebx + 48h]
    push ecx
    push edx
    jmp DWORD PTR [zpatch_nolivery_resume]
  }
}
// @exact 0x1003d340: name = &ebx[0x13c], index = ecx; resume with (hash, movsx ebx[0x48]).
extern "C" __declspec(naked) void abi_nolivery_b() {
  __asm {
    pushad
    lea edx, [ebx + 13Ch]
    mov DWORD PTR [zpatch_nolivery_name], edx
    mov DWORD PTR [zpatch_nolivery_index], ecx
    call abi_nolivery_hash_sign
    popad
    mov edx, DWORD PTR [zpatch_nolivery_hash]
    movsx eax, WORD PTR [ebx + 48h]
    push edx
    push eax
    jmp DWORD PTR [zpatch_nolivery_resume]
  }
}
// @exact 0x1003d370: like a, but the original also drops 8 bytes of args first.
extern "C" __declspec(naked) void abi_nolivery_c() {
  __asm {
    pushad
    lea edx, [ebx + 13Ch]
    mov DWORD PTR [zpatch_nolivery_name], edx
    mov DWORD PTR [zpatch_nolivery_index], eax
    call abi_nolivery_hash_sign
    popad
    mov edx, DWORD PTR [zpatch_nolivery_hash]
    movsx eax, WORD PTR [ebx + 48h]
    add esp, 8
    push edx
    push eax
    jmp DWORD PTR [zpatch_nolivery_resume]
  }
}
// @exact 0x1003d3a0: copies the frame name into a 16-byte buffer, counts the
// "<name>_sign_N" lookups, writes the count to [ebp+0xc2], then resumes.
extern "C" __declspec(naked) void abi_nolivery_d() {
  __asm {
    pushad
    mov DWORD PTR [zpatch_nolivery_arg], edi
    lea ecx, [ebp + 13Ch]
    mov DWORD PTR [zpatch_nolivery_dest], ecx
    lea ecx, [esp + 60h]
    mov DWORD PTR [zpatch_nolivery_bufptr], ecx
    call abi_nolivery_copy_name
    call abi_nolivery_count_loop
    popad
    push ebx
    mov ebx, DWORD PTR [zpatch_nolivery_count]
    mov BYTE PTR [ebp + 0C2h], bl
    pop ebx
    jmp DWORD PTR [zpatch_nolivery_resume_d]
  }
}
// @exact 0x1003d3e0: the same sequence as 0x1003d3a0 with ESI as the object
// (name = &edi[0x13c], count written to [edi+0xc2]); same resume slot.
extern "C" __declspec(naked) void abi_nolivery_e() {
  __asm {
    pushad
    mov DWORD PTR [zpatch_nolivery_arg], esi
    lea ecx, [edi + 13Ch]
    mov DWORD PTR [zpatch_nolivery_dest], ecx
    lea ecx, [esp + 60h]
    mov DWORD PTR [zpatch_nolivery_bufptr], ecx
    call abi_nolivery_copy_name
    call abi_nolivery_count_loop
    popad
    push ebx
    mov ebx, DWORD PTR [zpatch_nolivery_count]
    mov BYTE PTR [edi + 0C2h], bl
    pop ebx
    jmp DWORD PTR [zpatch_nolivery_resume_d]
  }
}
#else
extern "C" void abi_nolivery_a() {}
extern "C" void abi_nolivery_b() {}
extern "C" void abi_nolivery_c() {}
extern "C" void abi_nolivery_d() {}
extern "C" void abi_nolivery_e() {}
#endif

// 0x10036f00 CrashMsgFix: the game's message getter is called through the
// handler-published pointer (the original call target of the site at *(match-4))
// and every '~' in the returned string is replaced with a space. Entered via the
// handler's E8, so it returns normally. Not byte-exact (the original's two nops
// are omitted), so verify-bodies reports it INFO.
#if defined(_MSC_VER)
// @exact 0x10036F00 - hand-written transcription, byte-compared.
extern "C" __declspec(naked) void abi_crash_msg_shim() {
  __asm {
    call DWORD PTR [zpatch_crash_msg_getter]
    mov dl, BYTE PTR [eax]
    test dl, dl
    je cms_done
    mov ecx, eax
    // The original has a 2-byte alignment nop (66 90) here.
    __emit 066h
    __emit 090h
  cms_loop:
    cmp dl, 7Eh
    jne cms_skip
    mov BYTE PTR [ecx], 20h
  cms_skip:
    mov dl, BYTE PTR [ecx + 1]
    inc ecx
    test dl, dl
    jne cms_loop
  cms_done:
    ret
  }
}
#else
extern "C" void abi_crash_msg_shim() {}
#endif

// NewFilesCompatibility: body 0x10051600 registers a callback with the game's
// file-registration function (handler-published); bodies 0x10051890 / 0x100518D0
// are the per-file update hooks (E8 sites) that resolve the asset object, hash
// the name and register it. Helpers mirror sub_100515D0 / sub_100515E0 /
// sub_10051730 / sub_10051610 / sub_100517E0.
namespace {
std::uintptr_t newfiles_find(std::string_view signature) {
  const auto* base = reinterpret_cast<const std::byte*>(GetModuleHandleA(nullptr));
  const auto* dos = base ? reinterpret_cast<const IMAGE_DOS_HEADER*>(base) : nullptr;
  const auto* nt = dos && dos->e_magic == IMAGE_DOS_SIGNATURE
                       ? reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew)
                       : nullptr;
  if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) return 0;
  Match match;
  if (!find_pattern({base, nt->OptionalHeader.SizeOfImage}, signature, match)) return 0;
  return reinterpret_cast<std::uintptr_t>(match.address);
}
}  // namespace
extern "C" void* abi_newfiles_cb(void** a1) {  // 0x100515D0
  void* const result = *a1;
  *reinterpret_cast<int*>(*a1) = 1;
  return result;
}
// 0x100515E0 is five instructions: push callback, push hash, call the
// published register function, add esp,8, ret. The flags at the resume come
// from that dd esp,8, so a C wrapper (extra prologue => different ESP) leaks
// different flags. Transcribed naked.
#if defined(_MSC_VER)
extern "C" __declspec(naked) void abi_newfiles_register() {
  __asm {
    push OFFSET abi_newfiles_cb
    push 2C89A1F9h
    call DWORD PTR [zpatch_newfiles_register_slot]
    add esp, 8
    ret
  }
}
#else
extern "C" void abi_newfiles_register() {
  using RegisterFn = void(__cdecl*)(int, void*);
  const auto fn = reinterpret_cast<RegisterFn>(
      static_cast<std::uintptr_t>(zpatch_newfiles_register_slot));
  fn(0x2C89A1F9, reinterpret_cast<void*>(&abi_newfiles_cb));
}
#endif
extern "C" void* abi_newfiles_find_obj() {  // 0x10051730
  // The original re-dereferences the container global on EVERY call
  // (`**(_DWORD**)(match + 2)`), so the container is picked up again whenever the
  // game rebuilds its registry. Caching the container value instead of just the
  // match address left a stale (freed/relocated) container that faulted in the
  // game's container search (0x43a490) - crash dump GTAIV.exe.29676.dmp.
  static std::uintptr_t match = 0;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    match = newfiles_find(
        "8B 35 ? ? ? ? 6A 00 50 E8 ? ? ? ? 83 C4 08 50 8B CE E8 ? ? ? ? 85 C0 89 07 5E 74 05");
  }
  if (!match) return nullptr;
  const std::uint32_t global =
      *reinterpret_cast<const std::uint32_t*>(reinterpret_cast<const void*>(match + 2u));
  return global ? reinterpret_cast<void*>(*reinterpret_cast<const std::uint32_t*>(
                      reinterpret_cast<const void*>(global)))
                : nullptr;
}
extern "C" int abi_newfiles_call_obj(void* obj, std::uint32_t hash) {  // 0x10051610
  using Fn = int(__fastcall*)(void* self, int, std::uint32_t);
  static std::uintptr_t cached = 0;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    cached = newfiles_find("85 C9 53 55 56 57 74 40 8B 6C 24 14 8D 64 24 00");
    if (!cached) cached = newfiles_find("53 55 8B 6C 24 0C 56 57 EB 06");
  }
  if (!cached) return 0;
  return reinterpret_cast<Fn>(cached)(obj, 0, hash);
}
extern "C" int abi_newfiles_call_add(void* self, void* str) {  // 0x100517E0
  using Fn = int(__fastcall*)(void* self, int, void*);
  static std::uintptr_t cached = 0;
  static bool resolved = false;
  if (!resolved) {
    resolved = true;
    const std::uintptr_t m = newfiles_find("8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0");
    cached = m ? m - 20u : 0;
  }
  if (!cached) return 0;
  return reinterpret_cast<Fn>(cached)(self, 0, str);
}
#if defined(_MSC_VER)
// @exact 0x10051600 - hand-written transcription, byte-compared.
extern "C" __declspec(naked) void abi_newfiles_register_body() {  // 0x10051600
  __asm {
    pushad
    call abi_newfiles_register
    popad
    ret
  }
}
#else
extern "C" void abi_newfiles_register_body() {}
#endif
extern "C" char __fastcall abi_newfiles_update_a(void* self, int) {  // 0x10051890
  auto* base = static_cast<unsigned char*>(self);
  if (base[0x10] == 0) return 0;
  void* const obj = abi_newfiles_find_obj();
  const std::uint32_t hash = game_hash(reinterpret_cast<const char*>(base + 0x10));
  if (abi_newfiles_call_obj(obj, hash)) abi_newfiles_call_add(base + 8, base + 0x10);
  return 1;
}
extern "C" char __fastcall abi_newfiles_update_b(void* self, int) {  // 0x100518D0
  auto* base = static_cast<unsigned char*>(self);
  if (base[0x10F] == 0) return 0;
  void* const obj = abi_newfiles_find_obj();
  const std::uint32_t hash = game_hash(reinterpret_cast<const char*>(base + 0x10F));
  if (abi_newfiles_call_obj(obj, hash)) abi_newfiles_call_add(base + 4, base + 0x10F);
  return 1;
}

// BenchmarkFix: two match-site shims (E9 at match[0]/[1]+8). Each checks eax,
// runs the interposed callback (esi with edi), then tail-jumps to the
// handler-published resume, or unwinds via the enclosing function's tail when
// eax is null. Register-exact transcription; registered for the two match-jump
// destinations.
#if defined(_MSC_VER)
// @exact 0x10005650 - hand-written transcription, byte-compared.
extern "C" __declspec(naked) void abi_benchmark_a() {  // 0x10005650
  __asm {
    test eax, eax
    je bf_a_zero
    push edi
    mov ebx, eax
    call esi
    jmp DWORD PTR [zpatch_benchmark_resume_a]
  bf_a_zero:
    xor al, al
    pop esi
    pop ebp
    pop ebx
    add esp, 10h
    ret
  }
}
// @exact 0x10005670 - hand-written transcription, byte-compared.
extern "C" __declspec(naked) void abi_benchmark_b() {  // 0x10005670
  __asm {
    test eax, eax
    je bf_b_zero
    push edi
    mov ebx, eax
    call esi
    jmp DWORD PTR [zpatch_benchmark_resume_b]
  bf_b_zero:
    xor al, al
    pop esi
    pop ebp
    pop ebx
    add esp, 10h
    ret
  }
}
#else
extern "C" void abi_benchmark_a() {}
extern "C" void abi_benchmark_b() {}
#endif

// BuildingDynamicShadows: shim installed at the sig
// `8B 47 28 C1 E8 06 83 E0 0F 83 C0`. If [edi] is one of the two
// handler-published model hashes it takes the special resume, else it derives the
// shadow level `(([edi+0x28]>>6)&0xf)-1` and takes the normal resume (both
// handler-published, match+0xf / match+0x70). Register-exact transcription.
#if defined(_MSC_VER)
// @exact 0x10005AF0 - hand-written transcription, byte-compared.
extern "C" __declspec(naked) void abi_building_dynamic_shadows() {
  __asm {
    mov eax, DWORD PTR [edi]
    cmp eax, DWORD PTR [zpatch_bds_hash_a]
    je bds_special
    cmp eax, DWORD PTR [zpatch_bds_hash_b]
    je bds_special
    mov eax, DWORD PTR [edi + 28h]
    shr eax, 6
    and eax, 0Fh
    add eax, -1
    jmp DWORD PTR [zpatch_bds_resume_a]
  bds_special:
    jmp DWORD PTR [zpatch_bds_resume_b]
  }
}
#else
extern "C" void abi_building_dynamic_shadows() {}
#endif

// CopSirensFlag helpers (sub_10005F70 / sub_10006260) and the reimplemented IDE
// parser (sub_10003FA0). The shims stash the model index, run the check and
// tail-jump to the handler-published resume; the IDE hook fills the model-hash
// tables the checks read.
extern "C" int abi_copsirens_check_a() {  // sub_10005F70
  static const char* const kPolice[] = {
      "police",     "police2",    "police3",  "police4",   "noose",      "pstockade",
      "nstockade",  "polpatriot", "fbi",      "n_police",  "n_police2",  "n_polpatriot",
      "n_pstockade", nullptr};
  const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
      static_cast<std::uintptr_t>(zpatch_model_lookup));
  const std::int32_t index = static_cast<std::int32_t>(zpatch_copsirens_index);
  zpatch_copsirens_flag = 0;
  if (lookup == nullptr) {
    // Fault-preserving: sub_10005F70's `call dword_10159C0C` faults (guard
    // removed 2026-10-16).
    zpatch::log_printf("guard-removed option=CopSirensFlag lookup=null fault=0x10005FA7");
  }
  for (int i = 0; kPolice[i]; ++i) {
    std::int32_t out = -1;
    lookup(game_hash(kPolice[i]), &out);
    if (out == index) {
      zpatch_copsirens_flag = 1;
      return 1;
    }
  }
  // Original sub_10005F70 re-stores the flag before the models loop
  // (mov eax,[0x10159B80]; ...; mov [0x10159B80],eax); keep the store.
  {
    volatile std::uint32_t* flag = &zpatch_copsirens_flag;
    *flag = *flag;
  }
  for (int i = 0; i < 512 && zpatch_copsirens_models[i]; ++i) {
    std::int32_t out = -1;
    lookup(zpatch_copsirens_models[i], &out);
    if (out == index) {
      zpatch_copsirens_flag = 1;
      return 1;
    }
  }
  return 0;
}
extern "C" int abi_copsirens_check_b() {  // sub_10006260
  const auto lookup = reinterpret_cast<int(__cdecl*)(std::uint32_t, std::int32_t*)>(
      static_cast<std::uintptr_t>(zpatch_model_lookup));
  const std::int32_t index = static_cast<std::int32_t>(zpatch_copsirens_index_b);
  zpatch_copsirens_flag_b = 0;
  std::int32_t out = -1;
  if (lookup == nullptr) {
    // Fault-preserving: sub_10006260's `call dword_10159C0C` faults (guard
    // removed 2026-10-16).
    zpatch::log_printf("guard-removed option=CopSirensFlag lookup=null fault=sub_10006260");
  }
  lookup(game_hash("police2"), &out);
  if (out != index) {
    if (!zpatch_copsirens_models[0]) return 0;
    for (int i = 0; i < 512 && zpatch_copsirens_models[i]; ++i) {
      out = -1;
      lookup(zpatch_copsirens_models[i], &out);
      if (out == index) {
        zpatch_copsirens_flag_b = 1;
        return 1;
      }
    }
    return 0;
  }
  zpatch_copsirens_flag_b = 1;
  return 1;
}
// _strnicmp(str, prefix, length) equivalent: the original's IDE parser takes the
// case-insensitive prefix branch (`strnicmp(v0, "wheelangle:", 0xB)`), so mixed
// case matches too. Comparison stops at a NUL in either operand, like the CRT.
bool prefix_ci(const char* text, const char* prefix, std::size_t length) noexcept {
  for (std::size_t i = 0; i < length; ++i) {
    const unsigned char a = static_cast<unsigned char>(text[i]);
    const unsigned char b = static_cast<unsigned char>(prefix[i]);
    if (a == 0 || b == 0) return a == b;
    const unsigned char lower_a =
        (a >= 'A' && a <= 'Z') ? static_cast<unsigned char>(a + 32) : a;
    const unsigned char lower_b =
        (b >= 'A' && b <= 'Z') ? static_cast<unsigned char>(b + 32) : b;
    if (lower_a != lower_b) return false;
  }
  return true;
}

extern "C" void abi_ide_parse() {  // sub_10003FA0
  // Fault-preserving: the original dereferences the string unconditionally
  // (its first instruction after the prologue is `cmp eax,[edx]`), so a null
  // zpatch_ide_string faults there; the rebuilt helper had a null-guard return.
  const char* const str =
      reinterpret_cast<const char*>(static_cast<std::uintptr_t>(zpatch_ide_string));
  const std::uint32_t model = *reinterpret_cast<const std::uint32_t*>(
      reinterpret_cast<const void*>(static_cast<std::uintptr_t>(zpatch_ide_model) + 60u));
  const auto append = [](std::uint32_t* array, std::uint32_t value) {
    for (int i = 0; i < 512; ++i) {
      if (array[i] == value) return;
      if (array[i] == 0) {
        array[i] = value;
        return;
      }
    }
  };
  // The original's arms are independent `if`s on the same string (the
  // popuplights block falls through to the siren check), not else-if.
  if (std::strcmp(str, "popuplights") == 0) {
    append(zpatch_popuplights_models, model);
  }
  if (std::strcmp(str, "siren") == 0) {
    append(zpatch_copsirens_models, model);
  }
  if (prefix_ci(str, "wheelangle:", 11)) {
    // sub_10003FA0's third arm: strnicmp(...,0xB) then strtof(String1+11). An
    // existing model entry is left untouched (the original's scan only breaks
    // out); the angle is stored only on append.
    const float angle = std::strtof(str + 11, nullptr);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &angle, sizeof(bits));
    for (int i = 0; i < 512; ++i) {
      if (zpatch_wheelangle_models[i] == model) return;
      if (zpatch_wheelangle_models[i] == 0) {
        zpatch_wheelangle_models[i] = model;
        zpatch_wheelangle_angles[i] = bits;
        return;
      }
    }
  }
}
#if defined(_MSC_VER)
// @exact 0x10006310 - hand-written transcription, byte-compared.
extern "C" __declspec(naked) void abi_copsirens_a() {  // 0x10006310
  __asm {
    mov DWORD PTR [zpatch_copsirens_index], edx
    pushad
    pushfd
    call abi_copsirens_check_a
    popfd
    popad
    mov eax, DWORD PTR [zpatch_copsirens_flag]
    jmp DWORD PTR [zpatch_copsirens_resume]
  }
}
// @exact 0x10006330 - hand-written transcription, byte-compared.
extern "C" __declspec(naked) void abi_copsirens_b() {  // 0x10006330
  __asm {
    mov DWORD PTR [zpatch_copsirens_index_b], eax
    pushad
    pushfd
    call abi_copsirens_check_b
    popfd
    popad
    cmp DWORD PTR [zpatch_copsirens_flag_b], 1
    jmp DWORD PTR [zpatch_copsirens_resume_b]
  }
}
// @exact 0x100040E0 - hand-written transcription, byte-compared.
extern "C" __declspec(naked) void abi_copsirens_ide() {  // 0x100040E0
  __asm {
    pushad
    mov DWORD PTR [zpatch_ide_string], edi
    mov DWORD PTR [zpatch_ide_model], eax
    call abi_ide_parse
    popad
    mov ecx, 7
    xor edx, edx
    __emit 0F3h
    __emit 0A6h               // repe cmpsb
    jmp DWORD PTR [zpatch_ide_hook_slot]
  }
}
#else
extern "C" void abi_copsirens_a() {}
extern "C" void abi_copsirens_b() {}
extern "C" void abi_copsirens_ide() {}
#endif

// BorderlessWindowed: the two window-proc shims (0x10005890 = Unicode /
// 0x10005900 = ANSI). The installer replaces the game's `call [DefWindowProc]`
// with E8 -> body and NOPs the leftover byte; the body does the one-time style
// fixup then defers to the real DefWindowProc. Its `ret 0x10` consumes the
// DefWindowProc argument slots exactly like the replaced call, so the game's own
// `pop edi/esi/ebp/ebx; add esp,0x10; ret 0x10` still unwinds correctly.
extern "C" LRESULT __stdcall abi_borderless_w(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam) {
  if (!zpatch_borderless_w_done) {
    SetWindowPos(hwnd, reinterpret_cast<HWND>(static_cast<INT_PTR>(-2)), 0, 0, 0, 0, 3);
    SetWindowLongA(hwnd, GWL_STYLE,
                   GetWindowLongA(hwnd, GWL_STYLE) & static_cast<LONG>(0xff30ffffu));
    SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(8));
    zpatch_borderless_w_done = 1;
  }
  return DefWindowProcW(hwnd, msg, wparam, lparam);
}
extern "C" LRESULT __stdcall abi_borderless_a(HWND hwnd, UINT msg, WPARAM wparam,
                                              LPARAM lparam) {
  if (!zpatch_borderless_a_done) {
    SetWindowPos(hwnd, reinterpret_cast<HWND>(static_cast<INT_PTR>(-2)), 0, 0, 0, 0, 3);
    SetWindowLongA(hwnd, GWL_STYLE,
                   GetWindowLongA(hwnd, GWL_STYLE) & static_cast<LONG>(0xff30ffffu));
    SetWindowLongA(hwnd, GWL_EXSTYLE, GetWindowLongA(hwnd, GWL_EXSTYLE) & ~static_cast<LONG>(8));
    zpatch_borderless_a_done = 1;
  }
  return DefWindowProcA(hwnd, msg, wparam, lparam);
}

// SpeedupBugFix: replaces the game's timing helper (0x10050540, `__thiscall` on
// a _QWORD*). It lazily fills the game's eight timing-constant floats (the
// handler publishes pointers to them) from QueryPerformanceFrequency, then
// stores a 64-bit timestamp from the game's own clock fn (match-51) into *this
// and returns *this. Compiled (INFO in verify-bodies).
extern "C" void* __fastcall abi_speedup_body(void* self, int) {
  if (zpatch_tls_static_header(zpatch::zpatch_speedup_scan_guard)) {
    // Mirror sub_10050587's MSVC static-init guard and dword_101A849C cache.
    // The original body locates the game's clock fn with this signature and
    // calls (match - 51). (The handler's site signature `50 64 ...` locates the
    // E9 site / timing-constant pointers, NOT this fn.)
    zpatch::log_printf("SpeedupBugFix: one-time clock scan starting (guard=%#x)",
                       zpatch::zpatch_speedup_scan_guard);
    zpatch::log_flush();
    zpatch::zpatch_speedup_clock = static_cast<std::uint32_t>(newfiles_find(
        "1B D9 89 5C 24 24 0F 88"));
    // Diagnostic only: the original has no log call here, but a missed match
    // means the faithful `clock - 51` call faults immediately (0xFFFFFFCD).
    zpatch::log_printf("SpeedupBugFix: clock scan match=%p clock=%p",
                       reinterpret_cast<void*>(
                           static_cast<std::uintptr_t>(zpatch::zpatch_speedup_clock)),
                       reinterpret_cast<void*>(
                           static_cast<std::uintptr_t>(zpatch::zpatch_speedup_clock) - 51u));
    zpatch::log_flush();
    zpatch_tls_static_footer(zpatch::zpatch_speedup_scan_guard);
  }
  // Fault-preserving: the original dereferences the handler-published timing
  // pointers unconditionally (its one-time test is on the float, not on the
  // pointers), so the null-pointer checks are removed.
  float* const t50 = reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t50));
  if (*t50 == 0.0f) {
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    const double scale = 1000.0 / static_cast<double>(frequency.QuadPart);
    const float v5 = static_cast<float>(scale);
    const float v6 = static_cast<float>(scale * 1000.0);
    const float v7 = static_cast<float>(scale * 0.001000000047497451);
    const float v8 = static_cast<float>(0.001000000047497451 / scale);
    *reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t64)) = v5;
    *reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t60)) = v6;
    *reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t50)) = v7;
    *reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t5c)) = v8;
    *reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t68)) = v5;
    *reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t4c)) = v6;
    *reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t54)) = v7;
    *reinterpret_cast<float*>(static_cast<std::uintptr_t>(zpatch_speedup_t58)) = v8;
  }
  using ClockFn = __int64(__cdecl*)(void);
  // Fault-preserving: the original calls its resolved clock function
  // unconditionally; a failed scan must fault at match - 51 like the original.
  const __int64 stamp = reinterpret_cast<ClockFn>(
      static_cast<std::uintptr_t>(zpatch::zpatch_speedup_clock) - 51u)();
  *reinterpret_cast<__int64*>(self) = stamp;
  return self;
}

// @exact 0x1001b040: instruction-for-instruction transcription of the original.
// 0x1001b040 EpisodeSwitchFreezeFix: stores eax into [ebx+0x20]; null returns 0
// (pop edi/esi/ebx, ret 0x10), else tail-jumps to the resume (match+5).
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_episode_switch_store_shim() {
  __asm {
    mov DWORD PTR [ebx + 0x20], eax
    cmp eax, 0
    je ep_store_null
    mov edx, DWORD PTR [eax]
    jmp DWORD PTR [zpatch_episode_switch_resume_a]
  ep_store_null:
    xor al, al
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret 16
  }
}
// @exact 0x1001b060
[[maybe_unused]] __declspec(naked) void abi_episode_switch_shim_b() {
  __asm {
    cmp ebx, 0
    je ep_b_null
    mov ecx, DWORD PTR [ebx + 0x10]
    cmp ecx, DWORD PTR [ebx + 0x14]
    jmp DWORD PTR [zpatch_episode_switch_resume_b]
  ep_b_null:
    mov BYTE PTR [edi], 0
    pop edi
    pop esi
    mov eax, ebp
    pop ebp
    pop ebx
    pop ecx
    ret
  }
}
// @exact 0x1001b0a0
[[maybe_unused]] __declspec(naked) void abi_episode_switch_vcall() {
  __asm {
    cmp ecx, 0
    je ep_vc_null
    mov eax, ecx
    mov ecx, DWORD PTR [eax]
    mov edx, DWORD PTR [ecx]
    mov eax, DWORD PTR [eax + 4]
    mov edx, DWORD PTR [edx + 0x30]
    push eax
    call edx
    ret
  ep_vc_null:
    mov eax, 0
    ret
  }
}
// @exact 0x1001b0c0
[[maybe_unused]] __declspec(naked) void abi_episode_switch_shim_c() {
  __asm {
    cmp ecx, 0
    je ep_c_null
    push esi
    mov esi, ecx
    cmp DWORD PTR [esi + 0x14], 0
    jmp DWORD PTR [zpatch_episode_switch_resume_c]
  ep_c_null:
    mov eax, 0
    ret
  }
}
#elif defined(__i386__)
#define ZP_EP_ATTR [[maybe_unused]] __attribute__((naked))
ZP_EP_ATTR void abi_episode_switch_store_shim() {
  __asm__ __volatile__("movl %eax, 0x20(%ebx)\n" "testl %eax, %eax\n" "je 1f\n"
                       "movl (%eax), %edx\n" "jmp *zpatch_episode_switch_resume_a\n"
                       "1:\n" "xorb %al, %al\n" "popl %edi\n" "popl %esi\n" "popl %ebx\n"
                       "movl %ebp, %esp\n" "popl %ebp\n" "ret $16\n");
}
ZP_EP_ATTR void abi_episode_switch_shim_b() {
  __asm__ __volatile__("testl %ebx, %ebx\n" "je 1f\n" "movl 0x10(%ebx), %ecx\n"
                       "cmpl 0x14(%ebx), %ecx\n" "jmp *zpatch_episode_switch_resume_b\n"
                       "1:\n" "movb $0, (%edi)\n" "popl %edi\n" "popl %esi\n" "movl %ebp, %eax\n"
                       "popl %ebp\n" "popl %ebx\n" "popl %ecx\n" "ret\n");
}
ZP_EP_ATTR void abi_episode_switch_vcall() {
  __asm__ __volatile__("testl %ecx, %ecx\n" "je 1f\n" "movl %ecx, %eax\n" "movl (%eax), %ecx\n"
                       "movl (%ecx), %edx\n" "movl 4(%eax), %eax\n" "movl 0x30(%edx), %edx\n"
                       "pushl %eax\n" "call *%edx\n" "ret\n" "1:\n" "xorl %eax, %eax\n" "ret\n");
}
ZP_EP_ATTR void abi_episode_switch_shim_c() {
  __asm__ __volatile__("testl %ecx, %ecx\n" "je 1f\n" "pushl %esi\n" "movl %ecx, %esi\n"
                       "cmpl $0, 0x14(%esi)\n" "jmp *zpatch_episode_switch_resume_c\n"
                       "1:\n" "xorl %eax, %eax\n" "ret\n");
}
#undef ZP_EP_ATTR
#else
[[maybe_unused]] void abi_episode_switch_store_shim() {}
[[maybe_unused]] void abi_episode_switch_shim_b() {}
[[maybe_unused]] void abi_episode_switch_vcall() {}
[[maybe_unused]] void abi_episode_switch_shim_c() {}
#endif

// 0x1001ef60 HighFPSBikePhysicsFix: transcribable (x87 fld/fstp are fine in
// MSVC asm; the clamp helper is compiled). Saves the incoming value, clamps it,
// restores, then tail-jumps to the resume (match+9).
#if defined(_MSC_VER)
// @exact 0x1001EF60 - hand-written transcription, byte-compared.
[[maybe_unused]] __declspec(naked) void abi_bike_physics_detour() {
  __asm {
    fld DWORD PTR [ebp + 8]
    fstp DWORD PTR [zpatch_bike_value]
    pushad
    call abi_bike_physics_helper
    popad
    mov DWORD PTR [zpatch_bike_saved], eax
    mov eax, DWORD PTR [zpatch_bike_value]
    mov DWORD PTR [ebp + 8], eax
    mov eax, DWORD PTR [zpatch_bike_saved]
    fld DWORD PTR [ebp + 8]
    push ecx
    mov ecx, esi
    fstp DWORD PTR [esp]
    jmp DWORD PTR [zpatch_bike_resume]
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_bike_physics_detour() {
  __asm__ __volatile__("flds 0x8(%ebp)\n"
                       "fstps zpatch_bike_value\n"
                       "pushal\n"
                       "call abi_bike_physics_helper\n"
                       "popal\n"
                       "movl %eax, zpatch_bike_saved\n"
                       "movl zpatch_bike_value, %eax\n"
                       "movl %eax, 0x8(%ebp)\n"
                       "movl zpatch_bike_saved, %eax\n"
                       "flds 0x8(%ebp)\n"
                       "pushl %ecx\n"
                       "movl %esi, %ecx\n"
                       "fstps (%esp)\n"
                       "jmp *zpatch_bike_resume\n");
}
#else
[[maybe_unused]] void abi_bike_physics_detour() {}
#endif

// @exact 0x10005d80: instruction-for-instruction transcription of the original
// (see the predicate register contract below).
// 0x10005d80 CollisionDeadlockFix: full replacement of the node-walk function
// the handler matches (its prologue 55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66
// is rewritten with E9 to this body). The per-node predicate published in
// dword_1015BDD4 is NOT an ordinary __stdcall: it is called with the aligned
// entry pointer in EAX, a2 in EDX and the shift mask on the stack, and it pops
// its argument (ret 4). The compiled C model passed neither register and used
// the wrong cleanup, so the body is transcribed instruction-for-instruction
// from the original disassembly (the original also restores esp from ebp, which
// discards the predicate's cleanup). Clock the iteration counter in
// dword_1015BDD8 (our zpatch_collision_deadlock_iterations) bounds the walk at
// 10000 steps.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_collision_deadlock_walk() {
  __asm {
    mov DWORD PTR [zpatch_collision_deadlock_iterations], 0
    push ebp
    mov ebp, esp
    and esp, 0xFFFFFFF8
    push ecx
    mov eax, DWORD PTR [ebp + 0x0C]
    movzx ecx, WORD PTR [eax + 0x66]
    push ebx
    mov ebx, DWORD PTR [ebp + 0x08]
    mov edx, DWORD PTR [ebx + 0x70]
    push esi
    push edi
    movzx edi, WORD PTR [edx + ecx*8]
    cmp di, 0xFFFF
    je coll_return_minus1
  coll_loop:
    mov ecx, DWORD PTR [ebx + 0x70]
    movzx eax, di
    movzx edx, WORD PTR [ecx + eax*8]
    lea esi, [ecx + eax*8]
    mov eax, DWORD PTR [esi + 4]
    and eax, 0xFFFFFFF0
    cmp dx, 0xFFFF
    je coll_skip_prefetch
    movzx edx, dx
    mov ecx, DWORD PTR [ecx + edx*8 + 4]
    and ecx, 0xFFFFFFF0
    prefetcht0 BYTE PTR [ecx]
  coll_skip_prefetch:
    mov ecx, DWORD PTR [esi + 4]
    and ecx, 3
    mov edx, 1
    shl edx, cl
    push edx
    mov edx, DWORD PTR [ebp + 0x0C]
    call DWORD PTR [zpatch_collision_deadlock_should_walk]
    test al, al
    jne coll_return_di
    movzx edi, WORD PTR [esi]
    add DWORD PTR [zpatch_collision_deadlock_iterations], 1
    cmp DWORD PTR [zpatch_collision_deadlock_iterations], 0x2710
    je coll_return_minus1
    cmp di, 0xFFFF
    jne coll_loop
  coll_return_minus1:
    or ax, 0xFFFF
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret 8
  coll_return_di:
    mov ax, di
    pop edi
    pop esi
    pop ebx
    mov esp, ebp
    pop ebp
    ret 8
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_collision_deadlock_walk() {
  __asm__ __volatile__(
      "movl $0, zpatch_collision_deadlock_iterations\n"
      "pushl %ebp\n"
      "movl %esp, %ebp\n"
      "andl $0xFFFFFFF8, %esp\n"
      "pushl %ecx\n"
      "movl 0x0C(%ebp), %eax\n"
      "movzwl 0x66(%eax), %ecx\n"
      "pushl %ebx\n"
      "movl 0x08(%ebp), %ebx\n"
      "movl 0x70(%ebx), %edx\n"
      "pushl %esi\n"
      "pushl %edi\n"
      "movzwl (%edx,%ecx,8), %edi\n"
      "cmpw $0xFFFF, %di\n"
      "je 1f\n"
      "2:\n"
      "movl 0x70(%ebx), %ecx\n"
      "movzwl %di, %eax\n"
      "movzwl (%ecx,%eax,8), %edx\n"
      "leal (%ecx,%eax,8), %esi\n"
      "movl 4(%esi), %eax\n"
      "andl $0xFFFFFFF0, %eax\n"
      "cmpw $0xFFFF, %dx\n"
      "je 3f\n"
      "movzwl %dx, %edx\n"
      "movl 4(%ecx,%edx,8), %ecx\n"
      "andl $0xFFFFFFF0, %ecx\n"
      "prefetcht0 (%ecx)\n"
      "3:\n"
      "movl 4(%esi), %ecx\n"
      "andl $3, %ecx\n"
      "movl $1, %edx\n"
      "shll %cl, %edx\n"
      "pushl %edx\n"
      "movl 0x0C(%ebp), %edx\n"
      "call *zpatch_collision_deadlock_should_walk\n"
      "testb %al, %al\n"
      "jne 4f\n"
      "movzwl (%esi), %edi\n"
      "addl $1, zpatch_collision_deadlock_iterations\n"
      "cmpl $0x2710, zpatch_collision_deadlock_iterations\n"
      "je 1f\n"
      "cmpw $0xFFFF, %di\n"
      "jne 2b\n"
      "1:\n"
      "orw $0xFFFF, %ax\n"
      "popl %edi\n"
      "popl %esi\n"
      "popl %ebx\n"
      "movl %ebp, %esp\n"
      "popl %ebp\n"
      "ret $8\n"
      "4:\n"
      "movw %di, %ax\n"
      "popl %edi\n"
      "popl %esi\n"
      "popl %ebx\n"
      "movl %ebp, %esp\n"
      "popl %ebp\n"
      "ret $8\n");
}
#else
[[maybe_unused]] void abi_collision_deadlock_walk() {}
#endif

// @exact 0x10036ee0: instruction-for-instruction transcription of the original.
// 0x10036ee0 BlackscreenFix shim: the handler publishes the original call
// target (dword_1015AD58) and a pointer to the value the game reads
// (dword_1015AD5C). Values 8..10 pass through unchanged; everything else runs
// the original function.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void abi_blackscreen_shim() {
  __asm {
    mov eax, DWORD PTR [zpatch_blackscreen_value_ptr]
    mov eax, DWORD PTR [eax]
    cmp eax, 7
    jle tail
    cmp eax, 11
    jl keep
  tail:
    jmp DWORD PTR [zpatch_blackscreen_resume]
  keep:
    ret
  }
}
#elif defined(__i386__)
[[maybe_unused]] __attribute__((naked)) void abi_blackscreen_shim() {
  __asm__ __volatile__("movl zpatch_blackscreen_value_ptr, %eax\n"
                       "movl (%eax), %eax\n"
                       "cmpl $7, %eax\n"
                       "jle 1f\n"
                       "cmpl $11, %eax\n"
                       "jl 2f\n"
                       "1:\n"
                       "jmp *zpatch_blackscreen_resume\n"
                       "2:\n"
                       "ret\n");
}
#else
[[maybe_unused]] void abi_blackscreen_shim() {}
#endif

// 0x100455a0 SavegameFix read/copy: if (*out >= size) { if (dst) memcpy(dst, src, size);
// *out = size; return 0; } else { *out = size; return 0x8007007A; }
// __stdcall, five arguments (the fifth is unused by the original).
int __stdcall abi_savegame_fix_copy(const void* src, std::uint32_t size, void* dst,
                                    std::uint32_t* out_size, int) {
  if (*out_size >= size) {
    if (dst) std::memcpy(dst, src, size);
    *out_size = size;
    return 0;
  }
  *out_size = size;
  return static_cast<int>(0x8007007Au);
}

// @exact 0x1001b080: instruction-for-instruction transcription of the original.
// 0x1001b080 EpisodeSwitchFreezeFix sum: __thiscall, no stack arguments,
// returns [ecx+10h] + [ecx+0Ch] (0 when this is null). Naked for the same
// reason as above.
#if defined(_MSC_VER)
__declspec(naked) void abi_episode_switch_sum() {
  __asm {
    cmp ecx, 0
    je skip
    mov eax, [ecx + 0x10]
    add eax, [ecx + 0xc]
    ret
  skip:
    mov eax, 0
    ret
  }
}
#else
__attribute__((naked)) void abi_episode_switch_sum() {
  __asm__ __volatile__(
      "cmpl $0, %ecx\n"
      "je 1f\n"
      "movl 0x10(%ecx), %eax\n"
      "addl 0xc(%ecx), %eax\n"
      "ret\n"
      "1:\n"
      "movl $0, %eax\n"
      "ret\n");
}
#endif

// @exact 0x10045C30: instruction-for-instruction transcription of the original.
// 0x10045C30 SkipIntro body. Entered via E9 from match-6 with the site's ecx
// and edx; resumes by jumping to the match address the handler publishes
// (dword_1015AD74). The original zeroes edx below the 0x1F40 threshold, i.e.
// the intro screens are skipped outright (a shortened-but-visible variant was
// a reimplementation and is archived in docs/parity_and_improvements.md).
#if defined(_MSC_VER)
__declspec(naked) void abi_skip_intro_body() {
  __asm {
    imul ecx, ecx, 0x190
    cmp edx, 0x1f40
    jge ok
    xor edx, edx
  ok:
    jmp DWORD PTR [zpatch_skip_intro_match]
  }
}
#elif defined(__i386__)
__attribute__((naked)) void abi_skip_intro_body() {
  __asm__ __volatile__(
      "imull $0x190, %ecx, %ecx\n"
      "cmpl $0x1f40, %edx\n"
      "jge 1f\n"
      "xorl %edx, %edx\n"
      "1:\n"
      "jmp *zpatch_skip_intro_match\n");
}
#else
void abi_skip_intro_body() {}
#endif

// @exact 0x10046610: instruction-for-instruction transcription of the original.
// 0x10046610 SkipMenu body. Entered via E8 from the patched site; the first
// call with a zero argument switches the menu id to 6 unconditionally (the
// one-shot flag is the original's byte_101A8450), then resumes by jumping to
// the address the handler computes and publishes (dword_10159348 =
// *(match+24) + match + 28). A reimplementation that checked for an existing
// savegame (and a StartNewGameIfNoSavefile switch) before switching the id is
// archived in docs/parity_and_improvements.md - the original's unconditional
// switch is the shipped behaviour.
#if defined(_MSC_VER)
__declspec(naked) void abi_skip_menu_body() {
  __asm {
    push ebp
    mov ebp, esp
    cmp BYTE PTR [zpatch_skip_menu_flag], 0
    mov eax, DWORD PTR [ebp + 8]
    jne skip
    test eax, eax
    jne skip
    mov eax, 6
    mov BYTE PTR [zpatch_skip_menu_flag], 1
  skip:
    mov DWORD PTR [ebp + 8], eax
    pop ebp
    jmp DWORD PTR [zpatch_skip_menu_resume]
  }
}
#elif defined(__i386__)
__attribute__((naked)) void abi_skip_menu_body() {
  __asm__ __volatile__(
      "pushl %ebp\n"
      "movl %esp, %ebp\n"
      "cmpb $0, zpatch_skip_menu_flag\n"
      "movl 0x8(%ebp), %eax\n"
      "jne 1f\n"
      "testl %eax, %eax\n"
      "jne 1f\n"
      "movl $6, %eax\n"
      "movb $1, zpatch_skip_menu_flag\n"
      "1:\n"
      "movl %eax, 0x8(%ebp)\n"
      "popl %ebp\n"
      "jmp *zpatch_skip_menu_resume\n");
}
#else
void abi_skip_menu_body() {}
#endif

// DLC discovery post step (sub_10023150): enumerate \dlc\tlad\content\ /
// \TLAD\, \dlc\TBoGT\content\ / \TBoGT\ and \EpisodeN\ next to the game
// executable and register each through the two published game RVAs. The
// original writes the module base + RVA into the struct and hands it to the
// game's register/apply functions. Referenced from the naked body's asm, which
// some compilers cannot see - hence the maybe_unused.
#if defined(_MSC_VER) && !defined(_WIN64)
extern "C" __declspec(naked) void __cdecl zpatch_dlc_apply_capture_edx(
    void*, char*, DlcApplyFn, std::uint32_t*) {
  __asm {
    push ebp
    mov  ebp, esp
    mov  ecx, dword ptr [ebp + 8]
    push dword ptr [ebp + 0Ch]
    call dword ptr [ebp + 10h]
    mov  eax, dword ptr [ebp + 14h]
    mov  dword ptr [eax], edx
    mov  esp, ebp
    pop  ebp
    ret
  }
}
#else
void zpatch_dlc_apply_capture_edx(void* object, char* request,
                                  DlcApplyFn apply, std::uint32_t* edx_out) {
  apply(object, request);
  *edx_out = 0;
}
#endif

[[maybe_unused]] void* dlc_discovery_post(void* object) {
  auto* const module_path = static_cast<char*>(::operator new(0x104u));
  // Original ignores the return value and continues with the allocator buffer
  // (sub_10023150); a failed GetModuleFileNameA is not a clean early return.
  GetModuleFileNameA(nullptr, module_path, 0x104u);
  PathRemoveFileSpecA(module_path);
  std::uint32_t last_apply_edx = 0;
  bool episode254_registered = false;

  // The original picks ONE path per group: the "content" layout if it exists,
  // otherwise the legacy fallback; it never registers both.
  const auto try_paths = [&](const char* primary, const char* fallback, int id) {
    char path[0x104];
    strcpy_s(path, 0x104u, module_path);
    strcat_s(path, 0x104u, primary);
    if (!PathFileExistsA(path)) {
      if (std::strcmp(primary, fallback) == 0) return;
      strcpy_s(path, 0x104u, module_path);
      strcat_s(path, 0x104u, fallback);
      if (!PathFileExistsA(path)) return;
    }
    char request[360]{};
    strcpy_s(request, 0x104u, path);
    request[358] = 1;
    *reinterpret_cast<std::uint32_t*>(request + 344) = 1;
    const auto game_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleA(nullptr));
    const auto register_fn = reinterpret_cast<DlcRegisterFn>(
        game_base + zpatch::dlc_register_rva_global());
    const auto apply_fn = reinterpret_cast<DlcApplyFn>(
        game_base + zpatch::dlc_apply_rva_global());
    register_fn(id, request, 1);
    zpatch_dlc_apply_capture_edx(object, request, apply_fn, &last_apply_edx);
    if (id == 508) episode254_registered = true;
  };

  try_paths("\\dlc\\tlad\\content\\", "\\TLAD\\", 2);
  try_paths("\\dlc\\TBoGT\\content\\", "\\TBoGT\\", 4);
  for (int i = 3; i < 255; ++i) {
    char suffix[0x40];
    zpatch_spawns_sprintf(suffix, sizeof(suffix), "\\Episode%d\\", i);
    try_paths(suffix, suffix, 2 * i);
  }
  *reinterpret_cast<std::uint8_t*>(static_cast<std::uint8_t*>(object) + 358) = 2;
#if defined(_MSC_VER) && !defined(_WIN64)
  // sub_10023150's final sub_10001280 call leaves its 0x40 buffer-size in EDX;
  // the following PathFileExistsA import preserves EDX, and the caller's exact
  // resume observes that value. The CRT formatter above otherwise leaves its
  // last source pointer there. If Episode254 exists, its final apply call is
  // instead the last EDX producer; the assembly helper captures that value.
  if (episode254_registered) {
    __asm { mov edx, last_apply_edx }
  } else {
    __asm { mov edx, 40h }
  }
#elif defined(__i386__)
  if (episode254_registered)
    __asm__ __volatile__("movl %0, %%edx" :: "m"(last_apply_edx) : "edx");
  else
    __asm__ __volatile__("movl $0x40, %%edx" ::: "edx");
#endif
  return object;
}

// 0x10023490 LoadDLCs struct fixup. The object arrives in ESI and the epilogue
// pops esi/ebx and cleans 0x14 stack bytes; the original instruction sequence
// is reproduced (with our own callback slot and post step).
#if defined(_MSC_VER)
// @exact 0x10023490 - hand-written transcription, byte-compared.
__declspec(naked) void abi_load_dlcs_struct_fixup() {
  __asm {
    mov BYTE PTR [esi], 0
    mov BYTE PTR [esi + 0x40], 0
    mov BYTE PTR [esi + 0x80], 0
    mov BYTE PTR [esi + 0xc0], 0
    mov BYTE PTR [esi + 0x100], 0
    cmp BYTE PTR [esi + 0x167], 0
    je load
    mov BYTE PTR [esi + 0x168], 1
  exit_label:
    pop esi
    pop ebx
    add esp, 0x14
    ret
  load:
    mov ecx, esi
    call DWORD PTR [zpatch_dlc_load_callback]
    push esi
    call dlc_discovery_post
    pop esi
    mov BYTE PTR [esi + 0xa4], 1
    jmp exit_label
  }
}
#else
#if defined(__i386__)
__attribute__((naked)) void abi_load_dlcs_struct_fixup() {
  __asm__ __volatile__(
      "movb $0, (%esi)\n"
      "movb $0, 0x40(%esi)\n"
      "movb $0, 0x80(%esi)\n"
      "movb $0, 0xc0(%esi)\n"
      "movb $0, 0x100(%esi)\n"
      "cmpb $0, 0x167(%esi)\n"
      "je 1f\n"
      "movb $1, 0x168(%esi)\n"
      "jmp 2f\n"
      "1:\n"
      "movl %esi, %ecx\n"
      "movl $zpatch_dlc_load_callback, %eax\n"
      "call *(%eax)\n"
      "pushl %esi\n"
      "call dlc_discovery_post\n"
      "popl %esi\n"
      "movb $1, 0xa4(%esi)\n"
      "2:\n"
      "popl %esi\n"
      "popl %ebx\n"
      "addl $0x14, %esp\n"
      "ret\n");
}
#else
// 64-bit test builds never enter the ABI bodies (only the 32-bit ASI does);
// this stub keeps the portable build compilable.
void abi_load_dlcs_struct_fixup() {}
#endif
#endif

// Crash-probe bodies (debug-only). Each forces the condition its fix guards
// against, logs a hit, then falls through into the normal body so the fix's
// recovery path is exercised live. Registers/flags are preserved around the log
// call, so the normal body sees exactly the site's context with one register
// (or the blackscreen value) forced. Armed only via [Debug] CrashProbe.
#if defined(_MSC_VER)
[[maybe_unused]] __declspec(naked) void probe_blackscreen_shim() {
  __asm {
    pushad
    pushfd
    call zpatch_probe_hit
    popfd
    popad
    mov eax, DWORD PTR [zpatch_blackscreen_value_ptr]
    mov DWORD PTR [eax], 9
    jmp DWORD PTR [zpatch_probe_target]
  }
}
[[maybe_unused]] __declspec(naked) void probe_path_node_shim() {
  __asm {
    pushad
    pushfd
    call zpatch_probe_hit
    popfd
    popad
    mov eax, 0x1FFFDF
    jmp DWORD PTR [zpatch_probe_target]
  }
}
[[maybe_unused]] __declspec(naked) void probe_plane_crash_shim() {
  __asm {
    pushad
    pushfd
    call zpatch_probe_hit
    popfd
    popad
    xor eax, eax
    jmp DWORD PTR [zpatch_probe_target]
  }
}
[[maybe_unused]] __declspec(naked) void probe_wanted_level_shim() {
  __asm {
    pushad
    pushfd
    call zpatch_probe_hit
    popfd
    popad
    xor ecx, ecx
    jmp DWORD PTR [zpatch_probe_target]
  }
}
[[maybe_unused]] __declspec(naked) void probe_group_hack_shim() {
  __asm {
    pushad
    pushfd
    call zpatch_probe_hit
    popfd
    popad
    mov ecx, 8
    jmp DWORD PTR [zpatch_probe_target]
  }
}
[[maybe_unused]] __declspec(naked) void probe_smpa60_shim() {
  __asm {
    pushad
    pushfd
    call zpatch_probe_hit
    popfd
    popad
    mov ecx, 0xDDDDDDDD
    jmp DWORD PTR [zpatch_probe_target]
  }
}
// Fault-inclusive probes (2026-10-16): unlike the probes above these do NOT go
// through a fix's recovery path - they force the condition a removed
// fault-avoidance guard used to swallow, then run the de-guarded body, which
// must fault where the original faults. See the PRESERVED-FAULT catalogue.
[[maybe_unused]] __declspec(naked) void probe_fchs_null_lookup_shim() {
  __asm {
    mov DWORD PTR [zpatch_model_lookup], 0
    mov DWORD PTR [zpatch_fchs_hashes], 1
    jmp DWORD PTR [zpatch_probe_target]
  }
}
[[maybe_unused]] __declspec(naked) void probe_gfwl_zero_base_shim() {
  __asm {
    mov DWORD PTR [zpatch_xlive_base], 0
    mov DWORD PTR [zpatch_flag_gfwl_popup], 1
    mov DWORD PTR [zpatch_flag_patch4_modchecks], 0
    jmp DWORD PTR [zpatch_probe_target]
  }
}
[[maybe_unused]] __declspec(naked) void probe_garage_null_lookup_shim() {
  __asm {
    mov DWORD PTR [zpatch_model_lookup], 0
    mov BYTE PTR [zpatch_probe_garage_object + 19], 1
    mov ecx, OFFSET zpatch_probe_garage_object
    jmp DWORD PTR [zpatch_probe_target]
  }
}
[[maybe_unused]] __declspec(naked) void probe_driveby_return_zero() {
  __asm {
    xor eax, eax
    ret
  }
}
[[maybe_unused]] __declspec(naked) void probe_driveby_null_handle_shim() {
  __asm {
    mov eax, OFFSET probe_driveby_return_zero
    mov DWORD PTR [zpatch_driveby_resolver], eax
    mov DWORD PTR [zpatch_driveby_done], 0
    jmp DWORD PTR [zpatch_probe_target]
  }
}
#elif defined(__i386__)
#define ZP_PROBE_PROLOGUE \
    "pushal\n" "pushfl\n" "call zpatch_probe_hit\n" "popfl\n" "popal\n"
[[maybe_unused]] __attribute__((naked)) void probe_blackscreen_shim() {
  __asm__ __volatile__(ZP_PROBE_PROLOGUE "movl zpatch_blackscreen_value_ptr, %eax\n"
                                        "movl $9, (%eax)\n"
                                        "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_path_node_shim() {
  __asm__ __volatile__(ZP_PROBE_PROLOGUE "movl $0x1FFFDF, %eax\n"
                                        "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_plane_crash_shim() {
  __asm__ __volatile__(ZP_PROBE_PROLOGUE "xorl %eax, %eax\n" "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_wanted_level_shim() {
  __asm__ __volatile__(ZP_PROBE_PROLOGUE "xorl %ecx, %ecx\n" "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_group_hack_shim() {
  __asm__ __volatile__(ZP_PROBE_PROLOGUE "movl $8, %ecx\n" "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_smpa60_shim() {
  __asm__ __volatile__(ZP_PROBE_PROLOGUE "movl $0xDDDDDDDD, %ecx\n" "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_fchs_null_lookup_shim() {
  __asm__ __volatile__("movl $0, zpatch_model_lookup\n"
                       "movl $1, zpatch_fchs_hashes\n"
                       "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_gfwl_zero_base_shim() {
  __asm__ __volatile__("movl $0, zpatch_xlive_base\n"
                       "movl $1, zpatch_flag_gfwl_popup\n"
                       "movl $0, zpatch_flag_patch4_modchecks\n"
                       "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_garage_null_lookup_shim() {
  __asm__ __volatile__("movl $0, zpatch_model_lookup\n"
                       "movb $1, zpatch_probe_garage_object+19\n"
                       "movl $zpatch_probe_garage_object, %ecx\n"
                       "jmp *zpatch_probe_target\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_driveby_return_zero() {
  __asm__ __volatile__("xorl %eax, %eax\n" "ret\n");
}
[[maybe_unused]] __attribute__((naked)) void probe_driveby_null_handle_shim() {
  __asm__ __volatile__("movl $probe_driveby_return_zero, %eax\n"
                       "movl %eax, zpatch_driveby_resolver\n"
                       "movb $0, zpatch_driveby_done\n"
                       "jmp *zpatch_probe_target\n");
}
#undef ZP_PROBE_PROLOGUE
#else
[[maybe_unused]] void probe_blackscreen_shim() {}
[[maybe_unused]] void probe_path_node_shim() {}
[[maybe_unused]] void probe_plane_crash_shim() {}
[[maybe_unused]] void probe_wanted_level_shim() {}
[[maybe_unused]] void probe_group_hack_shim() {}
[[maybe_unused]] void probe_smpa60_shim() {}
[[maybe_unused]] void probe_fchs_null_lookup_shim() {}
[[maybe_unused]] void probe_gfwl_zero_base_shim() {}
[[maybe_unused]] void probe_garage_null_lookup_shim() {}
[[maybe_unused]] void probe_driveby_return_zero() {}
[[maybe_unused]] void probe_driveby_null_handle_shim() {}
#endif

// ---------------------------------------------------------------------------
// MouseFix (callback 0x10024600). The four axis helpers the callback E9s the
// game's mouse-read functions to (0x10024060/0x100240C0/0x10024120/0x10024170),
// the two bodies its module scan installs (0x10024560 at match-0xF of game
// 0xA81B1F; 0x100243A0 at match+0x746 of the same scan) and the internal helper
// 0x10024440 body 0x10024560 calls. All are byte-level transcriptions of the
// original trampolines (@exact): the decompiled C hides the x87 (st(0)) return
// convention, the xmm save/restore contract and the doubles' exact rounding.
// The one compiled helper (advance = original 0x100241E0) keeps its semantics
// without a register contract.
// ---------------------------------------------------------------------------
extern "C" void abi_mf_angle_step();

#if defined(_MSC_VER)
namespace {
void zpatch_mf_advance_c() noexcept {
  // sub_100241E0: when both mouse axes are zero, patch three 8-byte windows of
  // the resumed game function (dword_10159C40 + 52/94/107) with the "pull"
  // constants; otherwise NOP-ish them out. VirtualProtect failures are ignored
  // like the original (which would fault on a null resume; unreachable when the
  // install applied).
  const auto base = static_cast<std::uintptr_t>(zpatch_mf_resume_b);
  // Fault-preserving: sub_100241E0 dereferences the resume slot unconditionally
  // (the if (!base) return; fault-avoidance guard is removed).
  const auto* x = reinterpret_cast<const std::int32_t*>(
      static_cast<std::uintptr_t>(zpatch_mf_x));
  const auto* y = reinterpret_cast<const std::int32_t*>(
      static_cast<std::uintptr_t>(zpatch_mf_y));
  // sub_100241E0 evaluates Y first, then X, and dereferences both without
  // guards. Preserve the read order and its fault behavior.
  const bool zero = (static_cast<double>(*y) * 0.0078125 == 0.0) &&
                    (static_cast<double>(*x) * 0.0078125 == 0.0);
  struct Pair final {
    std::uintptr_t offset;
    std::uint32_t first;
    std::uint32_t second;
  };
  const Pair pairs[3] = {
      {52u, zero ? 0x86110FF3u : 0x90909090u, zero ? 0x144u : 0x90909090u},
      {94u, zero ? 0x8E110FF3u : 0x90909090u, zero ? 0x148u : 0x90909090u},
      {107u, zero ? 0x01489ED9u : 0x9090D8DDu, zero ? 0x5C8B0000u : 0x5C8B9090u},
  };
  for (const Pair& pair : pairs) {
    auto* target = reinterpret_cast<std::uint32_t*>(base + pair.offset);
    DWORD old = 0;
    if (!VirtualProtect(target, 8, PAGE_EXECUTE_READWRITE, &old)) continue;
    target[0] = pair.first;
    target[1] = pair.second;
    DWORD ignored = 0;
    VirtualProtect(target, 8, old, &ignored);
  }
  zpatch_cookie_tail();
}
}  // namespace

// @exact 0x10024060 axis-float helper: X/Y * 0.0078125 returned in st(0).
[[maybe_unused]] __declspec(naked) void abi_mouse_axis_float() {
  __asm {
    push ebp
    mov  ebp, esp
    mov  eax, dword ptr [ebp + 0Ch]
    sub  esp, 8
    test eax, eax
    jnz  mf_axis_float_y
    mov  eax, dword ptr [zpatch_mf_x]
    movd xmm0, dword ptr [eax]
    cvtdq2pd xmm0, xmm0
    mulsd xmm0, qword ptr [zpatch_mf_scale]
    movsd qword ptr [ebp - 8], xmm0
    fld  qword ptr [ebp - 8]
    mov  esp, ebp
    pop  ebp
    ret
  mf_axis_float_y:
    cmp  eax, 1
    jnz  mf_axis_float_zero
    mov  eax, dword ptr [zpatch_mf_y]
    movd xmm0, dword ptr [eax]
    cvtdq2pd xmm0, xmm0
    mulsd xmm0, qword ptr [zpatch_mf_scale]
    movsd qword ptr [ebp - 8], xmm0
    fld  qword ptr [ebp - 8]
    mov  esp, ebp
    pop  ebp
    ret
  mf_axis_float_zero:
    fldz
    mov  esp, ebp
    pop  ebp
    ret
    __emit 0CCh
  }
}
// @exact 0x100240C0 negated axis helper: -127.5 * value.
[[maybe_unused]] __declspec(naked) void abi_mouse_axis_neg() {
  __asm {
    push ebp
    mov  ebp, esp
    mov  eax, dword ptr [ebp + 0Ch]
    test eax, eax
    jnz  mf_axis_neg_y
    mov  eax, dword ptr [zpatch_mf_x]
  mf_axis_neg_xy:
    movd xmm0, dword ptr [eax]
    cvtdq2pd xmm0, xmm0
    mulsd xmm0, qword ptr [zpatch_mf_scale]
    jmp  mf_axis_neg_scale
  mf_axis_neg_y:
    cmp  eax, 1
    jnz  mf_axis_neg_zero
    mov  eax, dword ptr [zpatch_mf_y]
    jmp  mf_axis_neg_xy
  mf_axis_neg_zero:
    xorps xmm0, xmm0
  mf_axis_neg_scale:
    movss xmm1, dword ptr [zpatch_mf_sign]
    xorps xmm1, xmmword ptr [zpatch_mf_neg_mask]
    cvtpd2ps xmm0, xmm0
    mulss xmm1, xmm0
    movss dword ptr [ebp + 0Ch], xmm1
    mov  eax, dword ptr [ebp + 0Ch]
    pop  ebp
    ret
    __emit 0CCh
  }
}
// @exact 0x10024120 int axis helper: (int)(value * 0.0078125 * 255).
[[maybe_unused]] __declspec(naked) void abi_mouse_axis_int() {
  __asm {
    push ebp
    mov  ebp, esp
    mov  eax, dword ptr [ebp + 0Ch]
    test eax, eax
    jnz  mf_axis_int_y
    mov  eax, dword ptr [zpatch_mf_x]
  mf_axis_int_xy:
    movd xmm0, dword ptr [eax]
    cvtdq2pd xmm0, xmm0
    mulsd xmm0, qword ptr [zpatch_mf_scale]
    mulsd xmm0, qword ptr [zpatch_mf_scale_255]
    cvttsd2si eax, xmm0
    pop  ebp
    ret
  mf_axis_int_y:
    cmp  eax, 1
    jnz  mf_axis_int_zero
    mov  eax, dword ptr [zpatch_mf_y]
    jmp  mf_axis_int_xy
  mf_axis_int_zero:
    xorps xmm0, xmm0
    mulsd xmm0, qword ptr [zpatch_mf_scale_255]
    cvttsd2si eax, xmm0
    pop  ebp
    ret
    __emit 0CCh
  }
}
// @exact 0x10024170 flagged float helper: negated variant that flips sign again
// when the published game byte is non-zero.
[[maybe_unused]] __declspec(naked) void abi_mouse_axis_flag() {
  __asm {
    push ebp
    mov  ebp, esp
    mov  eax, dword ptr [ebp + 0Ch]
    test eax, eax
    jnz  mf_axis_flag_y
    mov  eax, dword ptr [zpatch_mf_x]
  mf_axis_flag_xy:
    movd xmm0, dword ptr [eax]
    cvtdq2pd xmm0, xmm0
    mulsd xmm0, qword ptr [zpatch_mf_scale]
    jmp  mf_axis_flag_scale
  mf_axis_flag_y:
    cmp  eax, 1
    jnz  mf_axis_flag_zero
    mov  eax, dword ptr [zpatch_mf_y]
    jmp  mf_axis_flag_xy
  mf_axis_flag_zero:
    xorps xmm0, xmm0
  mf_axis_flag_scale:
    movss xmm1, dword ptr [zpatch_mf_sign]
    xorps xmm1, xmmword ptr [zpatch_mf_neg_mask]
    mov  eax, dword ptr [zpatch_mf_flag]
    cvtpd2ps xmm0, xmm0
    cmp  byte ptr [eax], 0
    mulss xmm1, xmm0
    movss dword ptr [ebp + 0Ch], xmm1
    jz   mf_axis_flag_store
    mulss xmm1, dword ptr [zpatch_mf_mul_neg1]
    movss dword ptr [ebp + 0Ch], xmm1
  mf_axis_flag_store:
    mov  eax, dword ptr [ebp + 0Ch]
    pop  ebp
    ret
    __emit 0CCh
  }
}
// @exact 0x10024440 internal FPU helper called by abi_mouse_angle: advances the
// per-vehicle yaw (+0x148) and pitch (+0x144) by the mouse delta scaled by the
// frame-time factor, wrapping yaw into [-pi, pi]. Reads ECX (`this`).
[[maybe_unused]] __declspec(naked) void abi_mf_angle_step() {
  __asm {
    push ebp
    mov  ebp, esp
    and  esp, 0FFFFFFF8h
    sub  esp, 0Ch
    push esi
    mov  esi, ecx
    call dword ptr [zpatch_mf_time]
    movss xmm2, dword ptr [zpatch_mf_step_scale]
    mov  eax, dword ptr [zpatch_mf_x]
    fstp qword ptr [esp + 8]
    movsd xmm0, qword ptr [esp + 8]
    mulsd xmm0, qword ptr [zpatch_mf_time_mul]
    movd xmm4, dword ptr [eax]
    mov  eax, dword ptr [zpatch_mf_y]
    cvtdq2pd xmm4, xmm4
    addsd xmm0, qword ptr [zpatch_mf_time_add]
    movd xmm3, dword ptr [eax]
    mulsd xmm4, qword ptr [zpatch_mf_scale]
    mov  eax, dword ptr [zpatch_mf_flag]
    cvtdq2pd xmm3, xmm3
    cmp  byte ptr [eax], 0
    xorps xmm4, xmmword ptr [zpatch_mf_double_sign]
    mulsd xmm3, qword ptr [zpatch_mf_scale]
    cvtpd2ps xmm0, xmm0
    xorps xmm3, xmmword ptr [zpatch_mf_double_sign]
    mulss xmm2, xmm0
    mulss xmm2, dword ptr [esi + 60h]
    jz   mf_angle_step_skip
    mulsd xmm3, qword ptr [zpatch_mf_mul_neg1_d]
  mf_angle_step_skip:
    movss xmm0, dword ptr [esi + 144h]
    cvtps2pd xmm2, xmm2
    cvtps2pd xmm0, xmm0
    movaps xmm1, xmm2
    mulsd xmm2, xmm4
    mulsd xmm1, xmm3
    addsd xmm1, xmm0
    cvtpd2ps xmm0, xmm1
    movss dword ptr [esi + 144h], xmm0
    movss xmm0, dword ptr [esi + 148h]
    cvtps2pd xmm0, xmm0
    addsd xmm2, xmm0
    cvtpd2ps xmm0, xmm2
    cvtps2pd xmm1, xmm0
    comisd xmm1, qword ptr [zpatch_mf_pi]
    jbe  mf_angle_step_low
    subsd xmm1, qword ptr [zpatch_mf_two_pi]
    cvtpd2ps xmm0, xmm1
    movss dword ptr [esi + 148h], xmm0
    pop  esi
    mov  esp, ebp
    pop  ebp
    ret
  mf_angle_step_low:
    movsd xmm0, qword ptr [zpatch_mf_neg_pi]
    comisd xmm0, xmm1
    jbe  mf_angle_step_store
    addsd xmm1, qword ptr [zpatch_mf_two_pi]
  mf_angle_step_store:
    cvtpd2ps xmm0, xmm1
    movss dword ptr [esi + 148h], xmm0
    pop  esi
    mov  esp, ebp
    pop  ebp
    ret
    __emit 0CCh
  }
}
// @exact 0x10024560 MouseFix body A (installed at game 0xA81B10): saves xmm0-7
// through the plugin's scratch slots, advances the angles, restores them,
// replays the hooked function's 15-byte prologue and tail-jumps to the resume
// the handler published (match, game 0xA81B1F).
[[maybe_unused]] __declspec(naked) void abi_mouse_angle() {
  __asm {
    pushad
    movss dword ptr [zpatch_mf_xmm_save], xmm0
    movss dword ptr [zpatch_mf_xmm_save + 4], xmm1
    movss dword ptr [zpatch_mf_xmm_save + 8], xmm2
    movss dword ptr [zpatch_mf_xmm_save + 12], xmm3
    movss dword ptr [zpatch_mf_xmm_save + 16], xmm4
    movss dword ptr [zpatch_mf_xmm_save + 20], xmm5
    movss dword ptr [zpatch_mf_xmm_save + 24], xmm6
    movss dword ptr [zpatch_mf_xmm_save + 28], xmm7
    call abi_mf_angle_step
    movss xmm0, dword ptr [zpatch_mf_xmm_save]
    movss xmm1, dword ptr [zpatch_mf_xmm_save + 4]
    movss xmm2, dword ptr [zpatch_mf_xmm_save + 8]
    movss xmm3, dword ptr [zpatch_mf_xmm_save + 12]
    movss xmm4, dword ptr [zpatch_mf_xmm_save + 16]
    movss xmm5, dword ptr [zpatch_mf_xmm_save + 20]
    movss xmm6, dword ptr [zpatch_mf_xmm_save + 24]
    movss xmm7, dword ptr [zpatch_mf_xmm_save + 28]
    popad
    push ebp
    mov  ebp, esp
    and  esp, 0FFFFFFF0h
    xorps xmm0, xmm0
    sub  esp, 94h
    jmp  dword ptr [zpatch_mf_resume_a]
    __emit 0CCh
  }
}
// @exact 0x100243A0 MouseFix body B (installed at game 0xA82265): same xmm
// save/restore around the advance helper (sub_100241E0), then replays the three
// replaced instructions and tail-jumps to the published resume (match+0x756,
// game 0xA82275).
[[maybe_unused]] __declspec(naked) void abi_mouse_advance() {
  __asm {
    pushad
    movss dword ptr [zpatch_mf_xmm_save], xmm0
    movss dword ptr [zpatch_mf_xmm_save + 4], xmm1
    movss dword ptr [zpatch_mf_xmm_save + 8], xmm2
    movss dword ptr [zpatch_mf_xmm_save + 12], xmm3
    movss dword ptr [zpatch_mf_xmm_save + 16], xmm4
    movss dword ptr [zpatch_mf_xmm_save + 20], xmm5
    movss dword ptr [zpatch_mf_xmm_save + 24], xmm6
    movss dword ptr [zpatch_mf_xmm_save + 28], xmm7
    call zpatch_mf_advance_c
    movss xmm0, dword ptr [zpatch_mf_xmm_save]
    movss xmm1, dword ptr [zpatch_mf_xmm_save + 4]
    movss xmm2, dword ptr [zpatch_mf_xmm_save + 8]
    movss xmm3, dword ptr [zpatch_mf_xmm_save + 12]
    movss xmm4, dword ptr [zpatch_mf_xmm_save + 16]
    movss xmm5, dword ptr [zpatch_mf_xmm_save + 20]
    movss xmm6, dword ptr [zpatch_mf_xmm_save + 24]
    movss xmm7, dword ptr [zpatch_mf_xmm_save + 28]
    popad
    mulss xmm0, xmm2
    addss xmm0, xmm3
    movss xmm3, dword ptr [esi + 150h]
    jmp  dword ptr [zpatch_mf_resume_b]
    __emit 0CCh
  }
}
#else
[[maybe_unused]] void abi_mouse_axis_float() {}
[[maybe_unused]] void abi_mouse_axis_neg() {}
[[maybe_unused]] void abi_mouse_axis_int() {}
[[maybe_unused]] void abi_mouse_axis_flag() {}
[[maybe_unused]] void abi_mf_angle_step() {}
[[maybe_unused]] void abi_mouse_angle() {}
[[maybe_unused]] void abi_mouse_advance() {}
#endif

// ---------------------------------------------------------------------------
// OutOfCommissionFix (callback 0x1004C3F0). Scan 1 (pattern A9 39 44 24 18 at
// game 0xA22E90) E9s match+7 to the branch-widening body and publishes
// dword_10158034 = match+0x11 as its resume. Scan 2 (the freshly assigned
// pattern E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56 at game 0xB70733) E8s the
// match to the wrapper and publishes dword_10159BF8 (the E8 at match-0x33's
// target) / dword_10159C04 (the match E8's original target).
// ---------------------------------------------------------------------------
namespace {
// OutOfCommissionFix's real payload: the original embeds a compressed
// "finale2.sco" resource (163796 bytes at 0x100B6798) and its init
// (sub_1004B750 -> sub_1004B570/sub_1004B150) decodes it into a 179536-byte
// table, registers it, then sub_1004ACE0 builds an in-memory RPF archive around
// it. The wrapper mounts that archive with the game's own mount function
// ("memory:$<ptr>,<size>,0:") and the E9 body widens the game's script-id check
// to accept the archive's resource magic. The decode below mirrors
// sub_1004B150's opcode loop; the verified OOC Adler-32 of the table matches
// the blob's trailer (0x41E794AB), proving the transcription.
constexpr std::size_t kOocTableSize = 0x2BD50u;  // 179536
constexpr const unsigned char* kOocPakBlob = zpatch::zpatch_ooc_blob;

inline void zpatch_ooc_store(std::uint32_t* destination,
                             std::uint32_t value) noexcept {
  *reinterpret_cast<volatile std::uint32_t*>(destination) = value;
}

struct OocPakEntry final {
  char name_buffer[16];
  std::uint32_t name_size;
  std::uint32_t name_capacity;
  std::uint32_t file_size;
  std::uint32_t stored_offset;
  std::uint32_t stored_size;
  std::uint32_t padding;
  std::uint32_t data;
};
static_assert(sizeof(OocPakEntry) == 44);

void zpatch_ooc_register_entry(unsigned char* table) {
  auto* begin = reinterpret_cast<OocPakEntry*>(
      static_cast<std::uintptr_t>(zpatch::zpatch_ooc_vector_begin));
  auto* end = reinterpret_cast<OocPakEntry*>(
      static_cast<std::uintptr_t>(zpatch::zpatch_ooc_vector_end));
  auto* capacity = reinterpret_cast<OocPakEntry*>(
      static_cast<std::uintptr_t>(zpatch::zpatch_ooc_vector_capacity));
  const std::size_t size = begin == nullptr ? 0u
                                             : static_cast<std::size_t>(end - begin);
  const std::size_t cap = begin == nullptr ? 0u
                                            : static_cast<std::size_t>(capacity - begin);
  bool reallocated = false;
  if (size == cap) {
    const std::size_t new_capacity =
        (cap + cap / 2u > size) ? cap + cap / 2u : size + 1u;
    auto* replacement = static_cast<OocPakEntry*>(
        ::operator new(new_capacity * sizeof(OocPakEntry)));
    if (size != 0u) std::memcpy(replacement, begin, size * sizeof(OocPakEntry));
    if (begin != nullptr) ::operator delete(begin);
    begin = replacement;
    end = begin + size;
    capacity = begin + new_capacity;
    reallocated = true;
  }

  OocPakEntry entry{};
  std::memcpy(entry.name_buffer, "finale2.sco", sizeof("finale2.sco"));
  entry.name_size = 11u;
  entry.name_capacity = 15u;
  entry.file_size = static_cast<std::uint32_t>(kOocTableSize);
  entry.data = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table));
  std::memcpy(end, &entry, sizeof(entry));
  ++end;
  if (reallocated) {
    zpatch_ooc_store(&zpatch::zpatch_ooc_vector_begin,
                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(begin)));
    zpatch_ooc_store(&zpatch::zpatch_ooc_vector_end,
                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(end)));
    zpatch_ooc_store(&zpatch::zpatch_ooc_vector_capacity,
                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(capacity)));
  } else {
    zpatch_ooc_store(&zpatch::zpatch_ooc_vector_end,
                     static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(end)));
  }
}

struct OocStream final {
  std::size_t cursor = 0;  // the original's dword_101A8430
  unsigned char* dest = nullptr;

  void literal(std::size_t count, std::size_t from) noexcept {
    if (cursor + count <= kOocTableSize) {
      // The original's source bound (a1 + n >= dword_101887D4 = blob base - 16)
      // is constant-true: the stream starts past the base pointer.
      std::memcpy(dest + cursor, kOocPakBlob + from, count);
      cursor += count;
    } else {
      cursor = kOocTableSize + 1;
    }
  }
  void backref(std::size_t distance, std::size_t count) noexcept {
    if (cursor + count > kOocTableSize || distance > cursor) {
      cursor = kOocTableSize + 1;
      return;
    }
    unsigned char* out = dest + cursor;
    const unsigned char* in = out - distance;
    for (std::size_t i = 0; i < count; ++i) out[i] = in[i];  // overlap propagates
    cursor += count;
  }
  std::size_t step(std::size_t pc) noexcept {
    const unsigned char op = kOocPakBlob[pc];
    if (op < 0x20u) {
      if (op >= 0x18u) {
        backref(kOocPakBlob[pc + 2] + 256u * kOocPakBlob[pc + 1] + 65536u * op - 1572863u,
                kOocPakBlob[pc + 3] + 1u);
        return pc + 4;
      }
      if (op >= 0x10u) {
        backref(kOocPakBlob[pc + 2] + 256u * kOocPakBlob[pc + 1] + 65536u * op - 0xFFFFFu,
                (kOocPakBlob[pc + 3] << 8) + kOocPakBlob[pc + 4] + 1u);
        return pc + 5;
      }
      if (op >= 8u) {
        const std::size_t count = 256u * op + kOocPakBlob[pc + 1] - 2047u;
        literal(count, pc + 2);
        return pc + 2 + count;
      }
      if (op == 7u) {
        const std::size_t count = 256u * kOocPakBlob[pc + 1] + kOocPakBlob[pc + 2] + 1u;
        literal(count, pc + 3);
        return pc + 3 + count;
      }
      if (op == 6u) {
        backref(kOocPakBlob[pc + 3] + 256u * kOocPakBlob[pc + 2] +
                    65536u * kOocPakBlob[pc + 1] + 1u,
                kOocPakBlob[pc + 4] + 1u);
        return pc + 5;
      }
      if (op == 4u) {
        backref(kOocPakBlob[pc + 3] + 256u * kOocPakBlob[pc + 2] +
                    65536u * kOocPakBlob[pc + 1] + 1u,
                (kOocPakBlob[pc + 4] << 8) + kOocPakBlob[pc + 5] + 1u);
        return pc + 6;
      }
      return pc;  // no-progress opcode terminates the stream
    }
    if (op < 0x80u) {
      if (op >= 0x40u) {
        backref(kOocPakBlob[pc + 1] + 256u * op - 0x3FFFu, kOocPakBlob[pc + 2] + 1u);
        return pc + 3;
      }
      const std::size_t count = op - 31u;
      literal(count, pc + 1);
      return pc + (op - 30u);
    }
    backref(kOocPakBlob[pc + 1] + 1u, op - 127u);
    return pc + 2;
  }
};

// The original's Adler-32 variant (a = 1, b = 0, 1872-byte partition, 32-bit
// wrapping); reproduced because the blob's trailer was built with it.
std::uint32_t zpatch_ooc_adler32(const unsigned char* data, std::size_t size) noexcept {
  std::uint32_t a = 1u;
  std::uint32_t b = 0u;
  for (std::size_t block = 0; block < size; block += 1872u) {
    const std::size_t end = (block + 1872u < size) ? block + 1872u : size;
    for (std::size_t i = block; i < end; ++i) {
      a += data[i];
      b += a;
    }
    a %= 0xFFF1u;
    b %= 0xFFF1u;
  }
  return (b << 16) | a;
}

bool zpatch_ooc_decode(unsigned char* table) noexcept {
  // sub_1004B570: decode until the no-progress opcode, then require the
  // 05 FA terminator, the exact destination size and the Adler trailer.
  const auto input_bound = reinterpret_cast<std::uintptr_t>(kOocPakBlob) - 16u;
  const auto table_begin = reinterpret_cast<std::uintptr_t>(table);
  zpatch_ooc_store(&zpatch::zpatch_ooc_input_bound,
                   static_cast<std::uint32_t>(input_bound));
  zpatch_ooc_store(&zpatch::zpatch_ooc_output_begin,
                   static_cast<std::uint32_t>(table_begin));
  zpatch_ooc_store(&zpatch::zpatch_ooc_output_cursor,
                   static_cast<std::uint32_t>(table_begin));
  zpatch_ooc_store(&zpatch::zpatch_ooc_output_end,
                   static_cast<std::uint32_t>(table_begin + kOocTableSize));
  OocStream stream{0, table};
  std::size_t prior_cursor = stream.cursor;
  std::size_t pc = stream.step(0);
  if (stream.cursor != prior_cursor) {
    zpatch_ooc_store(&zpatch::zpatch_ooc_output_cursor,
                     static_cast<std::uint32_t>(table_begin + stream.cursor));
  }
  bool terminated = pc == 0;
  while (!terminated && stream.cursor <= kOocTableSize) {
    const std::size_t previous = pc;
    prior_cursor = stream.cursor;
    pc = stream.step(pc);
    if (stream.cursor != prior_cursor) {
      zpatch_ooc_store(&zpatch::zpatch_ooc_output_cursor,
                       static_cast<std::uint32_t>(table_begin + stream.cursor));
    }
    if (pc == previous) terminated = true;
  }
  if (!terminated || stream.cursor != kOocTableSize) return false;
  if (kOocPakBlob[pc] != 5u || kOocPakBlob[pc + 1] != 0xFAu) return false;
  const std::uint32_t trailer = (static_cast<std::uint32_t>(kOocPakBlob[pc + 2]) << 24) |
                                (static_cast<std::uint32_t>(kOocPakBlob[pc + 3]) << 16) |
                                (static_cast<std::uint32_t>(kOocPakBlob[pc + 4]) << 8) |
                                static_cast<std::uint32_t>(kOocPakBlob[pc + 5]);
  return zpatch_ooc_adler32(table, kOocTableSize) == trailer;
}

// sub_1004ACE0 for the single registered entry ("finale2.sco", table, 179536):
// 20-byte header, one 16-byte record, the name, then the payload at offset
// 2048 followed by the zero padding to the next 2048 boundary. The table is
// freed like the original; `final_offset` is the original's dword_10159C78.
unsigned char* zpatch_ooc_build_archive(unsigned char* table,
                                        std::uint32_t& final_offset) noexcept {
  const std::uint32_t size = static_cast<std::uint32_t>(kOocTableSize);
  const std::uint32_t remainder = size & 0x7FFu;
  const std::uint32_t pages = (size >> 11) + (remainder != 0u ? 1u : 0u);
  const std::uint32_t total = (pages << 11) + 2048u;
  // Fault-preserving (2026-10-16): no null check, like the original's archive
  // build (the memset below faults on a failed allocation).
  auto* archive = static_cast<unsigned char*>(std::malloc(total));
  if (archive == nullptr) {
    zpatch::log_printf("guard-removed option=OutOfCommissionFix archive_malloc=null "
               "fault=sub_1004B750 (unchecked archive memset)");
  }
  std::memset(archive, 0, total);
  std::uint32_t offset = 0;
  const auto append = [&](const void* source, std::uint32_t bytes) noexcept {
    std::memcpy(archive + offset, source, bytes);
    offset += bytes;
    zpatch_ooc_store(&zpatch::zpatch_ooc_archive_offset, offset);
  };
  zpatch_ooc_store(&zpatch::zpatch_ooc_archive_offset, 0);
  const char name[] = "finale2.sco";
  const std::uint32_t name_bytes = sizeof(name);  // includes the NUL
  struct OocHeader final {
    std::uint32_t fields[4];
    std::uint16_t words[2];
  };
  static_assert(sizeof(OocHeader) == 20);
  const OocHeader header{{0x86723DFFu, 3u, 1u, name_bytes + 16u},
                         {0x10u, 0xE9u}};
  append(&header, sizeof(header));
  unsigned char record[16] = {};
  std::memcpy(record, &size, 4);                 // physical size (entry[0x20])
  const std::uint32_t index = 1u;                // v9 + 1
  std::memcpy(record + 8, &index, 4);
  const std::uint16_t record_pages = static_cast<std::uint16_t>(pages);
  std::memcpy(record + 12, &record_pages, 2);    // sub_1004ABB0
  if (remainder != 0u) {
    const std::uint16_t padding = static_cast<std::uint16_t>(2048u - remainder);
    const std::uint16_t packed =
        static_cast<std::uint16_t>((static_cast<std::uint16_t>(0u) & 0xF800u) | padding);
    std::memcpy(record + 14, &packed, 2);        // sub_1004AB90
  }
  append(record, 16);
  append(name, name_bytes);
  offset = 2048u;                                // the original resets dword_10159C78
  zpatch_ooc_store(&zpatch::zpatch_ooc_archive_offset, offset);
  append(table, size);
  std::free(table);                              // j_j_free inside the payload loop
  if (remainder != 0u)
    offset += 2048u - remainder;                 // zero padding (already memset)
  if (remainder != 0u) zpatch_ooc_store(&zpatch::zpatch_ooc_archive_offset, offset);
  final_offset = offset;
  return archive;
}

// sub_1004B750: the one-shot init the wrapper runs before mounting. Its guard
// dword_10159C00 has no writer in the original, so it re-runs (and leaks the
// archive) on every call - kept 1:1.
std::uint32_t zpatch_ooc_lookup() noexcept {
  // Fault-preserving (2026-10-16): the original's sub_1004B750 does not check
  // the two mallocs (the decode memsets/writes through the table pointer, the
  // archive build memsets through the archive pointer); a failed allocation
  // faults there. The rebuilt helper's null returns were fault-avoidance and
  // are removed.
  auto* table = static_cast<unsigned char*>(std::malloc(kOocTableSize));
  if (table == nullptr) {
    zpatch::log_printf("guard-removed option=OutOfCommissionFix malloc=null "
               "fault=sub_1004B750 (unchecked table write)");
  }
  static_cast<void>(zpatch_ooc_decode(table));  // the original discards the result
  zpatch_ooc_store(&zpatch::zpatch_ooc_table_size,
                   static_cast<std::uint32_t>(kOocTableSize));
  zpatch_ooc_register_entry(table);
  std::uint32_t final_offset = 0;
  unsigned char* archive = zpatch_ooc_build_archive(table, final_offset);
  zpatch_ooc_store(&zpatch::zpatch_ooc_vector_end,
                   zpatch::zpatch_ooc_vector_begin);
  zpatch_ooc_store(&zpatch::zpatch_ooc_state, final_offset);
  return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(archive));
}

std::uint32_t zpatch_ooc_fnv1a(const unsigned char* data, std::size_t size) noexcept {
  std::uint32_t hash = 2166136261u;
  for (std::size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash;
}
}  // namespace

// Offline self-test for OutOfCommissionFix (used by tests/test_main.cpp): the
// decode must reproduce the blob's own Adler trailer and the archive must match
// the golden layout/hash produced by tools/extract_ooc_pak.py.
extern "C" bool zpatch_ooc_selftest() noexcept {
  auto* table = static_cast<unsigned char*>(std::malloc(kOocTableSize));
  if (table == nullptr) return false;
  const bool decoded = zpatch_ooc_decode(table);
  const std::uint32_t table_adler = zpatch_ooc_adler32(table, kOocTableSize);
  std::uint32_t final_offset = 0;
  unsigned char* archive = zpatch_ooc_build_archive(table, final_offset);
  if (archive == nullptr) {
    std::free(table);
    return false;
  }
  const std::uint32_t header[4] = {0x86723DFFu, 3u, 1u, 28u};
  bool header_ok = true;
  for (int i = 0; i < 4; ++i) header_ok = header_ok && std::memcmp(archive + 4 * i, &header[i], 4) == 0;
  header_ok = header_ok && archive[16] == 0x10u && archive[17] == 0u &&
              archive[18] == 0xE9u && archive[19] == 0u;
  const std::uint32_t record_size = 0x2BD50u;
  const std::uint32_t record_index = 1u;
  bool record_ok = std::memcmp(archive + 20, &record_size, 4) == 0;
  const std::uint32_t zero = 0u;
  record_ok = record_ok && std::memcmp(archive + 24, &zero, 4) == 0 &&
              std::memcmp(archive + 28, &record_index, 4) == 0;
  record_ok = record_ok && archive[32] == 88u && archive[33] == 0u &&
              archive[34] == 0xB0u && archive[35] == 0x02u;
  const bool name_ok = std::memcmp(archive + 36, "finale2.sco", 12) == 0;
  const bool ok = decoded && final_offset == 182272u && table_adler == 0x41E794ABu &&
                  header_ok && record_ok && name_ok &&
                  zpatch_ooc_fnv1a(archive, final_offset) == 0xB5AF7B10u;
  std::free(archive);
  return ok;
}

#if defined(_MSC_VER)
// @exact 0x1004C3C0 OutOfCommissionFix branch body (E9 target of game
// 0xA22E97): replay `cmp [esp+0x30], eax` + `movq xmm0, [esp+0x30]`; when the
// comparison was unequal, re-compare against the original's 0x86723DFF magic so
// the game's following `jz` also accepts the injected value; resume at match+0x11.
[[maybe_unused]] __declspec(naked) void abi_ooc_branch() {
  __asm {
    cmp  dword ptr [esp + 30h], eax
    movq xmm0, qword ptr [esp + 30h]
    jne  ooc_branch_check
    jmp  dword ptr [zpatch_ooc_resume]
  ooc_branch_check:
    mov  eax, 86723DFFh
    cmp  dword ptr [esp + 30h], eax
    jmp  dword ptr [zpatch_ooc_resume]
    __emit 0CCh
  }
}
#else
[[maybe_unused]] void abi_ooc_branch() {}
#endif

// 0x1004C350 OutOfCommissionFix wrapper (E8 target of game 0xB70733): run the
// one-shot fake-pak registration, print the fixed model name through the
// published logger, then call the game function the E8 replaced with the
// original arguments. Compiled: the original's register use is cdecl-clean.
extern "C" std::uint32_t abi_ooc_wrap(std::uint32_t arg0, std::uint32_t arg1) noexcept {
  const std::uint32_t lookup = zpatch_ooc_lookup();
  if (lookup != 0u) {
    char buffer[0x44];
    zpatch_spawns_sprintf(buffer, 0x40, zpatch_ooc_fmt_b, zpatch_ooc_fmt_a, 0x3A, 0x24,
                          static_cast<int>(lookup), static_cast<int>(zpatch_ooc_state),
                          0x30, 0x3A, 0);
    const auto logger = reinterpret_cast<void(__cdecl*)(char*, int, int)>(
        static_cast<std::uintptr_t>(zpatch_ooc_logger));
    if (logger == nullptr) {
      // Fault-preserving: sub_1004C350 calls dword_10159BF8 unconditionally
      // (guard removed 2026-10-16).
      zpatch::log_printf("guard-removed option=OutOfCommissionFix logger=null "
                         "fault=0x1004C39B");
    }
    logger(buffer, 1, -1);
  }
  const auto original = reinterpret_cast<std::uint32_t(__cdecl*)(std::uint32_t, std::uint32_t)>(
      static_cast<std::uintptr_t>(zpatch_ooc_orig));
  if (original == nullptr) {
    // Fault-preserving: sub_1004C350's `call dword_10159C04` is unconditional
    // (guard removed 2026-10-16).
    zpatch::log_printf("guard-removed option=OutOfCommissionFix orig=null "
                       "fault=0x1004C3B4");
  }
  return original(arg0, arg1);
}


// ---------------------------------------------------------------------------
// HighQualityReflections (callback 0x1001F390). The callback scans the pattern
// `8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8` (unique on every classic
// build and EFLC 1110/1120/1130; absent on 12043), writes an E9 at the match to
// the detour below, and publishes dword_10159CFC = match+0x13 (the resume) and
// dword_10159D04 = match + *(match+0xF) + 0x13 (the game's draw-command builder
// the detour calls). The original registers the callback/dest pointers via its
// module constructor at 0x10023CC0; our kAbiBodies entry is the equivalent.
// ---------------------------------------------------------------------------
#if defined(_MSC_VER)
// @exact 0x1001F030 detour body: dumps the registers (the original's crash aid),
// calls the game's draw-command builder 12 times with (index 0..11, flags
// 0x11D, mode 0) and resumes at match+0x13 (tail-jumped).
[[maybe_unused]] __declspec(naked) void abi_hqr_body() {
  __asm {
      mov       dword ptr [zpatch_hqr_dump], eax
      mov       dword ptr [zpatch_hqr_dump+4], ebx
      mov       dword ptr [zpatch_hqr_dump+8], ecx
      mov       dword ptr [zpatch_hqr_dump+12], edx
      mov       dword ptr [zpatch_hqr_dump+16], esi
      mov       dword ptr [zpatch_hqr_dump+20], edi
      mov       dword ptr [zpatch_hqr_dump+24], ebp
      mov       dword ptr [zpatch_hqr_dump+28], esp
      mov       eax, 0
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 1
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 2
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 3
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 4
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 5
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 6
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 7
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 8
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 9
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 0xa
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      mov       eax, dword ptr [zpatch_hqr_dump]
      mov       ebx, dword ptr [zpatch_hqr_dump+4]
      mov       ecx, dword ptr [zpatch_hqr_dump+8]
      mov       edx, dword ptr [zpatch_hqr_dump+12]
      mov       esi, dword ptr [zpatch_hqr_dump+16]
      mov       edi, dword ptr [zpatch_hqr_dump+20]
      mov       ebp, dword ptr [zpatch_hqr_dump+24]
      mov       esp, dword ptr [zpatch_hqr_dump+28]
      mov       eax, 0xb
      push      dword ptr [zpatch_hqr_mode]
      push      dword ptr [zpatch_hqr_flags]
      push      eax
      call      dword ptr [zpatch_hqr_builder]
      jmp       dword ptr [zpatch_hqr_resume]
      __emit 0CCh
  }
}
#else
[[maybe_unused]] void abi_hqr_body() {}
#endif

// ---------------------------------------------------------------------------
// CutsceneFixes chain B body (original sub_100388C0, the dest of the E8
// installs the worker 0x10038B40 writes at the two game camera call sites): a
// thiscall `char(float)` entered from the patched calls. It reads a scene float
// through the loader-published native-script resolver (the handle is the loader
// salt dword_1015803C, ecx/esi), pokes the camera function's
// `mov byte [esp+0x1C], imm` immediate at target+0x4E (published as target+0x4F
// by the worker), calls the original camera function, and - once the
// accumulated frame time passes 0.3333 - repeats the call with the immediate
// forced to 0 and restores the pre-call object (the second 1280-byte copy) when
// the two runs' position floats differ beyond (0.05, 0.05, 0.05, 0.5, 0.5,
// 0.5), resetting the accumulator. Instruction-for-instruction transcription of
// 0x100388C0..0x10038B3F (verify-bodies @exact).
// ---------------------------------------------------------------------------
#if defined(_MSC_VER) && !defined(_WIN64)
// The worker's sub_10003980: the resolver call with the salt in ECX and ESI.
extern "C" __declspec(naked) std::uint32_t zpatch_cutscene_scene() {
  __asm {
    push esi
    mov esi, ecx
    mov eax, dword ptr [zpatch_driveby_resolver]
    call eax
    pop esi
    ret
  }
}
// The worker's sub_1005FE7B cookie check (cookie in ECX), same body as the
// spawns probe's: it must preserve EAX/AL because the cutscene body returns
// its result in AL across this call.
__declspec(naked) void zpatch_cutscene_cookie_check() {
  __asm {
    jmp zpatch_spawns_cookie_check_ecx
  }
}
// @exact 0x100388C0
[[maybe_unused]] __declspec(naked) void abi_cutscene_chain_b() {
  __asm {
    push ebp
    mov ebp, esp
    sub esp, 0A18h
    mov eax, dword ptr [__security_cookie]
    xor eax, ebp
    mov dword ptr [ebp - 4], eax
    push esi
    mov esi, ecx
    mov ecx, dword ptr [zpatch_salt_3c]
    push edi
    mov dword ptr [ebp - 0A10h], esi
    call zpatch_cutscene_scene
    lea edi, [ebp - 504h]
    mov ecx, dword ptr [eax + 0Eh]
    mov eax, dword ptr [ecx + eax + 18h]
    mov ecx, 140h
    rep movsd
    movss xmm0, dword ptr [eax + 8]
    lea eax, [ebp - 0A08h]
    mov esi, dword ptr [zpatch_cutscene_poke]
    mov edi, dword ptr [zpatch_cutscene_virtualprotect]
    dec esi
    cvtps2pd xmm0, xmm0
    push eax
    push 40h
    push 1
    push esi
    addsd xmm0, qword ptr [zpatch_cutscene_accum]
    movsd qword ptr [zpatch_cutscene_accum], xmm0
    call edi
    lea eax, [ebp - 0A08h]
    mov byte ptr [esi], 1
    push eax
    push dword ptr [ebp - 0A08h]
    push 1
    push esi
    call edi
    mov eax, dword ptr [zpatch_cutscene_target]
    movss xmm0, dword ptr [ebp + 8]
    push ecx
    mov ecx, dword ptr [ebp - 0A10h]
    mov dword ptr [ebp - 0A14h], eax
    movss dword ptr [esp], xmm0
    call dword ptr [ebp - 0A14h]
    fstp qword ptr [ebp - 0A18h]
    movsd xmm0, qword ptr [ebp - 0A18h]
    ucomisd xmm0, qword ptr [zpatch_cutscene_zero]
    lahf
    test ah, 44h
    jnp ccb_al_zero
    mov al, 1
    jmp ccb_al_done
  ccb_al_zero:
    xor al, al
  ccb_al_done:
    mov edx, dword ptr [ebp - 0A10h]
    lea edi, [ebp - 0A04h]
    movsd xmm0, qword ptr [zpatch_cutscene_limit]
    mov ecx, 140h
    comisd xmm0, qword ptr [zpatch_cutscene_accum]
    mov esi, edx
    mov byte ptr [ebp - 0A09h], al
    rep movsd
    ja ccb_return_al
    mov edi, edx
    lea eax, [ebp - 0A08h]
    push eax
    mov ecx, 140h
    lea esi, [ebp - 504h]
    rep movsd
    mov esi, dword ptr [zpatch_cutscene_poke]
    mov edi, dword ptr [zpatch_cutscene_virtualprotect]
    dec esi
    push 40h
    push 1
    push esi
    call edi
    lea eax, [ebp - 0A08h]
    mov byte ptr [esi], 0
    push eax
    push dword ptr [ebp - 0A08h]
    push 1
    push esi
    call edi
    mov eax, dword ptr [zpatch_cutscene_target]
    movss xmm0, dword ptr [ebp + 8]
    push ecx
    mov ecx, dword ptr [ebp - 0A10h]
    mov dword ptr [ebp - 0A14h], eax
    movss dword ptr [esp], xmm0
    call dword ptr [ebp - 0A14h]
    fstp qword ptr [ebp - 0A18h]
    movsd xmm0, qword ptr [ebp - 0A18h]
    xorps xmm3, xmm3
    ucomisd xmm0, xmm3
    lahf
    test ah, 44h
    jnp ccb_dl_zero
    mov dl, 1
    jmp ccb_dl_done
  ccb_dl_zero:
    xor dl, dl
  ccb_dl_done:
    mov eax, dword ptr [ebp - 0A10h]
    lea edi, [ebp - 504h]
    movss xmm1, dword ptr [zpatch_cutscene_abs_mask]
    mov ecx, 140h
    movss xmm2, dword ptr [zpatch_cutscene_eps_a]
    mov esi, eax
    rep movsd
    movss xmm0, dword ptr [ebp - 4E4h]
    subss xmm0, dword ptr [ebp - 9E4h]
    andps xmm0, xmm1
    comiss xmm0, xmm2
    ja ccb_restore
    movss xmm0, dword ptr [ebp - 4E0h]
    subss xmm0, dword ptr [ebp - 9E0h]
    andps xmm0, xmm1
    comiss xmm0, xmm2
    ja ccb_restore
    movss xmm0, dword ptr [ebp - 4DCh]
    subss xmm0, dword ptr [ebp - 9DCh]
    andps xmm0, xmm1
    comiss xmm0, xmm2
    ja ccb_restore
    movss xmm0, dword ptr [ebp - 4C4h]
    subss xmm0, dword ptr [ebp - 9C4h]
    movss xmm2, dword ptr [zpatch_cutscene_eps_b]
    andps xmm0, xmm1
    comiss xmm0, xmm2
    ja ccb_restore
    movss xmm0, dword ptr [ebp - 4C0h]
    subss xmm0, dword ptr [ebp - 9C0h]
    andps xmm0, xmm1
    comiss xmm0, xmm2
    ja ccb_restore
    movss xmm0, dword ptr [ebp - 4BCh]
    subss xmm0, dword ptr [ebp - 9BCh]
    andps xmm0, xmm1
    comiss xmm0, xmm2
    ja ccb_restore
    pop edi
    mov al, dl
    pop esi
    mov ecx, dword ptr [ebp - 4]
    xor ecx, ebp
    call zpatch_cutscene_cookie_check
    mov esp, ebp
    pop ebp
    ret 4
  ccb_restore:
    mov edi, eax
    movsd qword ptr [zpatch_cutscene_accum], xmm3
    mov al, byte ptr [ebp - 0A09h]
    lea esi, [ebp - 0A04h]
    mov ecx, 140h
    rep movsd
  ccb_return_al:
    mov ecx, dword ptr [ebp - 4]
    pop edi
    xor ecx, ebp
    pop esi
    call zpatch_cutscene_cookie_check
    mov esp, ebp
    pop ebp
    ret 4
    __emit 0CCh
  }
}
#else
[[maybe_unused]] void abi_cutscene_chain_b() {}
#endif

// ---------------------------------------------------------------------------
// NewMemorySystem: the reimplemented CRageAllocator whose vtable (original
// 0x100B5038) the E8 body writes into the game's allocator singleton. The
// original swaps the game's arena allocator for one built on HeapAlloc:
// allocate(size, align, flags) uses unit = max(0x10, align), want =
// max(size, unit), HeapAllocs want + 2*unit bytes, rounds the block up to
// ((base-1)/unit + 2)*unit, stores the offset to the HeapAlloc base at [p-4],
// fills want bytes with 0xCD and registers the pointer in the plugin's own
// hash map (FNV-1a over the pointer's four bytes) under the critical section at
// 0x101A0808; free() recovers the base from [p-4], HeapFrees it and
// unregisters. The registry is only observable through vtable[18] (the game's
// "is this pointer mine" query), so the port keeps the same operations with a
// plain set. The two big methods and [18] are compiled ports (INFO); every
// other method is a disassembly transcription (@exact).
// ---------------------------------------------------------------------------
#if defined(_MSC_VER) && !defined(_WIN64)
// @exact 0x10038560
[[maybe_unused]] __declspec(naked) void abi_nms_noop_ret0() {
  __asm {
    ret 0
    __emit 0CCh
  }
}
// The shared trivial stubs (0x100057F0 / 0x10038550 / 0x10038570) already have
// verified bodies for other features; the vtable reuses them.
int __cdecl abi_better_mp_sync();
int __cdecl abi_rgsc_login_removed();
int __stdcall abi_savegame_fix_null(int, int);

// @exact 0x10038640
[[maybe_unused]] __declspec(naked) void abi_nms_ctor() {
  __asm {
    push ebp
    mov ebp, esp
    mov eax, dword ptr [ebp + 8]
    push esi
    mov esi, ecx
    and eax, 5
    mov dword ptr [esi], offset zpatch_nms_vtable
    cmp al, 5
    jne nms_ctor_done
    push 4
    push esi
    call abi_nms_noop_ret0
    add esp, 8
  nms_ctor_done:
    mov eax, esi
    pop esi
    pop ebp
    ret 4
    __emit 0CCh
  }
}
// @exact 0x10038350
[[maybe_unused]] __declspec(naked) void abi_nms_reject_ptr() {
  __asm {
    xor al, al
    ret 4
    __emit 0CCh
  }
}
// @exact 0x10038540
[[maybe_unused]] __declspec(naked) void abi_nms_self() {
  __asm {
    mov eax, ecx
    ret 4
    __emit 0CCh
  }
}
// @exact 0x10001950
[[maybe_unused]] __declspec(naked) void abi_nms_zero_ptr() {
  __asm {
    xor eax, eax
    ret 4
    __emit 0CCh
  }
}
// @exact 0x10038580
[[maybe_unused]] __declspec(naked) void abi_nms_noop_pair() {
  __asm {
    ret 8
    __emit 0CCh
  }
}
// @exact 0x10038590
[[maybe_unused]] __declspec(naked) void abi_nms_reject() {
  __asm {
    xor al, al
    ret
    __emit 0CCh
  }
}
// @exact 0x10038620
[[maybe_unused]] __declspec(naked) void abi_nms_thunk18() {
  __asm {
    push ebp
    mov ebp, esp
    mov eax, dword ptr [ecx]
    pop ebp
    jmp dword ptr [eax + 18h]
    __emit 0CCh
  }
}
// @exact 0x10038630
[[maybe_unused]] __declspec(naked) void abi_nms_noop_arg() {
  __asm {
    ret 4
    __emit 0CCh
  }
}
// @exact 0x10038670 insns=5
[[maybe_unused]] __declspec(naked) void abi_nms_apply() {
  __asm {
    mov eax, dword ptr [zpatch_nms_object]
    test eax, eax
    je nms_apply_done
    mov dword ptr [eax], offset zpatch_nms_vtable
  nms_apply_done:
    ret
    __emit 0CCh
  }
}
#else
[[maybe_unused]] void abi_nms_ctor() {}
[[maybe_unused]] void abi_nms_reject_ptr() {}
[[maybe_unused]] void abi_nms_self() {}
[[maybe_unused]] void abi_nms_zero_ptr() {}
[[maybe_unused]] void abi_nms_noop_ret0() {}
[[maybe_unused]] void abi_nms_noop_pair() {}
[[maybe_unused]] void abi_nms_reject() {}
[[maybe_unused]] void abi_nms_thunk18() {}
[[maybe_unused]] void abi_nms_noop_arg() {}
[[maybe_unused]] void abi_nms_apply() {}
#endif

namespace {
// NewMemorySystem's allocation registry: the original keeps the pointers it
// handed out in its own hash map (FNV-1a over the pointer's four bytes) under
// the recursive critical section at 0x101A0808. Only membership is observable,
// so the port stores the keys in a plain set behind an equivalent lock.
CRITICAL_SECTION g_nms_lock;
std::unordered_set<std::uint32_t> g_nms_allocations;
struct NmsRegistryInit final {
  NmsRegistryInit() noexcept { InitializeCriticalSection(&g_nms_lock); }
};
[[maybe_unused]] NmsRegistryInit g_nms_registry_init;
}  // namespace

// vtable[2] (0x10038360): HeapAlloc-backed allocate with the original's
// alignment scheme. ECX is the object (unused by the original's body too).
extern "C" void* __stdcall zpatch_nms_alloc(std::uint32_t size, std::uint32_t align,
                                            std::uint32_t flags) noexcept {
  (void)flags;
  const std::uint32_t unit = align < 0x10u ? 0x10u : align;
  const std::uint32_t want = size < unit ? unit : size;
  void* base = HeapAlloc(GetProcessHeap(), 0, want + unit * 2u);
  if (!base) return nullptr;
  const auto raw = reinterpret_cast<std::uintptr_t>(base);
  const std::uintptr_t aligned = ((raw - 1u) / unit + 2u) * unit;
  *reinterpret_cast<std::uint32_t*>(aligned - 4u) =
      static_cast<std::uint32_t>(aligned - raw);
  auto* result = reinterpret_cast<void*>(aligned);
  std::memset(result, 0xCD, want);
  EnterCriticalSection(&g_nms_lock);
  g_nms_allocations.insert(static_cast<std::uint32_t>(aligned));
  LeaveCriticalSection(&g_nms_lock);
  return result;
}

// vtable[3] (0x10038430): recover the HeapAlloc base from [ptr-4], free it and
// drop the pointer from the registry.
extern "C" void __stdcall zpatch_nms_free(void* ptr) noexcept {
  if (!ptr) return;
  const auto raw = reinterpret_cast<std::uintptr_t>(ptr);
  const std::uintptr_t base = raw - *reinterpret_cast<const std::uint32_t*>(raw - 4u);
  HeapFree(GetProcessHeap(), 0, reinterpret_cast<void*>(base));
  EnterCriticalSection(&g_nms_lock);
  g_nms_allocations.erase(static_cast<std::uint32_t>(raw));
  LeaveCriticalSection(&g_nms_lock);
}

// vtable[18] (0x100385A0): the game's "is this pointer mine" query.
extern "C" std::uint8_t __stdcall zpatch_nms_contains(std::uint32_t key) noexcept {
  EnterCriticalSection(&g_nms_lock);
  const bool found = g_nms_allocations.find(key) != g_nms_allocations.end();
  LeaveCriticalSection(&g_nms_lock);
  return found ? std::uint8_t{1} : std::uint8_t{0};
}

// The CRageAllocator vtable, entry order and per-entry conventions as the
// original's 0x100B5038 (see the individual methods above).
extern "C" {
std::uintptr_t zpatch_nms_vtable[24] = {
    reinterpret_cast<std::uintptr_t>(&abi_nms_ctor),         // [0] 0x10038640
    reinterpret_cast<std::uintptr_t>(&abi_nms_reject_ptr),   // [1] 0x10038350
    reinterpret_cast<std::uintptr_t>(&zpatch_nms_alloc),     // [2] 0x10038360
    reinterpret_cast<std::uintptr_t>(&zpatch_nms_free),      // [3] 0x10038430
    reinterpret_cast<std::uintptr_t>(&abi_nms_self),         // [4] 0x10038540
    reinterpret_cast<std::uintptr_t>(&abi_nms_self),         // [5] 0x10038540
    reinterpret_cast<std::uintptr_t>(&abi_nms_zero_ptr),     // [6] 0x10001950
    reinterpret_cast<std::uintptr_t>(&abi_nms_zero_ptr),     // [7] 0x10001950
    reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [8] 0x100057F0
    reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [9] 0x100057F0
    reinterpret_cast<std::uintptr_t>(&abi_rgsc_login_removed),  // [10] 0x10038550
    reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [11] 0x10038560
    reinterpret_cast<std::uintptr_t>(&abi_savegame_fix_null),   // [12] 0x10038570
    reinterpret_cast<std::uintptr_t>(&abi_nms_noop_pair),    // [13] 0x10038580
    reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [14] 0x10038560
    reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [15] 0x10038590
    reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [16] 0x10038590
    reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0),    // [17] 0x10038560
    reinterpret_cast<std::uintptr_t>(&zpatch_nms_contains),  // [18] 0x100385A0
    reinterpret_cast<std::uintptr_t>(&abi_nms_thunk18),      // [19] 0x10038620
    reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [20] 0x100057F0
    reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync),   // [21] 0x100057F0
    reinterpret_cast<std::uintptr_t>(&abi_nms_noop_arg),     // [22] 0x10038630
    reinterpret_cast<std::uintptr_t>(&abi_nms_reject),       // [23] 0x10038590
};
}  // extern "C"

struct AbiBody final {
  std::uint32_t reference_va;
  std::uintptr_t (*address)();
};

// Only destinations whose conventions are verified appear here; everything else
// resolves to 0 and the installer reports it as skipped.
const AbiBody kAbiBodies[] = {
    {0x10005590u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_aim_projectile_crash_fix);
     }},
    {0x100057f0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_better_mp_sync);
     }},
    {0x10005ed0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_combined_radio_freeze_fix);
     }},
    {0x10006880u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_carparts_shadow);
     }},
    {0x10044530u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_popup_headlights);
     }},
    {0x10044560u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_popup_headlights_edx);
     }},
    {0x1001ADB0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_fchs_shim);
     }},
    {0x100064F0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_dual_headlights_new);
     }},
    {0x10006630u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_dual_headlights_old);
     }},
    {0x1001B2E0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_car_decals_fix);
     }},
    {0x10037BA0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_bad_player_model);
     }},
    {0x10050AD0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_death_music_install);
     }},
    {0x10050A50u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_death_music_enabled);
     }},
    {0x100521E0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_new_saves_compat);
     }},
    {0x100054E0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_gfwl_stub);
     }},
    {0x1003D3E0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nolivery_e);
     }},
    {0x100068A0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_carparts_shadow_2);
     }},
    {0x100068D0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_carparts_shadow_2b);
     }},
    {0x10046C80u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_garage_body_a);
     }},
    {0x10046CA0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_garage_body_b);
     }},
    {0x100051B0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_steer_detour_eax);
     }},
    {0x100051E0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_steer_detour_edx);
     }},
    {0x100512F0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_local_settings_detour);
     }},
    {0x10051300u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_local_settings_fallback_detour);
     }},
    {0x10037510u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_probe);
     }},
    {0x100372F0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_guard);
     }},
    {0x10037330u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0x24);
     }},
    {0x100373F0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0x3D);
     }},
    {0x10037420u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0x56);
     }},
    {0x10037390u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0x6F);
     }},
    {0x10037450u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0x88);
     }},
    {0x10037480u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0x9E);
     }},
    {0x100374E0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0xB4);
     }},
    {0x10037360u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0xCA);
     }},
    {0x100373C0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0xE0);
     }},
    {0x100374B0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_spawns_case_0xF6);
     }},
    {0x10003e50u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_game_hash);
     }},
    {0x10050870u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_ped_models_in_cutscenes_check);
     }},
    {0x10045a30u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_improved_shader_streaming_find_hpe);
     }},
    {0x10045a80u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_improved_shader_streaming_find_v);
     }},
    {0x1004aab0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_vram_fix_shim);
     }},
    // 0x100064b0 (DrivebyFOVFix) is registered above (2026-10-13): the arm
    // installs unconditionally when its pattern matches, and the pattern exists
    // on 1.0.8.0 (1 hit), so the original does install it there. The earlier
    // "not installed on 1080" reading came from the coverage table's
    // handler_va=0/function-call-only entry, which missed the arm-only install.
    {0x1003d310u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nolivery_a);
     }},
    {0x1003d340u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nolivery_b);
     }},
    {0x1003d370u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nolivery_c);
     }},
    {0x1003d3a0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nolivery_d);
     }},
    {0x10036f00u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_crash_msg_shim);
     }},
    {0x10051600u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_newfiles_register_body);
     }},
    {0x10051890u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_newfiles_update_a);
     }},
    {0x100518d0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_newfiles_update_b);
     }},
    {0x10005650u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_benchmark_a);
     }},
    {0x10005670u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_benchmark_b);
     }},
    {0x10005af0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_building_dynamic_shadows);
     }},
    {0x10006310u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_copsirens_a);
     }},
    {0x10006330u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_copsirens_b);
     }},
    {0x100040e0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_copsirens_ide);
     }},
    {0x10005890u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_borderless_w);
     }},
    {0x10005900u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_borderless_a);
     }},
    {0x10050540u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_speedup_body);
     }},
    {0x1001b080u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_episode_switch_sum);
     }},
    {0x10023480u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_load_dlcs_dummy);
     }},
    {0x10023490u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_load_dlcs_struct_fixup);
     }},
    {0x10045C30u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_skip_intro_body);
     }},
    {0x10046610u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_skip_menu_body);
     }},
    {0x10038550u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_rgsc_login_removed);
     }},
    {0x10038570u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_savegame_fix_null);
     }},
    // NewMemorySystem's CRageAllocator stubs (the E8 body's vtable); the alloc/
    // free/contains methods are compiled ports registered only through the
    // vtable, not here.
    {0x10038640u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_ctor);
     }},
    {0x10038350u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_reject_ptr);
     }},
    {0x10038540u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_self);
     }},
    {0x10001950u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_zero_ptr);
     }},
    {0x10038560u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_noop_ret0);
     }},
    {0x10038580u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_noop_pair);
     }},
    {0x10038590u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_reject);
     }},
    {0x10038620u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_thunk18);
     }},
    {0x10038630u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_noop_arg);
     }},
    {0x10038670u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_nms_apply);
     }},
    {0x10045580u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_savegame_fix_close);
     }},
    {0x100455a0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_savegame_fix_copy);
     }},
    {0x100455e0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_savegame_fix_mutex);
     }},
    // Crash-fix shims (0x10036ee0..0x10005d80). Re-enabled after the two
    // register/frame bugs found against the originals: WantedLevelCrashFix's
    // null path pops ebx (the site sits after `push ebx`), and CollisionDeadlock
    // needs eax/edx for its predicate (see the body).
    {0x10036ee0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_blackscreen_shim);
     }},
    {0x1003d7b0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_path_node_shim);
     }},
    {0x1003d870u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_plane_crash_shim);
     }},
    {0x10050490u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_wanted_level_shim);
     }},
    {0x10036e90u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_group_hack_shim);
     }},
    {0x10046750u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_smpa60_shim);
     }},
    {0x10046780u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_smpa60_tail_shim);
     }},
    {0x10036eb0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_freeze_gun_shim);
     }},
    {0x1003d9a0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_player_ped_components_shim);
     }},
    {0x1001af60u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_emissive_lerp_shim);
     }},
    {0x100469e0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_super_lod_fix_shim);
     }},
    {0x1001b040u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_episode_switch_store_shim);
     }},
    {0x1001b060u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_episode_switch_shim_b);
     }},
    {0x1001b0a0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_episode_switch_vcall);
     }},
    {0x1001b0c0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_episode_switch_shim_c);
     }},
    {0x1001ef60u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_bike_physics_detour);
     }},
    {0x10005d80u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_collision_deadlock_walk);
     }},
    {0x10024060u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_mouse_axis_float);
     }},
    {0x100240C0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_mouse_axis_neg);
     }},
    {0x10024120u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_mouse_axis_int);
     }},
    {0x10024170u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_mouse_axis_flag);
     }},
    {0x10024440u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_mf_angle_step);
     }},
    {0x10024560u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_mouse_angle);
     }},
    {0x100243A0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_mouse_advance);
     }},
    {0x1004C3C0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_ooc_branch);
     }},
    {0x1004C350u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_ooc_wrap);
     }},
    {0x1001F030u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_hqr_body);
     }},
    {0x100388C0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_cutscene_chain_b);
     }},
    {0x100064B0u, []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&abi_driveby_fov_shim);
     }},
};

// Debug crash probes. CollisionDeadlockFix is not listed: its crash condition is
// a malformed node chain, which cannot be forced with a single register write.
struct ProbeBody final {
  const char* option;
  std::uint32_t reference_va;
  std::uintptr_t (*normal)();
  std::uintptr_t (*probe)();
};

const ProbeBody kProbeBodies[] = {
    {"BlackscreenFix", 0x10036ee0u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_blackscreen_shim); },
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&probe_blackscreen_shim); }},
    {"PathNodeCrashFix", 0x1003d7b0u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_path_node_shim); },
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&probe_path_node_shim); }},
    {"PlaneCrashFix", 0x1003d870u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_plane_crash_shim); },
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&probe_plane_crash_shim); }},
    {"WantedLevelCrashFix", 0x10050490u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_wanted_level_shim); },
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&probe_wanted_level_shim); }},
    {"GroupHackFix", 0x10036e90u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_group_hack_shim); },
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&probe_group_hack_shim); }},
    {"SMPA60Fix", 0x10046750u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_smpa60_shim); },
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&probe_smpa60_shim); }},
    // Fault-inclusive probes: these force the condition a removed
    // fault-avoidance guard used to swallow; the de-guarded body then faults
    // where the original faults (no recovery path - that is the point).
    {"ForceCarHeadlightShadows", 0x1001ADB0u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_fchs_shim); },
     []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&probe_fchs_null_lookup_shim);
     }},
    {"RemoveGFWLUpdatePopup", 0x100054E0u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_gfwl_stub); },
     []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&probe_gfwl_zero_base_shim);
     }},
    {"GarageModelFix", 0x10046CA0u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_garage_body_b); },
     []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&probe_garage_null_lookup_shim);
     }},
    {"DrivebyFOVFix", 0x100064B0u,
     []() -> std::uintptr_t { return reinterpret_cast<std::uintptr_t>(&abi_driveby_fov_shim); },
     []() -> std::uintptr_t {
       return reinterpret_cast<std::uintptr_t>(&probe_driveby_null_handle_shim);
     }},
};

std::uint32_t g_probe_va = 0;

}  // namespace

#if defined(ZP_PROBE_TRACE)
// Diagnostic wrappers for the FreezeCarFix spawn probe and guard. Entered by the
// game's `call` exactly like the real bodies, so the stack/register contract is
// unchanged: the wrapper logs around a `call` to the real body and forwards its
// return value. ZolikaPatch.ptrace.log records site (return address), argument,
// kind/class/skip state before and the result/skip state after.
__declspec(naked) void abi_spawns_probe_traced() {
  __asm {
    push ebp
    mov  ebp, esp
    push ebx
    pushad
    pushfd
    push dword ptr [ebp + 8]
    push dword ptr [ebp + 4]
    call zpatch_ptrace_probe_in
    add  esp, 8
    popfd
    popad
    push dword ptr [ebp + 8]
    call abi_spawns_probe
    add  esp, 4
    mov  ebx, eax
    pushfd
    pushad
    push ebx
    push dword ptr [ebp + 8]
    push dword ptr [ebp + 4]
    call zpatch_ptrace_probe_out
    add  esp, 12
    popad
    popfd
    pop  ebx
    pop  ebp
    ret
  }
}

__declspec(naked) void abi_spawns_guard_traced() {
  __asm {
    push ebp
    mov  ebp, esp
    push ebx
    pushad
    pushfd
    push dword ptr [ebp + 10h]
    push dword ptr [ebp + 0Ch]
    push dword ptr [ebp + 8]
    push dword ptr [ebp + 4]
    call zpatch_ptrace_guard_in
    add  esp, 16
    popfd
    popad
    push dword ptr [ebp + 10h]
    push dword ptr [ebp + 0Ch]
    push dword ptr [ebp + 8]
    call abi_spawns_guard
    mov  ebx, eax
    pushfd
    pushad
    push ebx
    push dword ptr [ebp + 4]
    call zpatch_ptrace_guard_out
    add  esp, 8
    popad
    popfd
    pop  ebx
    pop  ebp
    ret  0Ch
  }
}
#endif

std::uintptr_t hook_bodies_win_resolve(std::uint32_t reference_va, void*) noexcept {
#if defined(ZP_PROBE_TRACE)
  // Diagnostic build: route the FreezeCarFix spawn-probe and guard sites through
  // the logging wrappers (they forward to the real bodies, so behaviour is
  // unchanged). ZolikaPatch.ptrace.log then records every probe/guard call.
  if (reference_va == 0x10037510u)
    return reinterpret_cast<std::uintptr_t>(&abi_spawns_probe_traced);
  if (reference_va == 0x100372F0u)
    return reinterpret_cast<std::uintptr_t>(&abi_spawns_guard_traced);
#endif
  if (g_probe_va != 0u && reference_va == g_probe_va) {
    for (const ProbeBody& probe : kProbeBodies) {
      if (probe.reference_va == reference_va) return probe.probe();
    }
  }
  for (const AbiBody& body : kAbiBodies) {
    if (body.reference_va == reference_va) return body.address();
  }
  return 0;
}

std::size_t hook_bodies_win_count() noexcept {
  return sizeof(kAbiBodies) / sizeof(kAbiBodies[0]);
}

bool hook_bodies_win_set_probe(std::string_view option) noexcept {
  g_probe_va = 0;
  zpatch_probe_target = 0;
  if (option.empty() || option == "0") return false;
  for (const ProbeBody& probe : kProbeBodies) {
    if (option == probe.option) {
      g_probe_va = probe.reference_va;
      zpatch_probe_target = probe.normal();
      log_printf(
          "CrashProbe: armed %s (ref %08X) - forcing the condition at the site; "
          "under the fault-inclusive policy the body must now fault exactly where the "
          "original does (the fault-avoidance guards are removed)",
          probe.option, probe.reference_va);
      return true;
    }
  }
  log_printf("CrashProbe: unknown option '%s' (no probe armed)",
             std::string(option).c_str());
  return false;
}

extern "C" int zpatch_fault_probe(const char* name) noexcept {
  if (name == nullptr) return 1;
  if (std::strcmp(name, "fchs-null-lookup") == 0) {
    // abi_fchs_check at zapatch_model_lookup == 0: the original's
    // 0x1001AD5C `call dword_10159C0C` faults; the guard that returned early
    // is removed.
    zpatch_model_lookup = 0;
    zpatch_fchs_hashes[0] = 1;
    static std::uint8_t object[64] = {};
    zpatch_fchs_param =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(object));
    abi_fchs_check();
    return 1;
  }
  if (std::strcmp(name, "fchs-null-object") == 0) {
    // The original's 0x1001AD62 `movsx eax, word [v2+2Eh]` faults on a null
    // object; the guard that returned early is removed.
    zpatch_fchs_hashes[0] = 1;
    zpatch_fchs_param = 0;
    abi_fchs_check();
    return 1;
  }
  if (std::strcmp(name, "garage-null-lookup") == 0) {
    // sub_10046BD0's 0x10046C10 `call dword_10159C0C` faults; the `if (lookup)`
    // guards are removed.
    zpatch_probe_garage_object[19] = 1;
    zpatch_probe_garage_object[64] = 1;
    zpatch_probe_garage_object[65] = 1;
    zpatch_probe_garage_object[70] = 1;
    zpatch_model_lookup = 0;
    zpatch_garage_lookup_c(zpatch_probe_garage_object);
    return 1;
  }
  if (std::strcmp(name, "gfwl-zero-base") == 0) {
    // sub_10005340's 0x10005428 `VirtualProtect(0+0x66F770, ...)` +
    // 0x1000542D `*v2 = 0x10000E83` fault with xlive.dll absent; the
    // `base != 0` guard and wp_dword's silent-skip are removed.
    zpatch_xlive_base = 0;
    zpatch_flag_gfwl_popup = 1;
    zpatch_flag_patch4_modchecks = 0;
    abi_gfwl_patch();
    return 1;
  }
  if (std::strcmp(name, "focus-loss-null-site") == 0) {
    // sub_1001EDC0 writes its saved focus site unconditionally; a failed prime
    // leaves it null and the original faults. The setter's null check is
    // removed.
    g_focus_loss_site = nullptr;
    g_focus_loss_original = 0;
    abi_menu_focus_set(0, 0);
    return 1;
  }
  if (std::strcmp(name, "newfiles-null-register") == 0) {
    // sub_100515E0 calls dword_10159C08 unconditionally; the guard that
    // returned early is removed (fault=0x100515F3).
    zpatch_newfiles_register_slot = 0;
    abi_newfiles_register();
    return 1;
  }
  if (std::strcmp(name, "ooc-null-logger") == 0) {
    // sub_1004C350 calls dword_10159BF8 unconditionally after the archive is
    // built; the logger==null early return is removed (fault=0x1004C39B).
    zpatch_ooc_logger = 0;
    abi_ooc_wrap(0, 0);
    return 1;
  }
  if (std::strcmp(name, "vehicle-component-null-registry") == 0) {
    // sub_10003F40 dereferences the registry unconditionally; the null checks
    // are removed.
    static_cast<void>(abi_vehicle_component(nullptr, "name"));
    return 1;
  }
  return 1;
}

}  // namespace zpatch

#endif  // _WIN32
