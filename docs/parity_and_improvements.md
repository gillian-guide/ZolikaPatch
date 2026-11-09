# Parity and improvements — archived reimplementations and instability notes

**Policy (revised 2026-10-15, per user):** the shipped reconstruction is **1:1 with the original
ASI's behaviour, faults included** — same INI keys, same installs, same hook bodies, same
defaults, and the same fault/no-fault outcome for every input. The original's faults are
**preserved in the code and only documented**; do not ship guards, null-checks, or fail-soft
logic that prevent a fault the original has. The oracle is the shipped binary
(`game_refs/ZolikaPatch_original.asi.off`); `ZolikaPatch/` is an older mirror and documentation
only. Anything else that deviates (an improvement, a testing aid) is **documented here instead
of shipped**, with enough detail to re-apply it in a later project. Every divergence that *is*
shipped must be listed here (and in `HANDOFF.md`) with its evidence and, where possible, an INI
escape hatch; the only sanctioned cases are a config-level escape hatch with the original code
kept verbatim, or a strictly unavoidable divergence.

**Status note (2026-10-15):** the §4 table below still reflects the current tree, in which some
rebuilt bodies carry fault-avoidance guards recorded as "documented divergences". Under the
revised policy those guards are **divergences to remove** (restoring the original fault) and are
tracked by the parity-closure / fault-fidelity workstream. Read every "fault-avoidance", "guard",
or "documented divergence" entry in §4 as *pending de-guard*, not as accepted policy.

Reimplementations archived on 2026-10-13 (they had been shipped earlier; all were reverted to
the original behaviour):

1. `MiscFixes` per-fix split (five sub-options + master fallback).
2. `SkipIntro` shortened-intro variant + post-menu guard (FusionFix-style).
3. `SkipMenu` savegame check + `StartNewGameIfNoSavefile` key.

---

## 1. MiscFixes per-fix split (archived 2026-10-13)

**Original behaviour:** one `MiscFixes` INI key; the handler `0x10023dd0` applies all 11
XLivelessAddon rows when the key is enabled. The original INI has exactly one key:

    ; some features from XLivelessAddon, disable if not using GFWL and you have XLivelessAddon installed already
    MiscFixes=1

**Reimplementation:** each row of handler `0x10023dd0` was gated by its own sub-option, with
`MiscFixes` kept as a fallback "master" switch, so the bundle could be tested fix-by-fix:

| sub-option | rows (signature @ offset) |
| --- | --- |
| `RemoveDataIntegrityChecks` | `74 05 E8 ? ? ? ? 6A 05 E8 ? ? ? ? 83 C4 04 B9` @0 = `EB`; `C7 06 ? ? ? ? 74 19` @6 = `EB`; `83 C4 ? 3B 46 20` @12 = `EB`; `75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C4 0C` @0 = `EB` |
| `SkipTokenActivation` | `8B 54 24 04 52 A3` @0 = `EB C9`; `83 F8 01 75 30 A3` @0 = `B8` |
| `SkipFirstTimePlayAsk` | `C7 05 ? ? ? ? 03 00 00 00 33 C0 5E 59 C3 C7` @0 = `EB 0D` |
| `SkipInternetConnectionCheck` | `FF 15 ? ? ? ? 85 C0 75 04 32` @8 = `90 90`; `FF 15 ? ? ? ? 85 C0 75 07 A0` @8 = `90 90`; `FF 15 ? ? ? ? 85 C0 75 18` @8 = `90 90` |
| `SkipHealthCheck` | `81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ? ? ? 53 ? 6A 3C 33 DB 8D 44 24 ? 53 50 E8` @0 = `C3` |

(all rows have `handler: "0x10023dd0"`; the full JSON objects also carried a `why` string each).

**Mechanism that was removed** (re-apply all four pieces to restore it):

- `include/zpatch/op_corrections.hpp`:
  ```cpp
  // A curated split of a bundled option (MiscFixes) into one option per fix: the
  // op row matches on handler + signature + offset + replacement and is then
  // gated by `option` instead, with `master` kept as a fallback switch so an
  // existing INI that only sets the bundle keeps working.
  struct OpSplit final {
    std::uint32_t handler;
    std::string_view signature;
    std::int32_t offset;
    std::string_view replacement;
    std::string_view option;
    std::string_view master;
  };
  const OpSplit* op_splits(std::size_t& count) noexcept;
  const OpSplit* op_split_for(std::uint32_t handler, std::string_view signature,
                              std::int32_t offset, std::string_view replacement) noexcept;
  ```
- `src/op_corrections.cpp`: `op_splits`/`op_split_for` (the matcher compares handler,
  offset, signature and the whitespace-stripped uppercase replacement).
- `src/runtime.cpp` gate 1 in `apply_recovered_patches_logged`:
  ```cpp
  const OpSplit* split = op_split_for(op.handler, op.signature, op.offset, op.replacement);
  if (split) {
    if (!ini_option_enabled(config, split->option) &&
        !ini_option_enabled(config, split->master)) { ++report.gated_out; continue; }
  } else if (!ini_option_enabled(config, op.option)) { ++report.gated_out; continue; }
  const std::string_view effective_option = split ? split->option : op.option;
  ```
- `tools/apply_op_corrections.py`: emits the `data["splits"]` array into
  `static const OpSplit kOpSplits[] = { ... };` in `recovered_op_corrections.inc`
  (rows: handler, signature, offset, replacement, option, master).

**Why it was added:** explicit user request (2026-10-11/12) to make the XLivelessAddon
bundle testable per fix. **Why it was reverted:** the original ships one key; the split
changes the INI surface and the "1:1" contract.

---

## 2. SkipIntro shortened-intro variant + post-menu guard (archived 2026-10-13)

**Original body (`0x10045C30`), now shipped (`@exact`):**

```
69 C9 90 01 00 00   imul ecx, ecx, 0x190
81 FA 40 1F 00 00   cmp edx, 0x1f40
7D 02               jge short resume
33 D2               xor edx, edx          ; below the threshold -> skip outright
FF 25 74 AD 15 10   jmp dword ptr [0x1015AD74]   (ours: [zpatch_skip_intro_match])
```

**Archived reimplementation** (FusionFix-style: show the screens for ~300 ms, stop
compressing once the menu function has run so post-menu load screens keep real duration):

```cpp
// 0x10045C30 SkipIntro body. Entered via E9 from match-6 with the site's ecx
// and edx; resumes by jumping to the match address the handler publishes
// (dword_1015AD74). FusionFix's guard is applied: once the menu function has
// been reached (zpatch_skip_intro_served) the time compression stops, so the
// post-menu loading screens (savegame/world load) keep their real duration.
#if defined(_MSC_VER)
__declspec(naked) void abi_skip_intro_body() {
  __asm {
    imul ecx, ecx, 0x190
    push ecx
    push edx
    movzx eax, BYTE PTR zpatch_skip_intro_served
    push eax
    push edx
    push ecx
    call zpatch_log_intro_call
    add esp, 12
    pop edx
    pop ecx
    cmp BYTE PTR zpatch_skip_intro_served, 0
    jne ok
    cmp edx, 0x1f40
    jge ok
    mov edx, 0x12c                 ; 300 ms instead of 0
  ok:
    mov eax, offset zpatch_skip_intro_match
    jmp DWORD PTR [eax]
  }
}
#endif
```

Support pieces (also removed):

- `extern "C" std::uint8_t zpatch_skip_intro_served;` (`src/rebased_values.cpp`, set to 0 at
  load) — the "menu reached" flag; the archived SkipMenu body set it to 1.
- `zpatch_log_intro_call(ecx, edx, served)` in `src/log.cpp` (+ declaration in
  `include/zpatch/log.hpp`) with capped + hot-loop logging (40 calls then every 1000):
  `"hook SkipIntro  call #%u ecx=%08X edx=%08X served=%u"`.
- The GAS (`__i386__`) twin of the body used the same sequence with `movl $0x12c, %edx`.

**Note:** the log call clobbers `eax` (the original does not touch it); safe at this site
because the resume immediately executes `mov [ecx+disp], edx` and then `lea eax,[esp+...]`,
but a byte-parity check fails while the call is present.

---

## 3. SkipMenu savegame check + `StartNewGameIfNoSavefile` (archived 2026-10-13)

**Original body (`0x10046610`), now shipped (`@exact`):**

```
55                  push ebp
8B EC               mov ebp, esp
80 3D 50 84 1A 10 00  cmp byte ptr [0x101A8450], 0
8B 45 08            mov eax, [ebp+8]
75 10               jne resume-arg
85 C0               test eax, eax
75 0C               jne resume-arg
B8 06 00 00 00      mov eax, 6                 ; id 6 = load the last savegame
C6 05 50 84 1A 10 01  mov byte ptr [0x101A8450], 1
89 45 08            mov [ebp+8], eax
5D                  pop ebp
FF 25 48 93 15 10   jmp dword ptr [0x10159348] (ours: [zpatch_skip_menu_resume])
```

**Archived reimplementation** (unconditional-id-switch replaced with: switch only when a
savegame exists, or when `StartNewGameIfNoSavefile=1`):

```cpp
// True when at least one GTA IV savegame exists (SGTA4* under
// %LOCALAPPDATA%\Rockstar Games\GTA IV\savegames\<profile>\), cached per process.
extern "C" bool zpatch_has_savegame();   // FindFirstFileA enumeration

#if defined(_MSC_VER)
__declspec(naked) void abi_skip_menu_body() {
  __asm {
    push ebp
    mov ebp, esp
    mov BYTE PTR zpatch_skip_intro_served, 1
    call zpatch_has_savegame
    movzx ecx, al
    movzx edx, BYTE PTR zpatch_skip_menu_flag
    mov eax, [ebp + 8]
    push ecx
    push edx
    push eax
    call zpatch_log_menu_call
    add esp, 12
    cmp BYTE PTR zpatch_skip_menu_flag, 0
    jne skip
    cmp DWORD PTR [ebp + 8], 0
    jne skip
    call zpatch_has_savegame
    test al, al
    jne set6
    cmp BYTE PTR zpatch_start_new_game_no_save, 0
    je skip
  set6:
    mov DWORD PTR [ebp + 8], 6
    mov BYTE PTR zpatch_skip_menu_flag, 1
  skip:
    pop ebp
    mov ecx, offset zpatch_skip_menu_resume
    jmp DWORD PTR [ecx]
  }
}
#endif
```

Support pieces (also removed):

- `extern "C" std::uint8_t zpatch_start_new_game_no_save;` (default 1), set from the
  `StartNewGameIfNoSavefile` INI key in `run_loader()` (`src/dllmain.cpp`) — a key the
  original does not have.
- `zpatch_has_savegame()` above, and `zpatch_log_menu_call(arg, flag, has_save)`
  (`src/log.cpp`/`log.hpp`, same capped+hot logging convention:
  `"hook SkipMenu   call #%u arg=%u flag=%u has_save=%u"`).
- INI block:
  ```
  ; with SkipMenu, start a new game when no savefile exists (0=show the main menu instead)
  StartNewGameIfNoSavefile=1
  ```

**Why it was added:** explicit user request (2026-10-11/12) as a testing aid. **Why it was
reverted:** the original switches the id unconditionally and relies on the game's own
no-savegame fallback (the original INI comment: *"launching straight into the last savegame
regardless of what episode that's from (IV if no savegame is found)"*); the gate was an
extra, non-original INI key.

---

## 4. Known upstream instabilities / improvement candidates (documented, not shipped)

These are defects or rough edges in the **original** ASI's behaviour. The shipped
reconstruction keeps the original behaviour (1:1) unless noted; each item is a candidate for
a later project (or for a documented, INI-gated fix if the user asks for it).

| item | original behaviour | issue / candidate improvement | status in our build |
| --- | --- | --- | --- |
| `SkipIntro` (`0x10045C30`) | `xor edx,edx` below the threshold: intro screens are skipped outright, and the compression also applies to post-menu load screens when the site runs again | show brief screens (FusionFix uses 300 ms) and stop compressing once the menu is reached | shipped 1:1; reimplementation archived in §2 |
| `SkipMenu` (`0x10046610`) | first zero menu id becomes 6 unconditionally | with no savegame present the game must fall back to a new game (its own comment says "IV if no savegame is found"); an explicit savegame check + `StartNewGameIfNoSavefile` was our testing aid | shipped 1:1; reimplementation archived in §3 |
| `RemoveBoundingBoxCulling` (`0x10038770`) | `E9` at `0x957463` forces the bounding-box **size-cull** branch (inverted relative to the option name); the world nearly vanishes / LAN MP crashes; the original's own INI calls it *unstable* and defaults `0` | invert/NOP the branch, or drop the option | shipped **verbatim** with `RemoveBoundingBoxCulling=0` in the INI (documented divergence from 1:1 only via config); a by-name NOP variant was tried and also froze on save-load |
| `BuildingAlphaFix` pattern 2 (`F7 46 24 00 00 00 40 0F 84`, site `0x9594bd`) | the handler writes `E9` at match+7 with the displacement recomputed from the branch's own rel32 — i.e. the per-entity `je` becomes an unconditional `jmp` to the *same* target (the 'alpha fix' skips that entity); the earlier fear of 'world-state corruption / LAN MP crash' came from the *bisect window* shared with RemoveBoundingBoxCulling, which was the actual culprit | – | **shipped 1:1 (2026-10-13)** as a curated `match_branch` sig_install (recomputes the displacement); the generic bare-`E9` op row is suppressed. Pattern 1 (13-byte NOP at `0x959cd3`) stays. Both patterns exist on every classic build and EFLC; 12043 has only the `0F 85` variant |
| `MiscFixes` bundle | all 11 XLivelessAddon rows apply together | per-fix control (this is exactly the archived split in §1) | shipped 1:1 (single key) |
| `DrivebyFOVFix` (`0x100064b0`) | handler dereferences the resolver result unconditionally (`0x1000646E mov ecx,[eax+0Eh]`); the handle (salt `[0x101580D4]`, loader-rewritten to `0x09399528` on the probe-miss builds) can resolve to 0 → null-deref; `0x10006490 mov eax,[eax]` derefs the cached entry (possibly 0) | – (behaviour preserved) | **de-guarded 2026-10-16 and P13 closed 2026-10-21**: the resolver-result/cached guards were removed; the shim keeps the original sequence 1:1. The port's resolver call now uses the original catch-all SAFESEH behavior from sub_10003980 (exception returns EAX=0, then the caller faults at `[eax+0Eh]`). The offline x86 `driveby_seh_probe` verifies normal args/results, swallowed resolver AV, and the follow-up read AV at `0xE` on the dereference instruction. |
| `NewSavesCompatibility` converter (`0x10051B30`) | header/chunk `fread` results are ignored; `size < 9` underflows the `malloc` count; `malloc` is not null-checked; a wrong-magic file silently keeps the previous load's chunk flags | check `fread`/`size`, keep the flags per file | ported faithfully (2026-10-13); **re-verified 2026-10-16: no fault-avoidance guard remains** (`abi_new_saves_convert` ignores `fread` results, allows the `size-9` underflow and does not check the `malloc`; the `abi_new_saves_prepare` early-outs mirror the original's own `if (Src)` / `if (dword_101A8490)` guards). The candidate guards stay documented here, not shipped |
| `ForceCarHeadlightShadows` checker (`0x1001AD20`) | dereferences the shared model-lookup pointer (`0x1001AD5C call dword_10159C0C`) and the object (`0x1001AD62 movsx eax, word [v2+2Eh]`) unconditionally | – (behaviour preserved) | **de-guarded 2026-10-16**: the `lookup/_object == 0` early-outs were removed (the original's own `hashes[0]==0` early-out stays). Fault proven offline (child-process fault probes `fchs-null-lookup` / `fchs-null-object` → STATUS_ACCESS_VIOLATION) and armable live via CrashProbe `ForceCarHeadlightShadows`. One unreproducible detail: the original walks past 512 populated entries into adjacent .data; our module layout differs, so the 512 bound stays (documented; not reachable with the shipped INI's entry counts) |
| `RemoveGFWLUpdatePopup` helper (`0x10005340`) | writes `dword_10159C10 + 0x66F770..` with no base check; with xlive.dll absent the base is 0 → the four stores hit the low 0x66F770 page → access violation; the written constant is the `getnewver()` magic `0x10000E83` | – (behaviour preserved) | **de-guarded 2026-10-16**: the `base != 0` early-out was removed, and `wp_byte/word/dword` no longer skip on a failed `VirtualProtect` (they write anyway, like every original handler). Fault proven offline (fault probe `gfwl-zero-base` → AV) and armable live via CrashProbe `RemoveGFWLUpdatePopup`. **F36 fixed 2026-10-14**: the constant was `0x10000003`, now `0x10000E83` like the original's four stores |
| `BadPlayerModelCrashFix` MP branch (`0x10037AC0`) | non-ped player models print `"<user> used a non-ped model (<name>)"` using the ASI's own 38455-entry, 556 KB model-name table (`0x100DE770`), and call two native-script hashes | keep the table | the crash-fix path is ported 1:1; **F15 fixed 2026-10-14**: the MP-only diagnostic name lookup now scans our recovered 38455-name catalog in the original's order via `game_hash` (`model_name_from_hash`), returning the same `"**UNKNOWN**"` fallback only on a genuine miss |
| `DebugPrintSpawns` (`0x10037510` probe, shipped 2026-10-13) | the spawn probe logs `"<model> spawned a car"`-style lines and resolves the model name through the same 556 KB table | keep the table | same lookup as above (F15 fixed 2026-10-14); the probe body itself is a 394-instruction exact transcription |
| `FixPCIDKickbug` | deletes the HKCU GFWL `PCID` value with `SHDeleteValueW` | – | ported 1:1 (2026-10-13); INI-gated with the arm's default `1` (absent key runs it) |
| version guard (`DllMain 0x10055C70`) | compact versions in `[1201, 12999]` (Complete Edition and newer) open the downgrade page and `TerminateProcess(0x8000FFFF)`; not INI-gated | – | ported 1:1 (2026-10-13); it cannot fire on the supported builds (all < 1201) |
| `RemoveUselessChecks` mass patch (`sub_10044BD0`) | the loader arm (0x100541E4, arm default "0") runs a per-build routine that walks static RVA tables and writes five bytes at `game_base + entry` for ~2,200-2,500 entries (`90 90 90 90 90`, `B0 01 90 90 90`, `32`/`C0` byte patches) - recovered only in part before this pass (N5) | – (behaviour preserved) | **recovered 2026-10-16**: `tools/extract_useless_checks.py` + `src/recovered_useless_checks.inc` + `apply_recovered_useless_checks` (runs before the per-option passes, matching the original arm order; see docs/phase4/fix_report.md §2.7) |
| `MouseFix` (`0x10024600`, shipped 2026-10-13) | six of the seven destination bodies are byte-exact transcriptions; the seventh (`sub_100241E0`, the three 8-byte self-patches of the resumed function) is a compiled INFO body whose only contract is `VirtualProtect` on the handler-published resume; `dword_10159C34` is published by a `cmovne` (keeps the old value when the pattern misses) | parametrize the cmovne publish; transcribe the advance helper if a register contract ever surfaces | shipped; unconditional publish is identical on every build where the pattern exists (on a miss we publish nothing, exactly like the cmovne keeps 0); compiled helper documented here |
| `OutOfCommissionFix` (`0x1004C3F0`, shipped 2026-10-13) | `sub_1004B750`'s init runs on **every** call (its guard `dword_10159C00` has no writer): it decodes the embedded 163796-byte compressed "finale2.sco" resource (OOC Adler-32 trailer 0x41E794AB) into a fresh 179536-byte table, builds a 182272-byte in-memory RPF archive around it and mounts it with the game's own mount function via `memory:$<ptr>,<size>,0:`; the table build frees the table but the archive is leaked each call; neither `malloc` is null-checked | cache the init (the guard was clearly intended to) | ported 1:1: `src/recovered_ooc_pak.inc` + the transcribed decoder/builder in `hook_bodies_win.cpp`. **de-guarded 2026-10-16**: the two `malloc` null returns were removed (an allocation failure now faults in the decode/memset exactly like the original). The body now also reproduces the decoder cursor, fake-pak vector publication/reset and archive-offset writes; `abi_ooc_wrap` passes the eight-seed differential. `zpatch_ooc_selftest()` checks decode Adler + archive FNV; `tools/extract_ooc_pak.py` verifies the recovered blob. The separate malloc-failure probe remains unproven. |
| `NewSavesCompatibility` converter (`0x10051B30`) | `fread` results ignored, `size < 9` underflows the payload `malloc` count, `malloc` not null-checked | guard `fread`/`size`/`malloc` | ported faithfully (2026-10-13); the guards are candidates here, not shipped |
| `GarageModelFix` helpers (`0x10046B90`/`0x10046BD0`) | the lookup helper calls the shared model lookup unconditionally (`0x10046C10` / `0x10046C4A call dword_10159C0C`); the store helper dereferences the table entry chain unconditionally | – (behaviour preserved) | **de-guarded 2026-10-16**: the `if (lookup)` null checks were removed; the original's own `hash == 0` early-out and the `out <= 0 || out >= 32000` repair condition stay (both are the original's own). Fault proven offline (fault probe `garage-null-lookup` → AV) and armable live via CrashProbe `GarageModelFix` |
| `AddOptionsInMenu` focus-loss callback (`0x1001EDC0`) | the callback writes its saved 2-byte focus site unconditionally; a failed prime scan leaves that site null | – (behaviour preserved) | **de-guarded 2026-10-16**: the `g_focus_loss_site != nullptr` check was removed. Fault proven offline (fault probe `focus-loss-null-site` → AV); the live CrashProbe key is not wired for this body (the callback is reached through IVMenuAPI, not a reference-VA hook destination) |
| `shim_withheld()` op rows (`runtime.cpp:56`) | the loader writes `E9`/`EB` bytes *inside* the original shim bodies' own code (destinations like `0x10036FD0`) | – | deliberately withheld: our module layout differs at those VAs, so applying them would overwrite unrelated code; the shims themselves install through the curated/sig-jump rows and are verified (`verify-bodies` all PASS) |
| `LocalSettingsPath` loader rows | two duplicated bare-`E9` op rows (offset `-13` against the `FF D7` fallback pattern) | – | inert on every available build (the fallback pattern has 0 hits on 1.0.8.0; the curated sig_install covers the intended write); cannot be textually suppressed (runtime-built handler patterns), documented in `docs/parity_audit.md` |
| `CutsceneFixes` chain B body (`0x100388C0`, shipped 2026-10-13) | the body dereferences the native-script resolver result unconditionally (`eax = [eax+0xE]`, then `[ecx+eax+0x18]`, then the float at `+8`) and reads the object at `[0x19CF9F0]`-style addresses it does not own; it also **self-modifies the game's camera function** at `target+0x4E` (the immediate byte of `mov byte [esp+0x1C], imm`) via `VirtualProtect` + write, and only restores the object when the position drift test fires | guard the resolver result / skip the poke when the pattern's site is unexpected | shipped **1:1** (`@exact` 160-insn transcription, `PASS`); the poke is re-armed to 1 on every first call and 0 on the second, exactly like the original. Candidate guard documented here, not shipped |
| `NewMemorySystem` (`0x10038680`, shipped 2026-10-13) | the handler replaces the game's arena allocator wholesale: E8 at `match-34` replaces the game's init call `0x454740` (so the game's own allocator init **never runs**), E9 at `match-29` skips the following `push 0xd65c50; call 0xcf5047` registration, and the plugin's vtable (`0x100B5038`) is written into the allocator singleton (published from the second scan, `dword_10159CD8` = the `mov esi, 0x19cf9f0` immediate). The plugin's allocator is a `HeapAlloc` wrapper: `unit = max(0x10, align)`, `want = max(size, unit)`, block size `want + 2*unit`, returns `((base-1)/unit + 2)*unit` with the HeapAlloc base offset stored at `[p-4]`, `memset(p, 0xCD, want)`, and registers the pointer in the plugin's own FNV-1a hash map under its critical section; `free` restores the base from `[p-4]` and unregisters | – | shipped 1:1 (2026-10-13): the two installs + the publish are curated rows, the E8 body `sub_10038670` and the vtable's 24 entries are `@exact` (10 stubs + ctor) or compiled ports of `alloc`/`free`/`contains` (vtable[2]/[3]/[18]); the registry keeps the original's membership semantics with a plain set behind an equivalent recursive lock. The 556 KB registry internals (bucket layout, load-factor rehash) are not observable through the class and are not reproduced. Note the allocator's 0xCD fill makes every game allocation start filled (not zeroed) - that is the original's behaviour, not ours |
| `CarDynamicShadowFix` EA00/BDS byte at game 0x8F7422 | per the disassembly `sub_1000EA00` (called unconditionally at `sub_10010B60`'s common tail, `10014FB4`) rewrites the `push 0x104` immediate to `push 0x100`; `sub_10005B20` (BuildingDynamicShadows) calls the same installer step | – | **fixed 2026-10-15 (session 15)**: the EA00 `sig_write` is gated by `CarDynamicShadowFix` (its own arm's chain); the earlier `CarDynamicShadowFix@noFCHS` group was removed entirely - its three rows were misattributed to `sub_1000B790`, which is a path helper with no patch stores. Those sites belong to `sub_1000E870` (called from the FCHS handler tail only when FCHS == 1), whose writes restore the original bytes (no-ops); they are now five `ForceCarHeadlightShadows` sig_writes. With CarDynamic=1/FCHS=1 the EA00 write destroys E870's P2 pattern first (arm order), so the original's P2 scan fails identically |
| compiled-helper lookup guards (PlayerAsPedComponentsFix `sub_1003D930`, SuperLODFix `sub_100469A0`, PopupHeadlightsFlag `sub_10044310`, PedModelsInCutscenesFix `sub_10050870`, the MovingSteeringWheel trio consumer `sub_10004C70`, CopSirensFlag `sub_10005F70`/`sub_10006260`, NewFilesLoader `sub_100515E0`, the OutOfCommissionFix wrapper `sub_1004C350`) | every original calls the shared model lookup / the published helper pointer / the vehicle bone-matrix helper unconditionally (`call dword_10159C0C`, `call dword_101580B4`, `call dword_10159B88`, `call dword_10159C08`, `call dword_10159BF8`, `call dword_10159C04`) and dereferences the object (`[x+2Eh]`); a preamble miss or a null object faults | – (behaviour preserved) | **de-guarded 2026-10-16**: the rebuilt helpers’ `if (lookup)` / `if (fn)` / `object ? ...` guards were removed; each now logs a pre-fault `guard-removed ... fault=0x<original VA>` line and faults exactly where the original does. Proven offline for the model-lookup class (child-process fault probe `fchs-null-lookup` shares the same call); the other pointers are `unproven(fault reproduction)` offline (they need a preamble-miss scenario) but the guard is gone regardless |
| `NoImpostorHeightCutoff` default semantics | the loader arm reads the key with default **"1"**, so an INI without the key still installs the two 0x9090 writes | – | **fixed 1:1 2026-10-14 (F2/F3)**: the option gate now substitutes the recovered per-option arm default for an absent key and requires the parsed value to be exactly 1 (`cmp esi,1`), matching every arm |
| moving-parts first-scan gate (F17, verified-harmless) | `0x10005210` (called by every enabled MovingSteeringWheelSupport / MovingPlaneParts / ShakingEngineBlock arm) first scans `A1 ? ? ? ? 8B 0D …` (0x101295C8); when that pattern is absent it skips the E9 installs entirely | harden by making the first scan a precondition | not modelled; measured pattern availability shows p1 is present on every build that has p2/p3 (absent only on 12059, where both are also absent), so there is **no observable divergence on any available build** |
| `NoImpostorHeightCutoff` block A vec[1] (G8, original instability) | on 1.0.0.0 the block A pattern matches **once**, so the original writes `vec[1]` (a VirtualProtect + write through an uninitialised vector entry) | – | the port's match-literal rows skip absent indices (1 write on 1000, 2 elsewhere) — a safe divergence recorded here; no INI key needed |
| `DualVehicleHeadlights` build-gate extraction (fixed 2026-10-14) | the handler's `dword_1015BE04 in 1..6` selects the E9 **destination** (`sub_10006630` old / `sub_100064F0` new), it is not an install gate; the extractor had emitted mask 0x7E, excluding 1080 from the install entirely (and the only shipped row was a bare-`E9` no-op, so the feature was absent) | – | fixed 1:1 (session 14): two `@exact` bodies + five publishes + two install rows with explicit masks (`@old` 0x62, `@new` 0x4bf80) |
| `CarDynamicShadowFix` `sub_1000B790` gating (fixed 2026-10-14) | the three shadow-buffer reductions (`push 0x504->0x100` at 0x8F73EC, `push 0x104->0x100` at 0x8F7422, `jz->jmp` at 0x8F719F) run only when the arm's `ForceCarHeadlightShadows` read (default "0") != 1; our build applied them for FCHS=1 too (the same under-activation direction as the user's symptom) | – | fixed 1:1: rows moved to `CarDynamicShadowFix@noFCHS`; the original run's diff confirms all three sites untouched with FCHS=1 |
| IDE parser `wheelangle:` arm (fixed 2026-10-14) | `sub_10003FA0` leaves an existing model's angle untouched and matches the prefix case-insensitively (`strnicmp`); `abi_ide_parse` used to overwrite the angle and only matched all-lower/all-upper | – | fixed 1:1 (session 14), matching the portable `ide_flags.cpp` model |
| MiscFixes vs XLivelessAddon (fixed 2026-10-14) | `sub_10023DD0` skips **every** write when `byte_10159B84` is set, and the loader sets it iff `XLivelessAddon.asi` is loaded (0x10052DF3); our build applied the rows with XLA present | – | fixed 1:1: `ini_option_enabled_impl` disables MiscFixes when `zpatch_xliveless_present != 0` |
| build 18 / Complete Edition (12043, 12059) | the classifier (`0x10055C70`) maps version `0x2F0B` (1.2.0.43) to compact **18** and everything above it - including 1.2.0.59 (`0x2F1B`) - to **unknown (0)**. **No arm or table carries build-18 data**: every per-build switch in the loader arms and handlers covers only builds 1/5/6/7/8/9/0xA..0xF (no `== 0x12` case anywhere; the recovered rva gates are all `eq`-gated to those builds), so a CE exe receives **pattern-gated rows only** | – | ported 1:1: our classifier maps 12043 -> `v12043` and 12059 -> `unknown`, no rva row admits build 18 or 0, and ungated arms still run their pattern rows. On `GTAIV_12059.exe` only five curated patterns still match (SkipMenu x2, CrashMsgFix, CombinedRadioFreezeFix, RestoreDeathMusic) - the CE exe is a recompile. 1.2.0.43's exe is not available offline; the expected diff is the same "pattern rows only" behaviour, with whatever subset of patterns its binary retains |

## 5. Dead code in the original ASI (documented for parity; nothing to reproduce)

The original module contains code with **no live entry point**: no caller, no export, no data
reference that a live path reaches. It costs nothing to leave out, but it must be documented so a
future session does not mistake it for a missing feature. Verified 2026-10-13 by scanning the ASI
for `E8`/`E9` references and absolute dword references to each entry point, plus the export table
(the ASI exports **nothing**).

1. **ZMenuIV message-window IPC** (`0x100041A0`–`0x100046xx`):
   - `sub_100041A0` is a `WNDPROC` for a **message-only window** named `MessageWindow` /
     `MessageWindow2`; it handles `WM_NCCREATE` (stores the hwnd, sets `byte_1015AD9B`) and the
     custom message **`0xAB52`** (case 18 copies a 64-byte key-map blob from `0x10104050` to a
     pointer carried in the message, case 51 returns a sum, case 100 sets a flag).
   - `sub_10004260` registers the class + creates the window, `sub_10004300` runs the
     `GetMessage`/`DispatchMessage` pump, `sub_100043D0` spawns a `std::thread` for it gated on
     `byte_10158134`, and `sub_10004520`–`sub_10004570` prepare/send the custom messages.
   - `byte_10158134` is **1 in the .data image**, but its only writer is `sub_100043D0` itself
     (which sets it to 0) and `sub_100043D0` has **no caller**; `sub_10004570` has no caller
     either. `sub_100041A0` is referenced only as the WNDPROC stored inside the dead
     `sub_10004260`. ⇒ whole cluster unreachable.
   - Origin: ZMenuIV and ZolikaPatch share a source base; this is ZMenuIV's cross-ASI message
     channel, left in the ZolikaPatch binary by the linker. The real cross-ASI communication (the
     `AddIVMenuOption` registration) is implemented and documented in `HANDOFF.md` session 8.
2. **`CutsceneFixes`** (loader arm reads the key, default 1) has **two chains**, both live:
   - Chain A (**live — the earlier "vestigial / nothing publishes 0x10185F88" verdict was WRONG**;
     refuted byte-level by the 2026-10-14 independent audit F21): the ctor-called installer
     `0x1003DA50` (ctor `0x10001150`, this `0x101806F8`) builds the pattern text
     `74 24 8B 44 24 2C 83 F8 03` (fallback `74 0B 83 7C 24 2C 03`) and scans it via `0x1005AFA0`
     (call at `0x100413F4`); on a hit it publishes the first match (`0xC9A840` on 1080) into
     `[0x10185F88]` through the `this+0x5890` chain (`10041429 add ecx,0x5890; 1004144A mov [edx],eax`).
     The forwarder `0x100441A0` -> slot `[0x10189EE0]` -> callback `0x10044160` then pokes `EB` at the
     published site via `0x1005AD60` and self-clears through `[0x1018C22C]` (reset `0x10044130` zeroes
     `0x10189EE0`/`0x1018C22C`/`0x10185F88`). The port's chain-A row (`recovered_op_corrections.inc`
     signature + fallback, byte `EB`) is the correct transcription.
    - Chain B (the real fix, found 2026-10-13): the arm also calls `sub_10045420` -> forwarder
      `0x10045410` -> slot `[0x10164F04]` -> the **18 KB worker `sub_10038B40`** (4742 insns):
      it builds two pattern strings with the same obfuscated char-by-char builder MouseFix/OOC use
      (42 assigns, 24 appends, plus the direct first-char stores at `0x10038BC8`/`0x1003BB50`),
      scans the game (`sub_1005B7D0` at 0x1003B8E0, `sub_1005B6A0` at 0x1003D1E8 + `sub_1005BA00`
      at 0x1003D1F4), patches both camera call sites to its own body `sub_100388C0` and publishes
      `dword_1015BDE4`/`dword_1015BDF0` (stores at 0x1003B9C8/0x1003B9CE). A separate installer
      `sub_10045430` (gated by `ReversingLightFix`, not CutsceneFixes) scans the literal pattern
      `11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10` and writes `0x60` over the `64` operand
      byte at match+8 (the `[edx+0x64]` field offset becomes `[edx+0x60]`). Registration ctor
      `0x10045350` (worker + reset stub `0x100453E0`), slots `[0x10164F04]` / `[0x1016835C]`.
   The user's in-game observation (jittery cutscene camera + zoom at high FPS fixed) is chain B.
   **Shipped 2026-10-13 (session 11)**: the worker's two pattern strings were recovered with the
   builder-seed emulation (the char-by-char append stream, including the mid-worker string reset
   at `0x1003BB46`): scan 1 `8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8` (E8 at match-34 -> the worker's
   own body `sub_100388C0`, publishes `dword_1015BDE4` = the site's original call target
   (`match + *(match-4)`, the game camera fn, `0xC9A640` on 1080) and `dword_1015BDF0` = that
   `+ 0x4F`), scan 2 `8B CF 88 44 24 0F E8 ? ? ? ? A1` (E9/E8 at match-5 -> the same body via
   `sub_1005BA00`). Both patterns have 1 hit on every classic build + EFLC 1110/1120/1130 and are
   absent on 12059. The body (`0x100388C0..0x10038B3F`, 160 instructions) is an `@exact`
   transcription (verify-bodies PASS): it reads a scene float through the loader-published
   native-script resolver (handle = salt `dword_1015803C`), pokes the camera function's
   `mov byte [esp+0x1C], imm` immediate (1, then 0 on the second call), calls the original camera
   function once or twice, and after the accumulated frame time passes `0.3333` keeps the second
   result unless the object's position floats moved beyond (0.05, 0.05, 0.05, 0.5, 0.5, 0.5), in
   which case it restores the pre-call state and returns the first result. Ported 1:1 including
   the unguarded resolver dereference (see §4).

   The six `pusha`-forwarder options (`FastLoading`, `HighQualityReflections`, `MouseFix`,
   `CutsceneFixes`, `GarageModelFix`, `OutOfCommissionFix`) all use the same mechanism: a per-option
   registration ctor (called with `this`/the module base in ECX) stores {callback, aux/dest,
   reset-stub} into plugin BSS slots using `lea`/`add` obfuscation; the loader arm calls the
   callback through a `pusha; call [slot]; popa` forwarder; the callback does its work once and
   calls its reset stub, which zeroes the slots (a one-shot self-clearing callback). This is why a
   naive static scan sees the slots as 'never written': the stores are `this`-relative. HQR's
   registration ctor is `0x10023CC0` (stores `0x1001F390` -> `[0x10192F14]`, `0x1001F030` ->
   `[0x10192F18]`); FastLoading's is `0x1001EBA0` (stores `0x1001EC60` -> `[0x1015E840]`).
   CutsceneFixes additionally shows that an option can carry a *second*, larger worker chain
   (`0x10038B40`) alongside the small callback - a full per-option audit must check every
   `feature_callee` of the arm, not just the handler's forwarder.

   **Session-11 re-check (2026-10-14)**: every option arm's direct callees were scanned against
   the implemented/documented VA set. The only large callees are the shared (already ported or
   documented) installers - CarPartsShadowFix's `sub_10007E40` (the `83 F8 04 ... 75 0F` site,
   shipped), ForceCarHeadlightShadows' `sub_1001ADD0` (152 insns, its installer, covered by the
   option's rows), and CutsceneFixes' `sub_10038B40` (shipped this session). Every other callee is
   the shared arm machinery (INI read `0x1005C160`/`0x1005BFF0`, string helpers `0x10057870`/
   `0x100578B0`/`0x10003B20`/`0x1004AC90`/`0x10057710`, `atoi` `0x100014B0`, the forwarder wrapper
   `0x10045420` with exactly one caller - the CutsceneFixes arm - and the trivial `0x10005500`/
   `0x10005210` patch helpers). No additional hidden worker remains.

3. **`sub_10003A10`** (PlayGTAIV.exe PID scan) is live **only** as part of
   `LaunchWithPlayGTAIV` (implemented); any other apparent use is dead.
4. The **`dword_1015BDC4`** publish inside the CarDynamicShadowFix build-only branch
   (pattern D) is write-only in every available build (no reader) — omit, documented.
5. `detour_10022ED0` (the CarDynamicShadowFix reset stub that zeroes its own install pointers) is
   observationally a no-op after the installer caches its targets first; reproduce only if a
   re-entrant install path is ever added.

Related reading:

- `HANDOFF.md` "MP regression ROOT CAUSE" and "RemoveBoundingBoxCulling — ROOT CAUSE" for the
  bisect evidence and the decision trail.
- `evidence/option_coverage_gaps.json`, `docs/handlers_unresolved.md` for the handler write
  sets behind the remaining unported options.
- `docs/parity_audit.md` (session 9) for the per-option 1:1 audit of all 98 options.

## 6. Improvement candidates (Mission C, session 9 — documented, not shipped)

Grouped by kind. None of these are shipped; each would need an INI escape hatch (or a
config-independent guard that cannot change behaviour) and a §4 record if ever implemented.

### 6.1 Performance

| site | cost today (1:1 with the original) | candidate fix |
| --- | --- | --- |
| MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts trio (`abi_steer_body`, original `sub_10004C70`) | every consumer call walks a 512-entry `wheelangle` table and calls `zpatch_model_lookup` per entry | one-time `{hash → entry}` cache keyed off the game's own lookup; the wheelangle table only changes on vehicle load |
| `PopupHeadlightsFlag` (`0x100068A0`) | per call: 512-slot hash scan plus a walk of the popuplights list | same cache shape; the flag check can run last |
| `ForceCarHeadlightShadows` (`0x1001AD20`) | per call: full table walk looking up the model | cache the resolved entry |
| `DebugPrintSpawns` (`0x10037510`) | every spawn does a 31000-entry linear hash scan (the original's own table layout) | build a hash index once (the table is static) |
| `zpatch_script_resolve` (plane path) | re-resolves the script-pointer chain on every script-float read (up to 9×/frame in the plane path) | resolve once per frame / cache the last (script, index) pair |
| `NewSavesCompatibility` converter (`abi_new_saves_convert`) | reads **and discards** every chunk payload at each save load (up to ~37 KB per save) | `fseek` past the payloads; only the 9-byte chunk headers are needed |
| `OutOfCommissionFix` wrapper | one decode + RPF build + game mount per wrapped call; the archive is leaked (the original's guard `dword_10159C00` has no writer) | set the guard once (the original clearly intended to) and cache the mounted archive; or free the previous archive on re-entry |
| `MouseFix` axis helpers | one `movd`/`cvt` chain per read call - already minimal | – |

### 6.2 Stability

| site | issue | candidate fix |
| --- | --- | --- |
| trio registry walk (`abi_vehicle_component`, original `sub_10003F40`) | unguarded deref chain (`ped → +8 → +0xB4 → +0xC`) | null-check each hop; our compiled version already guards `registry`/`name` |
| `abi_vehicle_component` entries pointer | `*reinterpret_cast<...>(registry)` dereferenced with no check | null-check after the first load; the count at `+0x14` can exceed the allocation |
| popup model object (PopupHeadlightsFlag) | the model object is dereferenced to read the bone/position without a class check | check the object and its vtable before use |
| resolver results (`HookDestinationResolver`) | the original dereferences resolver results unconditionally (see DrivebyFOVFix in §4) | treat 0 as a clean skip in every body that calls through a slot |
| `ForceCarHeadlightShadows` checker | see §4 (shipped guards) | – |
| NewSaves converter | see §4 (`size < 9` underflow, short reads) | the guards listed there |
| `MouseFix` publish | the original's `cmovne` publish can leave `dword_10159C34` = 0 and the naked body then calls it; our unconditional publish avoids the 0 case on the builds where the pattern exists | keep the unconditional publish (already done) |
| `OutOfCommissionFix` wrapper log | `[0x10159BF8]` (game logger) is called only when the lookup returned non-null; our port additionally skips a null slot | keep the null-slot skip |

### 6.3 Safety

| site | issue | candidate fix |
| --- | --- | --- |
| `FixPCIDKickbug` | always-on HKCU `PCID` deletion (`SHDeleteValueW`), default `1` | INI-gate the deletion itself (the arm's default already is `1`; a separate opt-out would diverge) |
| version guard (`DllMain 0x10055C70`) | unconditional `TerminateProcess` + `system("start …")` for unknown/Complete Edition builds | make the message/launch path INI-gated (cannot fire on supported builds today) |
| spawn probe cookie check (`zpatch_spawns_cookie_check_c`) | `__fastfail(2)` on mismatch (faithful to `sub_1005FE7B`) | leave (it is the original's anti-tamper contract); probe-only in practice |
| loader double-load guard | `MessageBoxA` on a second load | replace with a log line (the original shows a modal box) |
| in-place operand patches (`p-5`/`p-11` in `abi_new_saves_convert`, `wp_word(site - 5, …)`) | code bytes adjacent to the scan site are rewritten; a moved/relocated site would corrupt neighbours | re-scan and assert the pre-image bytes at each adjacency before writing |
| `OutOfCommissionFix` fake-pak init | allocates 179 KB per call and leaks it (inherited) | see §4/§6.1 |

### 6.4 Further development

- **Framework**: `dest_kind 6 = literal_bytes` shipped this session (MouseFix's in-pattern
  rewrites). A per-option predicate for one-of install groups (A4's build-only patterns:
  trio fallback, CarDynamic B–E, PopupHeadlightsFlag's EDX twin, CarParts' `83 FA 04 75 0C`
  variant) would let them ship without a bespoke installer.
- **Tests**: derive the hard-coded counts in `tests/test_main.cpp` from the tables
  (`gated_out`, `install_count2`, `slot_count`) so row additions cannot silently desync them.
- **verify-bodies**: compare against the true function end (linker map / PDB) instead of
  stopping at the first `RET`/`INT3`; several compiled bodies are only INFO today.
- **Diagnostics**: embed the 38455-entry model-name table **once** and share it between
  `DebugPrintSpawns` and `BadPlayerModelCrashFix` (closes the `"**UNKNOWN**"` divergence).
- **Coverage**: recover the build-only patterns for the remaining supported builds
  (1004/1061/1100–1130) — each needs a matching game binary to verify against.
- **`NewMemorySystem`**: needs a game-vtable reimplementation (excluded today, §5).
- **IVMenuAPI**: the wider `AddIVMenuOption` surface (header in `IVMenuAPI_v1.1.zip` at the repo
  root) — currently only the callbacks needed by `AddOptionsInMenu`/`RestoreDeathMenu` are wired.
- **Cleanups**: the duplicated `LocalSettingsPath` bare-`E9` rows; `shim_withheld`'s list could
  be data-driven from the curated rows.

## Re-applying an archived improvement (checklist)

1. Restore the code/rows exactly as above (they were verified once: native tests +
   `verify-bodies` + in-game).
2. Keep it INI-gated; keep the original key as the fallback where a split adds sub-keys.
3. Re-mark the affected body **without** `@exact` unless it is a byte-parity transcription
   (the archived SkipIntro/SkipMenu bodies are not: log calls, extra branches).
4. Run `python ZolikaPatch_reconstruction/tools/apply_op_corrections.py` after touching
   `evidence/op_corrections.json`, then build `zpatch-test` + `msvc-asi` (runs
   `verify-bodies`).
5. Record the deviation here and in `HANDOFF.md`, and add an INI comment.

## 4. Resolved divergence: FreezeCarFix spawn-probe / security-cookie shim (2026-10-18/19)

**Resolved 2026-10-19: our `sub_1005FE7B` twin returned the security cookie in EAX, so the
probe returned the cookie instead of the model index. See the RESOLVED block below for the
evidence and fix.** The historical analysis that follows is kept for the record.

**Symptom** (user, 2026-10-16/18, GTA IV 1.0.8.0, GTAC multiplayer): loading into MP
faults deterministically at `GTAIV.exe+0x4B4D7A` (`sub_8B4D60`, `mov al,[edx+ecx*8+8]`,
`edx=[0xF493F0]`), called from `+0x71347D` - the instruction after the spawn-probe
redirect site `+0x71346A`. Turning `FreezeCarFix=0` removes the crash; the **original
ASI with the same INI does not crash**.

**What was verified 1:1** (so the divergence is *not* in the patched bytes):

* All 17 FreezeCarFix writes (10 factory-dispatch cases `+0x24..+0xF6`, the guard
  `+0x0B`, the six probe redirects `+7/+6/+9/+254/+9/+7`) match the original installer
  `sub_10037C70`'s `E8` sites and bytes.
* All 12 published slots (`dword_10159CB0`, `...C98`, the ten `...CBC/C9C/CA0/CA8/C7C/
  C88/CAC/C90/C94/C84`) match the call targets the original computes from the game image.
* The 11 rebuilt bodies (`abi_spawns_guard`, the ten `abi_spawns_case_*`, `abi_spawns_probe`)
  are instruction-identical to `sub_100372F0` / `sub_10037330..` / `sub_10037510`
  (compared with module operands resolved to slot names, which `verify_hook_bodies.py`
  normalises away).

**Divergence (then open; resolved below).** The install is byte- and body-identical, but
`FreezeCarFix=1`
still faults here and not in the original, and it does so **regardless of
`DebugPrintSpawns`** (user, 2026-10-18) - so the diagnosis is *not* the probe's
diagnostic/print path (that earlier hypothesis is retracted; the print shims
(`zpatch_spawns_print_edx`/`who_edx`/`name_ecx`, `zpatch_spawns_sprintf` -> CRT
`_vsnprintf_s` instead of `sub_10001280`) are still worth a 1:1 pass but are not the
crash). What is left is an **order/interaction effect with another enabled option in the
same INI**: FreezeCarFix's seventeen writes land inside the game's object-factory
dispatch (`0x2D6D2C..0x2D6DFE`), the guard site (`0x2D6FC4`), the streaming call sites
(`0x2DD71E`, `0x2DD813`, `0x2FAE07`, `0x31346A`) and `0x3163AB`, and any option whose
scan or write touches those bytes after them (or a mass pass such as
`RemoveUselessChecks`/`OutOfCommissionFix`, which are `0`/`1` in the crashing INI) can
then land on a shifted site. The per-option phase-3/4 differential cannot see a
*pairwise* interaction, which is why it reports clean.

Fault path, for reference: the faulting call is at `0x71347D`, right after the probe
redirect `0x71346A`. `sub_8DD330(arg)` (the shared helper all six probe sites publish as
`dword_10159CB0 = 0x149D330`) returns `ped_array[lookup(arg)]` - a *pointer* - and
`sub_713400` feeds it to `sub_8B4D60` as a small zone index, so the read goes wild
(`edi` in the dumps is that pointer). The helper early-outs to `0` when
`word[0xF3E9E0]` (its lookup-table count) is zero, which is the healthy case; a
nonzero/garbage count at that moment is what turns it into a crash.

**Interim behaviour (superseded 2026-10-19 by the cookie-shim fix):** leave
`FreezeCarFix=0`; the feature was unusable until the shim was corrected. `DebugPrintSpawns`
is not implicated.

**Differential results (2026-10-18, build 1080, driver `p3-dump --live-resolve` vs
`emu_oracle`), for the exact crashing INI (the user's 59-option config with
`FreezeCarFix=1`, cell `crash_cfg`):**

* `orig_installs=93 port_installs=93`, `install_sites_only_original=0`,
  `install_sites_only_port=0`, `install_opcode_mismatch=0`. The FreezeCarFix-only cell
  is `16/16` with the same result.
* `byte_diff_vas=22` in exactly 6 runs, all explained and none a divergence:
  * 1 run `DoNotPauseOnMinimize`, `75 20` -> `90 90` at RVA `0x338A`, `orig_writer=null`.
    Not a bug: the original patches that site from its **lazy** patcher `sub_1001ECB0`
    (reached from `sub_1001EDE0`, called by the arm at line 2444), which writes `90 90`
    when `byte_1015AD43 != 0` and otherwise restores the two saved bytes
    (`sub_1001ECBD`'s site `[0x101A84A8]` = the `75 ? 8B 0D ? ? ? ? 51 FF 15 ...` match,
    saved in `word_1015AD48`). The emulator never reaches it because the lazy scan sits
    behind the CRT init counter (`[0x101A846C]`/`fs:[0x2C]`) - an emulator blind spot.
    Its other row (`6A 0A 56 50 FF D2 A1` +1 -> `06`) matches the original byte for byte.
  * 3 runs `StartupTimeFix` (RVA `0x77A48`, `0x4973D9`, `0x498859`) plus 1
    `ForceCarHeadlightShadows` (`0x8F7D2E`) and 1 `PopupHeadlightsFlag` (`0xA53AB6`):
    each side writes **its own module's** global address there
    (`rebased_values.cpp:305-331` `g_startup_time`, and the FCHS/popup float globals
    documented at `rebased_values.cpp:100/110`). Expected class, not a divergence.
* The FreezeCarFix arm gate is confirmed in the loader decompilation
  (`100523C0_sub_100523C0.c:1717`): `if (FreezeCarFix==1) { read DebugPrintSpawns;
  byte_10159C15=...; sub_10037C70(); }`.

**Per-option sweep (2026-10-18): every option in the crashing halves was differential-
tested alone** (`tools/phase3/emu_oracle` + `p3-dump --live-resolve` + `diff_oracle`,
all 29 H1 and 29 H2 options). Result: **no option has any install-site, opcode or
unexplained-byte divergence.** The only non-zero runs are the three benign classes above
(`DoNotPauseOnMinimize`'s lazy-patcher site - emulator blind spot; `ForceCarHeadlightShadows`,
`PopupHeadlightsFlag` and `StartupTimeFix` - each side stores its own module global).
With `SkipLauncher=1` the launcher family diffs to **zero** (`runs=0`), confirming the
`only_port=10` seen in the other cells is purely the phase-3 driver's unconditional
launcher staging, not the shipped ASI (`dllmain.cpp:83` gates it on the key).

**Bodies audited against the original's disassembly (all faithful):** the eleven
FreezeCarFix bodies; `abi_game_hash` (`sub_10003E50`: same joaat loop, `>=2 ? v : v+2`
tail, `\\`->`/`, uppercase->lowercase, quoted-string handling, and `hash:` prefix parsed
with `strtoul(str, NULL, 0)` base-0 semantics - `parse_ulong32` matches); the two
ImprovedShaderStreaming finders (`sub_10045A30`/`sub_10045A80` + filters
`sub_100459E0`/`sub_10045A10`: same `this+0xC` 16-bit count, `this+8` array, vtable+0x10
name call, same `name[4]=='h' || name[4]=='p'&&name[5]=='e'` / `name[4]=='v'` filters;
ours adds null guards only); `abi_ped_models_in_cutscenes_check` (`sub_10050870`: hashes
the string at `0x1013A038` = `"player"`, calls the model lookup with `&index` seeded to
-1, compares `this->+0x2E` sign-extended - matches ours).

Conclusion: the install is provably faithful for this configuration, so the crash and
the entity-sync loss come from **runtime state the differential cannot model**, not from
any applied byte. Remaining candidates to check, in order: (a) the 10 salt values
(`zpatch_salt_*` vs `dword_10158028..D4`) used by `zpatch_script_call` - the port keeps
the on-disk constants when the salt probe hits, confirm the original does too; (b) the
initial values of port-side globals the bodies read (`g_startup_time`,
`zpatch_popup_angle`, the FCHS float); (c) the `byte_1015AD43`/`word_1015AD48` pair -
the original re-runs `sub_1001ECB0` from the setter `sub_1001EDC0` at runtime, our port
writes once at load, so a runtime toggle diverges; (d) an `AddVectoredExceptionHandler`
flight recorder around the FreezeCarFix bodies if (a)-(c) are clean.

**To finish:** find the colliding option by bisecting the *other* options with
`FreezeCarFix` pinned on:
`python tools/mp_bisect.py start --pin FreezeCarFix` then `step`/`auto` (see
`docs/mp_crash_bisect.md`). The minimal crashing pair isolates the write whose site
FreezeCarFix displaces (or vice versa); then order the two installs as the original's
loader arms do, or fix the displaced scan.

**Also found while auditing:** `dword_10159CB4` (skip), `dword_10158058` (kind) and
`dword_10159C80` (class) are absent from `rebased_values.cpp`'s slot table - the port
re-homed them to `zpatch_spawns_skip/kind/class`. Benign only while nothing else reads
those slots; register or re-verify them. Pattern `32 DB 8B 54 24 24 52 E8` and its
`... 8B F0` variant both match 0x3163A4, so site `0x3163AB` is written twice with
identical bytes - harmless, but it shows the importer keeps both rows.

**Live probe trace (2026-10-18, diagnostic build `tools/build_asi_probe_trace.cmd`,
`[Debug]`-free: the resolver redirects `0x10037510`/`0x100372F0` to logging wrappers
that forward to the real bodies; log = `game_refs/ZolikaPatch.ptrace.log`):**

* One crashing run (single probe call, from site `0x31346A`, arg = the MP player-model
  hash `0x879495E2`): the probe returned `0x49627263`, a *model-info pointer*, where the
  caller needs a small model index - `sub_713400` then feeds it to `sub_8B4D60` as a zone
  index and the read goes wild (`+0x4B4D7A`) - the crash.
* One non-crashing run (sync still broken): 715 guard calls, **every one with
  `skip=0` in and `result=1` out, i.e. no entity was ever dropped**, and 715 probe calls
  across sites `0x2DD71E`/`0x31346A`/`0x3163AB` all returning the same large value
  (`0x14534DEB`) - again not an index, so the caller's grid lookup reads garbage (no
  fault, so no crash) and the model/entity stream is wrong - the sync loss.
* Slot values at the time (live base `0x00CE0000`): `orig_a=0x011BD330` (= base +
  `0x4DD330`, `sub_8DD330`), `lookup=0x011BD2D0` (= base + `0x4DD2D0`, `sub_8DD2D0`),
  `orig_b=0x00FB61A0` (base + `0x2D61A0`), `case0=0x00FAE140` (base + `0x2CE140`) - **all
  twelve FreezeCarFix slots are published correctly**, so the earlier "off by 0x60"
  hypothesis is retracted.

The value comes from the *game's* own lookup: `sub_8DD330` = `[node+4]` of the map at
`[0xF3E9DC]`/`word[0xF3E9E0]` (built one-shot by `sub_8DD380`, guarded by
`byte[0xF3E9D8]`), or `-1` on a miss; `sub_8DD2D0` is the same lookup plus
`ped_array[[node+4]]`. Neither the map nor its limits are touched by our writes (the only
non-.text writes are `IncreaseVehicleModelLimit` at RVA `0xB3E8E8` and `NoPhysicsSleep` at
`0xC90CA8`), and the probe/guard bodies are byte-identical, so this behaviour is what the
original's code does too. **Decisive confirmation still needed: run the original ASI with
`FreezeCarFix=1` in the same lobby.**

**Fidelity bug found while tracing (inert for the crash, fix anyway):** the
`DebugPrintSpawns` flag global. The original's `byte_10159C15` initial value is `0x31`
(non-zero) and the loader arm only *raises* it to 1 when the key is 1, so the original's
probe takes its print path even with `DebugPrintSpawns=0`; our `rebased_values.cpp` seeds
`zpatch_flag_debug_spawns = 0` and `runtime.cpp` overwrites it from the INI, so our probe
never prints. The print path is side-effect-free (its helpers `sub_10003F00` - a pure
38455-entry name scan, same count as our `model_name_from_hash` - `sub_1005B290`,
`sub_1005B530`), so this changes only whether the GTAC console gets the spawn line; seed
the global with `0x31` for 1:1.

**Live parity harvest from the ORIGINAL's own process (2026-10-18, x64dbg MCP; game base
`0x00CE0000`, original ASI mapped at `0x5B660000` - its installer's E8 dests resolve to
base+`0x37510` (probe) at all six probe sites, base+`0x372F0` (guard) and
base+`0x37330..` (the ten thunks), i.e. exactly the dests our port installs):**

* All twelve FreezeCarFix slots match our published values exactly: `orig_a` =
  base+`0x4DD330`, `orig_b` = base+`0x2D61A0`, the ten case resumes
  base+`0x2CE320/0x2CE260/0x2CE2B0/0x2CE200/0x2CE050/0x2CE0C0/0x2CDF30/0x2CDF90/0x2CE140`
  (+`0x10159C7C`), `class` = 0, `skip` = 0.
* Preamble values match: `dword_1015AD78` (pre-ped array) = base+`0xE2C168`,
  `dword_10159C0C` (model lookup) = base+`0x4DD2D0`, `dword_1015AD70` = the game base.
* Salt seeds match our `rebased_values.cpp` seeds slot for slot (`0x10158028` =
  `6A6B4F18`, `3C` = `022B2DA9`, `40` = `4BF36A32`, `50` = `3B0C6738`, `54` = `511454A9`,
  `5C` = `62E319C6`, plus `A8/B8/C8/D4` verified earlier from the file), and
  `dword_10158058` (the spawn-probe `kind`) = `0xFFFFFFFF`, i.e. our init.
* `sub_8DD330` is unpatched and reads `[base+0xB3E9E0]` / `[base+0xB3E9DC]`; in the live
  original the model-hash map is built (flag `0xB3E9D8` = 1) with a valid pointer
  (`0x230DA020`). The live count word is `0xB3E9E0 = 0x6BB0` (27568), capacity
  `0xB3E9E2 = 0x7148` (29000), and the ped counter `[0x122C15C]` is also 27568. The
  earlier "41000 entries (`0xA020`)" reading was the **low word of the map pointer**
  (`0x230DA020`), not the count; the full live image snapshot from the original's
  process (`gtaiv_memory_direct.bin`) confirms the corrected values. The map is warm in
  both builds and its state matches ours.
* The `DebugPrintSpawns` flag `byte_10159C15` in the live original reads `0x31`-based data
  (the on-disk initial value), confirming our port's `0` seed was a real divergence in
  this family (the other was the cookie shim below).

**FIXED 2026-10-18 - a real fidelity divergence (not the spawn-probe return-value bug):
the option-flag globals' initial values.** Every flag a live body reads has a *non-zero*
on-disk value in
the original (`byte_10159B86`=0x0F, `10159C14`=0x48, `10159B87`=0x34, `10159C15`=0x31,
`1015AD43`=0x39, `1015AD40`=0x82, `1015AD41`=0x39, `10159C17`=0x31, `10158038`=0x01,
`1015BDF4`=0x06), and the loader arms only ever *raise* a flag to 1 - they never clear
one. Our `rebased_values.cpp` seeded them all `0` and `runtime.cpp` overwrote them from
the INI (`key ? 1 : 0`), so with a key off our flag was 0 where the original's was set.
For most flags that is inert (the body is only installed when the key is on, and then both
sides are 1), but `FreezeCarFix` installs a body that *reads* the `DebugPrintSpawns` flag:
in the original the spawn probe takes its print path - and makes its two native-script
calls - on every spawn regardless of that key, while ours skipped it. Fixed: the seeds are
now the original's on-disk bytes and the loader only raises them
(`rebased_values.cpp:148-156` and `:237-239`, `runtime.cpp:213-229`). `msvc-asi` +
`verify-bodies` still pass (76/76 exact bodies - the flags are globals, not bodies).
The spawn-probe return-value divergence was separate: the security-cookie shim clobbered
EAX (see RESOLVED 2026-10-19 below).

**RESOLVED 2026-10-19 - the divergence was the probe's security-cookie shim, not the
map.** The original's `sub_1005FE7B` is `cmp ecx, dword_1015800C; jne ...; ret`, so it
preserves EAX. Our port's shim was a C helper (`zpatch_spawns_cookie_check_c`) that
returned the cookie value in EAX, and the probe calls it **after** the final
`call [zpatch_spawns_orig_a]`. Every probe therefore returned the process's
`__security_cookie` instead of the model index (and instead of 0 on the skip path): the
constant large value seen across all sites/hashes in one run (`0x49627263`, `0x14534DEB`,
...) that changes per process. Crash
dump evidence (2026-10-18 23:11): `EDI=0x49627263` is the value passed to `sub_8B4D60`,
and `0x49627263` is exactly the byte value at the `__security_cookie` slot in the mapped
`ZolikaPatch.asi` `.data` (`asi+0x164C00`, `.data+0xC00`). Fix: the shim is now naked
`cmp ecx, dword ptr [__security_cookie]; jne <int 29h, code 2>; ret`, preserving EAX;
CutsceneFixes chain B (which returns in AL and had the same clobber) tail-jumps to it.
(The original's mismatch path jumps to `__report_gsfailure`; ours issues the same
`__fastfail(2)` class of termination directly. That path is unreachable unless the frame
cookie is corrupted.) The 2026-10-18 flag fix was orthogonal: it changes only the
GTAC-only print path, which GFWL skips in both builds, so the cookie clobber persisted.
Whether the random cookie value lands on a mapped address (silent mis-streaming) or an
unmapped one (the `+0x4B4D7A` fault) explains the crash/no-crash split.
`msvc-asi` + `verify-bodies` still pass (76/76 exact bodies); the phase-3 differential
for the same INI is unchanged (93/93 installs, 0 site/opcode mismatches).
**GFWL note:** the failing sessions are GFWL multiplayer, not GTAC; `GTAC.dll` is absent,
so both builds skip the spawn probe's print path. The print shims are not implicated.

**Verification gaps found while auditing (fix these in the tooling):**

* `verify_hook_bodies.py:37-38` normalises *every* immediates/displacement in the module
  window to `?`, and that window explicitly includes **relative branch targets**. So a
  transcription bug that points a `je`/`jmp` at the wrong *internal label* passes
  verification silently - it is invisible to the game-byte differential as well. An
  audit that compares body-relative internal branch deltas (ours vs the original) over
  all 107 `kAbiBodies` bodies shows the eleven FreezeCarFix bodies (`abi_spawns_guard`,
  the ten `abi_spawns_case_*`, `abi_spawns_probe`) are clean, but **17 compiled bodies
  legitimately differ** (`abi_crash_msg_shim`, `abi_borderless_w/a`, `abi_vram_fix_shim`,
  `abi_game_hash`, `abi_load_dlcs_struct_fixup`, `abi_improved_shader_streaming_find_*`,
  `abi_savegame_fix_*`, `abi_ooc_wrap`, `abi_emissive_lerp_shim`, `abi_driveby_fov_shim`,
  `abi_death_music_*`, `abi_ped_models_in_cutscenes_check`) - rebuilt-in-C bodies whose
  register/flag contract is unverified by construction. Extending `verify_hook_bodies.py`
  to compare internal branch targets (and to report compiled bodies separately) closes
  the first half of that gap.
* Comparing every re-based slot's initial value against the original's on-disk value is
  **not** a usable check: most of the original's slots are stored obfuscated (or are BSS),
  so only the loader-published ones are comparable (those match). A correct check must
  compare against the original's *runtime* values from an `emu_oracle` run.


## 4a. Parity-closure corrections (2026-10-20)

These are not improvements: they are restorations of the original's actual
behaviour, each found by a new differential and each reproducible from the
pinned oracle. The gate that produced them is described in
`docs/parity/notes.md`; `PARITY.md` is generated from its evidence.

### 4a.1 Option-flag seeds read through the wrong `.rdata` file offset

`rebased_values.cpp` seeded eight live option flags with non-zero values:

| global | original symbol | old seed | original image byte |
|---|---|---|---|
| `zpatch_flag_death_music` | `byte_10159B86` | `0x0F` | **0x00** |
| `zpatch_flag_patch4_modchecks` | `byte_10159C14` | `0x48` | **0x00** |
| `zpatch_flag_gfwl_popup` | `byte_10159B87` | `0x34` | **0x00** |
| `zpatch_flag_debug_spawns` | `byte_10159C15` | `0x31` | **0x00** |
| `zpatch_flag_donotpause` | `byte_1015AD43` | `0x39` | **0x00** |
| `zpatch_flag_steering` | `byte_1015AD40` | `0x82` | **0x00** |
| `zpatch_flag_engine` | `byte_1015AD41` | `0x39` | **0x00** |
| `zpatch_flag_plane` | `byte_10159C17` | `0x31` | **0x00** |

The original's loader arms *only raise* a flag to 1; because the on-disk byte is
already 0, an option that is off leaves 0. With the non-zero seeds a disabled
option looked enabled: the Phase-4 state differential showed the original ending
`zpatch_flag_debug_spawns = 0` while the port ended `0x31`, and the G5 claim
that these bytes are "the original's own on-disk bytes" came from reading
through the ASI's `.rdata` file-offset formula applied to `.data`.

The values are `.reloc` bytes: `.data` has `SizeOfRawData=0x1000` against
`VirtualSize=0x504D4`, so every one of these VAs is BSS; mapping them without a
raw-size bound lands in `.reloc` and the byte there is exactly the "seed"
(`0x10159B86 -> 0x158986 -> 0x0F`, `0x10159C15 -> 0x158A15 -> 0x31`, ...;
full table in `docs/parity/notes.md`). A loaded process has 0 at every one.

Correction: seeds are 0. The three globals whose original bytes really are
non-zero are now seeded to match them (`dual_light_a = 1`,
`dual_light_b = 2`, `driveby_fov = 45.0f` = `0x42340000`).

`DebugPrintSpawns` is also read **inside** the FreezeCarFix arm only
(`0x10053FAB..0x10053FE0`: `movzx ecx,[0x10159C15]; cmp esi,1; cmove ecx,1;
mov [0x10159C15],cl; call 0x10037C70`). The port wrote the flag
unconditionally; with `FreezeCarFix=0, DebugPrintSpawns=1` it set 1 where the
original leaves 0. Fixed, and the four-way matrix
`flags_fcf_{0,1}_dps_{0,1}` is now a state cell.

The user-visible MP crash in this area was the cookie shim, not this byte: the
probe returns the model index in EAX past a final `call 0x1005FE7B`, and the old
C shim returned the cookie. Every consumer of `byte_10159C15` only tests zero vs
non-zero, so `0x31` and `1` are indistinguishable to the code. The shim's EAX
contract is now a static helper contract plus a mutation control.

Acceptance: `python tools/parity/state_diff.py --cell probe --ini <any>` shows
zero load-time diffs; `all_off` and `all_on` cells pass.

### 4a.2 Slot publishes bypassed the recovered handler build gate

`apply_recovered_slot_installs` gated on `ini_option_enabled` and the per-row
`SlotInstall.build` field, but not on `build_allows_option` (the handler-level
gate the sig-write and sig-install passes already consulted). Evidence:
`BenchmarkFix`'s handler `0x10005690` starts

```
mov eax, [0x1015BE04]
cmp eax, 9
je  0x100057D9        ; epilogue
cmp eax, 0xD
je  0x100057D9
```

so on 1.0.8.0 it publishes nothing, while the port published
`dword_1015AD50 = 0x0552F64D`. The pass now consults the gate; the state
differential's `benchmark_resume_a` diff is gone.

Note (from `recovered_build_gates.hpp`): a recovered gate is the branch
recovered for that build, not a general applicability predicate. This call site
is safe because the per-build spellings (`@old`/`@new`) already split the
trampoline-target handlers.

### 4a.3 Fault-probe gate could not distinguish a blocked child

`zpatch-test` launched probe children with `std::system`, whose exit code goes
through `cmd.exe`. A child blocked by Defender (immediate `0xC0000005` on load)
and a child that ran and returned 1 were indistinguishable; session 17 recorded
a phantom `DIAG fault-probe fchs-null-lookup exit=1`. The driver now uses
`CreateProcessW` + `GetExitCodeProcess`, prints `launched=<0|1>` and asserts
`0xC0000005`. All six probes fault.

### 4a.4 Verification gaps this closes

The earlier note in this file said `verify_hook_bodies.py` normalised branch
targets to `?` (a wrong internal label passes silently) and that a correct
global check must compare the original's *runtime* values. Both are now
covered:

* `tools/parity/verify_bodies.py` compares body-relative branch deltas and
  resolves every module operand through a pinned binding table;
  `tools/parity/mutation_static.py` proves the comparison is not vacuous.
* `tools/parity/state_diff.py` compares the original's post-load globals (its
  emulated write log replayed onto a section-mapped image) with the port's,
  per INI cell.
* `tools/parity/body_equiv.py` gives compiled bodies a runtime differential;
  bodies without a PASS are `UNPROVEN` in the ledger, not assumed equivalent.


### 4a.5 Byte-exact closure of the hand-written bodies (2026-10-20)

Fourteen bodies that were hand-written asm but not opted into the byte
comparison are now `@exact` and pass operand-for-operand (76 -> 90 exact):

`abi_benchmark_a/b`, `abi_bike_physics_detour`, `abi_building_dynamic_shadows`,
`abi_copsirens_a/b/ide`, `abi_newfiles_register_body`, `abi_super_lod_fix_shim`,
`abi_crash_msg_shim` (missing 2-byte `66 90` alignment nop),
`abi_load_dlcs_struct_fixup` (shared epilogue + direct `call [callback]`),
`abi_vram_fix_shim` (helper extracted to `abi_vram_fix_helper`),
`abi_load_dlcs_dummy` and `abi_rgsc_login_removed`.

The last two are a genuine 1:1 fix the runtime differential found: the original
is `mov al,1; ret` (only AL is written; the upper 24 bits of EAX are preserved),
while the C stub compiled to `mov eax,1`. A caller reading the full EAX saw
`0xFFFFFF01` vs `0x00000001`. Both are now naked `mov al,1` transcriptions.

Remaining non-exact bodies: `abi_driveby_fov_shim` and
`abi_emissive_lerp_shim` cannot be byte-exact because MSVC inline asm cannot
emit an SSE instruction with a relocatable absolute displacement (the codebase's
workaround is a register-held address, which changes the encoding); they are
DIFFERENTIAL and must be proved by the runtime differential.


### 4a.6 Parity-closure pass: 103/107 bodies, 19/19 state cells (2026-10-20)

Historical snapshot as of 2026-10-20. Its runtime statuses were superseded by
the 2026-10-21 SEH/OOC/cutscene closure in §4a.11 below.

* Byte-exact closure of fourteen more hand-written bodies (see 4a.5).
* Compiled-body differential: the remaining contract differences were fixed or
  proven to be harness artefacts; `ped_models_in_cutscenes_check`,
  `improved_shader_streaming_find_hpe/_v`, `newfiles_update_a/_b` and
  `driveby_fov_shim` now PASS. `driveby_fov_shim`'s helper was extracted
  (`abi_driveby_fov_helper`) so the shim's `call` matches the original stack
  shape; the SSE absolute load uses a pushed/popped EAX base so no GPR the
  resume consumes is clobbered.
* `speedup_body`: the `match ? call : 0` and pointer-null guards were removed
  (fault-preserving). It stays UNPROVEN because the original resolves the clock
  through the PEB/TLS module slot and the emulator cannot reproduce that scan.
* CrashMsgFix: `p3-dump` now rebases the game pointer embedded at `match-4`
  (the `ptr_site_field` row) onto the mapped image when the driver has to map
  the game on the heap. This is what the real 0x400000 mapping would do; the
  `zpatch_crash_msg_getter` state row now matches.
* Fault probes: added `newfiles-null-register` (P11) and `ooc-null-logger`
  (P7); eight probes now assert 0xC0000005. P6/P9/P12/P13 still need a live
  process (documented), P14/P15 are layout/heap-unreproducible.
* Interaction cells: `pair_carparts_dynamic`, `pair_carparts_both`,
  `pair_removeuseless_miscfixes`, `pair_removeuseless_all` (19/19 cells PASS).
* Flight recorder: `tools/parity/flight_recorder.py` converts a ZZWatch session
  into an ordered trace and compares original vs port (module-relative
  addresses, ordered writes and calls). Self-tested on real logs; live capture
  stays user-driven.


### 4a.7 Helper-level contracts (2026-10-20)

* The `/GS` epilogue supplies the flags seen at a shim resume (`pushal/popal`
  does not restore EFLAGS). C replacements now end through
  `zpatch_cookie_tail()` (`abi_nolivery_hash_sign`,
  `zpatch_local_settings_step`, `zpatch_garage_lookup_model`), and
  `zpatch_garage_store_model` is transcribed (`shr eax,0x18` supplies the
  original flags). This closes the whole flags category.
* `abi_newfiles_register` is a five-instruction transcription of
  `sub_100515E0`; the C wrapper's prologue changed ESP and therefore PF in the
  trailing `add esp,8`.
* Harness defects fixed (not product changes): the vsprintf stub's argument
  indices, code bindings being initialised as globals, and module-self callback
  pointers in call records.


### 4a.8 Harness array/fault fixes (2026-10-20)

Name-hint arrays: scalars whose names contain `saves`/`salt_`/`path_` were
initialised as 64-byte arrays, zeroing neighbours (FCHS `hashes[0]`). Arrays are
now declared-size driven and written last. Fault comparison normalises the EIP
(body-relative) and treats a fault inside a called helper as a class, not a VA.
These are harness-only; with them, runtime PASS is 72/107 and helper-level open
rows are 31 (12 observable).


### 4a.9 Read-only bindings (2026-10-20)

`body_equiv` used to initialise bindings in any non-executable section, which
includes import-table slots. Overwriting the `VirtualProtect` IAT entry made a
body's `call [IAT]` fetch unmapped memory. State is now restricted to writable
sections; read-only constants and IAT slots are left as loaded. This is a
harness fix, not a product change.


### 4a.10 Parity-closure continuation (2026-10-21)

The following product changes are transcriptions of original contracts, not
behavior improvements:

* `abi_bad_player_model` / `abi_bpm_check`: original sub_10037AC0 keeps ECX at
  the models-table base through `call [vtable+0x0c]`; it does not set ECX to the
  selected model object. The port now passes the table base to the class call.
  This fixes the 1:1 register contract and the subsequent model-index update.
* `abi_ide_parse`: removed the port's null-string early return. The original
  sub_10003FA0 dereferences `zpatch_ide_string` before any check, so null must
  fault. The parser arms are independent as in the original. No fault-avoidance
  guard was added.
* Local settings: sub_10050D10 writes `\\saves\\GTA IV\\` to `path_18`, then
  `\\saves\\<episode>\\` to the ANSI and wide episode buffers. It does not
  prepend the process CWD. The port now builds those exact relative paths
  directly, avoiding the prior `snprintf`/`swprintf` output divergence.
* `abi_nolivery_copy_name`: original sub_1003D2F0 calls imported
  `strcpy_s(dest, 0x10, bufptr)` and has neither a null check nor a `/GS` cookie
  tail. The port now uses that contract rather than truncating `_strncpy` and
  preserves the original's null/fault outcome.
* Script-call dispatch: sub_100038E0 calls dword_1015BDE0 with ECX=hash and
  EDX=call-block. The port's dispatcher now passes both. The unary
  sub_10003980 caller still leaves EDX as scratch, which the resolver wrapper
  preserves. The emulator curates the returned handler slot only to terminate
  the synthetic script walk.
* GFWL and speedup scan caches: the original uses MSVC thread-safe-static
  guards at dword_101A8480 / dword_101A84C8 and caches at dword_101A84C4 /
  dword_101A849C. The port now uses those globals and the shared TlsIndex
  dword_10159328; its header atomically claims guard 0, waits on the in-progress
  sentinel, and synchronizes a new thread's TLS epoch as sub_1005FFC3 does. Its
  footer increments dword_10158000 and updates the current TLS epoch as original
  sub_1005FF77 does. This removes port-only function-local cache writes and
  retains the one-time scan semantics.
* LoadDLCs: the exact trampoline remains byte-identical. The C++ post-helper
  leaves EDX=0x40 after its final `sub_10001280`-equivalent iteration, matching
  the original helper's final buffer-size register at the resume point. It now
  uses the original 0x104-byte `operator new` buffer, ignores the
  `GetModuleFileNameA` return, calls `PathRemoveFileSpecA`, and uses bounded
  `strcpy_s`/`strcat_s` paths. The null-result and null-object early returns
  were removed; registration remains gated only by the original path-exists
  branches.
* MouseFix `sub_100241E0`: the port no longer guards X/Y pointers and reads Y
  then X, matching the original `dword_10159C50`/`dword_10159C3C` access order.
* DrivebyFOV P13: sub_10003980's recovered filter returns 1 and its handler
  returns EAX=0 on a resolver exception. The port now wraps only that resolver
  call in the same catch-all x86 SEH behavior; the caller then faults at the
  original `[eax+0x0E]` dereference. `tools/parity/driveby_seh_probe.cpp`, built
  by `driveby_seh_probe.cmd`, verifies the inner AV is swallowed and the next
  read AV is at address `0xE` on the follow-up dereference instruction.

Harness-only changes: stub/API writes are captured in order (Python memory
writes are not seen by Unicorn's guest write hook), contiguous non-overlapping
stores coalesce by bytes to normalise imported-vs-inlined CRT copies, element
writes map through declared array extents, empirical bindings inherit header
widths, and `stack`/`stack_pad`/`arena_bytes`/call-cleanup inputs are available.
The body emulator neutralizes only port log emitters; logging is not original
behavior and its mutex/CRT/file initialization polluted the effect trace.

Final generated evidence (8 seeded iterations): **107/107 runtime PASS**, 0
MISMATCH, INCONCLUSIVE, or INPUT-LIMITED rows. Ledger row statuses are 107
PASS, 0 SANCTIONED, 0 UNPROVEN; helper-level open rows are 0. The OOC runtime
proof includes the decoder cursor, fake-pak vector publication/reset and
archive-offset writes; the cutscene proof initializes the port-only
VirtualProtect function pointer from its mapped IAT entry. State cells are
19/19, options 99/99, bindings 213/213, and supported-build checks 9/9. The
full `parity` gate also reports deterministic SHA-256 matches for bindings,
static proof, and body effects (3/3), plus a passing offline x86 P13 SEH probe.
This is completeness for the declared 107 hook bodies and associated parity
corpus, not a claim that every function in the original ASI has been
decompiled. Builds 1061, 1100 and 12043 remain excluded because no usable oracle
binaries are pinned.


### 4a.12 Live startup crash fix: MSVC TLS-static guard (2026-10-21)

The 21:29 ASI crashed before the game window with an access violation at
0xFFFFFFCD. That address is `0 - 51`: `abi_speedup_body` calls its cached clock
at `match - 51` after a one-time scan, and the cache was null because the scan
never ran. The guard is the MSVC thread-safe-static protocol used by original
sub_10050587 (speedup) and sub_10005354 (GFWL patch4):

```
if ((int)guard > (int)*(TLS[_tls_index] + 4)) {
    _Init_thread_header(&guard);
    if (guard == -1) { scan; cache = match; _Init_thread_footer(&guard); }
}
```

The rebuild had hand-rolled the guard using a fake `zpatch_tls_index` global
(always 0), so `TLS[0] + 4` was an unrelated block's epoch. On the first frame
that could look "already initialized", skipping the scan. The port now declares
a `__declspec(thread)` anchor (creating a real TLS directory), reads the
loader-assigned `_tls_index`, and calls the CRT's own `_Init_thread_header` and
`_Init_thread_footer`. `dllmain.cpp` publishes `_tls_index` into the mirrored
`zpatch_tls_index` global. The hand-maintained epoch counter model was removed.
No fault-avoidance guard was added: if the pattern is genuinely absent, the
faithful `clock - 51` call must fault exactly as before.

The pattern ("1B D9 89 5C 24 24 0F 88") is present in GTAIV.exe at image offset
0x56F93 and matches the original's pattern string at 0x1012ED78, so a correct
guard run finds the same function. A diagnostic log line now prints the scan
result (`SpeedupBugFix: clock scan match=... clock=...`).

Harness-only: `tls_state` also seeds the port's `__tls_index`, and each
`body_equiv` Side releases its Unicorn hooks/mappings after use (the 16-seed
corpus previously accumulated them; a run could exhaust memory).

### 4a.11 Final hook-body proof (2026-10-21)

The full `make parity` gate now reports 107/107 hook-body PASS, with 90 exact
instruction-for-instruction bodies and 17 compiled-body differential PASS rows;
there are 0 MISMATCH, INCONCLUSIVE, INPUT-LIMITED, SANCTIONED or UNPROVEN body
rows. `helper_gaps.json` is 0/0 open. The standard 8-seed corpus is deterministic
across two runs for the binding map, static proof and body effects (3/3 SHA-256
matches). An extended 16-seed run also passes 107/107; its repeated result hash
matches at `9c4c02907b8f376e`.
Static mutation checks detect branch-delta, wrong-global, immediate, and
helper-clobber mutations. The offline x86 P13 probe confirms argument/result
preservation, catches a resolver AV, and observes the next AV at the original
caller dereference, read address 0xE.

OOC uses a bounded 4,000,000-instruction cap because the faithful decoder is
larger than the generic cap; all eight runs terminate and match. The WinEmu
`_errno` model now returns a stable per-emulator TLS slot, matching the CRT API
contract rather than allocating a new heap object on every call. Cutscene's
port-only function pointer is initialized from the same mapped VirtualProtect
IAT slot its in-game initializer reads. A positive DLC fixture covers the TLAD
fallback, TBoGT content and Episode 3/254 registration paths. The 16-seed run
exposed and closed TLS epoch-state inputs, mouse axis pointers, and a collision-
walk cap that needed to reach the original's 10,000-step terminator.

The separate fault ledger has 9/15 probe/static cases proven. P6/P7/P9/P11 and
the P14/P15 layout-dependent outcomes remain UNPROVEN for their injected
fault/adjacent-memory subcases; normal-input body PASS does not prove them.

This establishes completeness only for the declared 107 hook bodies and their
tested helper effects. ZolikaPatch's unconditional ZMenuIV `GetZMenuVersion`
guard is implemented in DllMain and its threshold is tested; the separate
ZMenuIV plugin's internal feature bodies are not part of this audit. The
deterministic finite corpus is strong reproducible evidence, not a formal proof
over every possible game state. It does not claim decompilation of every
internal function in the original ASI or binary equivalence on the three
recognized but unverified builds (1061/1100/12043).
