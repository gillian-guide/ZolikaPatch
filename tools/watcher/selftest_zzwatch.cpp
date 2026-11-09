// Offline self-test for ZZWatch's hook mechanics (32-bit console exe).
// Build + run: build_zzwatch_selftest.cmd. Includes zzwatch.cpp so the
// anonymous-namespace internals are visible, then hooks two fake functions
// (one with the 9-byte chain-B body prologue, one with F's 6-byte prologue)
// and verifies: the trampoline executes the stolen prologue, the body returns
// correctly, and the relays record (ret, this, arg) / (ret) with the right
// stack offsets.

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

#include "zzwatch.cpp"

namespace {

uintptr_t alloc_code(const unsigned char* bytes, size_t n) {
  auto* p = static_cast<unsigned char*>(
      VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!p) return 0;
  memcpy(p, bytes, n);
  FlushInstructionCache(GetCurrentProcess(), p, n);
  return reinterpret_cast<uintptr_t>(p);
}

// 55 8B EC 81 EC 18 0A 00 00 | 8B 45 08 | 83 C0 07 | C9 | C2 04 00
// __thiscall(this, int arg) -> arg + 7, ret 4
uintptr_t make_fake_body() {
  static const unsigned char code[] = {
      0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x18, 0x0A, 0x00, 0x00,
      0x8B, 0x45, 0x08, 0x83, 0xC0, 0x07, 0xC9, 0xC2, 0x04, 0x00};
  return alloc_code(code, sizeof(code));
}

// F's real prologue: 55 8B EC 83 E4 F0 (6 bytes) | mov eax, [ebp+8] | leave | ret 4
// __thiscall(this, int arg) -> arg
uintptr_t make_fake_f() {
  static const unsigned char code[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0,
      0x8B, 0x45, 0x08, 0xC9, 0xC2, 0x04, 0x00};
  return alloc_code(code, sizeof(code));
}

uintptr_t g_fn = 0;
int g_result = 0;
uint32_t g_expected_ret = 0;

// Calls g_fn as __thiscall(this = 0x12345678, arg = 35.0f). The return address
// recorded by the relay is exactly `after_call`. The arg is pushed as its
// float bit pattern, like the game sites do.
__declspec(naked) void call_once() {
  __asm {
    mov ecx, 12345678h
    push 42A00000h  // 35.0f
    call dword ptr [g_fn]
  after_call:
    mov g_expected_ret, offset after_call
    mov g_result, eax
    ret
  }
}

int g_failures = 0;
void check(bool ok, const char* what) {
  printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++g_failures;
}

}  // namespace

int main(void) {
  load_ini();
  open_log();
  g_ring = static_cast<FCall*>(VirtualAlloc(
      nullptr, kRingCap * sizeof(FCall), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  check(g_ring != nullptr, "ring allocation");
  LARGE_INTEGER f{}, q{};
  QueryPerformanceFrequency(&f);
  g_freq = f.QuadPart;
  QueryPerformanceCounter(&q);
  g_qpc_start = q.QuadPart;

  // --- chain-B body hook (9-byte steal) -----------------------------------
  const uintptr_t body = make_fake_body();
  check(install_hook_body(body), "install_hook_body");
  g_fn = body;
  const LONG head_before = g_head;
  call_once();
  check(g_result == 0x42A00007, "hooked body returned arg+7 (trampoline replayed the prologue)");
  check(g_head == head_before + 1, "body relay recorded one event");
  if (g_head > head_before) {
    const FCall& rec = g_ring[(g_head - 1) & (kRingCap - 1)];
    check(rec.type == 2, "record type == body");
    check(rec.aux == 0x12345678u, "record this == 0x12345678");
    float arg = 0.0f;
    memcpy(&arg, &rec.arg, sizeof(arg));
    check(rec.ret == g_expected_ret, "record site ret == expected return address");
    printf("  (ret recorded %#010x, expected %#010x)\n", rec.ret, g_expected_ret);
    check(arg == 80.0f, "record arg == 80.0f (0x42A00000 bits passed through)");
    printf("  (arg recorded %.9g)\n", static_cast<double>(arg));
  }

  // --- F hook (6-byte steal) ----------------------------------------------
  const uintptr_t fn = make_fake_f();
  check(install_hook(fn), "install_hook (F)");
  g_fn = fn;
  const LONG head2 = g_head;
  call_once();
  check(g_result == static_cast<int>(0x42A00000u), "hooked F returned the arg unchanged");
  check(g_head == head2 + 1, "F relay recorded one event");
  if (g_head > head2) {
    const FCall& rec = g_ring[(g_head - 1) & (kRingCap - 1)];
    check(rec.type == 1, "record type == F call");
    check(rec.ret == g_expected_ret, "F record ret == expected return address");
  }

  printf(g_failures ? "SELFTEST FAILED (%d)\n" : "SELFTEST OK\n", g_failures);
  return g_failures ? 1 : 0;
}
