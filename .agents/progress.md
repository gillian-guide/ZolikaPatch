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
