# Progress — readable-fusion

Newest last. One entry per work session; the exact gate output is what counts.

## 2026-11-06 — the native-script call layer, and DrivebyFOVFix

**Closed:** `DrivebyFOVFix` (78 -> 79 keys on 1.0.8.0). The key was blocked on the
original's native-script call layer; that layer now exists as `source/native.ixx`.

**What was built**

- `source/native.ixx` — the layer. Five pieces transcribed as `static
  __declspec(naked)` bodies and proven whole:
  `sub_100038C0` (5), `sub_1005B3F0` (46), `sub_1005B150` (44), `sub_1005DE40`
  (21), `sub_1005DE80` (21). Two pieces rebuilt as C because they are compiler
  output for `__try`/`__except` functions whose SEH scope table names
  image-specific handler addresses: `sub_100038E0` (59) and `sub_10003980` (52).
  The rejected-`memset` call inside the two wrappers (inline asm cannot name the
  intrinsic, C2420) goes through a local `NativeZeroBlock`.
- `Native::Register()` resolves `dword_1015BDE0` exactly as the original's loader
  preamble does (`0x10052E88..0x10052E9E`): scan the game image for
  `08 0B C2 8B F0 E8`, then `match + 10 + [match + 6]`. On the 1.0.8.0 image that
  is `0x626AF0`, the game's native-handle resolver — verified through the real
  code path in `zp_dump` (resolver RVA `0x226AF0`, probe removed afterwards).
- The layer's ABI: hash in **ESI** for the game resolver (C++ cannot ask for a
  register, so a naked `ResolveInEsi` carries it), block in EDX, handler called
  cdecl with the block.
- `source/misc.ixx` — the `DrivebyFOVFix` arm: `PublishDrivebyFOVFix` reads the
  site's own `subss xmm1, xmm2` at the anchor's +5 for the resume; the install
  jumps to the transcribed shim (`sub_100064B0`, 5) whose helper (`sub_10006450`,
  20) resolves the FOV native through the layer. Write set on 1.0.8.0: an `E9` at
  RVA `0x45031F` plus its displacement to `0x100064B0`, the original's bytes.
- `CMakeLists.txt` (one module line), `source/dllmain.cpp` +
  `tests/dump_main.cpp` (`import native;` and `Native::Register()` before the
  feature registrations), `tests/bodies.tsv` (7 new rows, 62 total),
  `tests/signatures.tsv` (DrivebyFOVFix site + the layer's resolver scan
  comment), `docs/parity.md` (layer section, status, blocked list, call-target
  substitutions).

**Gates (final runs)**

- `cmake --build --preset release` — success.
- `python tools/parity_report.py 9` — `build 9: 79 key(s) closed, 20 open`
  (`OPEN DrivebyFOVFix` is gone; was 78 closed).
- `ctest --preset release` — `100% tests passed out of 9`.
- `python tools/verify_bodies.py ...` — `62 body(ies), 0 failure(s)`.

**Not done / not verifiable here**

- No game launch: the in-game behaviour of the resolver walk and the native call
  is transcribed/proven but not executed against the running game.
- `AGENTS.md`'s "Current state" still says 78 keys / 52 bodies and is now stale;
  it was outside this task's edit scope.
- `BadPlayerModelCrashFix` still needs the GTAC.dll branch
  (`sub_1005B290`/`sub_1005B530`/the sprintf chain) and the original's
  38,455-entry model-name table; the layer it needed is now available.

## 2026-11-06 — the misc keys: SpeedupBugFix, Patch4RemoveModChecks, CopSirensFlag, PopupHeadlightsFlag

Four catalogue keys in `source/misc.ixx` closed on 1.0.8.0 (80 → 84 keys,
67 → 73 proven bodies).

- `SpeedupBugFix`: the arm replaces the game's timer init (`0x4571B0`, anchor
  `50 64 89 25 …` at `0x4571C5`) and publishes the eight absolute float pointers
  at the anchor's `+26/+165/+188/+226/+234/+249/+264/+276`. The body
  (`sub_10050540`, 109 instructions) is compiled C (SEH + /GS + the thread-safe
  static guard around its own clock scan), so it is reimplemented in C++ with the
  original arithmetic (`1000.0 / QueryPerformanceFrequency`, then the four
  `scale`/`scale*1000`/`scale*0.001…`/`0.001…/scale` floats and their four
  copies) and the game clock at scan-match − 51. **No proof row** (documented in
  `docs/parity.md`); the arm still registers `0x10050540` for the dump.
- `Patch4RemoveModChecks`: the 8-instruction shared stub `sub_100054E0` is
  transcribed and proven; `misc.ixx` owns only the Patch4 branch of
  `sub_10005340` (one-time scan for `33 C4 89 84 24 A8 01 …`, then zero the
  dword at site+30). That signature has **no hit on 1.0.8.0**, so the branch is a
  scan and nothing else there; the GFWL branch stays in `loading.ixx`.
- `CopSirensFlag`: three transcribed bodies (8/8/9) at `0x10006310`,
  `0x10006330`, `0x100040E0`; the checkers (`sub_10005F70` 191,
  `sub_10006260` 53, `sub_10003FA0` 92) are compiled C and rebuilt in C++.
- `PopupHeadlightsFlag`: two transcribed bodies (10/10) at `0x10044530` /
  `0x10044560` (the EDX twin's pattern is absent on 1.0.8.0; the body is still
  proven) and the rebuilt `sub_10044310` (125). The `fld` operand at the
  anchor's `+82` is retargeted to our pi/2 float; `Bodies::RegisterPublished`
  makes the dump write the baseline's `0xD0000000` placeholder.
- Both siren/popup keys install the same IDE hook (`0x100040E0` + `sub_10003FA0`)
  at `0xA46546`, as the original's two handlers do. The `[PopupLightAngles]` /
  `[HeadlightConeOffsets]` INI table parse is still not in this tree (the
  shipped sections are empty; recorded in `docs/parity.md`).
- Recorded split: the original's one helper reads both the Patch4 and GFWL flags;
  this tree has one helper per module, and the misc arm installs later, so a run
  with both keys enabled performs Patch4 but not the GFWL writes. Each key alone
  is 1:1.

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` → `build 9: 84 key(s) closed, 15 open`;
`ctest --preset release` → `100% tests passed out of 9`;
`python tools/verify_bodies.py …` → `73 body(ies), 0 failure(s)`.

**Not done / not verifiable here**: no game launch (the shims/rebuilt helpers are
proven at instruction level where transcribed, but the rebuilt helpers' live
behaviour is not executed); `AGENTS.md`'s "Current state" still says 79/62 and is
outside this task's file scope, so the parent must refresh it (84/73 now).

## 2026-11-06 — the loader preamble's salt rule, and the two wrong constants

**What was wrong:** `Native::CallBool`'s body read a slot seeded with `0x62BFDB37`
and `DrivebyFOVFix`'s helper read its own slot seeded with `0x09399528`. Both are
the original preamble's *probe-miss* values; on 1.0.8.0 the probe hits, so the
original runs with the shipped `.data` values (`0x4E61480A` for `dword_101580B8`,
`0x75E005F1` for `dword_101580D4`).

**The recovered rule** (IDA `idZolikaPatch.asi.i64`, preamble `sub_100523C0`):

```
10052e10  mov  ecx, offset "68 ? ? ? ? 68 A9 54 14 51 E8"  ; 0x1012B710
10052e15  call sub_10003830                                 ; pattern scan
10052e1a  test eax, eax
10052e1c  jnz  short loc_10052E82                           ; hit: skip all ten stores
10052e1e..10052e78  ten `mov dword_10158xxx, imm`           ; miss: fallback set
10052e88  ... resolver scan
10052e96  mov ecx,[eax+6]; add eax,0Ah; add eax,ecx; mov dword_1015BDE0,eax
```

A hit copies nothing from the game image; it jumps past every store, leaving the
ASI's `.data`. The ten slot/value/consumer rows are in `docs/parity.md`. On the
nine images: 1.0.8.0 hits at game `0xB3878C`; 1.0.0.0 and 1.0.4.0 miss (the other
six supported images also hit); misses substitute `0x62BFDB37` / `0x09399528` /
`0x406B4B20` / `0xC9DBDA90` for B8/D4/C8/40.

**Changes**

- `source/native.ixx` — ten `zpatch_native_salt_10158xxx` words seeded with the
  shipped values; `Native::ResolveSalts()` scans the probe and applies the miss
  set; `Native::Register()` calls it before any arm. `NativeCallBoolBody` reads
  the resolved B8 slot; `Native::Salt(reference)` exposes a slot for the tools.
- `source/misc.ixx` — only the DrivebyFOVFix salt declaration/comment and one
  `mov ecx` operand: the helper now reads the layer's D4 slot instead of a local
  `0x09399528`.
- `source/stability.ixx` — `BadPlayerModelCrashFix`'s duplicate 46-instruction
  `0x1005B3F0` body is retired. `BpmCheckBody` calls the layer's copy through a
  six-byte `jmp [zpatch_bpm_call_bool]` forwarder (slot filled by
  `RegisteredBody(0x1005B3F0)`), its local B8 salt and the now-unused
  `BpmCleanupBoolEntry`/`zpatch_bpm_cleanup_bool` are gone; C8/40 stay local.
- `tests/salt_check.cpp` + `zp_salt_probe` (new 10th ctest): loads
  `GTAIV.exe`, `GTAIV_1000.exe` and `GTAIV_1040.exe`, calls
  `Native::ResolveSalts()` and asserts the probe branch and all ten slots per
  image; 33 checks. Also disassembly-confirmed in the built ASI that
  `NativeCallBoolBody` reads slot `0x1014714C` (B8) and `DrivebyFovHelperBody`
  reads `0x10147160` (D4).
- `tests/bodies.tsv` — `bad_player_model_call_bool` row removed (73 -> 72).
- `docs/parity.md` — the rule table, the two fixes and the retirement; body count
  73 -> 72; public surface adds `Salt`/`ResolveSalts`.

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` -> `build 9: 84 key(s) closed, 15 open`
(exit 1, as always with `SkipLauncher` open by construction);
`ctest --preset release` -> `100% tests passed out of 10`;
`python tools/verify_bodies.py ...` -> `72 body(ies), 0 failure(s)`.

**Not verifiable here**: the helper's live FOV arithmetic and `Native::CallBool`
against the real resolver need a running game; the input value and the body's
slot operand are what were proven. `AGENTS.md` still says 73 bodies (now 72) and
is outside this task's file scope.
