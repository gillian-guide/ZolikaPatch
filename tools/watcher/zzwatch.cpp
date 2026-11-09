// ZZWatch.asi - read-only decision-stream observer for the CutsceneFixes
// chain-B hook. NOT part of the shipped mod.
//
// Usage: drop this file next to ZolikaPatch.asi (either the original or this
// reimplementation) in the game directory, run the game, play a cutscene, then
// read ZZWatch.log (written next to the ASI).
//
// It reads the mod's own published globals (ZolikaPatch.asi base + the
// ZZWatch.ini offsets; the defaults are the original 7.6.5.0's):
//
//   target_slot_offset  0x15BDE4  dword = the game camera function F
//   poke_slot_offset    0x15BDF0  dword = F + 0x4F (the poked immediate lives
//                                          at F + 0x4E: chain B writes 1 in
//                                          phase 1 and 0 in phase 2)
//   accum_offset        0x15BDE8  qword double = the chain-B frame-time
//                                 accumulator (BSS, starts at 0; the drift
//                                 restore resets it)
//
// Then it installs an entry hook on F (a 6-byte steal - F starts with
// push ebp; mov ebp, esp; and esp, 0FFFFFFF0h) whose relay records, for every
// F call: the QueryPerformanceCounter timestamp, the return address (which
// identifies the caller: the two patched game sites' returns, the body's own
// phase-1/phase-2 calls, or any other game caller), the poke byte at entry and
// the accumulator. The records go to a lock-free ring; the watcher's own
// thread drains it to ZZWatch.log. If the hook cannot be installed it falls
// back to spin-sampling the poke byte and the accumulator (transitions only).
//
// It never writes to the game's meaningful state (the hook trampoline is the
// only code patch; the relay preserves all registers/flags/x87 state).

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

namespace {

HMODULE g_self = nullptr;
FILE* g_log = nullptr;
long g_lines = 0;
long g_max_lines = 400000;
long g_max_seconds = 600;
uint32_t g_target_offset = 0;  // 0 = auto-detect below
uint32_t g_poke_offset = 0;
uint32_t g_accum_offset = 0;
int64_t g_freq = 0;

// Known slot layouts: the original 7.6.5.0 and our reimplementation build
// (rebased_values.cpp link map, 2026-10-14). Auto-detection validates the
// target against the main module and checks poke == target + 0x4F.
struct SlotSet {
  const char* name;
  uint32_t target;
  uint32_t poke;
  uint32_t accum;
};
const SlotSet kSlotSets[] = {
    {"original", 0x15BDE4u, 0x15BDF0u, 0x15BDE8u},
    {"ours", 0x16A6B0u, 0x16A6B4u, 0x16A6B8u},   // 2026-10-20 map
};

volatile unsigned char* g_poke = nullptr;
volatile double* g_accum = nullptr;

// Layout-independent fallback: scan the module's writable data for the slot
// triple (target game pointer in the main module whose first byte is 0x55,
// poke dword at +4 == target + 0x4F, accumulator at +8). The fixed offsets above
// only exist so the log can name the layout; a rebuild shifts them.
struct SlotScan {
  uint32_t offset;
  uint32_t target;
  uint32_t poke;
};

SlotScan scan_target_slot(unsigned char* zp, uintptr_t mod_base, uintptr_t mod_end) {
  SlotScan none{0, 0, 0};
  if (!zp) return none;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(zp);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) return none;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(zp + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return none;
  const uint32_t size = nt->OptionalHeader.SizeOfImage;
  for (uint32_t off = 0x1000; off + 0x10 <= size; off += 4) {
    uint32_t target = 0, poke = 0;
    memcpy(&target, zp + off, 4);
    if (target < mod_base || target >= mod_end) continue;
    memcpy(&poke, zp + off + 4, 4);
    if (poke != target + 0x4F) continue;
    if (*reinterpret_cast<const unsigned char*>(target) != 0x55) continue;
    const unsigned char poke_byte =
        *reinterpret_cast<const unsigned char*>(target + 0x4E);
    if (poke_byte > 1) continue;
    return SlotScan{off, target, poke};
  }
  return none;
}

// --- event ring -------------------------------------------------------------
const uint32_t kRingCap = 262144;
struct FCall {
  int64_t t;       // QPC ticks since start
  double accum;
  uint32_t ret;    // F call: caller; body entry: the patched site's return
  uint32_t aux;    // body entry: `this` (the camera object)
  float arg;       // body entry: the site's float argument
  float f[6];      // body entry: this+0x20/+0x24/+0x28/+0x40/+0x44/+0x48
  uint8_t type;    // 1 = F call, 2 = body entry
  uint8_t poke;
  uint8_t pad[2];
};
FCall* g_ring = nullptr;
volatile LONG g_head = 0;   // producer
volatile LONG g_tail = 0;   // consumer
volatile LONG g_dropped = 0;
volatile int64_t g_qpc_start = 0;

void log_line(const char* fmt, ...) {
  if (!g_log || g_lines >= g_max_lines) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(g_log, fmt, ap);
  va_end(ap);
  fputc('\n', g_log);
  ++g_lines;
  if ((g_lines & 0xFF) == 0) fflush(g_log);
}

// Reserve -> fill -> publish (single logical producer thread; the F and body
// relays only ever run on the game's camera thread). Built /arch:IA32 so no
// SSE is emitted; x87 use is balanced.
void ring_push(uint8_t type, uint32_t ret, uint32_t aux, uint32_t arg_bits,
               uint8_t poke, const float* floats) {
  FCall* ring = g_ring;
  if (!ring) return;
  const LONG idx = g_head;
  if (idx - g_tail >= static_cast<LONG>(kRingCap)) {
    InterlockedIncrement(&g_dropped);
    return;
  }
  FCall& rec = ring[idx & (kRingCap - 1)];
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  rec.t = now.QuadPart - g_qpc_start;
  rec.accum = g_accum ? *g_accum : 0.0;
  rec.ret = ret;
  rec.aux = aux;
  memcpy(&rec.arg, &arg_bits, sizeof(rec.arg));
  for (int i = 0; i < 6; ++i) rec.f[i] = floats ? floats[i] : 0.0f;
  rec.type = type;
  rec.poke = poke;
  InterlockedExchange(&g_head, idx + 1);
}

// Called from the F-entry relay (x86 cdecl).
extern "C" void zzwatch_record(uint32_t return_address) {
  ring_push(1, return_address, 0, 0, g_poke ? *g_poke : 0xFF, nullptr);
}

// Called from the body-entry relay: (uint32_t site_ret, uint32_t this_, uint32_t arg_bits).
extern "C" void zzwatch_record_body(uint32_t site_ret, uint32_t this_,
                                    uint32_t arg_bits) {
  float floats[6] = {};
  if (this_) {
    const auto* base = reinterpret_cast<const unsigned char*>(this_);
    const uint32_t offsets[6] = {0x20, 0x24, 0x28, 0x40, 0x44, 0x48};
    for (int i = 0; i < 6; ++i) {
      __try {
        memcpy(&floats[i], base + offsets[i], sizeof(float));
      } __except (EXCEPTION_EXECUTE_HANDLER) {
        floats[i] = 0.0f;
      }
    }
  }
  ring_push(2, site_ret, this_, arg_bits, g_poke ? *g_poke : 0xFF, floats);
}

// The relays: preserve everything, record, replay the stolen bytes via the
// trampoline, resume.
extern "C" void* g_trampoline = nullptr;
extern "C" void* g_trampoline_body = nullptr;

__declspec(naked) void zzwatch_relay_impl() {
  __asm {
    pushad
    pushfd
    push dword ptr [esp + 36]  // return address (pushad 32 + pushfd 4)
    call zzwatch_record
    add esp, 4
    popfd
    popad
    jmp dword ptr [g_trampoline]
  }
}

__declspec(naked) void zzwatch_relay_body() {
  __asm {
    pushad
    pushfd
    // Stack after pushad+pushfd: [esp+36]=site ret, [esp+40]=float arg.
    // pushad saved ecx (this) at [esp+24+4]; but ecx is still live - use it.
    push dword ptr [esp + 40]  // arg bits
    push ecx                   // this (live; pushad preserved it)
    push dword ptr [esp + 44]  // site return address
    call zzwatch_record_body
    add esp, 12
    popfd
    popad
    jmp dword ptr [g_trampoline_body]
  }
}

void load_ini(void) {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(g_self, path, MAX_PATH);
  char* slash = strrchr(path, '\\');
  if (!slash) return;
  *slash = 0;
  char ini[MAX_PATH];
  _snprintf_s(ini, sizeof(ini), _TRUNCATE, "%s\\ZZWatch.ini", path);
  // 0 keeps auto-detection; explicit values override it.
  g_target_offset = GetPrivateProfileIntA("ZZWatch", "target_slot_offset", 0, ini);
  g_poke_offset = GetPrivateProfileIntA("ZZWatch", "poke_slot_offset", 0, ini);
  g_accum_offset = GetPrivateProfileIntA("ZZWatch", "accum_offset", 0, ini);
  g_max_lines = GetPrivateProfileIntA("ZZWatch", "max_lines", 400000, ini);
  g_max_seconds = GetPrivateProfileIntA("ZZWatch", "max_seconds", 600, ini);
}

void open_log(void) {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(g_self, path, MAX_PATH);
  char* slash = strrchr(path, '\\');
  if (!slash) return;
  *slash = 0;
  SYSTEMTIME st{};
  GetLocalTime(&st);
  char log[MAX_PATH];
  _snprintf_s(log, sizeof(log), _TRUNCATE, "%s\\ZZWatch_%04u%02u%02u_%02u%02u%02u.log",
              path, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
  fopen_s(&g_log, log, "w");
}

bool main_module_range(uintptr_t& base, uintptr_t& end) {
  HMODULE mod = GetModuleHandleA(nullptr);
  if (!mod) return false;
  const auto* dos = reinterpret_cast<const unsigned char*>(mod);
  if (dos[0] != 'M' || dos[1] != 'Z') return false;
  const uint32_t pe = *reinterpret_cast<const uint32_t*>(dos + 0x3C);
  const auto* nt = dos + pe;
  if (nt[0] != 'P' || nt[1] != 'E') return false;
  const uint32_t size = *reinterpret_cast<const uint32_t*>(nt + 0x50);
  base = reinterpret_cast<uintptr_t>(dos);
  end = base + size;
  return size > 0x1000;
}

bool install_hook(uintptr_t f) {
  auto* code = reinterpret_cast<unsigned char*>(f);
  if (code[0] != 0x55 || code[1] != 0x8B || code[2] != 0xEC) return false;
  auto* tramp = static_cast<unsigned char*>(
      VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!tramp) return false;
  for (int i = 0; i < 6; ++i) tramp[i] = code[i];
  const intptr_t back = reinterpret_cast<intptr_t>(code + 6) -
                        (reinterpret_cast<intptr_t>(tramp) + 6 + 5);
  tramp[6] = 0xE9;
  *reinterpret_cast<int32_t*>(tramp + 7) = static_cast<int32_t>(back);
  g_trampoline = tramp;

  DWORD old = 0;
  if (!VirtualProtect(code, 6, PAGE_EXECUTE_READWRITE, &old)) return false;
  const intptr_t rel = reinterpret_cast<intptr_t>(&zzwatch_relay_impl) -
                       (reinterpret_cast<intptr_t>(code) + 5);
  code[0] = 0xE9;
  *reinterpret_cast<int32_t*>(code + 1) = static_cast<int32_t>(rel);
  code[5] = 0x90;
  VirtualProtect(code, 6, old, &old);
  FlushInstructionCache(GetCurrentProcess(), code, 6);
  return true;
}

// The chain-B body's entry (both the original and our build): 9 bytes
//   55 8B EC                push ebp; mov ebp, esp
//   81 EC 18 0A 00 00       sub esp, 0A18h
static const unsigned char kBodyPrologue[9] = {0x55, 0x8B, 0xEC, 0x81, 0xEC,
                                               0x18, 0x0A, 0x00, 0x00};

// Locate the chain-B body by decoding the PATCHED call at scan-2's site
// (pattern B: 8B CF 88 44 24 0F E8 rel32 A1). The patched call is 5 bytes
// BEFORE the pattern start: its rel32 sits at match-4. Works for both mods:
// whichever ASI is loaded patched the site to point at its own body. Retries
// until the patch is visible (the mod's loader may run after us); before the
// patch the target is F and the body-prologue check fails.
uintptr_t find_body(uintptr_t mod_base, uintptr_t mod_end) {
  static const unsigned char kPatB[6] = {0x8B, 0xCF, 0x88, 0x44, 0x24, 0x0F};
  for (int attempt = 0; attempt < 300; ++attempt) {
    const auto* found = static_cast<const unsigned char*>(nullptr);
    for (size_t pos = 16; pos + 16 < mod_end - mod_base; ++pos) {
      const auto* p = reinterpret_cast<const unsigned char*>(mod_base + pos);
      if (p[0] != kPatB[0] || memcmp(p, kPatB, 6) != 0) continue;
      if (p[6] == 0xE8 && p[11] == 0xA1) found = p;
      break;  // pattern is unique; stop either way
    }
    if (!found) return 0;  // pattern not in the image at all
    const int32_t rel = *reinterpret_cast<const int32_t*>(found - 4);
    const uintptr_t body = reinterpret_cast<uintptr_t>(found) + rel;
    if (memcmp(reinterpret_cast<const unsigned char*>(body), kBodyPrologue, 9) == 0)
      return body;
    Sleep(100);  // site found but not patched yet (still points at F)
  }
  return 0;
}

bool install_hook_body(uintptr_t body) {
  auto* code = reinterpret_cast<unsigned char*>(body);
  if (memcmp(code, kBodyPrologue, 9) != 0) return false;
  auto* tramp = static_cast<unsigned char*>(
      VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
  if (!tramp) return false;
  for (int i = 0; i < 9; ++i) tramp[i] = code[i];
  const intptr_t back = reinterpret_cast<intptr_t>(code + 9) -
                        (reinterpret_cast<intptr_t>(tramp) + 9 + 5);
  tramp[9] = 0xE9;
  *reinterpret_cast<int32_t*>(tramp + 10) = static_cast<int32_t>(back);
  g_trampoline_body = tramp;

  DWORD old = 0;
  if (!VirtualProtect(code, 9, PAGE_EXECUTE_READWRITE, &old)) return false;
  const intptr_t rel = reinterpret_cast<intptr_t>(&zzwatch_relay_body) -
                       (reinterpret_cast<intptr_t>(code) + 5);
  code[0] = 0xE9;
  *reinterpret_cast<int32_t*>(code + 1) = static_cast<int32_t>(rel);
  code[5] = 0x90;
  code[6] = 0x90;
  code[7] = 0x90;
  code[8] = 0x90;
  VirtualProtect(code, 9, old, &old);
  FlushInstructionCache(GetCurrentProcess(), code, 9);
  return true;
}

void drain_ring(bool final_flush) {
  static char buf[1 << 16];
  size_t used = 0;
  for (;;) {
    const LONG tail = g_tail;
    if (tail >= g_head) break;
    const FCall& rec = g_ring[tail & (kRingCap - 1)];
    const double t = static_cast<double>(rec.t) / static_cast<double>(g_freq);
    int n = 0;
    if (rec.type == 2) {
      n = _snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
                      "B t=%.6f site=%#010x this=%#010x arg=%.9g poke=%u accum=%.17g "
                      "f=[%.9g %.9g %.9g %.9g %.9g %.9g]\n",
                      t, rec.ret, rec.aux, static_cast<double>(rec.arg),
                      static_cast<unsigned>(rec.poke), rec.accum,
                      static_cast<double>(rec.f[0]), static_cast<double>(rec.f[1]),
                      static_cast<double>(rec.f[2]), static_cast<double>(rec.f[3]),
                      static_cast<double>(rec.f[4]), static_cast<double>(rec.f[5]));
    } else {
      n = _snprintf_s(buf + used, sizeof(buf) - used, _TRUNCATE,
                      "F t=%.6f ret=%#010x poke=%u accum=%.17g\n", t, rec.ret,
                      static_cast<unsigned>(rec.poke), rec.accum);
    }
    if (n > 0) used += static_cast<size_t>(n);
    if (used > sizeof(buf) - 256) {
      fwrite(buf, 1, used, g_log);
      used = 0;
      if (final_flush) fflush(g_log);
    }
    g_tail = tail + 1;
    ++g_lines;
    if (g_lines >= g_max_lines) break;
  }
  if (used) {
    fwrite(buf, 1, used, g_log);
    if (final_flush) fflush(g_log);
  }
}

// Compare the loaded main module's executable code against the on-disk exe and
// log byte runs that differ. The mods patch only code (E8/E9/EB/NOP writes),
// so any differing run is an installed patch - chain A's EB write included
// (it lands during load and its bookkeeping slots are self-reset afterwards,
// which is why the slot probe above reads zeros).
void report_text_diff(HMODULE mod) {
  char path[MAX_PATH] = {};
  GetModuleFileNameA(g_self, path, MAX_PATH);
  char* slash = strrchr(path, '\\');
  if (!slash) return;
  *slash = 0;
  char exe_path[MAX_PATH];
  _snprintf_s(exe_path, sizeof(exe_path), _TRUNCATE, "%s\\GTAIV.exe", path);
  FILE* exe = nullptr;
  fopen_s(&exe, exe_path, "rb");
  if (!exe) {
    log_line("code-diff: cannot open %s", exe_path);
    return;
  }
  unsigned char hdr[0x600] = {};
  if (fread(hdr, 1, sizeof(hdr), exe) != sizeof(hdr) || hdr[0] != 'M' || hdr[1] != 'Z') {
    log_line("code-diff: bad exe header");
    fclose(exe);
    return;
  }
  const uint32_t pe = *reinterpret_cast<const uint32_t*>(hdr + 0x3C);
  const auto* nt = hdr + pe;
  const uint16_t nsec = *reinterpret_cast<const uint16_t*>(nt + 6);
  const uint16_t opt_size = *reinterpret_cast<const uint16_t*>(nt + 20);
  const auto* opt = nt + 24;
  const uint32_t file_ib = *reinterpret_cast<const uint32_t*>(opt + 28);
  const auto* sec = opt + opt_size;
  const auto* mem = reinterpret_cast<const unsigned char*>(mod);
  int total_runs = 0;
  for (int i = 0; i < nsec; ++i) {
    const auto* s = sec + i * 40;
    char name[9] = {};
    memcpy(name, s, 8);
    if (strcmp(name, ".text") != 0) continue;
    const uint32_t va = *reinterpret_cast<const uint32_t*>(s + 12);
    const uint32_t rawsize = *reinterpret_cast<const uint32_t*>(s + 16);
    const uint32_t rawptr = *reinterpret_cast<const uint32_t*>(s + 20);
    if (_fseeki64(exe, rawptr, SEEK_SET) != 0) continue;
    static unsigned char buf[0x10000];
    // Skip the DRM/entry stub at the start of .text (the SecuROM-wrapped exe
    // stores it encrypted; memory shows the decrypted code, producing hundreds
    // of false diffs). No mod patches live below 0x30000 into .text.
    const uint32_t skip = 0x1000 < rawsize ? 0x1000 : 0;
    uint32_t done = 0;
    uint32_t run_start = 0;
    bool in_run = false;
    uint32_t run_src = 0;
    unsigned char run_file_byte = 0, run_mem_byte = 0;
    while (done < rawsize) {
      const uint32_t chunk = rawsize - done > sizeof(buf) ? sizeof(buf) : rawsize - done;
      if (fread(buf, 1, chunk, exe) != chunk) break;
      for (uint32_t j = 0; j < chunk; ++j) {
        const uint32_t mem_va = done + j;
        if (mem_va < skip) continue;
        const bool differs = mem[va + mem_va] != buf[j];
        if (differs && !in_run) {
          in_run = true;
          run_start = mem_va;
          run_src = file_ib + va + mem_va;
          run_file_byte = buf[j];
          run_mem_byte = mem[va + mem_va];
        } else if (!differs && in_run) {
          in_run = false;
          if (total_runs < 200000) {
            log_line("code-diff VA %#010x len %u file=%02x mem=%02x",
                     run_src, mem_va - run_start, run_file_byte, run_mem_byte);
          }
          ++total_runs;
        }
      }
      done += chunk;
    }
    if (in_run && total_runs < 200000) {
      log_line("code-diff VA %#010x len %u (end of text) file=%02x mem=%02x",
               file_ib + va + run_start, rawsize - run_start, run_file_byte, run_mem_byte);
    }
    if (in_run) ++total_runs;
  }
  log_line("code-diff done: %d differing run(s)", total_runs);
  fclose(exe);
}

DWORD WINAPI watch_thread(LPVOID) {
  HMODULE zp = nullptr;
  uint32_t target = 0, poke_plus = 0;
  uintptr_t mod_base = 0, mod_end = 0;
  const char* chosen = "ini";
  for (int i = 0; i < 900; ++i) {
    if (!zp) zp = GetModuleHandleA("ZolikaPatch.asi");
    if (zp && main_module_range(mod_base, mod_end)) {
      if (g_target_offset) {
        target = *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<unsigned char*>(zp) + g_target_offset);
        poke_plus = *reinterpret_cast<const uint32_t*>(
            reinterpret_cast<unsigned char*>(zp) + g_poke_offset);
        if (!(target >= mod_base && target < mod_end &&
              poke_plus == target + 0x4F && *reinterpret_cast<const unsigned char*>(target) == 0x55))
          target = 0;
      } else {
        for (const SlotSet& set : kSlotSets) {
          const uint32_t t = *reinterpret_cast<const uint32_t*>(
              reinterpret_cast<unsigned char*>(zp) + set.target);
          const uint32_t p = *reinterpret_cast<const uint32_t*>(
              reinterpret_cast<unsigned char*>(zp) + set.poke);
          if (t >= mod_base && t < mod_end && p == t + 0x4F &&
              *reinterpret_cast<const unsigned char*>(t) == 0x55) {
            target = t;
            poke_plus = p;
            g_target_offset = set.target;
            g_poke_offset = set.poke;
            g_accum_offset = set.accum;
            chosen = set.name;
            break;
          }
        }
        if (!target) {
          const SlotScan found = scan_target_slot(
              reinterpret_cast<unsigned char*>(zp), mod_base, mod_end);
          if (found.target) {
            target = found.target;
            poke_plus = found.poke;
            g_target_offset = found.offset;
            g_poke_offset = found.offset + 4;
            g_accum_offset = found.offset + 8;
            chosen = "scan";
          }
        }
      }
      if (target) break;
    }
    Sleep(100);
  }
  if (!target) {
    log_line("ZolikaPatch.asi target slot never resolved (ini offsets %#x/%#x/%#x)",
             g_target_offset, g_poke_offset, g_accum_offset);
    return 0;
  }
  g_accum = reinterpret_cast<volatile double*>(
      reinterpret_cast<unsigned char*>(zp) + g_accum_offset);
  g_poke = reinterpret_cast<volatile unsigned char*>(poke_plus - 1);
  LARGE_INTEGER freq{}, qpc{};
  QueryPerformanceFrequency(&freq);
  g_freq = freq.QuadPart;
  QueryPerformanceCounter(&qpc);
  g_qpc_start = qpc.QuadPart;
  log_line("module=%p layout=%s F=%#x (prologue %02x %02x %02x) poke=%p accum=%p accum_now=%.17g",
           zp, chosen, target,
           reinterpret_cast<const unsigned char*>(target)[0],
           reinterpret_cast<const unsigned char*>(target)[1],
           reinterpret_cast<const unsigned char*>(target)[2],
           const_cast<unsigned char*>(g_poke), const_cast<double*>(g_accum),
           static_cast<double>(*g_accum));

  // Chain A (CutsceneFixes' first stage): the original's sub_1003DA50 worker
  // publishes a patch target into 0x10185F88 and the loader's callback writes
  // EB there. Read the three chain-A globals and the published target's bytes.
  // Everything is SEH-guarded: the offsets are for the original 7.6.5.0 build,
  // so a different binary (or a bad slot value) must not take the game down.
  if (strcmp(chosen, "original") == 0) {
    const auto* base = reinterpret_cast<const unsigned char*>(zp);
    uint32_t fn_slot = 0, second = 0, patch_target = 0;
    unsigned char tbytes[16] = {};
    unsigned char fbytes[16] = {};
    __try {
      fn_slot = *reinterpret_cast<const uint32_t*>(base + 0x189EE0);
      second = *reinterpret_cast<const uint32_t*>(base + 0x18C22C);
      patch_target = *reinterpret_cast<const uint32_t*>(base + 0x185F88);
      if (patch_target) {
        memcpy(tbytes, reinterpret_cast<const void*>(static_cast<uintptr_t>(patch_target)), 16);
        memcpy(fbytes, reinterpret_cast<const void*>(static_cast<uintptr_t>(target) + 0x1F8), 16);
      }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
      log_line("chainA probe faulted (SEH caught): fn=%#x second=%#x target=%#x",
               fn_slot, second, patch_target);
    }
    log_line("chainA fn_slot(10189EE0)=%#x second(1018C22C)=%#x patch_slot(10185F88)=%#x",
             fn_slot, second, patch_target);
    if (patch_target) {
      log_line("chainA target %#x bytes: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x",
               patch_target, tbytes[0], tbytes[1], tbytes[2], tbytes[3], tbytes[4],
               tbytes[5], tbytes[6], tbytes[7], tbytes[8], tbytes[9], tbytes[10],
               tbytes[11], tbytes[12], tbytes[13], tbytes[14], tbytes[15]);
      log_line("F+0x1F8: %02x %02x %02x %02x %02x %02x %02x %02x  %02x %02x %02x %02x %02x %02x %02x %02x",
               fbytes[0], fbytes[1], fbytes[2], fbytes[3], fbytes[4], fbytes[5],
               fbytes[6], fbytes[7], fbytes[8], fbytes[9], fbytes[10], fbytes[11],
               fbytes[12], fbytes[13], fbytes[14], fbytes[15]);
    }
  }

  // Give the ASI loader a moment to finish every mod's load-time patch pass,
  // then diff the loaded code against the on-disk exe.
  Sleep(500);
  report_text_diff(GetModuleHandleA(nullptr));

  g_ring = static_cast<FCall*>(VirtualAlloc(nullptr,
      kRingCap * sizeof(FCall), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  if (!g_ring) {
    log_line("ring allocation failed");
    return 0;
  }
  const bool hooked = install_hook(target);
  log_line(hooked ? "hooked F entry (recording every call)"
                  : "hook install failed; falling back to poke/accum sampling");
  uintptr_t body = 0;
  bool body_hooked = false;
  if (hooked) {
    body = find_body(mod_base, mod_end);
    if (body) {
      body_hooked = install_hook_body(body);
      log_line(body_hooked ? "hooked body entry (recording site/this/arg)"
                           : "body hook install failed");
    } else {
      log_line("body not found via scan-2 site decode");
    }
  }
  if (!hooked) {
    unsigned char last_poke = *g_poke;
    double last_accum = *g_accum;
    unsigned spin = 0;
    for (;;) {
      LARGE_INTEGER now{};
      QueryPerformanceCounter(&now);
      const double t = static_cast<double>(now.QuadPart - g_qpc_start) /
                       static_cast<double>(g_freq);
      if (t > static_cast<double>(g_max_seconds) || g_lines >= g_max_lines) break;
      const unsigned char p = *g_poke;
      if (p != last_poke) {
        last_poke = p;
        log_line("t=%.6f poke=%u accum=%.17g", t, static_cast<unsigned>(p),
                 static_cast<double>(*g_accum));
      } else {
        const double a = *g_accum;
        if (a != last_accum) {
          last_accum = a;
          log_line("t=%.6f poke=%u accum=%.17g%s", t, static_cast<unsigned>(p), a,
                   a == 0.0 ? " RESET" : "");
        }
      }
      if ((++spin & 0x7FF) == 0) SwitchToThread();
    }
    fflush(g_log);
    return 0;
  }
  for (;;) {
    Sleep(50);
    drain_ring(false);
    fflush(g_log);
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const double t = static_cast<double>(now.QuadPart - g_qpc_start) /
                     static_cast<double>(g_freq);
    if (t > static_cast<double>(g_max_seconds) || g_lines >= g_max_lines) break;
  }
  drain_ring(true);
  if (g_dropped) log_line("dropped=%ld", g_dropped);
  log_line("stop lines=%ld", g_lines);
  fflush(g_log);
  return 0;
}

}  // namespace

// Some ASI loaders are picky about DLLs without exports; this is inert.
extern "C" __declspec(dllexport) void ZZWatchInit() {}

BOOL WINAPI DllMain(HMODULE self, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(self);
    g_self = self;
    load_ini();
    open_log();
    if (g_log) {
      HANDLE th = CreateThread(nullptr, 0, watch_thread, nullptr, 0, nullptr);
      if (th) CloseHandle(th);
    }
  }
  return TRUE;
}
