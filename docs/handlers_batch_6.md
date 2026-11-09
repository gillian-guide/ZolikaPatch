# Option handler summary — batch 6

Ten handlers requested: `0x10045490 0x10045610 0x10045980 0x10045ad0 0x10045c50
0x10045ce0 0x10046640 0x100467a0 0x100468d0 0x10046a10`.

Evidence used (all read-only):

- `ZolikaPatch_decompiled/checkpoints/option_handlers/batch_2,3,4/decompiled_1004*.c`
  (Ghidra decompilation, requested 1:1 in the batch logs).
- `ZolikaPatch_decompiled/ghidra/decompiled_0001.c` (same functions in the bulk export).
- i686 disassembly of `ZolikaPatch_extracted/ZolikaPatch.asi` (`i686-w64-mingw32-objdump`).
- Signature strings read directly from the ASI `.rdata` at the `mov $VA,%ecx` operand
  feeding each `call 0x10003830` (the recovered matcher).
- `tools/verify_game_signatures.py` + a read-only copy of `GTAIV.exe` (1.0.8.0,
  image base 0x400000, sha256 `3d90e7c5…fbfd0f`) to confirm match uniqueness and
  resolve targets.
- `evidence/option_handlers.json`, `evidence/option_build_gates.json`,
  `evidence/option_patch_data.json`, `evidence/patch_operations_all.json`.

Common handler shape: a handler is called from the loader only when the INI value
is 1. It calls `FUN_10003830(sig, GetModuleHandleA(0))` to locate the game site,
then writes through the `VirtualProtect`-toggled store idiom recovered earlier
(`GetModuleHandleA(0)` is cached at `0x1015ad70`; `0x1015be04` is the build slot
`GameBuild` ordinal: v1060=7, v1070=8, v1080=9). Replacement bytes below are given
as the *absolute* value the handler stores. Offsets are relative to the start of
the signature match. `E9`/`E8`/`EB` displacement is written separately at
`site+1` and equals `target - site - 5`.

## 0x10045490 — RemoveRGSCLoginRequirement

- INI: `RemoveRGSCLoginRequirement=1` ("remove Social Club login requirement …").
- Signatures:
  1. `83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B 5C 24 4C 56 68 ? ? ? ? E8 ? ? ? ? 83 C4`
     (string VA `0x1012d990`).
- Build gate: `if (match == 0) { if (DAT_1015be04 == 9) { … } }` — the fallback
  path runs **only** on build 9 (1.0.8.0); the signature path is ungated.
- Writes:
  - signature path: offset `+0`, `E9` (1 byte) + relative jump to `0x10038550`
    (`mov al,1; ret`).
  - fallback path: **module-relative**, `DAT_1015ad70 + RVA 0x498460`, `E9` (1 byte)
    + relative jump to `0x10038550`.
- Feature: forces the RGSC/`Social Club` login check to return success, so MP works
  even when login is skipped on pre-patch-6 builds or "Play Offline" on patch 7.
- Verification: the signature is **absent** in GTAIV 1.0.8.0 (0 hits) — expected,
  because build 9 is the one that takes the RVA fallback. RVA `0x498460` in the game
  (`VA 0x898460`) begins `8B 44 24 04 85 C0 74 17 F3 0F 7E 05 …`, preceded by `c3 cc cc`
  padding, i.e. a real function entry — consistent with `evidence/fallback_patch_sites.json`.
- **Uncertainty:** which build the signature was written for is not recoverable here;
  only that it does not match 1.0.8.0.

## 0x10045610 — SavegameFix

- INI: `SavegameFix=1` ("remove GFWL savegame CRC check").
- Signatures:
  1. `75 ? 83 C1 01 83 C0 01 83 F9 04` (VA `0x1012da28`, game RVA `0x21edb5`).
  2. `8D 4C 24 18 51 6A 00 6A 0C 57` (VA `0x1012da84`, game RVA `0x21eba1`).
- Build gate: none recovered.
- Writes (all verified against 1.0.8.0; the game sites already contain the pre-patch
  calls, and the recovered trampolines sit at `0x10045580/0x100455a0/0x100455e0/0x10038570`):

  | sig | offset | bytes | target |
  |-----|--------|-------|--------|
  | 1 | `+0` | `90 90` (2) | — (NOPs out the CRC mismatch branch) |
  | 1 | `-0x49` | `E8` (1) | `0x10045580` |
  | 1 | `-0x8f` | `E8` (1) | `0x100455a0` |
  | 1 | `-0xb0` | `E8` (1) | `0x100455a0` |
  | 1 | `-0x118` | `E8` (1) | `0x100455e0` |
  | 1 | `-0xf0` | `E8` (1) | `0x10038570` |
  | 2 | `-0x56` | `E8` (1) | `0x100455e0` |
  | 2 | `-0x2f` | `E8` (1) | `0x10038570` |
  | 2 | `+0xa` | `E8` (1) | `0x100455a0` |
  | 2 | `+0x5a` | `E8` (1) | `0x100455a0` |
  | 2 | `+0x80` | `E8` (1) | `0x10045580` |

- Feature: replaces the GFWL savegame CRC-check call sites with ZolikaPatch
  trampolines (`0x10045580` = `CloseHandle`+`return 0`, `0x100455a0`/`0x100455e0` are
  small argument/return shims, `0x10038570` = `xor eax,eax; ret 8`) so the CRC check
  can never fail.
- Verification: both signatures resolve **uniquely** in 1.0.8.0 and every computed
  site already holds an `E8` call with a plausible target, so the extracted offsets
  and lengths are self-consistent.
- Note on Ghidra's `UNK_*` names: `UNK_1004559b`, `UNK_100455db` and
  `UNK_1003856b` are simply `target - 5` (e.g. `0x1004559b + 5 = 0x100455a0`), i.e.
  they name the *displacement field*, not extra sites. Disassembly confirms exactly
  ten `E8` opcode stores, matching the ten rows above.

## 0x10045980 — SCOSignatureFix

- INI: `SCOSignatureFix=1` ("allow loading of any .sco file …").
- Signature: `8B 6C 24 1C 66 89 6E 14 64 8B 0D 2C 00 00 00 8B`
  (VA `0x1012dae8`, game RVA `0x226dc0`).
- Build gate: none.
- Write: offset `-0x20`, `EB` (1 byte). This converts a conditional jump into an
  unconditional one, skipping the SCO episode/`platform` rejection.
- Feature: bypasses the `.sco` script signature/episode check so any episode's
  script loads (needed for trainer parachute support).
- Verification: signature unique in 1.0.8.0; site `0x626da0` currently holds
  `74 1e …` (a `JZ`), so replacing the opcode with `EB` is consistent.
- **Uncertainty:** the exact conditional that is turned unconditional is stated as
  observed (`JZ`→`JMP`); the decompiler only reports `lpAddress = iVar2 + -0x20`.

## 0x10045ad0 — ImprovedShaderStreaming

- INI: `ImprovedShaderStreaming=1` ("improved a really badly coded function,
  reduces stutter somewhat").
- Signature: `33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E
  C3 8B 44 24 0C 8B 74 24` (VA `0x1012db60`, game RVA `0x8b21f1`).
- Build gate: none.
- Writes (single match, base `0xcb21f1`):

  | offset | bytes | target |
  |--------|-------|--------|
  | `+0x55` | `E8` (1) | `0x10045a30` |
  | `+0x62` | `E9` (1) | relative `+0x8e` → `0xcb22e6` (rest of the original code) |
  | `+0x63` | `8E 00 00 00` (4) | the displacement itself |
  | `+0xfd` | `E8` (1) | `0x10045a80` |
  | `+0x10a` | `E9` (1) | relative `+0x377` → `0xcb2677` |
  | `+0x10b` | `77 03 00 00` (4) | the displacement itself |

- Feature: redirects two container-scan call sites into ZolikaPatch shims
  (`0x10045a30`/`0x10045a80`, which walk a vector and call `0x100459e0`/`0x10045a10`)
  and short-circuits the original loop with two `E9` jumps.
- Verification: signature unique in 1.0.8.0; both `E8` sites currently hold calls to
  `0x4399c0`; both `E9` sites currently hold `8B 4E 08 68 …` (i.e. the `E9` overwrites
  the first byte of an existing instruction), so a 5-byte jump plus displacement is
  the recovered shape. The two computed jump destinations land inside the original
  function body (`0xcb22e6`, `0xcb2677`).
- **Uncertainty:** the decompiler renders the `E9` displacements as literals `0x8e`
  and `0x377`; I interpret them as the raw relative displacement stored at `site+1`
  (verified arithmetically against the game image). They are not absolute targets.

## 0x10045c50 — SkipIntro

- INI: `SkipIntro=1` ("skip intro screens").
- Signature: `89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50` (VA `0x1012dc08`,
  game RVA `0x4a347e`).
- Build gate: none. The loader sets bitfield bit 0x4 (`local_e8 |= 4`) and calls the
  handler only when the value parses to 1 **and** `FUN_10003e20()` returns 0. The
  recovered `FUN_10003e20` is `compatibility_module_lacks_signature` (true when the
  compatibility module is present but its signature is absent), so SkipIntro is
  suppressed when a known-compatible third-party module already handles the intro.
- Write: offset `-6`, `E9` (1 byte) + relative jump to `0x10045c30`.
  The handler *also* stores the match address into global `0x1015ad74`, which is the
  pointer the `0x10045c30` trampoline tail-jumps through
  (`jmp *0x1015ad74`).
- Feature: intro-screen skip. `0x10045c30` computes `ecx*400`, clamps `edx` to `>= 0x1f40`
  (`xor edx,edx` below the threshold) and then jumps back to the original function.
- Verification: signature unique in 1.0.8.0; site `0x4a3478` currently holds
  `69 c9 90 01 00 00` (`imul $0x190,%ecx,%ecx`), i.e. the `E9` writes over the first
  byte of that `imul` — consistent with the recovered negative offset.
- **Uncertainty:** the trampoline at `0x10045c30` is only partially understood; the
  exact meaning of the `0x1f40`/`400` constants is not resolved here.

## 0x10045ce0 — SkipLauncher

- INI: `SkipLauncher=1` ("from XLivelessAddon … ability to launch game directly from
  .exe and/or skip the patch 4 launcher …").
- This is the largest handler: it makes **25** `FUN_10003830` signature lookups plus
  **one** `FUN_1005afa0` multi-match lookup, all inside a single
  `if (DAT_10159b84 == '\0')` guard (so the handler is idempotent). Because the
  decompiler assigns many results to the same stack local, offsets below are given
  relative to the match address of the signature in that row and were cross-checked
  against the disassembly.
- Build gate: none recovered.
- Writes: `=` means the site is the match address itself (offset `+0`).

  | # | signature (prefix) | write |
  |---|--------------------|-------|
  | 1 | `84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1` | `EB` at `+0x22`, `EB` at `+0xdf` |
  | 2 | `68 ? ? ? ? 89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0 0F 84` | (address captured as `local_2c`, used by #10 only) |
  | 3 | `74 EC 38 5E 06 74 E7 68` | `90 90` at `=`, `90 90` at `+5` |
  | 4 | `74 C4 8B 07 3B 46 18 75 BD 68` | `90 90` at `=` |
  | 5 | `3B 56 18 0F 85` | `90×6` at `+3` |
  | 6 | `3B 4E 18 0F 85` | `90×6` at `+3` |
  | 7 | `0F 85 ? ? ? ? 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC C6` | `90×6` at `=` |
  | 8 | `3B 46 18 75 BD` | `33 C0` at `+3`, `90 A3` at `+10` |
  | 9 | `57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ? C7 44 24` | `0x1BF` bytes of `0x90` memset at `+1` |
  | 10 | `8B 56 1C 3B 56 20 ? ? 6A 00 6A 00` | `33 C0` at `=`, `E9` at `+2` with target `#2 match` |
  | 11 | `89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0` | `90 A3` at `=` |
  | 12 | `8B 50 2C 3B 50 30` | `33 C0` at `=`, `90×4` at `+2` |
  | 13 | `8B 48 2C 83 EC 78` | `90×3` at `=`, `90×11` at `+3` |
  | 14 | `8B 48 2C 83 EC 74` | `90×3` at `=`, `90×11` at `+3` |
  | 15 | `8B 48 2C 3B 48 30 74 03` | `90×11` at `=` |
  | 16 | `8B 48 2C 3B 48 30 53` | 32-byte copy of the float block at `0x10152400` |
  | 17 | `8B 48 2C 3B 48 30 74 11` | 25-byte copy of the same block + `90` |
  | 18 | `8B 48 2C 3B 48 30 75 DC 8D 54 24 18` | `90×8` at `=` |
  | 19 | `8B 48 2C 3B 48 30 75 DC 8D 54 24 1C` | `90×8` at `=` |
  | 20 | `8B 0D ? ? ? ? 8B 51 2C` | `90×14` at `=` |
  | 21 | `51 53 55 56 57 8B 3D ? ? ? ? 8B 47 2C` | `C3` at `=` |
  | 22 | `3B C7 0F 85 ? ? ? ? 85 C0` | 16-byte copy of the `0x10152400` float block |
  | 23 | `0F 85 ? ? ? ? 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? 85 D2` | `90×14` at `=` |
  | 24 | `83 EC 24 A1 ? ? ? ? 8B 48 14 53 55 56 57` | `33 C0 C3 90` at `=` |
  | 25 | `83 EC 08 8B 0D ? ? ? ? 8B 51 14 8D 04 24 50 6A 00 52` | `33 C0 C3 90` at `=` |

- **Multi-match block.** Between signatures 9 and 10 the handler calls
  `FUN_1005afa0(&vec, "A1 ? ? ? ? 85 C0 8B 0D ? ? ? ? 75", GetModuleHandleA(0), 0)`,
  which returns a *vector* of matches, intersects it with the vector from
  `FUN_1005afa0(&other, "33 C0 83 3D ? ? ? ? 01 0F 94 C0 C3", …)` via
  `FUN_10056f90`, and writes the 4 bytes `B0 01 C3 90` (`mov al,1; ret`-style
  hardening) at the **first**, **second**, **penultimate** and **last** entries of the
  result vector. Because this is a vector patch there is no single signature in
  `option_signatures.json` for it.
- Verification against 1.0.8.0: sig 1 (`84 DB 0F 85 43 01 00 00 …`) has **0 hits**;
  signatures 2, 7, 10, 11, 22 and 23 resolve uniquely (e.g. sig 2 at `VA 0x4900de`,
  sig 11 at `0x4900e3`); signatures 3–6, 12–21 and 24–25 are **absent** from 1.0.8.0.
  This is expected, because SkipLauncher targets launcher/`PlayGTAIV` variants across
  several supported builds.
- Feature: patches the launcher/PlayGTAIV startup chain so the game can be started
  directly from `GTAIV.exe` and the patch-4 launcher can be skipped. The 54 `0x90`
  fills remove launcher-only startup checks; the `0x10152400` float-block copies
  restore an identity transform table that the launcher path overwrites.
- **Uncertainty:** the intended *meaning* of the `0x10152400` float block beyond "a
  16/25/32-byte table restored on the launcher path" is not recovered, and the
  multi-match intersection result depends on the runtime vector contents, so the exact
  number of sites patched cannot be predicted statically from the ASI alone.

## 0x10046640 — SkipMenu

- INI: `SkipMenu=0` (shipped disabled; "skip main menu, launching straight into the
  last savegame").
- Signatures:
  1. `83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E`
     (VA `0x1012e6d8`, game RVA `0x7d240`) — **unique** in 1.0.8.0.
  2. `6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3`
     (VA `0x1012e770`) — absent in 1.0.8.0.
  3. `6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3`
     (VA `0x1012e808`) — absent in 1.0.8.0.
- Build gate: none; there is, however, an extra loader-side condition identical to
  SkipIntro: bitfield bit 0x8 is set (`local_e8 |= 8`) and the handler is called only
  when the value parses to 1 **and** `FUN_10003e20()` (compatibility-module probe)
  returns 0. The handler itself tries sig 1, then falls back to sig 2, then sig 3.
- Writes (sig 1 path): the match is a function whose *first `E8` call* is rewritten.
  `lpAddress = match + 0x17`, writes `E8` and a displacement relative to that site
  with target `0x10046610`; the original `E8` displacement is read from `match+0x18`
  and saved to global `0x10159348`. Sig 2 path uses `match + 2`, same target.
- Feature: forces the main-menu entry routine to jump into the ZolikaPatch trampoline
  `0x10046610`, which (once) substitutes menu id `6` (load last savegame) for a null
  menu id and then tail-jumps to the saved original via `0x10159348`.
- Verification: sig-1 match `0x47d240` in the game is
  `83 f8 03 75 2f a1 98 82 1f 01 80 88 …`, and `match+0x18` holds the `E8` displacement
  the handler reads — self-consistent.
- **Uncertainty:** the handler records the original displacement in `0x10159348`; the
  trampoline at `0x10046610` uses it as a jump target. This indirect linkage is
  inferred from the trampoline body, not from Ghidra's variable naming.

## 0x100467a0 — SMPA60Fix

- INI: `SMPA60Fix=1` ("fix SMPA60 both when abusing the poor game with car mods and
  if another player is doing that in MP").
- Signatures:
  1. `01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C` (VA `0x1012e894`,
     game RVA `0x454d71`) — unique.
  2. `8B 48 70 83 C0 40 50 51 8D 51 10` (VA `0x1012e904`, game RVA `0xa6973d`) — unique.
- Build gate: none. Sig 2 is only attempted if sig 1 matched (`if (iVar3 != 0) { … }`).
- Writes:
  - sig 1, offset `-0x26`: `EB` (1 byte) — turns a conditional `JZ` into `JMP`
    (game site `0x454d4b` currently holds `74 0f`).
  - sig 1, offset `+0x7b`: `E9` (1 byte) + relative jump to `0x10046780`.
  - sig 2, offset `+0`: `E9` (1 byte) + relative jump to `0x10046750`.
  - The handler also stores side pointers into globals `0x10159c6c` (`match+0x88`),
    `0x10159c70` (`puVar4+0x1c`) and `0x10159c68` (`puVar4+0xe`) for sig 2.
- Feature: SMPA60 crash fix. Trampoline `0x10046750` re-implements the object-link
  fixup (`cmp $0xdddddddd` poison check, then relink and tail-jump through
  `0x10159c68`/`0x10159c70`); `0x10046780` does the same for `ecx = [eax+0x14]` with
  tail jump through `0x10159c6c`.
- Verification: both signatures resolve uniquely to the sites and the sites match the
  recovered shapes.
- **Uncertainty:** the mapping from `_DAT_1015ad70`-relative globals to specific
  game functions is only inferred from the trampoline tails.

## 0x100468d0 — StartupTimeFix

- INI: `StartupTimeFix=1` ("fixes the 15+ second startup times before the game window
  even pops up").
- **No `FUN_10003830` call.** Instead the handler builds a pattern via
  `FUN_1005afa0(&vec, "68 00 C2 EC 80 68", GetModuleHandleA(0), 0)`, which returns a
  *vector of match addresses*, then loops over the vector and writes, at every
  `match + 6`, the single 32-bit constant **`0x101580d8`** (disassembly:
  `movl $0x101580d8,(%esi)`; Ghidra renders it `*lpAddress = &PTR_DAT_101580d8`).
  `0x101580d8` is an ASI data slot whose *contents* are `0x1012e964`, the ASI's own
  ASCII bytes `2A 2F 2A 00` = `"*/*"`; the handler writes the slot **address**, not
  the string address.
- Build gate: none.
- Verification (1.0.8.0): pattern `68 00 C2 EC 80 68` matches **three** RVAs:
  `0x477a42`, `0x8973d3`, `0x898853`, so three sites are patched at `match+6`:
  - `0x477a48` → currently `70 af e5 00`, the 32-bit immediate of
    `push $0xe5af70` at `0x477a47` (and `0xe5af70` holds `"*/*"` in the game);
  - `0x8973d9` → `2c 9c d8 00` (the analogous `push` immediate; `0xd89c2c` = `"*/*"`);
  - `0x898859` → `04 a5 d8 00` (`0xd8a504` = `"*/*"`).
  So the handler replaces the game's own `"*/*"` literal pointer with the ASI slot
  `0x101580d8` in three window/settings-creation call sequences.
- Feature: replaces three startup-time `"*/*"` argument pointers in window/settings
  creation calls so the game does not spend 15+ seconds resolving them.
- **Uncertainty:** whether the call site expects a `const char *` or a `char **`
  (i.e. whether writing the slot *address* rather than the string address is
  intentional) is not resolved; both interpretations are consistent with the recovery.
  `FUN_1005afa0` is used here as a **multi-match** pattern finder, not the
  single-match `FUN_10003830`, which is why this handler has no entry in
  `option_signatures.json` and patches a data-dependent number of sites.

## 0x10046a10 — SuperLODFix

- INI: `SuperLODFix=1` ("fix crash if superlod is spawned, disable this for GTAC as
  it's already fixed there").
- **No signature call.** The handler is entirely **build-gated and module-relative**:
  - `DAT_1015be04 == 8` (v1070): sets globals from `DAT_1015ad70 + {0x4a3d37, 0x4a3cef,
    0x58aae0}`, patches `DAT_1015ad70 + 0x4a3ce9` with `E9` + jump to `0x100469e0`.
  - `DAT_1015be04 == 9` (v1080): sets the same three globals from
    `DAT_1015ad70 + {0x540e67, 0x540e1f, 0x4dd2d0}`, patches `DAT_1015ad70 + 0x540e19`
    with `E9` + jump to `0x100469e0`.
- Build gate: `==8` and `==9` (matches `evidence/option_build_gates.json`).
- Global writes (module-relative, per build):
  - build 8: `0x101580ac = base+0x4a3d37`, `0x101580b0 = base+0x4a3cef`,
    `0x101580b4 = base+0x58aae0`.
  - build 9: `0x101580ac = base+0x540e67`, `0x101580b0 = base+0x540e1f`,
    `0x101580b4 = base+0x4dd2d0`.
- Verification (1.0.8.0 → build 9, image base 0x400000):
  - site VA `0x400000+0x540e19 = 0x940e19` currently holds
    `f6 40 72 10 74 48 …` (a `testb`), so the `E9` overwrites a real instruction.
  - `0x940e67` (`base+0x540e67`) holds `0f 57 c0 f3 0f 11 44 24 18 …`, a plausible
    continuation point.
  - `0x940e1f`, `0x8dd2d0`, `0x8a3ce9`, `0x8a3d37`, `0x98aae0` were inspected and all
    lie inside `.text` with plausible code/padding.
- Feature: hooks the SuperLOD spawn path with trampoline `0x100469e0`, which tests
  `byte [eax+0x72] & 0x10` and either tail-jumps through `0x101580ac`
  (build-specific "already fixed" path) or `0x101580b0`; the third global
  (`0x101580b4`) feeds `FUN_100469a0`, the `movswl [eax+0x2e]` comparison helper.
- **Uncertainty:** the semantics of the three per-build globals (`0x101580ac/b0/b4`)
  are only inferred from the trampoline; the exact game function at `0x940e19`
  (`+0x540e19`) is not identified by symbol.

---

## Cross-handler notes and unresolved items

1. **Loader dispatch.** Eight of the ten are called from the standard shape
   `pcVar30 = FUN_1005c160(ini, "Option", "default"); iVar18 = FUN_100014b0(pcVar30,
   0, 10); FUN_1004ac90(...); if (iVar18 == 1) { FUN_<handler>(); }` (i.e. the INI
   string is parsed with base 10 and the handler fires when the value equals 1).
   The two exceptions are `0x10045c50` (SkipIntro) and `0x10046640` (SkipMenu), which
   additionally set a loader bitfield bit (`local_e8 |= 4` / `|= 8`) and are gated by
   `FUN_10003e20()` (the recovered compatibility-module probe) as well. `0x10045490`
   and `0x100468d0` use the plain `== 1` shape with no bitfield bit.
2. **Absent signatures.** `RemoveRGSCLoginRequirement` (build 9 takes the RVA fallback)
   and many `SkipLauncher`/`SkipMenu` signatures are absent from 1.0.8.0. This is
   consistent with the recovered build gates and with
   `evidence/fallback_patch_sites.json`; it is not evidence of extraction error.
3. **Multi-match patchers.** `0x100468d0` (`FUN_1005afa0`) and one block inside
   `0x10045ce0` use the multi-match finder rather than the single-match
   `FUN_10003830`. Those handlers therefore have **no** entry in
   `option_signatures.json`, and their patched site count is data dependent.
4. **Trampoline tails.** Every `E9`/`E8` target above points into the ASI's own
   `.text` (`0x10038550`, `0x10045580`, `0x100455a0`, `0x100455e0`, `0x10038570`,
   `0x10045a30`, `0x10045a80`, `0x10045c30`, `0x10046610`, `0x10046750`, `0x10046780`,
   `0x100469e0`), all disassembled and recorded here as the recovered trampoline set
   for this batch.

### Explicitly unresolved

- `0x10045ad0`: the `E9` displacements at `+0x62` and `+0x10a` are read as raw
  relative displacements (verified against the game image) but their semantic role is
  not recovered.
- `0x10045c50`: the meaning of the `*400` / `0x1f40` clamp in the `0x10045c30`
  trampoline.
- `0x100468d0`: the semantics of the three `"*/*"` string-pointer arguments that are
  overwritten.
- `0x10046a10`: identification of the hooked function at `+0x540e19` / `+0x4a3ce9`
  and the meaning of the three per-build pointer globals.
