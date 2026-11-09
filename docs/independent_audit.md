# Independent adversarial audit — ZolikaPatch reimplementation

Auditor: independent verification session (audit-only; no `src/`, `include/`,
`evidence/`, `tests/` or existing-doc edits). This file is the only artifact written.

Revision audited (working tree, uncommitted — this repo carries concurrent edits from
another session; hashes below pin exactly what was audited):

| file | sha256 (first 16) |
|---|---|
| `game_refs/ZolikaPatch_original.asi.off` | bc01bcbc12b4e2b0 |
| `build/msvc-x86/ZolikaPatch.asi` | 5ff72aeb1bf0de6e |
| `build/native-win/zpatch-test.exe` | 61372934c8b672e1 |
| `ZolikaPatch_reconstruction/src/hook_bodies_win.cpp` | 29cc1452428c2fd3 |
| `ZolikaPatch_reconstruction/src/runtime.cpp` | f3721b4f4f2eff44 |
| `ZolikaPatch_reconstruction/src/config.cpp` | 796b17f30385b07e |
| `ZolikaPatch_reconstruction/src/rebased_values.cpp` | c6fe86caa2eadf6b |
| `ZolikaPatch_reconstruction/src/recovered_patch_ops.inc` | 3584972a0cb515d6 |
| `ZolikaPatch_reconstruction/src/recovered_rva_installs.inc` | bc9441e8347d29ad |
| `ZolikaPatch_reconstruction/evidence/op_corrections.json` | 8c84c74e918f47d9 |

Git: HEAD `43c6cc5`, tree dirty (37 files modified, including the files above).
The original ASI was **not** modified during the audit; all its VAs below are
image-base `0x10000000` and were read through the PE section table (my own mapper,
not `read_asi.py`): `.text` VA−0x101000, `.rdata` VA−0x10C00 → file offset = VA−0x10000000−0xC00
(holds for `.text` and `.rdata` only; `.data` is VA−0x101200 — the `-0xC00` shortcut
must not be used there). The game exes map with VA−0x400000.

> **Historical snapshot.** This independent audit pins an earlier dirty-tree
> revision. Its F1/F8/F12/F13/F14/F19/F22 findings have since been fixed or
> closed as verified-harmless; see `docs/parity/ledger.json`, `PARITY.md`, and
> the 2026-10-21 session-19/20 entries in `HANDOFF.md` for the current proof.
> Remaining P14/P15 adjacent-data/heap fault shapes and excluded builds are
> still documented boundaries; do not interpret old row statuses below as the
> current body ledger.

---

## 1. Method & environment

* Tooling: Python 3 + capstone x86-32 (my own disassembly/PE/scan scripts in
  `%TEMP%\kilo\audit_indep\`; independent re-implementations, not the repo tools).
* All claims were re-derived from the **bytes** of `ZolikaPatch_original.asi.off`
  and the game executables. Decompiled C (`ZolikaPatch_decompiled/ida/*.c`), the
  repo's generators and `zpatch-test` were used only as hypotheses, never as evidence.
* Built and ran `build/native-win/zpatch-test.exe` (crashes — see F8) and rebuilt
  `msvc-asi` (the shipped ASI at the hash above) and wrote my own
  instruction-stream comparer for the hook bodies (normalizes every operand in
  `[0x10000000,0x10200000)` to `A`, compares to the original's true function end,
  including past internal `ret`s).
* No live game run was performed by me (no launches); live-only evidence is marked
  unverifiable.
* Builds available for scanning: 1000, 1004, 1010, 1040, 1060, 1070, 1.0.8.0
  (`GTAIV.exe`), `GTAIV_12059.exe`, EFLC 1110/1120/1130. **Missing:** 1061 (per
  AGENTS.md = 1070 code), 1100 (SecuROM), 12043 (only 12059 exists).

Key command shapes used (reproducible):

```
# disassemble / read original ASI through its section table
python -c "from zp import asi, fmt_insns; print(fmt_insns(asi().disasm(0x10052E10, 40)))"
# scan a pattern in a game build
python -c "from zp import game; print(game('1080').scan('68 ?? ?? ?? ?? 68 A9 54 14 51 E8'))"
# hash helpers: zp.asi().read(va,n), zp.game(b).read(va,n)
```

---

## 2. Summary tables

### 2.1 Options (98)

Status legend: **C** = confirmed from the binaries (arm present in the loader with
the stated key/default semantics + handler/write-set consistent with the shipped
rows), **F** = finding (see §3), **U** = unverifiable (see §4). The "arm" column is
my independent extraction; "rows" = the repo tables are consistent with the bytes I
re-read (spelling/per-row detail sampled or fully re-derived as noted).

| option | status | evidence pointer |
|---|---|---|
| AddOptionsInMenu | C | key 0x101302C0, ref 0x1005327C; value→flag [0x10158038] (0x10053337 `setne`) |
| AimProjectileCrashFix | C | key @0x10130840; handler 0x100055C0: scan 0x10129870, E9@match→body 0x10005590 |
| AllowHashesForIDE | C | key @0x10130644; handler per arms table |
| BadPlayerModelCrashFix | C | key @0x10130944 (default "1"); handler 0x10037BE0 |
| BenchmarkFix | C | key @0x10130790; bodies abi_benchmark_a/b byte-verified |
| BetterMPSync | C | key @0x10130ABC (default "0"); body abi_better_mp_sync byte-verified |
| BikeFeetFix | C | key @0x10131590 |
| BikePhoneAnimsFix | C | key @0x10131640 |
| BlackscreenFix | C | key @0x101319C8; body abi_blackscreen_shim byte-verified |
| BorderlessWindowed | C/F9/F12 | key @0x10132264, default "1", bitmap bit 0x10; row conflict → F9; FusionFix gate missing → F12 |
| BuildingAlphaFix | C | key @0x10130A60; pattern-2 branch kept unapplied per policy |
| BuildingDynamicShadows | **F4** | arm default "1" (push 0x101300F4 @0x10054191), coverage/catalog say "0" |
| CarDecalsFix | C | key @0x10130138; body abi_car_decals_fix byte-verified |
| CarDynamicShadowFix | C | key is **runtime-built**: `_snprintf_s` @0x1005446C with fmt `"Ca%snam%s%swF%s"` (0x10130FB8) + `"rDy"`,`"icSh"`,`"ado"`,`"ix"` = "CarDynamicShadowFix"; gate `cmp esi,1` @0x100544AD; `sub_10022F40(CarParts!=1, FCHS!=1)` @0x1005458E |
| CarPartsShadowFix | C | rev key 0x10131010 @0x100544B6/0x10054658; shared path semantics match runtime special-case |
| CollisionDeadlockFix | C | key @0x101307E4; body abi_collision_deadlock_walk byte-verified (73 insns incl. internal ret path) |
| CombinedRadioFreezeFix | C | key @0x101305EC; body byte-verified |
| CopSirensFlag | C | rev key 0x1013137C @0x10054946; handler 0x10006350; collector claims re-checked (see §3/F10 notes) |
| CrashMsgFix | C | key @0x101310D4; body abi_crash_msg_shim INFO (compiled), semantics read |
| CutsceneFixes | C | rev key 0x10131B30 @0x10055048; chains A/B re-derived (§2.3) |
| DebugPrintSpawns | **F6** | read **inside FreezeCarFix's arm only** (0x10053F7F-0x10053FBD), stores flag 0x10159C15; coverage handler "0x0" is a classification artifact |
| DoNotPauseOnMinimize | C | key @0x1013220C |
| DrivebyFOVFix | **F1** | key @0x10130428; arm installs (pattern `F3 0F 10 46 60 F3 0F 5C CA` 1 hit) — shim handle wrong → see §3 |
| DualVehicleHeadlights | **F4** | rev key 0x10130C28, arm default "1"; coverage/catalog "0" |
| EmissiveLerpFix | C | key @0x10131540; body abi_emissive_lerp_shim INFO |
| EpisodeSwitchFreezeFix | C | key @0x10130588; bodies byte-verified (sum/store/shim_b/vcall/shim_c) |
| EpisodicVehicleSupport | C | key @0x1013175C; handler 0x1001B580 has `push 0xD` fills (13×0x90) → suppressed 8-byte row is a mis-split, 13-byte fill shipped |
| EpisodicWeaponSupport | C | key @0x101317C4 |
| FastLoading | C | rev key 0x101306F4 @0x10053D94; forwarder 0x1001EC90, body 0x1001EC60, reset 0x1001EC30 zeroes slots via `lea/sub` chains (verified) |
| FixPCIDKickbug | C | key @0x10131AD4 |
| ForceCarHeadlightShadows | C | rev key 0x10131070 ×2 reads (0x1005451E default "0", 0x100545A2 default "1") |
| ForceDynamicShadowsEverywhere | C | key @0x10130C84 |
| ForceShadowsOnObjects | C | key @0x101315E0 |
| FreezeCarFix | **F6** | key @0x1013089C (default "1"); arm also reads DebugPrintSpawns and calls installer 0x10037C70 |
| FreezeGunFix | C | key @0x10131A28; body byte-verified |
| GarageModelFix | C | rev key 0x10132068; reset stub 0x100508xx chains verified |
| GroupHackFix | C | key @0x10131A80; body byte-verified |
| HighFPSBikePhysicsFix | C | key @0x101309A4; sig-jump row targets 0x1001EF60 only (no live misdirection) |
| HighFPSSpeedupFix | **F19** | key @0x10130A04; handler writes 9 sites; only 1 of 8 NOPs shipped (8 duplicate rows) |
| HighQualityReflections | C | chain fully re-derived (see §2.3); body 0x1001F030 (12 builder calls, flags 0x11D, mode read-only) |
| ImprovedShaderStreaming | C | key @0x10131DE0; INFO bodies (find_hpe/find_v) |
| IncreasePtrNodes | C | key @0x10131C48 |
| IncreaseVehicleModelLimit | C | key @0x101311DC |
| IncreaseVehicleStructLimit | C | key @0x10131234 |
| InfiniteCarBottomDistance | C | key @0x10130CEC; handler 0x10022D80 writes `EB`@match+3 and `2A`@match+4 (byte-verified) → suppressions correct |
| LaunchWithPlayGTAIV | **F4** | key @0x10131E38, arm default "0"; coverage/catalog "1" |
| LoadDLCs | C | key @0x10130748; handler 0x100234E0 contains the shipped per-build constants (0x414560 @0x100236B1) |
| LocalSettingsPath | C | key @0x10132650; detours byte-verified |
| MPNikoCrashFix | C | key @0x10130B18 |
| MiscFixes | C | key @0x10132428; handler 0x10023DD0; per-fix split reverted (policy) |
| MouseFix | C | rev key 0x101321B8; constant pool byte-verified (§2.3) |
| MovingPlaneParts | C | rev key 0x10131488 @0x10054A86 |
| MovingSteeringWheelSupport | C | rev key 0x10131428 @0x10054A18 |
| NewFilesCompatibility | C/F13 | key @0x101324DC; bodies abi_newfiles_*; GTAC/JacksIVFixes gate missing (F13) |
| NewMemorySystem | C | key @0x10130478 @0x10053B4F |
| NewSavesCompatibility | C | key @0x10132540 |
| NoImpostorHeightCutoff | C | key @0x10130368 @0x10053583 |
| NoLODLightHeightCutoff | C | key @0x101303C8 @0x100537C8 |
| NoLiveryLimit | C | rev key 0x101318FC |
| NoLoadingSleep | **F4** | key @0x101320BC (= "NoLoadingSleep" verified), arm default "0"; coverage/catalog "1" |
| NoPhysicsSleep | C | key @0x10130314 @0x100533DB |
| OutOfCommissionFix | C | rev key 0x10132370; payload byte-verified (§2.3) |
| OverrideGTADatPath | C | key 0x10131128 + `To` value read (0x10131184) |
| OverrideGTADatPathTo | C | read at 0x10054768/0x100547AA (string semantics) |
| Patch4RemoveModChecks | C | key 0x10131F58 @0x10055453; value==1 → `[0x10159C14]=1; call 0x10005500` |
| Patch6and7RemoveModChecks | **F4** | key 0x10131E94, arm default "0"; coverage/catalog "1"; per-build E9 row 0x3982→0x3A09 re-derived from handler |
| Patch7RemoveModChecks | **F7** | loader reads it (site 0x100553A9, default "0", handler 0x10044710); not in coverage/catalog/INIs |
| PathNodeCrashFix | C | key @0x10130698; body withheld (shim list) |
| PedModelsInCutscenesFix | C | key @0x101325A0; body INFO w/ documented divergence |
| PipeBombDropIconSupport | C | key @0x1013052C |
| PlaneCrashFix | C | key @0x1013132C; body withheld |
| PlayerAsPedComponentsFix | C | key @0x10131CEC |
| PoliceEpisodicWeaponSupport | C | key @0x101304CC |
| PopupHeadlightsFlag | C | rev key 0x101313D0; handler 0x10044590; collector runs via CopSirensFlag per docs |
| RemoveAllWeaponsProtection | C | key @0x10131BE4 |
| RemoveBoundingBoxCulling | C | key @0x101316FC; original branch kept for 0 |
| RemoveGFWLUpdatePopup | C | key 0x10131FB0 @0x100554A2; value==1 → `[0x10159B87]=1; call 0x10005500` |
| RemoveRGSCLoginRequirement | **F4** | key 0x1013195C, arm default "0"; coverage/catalog "1" |
| RemoveStaticCarShadows | C | key @0x10130D50 |
| RemoveStaticCutsceneCarShadows | C | key @0x10130DB0 |
| RemoveUselessChecks | C | key 0x10130BD0; 23-row RVA group sampled |
| RestoreDeathMusic | C | key @0x101325FC; `ini_option_enabled` menu special-case matches arm's menu override claim (not re-derived live) |
| ReversingLightFix | C | rev key 0x10130E1C |
| SCOSignatureFix | C | key @0x10131CA0 |
| SMPA60Fix | C | key @0x10131288; body withheld |
| SavegameFix | C | key @0x10132318; sig_write + suppression 0x10045610 verified (pattern match) |
| ShakingEngineBlock | C | rev key 0x101314E0 |
| SkipIntro | C/F12 | key @0x10132114, default "1", bit 4 @0x10055663; handler 0x10045C50; FusionFix gate missing (F12) |
| SkipLauncher | C | key 0x10132010, default "1", handler 0x10045CE0 |
| SkipMenu | C/F12 | key @0x10132164, default "0", bit 8; handler 0x10046640 (post-guard); FusionFix gate missing (F12) |
| SpeedupBugFix | C | key @0x10132480 |
| StartupTimeFix | C | key @0x101323D0 |
| SuperLODFix | C | key @0x10131D48; RVA slot rows present |
| TBoGTHeliHeightLimit | C | key @0x10131830 |
| TeleportProtection | C | key @0x10131B88 |
| TryToSkipAllErrors | C | key @0x101312D8 |
| VRAMFix | C | key @0x101322C4; body abi_vram_fix_shim INFO |
| VSyncFix | **F4** | key @0x10131D94, arm default "0"; coverage/catalog "1" |
| WantedLevelCrashFix | C | key @0x10131898; body byte-verified |
| Patch7RemoveModChecks | (see Patch7) | |

Independent key-coverage result: **97/98 keys are read by the loader** as plain
`.rdata` strings with an in-loader reference; the 98th (`CarDynamicShadowFix`) is read
through a runtime-formatted key (evidence above). All 98 keys are present in both
`game_refs/ZolikaPatch.ini` and the shipped `ZolikaPatch.ini` from
`ZolikaPatch_v7.65.zip` (no extras, no missing).

### 2.2 Hook bodies

* 105 `kAbiBodies` mappings parsed (91 distinct symbols in the map).
* My comparer normalizes operands and compares to the **true** body end (past
  internal `ret`s — the repo verifier stops at the first `RET`/`INT3`, mine does not):
  **no divergence was found in any `@exact` body**, including all 37 with internal
  rets (`spawns_probe` 397 insns, `cutscene_chain_b` 161, `mf_angle_step` 74,
  `collision_deadlock_walk` 73, ...). The matched prefix ends exactly at `ret`+`int3`
  on both sides; both sides' next instruction is the next function.
* 21 pairs diverge; all are bodies the repo itself reports as compiled/INFO
  (`death_music_*`, `game_hash`, `ped_models_in_cutscenes_check`, `improved_shader_*`,
  `vram_fix_shim`, `crash_msg_shim`, `borderless_*`, `speedup_body`, `load_dlcs_dummy`,
  `rgsc_login_removed`, `savegame_fix_*`, `emissive_lerp_shim`, `ooc_wrap`,
  `driveby_fov_shim`, `newfiles_update_a/b` (latter two unresolved in the map — symbol
  name mismatch, not a body defect)). For the compiled ones the divergence is
  documented (guards + compiler codegen).
* One **semantic bug** in a compiled body: `abi_driveby_fov_shim` (F1).
* `hook_bodies::` portable models are never served live (verified: live path passes
  `hook_bodies_win_resolve`; the default resolver reading
  `recovered_hook_targets.inc` is offline-only) — but one entry is misattributed (F5).

### 2.3 Claims re-derived from scratch (§4 of the task) — results

| claim | result |
|---|---|
| OOC format string + "mor" | **confirmed**: `"mor"` @0x1012EB0C, `"me%sy%c%c%p,%d,%c%c%s"` @0x1012EB58, both pushed at 0x1004C37E/0x1004C383 into the wrapper buffer (0x40) — no runtime builder for these |
| OOC payload | **confirmed byte-identical**: 163796 bytes at 0x100B6798, sha256 `6dc0e5fe…` matches the doc, Adler-32 trailer `41e794ab` present |
| HQR constants/pattern | **confirmed**: flags 0x11D @0x10158060; mode 0x10159CE0 read-only (12 refs, all reads inside the body); body 0x1001F030 makes 12 calls through builder slot 0x10159D04 (refs 0x1001F073…0x1001F380); ctor 0x10023CC0 stores 0x1001F390→[0x10192F14] and 0x1001F030→[0x10192F18] via `lea/add` chains; forwarder 0x10023DB0; installer 0x1001F390 writes E9 via 0x1005B5D0 with dest=[0x10192F18] (read @0x10020123) and publishes match+0x13 and match+*(match+0xF)+0x13 @0x10022CA5-B6; pattern row 1 hit @0xC51311 on 1080; reset stub 0x10023D70 zeroes both slots |
| MouseFix/cutscene/popup constants in `.data`/`.rdata` | **confirmed**: 0x10152318=2^-7, 0x10152328=0.025, 0x10152330=0x333334015BC21 hex-float, 0x10152360=π, 0x10152368=2π, 0x101523A0=−1.0, 0x101523B0=−π, 0x101523E0/0x101523F0 masks, 0x10158044=127.5f, 0x10158048=0.1f; 0x101580C0=2.0f, 0x101580CC=0x3FC90FDB, 0x101522F0/0x10152340/0x101523D0 cutscene pool. The `0x3AA63A923A843A4F` note in `rebased_values.cpp` is correctly identified as a file-offset mistake |
| FastLoading site | **confirmed**: handler 0x1001ECA0 → forwarder 0x1001EC90 (`mov eax,[0x1015E840]`) → body 0x1001EC60 (`sub ecx,6` + `call [0x1016088C]`), reset 0x1001EC30 zeroes all three slots (`0x10260C52-0x102412`=0x1015E840, `0x101E7BD1-0x87345`=0x1016088C, `0x101E4D44-0x78954`=0x1016C3F0) |
| CutsceneFixes chains | **chain A LIVE — docs refuted (F21)**: the ctor-called installer 0x1003DA50 publishes [0x10185F88] = first match of `74 24 8B 44 24 2C 83 F8 03` (1 hit @game 0xC9A840 on 1080; fallback `74 0B 83 7C 24 2C 03` via 0x1004401B-0x10044051) through the chain `this+0x5890` (`10041429 add ecx,0x5890; 1004144A mov [edx],eax`); the callback 0x10044160 pokes `EB` at 0xC9A840 via 0x1005AD60 and self-clears through [0x1018C22C] (reset 0x10044130 zeroes 0x10189EE0/0x1018C22C/0x10185F88). The shipped port row (signature + fallback, byte "EB") is correct; §5's dead-chain claim and my own earlier note are wrong. Chain B worker 0x10038B40 (detour body `ret 4` at 0x100388C0 byte-verified); chain B reset 0x100453E0 zeroes [0x10164F04]/[0x1016835C] |
| DrivebyFOV handle | **refuted** (F1) |
| six pusha-forwarders | **two re-derived**: HQR (above) and FastLoading (above); chain A (above). All use this-relative/`lea`+`add/sub` slot addressing; callbacks are one-shot (callback calls its reset stub which zeroes the slots) |
| 11 suppressions + `handler:"0x0"` rows | re-derived: InfiniteCarBottomDistance (`mov byte [esi],0xEB` @0x10022DC4, `mov byte [esi],0x2A` @0x10022DE1), EpisodicVehicleSupport (`push 0xD` fills @0x1001B5E0/F9/FE/717) and SavegameFix (`mov eax,0x90909090; mov word ptr [esi],ax` @0x10045655 — 2 bytes, so the 4-byte row is correctly suppressed and the shipped row is `90 90` @offset 0) all match the curated corrections; the remaining 8 are structurally analogous and were checked for presence/plausibility only (see §4) |
| 44 per-build RVA rows | count confirmed (44: 12 jumps-game, 7 jumps-module, 12 fills, 7+6 slot rows; builds 5/7/8/9). Sampled rows re-derived from handler bytes: `Patch6and7` row `{rva 0x3982, E9, →0x3A09, build 1060}` is exactly the handler's `lea edi,[eax+0x3982]; lea ebx,[eax+0x3A09]; mov byte [edi],0xE9` (@0x10044872-0x100448AE); LoadDLCs' slot constant 0x414560 present @0x100236B1. Full per-row re-derivation of all 44 not completed (§4) |
| obfuscation doc values | **confirmed**: `0xFDD6C5B+0x248735`=0x1001F390 (HQR ctor @0x10023CF5-0x10023D02) and `0x1020B548−0x78634`=0x10192F14 (HQR reset stub @0x10023D71) |

### 2.4 Other coverage passes (delegated, results folded in)

Parallel passes run under this audit (their raw reports live in
`%TEMP%\kilo\audit_indep\sub*/`; every result was spot-checked by the lead auditor):

* **op-row sweep (sub1)**: done — 183/191 confirmed, 8 mismatches (7 suppressed,
  1 inert), only live defect = F19. See F20.
* **effect coverage A (sub4)**: done — all 18 arm handlers FULLY COVERED (per-effect
  tables in `sub4/effects_a.md`); non-blocking observations: StartupTimeFix slot
  initial content nuance, BenchmarkFix slot rows lack a build gate (internal globals
  only), FastLoading slot 0x1015E840 / [0x1016088C] are statically unresolvable in
  the original too.
* **effect coverage B (sub5)**: done — registrar table corrected (chain A this
  0x101887D8, chain B this 0x10160890, CarDynamic this 0x1018C230, missed registrar
  0x1003D6D0 (MouseFix cb 0x10024600 / reset 0x1003D760); 0x1003DA50/0x1001BF30 are
  pattern-scan ctors, 0x10058C30 is `std::vector<u32>::assign`); every registered
  callback's effects COVERED by the port; chain A found **live** (F21).
* **ctor installers (sub6)**: done — 0x1003DA50 = CutsceneFixes chain A installer,
  0x1001BF30 = FastLoading installer, 0x10058C30 = `std::vector<u32>::assign`;
  all 9 game-visible effects covered by rows :139/:150 (see F18); remaining
  findings = F2/F3 (gating) and F21 (docs).
* **RVA rows (sub3)**: done — 43/44 confirmed, 1 partial (F14).

### 2.5 Structural notes (independently derived)

* Live apply path (`runtime.cpp:326-620`): option flags → mapped op capture
  (`kOps`) → `kRvaInstalls` (self-gated by row build) → slot installs →
  sig installs → sig jumps (pattern-gated + feature mask) → sig writes → rva jumps
  (gated by INI + feature mask + `rva_build_allows(handler, rva, build)`) →
  indirect writes → match jumps. `install_recovered_rva_jumps` in
  `hook_install.cpp` is **not** called by the live path (only by tests), so the
  F14 mispairing cannot fire today.
* Build gating: unknown build → the whole apply is skipped (`dllmain.cpp:302-306`);
  the 7 hard gates (`recovered_build_gates.inc`) have bit 0 clear (unknown=0 never
  matches); `rva_build_allows` returns false for un-attributed (handler,rva) pairs;
  the 98-option feature masks are enforced on the sig-jump / rva-jump / indirect /
  match-jump / literal-store paths via `recovered_feature_allows_build`.
* `patch_catalog.cpp` (defaults/coverage) is documentation-only except the mask
  lookup used by `recovered_feature_allows_build` (`patch_catalog.cpp:137`).

---

## 3. Refuted findings (with counter-evidence and impact)

### F1 (high) — DrivebyFOVFix shim uses the *fallback* salt; the fix is inert on all available builds

* **Claim in code** (`hook_bodies_win.cpp:3036`): "dword_101580D4 (the loader's salt
  **0x09399528 on 1.0.8.0**)". `parity_audit.md:35` labels the option "1:1 with
  documented guards".
* **Counter-evidence (original ASI):**
  * Preamble: `push 0; call GetModuleHandleA; mov edx,eax; mov ecx,0x1012b710
    (pattern); call 0x10003830; test eax,eax; jne 0x10052E82` — the probe **hits** on
    1.0.8.0 (pattern `68 ?? ?? ?? ?? 68 A9 54 14 51 E8` has 1 hit at VA `0x0B3878C`),
    so the fallback block (0x10052E20-0x10052E7C, which contains
    `mov dword ptr [0x101580D4], 0x9399528` at 0x10052E70) is **skipped**.
  * On-disk `.data`: `[0x101580D4] = 0x75E005F1` (file off 0x156ED4).
  * The walker (0x10006465) passes that value to the resolver; game resolver
    (published from `08 0B C2 8B F0 E8`, → `0x00626AF0` on 1.0.8.0) is a
    hash-table lookup (count @0x01190FDD4, table @0x01190FDD0).
  * `0x75E005F1` is a **registered native** on 1.0.8.0: `push 0xB5EED0;
    push 0x75E005F1; call 0x625620` @0x0B5F568. Its bytes are present in every
    probe-hit build (1004/1060/1070/1080/12059/EFLC).
  * `0x09399528` occurs **zero times in all 11 available game executables** — it is
    not registered anywhere, so the resolver returns 0.
* **Impact:** the shipped shim (`mov esi,9399528h; mov ecx,9399528h` @
  `hook_bodies_win.cpp:3061-3062`) resolves to 0 on every build; the guard then keeps
  the 45.0f default forever. **DrivebyFOVFix never applies on 1.0.8.0** while the
  original does install and resolve it (on probe-miss builds 1000/1010/1040 the
  original writes 0x09399528 and faults at `[eax+0xE]` when a drive-by occurs —
  the guard there is a genuine, documented improvement).
* **Required fix:** load the runtime's salt global (`zpatch_salt_d4`, which is already
  0x75E005F1 by default and rewritten to 0x09399528 on probe-miss builds exactly like
  the original) instead of the literal, i.e. `mov esi, DWORD PTR [zpatch_salt_d4]`
  (or the two literal moves replaced by one load used for both), and correct the
  comment. Evidence VAs above.

### F2 (medium) — absent INI keys are disabled; the original substitutes the arm default

* **Claim in code** (`config.hpp` comment + `runtime.cpp:380-383`): "the option must
  be enabled in the loaded INI, exactly like the original loader (absent keys are
  not applied at all)".
* **Counter-evidence:** the original's map getter `sub_1005C160` builds the key and
  default strings and calls `sub_1005D630(out, key, default)`; the not-found path at
  0x1005D6BA-0x1005D6C7 (`je 0x1005D75E`) copies the **default** string into the
  output. Every arm then parses that string; most options pass default `"1"`
  (e.g. `push 0x101300F4` immediately before the key push at 0x10053F41
  (`FreezeCarFix`), 0x10054191 (`BuildingDynamicShadows`), and 93 others). So
  deleting a key **enables** the option in the original; our port disables it.
* **Impact:** any trimmed/hand-edited INI diverges. Concrete case verified in bytes:
  with `CarPartsShadowFix` absent, the original reads default "1" and (via the
  dedicated arm at 0x10054658 and the shared path's `a1 = (parts != 1)` logic)
  behaves as `CarPartsShadowFix=1`; our `option_enabled` returns false
  (`config.cpp:102`) and skips the rows.
* **Required fix:** give `ini_option_enabled` the per-option arm default (see F4 for
  the 7 wrong values) and return false only when the parsed value is not 1 (F3).

### F3 (medium) — value semantics: original requires exactly `1`; we accept any non-zero

* **Counter-evidence:** every simple arm ends `cmp esi,1; jne skip; call handler`
  (e.g. 0x10053FAC-0x10053FB4 for FreezeCarFix; 0x10054EAD-0x10054EB7 for
  RemoveRGSCLoginRequirement). Bitmask options set their bit only when the parse
  `== 1` (SkipIntro 0x1005563A-0x10055644). The port's `option_enabled` returns
  `value != "0"` (`config.cpp:102`), so `MouseFix=2` enables the option in our port
  but not in the original.
* **Required fix:** `std::strtol(value) == 1` (with the already special-cased
  internal spellings unchanged).

### F4 (medium) — seven arm defaults in the coverage/catalog do not match the binary

The "observed default" column in `recovered_feature_coverage.inc` and
`patch_catalog.cpp` equals the **shipped INI value** (verified identical to the
v7.65 INI for all 98 keys), but the *arm default* (used when the key is missing)
differs for these keys — all read from the pushes directly before the map get:

| option | arm default (pushed string) | coverage/catalog |
|---|---|---|
| BuildingDynamicShadows | `"1"` @0x10054191 | `"0"` |
| DualVehicleHeadlights | `"1"` @0x10054244 | `"0"` |
| LaunchWithPlayGTAIV | `"0"` @0x10055312 | `"1"` |
| NoLoadingSleep | `"0"` @0x100555AE | `"1"` |
| Patch6and7RemoveModChecks | `"0"` @0x1005535B | `"1"` |
| RemoveRGSCLoginRequirement | `"0"` @0x10054ECB | `"1"` |
| VSyncFix | `"0"` @0x10055280 | `"1"` |

Impact: none while the INI carries the key (it does in both shipped and user INIs);
becomes visible as soon as F2's fix lands or a user drops a key. Fix: correct the
defaults in whichever table becomes the source for the missing-key fallback and note
the INI-value vs arm-default distinction in the docs.

### F5 (low, latent) — `recovered_hook_targets.inc` maps the HQR body 0x1001F030 to BikePhysics

`{0x1001f030u, …bike_physics_loop_body…, "HighFPSBikePhysicsFix"}`
(`recovered_hook_targets.inc:30`). The original installs 0x1001F030 from the **HQR**
installer (ctor 0x10023CC0 → slot [0x10192F18]; installer reads it at 0x10020123 and
writes the E9 via 0x1005B5D0) and its first instructions make the 12 builder calls
with `[0x10158060]` (flags 0x11D). The live path already maps the same VA to
`abi_hqr_body`, so nothing live breaks today, but any consumer of the default
resolver would install an HQR body under the bike option. Fix: remove the entry or
attribute it to `HighQualityReflections`.

### F6 (low/doc) — FreezeCarFix/DebugPrintSpawns classification and gating

`recovered_feature_coverage.inc` says handler `0x0` for both, and `parity_audit.md`
rows 21/26 report no install. Bytes: the FreezeCarFix arm (key @0x1013089C, default
"1") gates a block that reads **DebugPrintSpawns** (default "0") into
`[0x10159C15]` and calls installer `0x10037C70` (0x10005xxx-class hook installer that
scans patterns 0x1012CF10/0x1012CF78, publishes [0x10159CB0] = match+8 and jumps to
0x10037510). Consequences: (a) the option *does* install (through the FreezeCarFix
arm), so "handler 0x0/no install" is misleading; (b) DebugPrintSpawns is only read
when `FreezeCarFix == 1`, while our runtime reads it unconditionally
(`runtime.cpp` loader_arm_flag call) — harmless today because the hook that consumes
the flag is only installed by the FreezeCarFix path, but it is a divergence if the
install gating is ever changed.

### F7 (low/doc) — undocumented loader key `Patch7RemoveModChecks`

Read by the loader at 0x100553A9-0x100553BA (`or ecx,2` bit group; value==1 calls
0x10044710 together with the Patch6and7 read). Absent from
`recovered_feature_coverage.inc`, `patch_catalog.cpp` and both INIs; default "0", so
inert unless a user adds it. The port does not read it. Document the key (or port
it) to keep the "no key the loader reads is missing" invariant.

### F8 (high, blocking) — `zpatch-test` crashes: `zpatch_nms_alloc` truncates the heap pointer in the x64 test build

`build\native-win\zpatch-test.exe` (sha 61372934…) exits `0xC0000005` reproducibly.
Root cause proven live (debugger harness) and by inspection:

* Fault: write AV at `0xB94FC8BC`, `RIP = base+0x135AF`, instruction `mov dword ptr [rbp-4], eax`
  inside the x64 build of `zpatch_nms_alloc` (RVA 0x13550; source
  `src/hook_bodies_win.cpp:5495-5512`, store at :5504, `memset` at :5507).
* `zpatch_nms_alloc` truncates the 64-bit `HeapAlloc` pointer to `uint32_t` (:5502),
  does 32-bit alignment arithmetic, then uses the truncated value as an address (:5504,
  :5506-5507). In the x64 test process the heap lives above 4 GiB, so `aligned-4`
  (`0xB94FC8BC` in the captured run) is unmapped.
* Registers reproduce the math exactly: raw `0xB94FC850`, unit 0x40, aligned
  `0xB94FC8C0`, store `[aligned-4]`.
* Caller: `tests/test_main.cpp:2932` → `zpatch_nms_alloc(0x20,0x40,0)`.
* My earlier "DIAG printing" attribution was wrong: the DIAG loop completes; the dying
  process's unflushed stdout buffer merely made it look like the crash was in the DIAG
  sequence. Sub-auditor 2 refuted this live (a second run with a debugger and a
  post-mortem memory read of the stdout buffer).
* Impact: the 32-bit ASI is unaffected (32-bit pointers); the native test gate fails on
  x64. Fix: use `uintptr_t` for all pointer arithmetic and truncate only for the
  registry key (`g_nms_allocations.insert(static_cast<std::uint32_t>(aligned))`),
  mirroring the original's 32-bit-keyed FNV registry.

### F10 (info) — registration (ctor) inventory: complete and consistent

The C++ ctor table `[0x100633BC,0x10063408)` runs 18 ctors; ten registrar/installer
functions are called once each with a distinct `this` (`mov ecx,<imm>` immediately
before the `call`, re-read for every ctor). Registrars store a callback and a reset
stub through the obfuscated `lea/add` chains (all values below re-computed from the
byte constants; the slot arithmetic is `this + K` with K taken from the chains):

| ctor | registrar | this | stores (slot = value) | reset zeroes |
|---|---|---|---|---|
| 0x10001020 | 0x1001BF30 | 0x10168360 | (large installer; see F18/sub6) | — |
| 0x10001043 | 0x1001EBA0 | 0x1015D640 | 0x1015E840 = 0x1001EC60 (FastLoading body); 0x1016088C = 0x1001EC30 | 0x1015E840, 0x1016088C, 0x1016C3F0 |
| 0x10001053 | 0x10022E00 | 0x1018C230 | 0x1018E574 = 0x10010B60 (shared car-shadow installer); 0x1018E578 = 0x100068A0; 0x1018E57C = 0x100068D0; … | 0x1018E574, 0x1018E57C, 0x1018E578, 0x10190ABC, 0x10159D24 |
| 0x10001063 | 0x10023CC0 | 0x10190AC0 | 0x10192F14 = 0x1001F390 (HQR installer); 0x10192F18 = 0x1001F030 (HQR body); 0x10196370 = 0x10023D70 | 0x10192F14, 0x10192F18, 0x10196370 |
| 0x10001143 | 0x1003D6D0 | 0x10196378 | 0x1019E7B8 = 0x10024600; 0x101A0804 = 0x1003D760 | (see F18/sub6) |
| 0x10001150 | 0x1003DA50 | 0x101806F8 | (large installer; see F18/sub6) | — |
| 0x10001183 | 0x100440A0 | 0x101887D8 | 0x10189EE0 = 0x10044160 (chain A callback); 0x1018C22C = 0x10044130 | 0x10189EE0, 0x1018C22C, 0x10185F88 |
| 0x10001193 | 0x10045350 | 0x10160890 | 0x10164F04 = 0x10038B40 (CutsceneFixes chain B worker); 0x1016835C = 0x100453E0 | 0x10164F04, 0x1016835C |
| 0x100011b3 | 0x10050790 | 0x101A0820 | 0x101A4E98 = 0x10046CC0 (garage callback); reset ~0x1005081C | 0x101A4E98, 0x101A8300 |
| 0x100011c3 | 0x10050950 | 0x1016E448 | 0x1017076C = 0x1004C3F0 (OutOfCommission deploy body); 0x101806F0 = 0x100509E0 | 0x1017076C, 0x101806F0 |

Registrar 0x10058C30 is a helper (`std::vector<u32>::assign`, called internally and
from the ctor region), not an option registrar. The ctor-called pattern installers
0x1003DA50 / 0x1001BF30 publish matches into slots through stack-round-tripped chains
(0x1003DA50 → [0x10185F88], the live chain-A site — see F21; 0x1001BF30 →
[0x1016C3F0]). The one-shot semantics were re-checked: the HQR installer calls
`call [0x10196370]` (its own reset) at 0x10021775, and the chain-A callback ends by
`call [0x1018C22C]` (its reset), so both self-clear. This inventory independently
confirms the "six pusha-forwarder options" family: HQR, CutsceneFixes A+B,
FastLoading, OOC, garage, CarDynamicShadow.

### F11 (info) — every port-referenced original VA is a plausible function start

All 152 VAs referenced by the port as hook targets / ABI bodies / curated handlers /
curated dests were checked against the original image: 151 are preceded by `CC`/`ret`
padding, and the one flagged (0x10051890) directly follows a `ret 4` (its "previous
byte" is the tail of that ret) — so all 152 are true function starts. The only
mapping defect found is the *name* attribution F5.

### F12 (medium) — the original's FusionFix compatibility gate is not applied

* Original: the loader arms for **SkipIntro**, **SkipMenu** and **BorderlessWindowed**
  call `sub_10003E20` before installing and skip the option when it returns 1
  (0x1005563F, 0x100556D1, 0x10055836; `call 0x10003e20; test al,al; jne/mov
  [ebp-0xd1],0`). `sub_10003E20` = `GetModuleHandleW(L"GTAIV.EFLC.FusionFix")`
  (UTF-16 string at 0x10128B88, GetModuleHandleW IAT 0x10063074) then checks
  `[module+0x20D3]==0xC7C47589 && [module+0x20D7]==0x00002446`; returns 1 only when
  FusionFix is loaded with a *non-matching* build, 0 when absent or matching.
* Port: `compatibility_module_lacks_signature` implements exactly this check
  (`game.cpp:148-163`) and is unit-tested (`test_main.cpp:355-369`) but **no
  production code calls it** (verified by exhaustive grep: only definition,
  declaration and tests). So the three options install even when the original
  would skip them (FusionFix loaded, mismatched build).
* Impact: with a FusionFix build whose signature check fails, we apply
  SkipIntro/SkipMenu/BorderlessWindowed where the original refuses — potential
  double-hooking of the intro/menu path.
* Fix: in the runtime's option gate (or before the three options' installs), call
  the existing helper on the loaded module image when `GetModuleHandleW(L"GTAIV.EFLC.FusionFix")`
  is non-null, and skip when it lacks the signature.

### F13 (medium) — NewFilesCompatibility's GTAC/JacksIVFixes gate is not applied

* Original handler 0x10051920 (called from the NewFilesCompatibility arm at
  0x10055AAB): first `GetModuleHandleA("GTAC.dll")` (string 0x10129018) and
  `GetModuleHandleA("JacksIVFixes.asi")` (string 0x1012F718); if either is present it
  jumps to the function's exit (0x10051A75) **before any patch**: no E9 at
  match+0x1C, no resolver publish [0x10159C08], nothing.
* Port: the rows for NewFilesCompatibility (patch_ops 111-113, sig_write, op
  correction row, slot publish "dword_10159C08") are gated only by
  `ini_option_enabled` + the 7 hard build gates; grep shows no GTAC/JacksIVFixes
  gate anywhere in `src/` for this option (GTAC is only checked for the DFA hooks
  and inside some bodies).
* Impact: with JacksIVFixes.asi (or GTAC.dll) loaded, we install where the original
  skips — the exact conflict the guard exists to prevent.
* Fix: add the two module checks to NewFilesCompatibility's enable gate (or an
  install-time check), matching the original's early-out.

### F14 (low, latent) — `recovered_rva_jumps.inc` LoadDLCs rows are mis-paired

* The handler's real writes (re-derived): E8 at 0x4137CD/0x41356D/0x48F558 →
  0x10023480; E9 at 0x4144D4/0x414274/0x4903D4 → 0x10023490
  (0x1002366B/0x10023874/0x10023A81 write 0xE8; 0x100236BA/0x100238C3/0x10023AD0
  write 0xE9; the dest constants follow each write: 0x10023480 at 0x10023692/
  0x1002389B/0x10023AA8, 0x10023490 at 0x100236E9/0x100238F2/0x10023AFF).
* The table ships `{0x414274→0x10023480}` and `{0x4144D4→0x10023480}` (wrong dest)
  next to the correct `→0x10023490` rows, and **omits** the three →0x10023480
  sites entirely.
* Net runtime effect today: none. The live loop
  (`runtime.cpp:523-553`) gates each row with `rva_build_allows(handler, rva, build)`
  (0x414274→build 8, 0x4144D4→build 7) and the wrong-dest row is overwritten by the
  correct row at the same site later in file order; the real →0x10023480 writes are
  delivered by `kRvaInstalls` (kind 1 / opcode E8, self-gated by build). It is still
  a data error that should be fixed (wrong rows removed, missing rows added).
  Also reported by sub-auditor 3: `kRvaBuildGates` has three duplicated entries and
  one missing entry (rva 4276910/build 8), with no effect because `kRvaInstalls`
  self-gates by its own build field and the duplicate gates are idempotent.

### F15 (low) — CLOSED 2026-10-14: MP model-name lookup

* Original `sub_10003F00` (FreezeCarFix/DebugPrintSpawns, GTAC-gated) linearly
  scans the 38455-entry name table at 0x100DE770 in original order, hashes each
  name, and returns the first match or 0x10128D20 (`**UNKNOWN**`).
* `zpatch_bpm_model_name` and the DebugPrintSpawns helper now call
  `model_name_from_hash`, which scans the extracted catalog with the recovered
  `game_hash` function and has the same first-match and `**UNKNOWN**` outcomes.
* `extract_model_catalog.py --check` verifies all 38455 entries against the
  original ASI SHA-256; `test_main.cpp` checks the first/last entries, known
  hashes and unknown sentinel. The earlier unconditional-UNKNOWN finding is
  superseded.

### F21 (high, doc refutation) — CutsceneFixes chain A is **live**; the "nothing publishes 0x10185F88" conclusion is false

* Docs claim (parity_and_improvements §5 + my own earlier note): chain A's site slot
  0x10185F88 "is never published (the handler's pusha-forwarder was vestigial)" →
  dead code.
* Counter-evidence (byte-level, independently re-read by the lead auditor):
  * The ctor-called installer `0x1003DA50` (ctor 0x10001150, this=0x101806F8) builds
    the pattern text `74 24 8B 44 24 2C 83 F8 03` (char-by-char byte stores, verified)
    and scans it via `0x1005AFA0` (call at 0x100413F4); on ≥1 hit it stores the first
    match through a stack-round-tripped chain:
    `1004140F mov edx,4; 10041414 imul eax,edx,0; 10041417 add eax,[ebp-0xF08] (this);
     10041429 add ecx,0x5890 (this+0x5890 = 0x10185F88); 10041448 mov eax,[eax];
     1004144A mov [edx],eax` — **store [0x10185F88] = first match**.
  * Phase-2 fallback (0x1004401B-0x10044051) stores the fallback pattern's match with
    the same arithmetic when phase 1 finds nothing.
  * The pattern has exactly 1 hit on 1.0.8.0 at game VA **0xC9A840** (fallback text
    `74 0B 83 7C 24 2C 03` has 0 hits), so at load `[0x10185F88] = 0xC9A840`.
  * The chain-A callback 0x10044160 then does exactly what the docs said it would:
    `cmp [0x10185F88],0; je; mov dl,0xEB; mov ecx,[0x10185F88]; call 0x1005AD60`
    (VirtualProtect + byte write) and finally `call [0x1018C22C]` (its reset stub
    0x10044130, which zeroes 0x10189EE0/0x1018C22C/0x10185F88).
* Impact: chain A is a working one-shot hook on 1.0.8.0 — the game byte at 0xC9A840
  (`74`) is turned into `EB` when CutsceneFixes (default 1) runs. The shipped port row
  `{"CutsceneFixes", "74 24 8B 44 24 2C 83 F8 03", 0, "EB", fallback "74 0B 83 7C 24 2C 03"}`
  already implements this correctly — **the port is right and the documentation is
  wrong**. My own earlier report note ("only writer is the reset stub") was also
  wrong: the store is chain-computed and invisible to immediate scans/IDA xrefs
  (IDA lists only the callback's two reads).
* Required action: correct `parity_and_improvements.md` §5 and any status derived
  from it; no code change.

### F18 (info) — the two ctor-called pattern installers identified; effects covered

* **0x1003DA50** = the **CutsceneFixes chain A installer**: builds
  `74 24 8B 44 24 2C 83 F8 03` (fallback `74 0B 83 7C 24 2C 03`), scans the main
  module at load and publishes the first match into [0x10185F88]
  (0xC9A840 on 1.0.8.0; F21). Covered by `recovered_op_corrections.inc:150`
  (signature + fallback, byte `EB`).
* **0x1001BF30** = the **FastLoading installer**: builds `83 C7 0A 83 C4 04 81`,
  1 hit at 0xCB1D28 on 1.0.8.0, publishes it into [0x1016C3F0]; the arm's body then
  writes `00` at 0xCB1D28−6 = **0xCB1D22** (the `push 0xA` immediate). Covered by
  `recovered_op_corrections.inc:139` (pattern `6A 0A E8 …`, 1 hit at 0xCB1D21,
  write at +1 = 0xCB1D22 — verified by the lead auditor; note sub-auditor 4's
  "0x8B1D22" was the file offset, not the VA).
* **0x10058C30** = MSVC `std::vector<unsigned int*>::assign` helper (ctor fills
  0x101A8418 with 16 head pointers; also used by the FNV rehash) — no game effect.
* Gating note: both installers' option arms use the value-or-default-1 + `==1`
  semantics already reported as F2/F3 (byte evidence 0x1005C160/0x1005BFF0 default
  "1", `cmp esi,1` at 0x100550A7 / 0x10053DFE).

### F19 (high) — CLOSED 2026-10-15: HighFPSSpeedupFix writes nine distinct sites

* Original handler 0x10023B30 (HighFPSSpeedupFix's handler, called from the arm at
  0x10054084 when the option is 1): scans `0F 2F C1 76 03 0F 28 C8 F3 0F 10 15`
  (0x1012BEA8, 1 hit on 1.0.8.0) into `ebx`, then writes, in straight-line code
  (no build branch):
  `90 90` at **ebx−7** (0x10023B91/B9C), `90 90` at **ebx+3** (0x10023B6F/B7A),
  `EB` at **ebx+0xE0** (0x10023BB3/BBF), `90 90` at **ebx+0xE7** (0x10023BD3/BE1),
  **ebx+0x16F** (0x10023BF8/C06), **ebx+0x179** (0x10023C1B/C2B), **ebx+0x25F**
  (0x10023C42/C50), **ebx+0x266** (0x10023C67/C75), **ebx+0x2FC** (0x10023C8C/C9A).
* `recovered_patch_ops.inc:62-70` now contains `−7`, `+3`, `+0xE0 (EB)`, `+0xE7`,
  `+0x16F`, `+0x179`, `+0x25F`, `+0x266`, and `+0x2FC` as distinct writes.
* `extract_patch_ops.py --check`, the final install diagnostics and the 9-build
  parity gates verify all nine offsets. The earlier duplicate-row finding is
  superseded; the generated table no longer collapses the NOP sites.

### F20 (info) — op-row sweep result (sub-auditor 1, spot-checked)

191 rows checked (137 signature rows + 54 module-RVA rows across 60 options):
**183 confirmed**, 8 mismatches of which 7 are already suppressed by
`recovered_op_corrections.inc` (ImprovedShaderStreaming ×3, InfiniteCarBottomDistance,
NewMemorySystem, SMPA60Fix) or inert (SkipMenu's lone-E8 row is skipped by the
`runtime.cpp:418-428` guard; the curated +2 sig-install performs the real write).
The 6 RemoveUselessChecks 38-byte fill rows are partial by construction (SSE stores;
superseded by the `kRvaInstalls` fills). F19 was the only live defect in this
snapshot and is now closed as documented above.
Signature scan on 1.0.8.0: 94 of 111 unique signatures have exactly 1 hit, 16 have 0
(build-specific branches, correctly no-op), 1 has 2 (MiscFixes; the original's
find-first semantics match).

### F17 (low, latent) — the moving-parts installer's first-scan gate is not modelled

* Original 0x10005210 (called by every enabled MovingSteeringWheelSupport /
  MovingPlaneParts / ShakingEngineBlock arm at 0x10054A81/0x10054AEF/0x10054B5D):
  first scans `A1 ? ? ? ? 8B 0D …` (0x101295C8); if that pattern is **absent** it
  jumps straight to `call 0x10004110`, skipping the E9 installs of the second/third
  patterns; if present it installs whichever of `32 DB 8B 16 …` (post-1060) or
  `7C E0 8B 06 …` (pre-1060) exists and unconditionally calls 0x10004110.
* Port: the three options' E9 rows scan the second/third patterns directly with no
  reference to the first-pattern gate. Measured pattern availability: p1 is present
  on every build that has p2 or p3 (absent only on 12059, where p2/p3 are also
  absent), so there is **no observable divergence on any available build** — but a
  build with p2/p3 and without p1 would get installs the original skips.
* Fix (hardening): make the 0x101295C8 presence check a precondition for the
  moving-parts E9 rows.

### F16 (info) — preamble / ctor / module-check coverage independently confirmed

* Loader preamble (0x100523C0-0x10053400): exactly 8 pattern scans
  (`68 ? ? ? ? 68 A9 54 14 51 E8` salt probe; `08 0B C2 8B F0 E8` → [0x1015BDE0];
  `85 F6 74 30 …` → [0x10159C74]=match-42; `8B 44 24 04 …` → [0x10159C0C];
  `85 C0 C6 44 24 0C 00 …` → [0x1015AD78]=*(match-4); `56 8B F1 …` → [0x10159B88];
  `0F 10 5E 14 …` → [0x1015D63C]=match-48; `0F 10 16 …` → [0x10159CDC]=match-48).
  The port's `apply_loader_preamble` performs all eight with identical patterns,
  slot math and miss-behaviour (keeping the previous value where the original uses
  `cmovne`), and the salt fallback list matches the original's ten dwords including
  the ordering. The 18 `GetModuleHandleW(L"GTAC.dll")` sites in the spawns-probe
  region (0x100375CC-0x10037AFC) are inside that body's branches and the port
  documents/branches on the same condition.
* DllMain (0x10055C70): dsound size warning (0x16600), version nag range
  1201..12999 + `start https://…downgrading` + `TerminateProcess(0x8000FFFF)`,
  build-table 1000/1004/1040/1060/1061/1070/1080/1100/1110/1120/1130/12043,
  CitizenIV detection — all present in `dllmain.cpp:378-417` with matching values.
* Section parsing (`[PopupLightAngles]`/`[HeadlightConeOffsets]`) is modelled
  (`vehicle_tables.cpp`, `runtime.cpp:127`) — the loader's first arms read
  "Name"/"Angle"/"Offset" keys of those sections (0x1012FF10/0x1012FEB8).
* Menu integration (`GetModuleHandleA("IVMenuAPI.asi")`/`"ZMenuIV.asi"` +
  `GetProcAddress` at 0x100522BE/0x10052315/0x1005238E): modelled in
  `menu_integration.hpp` + `dllmain.cpp:199-232`.
* Exports: original has none; the port's ASI has an empty export directory
  (0 named/ordinal functions) — cosmetic linker difference only.



### F9 (low) — stale/contradictory parity_audit rows

* `parity_audit.md:35` DrivebyFOVFix "1:1", `parity_audit.md:928` "not installed on
  1.0.8.0"; both wrong (F1: it installs and works in the original; our shim is the
  inert one).
* HQR row 40 "withheld (blocker…)" contradicts §5 and the built ASI (abi_hqr_body
  registered and independently verified byte-exact).
* Row 14+ CarDynamicShadowFix: rows exist and the key is real (runtime-built, §2.1);
  the doc's "handler 0x0" is fine but the arm is the `sub_10022F40` call inside that
  arm — worth spelling out since the alias key now must not be dropped.

---

## 4. Unverifiable items (named blockers)

1. **EFLC 1.1.0.0** — no usable exe (SecuROM `matrosch`). Build-ordinal 10 rows and
   `build_allows_option` for that build could not be checked offline.
2. **1.0.6.1 (1061)** — not present; AGENTS.md says it shares 1070 code. Not
   independently verified; build-ordinal 15 rows unverified.
3. **1.2.0.43 (12043)** — only `GTAIV_12059.exe` exists; the 44-row table has no
   build-18 rows to check, but the 12059 exe was used as a stand-in in my scans. Any
   row that *should* be build-18 would be silently absent; cannot confirm from here.
4. **Live behaviour** — no game launch performed (per task constraints); the
   `CrashProbe` recovery paths and the driveby walker's runtime resolution were
   derived statically only. A live watchpoint on `[0x101580D4]` with the original
   would be the strongest tie-breaker for F1 (prediction: 0x75E005F1 on 1.0.8.0).
5. **Per-row re-derivation of all 44 RVA rows** — sampled 4 (2 fully re-derived from
   handler bytes, 2 located as handler constants). The remaining 40 were checked only
   for build/format consistency.
6. **`recovered_ooc_pak.inc` decode** — payload bytes verified identical to the ASI,
   but the decompression result (finale2.sco semantics) was not reproduced.
7. **INFO (compiled) bodies** — verified as *documented* compiled bodies with
   semantics read instruction-by-instruction; only the driveby one had a provable
   contract bug. The others depend on compiler output that `verify_hook_bodies.py`
   also reports as INFO; no live execution checked them.
8. **`RestoreDeathMusic`/menu-integration special case**, **collector tables** and
   `PopupHeadlightsFlag`'s `&dword_101580CC` address-store were not re-derived to the
   last byte (time); they are marked C in §2.1 only where the arm/keys were directly
   read.
9. **ZMenuIV.asi** (the co-built menu plugin) was not audited in this pass — the task
   scope is the ZolikaPatch option set. Its build succeeds under `msvc-asi`.
10. **The two ctor-called installers** 0x1003DA50 / 0x1001BF30 and the helper
    0x10058C30 — resolved (F18): chain A installer, FastLoading installer,
    `vector::assign`; all game-visible effects covered.

---

## 5. Required fixes (do not apply — user decides)

1. `src/hook_bodies_win.cpp:3061-3062` (`abi_driveby_fov_shim`): replace
   `mov esi, 9399528h` / `mov ecx, 9399528h` with a load of `zpatch_salt_d4` (both
   lanes) so the probe-hit builds use 0x75E005F1 and probe-miss builds keep the
   0x09399528 fallback; update the comment at :3036. Evidence: F1 VAs.
2. `src/config.cpp:100-103` + `src/runtime.cpp:72-102`: implement the original's
   missing-key and value semantics — absent key → the arm default (F4 table; keep
   `"0"` only where the arm pushes `0x1012B264`), enabled iff parsed value == 1
   (F2/F3). Add a regression test that deletes a key and one that sets `=2`.
3. `src/recovered_feature_coverage.inc` / `patch_catalog.cpp`: correct the 7 arm
   defaults (F4 table), or mark the column "shipped-INI value" and add a separate
   `arm_default` field.
4. `src/recovered_hook_targets.inc:30`: remove or re-attribute the
   `{0x1001f030u … bike_physics_loop_body}` entry (F5).
5. `docs/parity_audit.md`: fix rows 23 (DrivebyFOVFix — inert, not 1:1), 40 (HQR —
   shipped, verified), 21/26 (FreezeCarFix/DebugPrintSpawns install path), and drop
   the line-928 claim (F9).
6. `docs/parity_and_improvements.md` §5: **the dead-chain-A conclusion is wrong** —
   [0x10185F88] is published non-zero at load by ctor 0x1003DA50 (F21); the sentence
   "which nothing publishes" must be replaced with the live description (chain A
   pokes `EB` at game 0xC9A840 on 1.0.8.0). The shipped port row is already correct.
7. Document `Patch7RemoveModChecks` (loader-read, default "0", handler 0x10044710)
   in the coverage table/INI docs, or explicitly state it is intentionally unported
   (F7).
8. `src/hook_bodies_win.cpp:5495-5512` (`zpatch_nms_alloc`): replace the `std::uint32_t`
   pointer arithmetic with `std::uintptr_t` (the 64-bit `HeapAlloc` pointer at :5502 is
   truncated and then dereferenced at :5504/:5506-5507, which AVs in the x64 test
   build; the 32-bit ASI is unaffected). Keep the registry key 32-bit to mirror the
   original's FNV-over-four-bytes registry (debugger evidence: fault `RIP=base+0x135AF`,
   store `[rbp-4]`, raw=0xB94FC850, aligned=0xB94FC8C0; full chain in
   `sub2/crash_rootcause.md`).
9. Wire the FusionFix gate (F12): skip SkipIntro/SkipMenu/BorderlessWindowed when
   `GetModuleHandleW(L"GTAIV.EFLC.FusionFix")` is loaded and
   `compatibility_module_lacks_signature` returns true (the helper already exists and
   is tested; call it with the module's mapped image).
10. Wire the GTAC/JacksIVFixes gate (F13): skip NewFilesCompatibility when
    `GetModuleHandleA("GTAC.dll")` or `GetModuleHandleA("JacksIVFixes.asi")` is
    loaded (original 0x10051920 early-out).
11. Fix the `recovered_rva_jumps.inc` LoadDLCs rows (F14): remove the two
    `→0x10023480` rows at 0x414274/0x4144D4, add the real E8-destination rows
    (0x4137CD/0x41356D/0x48F558 → 0x10023480), and de-duplicate/mend
    `kRvaBuildGates` (missing 4276910/build 8; duplicated 4276852/4277460/4785108).
12. **F15 completed (2026-10-14):** `zpatch_bpm_model_name` hashes and searches
    the extracted 38455-entry `model_catalog` in original order; it returns the
    matching first entry or the original `**UNKNOWN**` miss sentinel. The
    catalog extractor is pinned to the original ASI and verifies all entries.
13. `src/recovered_patch_ops.inc:63-70` (F19): replace the eight duplicate
    `-7/90 90` rows for HighFPSSpeedupFix with the seven distinct offsets
    (`-7, +3, +0xE7, +0x16F, +0x179, +0x25F, +0x266, +0x2FC`; keep one `-7` row)
    so all eight NOP sites the handler writes are patched.

---

## 6. Doc corrections (quote-level)

* `parity_audit.md:35` — replace "1:1 with documented guards (registered 2026-10-13)"
  with a note that the shim is currently inert on probe-hit builds (wrong salt) and
  that this is under fix; `parity_audit.md:928`'s "the original does not install it on
  1080" is false (pattern hit @0xB3878C-era layout; arm scans and installs).
* `hook_bodies_win.cpp:3036` — "(the loader's salt 0x09399528 on 1.0.8.0)" → the
  on-disk/probe-hit value is 0x75E005F1; 0x09399528 is the miss-fallback.
* `config.hpp` ("absent keys are not applied at all") — false; the loader's
  `sub_1005C160`/`sub_1005D630` substitutes the per-key default (usually "1").
* `parity_and_improvements.md` §5 chain-A sentence "which nothing publishes" →
  **wrong**; replace with: "the site slot is published at load by ctor 0x1003DA50
  (first match of `74 24 8B 44 24 2C 83 F8 03`, 0xC9A840 on 1.0.8.0); the callback
  pokes `EB` there and self-clears" (F21).
* `parity_audit.md` row 40 HQR "withheld" → shipped and verified (see §2.3).

---

## Appendix — independent arm table (site, key, default, handler)

Raw extraction (99 sites, 86 keys; `??` = key built at runtime, all resolved in
§2.1) is at `%TEMP%\kilo\audit_indep\arms3.txt` in this session's scratch. The
per-option evidence VAs in §2.1 are sufficient to re-run any single check with
`python -c "from zp import asi, fmt_insns; print(fmt_insns(asi().disasm(<VA>-24, 14)))"`.


##### 2026-10-14 update (same session, post-report findings)

Two more checks completed after the first write-up; both **confirmed**, no fixes needed:

* **Probe-hit build matrix** (independent scan, all 11 exes): the salt-probe pattern
  hits on 1004/1060/1070/1080/12059/EFLC-1110/1120/1130 and misses on 1000/1010/1040;
  0x75E005F1 is embedded only in the hit builds; 0x09399528 in none — so the driveby
  shim (F1) is wrong on **every** hit build, not just 1.0.8.0.
* **EpisodicVehicleSupport 13-byte fill**: handler 0x1001B580 contains `push 0xD`
  at 0x1001B5E0/0x1001B5F9/0x1001B6FE/0x1001B717 and the shipped 13×`90` fill row is
  present; the suppressed 8-byte row is indeed a mis-split. Consistent.

---

## Phase 2 (top-down game-side inventory + remaining gaps) — 2026-10-14

A second independent audit re-derived the original ASI **top-down**: all 225 scan-primitive call
sites + the write helpers decoded to (pattern, effect, per-build VA), applied symbolically to all
9 available exes, and diffed byte-for-byte against the port's complete write log (scratch all-on
build, 1805 writes). Result: 983 byte-effects match; the remaining deltas are triaged to new
findings **F22-F37** (spawns who-salt hardcode; EpisodicVehicleSupport gate 0x300 missing 108
writes on 1060/EFLC + 4 extra episodic defects; RemoveRGSC gate 0x200 blocks the primary install
on 7 builds; EpisodicWeapon over-apply on 1000; shared mod-check-bypass gating; missing ZMenuIV
version guard; MiscFixes M5 companion word write; TeleportProtection writes C2 only (original
C2 08 00 = `ret 8`, crash-class); NoLiveryLimit P3 slots on 1000/1040; Patch6and7 five byte
writes; DFA installer under/over-application; GFWL version constant mismatch) plus the phase-1
leftovers re-confirmed open (F19 reproduced live, F8/F1/F12/F13 unchanged). Positive claims
re-verified independently: all 76 @exact hook bodies (fresh msvc-asi + verify-bodies PASS), the
OOC payload hash, all 98 loader-read keys (incl. the sprintf-composed CarDynamicShadowFix), the
preamble salt block/fallback build set, the vehicle-light parsing, IVMenuAPI wiring, the
garage/OOC/car-shadow registrars, and the 44 RVA data rows. Full report with the per-site
inventory table: `docs/independent_audit_phase2.md`.
