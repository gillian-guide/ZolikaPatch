# Option handler implementations — batch 1

Reconstruction documentation for ZolikaPatch option handlers
`0x10003e20, 0x10005500, 0x10005690, 0x10005800, 0x10005b20, 0x10005ca0,
0x10005e30, 0x10005ee0, 0x10006350, 0x10006770`.

Sources of evidence (read-only):

- Decompiled bodies: `ZolikaPatch_decompiled/ghidra/decompiled_0000.c` /
  `decompiled_0001.c` (`/* ===== ADDR : FUN_ADDR ===== */` markers).
- Disassembly: `ZolikaPatch_decompiled/checkpoints/zolika_full_disasm.txt`.
- Recovered JSON: `ZolikaPatch_reconstruction/evidence/option_handlers.json`,
  `option_signatures.json`, `option_jump_sites.json`, `option_patch_data.json`,
  `option_build_gates.json`, `literal_write_sites.json`,
  `patch_operations_all.json`, `option_references.json`, `option_flag_map.json`.
- INI descriptions: `ZolikaPatch_extracted/ZolikaPatch.ini`.
- Signature verification against the real game image
  (`/Users/kreato/Downloads/GTAIV.exe`, PE32, image base `0x400000`) via
  `tools/verify_game_signatures.py` (`find_all` / `parse_signature`).

## Conventions / common machinery

- Options are read in the dispatcher `FUN_100523c0` from `ZolikaPatch.ini`.
  For each key it calls a small read routine and then, when the parsed value is
  `1`, calls the handler. The option→handler mapping is in
  `evidence/option_handlers.json`.
- A handler locates game code with the pattern scanner `FUN_10003830`
  (single hit, returns first match, `0` if none) or with the multi-hit
  collector `FUN_10005afa0` (returns a `std::vector` in a stack object; callers
  index `[0]`, `[1]`, …). Both take a `LPCSTR` space-separated hex pattern with
  `?`/`??` wildcards.
- All game writes are guarded with `VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE=0x40)`
  before and restored afterwards.
- A typical hook is: write `0xE9` (`jmp rel32`) at the match start and the 4-byte
  little-endian relative displacement at match+1, where
  `rel32 = target_stub - (match+1)` in the *disassembly view*; the decompiler
  often prints the equivalent `stub - match` with the stub named 5 bytes before
  the real entry (the `UNK_...` labels land on `int3` padding, real stub is
  `UNK_.. + 5`). All targets below are given as the effective stub entry.
- `DAT_1015be04` is the internal **BuildSlot ordinal** set once by the loader
  `FUN_10055c70` from the host EXE version ("patch" number). Observed mapping
  (decompiled switch): `1000→1, 1004→6, 1040→5, 1060→7, 1061→15, 1070→8,
  1080→9, 1100→10, 1110→11, 1120→12, 1130→13, 12043→18`, else `0`.
  The supplied test binary is GTA IV **1.0.8.0**, so `BuildSlot == 9` there.
- Addresses in `0x1000xxxx`/`0x1010xxxx` range are inside the plugin image
  (ZolikaPatch.asi is linked at `0x10000000`); the game-site addresses printed
  in verification are game-image VAs (image base `0x400000`).

---

## 0x10003E20 — FusionFix compatibility probe (helper, not a toggled option)

- **Signature strings searched:** none. It is not a pattern-scanning patch;
  it calls `GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi")`.
- **What it reads / writes:** no code writes. It reads
  `*(uint32*)(module + 0x20D3)` and compares to `0xC7C47589`, and
  `*(uint16*)(module + 0x20D7)` and compares to `0x2446`.
  Returns `1` when the module is present **and** either value mismatches;
  returns `0` when the module is absent or both values match.
- **Build gate:** none.
- **Used by:** `SkipIntro`, `SkipMenu`, `BorderlessWindowed` (each calls it and
  only proceeds when it returns non-zero), i.e. "apply unless an incompatible
  FusionFix build is already handling this".
- **Plain English:** detects whether a *foreign/unknown* build of
  `GTAIV.EFLC.FusionFix.asi` is loaded (via fixed offsets/values) so those three
  options can defer to it.
- **Notes / uncertainty:** the disassembly confirms offsets `+0x20D3`
  (dword `0xC7C47589`) and `+0x20D7` (word `0x2446`); the *semantic* meaning of
  those two values (a FusionFix build marker vs. a patched-code signature) is
  inferred, not proven. This is the same predicate already reconstructed as
  `compatibility_module_lacks_signature` in `src/game.cpp`.

---

## 0x10005500 — shared hook installer for Patch4 / GFWL options

- **Signature string searched:** `2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08`
  (via `FUN_10003830`). Verified: **1 hit** in the game image at VA `0x7CBF70`.
- **What it writes:**
  - at `match+0`: byte `0xE9`;
  - at `match+1`: 4-byte rel32 to the stub at **`0x100054E0`** (decompiler shows
    `UNK_100054db`, i.e. stub entry `0x100054E0`);
  - stores `_DAT_10159c18 = match + 0xC` (the continuation address the stub jumps
    back to via `jmp [0x10159c18]`).
- **Build gate:** none.
- **Callers / options:** invoked (together with a decision byte) by
  `Patch4RemoveModChecks` (sets `DAT_10159c14=1`) and `RemoveGFWLUpdatePopup`
  (sets `DAT_10159b87=1`). The stub `0x100054E0` (`pusha; call 0x10005340;
  popa; sub eax,ecx; sbb edx,esi; ...; jmp [0x10159c18]`) dispatches into
  `FUN_10005340`, which performs the real edits.
- **Plain English:** installs a one-shot hook (stub `0x100054E0`) that lets
  `FUN_10005340` neutralize Patch 4 mod-check byte sequences and the GFWL update
  popup; both toggles funnel through this single installer.
- **Notes / uncertainty:** the hook body (`FUN_10005340`) is shared, so the two
  options patch *different* sites selected inside `FUN_10005340`
  (`DAT_10159c14` → game `+0x1E` relative pointer; `DAT_10159b87` → game
  offsets `+0x66F770`, `+0x66F774`, `+0x6B55D0`, `+0x6B55F0`, each set to
  `0x10000E83`). Which option maps to which write is clear from the flags, but
  the high-level purpose of the `0x10000E83` immediate was not independently
  validated.

---

## 0x10005690 — BenchmarkFix

- **Signature string searched:** `E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20`
  (via the multi-hit collector `FUN_10005afa0`; decompiled body uses up to the
  first **two** hits). Verified: **1 hit** in the game image at VA `0x47F600`.
- **What it writes (per hit, hits `[0]` and `[1]`):**
  - at `hit+8`: byte `0xE9`;
  - at `hit+9`: 4-byte rel32 to stub **`0x10005650`** (`hit[0]`) and
    **`0x10005670`** (`hit[1]`) (decompiler prints `UNK_1000564b` / `UNK_1000566b`);
  - stores `_DAT_1015ad50 = hit[0] + 0xD` and `_DAT_1015ad54 = hit[1] + 0xD`
    (continuations used by the stubs `jmp [0x1015ad50]` / `jmp [0x1015ad54]`).
- **Build gate:** applies only when `BuildSlot != 9` **and** `BuildSlot != 13`
  (recorded in `option_build_gates.json` as `["!=",9],["!=",13]`), i.e. **not**
  on the 1.0.8.0 binary.
- **Plain English:** hooks two call sites (offset +8 inside each match) so that,
  after a benchmark run ends, control is redirected into small stubs
  (`0x10005650` / `0x10005670`) that test the return value and either skip or
  resume — fixing the crash after the benchmark finishes.
- **Notes / uncertainty:** the decompiler prints offsets relative to the *vector
  element*; the two recovered continuations (`+0xD`) are consistent with the
  `E9`+rel32 (5 bytes) landing back at `hit+0xD`. The stub bodies call through a
  register (`call esi`) whose target is set by surrounding game code and was not
  further traced. The body guards only on `count != 0` and then indexes `[0]` and
  `[1]`; on the tested 1.0.8.0 binary the pattern matches once, consistent with
  the `BuildSlot != 9` gate suppressing this handler there (it would run only on
  a build where the pattern is expected to match twice).

---

## 0x10005800 — BetterMPSync

- **Signature string searched:** `8B 90 E8 00 00 00 55 8B CE FF`
  (via `FUN_10003830`). Verified: **1 hit** at VA `0x6D9356`.
- **What it writes:**
  - at `match+6`: byte `0xE8` (a `call rel32`, deliberately the **opcode `0xE8`**,
    not `0xE9`);
  - at `match+7`: 4-byte rel32 to the stub **`0x100057F0`**
    (`xor eax,eax; ret`, i.e. "return 0").
- **Build gate:** none recovered.
- **Plain English:** replaces a call site with a call to a stub that always
  returns 0, disabling the underlying interval/getter so the multiplayer sync
  interval grows when players are far apart (BetterMPSync).
- **Notes / uncertainty:** default in the shipped INI is `0` (off). Because both
  the original and the injected instruction are `call`s, the surrounding stack
  contract is unchanged; the exact callee being neutralised was not named.

---

## 0x10005B20 — BuildingDynamicShadows

- **Signature strings searched (three):**
  1. `8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06`
     (via `FUN_10005afa0`, vector; verified **2 hits**: `0x8DDBF8`, `0xBA9998`);
  2. `8B 47 28 C1 E8 06 83 E0 0F 83 C0` (via `FUN_10003830`; verified **1 hit**
     at `0x958955`);
  3. `50 68 04 01 00 00 6A 02 6A 00 E8` (via `FUN_10003830`; verified **1 hit**
     at `0x8F7420`).
- **What it writes / stores:**
  - sig 1 (read-only): `DAT_10159c1c = *(uint32*)(hit[0]+0x14)` and
    `DAT_10159c24 = *(uint32*)(hit[1]+0x14)` — pointers into game structures.
  - sig 2: at `match+0` byte `0xE9`; at `match+1` rel32 to stub **`0x10005AF0`**
    (decompiler `UNK_10005aeb`); stores `_DAT_10159c20 = match+0xF` and
    `_DAT_10159c28 = match+0x70` (far-branch continuations).
  - sig 3: at `match+2` a single byte `0x00` (neutralises the immediate/flags
    after the two `push` instructions).
- **Build gate:** none recovered.
- **Plain English:** enables night shadows cast by buildings: captures two live
  pointers used by the shadow pass (sig 1), installs a jump to the shadow stub
  `0x10005AF0` (sig 2), and clears one game constant (sig 3).
- **Notes / uncertainty:** sig 1 matching **twice** is expected (the collector
  takes `[0]` and `[1]`); which of the two game structures each slot refers to is
  not established. Default in the shipped INI is `0` (off).

---

## 0x10005CA0 — BuildingAlphaFix

- **Signature strings searched (two):**
  1. `F7 46 24 00 00 00 40 0F 85` (via `FUN_10003830`; verified **1 hit** at
     `0x959CD3`);
  2. `F7 46 24 00 00 00 40 0F 84` (via `FUN_10003830`; verified **1 hit** at
     `0x9594BD`).
- **What it writes:**
  - sig 1: 13 bytes at `match+0` set to `90 90 90 90 90 90 90 90 90 90 90 90 90`
    (all-NOP; removes the `test …,0x400000` + `jnz` sequence, disassembly shows
    three `mov dword` `0x90909090` plus one `0x90`).
  - sig 2: at `match+7` byte `0xE9`; at `match+8` a 4-byte rel32 computed as
    `(*(int32*)(match+9) + match) - (match+7) + 8` (rewrites the `0F 84` `jz`
    at `match+7` into a `jmp`; the decompiler expression reduces to
    `orig_rel32 + 1`).
- **Build gate:** none recovered (INI note says "disable on patch 5 and below",
  but no `BuildSlot` check exists in the body).
- **Plain English:** fixes the missing LOD fade-in on patch 6+ by NOP-ing a
  gate check (sig 1) and redirecting the paired `jz` branch (sig 2).
- **Notes / uncertainty:** the `e9` write in
  `evidence/literal_write_sites.json` is attributed to the sig-1
  (`0F 85`) entry, but the disassembly shows the `0xE9` belongs to the sig-2
  (`0F 84`) site. `option_jump_sites.json` has no `10005ca0` entry at all
  (`jumps: []`), so the second `E9` is under-reported by the automated extractor.
  This is an **evidence inconsistency**, not a code ambiguity — the bytes written
  are taken from the disassembly.

---

## 0x10005E30 — CollisionDeadlockFix

- **Signature string searched:** `55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66`
  (via `FUN_10003830`). Verified: **1 hit** at VA `0x4E6250`.
- **What it writes:**
  - at `match+0`: byte `0xE9`;
  - at `match+1`: 4-byte rel32 to stub **`0x10005D80`** (decompiler `0x10005d7b`);
  - stores `DAT_1015bdd4 = match + *(int32*)(match+0x5B) + 0x5F` — an interior
    pointer into the original function, used by the stub as a callback.
- **Build gate:** none recovered.
- **Plain English:** hooks the collision-detection routine and routes it through
  a stub (`0x10005D80`) that re-issues the original traversal with a substituted
  callback, breaking the collision deadlock.
- **Notes / uncertainty:** the stub `0x10005D80` reads a linked list via
  `[ebx+0x70]`, walks up to `0x2710` (10000) iterations and calls
  `[0x1015bdd4]` per node; the "deadlock" naming comes from the option string and
  INI comment, the exact re-entrancy it prevents was not independently proven.

---

## 0x10005EE0 — CombinedRadioFreezeFix

- **Signature string searched:** `E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0`
  (via `FUN_10003830`). Verified: **1 hit** at VA `0x895F59`.
- **What it writes:**
  - at `match+0`: byte `0xE8` (a `call rel32`);
  - at `match+1`: 4-byte rel32 to stub **`0x10005ED0`** (decompiler `UNK_10005ecb`),
    whose body is `mov eax,0x13; ret` (returns `19`).
- **Build gate:** none recovered.
- **Plain English:** replaces a game call with a call returning `19` (0x13), so
  the code takes a different path — fixing the freeze after the "Concrete Jungle"
  mission when the three EFLC-exclusive radio stations are installed.
- **Notes / uncertainty:** the meaning of return value `0x13` is a bare constant
  in the stub and was not tied to a named enum.

---

## 0x10006350 — CopSirensFlag

- **Signature strings searched (two):**
  1. `E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00` (via `FUN_10003830`; verified **1 hit**
     at `0x8FEBD3`);
  2. `0F BF 41 2E 39 05` (via `FUN_10003830`; verified **1 hit** at `0x8FABD0`).
- **What it writes:**
  - sig 1: at `match+0` byte `0xE9`; at `match+1` rel32 to stub **`0x10006310`**
    (decompiler `0x1000630b`); stores `_DAT_1015937c = match+5`.
    Stub `0x10006310`: `mov [0x10159370],edx; pusha; pushf; call 0x10005F70;
    popf; popa; mov eax,[0x10159b80]; jmp [0x1015937c]`.
  - sig 2: at `match+0x16` byte `0xE9`; at `match+0x17` rel32 to stub
    **`0x10006330`** (decompiler `UNK_1000632b`); stores `_DAT_10159374 = match+0x1C`.
    Stub `0x10006330`: `mov [0x1015936c],eax; pusha; pushf; call 0x10006260;
    popf; popa; cmp [0x10159378],1; jmp [0x10159374]`.
- **Build gate:** none recovered.
- **Plain English:** adds the `+siren` vehicles.ide flag handling so designated
  cars use sirens: two hooks let helper routines (`0x10005F70` / `0x10006260`)
  inspect a model name/hash and decide whether the vehicle is police-like,
  setting `DAT_10159b80` / `DAT_10159378`.
- **Notes / uncertainty:** the two helper routines check hard-coded model names
  (`police`, `police2`, `police3`, `police4`, `noose`, `pstockade`, `nstockade`,
  `polpatriot`, `fbi`, `n_police`, `n_police2`, `n_polpatriot`, `n_pstockade`).
  `options_patch_data.json` records the sig-2 site offset as `22` (`0x16`), which
  matches the disassembly.

---

## 0x10006770 — DualVehicleHeadlights

- **Signature string searched:** `8B 17 8B 42 64 8B CF FF D0 8B 54`
  (via `FUN_10003830`). Verified: **1 hit** at VA `0x8F7CE5`.
- **What it writes / stores:**
  - reads five interior pointers into the game function:
    `DAT_10159bcc = *(uint32*)(match+0xCF)`,
    `DAT_10159bd4 = match + *(int32*)(match+0x14B) + 0x14F`,
    `DAT_10159bd0 = match + *(int32*)(match+0x8C) + 0x90`,
    `DAT_10159be4 = match + *(int32*)(match+0x97) + 0x9B`,
    `_DAT_10159be0 = match + 0x152`;
  - writes at `match+0` byte `0xE9`; at `match+1` rel32 to **one of two stubs**
    selected by build: `BuildSlot ∈ {1,2,3,4,5,6}` → stub **`0x10006630`**, else
    stub **`0x100064F0`** (decompiler `LAB_10006630` / `LAB_100064F0`).
- **Build gate:** positive — `BuildSlot` in `{1,2,3,4,5,6}`
  (`option_build_gates.json`: `==1..==6`). Chosen at runtime; the hook is still
  installed on other builds, only the destination differs.
- **Plain English:** enables a second, independently shadowed headlight light for
  cars; grabs several view/model pointers from the headlight routine and redirects
  it to a per-build version of the replacement logic.
- **Notes / uncertainty:** the two destination stubs (`pusha`-prefixed) were not
  disassembled in full here; the difference between them (build-specific
  register/struct layout) is inferred from the build switch, not proven.
  Default in the shipped INI is `0` (off).

---

## Summary table

| Handler | Option(s) | Signature(s) (1st anchor) | Write | Target | Build gate | Feature |
|---|---|---|---|---|---|---|
| `0x10003e20` | (helper) | — (module check) | none | — | none | FusionFix compatibility probe for SkipIntro/SkipMenu/BorderlessWindowed |
| `0x10005500` | Patch4RemoveModChecks, RemoveGFWLUpdatePopup | `2B C1 1B D6 …` | `E9`+rel32 @+0/+1; store `+0xC` | `0x100054E0` | none | shared hook installer for Patch4 mod-check / GFWL-popup edits |
| `0x10005690` | BenchmarkFix | `E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20` | `E9`+rel32 @+8/+9; stores `+0xD` | `0x10005650`,`0x10005670` | `!=9 && !=13` | fix crash after benchmark ends |
| `0x10005800` | BetterMPSync | `8B 90 E8 00 00 00 55 8B CE FF` | `E8`+rel32 @+6/+7 | `0x100057F0` (ret 0) | none | raise MP sync interval when far away |
| `0x10005b20` | BuildingDynamicShadows | 3 sigs (see above) | `E9`+rel32 @+0/+1 (sig2); `00` @+2 (sig3); pointer stores (sig1) | `0x10005AF0` | none | enable night shadows cast by buildings |
| `0x10005ca0` | BuildingAlphaFix | `F7 46 24 00 00 00 40 0F 85` / `… 0F 84` | 13×`90` @+0 (sig1); `E9`+rel32 @+7/+8 (sig2) | inline redirect | none | restore LOD fade-in on patch 6+ |
| `0x10005e30` | CollisionDeadlockFix | `55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66` | `E9`+rel32 @+0/+1; store `match+[+0x5B]+0x5F` | `0x10005D80` | none | break the collision deadlock/freeze |
| `0x10005ee0` | CombinedRadioFreezeFix | `E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0` | `E8`+rel32 @+0/+1 | `0x10005ED0` (ret 0x13) | none | fix freeze after Concrete Jungle with EFLC radio stations |
| `0x10006350` | CopSirensFlag | `E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00` / `0F BF 41 2E 39 05` | `E9`+rel32 @+0/+1 and @+0x16/+0x17; stores `+5`,`+0x1C` | `0x10006310`,`0x10006330` | none | make +siren vehicles.ide flag work |
| `0x10006770` | DualVehicleHeadlights | `8B 17 8B 42 64 8B CF FF D0 8B 54` | `E9`+rel32 @+0/+1; 5 pointer stores | `0x100064F0` / `0x10006630` | `BuildSlot ∈ {1..6}` selects stub | two separately-shadowed car headlights |

## Verification summary

Signatures in `evidence/option_signatures.json` / handler bodies were re-matched
against the real game image with `tools/verify_game_signatures.py`; every handler
signature resolved to exactly the expected number of hits:

```
0x10005500  -> 1 hit  0x7CBF70
0x10005690  -> 1 hit  0x47F600
0x10005800  -> 1 hit  0x6D9356
0x10005B20a -> 2 hits (0x8DDBF8, 0xBA9998)   [collector expects 2]
0x10005B20b -> 1 hit  0x958955
0x10005B20c -> 1 hit  0x8F7420
0x10005CA0a -> 1 hit  0x959CD3
0x10005CA0b -> 1 hit  0x9594BD
0x10005E30  -> 1 hit  0x4E6250
0x10005EE0  -> 1 hit  0x895F59
0x10006350a -> 1 hit  0x8FEBD3
0x10006350b -> 1 hit  0x8FABD0
0x10006770  -> 1 hit  0x8F7CE5
```

(`0x10003E20` has no signature; it is a module query.)

## Uncertainties (recorded, not resolved)

1. **`0x10003E20` values.** Offsets `+0x20D3`/`+0x20D7` and values
   `0xC7C47589`/`0x2446` are exact from the disassembly; their interpretation as a
   *version* predicate is inferred.
2. **`0x10005500` / `FUN_10005340`.** The `0x10000E83` immediate written at game
   `+0x66F770` etc. and the zeroing at `[DAT_1015bdd4]` are literal; the
   higher-level intent (which launcher/GFWL branch is neutralised) is not proven.
3. **`0x10005CA0` evidence mismatch.** `literal_write_sites.json` mis-attributes
   the `0xE9` byte to the `0F 85` signature; disassembly shows it belongs to the
   `0F 84` signature (`option_jump_sites.json` omits `10005ca0` entirely).
4. **`0x10006770` dual stubs.** The two destination stubs (`0x100064F0`,
   `0x10006630`) were not fully disassembled; their build-specific difference is
   inferred from the `BuildSlot` switch.
5. **`BuildSlot` ordinal 13** (excluded by BenchmarkFix) maps to host build
   `1130` per the loader switch; whether 13 denotes a specific EFLC build was not
   independently confirmed.
6. **Signature-2 (`0x10005B20`) single hit** is taken from `FUN_10003830`
   (first-match); the body performs only that one installation, consistent.
7. **Stub return constants** `0x13` (`0x10005ED0`) and `0` (`0x100057F0`) are bare
   immediates; no symbolic meaning recovered.
