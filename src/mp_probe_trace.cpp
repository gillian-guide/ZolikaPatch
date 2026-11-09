// Temporary diagnostics for the FreezeCarFix / spawn-probe investigation.
//
// Built only when ZP_PROBE_TRACE is defined (tools/build_asi_probe_trace.cmd).
// The shipped build never compiles the resolver redirect below, so the real
// abi_spawns_probe / abi_spawns_guard bodies and the installed bytes stay
// untouched.  The wrappers here log, per call:
//
//   * which game site called in (the return address, as an RVA),
//   * the argument the site pushed,
//   * the `kind`/`class`/`skip` state the body will consume,
//   * the body's return value and the `skip`/`kind` state after it ran.
//
// That is exactly the decision the probe makes (class-5 test, then the skip
// branch), so one live GTAC session shows which classification diverges.
//
// Output: ZolikaPatch.ptrace.log in the game directory (same place as
// ZolikaPatch.log), one line per event, flushed per line so a crash cannot
// lose it.

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <windows.h>

#include "zpatch/rebased_values.hpp"

// The spawn-probe state the traced bodies consume (declared inside namespace
// zpatch with C linkage, so the names need qualifying here).
using zpatch::zpatch_model_lookup;
using zpatch::zpatch_spawns_case0_resume;
using zpatch::zpatch_spawns_class;
using zpatch::zpatch_spawns_kind;
using zpatch::zpatch_spawns_orig_a;
using zpatch::zpatch_spawns_orig_b;
using zpatch::zpatch_spawns_skip;

// The logging entry points are called from the traced wrappers in
// hook_bodies_win.cpp (the real bodies live in that file's anonymous namespace,
// so the wrappers must be there too).

namespace {

std::FILE* g_file = nullptr;
std::uint32_t g_base = 0;
unsigned long g_lines = 0;
constexpr unsigned long kMaxLines = 400000ul;

std::FILE* trace_file() noexcept {
  if (g_file != nullptr) return g_file;
  g_base = static_cast<std::uint32_t>(
      reinterpret_cast<std::uintptr_t>(::GetModuleHandleA(nullptr)));
  g_file = std::fopen("ZolikaPatch.ptrace.log", "w");
  if (g_file != nullptr) {
    std::fprintf(g_file,
                 "# ZolikaPatch probe trace (diagnostic build)\n"
                 "# base=%08X\n"
                 "# PIN/POUT  probe body entry/exit; GIN/GOUT guard body entry/exit\n"
                 "# ret = caller return address, rva = ret - module base\n",
                 g_base);
    std::fflush(g_file);
  }
  return g_file;
}

unsigned long bump() noexcept {
  ++g_lines;
  return g_lines;
}

}  // namespace

extern "C" void zpatch_ptrace_probe_in(std::uint32_t ret_addr,
                                       std::uint32_t arg) noexcept {
  std::FILE* f = trace_file();
  if (f == nullptr || bump() > kMaxLines) return;
  const std::uint32_t rva = ret_addr - g_base;
  // The game's model-hash map that sub_8DD330/sub_8DD2D0 read: the "built"
  // one-shot flag, the entry count (word) and the map pointer.
  const auto* map_state = reinterpret_cast<const unsigned char*>(
      static_cast<std::uintptr_t>(g_base) + 0xB3E9D8u);
  const unsigned built = map_state[0];
  const unsigned count =
      *reinterpret_cast<const std::uint16_t*>(map_state + 8);
  const unsigned capacity =
      *reinterpret_cast<const std::uint16_t*>(map_state + 10);
  const std::uint32_t ptr =
      *reinterpret_cast<const std::uint32_t*>(map_state + 4);
  const std::uint32_t ped_counter = *reinterpret_cast<const std::uint32_t*>(
      static_cast<std::uintptr_t>(g_base) + 0xE2C15Cu);

  // Replicate sub_8DD330's lookup against the live map so the log answers the
  // question the probe itself cannot: is `arg` in the map, at which slot, with
  // which value, and is the array sorted the way bsearch expects?
  int found_slot = -1;
  unsigned found_value = 0;
  unsigned first_hash = 0, first_value = 0, last_hash = 0, last_value = 0;
  int sorted = -1;
  unsigned unreadable = 0;
  if (ptr != 0 && count != 0) {
    const auto span = static_cast<SIZE_T>(count) * 8u;
    MEMORY_BASIC_INFORMATION first_info{};
    MEMORY_BASIC_INFORMATION last_info{};
    const auto start_va = static_cast<std::uintptr_t>(ptr);
    const auto end_va = start_va + span - 1u;
    const auto readable = [](const MEMORY_BASIC_INFORMATION& info) {
      return info.State == MEM_COMMIT && (info.Protect & PAGE_NOACCESS) == 0 &&
             (info.Protect & PAGE_GUARD) == 0;
    };
    if (::VirtualQuery(reinterpret_cast<const void*>(start_va), &first_info,
                       sizeof(first_info)) == 0 ||
        !readable(first_info) ||
        ::VirtualQuery(reinterpret_cast<const void*>(end_va), &last_info,
                       sizeof(last_info)) == 0 ||
        !readable(last_info)) {
      unreadable = 1;
    } else {
      const auto* entries = reinterpret_cast<const std::uint32_t*>(
          static_cast<std::uintptr_t>(ptr));
      // Snapshot the live hash->index map once per distinct size.  The original
      // ASI's map can be captured the same way with an x64dbg `savedata`, so the
      // two snapshots can be diffed entry-for-entry.
      static bool first_snapshot = true;
      static unsigned last_count = 0xFFFFFFFFu;
      static unsigned snapshot_count = 0;
      if (snapshot_count < 32u && count != last_count) {
        last_count = count;
        ++snapshot_count;
        std::FILE* m = std::fopen("ZolikaPatch.ptrace.map",
                                  first_snapshot ? "wb" : "ab");
        first_snapshot = false;
        if (m != nullptr) {
          const std::uint32_t header[8] = {0x5A504D50u, g_base, built, count,
                                           capacity, ptr, ped_counter, 0u};
          std::fwrite(header, sizeof(header), 1, m);
          std::fwrite(entries, 8u, count, m);
          std::fclose(m);
        }
      }
      first_hash = entries[0];
      first_value = entries[1];
      last_hash = entries[(count - 1u) * 2u];
      last_value = entries[(count - 1u) * 2u + 1u];
      bool ok = true;
      for (unsigned i = 0; i < count; ++i) {
        const auto hash = static_cast<std::int32_t>(entries[i * 2u]);
        const auto value = entries[i * 2u + 1u];
        if (hash == static_cast<std::int32_t>(arg) && found_slot < 0) {
          found_slot = static_cast<int>(i);
          found_value = value;
        }
        if (i != 0) {
          const auto prev = static_cast<std::int32_t>(entries[(i - 1u) * 2u]);
          if (prev > hash) ok = false;
        }
      }
      sorted = ok ? 1 : 0;
    }
  }
  std::fprintf(f,
               "PIN  ret=%08X rva=%08X arg=%08X kind=%08X class=%08X skip=%u "
               "orig_a=%08X lookup=%08X map_built=%u map_count=%u map_cap=%u "
               "map_ptr=%08X ped_count=%u map_sorted=%d map_found=%d "
               "map_value=%08X map_unreadable=%u first=%08X:%08X last=%08X:%08X\n",
               ret_addr, rva, arg, zpatch_spawns_kind, zpatch_spawns_class,
               zpatch_spawns_skip, zpatch_spawns_orig_a, zpatch_model_lookup,
               built, count, capacity, ptr, ped_counter, sorted, found_slot,
               found_value, unreadable, first_hash, first_value, last_hash,
               last_value);
  std::fflush(f);
}

extern "C" void zpatch_ptrace_probe_out(std::uint32_t ret_addr, std::uint32_t arg,
                                        std::uint32_t result) noexcept {
  std::FILE* f = trace_file();
  if (f == nullptr || bump() > kMaxLines) return;
  std::fprintf(f,
               "POUT ret=%08X rva=%08X arg=%08X result=%08X kind=%08X class=%08X skip=%u\n",
               ret_addr, ret_addr - g_base, arg, result, zpatch_spawns_kind,
               zpatch_spawns_class, zpatch_spawns_skip);
  std::fflush(f);
}

extern "C" void zpatch_ptrace_guard_in(std::uint32_t ret_addr, std::uint32_t a1,
                                       std::uint32_t a2, std::uint32_t a3) noexcept {
  std::FILE* f = trace_file();
  if (f == nullptr || bump() > kMaxLines) return;
  std::fprintf(f, "GIN  ret=%08X rva=%08X a1=%08X a2=%08X a3=%08X skip=%u\n",
               ret_addr, ret_addr - g_base, a1, a2, a3, zpatch_spawns_skip);
  std::fflush(f);
}

extern "C" void zpatch_ptrace_guard_out(std::uint32_t ret_addr,
                                        std::uint32_t result) noexcept {
  std::FILE* f = trace_file();
  if (f == nullptr || bump() > kMaxLines) return;
  std::fprintf(f, "GOUT ret=%08X rva=%08X result=%08X skip=%u kind=%08X\n",
               ret_addr, ret_addr - g_base, result, zpatch_spawns_skip,
               zpatch_spawns_kind);
  std::fflush(f);
}
