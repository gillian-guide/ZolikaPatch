# Shadow-features audit handoff (for the next agent)

> **CORRECTION (2026-10-15, session 15):** the `sub_1000B790` attribution below is WRONG.
> `sub_1000B790`'s disassembly (0x1000B790) is a path/string helper: it contains **no
> `sub_10003830` scan calls and no patch stores** (verified with objdump; 318 instructions,
> the only calls are the string builders and GetCurrentDirectory). The three pattern
> `8D 4C 24 50 51 68 04 05 00 00` / `8D 44 24 50 50 68 04 01 00 00` /
> `F3 0F 11 44 24 1C 0F 84 9E 00 00 00` are scanned ONLY by **sub_1000E870**, whose only
> caller is the ForceCarHeadlightShadows handler tail (`1001AF0A`, guarded by the parsed
> FCHS==1 flag). E870 *restores* the two push immediates (04/05, 04/01) and the jz (0F 84 9E
> 00 00) - all no-ops in a clean image. The `CarDynamicShadowFix@noFCHS` rows were therefore
> removed and replaced with five `ForceCarHeadlightShadows` sig_writes (see HANDOFF session 15).
> With CarDynamic=1/FCHS=1 the EA00 write lands first (arm order) and destroys E870's P2
> pattern, so the original's E870 P2 scan fails too (equivalent behavior).

Mission: **audit every shadow/light-related feature for 1:1 completeness against the original
ASI** (missing code, missing arms/paths, wrong offsets/gating), then fix what is missing and
verify. User-visible symptom driving this: with all shadow options `=1`, **our build does not
activate all the dynamic light sources the original does, while the original over-activates the
dynamic shadows until they visibly break (this breakage must be replicated 1:1; fixes only
later, as documented candidates).**

## 0. Ground rules (read first)

- Read `AGENTS.md` and `HANDOFF.md` (Session 13 at the end). Fidelity policy is strict:
  shipped behaviour must be 1:1 with the original ASI. Improvements/fixes do NOT ship; they go
  to `docs/parity_and_improvements.md` §4 with an INI escape hatch.
- The user runs **1.0.8.0** (and 1.0.7.0) only. `game_refs/ZolikaPatch.ini` has **every**
  shadow/light option `=1` (BuildingDynamicShadows, CarDynamicShadowFix, CarPartsShadowFix,
  CopSirensFlag, DualVehicleHeadlights, EmissiveLerpFix, ForceCarHeadlightShadows,
  ForceDynamicShadowsEverywhere, ForceShadowsOnObjects, NoImpostorHeightCutoff,
  NoLODLightHeightCutoff, PopupHeadlightsFlag, RemoveStaticCarShadows,
  RemoveStaticCutsceneCarShadows, ReversingLightFix, BuildingAlphaFix, ImprovedShaderStreaming,
  SuperLODFix).
- Original ASI: `game_refs/ZolikaPatch_original.asi.off` (base 0x10000000; IDB
  `ida_projects/ZolikaPatch.asi.i64`; saved decompiles `ZolikaPatch_decompiled/ida/*.c`).
  Loader = `sub_100523C0` (file `100523C0_sub_100523C0.c`).
- Per-build exes for pattern matrices: `game_refs/GTAIV*.exe`, `EFLC_*.exe`;
  `ZolikaPatch_reconstruction/tools/builds_scan.py <pattern>...`.
- Everything in the working tree is **uncommitted**; the tested ASI is
  `game_refs/ZolikaPatch.asi` SHA-256 `E4CD3B8E...`. Build like:
  `& "C:\Users\Gillian\scoop\apps\git\current\usr\bin\sh.exe" -c "cd /d/GitHub/OpenZDecomp/decomp && make native-test CXX=g++ BUILD_DIR=build/native-win"`
  (run `build/native-win/zpatch-test` **under gdb** - Defender blocks direct runs), `make
  msvc-asi` (74/74 exact bodies + aligned-SSE scan). Copy the ASI to `game_refs/` to install
  (game must be closed). XLivelessAddon is being turned **off** by the user; our GFWL guard
  handles it, the ORIGINAL crashes without it when `RemoveGFWLUpdatePopup=1` (do not run the
  original with XLA off + that key on).

## 1. Obfuscation gotchas that break naive searching

- The loader builds option names **reversed** as string literals (`sub_10057870/40` with e.g.
  `"swodahSthgildaeHraCecroF"` = `ForceCarHeadlightShadows`, `"xiFwodahSstraPraC"` =
  `CarPartsShadowFix`, `"galFsneriSpoC"` = `CopSirensFlag`, `"sthgildaeHelciheVlauD"` =
  `DualVehicleHeadlights`, `"xiFthgiLgnisreveR"` = `ReversingLightFix`), and some names are
  assembled from fragments (`sub_10001280(v, 0x40, "Ca%snam%s%swF%s", "rDy","icSh","ado","ix")`
  = `CarDynamicShadowFix`). Search for BOTH orientations and fragments.
- Handler bodies are tiny and do their work inline; several features are reached through
  **pusha-forwarder callback slots** (a ctor publishes `{callback, aux, reset}` via
  `lea X; add Y` obfuscated stores; the loader calls through the slot; a reset stub zeroes it
  afterwards). Always audit the whole **arm** (every callee of the arm block in
  `sub_100523C0`), not just the handler named after the option - the chain-A lesson.

## 2. Verified original-side arm map (from `100523C0_sub_100523C0.c`)

| option (key==1) | loader call(s) | handler/installer | known original writes |
| --- | --- | --- | --- |
| BuildingDynamicShadows | `sub_10005B20()` | scans `8B 47 28 C1 E8 06 83 E0 0F 83 C0` -> publishes dword_10159C20=match+15, dword_10159C28=match+112, writes E9 -> `sub_10005AF0`; second scan `50 68 04 01 00 00 6A 02 6A 00 E8` -> 0x00 at match+2 | shipped |
| DualVehicleHeadlights | `sub_10006770()` | build mask 0x7E (1000/1004/1040 only). Our only row is a bare E9 (not applied) + gate. **Audit: old-build E9 destination + publishes missing? irrelevant to 1080 but part of the set.** | partial |
| ForceDynamicShadowsEverywhere | `sub_1001EE50()` | scan `F7 46 4C 00 80 00 00 74` -> `90` at match+7 AND `90` at match+8 (two single-byte writes) | shipped as one 2-byte row (equivalent) |
| ForceShadowsOnObjects | `sub_1001EED0()` | scan `F7 47 40 00 10 00 00 74 07 81 4E 24 00 00` -> `EB` at match+7 | shipped |
| RemoveStaticCarShadows | `sub_10038800()` | scan `F3 0F 11 4C 24 0C 76 5B D9 45 14` -> `EB` at +6 | shipped |
| RemoveStaticCutsceneCarShadows | `sub_10038860()` | scan `F3 0F 11 44 24 0C 76 5B D9 45 14` -> `EB` at +6 | shipped |
| ReversingLightFix | `sub_10045430()` | scan `11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10` -> `60` at +8 (0x64 -> 0x60 field offset) | shipped |
| PopupHeadlightsFlag | `sub_10044590()` | EAX form (1060+): E9@0 -> `0x10044530`, EB@10 (75->EB), rebased operand @82, publish dword_1015D62C/28; EDX twin (1000..1040): E9@0 -> `0x10044560`; shares IDE parser hook `sub_10004110` -> `0x100040E0` | shipped (EAX + EDX) |
| CopSirensFlag | `sub_10006350()` | E9@0 -> `0x10006310`, E9@22 -> `0x10006330`, IDE parser E9 match-10 -> `0x100040E0`, publishes dword_1015937C/74, dword_1015BE10 | shipped |
| CarPartsShadowFix | `sub_10006910(); sub_10007E40();` | the 28 KB installer + the old-build variant EB (`83 F8 04 ... 75 0F`@+8, `83 FA 04 75 0C`@+3) | shipped (+@standalone spelling) |
| CarDynamicShadowFix | inside `if (key==1)`: reads CarPartsShadowFix (default 1) + ForceCarHeadlightShadows (default 0), then `sub_10022F40()` | **`sub_10022F40` -> `sub_10022F30` -> `call [0x1018E574]`** with arg1=(CarParts!=1) [float slot], arg2=(FCHS!=1). The slot's target + all its argument-dependent branches are THE unknown. Reset stub `detour_10022ED0` zeroes 0x1018E574/0x1018E57C/0x1018E578/0x10190ABC/0x10159D24 (their publisher ctor is unrecovered) | risky |
| ForceCarHeadlightShadows | own arm after the CarDynamic block: read key default "1"; if ==1 -> second read (default "0") -> `sub_1001ADD0(arg)` | `sub_1001ADD0` = installer (152 insns; E9s game fn to `0x1001ADB0`; scan `3C 01 75 50 84 DB 75 79` -> EB@6, E9@95->+476, EB@379, rebased global @211; publishes dword_1015B5BC) | shipped with a documented fault-avoidance guard; **arg semantics unaudited** |

## 3. Priority suspects (order of investigation)

1. **`dword_1018E574` installer chain (CarDynamicShadowFix / CarPartsShadowFix /
   ForceCarHeadlightShadows interaction).** Highest suspicion for the user's symptom. Tasks:
   a. Find the publisher ctor of `0x1018E574/0x1018E57C/0x1018E578/0x10190ABC/0x10159D24`
      (search the ASI for the obfuscated `lea X; add Y` stores; IDs via the reset-stub
      `detour_10022ED0`; same technique as chain A's ctor `sub_100440A0`). The published target
      of 0x1018E574 is almost certainly a wrapper around the **236 KB `sub_10010B60`**
      decompile (`10010B60_sub_10010B60.c`).
   b. Enumerate **every branch of the installer that depends on arg1/arg2** (CarParts!=1,
      FCHS!=1) and every write/publish in it; map 1:1 to our shipped rows
      (`recovered_op_corrections.json` + regenerated inc) and to the `@standalone`/`@noE`
      internal spellings. The user's combination is CarDynamic=1, CarParts=1, FCHS=1 - verify
      our write set for exactly that combination matches the installer's path bit-for-bit.
   c. Verify the bodies `0x100068A0`/`0x100068D0` (CarDynamicShadowFix / CarPartsShadowFix
      submission bodies) still byte-match (`make verify-bodies`); check the second body's
      slots (`dword_1015B5B8`, `dword_1015BDCC`, `dword_1015BDC4` = `zpatch_car_dynamic_d`).
   d. `dword_1018E57C` / `dword_1018E578` are also used by the pattern C/D/E branch - confirm
      our publishes for those (`zpatch_car_dynamic_d`, the E-fill NOP row) match.
2. **`sub_10006910` (28 KB) - the dynamic-shadow submission installer.** Enumerate every scan/
   write/publish; diff against our rows; specifically confirm pattern A (1060+) E9 ->
   `0x100068A0` + publishes, and that no additional per-build scans exist.
3. **IDE parser tables (`sub_10003FA0` -> our `abi_ide_parse`).** Sirens and pop-up lights are
   dynamic light sources; if our reimplemented parser misses tables/entries, those lights
   never activate. Compare field-by-field with the 2047-byte decompile; check the
   siren/popuplights/wheelangle model arrays (`zpatch_copsirens_models` etc.) against the
   original's globals; test on a scene with police + pop-up-headlight cars.
4. **ForceCarHeadlightShadows arg path**: `sub_1001ADD0(v244 != 1)` - determine what the arg
   does (the decompiled body is `1001ADD0_sub_1001ADD0.c`); if any of its writes are
   conditional, add the missing gate. Also re-check the documented fault-avoidance guard
   (parity doc §4) is the only divergence.
5. **NoImpostorHeightCutoff** (`match_literals`, two rows offsets 0/1 both 0x9090 - possible
   off-by-one class bug; verify against the original callback) and **NoLODLightHeightCutoff**
   (literal store 9090 - same check).
6. **DualVehicleHeadlights** on old builds: the bare E9 row is currently a
   `jump_opcode_pending` no-op; recover the handler body destination + publishes for
   1000/1004/1040 (pattern `8B 17 8B 42 64 8B CF FF D0 8B 54` in file
   `10006770_sub_10006770.c`).
7. **Vehicle-light tables** (`src/vehicle_tables.cpp`: `[PopupLightAngles]`/`[HeadlightConeOffsets]`):
   confirm the original reads them unconditionally in the preamble and our parse matches
   (hashing, degrees->radians, first-zero-hash stop semantics, the special
   `0xeb9f21d3` model, 512-slot order).
8. EmissiveLerpFix / SuperLODFix / ImprovedShaderStreaming shadow interaction: only if the
   above leave a delta - they were verified 1:1 earlier.

## 4. Method

Offline (per feature):
- Re-derive the arm and handler from the IDB (`idb_open
  ida_projects/ZolikaPatch.asi.i64`) / saved decompiles. Enumerate scans, writes, publishes,
  branches; each pattern -> `tools/builds_scan.py` across all builds; each write -> compare
  against the generated tables (`src/recovered_*.inc`) and the curated evidence
  (`evidence/op_corrections.json`, `evidence/patch_operations_all.json`). Report missing rows
  with the handler VA + exact offset/bytes.
- Add native tests in `tests/test_main.cpp` for each recovered row (1080 + 1040 style checks).

Live:
- Use the ZZWatch code-diff technique (`ZolikaPatch_reconstruction/tools/watcher/zzwatch.cpp`,
  deployed as `game_refs/ZZWatch.asi` + `.ini`): run the ORIGINAL and OURS on the same save/
  scene, then subtract our `game_refs/ZolikaPatch.log` write list (absolute VAs, game base
  0xC60000 + RVA) from the diff candidates. Leftovers = original-only patches (missing in
  ours); reverse leftovers = ours-only (over-application). Filter the DRM/reloc noise the
  watcher skips via `skip=0x1000` and the ~3% file-vs-mem relocation noise.
- For per-frame behaviour (dynamic light activation): extend the watcher to hook the shadow
  submission path (bodies at `0x100068A0`/`0x100068D0` and/or the game sites they replace) and
  compare the per-frame call/argument/object stream original-vs-ours on a fixed scene (e.g.
  Star Junction at night with many vehicles + police + pop-up-headlight cars).
- Batch user runs: each run must be decisive (only ask for a run when the offline work is
  exhausted and the observation needs the live game).

Deliverable: a per-option report (original behaviour -> our current behaviour -> delta),
with fixes only where recovery is proven; verification = native tests + `make msvc-asi`
(74/74 + aligned-SSE) + `make evidence`; install the ASI; document in `HANDOFF.md` (new
session section) and, for any deliberate divergence or original-breakage candidate, in
`docs/parity_and_improvements.md` §4.

## 5. Reference pointers

- Loader: `ZolikaPatch_decompiled/ida/100523C0_sub_100523C0.c` (arms ~lines 1780-1960 for
  these features; names reversed/fragmented).
- Handlers: `10005B20` (BuildingDynamicShadows), `10006770` (DualVehicleHeadlights),
  `1001EE50`/`1001EED0`, `10038800`/`10038860`, `10045430`, `10045590`/`10044560`,
  `10006350` + `10003FA0` (IDE parser), `10006910`, `10010B60`, `1001ADD0`,
  `10022F30/10022F40/10022ED0/10022D80`.
- Our artifacts: `evidence/op_corrections.json` (sig_installs/sig_writes/slot_installs),
  `src/recovered_patch_ops.inc`, `recovered_literal_stores.inc`,
  `recovered_match_literals.inc`, `recovered_rva_installs.inc`,
  `src/hook_bodies_win.cpp` (`@exact` bodies; `abi_ide_parse`, `abi_popup_headlights*`,
  `abi_copsirens_*`, `abi_fchs_shim`, `abi_carparts_shadow*`, `abi_building_dynamic_shadows`).
- Known shipped shadow work (do not redo, but re-verify): session 10 variant rows (trio
  fallback, CarDynamic B-E, Popup EDX twin, CarParts `83 FA 04 75 0C`), session 12
  ForceDynamicShadowsEverywhere 2-byte row fix, `docs/parity_audit.md` per-feature notes,
  `docs/parity_and_improvements.md` §4 (FCHS guard) and §5 (perf candidates).
- History to avoid repeating: generated pattern `485 DB 7E 41 55 EB 0A` is corrupt - the
  curated `85 DB 7E 41 55 EB 0A` row is authoritative; op rows for consecutive same-value
  writes must be single multi-byte rows; resume tail-jumps and slot-publish ordering rules
  in `AGENTS.md`.

## 6. AUDIT RESULT (session 14, 2026-10-14) - outcome per priority item

**P1 - `[0x1018E574]` chain: RESOLVED.** Publisher ctor = `sub_10001050` -> `sub_10022E00`,
which stores `0x1018E574=sub_10010B60` (the installer), `0x1018E578=sub_100068A0`,
`0x1018E57C=sub_100068D0`, `0x10190ABC=detour_10022ED0`. The loader forwards
`(a1=CarParts!=1, a2=FCHS!=1)`; `sub_10010B60` runs `sub_10006910` iff a1 and `sub_1000B790`
iff a2, then the A/B/C/D/E/F pattern chain and the unconditional `sub_1000EA00` step. The
reset stub is zero-op for the game (called after both body pointers were cached into locals).
Fixes shipped: `sub_1000B790`'s three writes + the same-site EA00 row were being applied for
FCHS=1 although the original skips them -> moved to `CarDynamicShadowFix@noFCHS` (§2 row 2 of
the session-14 HANDOFF section); the `@noE` fill got the 1004-excluding build gate
`{"CarDynamicShadowFix@noE", 0x22}`. Bodies `0x100068A0`/`0x100068D0` byte-match
(`verify-bodies` PASS); the C/D/E publishes match the handler math.

**P2 - `sub_10006910`: CONFIRMED.** Exactly one scan (`85 DB 7E 41 55 EB 0A`), E9 at match-6 ->
`0x10006880`, publishes `dword_1015BDC8=match+2` / `dword_1015BDC0=match+0x45`; no other
scans. Shipped rows match; the only trigger subtleties are the two loader arms already
modelled (`CarPartsShadowFix` value gate + `@standalone` spelling).

**P3 - IDE parser: DIVERGENCE FOUND + FIXED.** `abi_ide_parse` now leaves existing
wheelangle entries untouched (the original's scan just breaks) and takes the case-insensitive
`wheelangle:` prefix (`strnicmp`), matching the original `sub_10003FA0` and the portable
`ide_flags.cpp` model.

**P4 - `sub_1001ADD0` arg: CLOSED.** arg = (second read != first read) = "FCHS key absent".
It only selects `sub_1000E870`, whose three patterns are always pre-modified (when B790 ran)
or idempotent rewrites - a proven no-op; the omission is already documented.

**P5 - cutoff rows: VERIFIED.** NoImpostorHeightCutoff = 16-bit 0x9090 at matches[0] and
matches[1] (two match_literal rows are correct; the off-by-one suspicion is resolved);
NoLODLightHeightCutoff = 0x9090 at match+0. Note NoImpostor's handler default is "1".

**P6 - DualVehicleHeadlights: WAS MISSING ENTIRELY, NOW SHIPPED.** The arm-map entry above
("build mask 0x7E (1000/1004/1040 only)") was the extraction misreading the destination
selector as a gate - the handler installs on every build; only the E9 destination differs
(`sub_10006630` for ordinals 1..6, `sub_100064F0` otherwise). The old bare-E9 row never wrote
anything. Shipped: two `@exact` bodies, five publishes, two masked install rows; the live
original's run had the E9 at game 0x8F7CE5 (ours had nothing) - this is the main driver of
the user's under-activation symptom.

**P7 - vehicle-light tables preamble: VERIFIED 1:1** (512 slots, skip-not-stop on empty
names, degrees->radians for [PopupLightAngles], game-hash keys, 0xEB9F21D3 special).

**Related find outside the shadow set:** MiscFixes is skipped entirely by the original when
XLivelessAddon.asi is loaded (`byte_10159B84`, set at 0x10052DF3; `sub_10023DD0` guard) - our
runtime now mirrors that gate; this explains the session-13 MiscFixes row observations.

**Remaining live question (one run, with the .off in place):** 0x8F7422
(`push 0x104 -> 0x100`), written by both `sub_10005B20` and `sub_1000EA00` per the
disassembly, was UNCHANGED in the 05:00 original snapshot with FCHS=1. The same snapshot is
missing four more unguarded write-sets (IncreaseVehicleModelLimit 0xF3E8E8,
IncreaseVehicleStructLimit 0xA4A2B1, BikePhoneAnimsFix 0xB67F22, PlayerAsPedComponentsFix
0xB40809), and the two runs' `file=` bytes are byte-identical everywhere (exe unchanged), so
the 05:00 baseline was most likely a DIFFERENT (older) original binary than the .off. Our
build keeps the code-faithful EA00/BDS write; if a fresh .off run still shows 0x04, gate the
two EA00 `sig_writes` rows with `@noFCHS`.

**Installed binary:** `game_refs/ZolikaPatch.asi` SHA-256
`C96639A4925638F9B72E6534DFB42D7C6B912F6A29AE129D75E065C03F3FBDD8` (supersedes E4CD3B8E).
