# Progress â€” readable-fusion

Newest last. One entry per work session; the exact gate output is what counts.

## 2026-11-06 â€” the native-script call layer, and DrivebyFOVFix

**Closed:** `DrivebyFOVFix` (78 -> 79 keys on 1.0.8.0). The key was blocked on the
original's native-script call layer; that layer now exists as `source/native.ixx`.

**What was built**

- `source/native.ixx` â€” the layer. Five pieces transcribed as `static
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
  is `0x626AF0`, the game's native-handle resolver â€” verified through the real
  code path in `zp_dump` (resolver RVA `0x226AF0`, probe removed afterwards).
- The layer's ABI: hash in **ESI** for the game resolver (C++ cannot ask for a
  register, so a naked `ResolveInEsi` carries it), block in EDX, handler called
  cdecl with the block.
- `source/misc.ixx` â€” the `DrivebyFOVFix` arm: `PublishDrivebyFOVFix` reads the
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

- `cmake --build --preset release` â€” success.
- `python tools/parity_report.py 9` â€” `build 9: 79 key(s) closed, 20 open`
  (`OPEN DrivebyFOVFix` is gone; was 78 closed).
- `ctest --preset release` â€” `100% tests passed out of 9`.
- `python tools/verify_bodies.py ...` â€” `62 body(ies), 0 failure(s)`.

**Not done / not verifiable here**

- No game launch: the in-game behaviour of the resolver walk and the native call
  is transcribed/proven but not executed against the running game.
- `AGENTS.md`'s "Current state" still says 78 keys / 52 bodies and is now stale;
  it was outside this task's edit scope.
- `BadPlayerModelCrashFix` still needs the GTAC.dll branch
  (`sub_1005B290`/`sub_1005B530`/the sprintf chain) and the original's
  38,455-entry model-name table; the layer it needed is now available.

## 2026-11-06 â€” the misc keys: SpeedupBugFix, Patch4RemoveModChecks, CopSirensFlag, PopupHeadlightsFlag

Four catalogue keys in `source/misc.ixx` closed on 1.0.8.0 (80 â†’ 84 keys,
67 â†’ 73 proven bodies).

- `SpeedupBugFix`: the arm replaces the game's timer init (`0x4571B0`, anchor
  `50 64 89 25 â€¦` at `0x4571C5`) and publishes the eight absolute float pointers
  at the anchor's `+26/+165/+188/+226/+234/+249/+264/+276`. The body
  (`sub_10050540`, 109 instructions) is compiled C (SEH + /GS + the thread-safe
  static guard around its own clock scan), so it is reimplemented in C++ with the
  original arithmetic (`1000.0 / QueryPerformanceFrequency`, then the four
  `scale`/`scale*1000`/`scale*0.001â€¦`/`0.001â€¦/scale` floats and their four
  copies) and the game clock at scan-match âˆ’ 51. **No proof row** (documented in
  `docs/parity.md`); the arm still registers `0x10050540` for the dump.
- `Patch4RemoveModChecks`: the 8-instruction shared stub `sub_100054E0` is
  transcribed and proven; `misc.ixx` owns only the Patch4 branch of
  `sub_10005340` (one-time scan for `33 C4 89 84 24 A8 01 â€¦`, then zero the
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
`python tools/parity_report.py 9` â†’ `build 9: 84 key(s) closed, 15 open`;
`ctest --preset release` â†’ `100% tests passed out of 9`;
`python tools/verify_bodies.py â€¦` â†’ `73 body(ies), 0 failure(s)`.

**Not done / not verifiable here**: no game launch (the shims/rebuilt helpers are
proven at instruction level where transcribed, but the rebuilt helpers' live
behaviour is not executed); `AGENTS.md`'s "Current state" still says 79/62 and is
outside this task's file scope, so the parent must refresh it (84/73 now).

## 2026-11-06 â€” the loader preamble's salt rule, and the two wrong constants

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

- `source/native.ixx` â€” ten `zpatch_native_salt_10158xxx` words seeded with the
  shipped values; `Native::ResolveSalts()` scans the probe and applies the miss
  set; `Native::Register()` calls it before any arm. `NativeCallBoolBody` reads
  the resolved B8 slot; `Native::Salt(reference)` exposes a slot for the tools.
- `source/misc.ixx` â€” only the DrivebyFOVFix salt declaration/comment and one
  `mov ecx` operand: the helper now reads the layer's D4 slot instead of a local
  `0x09399528`.
- `source/stability.ixx` â€” `BadPlayerModelCrashFix`'s duplicate 46-instruction
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
- `tests/bodies.tsv` â€” `bad_player_model_call_bool` row removed (73 -> 72).
- `docs/parity.md` â€” the rule table, the two fixes and the retirement; body count
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

## 2026-11-06 â€” RestoreDeathMusic and MouseFix (audio.ixx)

**Closed:** `MouseFix` (84 -> 85 keys on 1.0.8.0 in this module's share; the tree
total was 88 after another pass closed three loading keys). `RestoreDeathMusic` was
already *counted* closed â€” its whole baseline cell is branch displacements, which
the scoreboard excludes â€” and now has its arm.

**What was built (source/audio.ixx, 12 new proven bodies)**

- `RestoreDeathMusic`: the gate body `0x10050A50` (41) transcribed, plus the three
  native-script wrappers its calls need, transcribed here because the layer does
  not export them: `0x1005B1E0` (48, two-argument, salt `0x10158054`),
  `0x1005B490` (46, byte, salt `0x10158028`), `0x1005B360` (44, zero-argument
  bool). They reach `0x100038C0`/`0x100038E0`/`0x1005DE40`/`0x1005DE80`/
  `0x1005B150` through jmp forwarders filled from the body registry. The five salts
  are copied from `Native::Salt(ref)` at registration, never literals. The install
  body `0x10050AD0` is **rebuilt in C++** (no proof row): it is compiler output
  with an EH4 frame (handler `0x100619C1`, scope table `0x10154384`) and a
  clock/seed block calling CRT-internal helpers (`0x1005FFC3`, `0x1005FF77`) that
  read `fs:[2Ch]` and the TLS index `dword_10159328`.
- `MouseFix`: all eight pieces transcribed â€” `0x10024060` (30), `0x100240C0` (23),
  `0x10024120` (22), `0x10024170` (28), `0x10024440` (62), `0x10024560` (25),
  `0x100243A0` (23), `0x100241E0` (138). None carries an EH frame, and the advance
  helper's only indirect call is the game's `VirtualProtect` through a published
  slot (the original's `FF 15`). The arm writes the reference's 25-write set: five
  `E9`s, two `E8`s, five NOP pairs and five 4-byte `movd` writes, at the ten
  recorded sites.
- `tests/bodies.tsv` +12 rows (96 total), `tests/signatures.tsv` +12 site rows (all
  verified unique on `game_refs/GTAIV.exe`), `docs/parity.md` gets both key
  sections and the `OutOfCommissionFix` blocker.

**Measured correction to the brief**: `sub_10050A50` is 41 instructions and
`sub_10050AD0` is **75** (0x10050AD0..0x10050BC8), not 91; 91 is the *handler*
`0x10050BD0`'s count (0x10050BD0..0x10050D09), which is not a body.

**Skipped â€” `OutOfCommissionFix`, exact blocker**: the payload is 163,796 bytes of
`.rdata` at `0x100B6798` decoded into a 179,536-byte table; carrying it needs a new
~1 MB recovered-data file outside this pass's edit set, and the code around it
(`sub_1004C3F0`, 16,311 bytes; `sub_1004B750`; the archive builder) is C++ with EH4
frames, `std::string` SSO temporaries and CRT import calls, i.e. a new module to
rebuild. The `E9` was left uninstalled as instructed: it only widens the game's
script check to accept the magic the missing table produces.

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` -> `build 9: 88 key(s) closed, 11 open` (exit 1,
`SkipLauncher` open by construction); `ctest --preset release` -> `100% tests
passed out of 10`; `verify_bodies.py` -> `96 body(ies), 0 failure(s)`; the full
1.0.8.0 cell still has `0 differing`.

**Not verifiable here**: no game launch â€” the death-music gate's three native calls
and the mouse axis arithmetic are proven at instruction level where transcribed, but
the rebuilt install body's runtime scan/`rand()` and the in-game mouse feel are not
executed. `AGENTS.md`'s "Current state" (84 keys / 72 bodies) is outside this task's
file scope and is stale.

## 2026-11-06 â€” the three loading keys: NewMemorySystem, NewFilesCompatibility, LoadDLCs

**Closed:** three of the four remaining loading keys: 84 -> 87 keys on 1.0.8.0 from
this pass (the gate reads 88 because `audio.ixx`'s MouseFix landed in the same
window). Fourteen new proof rows of mine (72 -> 86); the tree's total at the final
gate is 98 with the audio rows. The fourth key, `NewSavesCompatibility`, is reported
below with its blocking condition named, not attempted.

**What was built (source/loading.ixx)**

- `NewMemorySystem`: the original handler's two writes (the allocator-init `call` at
  RVA `0x107A` retargeted to the vtable installer, and a jump at `0x107F` skipping the
  five-slot clear, resuming at `0x108C`), the singleton published from the
  `6A 01 B9 00 00 40 06` anchor's `mov esi, <address>` operand (`0x19CF9F0`, rebased
  through `GamePointer`), and the 24-slot vtable read entry by entry from the
  original's `0x100B5038`. Eleven methods transcribed and proven (ctor 17,
  reject-pointer 2, self 2, null 2, `ret 0` 1, paired no-op 1, reject 2, MP-sync 2,
  slot-18 thunk 5, no-arg no-op 1, installer 5); the three arena methods rebuilt in
  C++ with the original's alignment scheme (`unit = max(0x10, align)`,
  `want = max(size, unit)`, `HeapAlloc(want + 2*unit)`, `((base-1)/unit + 2)*unit`,
  the distance at `[p-4]`, `memset(0xCD)`, registry under a critical section; free
  recovers the base; membership query). Slots 10/12 reuse the RGSC-login stub and the
  savegame null path this unit already owns, as the original's table does; slots
  8/9/20/21 get a second copy of the 2-instruction MP-sync stub because `audio.ixx`
  keeps its body internal.
- `NewFilesCompatibility`: the original's gate (`GTAC.dll` / `JacksIVFixes.asi`, in
  both publish and install), the registrar's call operand published from
  `68 64 28 C8 67 E8` (`match + 10 + disp` = `0x625620`), the two update-call
  retargets and the registrar `ret` jump. The registration tail (`pushad/call/popad/
  ret`, 4) is transcribed and proven; the two update bodies are rebuilt with the
  original ABI and call order, hashing through `Shared::ValueResolver()`
  (`sub_10003E50`), and the three helpers use the original's own signatures
  (`8B 35 ? ? ? ? 6A 00 50 E8 â€¦`, `85 C9 53 55 56 57 74 40 â€¦` /
  `53 55 8B 6C 24 0C 56 57 EB 06`, `8B 0F 85 C9 74 ? 0F B7 41 0A 66 85 C0` minus
  `0x14`), unguarded on a miss as the original is.
- `LoadDLCs`: the handler does not scan â€” it switches on the build id and writes six
  fixed-RVA sites per build. The rows were read from the handler's own `cmp eax, 7/8/9`
  blocks and checked in all three images (1060/1070/1080, both the site shapes and the
  dump's write sets for builds 7 and 8). Three entry points published per build
  (register/apply as RVAs, the loader callback absolute). Bodies `0x10023480` (2) and
  `0x10023490` (19) transcribed and proven; `sub_10023150`, the discovery, rebuilt in
  C++ with the original's call sequence (unchecked 0x104 module-path buffer,
  GetModuleFileNameA + PathRemoveFileSpecA, the TLaD id-2 / TBoGT id-4 groups with the
  content/legacy fallback and the original's second probe, 252 `\Episode3\`..`\Episode254\`
  probes with id = 2n, the 0x168-byte request with mode at +344 and flag at +358, then
  the object's +0x166 = 2).
- **The EDX contract the earlier attempt refused is unobservable**, and that is
  measured, not assumed: `sub_10023150` returns with EDX = its last callee's (the only
  instructions after the last call are the object store, pops, `xor ecx,esp`,
  `call __security_check_cookie` â€” ECX only â€” and the epilogue); the body passes it
  through untouched; and its only caller (game `0x4931FA`) is immediately followed by
  `call 0x8CCCD0`, which reads neither EAX nor EDX, while no other reference to
  `0x890320` exists in either image (checked as branch targets and as a dword). The
  rebuild keeps the same call sequence anyway, so EDX ends on the same value on every
  path (the game's apply on a hit, `PathFileExistsA` on a miss).
- `docs/parity.md`: status counts, two new preserved-fault rows (the file-registry
  helpers' unguarded null on a scan miss; the DLC register call's undefined `this`),
  the loading-keys section with the per-build table, and the `NewSavesCompatibility`
  blocker.
- `tests/bodies.tsv` (14 rows), `tests/signatures.tsv` (the three keys' anchors and the
  LoadDLCs fixed-site table).

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` -> `build 9: 88 key(s) closed, 11 open` (exit 1,
`SkipLauncher` open by construction); `ctest --preset release` -> `100% tests passed
out of 10`; `verify_bodies.py` -> `98 body(ies), 0 failure(s)`.

**Not done / not verifiable here**: `NewSavesCompatibility` stays open. It needs
`sub_10051B30` (the save converter: compiled file IO, the 18-chunk walk and three
conversion-time site patches at `0x872185`, `0x7D8C50`, `0x8DA090`) plus
`sub_10051FD0`'s save-directory path build; its trampoline (`0x100521E0`, 8
instructions) is transcribable but calls that prepare step, so shipping the write
without the converter would corrupt save loading. No game launch: the allocator
vtable's live behaviour, the file-registry helpers' scans and the DLC discovery's
folder probes are proven at instruction level (or rebuilt faithfully) but not executed
against a running game.

## 2026-11-06 â€” the steer consumer `sub_10004C70`, and five keys

**Closed:** `MovingSteeringWheelSupport`, `ShakingEngineBlock`, `MovingPlaneParts`,
`GarageModelFix`, `NoLiveryLimit` â€” 88 -> 93 keys on 1.0.8.0, and each one's
per-option cell is byte-identical to its baseline on all nine images (build 1 is
measured with isolated single-key cells, because its per-option dump faults at
`NoImpostorHeightCutoff` by design).

**Characterisation (IDA `ZolikaPatch.asi.i64`, 320 instructions / 1,343 bytes).**
Compiler output for C, not a transcribable stream: `/GS` frame
(`mov eax,___security_cookie` at `0x10004C76`, `call @__security_check_cookie@4` at
`0x100051A6`), SSE double conversions around every float expression (`cvtps2pd` /
`mulsd` / `divsd` / `cvtpd2ps`), the MSVC float-`!=` idiom (`ucomiss` `lahf`
`test ah,44h` `jnp` at `0x10004DDF..0x10004DE7`) and `cmova` for the two `>`
comparisons. It is a pure consumer: reads `dword_1015AD38` (the vehicle the detour
stores), the three switch bytes, the model word at `+0x2E` and the vehicle's
`+0xE18/+0xF64/+0x10C8/+0x10D8/+0x1354/+0x1F00..1F3C` fields; writes only the pair
of matrices and its own state words (`byte_1015AD42`, `dword_1015AD3C`,
`dword_10159D34`); every game call goes through a loader-resolved slot
(`dword_10159C0C`, `dword_10159B88`, `dword_1015D63C`, `dword_10159CDC`,
`dword_1015AD4C`, `sub_10003980`).

**What was built (source/vehicles.ixx, 15 new proven bodies, 98 -> 113).**

- The consumer, rebuilt in C++ with the original's statement order, call sequence
  and state words; no proof row (docs/parity.md has the assumption table).
- Transcribed and proven: `sub_10003F40` (43, the model-registry lookup),
  `sub_100051B0` / `sub_100051E0` (10/10, the two detour variants),
  `sub_10046B90` (19, garage colour), `sub_10046BD0` (55, garage model lookup,
  /GS frame + the two `dword_10159C0C` calls), `sub_10046C80` / `sub_10046CA0`
  (8/7, garage site bodies), `sub_1003D220` (26, livery hasher), `sub_1003D270`
  (37, livery counter), `sub_1003D2F0` (6, name copy), `sub_1003D310/340/370`
  (11/11/12, livery site bodies) and `sub_1003D3A0/D3E0` (14/14, collectors).
  The only rebuilt pieces are `sub_10001280` (the bounded formatter -> `NoliveryFormat`,
  `_vsnprintf_s` + `_TRUNCATE`) and a one-line forwarder to `Shared::ValueResolver()`.
- Preamble-like resolution in `ResolveSteerHelpers()`: `dword_1015AD78` (rebased),
  `dword_10159B88` (its pattern has two hits; the original takes the first,
  `0x315390`), `dword_1015D63C`/`dword_10159CDC` (-0x30), `_stricmp`, `strcpy_s`, and
  the `CitizenIV.exe` probe for `byte_10159B85`.
- The steer keys install the shared IDE parser hook that fills the wheelangle table
  by jumping to misc's body through `Bodies::All()` (no second copy): published slot
  `zpatch_ide_hook_slot`, same as CopSirens/PopupHeadlights.

**Write sets on 1.0.8.0** â€” steer trio: `E9` at `0x6538A0` (detour) and `0x646546`
(IDE hook), publishing resume `0x6538AE`, part-drive `0x4F65F0`, IDE resume
`0x64654F`. `GarageModelFix`: `E9` at `0x868BB0` (body `0x10046C80`) and `0x5659C3`
(body `0x10046CA0`), resumes `0x868BB6`/`0x5659C8`. `NoLiveryLimit`: `E9` at
`0x64A53D` (D), `0x797A29` (A), `0x797962` (C), `0x79793C` (B), publishing
fn `0x3A490`, D/E resume `0x64A595`, A/B/C resume `0x797A36`.

**Gates (final runs)**: `cmake --build --preset release` success;
`python tools/parity_report.py 9` -> `build 9: 93 key(s) closed, 6 open` (exit 1,
`SkipLauncher` open by construction); `ctest --preset release` -> `100% tests passed
out of 10`; `verify_bodies.py` -> `113 body(ies), 0 failure(s)`; the shipped-INI full
cell is `0 differing, 3 missing, 57 extra`, the three missing being open keys
(`NewSavesCompatibility`, `OutOfCommissionFix`, `CutsceneFixes`) and the extras
`SkipLauncher`'s incomplete baseline.

**Not verifiable here**: the rebuilt consumer's runtime arithmetic (the two setters,
the part calls' values, the engine phase) is not executed by the offline tools; it is
pinned by the disassembly-derived table in `docs/parity.md`, and every piece around
it is proven instruction for instruction. No game launch. `AGENTS.md`'s "Current
state" (88 keys / 98 bodies) is outside this pass's edit set and is stale.

## 2026-11-06 - `ForceCarHeadlightShadows` closed (93 -> 94 keys, 113 -> 114 bodies)

**What closed it.** The key's arm writes a *pointer to a float* into the shadow
function's `movss` operand, and the stored baseline records the reference dump's
`0xD0000000` placeholder for that word. `Bodies::RegisterPublishedImage` (the
registry `source/bodies.ixx` already exposed) names the word for the dump; the arm
writes the real module address and the mod writes no placeholder.

**Write set on 1.0.8.0** (game RVAs; anchor match = 0x8F7C5B): `E9` at `0x8F7590`
-> `0x1001ADB0` (shadow function entry; resume match+12); `EB` at `0x8F7C61`,
`E9` at `0x8F7CBA` -> match+476 and `EB` at `0x8F7DD6` (the three shadow
branches forced); the dword at `0x8F7D2E` -> this module's offset float (the
`movss xmm2,[0xD95B68]` operand; the game's word is 0.0f); and the handler tail's
three restores at `0x8F73EC` (0x504), `0x8F7422` (0x104) and `0x8F719F` (`je
+9Eh`), which are no-ops on a pristine image and undo `CarDynamicShadowFix`'s
`@noFCHS` rewrites when both keys are on.

**Transcribed vs rebuilt.** The hook thunk `0x1001ADB0` (9 instructions) is
transcribed and proven (`force_headlight_shadows_shim`, new row in
`tests/bodies.tsv`); the per-model offset check `0x1001AD20` (139 bytes,
..0x1001ADAA) is rebuilt in C++ with the same state words (`dword_1015BDD0`,
`dword_101580C0` seeded 2.0f, `dword_1015B5BC`, the `dword_1015ADB8` /
`dword_1015B5C0` 512-slot tables) and the same unguarded call-then-load order, so
it has no proof row: it is compiler output (/GS frame, tail-merged epilogue,
`movss` store, `nop dword ptr [eax]` alignment). One unavoidable divergence: the
original's walk runs off its own table into the following .data; the rebuild stops
at the 512th slot (unreachable through the INI).

**Gates (final runs).** `cmake --build --preset release` -> success;
`python tools/parity_report.py 9` -> `build 9: 94 key(s) closed, 5 open` (exit 1,
`SkipLauncher` still open by construction; the key is not in the OPEN list);
`ctest --preset release` -> `100% tests passed out of 10`;
`tools/verify_bodies.py` -> `114 body(ies), 0 failure(s)`. The key's per-option cell
is byte-identical to the baseline on builds 5, 6, 7, 8, 9, 11, 12 and 13, and on
build 1 in an isolated single-key cell (15 rows; only the four `E9` displacement
bytes differ, which the scoreboard excludes by design).

**Open, recorded in `docs/parity.md`.** The `[HeadlightConeOffsets]` table the
checker walks is still not parsed from the INI (the same shared gap as
`[PopupLightAngles]`): with the shipped INI's empty sections the checker's 2.0f is
the original's own value, but a section with rows (as in `game_refs/ZolikaPatch.ini`)
would use per-model offsets in the original and 2.0f here. Fixing it means the
loader preamble's reader (`sub_100523C0`) plus its unguarded `std::stof`, which
covers both sections and is a pass of its own.

## 2026-11-06 â€” the loader preamble's light tables: `[HeadlightConeOffsets]` and `[PopupLightAngles]`

**Closed:** the gap the previous entry recorded. The two 512-slot tables are now
read from the INI, so `ForceCarHeadlightShadows` and `PopupHeadlightsFlag` publish
per-model offsets/angles from the user's actual `ZolikaPatch.ini` instead of the
constant, and keep the constant exactly where the original does (an empty section,
an absent row, or a hole before the model's row).

**Recovered reader (`sub_100523C0`, `0x10052419..0x10052D9B`).** Two structurally
identical loops, `[PopupLightAngles]` first (`0x10052419`) and
`[HeadlightConeOffsets]` second (`0x10052901`):

- `call sub_10055F10` (the section lookup) / `jz` skips a missing section's loop;
- `mov edi,1` â€¦ `inc edi` / `cmp [ebp-5Ch],200h` / `jl` â€” `Car1`..`Car512`, 512
  iterations; `Car513` is never asked for;
- the key is the literal `"Car"` (`sub_1005BEF0`, which prepends it to
  `std::to_string(ordinal)`) plus `"Name"` (`memmove ...,4`), then separately plus
  `"Angle"` (`...,5`) or `"Offset"` (`...,6`);
- `sub_1005AF10(value, "")` / `jnz` â€” an absent or empty name skips the whole row,
  so its value is never even fetched;
- `sub_10003E50(name)` -> `dword_1015C628[slot]` / `dword_1015ADB8[slot]`, stored
  *before* the value (`0x1005265E` before `0x10052763`, `0x10052B0C` before
  `0x10052C14`);
- the value is the inlined `std::stof`: `mov [errno],0` / `call strtof` /
  `cmp endptr,start` -> `0x100529BC` `_Xinvalid_argument("invalid stof argument")`,
  `cmp [errno],22h` -> `0x100529B1` `_Xout_of_range("stof argument out of range")`.
  Both escape the loader uncaught. A value with trailing junk (`5abc`) is truncated
  instead;
- the angle is `(double)value * pi / 180` (`0x1005286E..0x10052893`, pi at
  `0x10152358`, 180 at `0x10152370`), the offset is stored raw.

**Where it lives.** `Config::ReadModelTables(Config::Ini(), resolve)` in
`source/config.ixx` (`ModelTable`: 512 hashes + 512 floats;
`Config::PopupLightAngles()` / `Config::HeadlightConeOffsets()`), called once from
the top of `RegisterVehicles()` â€” the first registration after `Shared::Register()`
has published the resolver's two CRT slots, and before anything reads an option or
writes an image byte, so the tables are complete and a malformed row throws exactly
where the original's preamble does. `vehicles.ixx` and `misc.ixx` lost their local
`zpatch_fchs_*` / `zpatch_popup_*` table copies and their checkers read the config
tables; the 2.0f seed and the pi/2 seed are unchanged, and are what the checkers
keep when the tables are empty. Nothing in the reader hashes a name itself: the
caller passes `Shared::ValueResolver()`.

**Tests.** New `zp_model_tables` (`tests/model_tables.cpp`, 55 checks in ctest, 56
with a real INI path): the section gate, the `1..512` ordinal and `Car01`/`Car0`/
`Car513` non-keys, index-based fill with holes, an empty name skipping the row, a
name with no value storing the hash and leaving `0.0f`, a value with no name never
parsed, `std::invalid_argument` on `abc`/space-only, `std::out_of_range` on
`1e999`, `5abc` -> `5.0f`, the hash-before-value order, `[PopupLightAngles]`
before `[HeadlightConeOffsets]` on a throw, a full 512-row table and `Car513`
ignored, the degrees->radians constants (90 -> `0x3FC90FDB`, 180 -> `0x40490FDB`),
and the reader driving the transcribed `sub_10003E50` against a reference
implementation of its body. Manually run against `game_refs/ZolikaPatch.ini`:
`78 offset row(s) -> 78 slot(s), 0 angle row(s) -> 0 slot(s)`.

**Gates (final runs).** `cmake --build --preset release` -> success;
`python tools/parity_report.py 9` -> `build 9: 94 key(s) closed, 5 open` (the same
five as before; no image byte moved â€” the tables are read-only startup state);
`ctest --preset release` -> `100% tests passed out of 11`;
`tools/verify_bodies.py` -> `114 body(ies), 0 failure(s)`.

**Docs.** `docs/parity.md`: the gap paragraphs under `ForceCarHeadlightShadows` and
the misc keys are replaced with the recovered grammar table, the fault rows are in
Â§Preserved faults, and `zp_model_tables` is in Â§Contracts. `AGENTS.md`'s harness
sentence still lists the old executables and could name `zp_model_tables`.

## 2026-11-06 â€” `OutOfCommissionFix`: the payload carried, the key closed

**Decided first, then built.** The key had been refused twice because its payload
looked like game-side registration. It is not: `sub_1004B150` + `sub_1004B570` are a
pure LZ decode of the 163,796 bytes the ASI embeds, `sub_1004B750`/`sub_1004ACE0`
are the ASI's own registration and archive build, and the only game call is one the
original makes through a pointer published from the game image (the device register
at `0xA22BE0`), exactly like every other arm's calls. So the table is a deterministic
transform and was carried.

**Characterisation that mattered (IDA `ZolikaPatch.asi.i64`).**
`sub_1004C3F0` is **not** the decoder: it is the key's own handler (16,311 bytes /
4,199 instructions, EH4 frame, `/GS`, the original's one-character-at-a-time string
builder). It scans `A9 39 44 24 18`, publishes `dword_10158034 = match+0x11`,
`VirtualProtect`s `match+7` and writes `E9` to `sub_1004C3C0`; then scans
`E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56`, publishes `dword_10159BF8` (the target of
the call at `match-0x33`, `sub_10001670`) and `dword_10159C04` (the old target of the
call at the match), and calls `sub_1005BA80`, which writes `E8` + displacement to
`sub_1004C350` and returns the old target. The decoder is `sub_1004B150` (opcode
step) + `sub_1004B570` (driver: `05 FA` terminator, exact 179,536-byte output,
Adler-32 trailer). `sub_1004B750` = malloc(0x2BD50) + decode + push_back into the
`std::vector` at `0x1016E43C` + build + `dword_10159BFC = dword_10159C78`.

**What was built**

- `source/ooc_finale2.inc` â€” the 163,796-byte resource, verbatim, with the image
  SHA-256 (`720fbcebâ€¦`), VA `0x100B6798`, file offset `0xB5B98`, the extraction
  recipe, the decoded table's `sha256 86d2bb26â€¦` / `fnv1a 0xC883859F`, and the
  archive's 182,272 bytes / `fnv1a 0xB5AF7B10` in its header (1,035,979 bytes of
  source). The decode was verified independently in Python first: 446 opcodes,
  ends at `0x27FCE`, trailer `0x41E794AB` matches, and the table is byte-identical
  to `../readable/data/finale2.sco`.
- `source/ooc.ixx` (new module `ooc`) â€” the decoder (`OocStep`/`OocDecode`, the nine
  opcode forms and the two fault paths), the Adler-32, the fake-pak entry/vector,
  the archive builder (`OocBuildArchive`: 20-byte header, 16-byte record, name,
  payload at `0x800`, padding to the page), the init (`OocInitialise`, re-running on
  every call like the original's writerless guard), a rebuilt `sub_10001280`
  formatter, and `OocSelfCheck()`.
- The two bodies, transcribed: `OocBranchBody` (7, `0x1004C3C0`) and `OocWrapBody`
  (41, `0x1004C350` â€” the brief's "29" does not survive measurement). Both proved
  whole, and a byte comparison shows them identical to the original apart from the
  six absolute operands.
- The arm: publish tier reads the resume and the two call targets, install tier
  writes `E9` at `match+7` and retargets the `E8` at the second anchor.
- `CMakeLists.txt` (one module), `source/dllmain.cpp` + `tests/dump_main.cpp`
  (`import ooc;`, `RegisterOoc()`, and `zp_dump --ooc-selftest`), `tests/bodies.tsv`
  (+2 rows), `tests/signatures.tsv` (+2 anchors), `docs/parity.md` (the blocked
  section replaced by the closed one, two new preserved-fault rows, the rebuilt-body
  list).

**Gates (final runs)**

- `cmake --build --preset release` â€” success.
- `python tools/parity_report.py 9` â€” `build 9: 96 key(s) closed, 3 open`
  (`OutOfCommissionFix` gone; 94 before this pass, 96 with the cutscene pass beside
  it).
- `ctest --preset release` â€” `100% tests passed out of 11`.
- `python tools/verify_bodies.py â€¦` â€” `117 body(ies), 0 failure(s)` (both new rows
  PASS: `ooc_branch` 7, `ooc_wrap` 41).
- `zp_dump --ooc-selftest` â€” `ooc selftest: PASS` (the rebuilt decode, the table's
  `fnv1a`, the archive's length and `fnv1a`, the header words).
- The key's per-option cell is the baseline's 9 rows with our two displacements
  (excluded as branch bytes); the full shipped-INI cell differs from the same cell
  with the key off by exactly those 9 bytes.

**Not verifiable here.** No game launch: whether the game accepts the mounted
in-memory archive and loads `finale2.sco` from it is a runtime property. The artifact
is 1,637,376 bytes (the resource is 163,796 of them). `AGENTS.md`'s "Current state"
(94 keys / 114 bodies) is outside this pass's file scope and is stale.

## 2026-11-06 â€” HighQualityReflections, and the site arithmetic

**Closed:** `HighQualityReflections` (96 -> 97 keys on 1.0.8.0; the two remaining
open keys are `NewSavesCompatibility` and `SkipLauncher`).

**The question.** The triage recorded the handler's publish as
`resume = esi + 0x13` and `builder = esi + dword(esi + 0x0F) + 0x13` and read `esi`
as the `E9` site; that made the resume look like the last byte of a five-byte
`call` at `site + 0x0F` and the builder wrap. The bytes show the premise was one
byte off: on 1.0.8.0 the site is `0xC51311` (`8B 87 38 09 00 00 6A 08 68 1D 13 00 00
50 E8`, one hit), the `E9` lands on the six-byte `mov eax,[edi+938h]`, and the
site's own `E8` is at `site + 0x0E` (`E8 4C EA F1 FF`, target `0xB6FD70`), not
`site + 0x0F`. So `site + 0x0F` is the rel32, `dword(site+0x0F) = 0xFFF1EA4C`,
`builder = site + 0x13 + that = 0xB6FD70` (the call target), and `site + 0x13` is
the `push 0` after the call -- an instruction boundary; `site + 0x14` is the second
byte of that push, so the `esi = match+1` alternative would not resume at all.

**Scanner convention.** `sub_1005B6A0` returns the first hit-vector element
(`mov esi,[edx]` / `mov eax,esi` / `retn`), and the scanner stores the match start,
not match+1: `sub_10003290` computes `edx = 1 - length`, then
`lea eax,[edx+esi]` (current byte minus length-1) and writes it to the vector. So
`esi` is the match / `E9` site and the original arithmetic is coherent as written.

**What was built**

- `source/graphics.ixx` â€” `HqrBody` (`sub_1001F030`, 157 instructions / 858 bytes,
  static naked, the original's eight-register dump and twelve builder calls),
  `PublishHighQualityReflections` (`match+0x13`, `match+0x13 + [match+0x0F]`) and
  `InstallHighQualityReflections` (`Bodies::Register(..., 0x1001F030)`, `E9` at the
  match).
- `tests/bodies.tsv` â€” `hqr_body 1001F030 ?HqrBody@@YAXXZ 157`.
- `tests/signatures.tsv` â€” the site row (`0xC51311`, the 15-byte signature).
- `docs/parity.md` â€” the resolution section with the site and handler/scanner
  instruction boundaries, the status line (`97 closed, 2 open`) and the blocked
  list.

**Gates (final runs)**

- `cmake --build --preset release` â€” success (exit 0); `--preset trace` also built
  and its suite passed.
- `python tools/parity_report.py 9` â€” `build 9: 97 key(s) closed, 2 open`; the two
  OPEN lines are `NewSavesCompatibility` and `SkipLauncher` (both known baseline
  gaps), `HighQualityReflections` is not among them.
- `ctest --preset release` â€” `100% tests passed out of 11`.
- `python tools/verify_bodies.py â€¦` â€” `118 body(ies), 0 failure(s)`
  (`PASS hqr_body (157 instruction(s), 133 absolute operand(s))`).

**Not verifiable here.** No game launch: the twelve draw-command-builder calls and
the resume are transcribed/proven but not executed in the running game.
`AGENTS.md`'s "Current state" (96 keys / 117 bodies) is outside this pass's file
scope and is stale.

## 2026-11-06 â€” NewSavesCompatibility, closed on its rebuilt converter

**Closed:** `NewSavesCompatibility` (`build 9: 97 -> 98 key(s) closed, 1 open`; the
one open key is `SkipLauncher`'s baseline gap).

**What the original does (read end to end in the IDB).** The handler `0x10052200`
does not convert anything: it scans `68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08`
(game `0x857CAF`; the pushed operand is the save-name buffer the game fills with
`sprintf(buffer, "%s%02d", "SGTA4", slot)`) and
`89 84 24 E0 01 00 00 8B 45 08 53 33 DB` (game `0x85B093`), publishes `match+0xD`
and writes `E9` to `0x100521E0`. Behind that trampoline:

- `sub_10051B30` (363 instructions) opens the path with `fopen_s(...,"rb")`, reads
  the `0x110` header and `cmp dword ptr [eax],45564153h` at `+0xC`, then walks
  eighteen chunks: `fread(&chunk,9,1)`, the dword at `+5` is the total size,
  `add eax,0FFFFFFF7h` (size-9), `call operator new`, `fread(block,1,size-9)`,
  `free`. Chunks 9/13/16 are compared with `4C45h`/`1741h`/`0E77h` and stored in
  `byte_1015AD98/99/9A`. After `fclose` it patches three sites, each resolved once
  through `_Init_thread_header`/`_footer`-guarded statics: `0x872185` (byte `74` or
  `EB` from the chunk-9 size), `0x7D8C50` (`E8`+hook, read size `0CBEh` or `0E6Eh`,
  branch `75 26` or `90 90` from the chunk-16 size) and `0x8DA090` (`E8`+hook, and
  the read entry republished from `[match+1Eh]+1Eh+4`).
- `sub_10051FD0` (157 instructions) asks the game's save-directory function
  (`55 8B EC 83 E4 F8 83 EC 4C 56 33 C0 8D 4C 24 28`, called with `0`, the
  312-byte `byte_101A8308` and a suffix byte), builds
  `"<directory>\<save name>"` as a `std::string` and calls the converter.
- The two hooks it installs are compiled C: `sub_10051A90` (`sub esp,0E7Ch`, reads
  into its own scratch, keeps the read's value) and `sub_10051AD0` (`sub esp,10h`,
  twelve extra bytes before and after the original call when chunk 13 is new).

Nothing needed game-side state beyond the directory function and the save-name
buffer, both reached exactly as the original reaches them, so both steps were
rebuildable.

**What was built**

- `source/loading.ixx` â€” the arm (`PublishNewSavesCompatibility` /
  `InstallNewSavesCompatibility`, the install gated on the *first* anchor as the
  original returns before its second scan), the eight-instruction trampoline
  (`NewSavesTrampoline`, `pusha`/`popa` as `__emit 0x60/0x61`), and the four
  rebuilt pieces with the original's call shape: `NewSavesReadHook` (0x10051A90),
  `NewSavesSecondLoadHook` (0x10051AD0), `NewSavesPatchSites`, `NewSavesConvert`
  (0x10051B30), `NewSavesPrepare` (0x10051FD0). Everything is registered in
  `Bodies::Register`; the arm is `Loader::Enabled`-gated.
- `tests/bodies.tsv` â€” `new_saves_tramp 100521E0 ?NewSavesTrampoline@@YAXXZ 8`;
  the four rebuilt pieces are named in the comment as rebuilds with no row.
- `tests/signatures.tsv` â€” the six recovered scans (name, load, branch, read,
  reload, directory) with the per-image absence note.
- `docs/parity.md` â€” status (98/1, 119 bodies), the key's section with the quoted
  instructions and the transcribed/rebuilt table, and two new preserved-fault rows
  (the corrupt-save allocation throw / uninitialised header check, and the
  trampoline's unspecified-EAX store).

**Gates (final runs)**

- `cmake --build --preset release` â€” success (exit 0).
- `python tools/parity_report.py 9` â€” `build 9: 98 key(s) closed, 1 open`; the only
  OPEN line is `SkipLauncher`, `NewSavesCompatibility` is gone (was 97 closed).
- `ctest --preset release` â€” `100% tests passed out of 11`.
- `python tools/verify_bodies.py â€¦` â€” `119 body(ies), 0 failure(s)`
  (`PASS new_saves_tramp (8 instruction(s), 1 absolute operand(s))`).
- Cross-build parity spot check: `NewSavesCompatibility` has no OPEN line on builds
  1, 5, 6, 7, 8, 11, 12 and 13 either (1.0.0.4.0 writes nothing because its image
  lacks the first anchor, as the original does).

**Not verifiable here.** No game launch: the conversion path runs only when the game
loads a savegame, so the three site patches and the two hooks have not executed. The
file walk was replayed offline against the two real saves in
`game_refs/saves/GTA IV/savegames/user_ee000000deadc0de` (`SGTA400`/`SGTA401`: `SAVE`
at `0x0C`, eighteen `BLOCK` chunks, sizes `0x3AB1`/`0x1729`/`0x0CC7`, i.e. the older
layout), which validates the walk and the size conditions only.

## 2026-11-06 â€” SpeedupBugFix: per-build recovery, and the fault that was not this key

**The reported bug was misattributed.** `zp_dump --exe ..\game_refs\GTAIV_1000.exe
--build 1 out.txt build\win-x86\only-speedup.ini` does fault `0xC0000005`, but not
in `SpeedupBugFix`. A one-line INI is not a single-key document: every absent key
keeps its default (mostly `1`), so that run executes nearly the whole catalogue. A
per-key bisect on 1.0.0.0 (99 keys, each alone) finds exactly one faulting key:
`NoImpostorHeightCutoff`, the documented preserved original fault (its pattern
hits once on 1.0.0.0 and `graphics.ixx` indexes `pattern.get(1)`, i.e. reads past
the hit vector). Adding `NoImpostorHeightCutoff=0` to the same INI makes build 1
exit 0 (433 bytes). `SpeedupBugFix` alone on 1.0.0.0 is 0 bytes / exit 0: its
signature has no hit in `GTAIV_1000.exe`, which is the original handler's own
clean skip (`test eax,eax; jz 0x1005077D`).

**Recovered from the IDB (`ZolikaPatch.asi.i64`).** Handler `sub_100506B0` is
reached from one call site (`cmp esi,1; jnz; call sub_100506B0` at `0x10055A5D`
.. `0x10055A62`) with **no build gate**. It reads its eight operands at fixed
site-relative displacements (`lea ebx,[eax-15h]`, then `mov eax,[ebx+2Fh]` ..
`[ebx+129h]`), writes `E9` at `ebx` and `sub_10050540-ebx-5` at `ebx+1`. So the
original's row is identical on every build; only the match VA differs. The eight
displacements are the disp32 fields of eight `movss` in the game's timer-init
function, with the same opcode bytes on every image.

**Sites recovered** by byte scan of all nine images: 1.0.0.0 none; 1.0.4.0
`0x4544C5`, 1.0.0.4 `0x457395`, 1.0.6.0 `0x5AFBF5`, 1.0.7.0 `0x5AFDD5`, 1.0.8.0
`0x4571C5`, 1.1.1.0 `0x454BC5`, 1.1.2.0 `0x454C05`, 1.1.3.0 `0x5AD565`. All eight
operands validate on every one (same encodings, writable `.data` targets), and the
body's clock scan `1B D9 89 5C 24 24 0F 88` hits once per build with `-51`
landing on the clock function's `55 8B EC 83 E4 F8` prologue. 1.0.6.1, 1.1.0.0 and
1.2.0.43 have no image here: unverified, not verified.

**What changed.** `source/misc.ixx`: `ResolveSpeedupBugFix` validates each of the
eight operand fields (four recovered opcode bytes before the field + a writable
in-image target) before publishing any of them, and the install only runs for a
resolved site; a site that fails is a clean skip. `tools/single_key_matrix.py`
(new): the true single-key matrix, every key `0` and one `1`, one subprocess per
key, per-build exit-code table, with the 1.0.0.0 `NoImpostorHeightCutoff` cell
recorded as the expected fault. `tests/signatures.tsv` and `docs/parity.md`: the
per-build rows, the quoted handler/body instructions, the validation rule and the
one recorded divergence (an image whose signature matched another function would
be a skip here, where the original would install and its body would store through
the operands).

**Gates (final runs).** `cmake --build --preset release` exit 0;
`python tools/parity_report.py 9` -> `build 9: 98 key(s) closed, 1 open` (the
open key is still `SkipLauncher`); `ctest --preset release` -> 11/11;
`python tools/verify_bodies.py ...` -> `119 body(ies), 0 failure(s)`;
`python tools/single_key_matrix.py` -> 891 cells, 890 exit 0, one expected fault
(`NoImpostorHeightCutoff` on 1.0.0.0). SpeedupBugFix's per-key dump is
byte-identical to the pre-change dump on all nine images (build 9: 4 bytes).

## 2026-11-06 â€” the rebuilt set: six pieces upgraded, the rest catalogued

**What this pass was for.** Ten-odd pieces of the tree were C++ rebuilds with no proof
row â€” the weakest evidence in the project. The rule for "cannot be transcribed" was
re-derived from the comparator instead of from "it is compiler output": a body is
blocked only by an *indexed absolute* table operand, an exception frame, a TLS
static-init guard, or a call graph of other rebuilds. A `/GS` cookie and SSE code are
not blockers, and three `/GS` bodies were transcribed as part of this pass.

**Upgraded (six new proof rows, 119 -> 125 bodies).** The bounded formatter
(`0x10001280` 13, `0x10001240` 21, `0x10001230` 2), NewSaves' two installed hooks
(`0x10051A90` 20, `0x10051AD0` 33) and LoadDLCs' discovery (`0x10023150` 223). Five
C++ rebuilds are gone: the three copies of `_vsnprintf_s` (stability, vehicles, ooc â€”
the other two now reach the one transcribed body through a six-byte
`jmp [zpatch_format_entry]`), the two save hooks, and `DlcDiscovery` +
`BuildEpisodeSuffix`. The DLC body is the only rebuilt piece with no EH frame, no TLS
guard and no indexed absolute, so it is now the original's own instructions.

**How it was verified.** Proof rows (exact counts); a byte comparison
(`.scratch/byte_compare.py`) that classifies every differing instruction â€” the DLC
body is 187/223 byte-identical, the rest differ only in absolute operands and two
branch encodings; a probe of the original's engine call (`__stdio_common_vsprintf`
with options 2) against the old `_vsnprintf_s` rebuild â€” identical buffer contents at
every truncation, a different unused return value; and a call-to-symbol check
(`.scratch/resolve_calls.py`) so a `call` that lands on the wrong helper cannot hide
behind the proof's call-as-immediate rule.

**Recorded, not upgraded.** `docs/rebuilt.md` is new: every rebuilt piece with VA,
size, instruction count, the specific operand/frame pattern that blocks it, what the
rebuild reproduces, how that was verified, residual risk, and a risk-ordered "check
these first in play" list (save converter/prepare first). `docs/parity.md` now points
at it instead of repeating the reasons, and its body count and stale rebuild claims are corrected.
Follow-ups ranked in `rebuilt.md` Â§4: the OOC decoder pair (transcribable today),
the NewFiles updates (two `__fastcall` shims), the three transcribable DFA wrappers
(launcher.ixx, not this pass's file), the NMS arena methods.

**Gates (final runs).** `cmake --build --preset release` exit 0;
`ctest --preset release` 12/12; `python tools/parity_report.py 9` ->
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`python tools/verify_bodies.py ...` -> `125 body(ies), 0 failure(s)`;
`zp_dump --ooc-selftest` -> `ooc selftest: PASS`.

**Not verifiable here.** None of the six bodies can be made to run offline: the
formatter runs on livery/OOC/DLC paths, the save hooks on a savegame load, and the
discovery pass on DLC-manager init. `rebuilt.md` Â§2 and Â§5 say so per piece.

**One defect was caught by the new checks, not by the proof.** The first
`dlc_discovery` transcription attached `dlc_tlad_exists`/`dlc_tbogt_exists` to the
register block, while the original's `jne` lands on the *second* `PathFileExistsA`
probe (a "content" folder that exists is probed again, and a failed probe still skips
registration). `zp_body_proof` passed it because a `jcc` immediate compares as a slot,
so the target is never compared. The byte comparison showed two `jne` displacements 18
bytes off; after moving the labels, `.scratch/branch_targets.py` (new) reports
`125 row(s), 154 internal branch target(s), 0 failure(s)`. That check is the top
follow-up in `rebuilt.md` §4 — it belongs in `tools/verify_bodies.py`, and the whole
row set already passes it.
