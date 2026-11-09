# Option handler implementations — batch 3

Recovered read-only from `ZolikaPatch_extracted/ZolikaPatch.asi` (never modified).
Ten handlers, all reached from the INI option dispatch loop in the loader
`FUN_100523c0` (`ZolikaPatch_decompiled/ghidra/decompiled_0001.c`). The plugin is
linked at `0x10000000`; game-side addresses are GTA IV VAs (module base
`0x400000` as loaded in `/Users/kreato/Downloads/GTAIV.exe`, 1.0.8.0 / BuildSlot 9).

| # | handler | option (INI default) | build gate | patch vehicle |
|---|---------|----------------------|------------|---------------|
| 1 | `10022cf0` | AllowHashesForIDE (`1`) | none | `E9` rel32 → plugin stub |
| 2 | `10022d80` | InfiniteCarBottomDistance (`1`) | none | 2-byte `EB 2A` skip |
| 3 | `10022f40` | (helper; dispatched inside the `CarDynamicShadowFix=1` block, uses `CarPartsShadowFix` / `ForceCarHeadlightShadows`) | none in body (caller-gated) | thunk into build-adaptive stub |
| 4 | `10022f60` | OverrideGTADatPath (`0`) | none | pointer literal rewrite |
| 5 | `10022fc0` | IncreaseVehicleModelLimit (`1`) | none | 32-bit literal rewrite |
| 6 | `10023020` | IncreaseVehicleStructLimit (`1`) | none | 8-bit literal rewrite |
| 7 | `10023080` | IncreasePtrNodes (`1`) | none | 3 × 32-bit literal rewrite |
| 8 | `100234e0` | LoadDLCs (`1`) | `== 7`, `== 8`, `== 9` | per build: 4 inline jumps + 12×NOP + 2 plugin jumps + 3 global stores |
| 9 | `10023b30` | HighFPSSpeedupFix (`1`) | none | 8 × 2-byte NOP + 1-byte `EB` |
| 10 | `10023dc0` | HighQualityReflections (`0`) | none | no direct writes (calls `FUN_10023db0` → callback pointer) |

Verified signature match counts against the real game image (1.0.8.0) with
`tools/verify_game_signatures.py`:

```
10022cf0  8B 4C 24 08 ...         1 hit  @ 0x45CF50
10022d80  0F 2F D0 76 25 ...      1 hit  @ 0x8FB180
10022f60  68 ? ... E8            1 hit  @ 0x493948
10022fc0  83 C7 01 81 FF 18 79   1 hit  @ 0x8DD143
10023020  68 ? ? ? ? 6A 32 8B C8 1 hit  @ 0xA4A2AB
10023080a 6A 14 ...             1 hit  @ 0xBA47BE
10023080b 6A 08 ...             1 hit  @ 0x8DDADE
10023080c 6A 10 ...             1 hit  @ 0x8DDB1E
10023b30  0F 2F C1 76 03 ...     1 hit  @ 0x7CBFE9
10022f40  (none; dispatcher thunk)
100234e0  (none; RVA-based, build-gated)
10023dc0  (none; dispatcher thunk)
```

---

## 1. `FUN_10022cf0` — AllowHashesForIDE

- **INI key / default:** `AllowHashesForIDE=1` (`; allow entering raw hashes into .ide files if you add "hash:" ...`)
- **Signature string (1, `FUN_10003830` first-match):**
  `8B 4C 24 08 56 8B 74 24 08 80 3E 22` — game VA `0x45CF50`.
- **Write:** offset `+0`: `E9` (1 byte). Offset `+1`: rel32.
  In the ASI the target constant is `0x10003E50` and the stored displacement is
  `0x10003E50 - match - 5`; the patch site is the match itself, so the effective
  jump target is the plugin function **`0x10003E50`**.
  Game bytes: `E9 FB 6E BA 0F` at `0x45CF50` (orig `8B 4C 24 08 56 ...`).
- **Build gate:** none.
- **Feature (one line):** Redirects the `.ide` string parser entry into the plugin
  routine `0x10003E50`, which accepts `hash:`-prefixed unsigned decimal model
  hashes (the function returns either the parsed hash or a modified JOAAT value
  computed from the literal text), circumventing the `.ide` character limit.

---

## 2. `FUN_10022d80` — InfiniteCarBottomDistance

- **INI key / default:** `InfiniteCarBottomDistance=1` (`; remove the draw distance from static vehicle shadows`)
- **Signature string (1, `FUN_10003830`):** `0F 2F D0 76 25 0F 28 C2` — game VA `0x8FB180`.
- **Write (2 bytes, adjacent):**
  - `+3`: `EB` (was `76`, i.e. `JBE`)
  - `+4`: `2A` (was `25`)
  So `0F 2F D0 76 25` ("comisd xmm2,xmm0 / jbe +0x25") becomes
  `0F 2F D0 EB 2A` ("comisd… / jmp +0x2A"), an unconditional short jump.
  Effective branch destination: `0x8FB180 + 5 + 0x2A = 0x8FB1AF`.
- **Build gate:** none.
- **Feature (one line):** Makes the comparison that clamps static vehicle shadows
  to a draw distance always take the "no shadow" branch, removing the distance
  cutoff.
- **Notes / uncertainty:** the disassembly confirms both bytes; the alternative
  (post-rewrite) fallthrough at `+5` is never reached. `option_patch_data.json`
  lists the `eb` and `2a` writes as two separate operations, which is an artefact
  of the parser (they are one 2-byte rewrite).

---

## 3. `FUN_10022f40` — helper for the "Car…ShadowFix" gate in the loader

- **INI key:** not a key itself. It is called from the loader
  (`FUN_100523c0` at `0x1005458E`) inside the composed-key block that builds the
  name `Car%snam%s%swF%s` (from pooled fragments `rDy`, `icSh`, `ado`, `ix`) →
  `CarDynamicShadowFix`, and reads the sibling keys `CarPartsShadowFix` and
  `ForceCarHeadlightShadows`.
- **Signature strings:** none. The body (`push ebp; mov ebp,esp; movzx eax,[ebp+0xC];
  push eax; movzx ecx,[ebp+8]; push ecx; call 0x10022F30; add esp,8; pop ebp; ret`)
  simply forwards its two byte arguments to the thunk **`0x10022F30`**.
- **Write:** no direct writes in the handler. `0x10022F30` is
  `jmp DWORD PTR ds:0x1018E574` — the build-adaptive target pointer installed by
  the VTable/ctor function `FUN_10022E00` (`0x1018E574 = 0x10010B60`; the three
  sibling slots `0x1018E578 = 0x100068A0`, `0x1018E57C = 0x100068D0` and
  `0x10190ABC = 0x10022ED0` are filled in the same constructor). The `jmp [..]`
  therefore dispatches into **`0x10010B60`**, which performs the actual writes on
  its two byte parameters (`cmp [ebp+8],0 / call 0x10006910`, `cmp [ebp+0xC],0 /
  call 0x1000B790`, then a long table-driven virtual-protect/store sequence).
- **Build gate:** none recovered inside the body; the two arguments are the
  caller's `setne` results for `CarPartsShadowFix` and `ForceCarHeadlightShadows`.
- **Feature (one line):** Shared, argument-driven installer that (re-)applies the
  car shadow patches — `FUN_10006910` for `CarPartsShadowFix` (detached-part
  shadows) and `FUN_1000B790` for the headlight-shadow re-enable path — used when
  `CarDynamicShadowFix` is enabled.
- **Notes / uncertainty:** `0x1018E574` has no relocation entry and no static
  writer in `.text` (the writer `0x10023CC0`-style ctor stores it as a
  pointer via `FUN_10022E00`'s obfuscated `lea/add` pair, verified by arithmetic:
  `0xFF7D41B + 0x93745 = 0x10010B60`). Which of the two byte arguments maps to
  which option is confirmed from the caller's argument order: at `0x10054582`
  the loader does `push [ebp-0xC8]` (the ForceCarHeadlightShadows `setne`) then
  `push [ebp-0xDC]` (the CarPartsShadowFix `setne`), so under cdecl the *first*
  parameter = CarPartsShadowFix → `FUN_10006910`, and the *second* =
  ForceCarHeadlightShadows → `FUN_1000B790`. The intermediate routine `0x10022F30`
  is a plain import-style thunk. Depth of `0x10010B60`'s table was not fully
  enumerated here — only its two entry calls and its 4 `FUN_10003830` scans.

---

## 4. `FUN_10022f60` — OverrideGTADatPath

- **INI key / default:** `OverrideGTADatPath=0` (`; load gta.dat from another path`)
- **Signature string (1, `FUN_10003830`):**
  `68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8` — game VA `0x493948`.
  (The wildcard `?` bytes are the code's own `push`/`call` displacements, so the
  pattern is the literal 15-byte prologue of the gta.dat loader.)
- **Write:** offset `+1`, 4 bytes: the plugin's own ANSI string pointer
  **`0x10158068`** = `"common:/data/gta.dat"`.
  Game bytes at the site are `68 A0 96 E5 00 E8 ...`; the handler overwrites the
  `push` immediate `A0 96 E5 00` with `68 80 15 10` (`0x10158068`), i.e. it
  changes the argument of the gta.dat `push` from the game's own string to the
  plugin's. (`option_patch_data.json` records no `E9`/`E8` for this handler — it
  is a pointer-literal rewrite, not a jump.)
- **Build gate:** none.
- **Feature (one line):** Forces the "load gta.dat" call to use the plugin's
  hard-coded `common:/data/gta.dat` string; combined with the sibling
  `OverrideGTADatPathTo` key (which calls `FUN_1005B730` on a user string) it
  redirects gta.dat loading to an alternate path.
- **Notes / uncertainty:** the plugin string is `"common:/data/gta.dat"`, i.e. the
  *same logical path* the game would normally use — the effective override comes
  from `OverrideGTADatPathTo`, which the loader pushes separately into the same
  slot. Because the loader only calls this handler when `OverrideGTADatPath == 1`
  (INI default `0`), the pointer rewrite is inert unless the user also sets the
  "To" path. Recorded as **partially resolved**: the mechanism is certain, the
  end-to-end effect of the default value is not.

---

## 5. `FUN_10022fc0` — IncreaseVehicleModelLimit

- **INI key / default:** `IncreaseVehicleModelLimit=1` (`; increase the amount of possible vehicles.ide entries to 1024`)
- **Signature string (1, `FUN_10003830`):** `83 C7 01 81 FF 18 79 00 00 7C D2` — game VA `0x8DD143`.
- **Write:** the handler reads the dword at `match+0x4D` (the immediate operand of
  the matched code's own `mov ecx,0xF3E8E8`) and writes 4 bytes
  `00 04 00 00` = **`0x400` (1024)** to that address. `0xF3E8E8` is a game VA in
  `.data` whose current dword is `0xCD` (205) — the live model-limit counter.
  So the write is *indirect*: it does not patch code, it patches the limit
  variable named by the matched code's immediate.
- **Build gate:** none.
- **Feature (one line):** Raises the vehicle-model limit constant from the stock
  value (0xCD = 205) to 1024, the value the `cmp edi,0x7918` / loop bound in the
  scanned function tests against.
- **Notes / uncertainty:** the pattern's `0x7918` (`81 FF 18 79 00 00`) is the
  stock 31000-entry bound in the loop being edited; the relation between the
  loop bound and the `0x400` store is not proven (the store targets a data dword,
  not the immediate). Registered as the recovered behaviour only.

---

## 6. `FUN_10023020` — IncreaseVehicleStructLimit

- **INI key / default:** `IncreaseVehicleStructLimit=1` (`; increase the amount of vehicles with different models that can be loaded at one time`)
- **Signature string (1, `FUN_10003830`):** `68 ? ? ? ? 6A 32 8B C8` — game VA `0xA4A2AB`.
- **Write:** offset `+6` (the immediate byte of `push 0x32`), 1 byte **`64` (0x64)**.
  Site `0xA4A2B1`, was `32` (`push 0x32` = 50), becomes `push 0x64` = 100.
- **Build gate:** none.
- **Feature (one line):** Raises the per-model vehicle-struct pool size argument
  from 50 to 100 when the vehicle-struct array is allocated.
- **Notes / uncertainty:** only the immediate is rewritten; the surrounding call
  is unchanged, consistent with "increase the amount".

---

## 7. `FUN_10023080` — IncreasePtrNodes

- **INI key / default:** `IncreasePtrNodes=1` (`; increase the PtrNode limit, fixes random crashing in big Cunning Stunts maps`)
- **Signature strings (3, each via `FUN_10003830`, each writes `+8`):**
  1. `6A 14 ? ? ? ? ? 68 80 3E 00 00` — game VA `0xBA47BE`
  2. `6A 08 ? ? ? ? ? 68 80 38 01 00` — game VA `0x8DDADE`
  3. `6A 10 ? ? ? ? ? 68 80 3E 00 00` — game VA `0x8DDB1E`
- **Writes:** offset `+8`, 4 bytes each (little-endian) — the `68 imm32` size
  argument of each `push` sequence (the preceding `6A xx` class id is untouched):
  1. `00 FA 00 00` (**64000**) at `0xBA47C6` (was `80 3E 00 00` = 16000)
  2. `00 E2 04 00` (**320000**) at `0x8DDAE6` (was `80 38 01 00` = 80000)
  3. `00 FA 00 00` (**64000**) at `0x8DDB26` (was `80 3E 00 00` = 16000)
- **Build gate:** none.
- **Feature (one line):** Raises the three PtrNode pool-size arguments passed to
  the pool constructors (64000 / 320000 / 64000) so large Cunning Stunts-style
  maps no longer exhaust the pointer-node pool.
- **Notes / uncertainty:** the first immediate of each pair (`6A 14/08/10` = 20/8/16,
  a class/type id) is left untouched; only the size immediate is rewritten. The
  sites' exact original immediate values are visible in the disassembly
  (raw game bytes `6A 14 68 80 EB DC 00 68 80 3E 00 00`); the handler
  overwrites the third push's immediate, at `+8`.

---

## 8. `FUN_100234e0` — LoadDLCs

- **INI key / default:** `LoadDLCs=1` (comment: load TBoGT/TLaD in the EFLC folder
  structure, plus `Episode<N>` addon folders whose `setup2.xml` episode ids are
  `id*2`).
- **Signature strings:** none. This handler patches by **module-relative RVA**
  (`DAT_1015AD70`, the live module base) — the only handler in this batch that
  does so.
- **Build gate (recovered, exact):** three exclusive blocks,
  `DAT_1015BE04 == 7`, `== 8`, `== 9` (`evidence/option_build_gates.json`:
  `LoadDLCs -> [==7,==8,==9]`). On GTA IV 1.0.8.0 the slot is **9**.
- **Writes per build** (`base = DAT_1015AD70`; all via `VirtualProtect`; `E9` =
  `jmp`, `E8` = `call`, displacement `dest-site-5`):

  | build | site RVA | site VA | bytes | destination |
  |---|---|---|---|---|
  | 7 | `0x41450E` | `0x81450E` | `E9 19 00 00 00` | `0x81452C` (RVA `0x41452C`) |
  | 7 | `0x41453A` | `0x81453A` | `E8 F1 F8 FF FF` | `0x813E30` (RVA `0x413E30`) |
  | 7 | `0x41453F` | `0x81453F` | `90 90 90 90 90 90 90 90 90 90 90 90` | 12-byte NOP fill |
  | 7 | `0x41445C` | `0x81445C` | `E9 57 00 00 00` | `0x8144B8` (RVA `0x4144B8`) |
  | 7 | `0x4137CD` | `0x8137CD` | `E8 AE FC C0 0F` | plugin `0x10023480` |
  | 7 | `0x4144D4` | `0x8144D4` | `E9 B7 EF C0 0F` | plugin `0x10023490` |
  | 8 | `0x4142AE` | `0x8142AE` | `E9 19 00 00 00` | `0x8142CC` (RVA `0x4142CC`) |
  | 8 | `0x4142DA` | `0x8142DA` | `E8 F1 F8 FF FF` | `0x813BD0` (RVA `0x413BD0`) |
  | 8 | `0x4142DF` | `0x8142DF` | 12 × `90` | 12-byte NOP fill |
  | 8 | `0x4141FC` | `0x8141FC` | `E9 57 00 00 00` | `0x814258` (RVA `0x414258`) |
  | 8 | `0x41356D` | `0x81356D` | `E8 0E FF C0 0F` | plugin `0x10023480` |
  | 8 | `0x414274` | `0x814274` | `E9 17 F2 C0 0F` | plugin `0x10023490` |
  | 9 | `0x49040E` | `0x89040E` | `E9 19 00 00 00` | `0x89042C` (RVA `0x49042C`) |
  | 9 | `0x49043A` | `0x89043A` | `E8 71 F7 FF FF` | `0x88FBB0` (RVA `0x48FBB0`) |
  | 9 | `0x49043F` | `0x89043F` | 12 × `90` | 12-byte NOP fill |
  | 9 | `0x49035C` | `0x89035C` | `E9 57 00 00 00` | `0x8903B8` (RVA `0x4903B8`) |
  | 9 | `0x48F558` | `0x88F558` | `E8 23 3F B9 0F` | plugin `0x10023480` |
  | 9 | `0x4903D4` | `0x8903D4` | `E9 B7 30 B9 0F` | plugin `0x10023490` |

  In addition each block stores three plugin globals:
  - build 7: `DAT_1015804C = 0x4136E0`, `DAT_101580D0 = 0x414080`,
    `DAT_1015BDDC = base + 0x414560`
  - build 8: `DAT_1015804C = 0x413480`, `DAT_101580D0 = 0x413E20`,
    `DAT_1015BDDC = base + 0x414300`
  - build 9: `DAT_1015804C = 0x48F440`, `DAT_101580D0 = 0x48FE00`,
    `DAT_1015BDDC = base + 0x490460`

  All game-side bytes were re-read from the real image and match the RVA table
  (e.g. `0x89042C` starts `C6 86 64 01 00 00 00 …`; the 12 bytes NOPed at
  `0x89043F` are `8B CE E8 CA FA FF FF E8 F5 9B 25 00`, i.e. a `mov ecx,esi`
  plus two calls). For build 9 the NOP fill and the four inline jumps were
  byte-verified in the game image; only the two plugin-relative `E8`/`E9` targets
  come from the plugin's own constants.
- **Feature (one line):** Installs the EFLC-style addon-episode loader: it hooks
  the episode/loader routines (two plugin stubs `0x10023480` / `0x10023490`
  installed as `call`/`jmp`), NOPs a 12-byte guard, and rewrites three game
  globals per build so `dlc/<name>/content` plus `Episode<N>` folders are
  accepted as loadable episodes.
- **Notes / uncertainty:** the per-build sites were reported by the extraction as
  RVAs only; the `0x10023480` / `0x10023490` plugin stubs disassemble as
  `mov al,1; ret 8` and a 5-instruction zero-fill routine respectively, but which
  game function each replaces was **not** identified (the site RVAs do not
  resolve to exported symbols). The build-7/8 `E9 19`/`E9 57` and `E8 F1 F8`
  displacements are the *plugin-relative* jumps the original emits into the
  hook targets, not self-patches. Whether builds 7/8 are reachable at all with
  the supplied binary was not tested (the supplied image is build 9 only); the
  site RVAs for 7/8 were therefore **not** byte-verified against a real image.

---

## 9. `FUN_10023b30` — HighFPSSpeedupFix

- **INI key / default:** `HighFPSSpeedupFix=1` (`; fix game speeding up above 300FPS`)
- **Signature string (1, `FUN_10003830`):**
  `0F 2F C1 76 03 0F 28 C8 F3 0F 10 15` — game VA `0x7CBFE9`.
- **Writes (9, all relative to the match VA):**
  | offset | site VA | bytes | was |
  |---:|---|---|---|
  | `+3` | `0x7CBFEC` | `90 90` | `76 03` (`jbe +3`) |
  | `-7` | `0x7CBFE2` | `90 90` | `76 1D` (`jbe +0x1D`) |
  | `+0xE0` | `0x7CC0C9` | `EB` | `77 17` → `EB 17` (opcode only) |
  | `+0xE7` | `0x7CC0D0` | `90 90` | `76 03` |
  | `+0x16F` | `0x7CC158` | `90 90` | `76 1D` |
  | `+0x179` | `0x7CC162` | `90 90` | `76 03` |
  | `+0x25F` | `0x7CC248` | `90 90` | `77 17` |
  | `+0x266` | `0x7CC24F` | `90 90` | `76 03` |
  | `+0x2FC` | `0x7CC2E5` | `90 90` | `76 03` |
  All bytes re-verified in the game image; the `-7` write lands before the match
  (the injector is allowed negative offsets).
- **Build gate:** none.
- **Feature (one line):** Removes the frame-time scaling branches (NOPs the
  `comiss`/`jbe` guards that would scale physics by delta-time) so the game stops
  speeding up at very high FPS.
- **Notes / uncertainty:** the recovered JSON for this handler is incomplete
  (`evidence/patch_operations_all.json` records the `-7` site eight times and one
  `eb` at offset 224 = `+0xE0`, i.e. 9 operations for 9 *distinct* sites with
  wrong offsets; `option_patch_data.json` likewise). The handler's own
  disassembly (`0x10023B30`) shows nine sites, each `VirtualProtect(…,2,0x40)` +
  `mov WORD PTR [site],0x9090` except `+0xE0`, which is a 1-byte `mov BYTE PTR
  [site],0xEB`. Bytes above are therefore taken from the disassembly of the
  handler (authoritative) and confirmed against the game image; the JSON is an
  under-report and an **evidence/parser inconsistency**, not a code ambiguity.
  The `0x16F`/`0x179`/`0x25F`/`0x266`/`0x2FC` sites are in the same routine's
  second half; their functional relation to the first two is not proven beyond
  "same guard idiom". The `+0xE0` rewrite (`77 17` → `EB 17`) preserves the
  original branch destination (`0x7CC0E2`) while making it unconditional.

---

## 10. `FUN_10023dc0` — HighQualityReflections

- **INI key / default:** `HighQualityReflections=0` (`; make everything reflect
  the entire world except vehicles and peds, major performance hit`)
- **Signature strings:** none.
- **Body:** `push ebp; mov ebp,esp; call 0x10023DB0; pop ebp; ret` — it forwards
  to `FUN_10023DB0`, which is `pusha; lea ecx,[0x10192F14]; mov ecx,[ecx];
  call ecx; popa; ret`. So the handler performs **no writes itself**; it invokes
  the build-adaptive callback stored at `0x10192F14`.
- **Build gate:** none in the body.
- **Write (via the callback):** the callback slot `0x10192F14` is installed by the
  ctor `FUN_10023CC0` (`0x10190AC0 + 0x2454 = 0x10192F14`, value **`0x1001F390`**;
  the same ctor also fills `0x10192F18 = 0x100068D0` and `0x10196370`).
  `FUN_1001F390` reads its config-derived bytes and assembles a **pattern string
  at runtime** out of ASCII byte constants; decoding the `mov ebx,imm8` /
  `mov [mem],imm8` sequence in `0x1001F390..0x10022CDF` yields
  **`8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8`**, which resolves to **exactly
  one hit in the real game image at VA `0xC51311`** (verified with the project's
  own `parse_signature`/`find_all`). It then installs an `E9` hook at that match
  to redirect the reflection routine.
- **Feature (one line):** Routes the whole reflection pass through the plugin's
  replacement (installed at the runtime-built signature `8B 87 38 09 00 00 6A 08
  68 1D 13 00 00 50 E8`, game VA `0xC51311`) so everything in the world reflects
  except vehicles and peds.
- **Notes / uncertainty:** the handler is a pure thunk; the actual patch site is
  inside the callback installed by `FUN_10023CC0`. The callback's write bytes were
  **not** fully reconstructed here (the routine is ~5 KB of table-driven
  `VirtualProtect`/store code reading many INI-derived bytes); only its signature
  and the single matching game VA are asserted. Treat the exact replacement bytes
  as unrecovered.

---

## Cross-checks performed

- Every signature string above was re-matched against
  `/Users/kreato/Downloads/GTAIV.exe` with `tools/verify_game_signatures.py`
  `parse_signature`/`find_all`; each of the nine literal signatures resolves to
  **exactly one** hit at the VA shown (no ambiguity to disambiguate).
- Every game-side patch site listed for build 9 (LoadDLCs) was re-read from the
  image and matched the expected surrounding bytes.
- Plugin-side constants (`0x10003E50`, `0x10158068`, `0x1018E574`, `0x10192F14`,
  `0x10010B60`, `0x1001F390`) were resolved by arithmetic on the plugin's own
  obfuscated `lea`/`add` pairs and by import/global reads, not by guessing.

## Uncertainties (recorded, unresolved)

1. **`10022f40`** is a dispatcher thunk with no signature/gate of its own; its
   effective writes live in `0x10010B60`, whose full write table was not
   enumerated.
2. **`10022f60` (OverrideGTADatPath)** writes the plugin's own
   `"common:/data/gta.dat"` pointer — same logical path the game uses; the
   end-to-end override depends on the sibling `OverrideGTADatPathTo` key. Effect
   at the shipped default (`0`) is inert, which is consistent but not proven.
3. **`10022fc0`** writes `0x400` through a pointer loaded from the matched code's
   own immediate; the link between the `0x7918` loop bound in the pattern and the
   pointed-to limit variable is not established.
4. **`100234e0` (LoadDLCs)** build-7/8 site RVAs could not be byte-verified (the
   supplied image is build 9); the identity of the game functions replaced by the
   plugin stubs `0x10023480` / `0x10023490` is unknown.
5. **`10023b30`** the machine-readable evidence
   (`patch_operations_all.json`, `option_patch_data.json`,
   `literal_write_sites.json`) records nine *operations* for this handler with
   offsets that do not match the nine sites in its own body (all `-7` plus one
   `eb` at `224`); the site list here follows the handler disassembly and the
   game image instead. This is an unresolved evidence-extraction defect for this
   one handler, not a code ambiguity. Note also that the same defect means
   `option_jump_sites.json` reports a single `eb->None` for `10023b30`, which
   understates the NOP writes.
6. **`10023dc0` (HighQualityReflections)** replacement bytes are unrecovered; only
   the runtime-built signature (`8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8`,
   game VA `0xC51311`) and the `E9` installation are asserted.
7. **`1018E574` / `10192F14`** have no PE relocation entries and no static store
   in `.text`; they are written by constructor-time code that the decompiler
   renders as `lea`+`add` arithmetic. The decoded targets (`0x10010B60`,
   `0x1001F390`) are arithmetically exact, but the writers were not disassembled
   line-by-line.
