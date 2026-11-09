> **Superseded by `PARITY.md` (2026-10-20).** This generated table is kept for its
> per-option provenance notes, but its status column is stale: `DebugPrintSpawns`,
> `HighQualityReflections`, `DualVehicleHeadlights`, `NewMemorySystem`,
> `DrivebyFOVFix` and `PopupHeadlightsFlag` are shipped, not withheld/off (their
> `@exact` bodies and state cells pass). `status:1:1` here means
> "generator-verified against the handler + bodies verified or absent", not an
> independent re-decompilation. Use `docs/parity/ledger.json` for verdicts.

# ZolikaPatch 1:1 parity audit (Mission B)

Generated 2026-10-13 (session 9) by `tools/`-adjacent script `s9_audit.py` (scratch) from the shipped evidence tables; per-option notes are hand-written. Scope: every option in `src/recovered_feature_coverage.inc` (98 rows).

Evidence sources: `evidence/op_corrections.json` (curated rows; each row's `why` names the handler/disassembly site and `tools/apply_op_corrections.py` re-verifies every row textually against the saved handler decompile), `src/recovered_*.inc` (generated from the handlers), `evidence/handler_table.json`, `evidence/option_build_gates.json`, `src/hook_bodies_win.cpp` (kAbiBodies + `@exact` markers; `tools/verify_hook_bodies.py` byte-compares every exact body), `game_refs/ZolikaPatch.ini`.

Legend: **op** = generated literal op rows; **cur** = curated rows; **jmp** = sig/match/rva jump rows; **lit** = literal stores; **ind** = indirect writes; **gate** = build mask from `recovered_build_gates.inc`; bodies list `VA (symbol)` with EXACT (byte-verified), INFO (compiled, semantics checked), or UNRESOLVED (clean no-op). Statuses: 1:1, no-op, excluded, withheld, divergence (see §4 of parity_and_improvements.md).

## Summary

| # | option | class | handler | gate | INI | rows (op/cur/jmp) | bodies | status |
|---|--------|-------|---------|------|-----|-------------------|--------|--------|
| 1 | AddOptionsInMenu | flag-only | 0x0 | - | 0 | 0/0/0 | - | 1:1 |
| 2 | AimProjectileCrashFix | signature-patch | 0x100055c0 | - | 1 | 1/0/1 | abi_aim_projectile_crash_fix* | 1:1 |
| 3 | AllowHashesForIDE | signature-patch | 0x10022cf0 | - | 1 | 1/0/1 | abi_game_hash | 1:1 |
| 4 | BadPlayerModelCrashFix | signature-patch | 0x10037be0 | - | 1 | 1/3/1 | abi_bad_player_model | 1:1 with documented divergence |
| 5 | BenchmarkFix | function-call-only | 0x10005690 | - | 1 | 0/2/2 | abi_benchmark_a, abi_benchmark_b | 1:1 |
| 6 | BetterMPSync | signature-patch | 0x10005800 | - | 1 | 1/1/0 | abi_better_mp_sync* | 1:1 (re-added 2026-10-13) |
| 7 | BikeFeetFix | signature-patch | 0x1001bdd0 | - | 1 | 1/0/0 | - | 1:1 |
| 8 | BikePhoneAnimsFix | signature-patch | 0x1001bd60 | - | 1 | 1/0/0 | - | 1:1 |
| 9 | BlackscreenFix | signature-patch | 0x10036fd0 | - | 1 | 1/2/1 | abi_blackscreen_shim | 1:1 (exact shim) |
| 10 | BorderlessWindowed | function-call-only | 0x10005970 | - | 1 | 0/2/2 | abi_borderless_w, abi_borderless_a | 1:1 |
| 11 | BuildingAlphaFix | signature-patch | 0x10005ca0 | - | 1 | 2/1/0 | - | 1:1 except pattern 2 (withheld) |
| 12 | BuildingDynamicShadows | signature-patch | 0x10005b20 | - | 1 | 1/5/1 | abi_building_dynamic_shadows | 1:1 |
| 13 | CarDecalsFix | function-call-only | 0x0 | - | 1 | 0/1/0 | abi_car_decals_fix | 1:1 |
| 14 | CarDynamicShadowFix | function-call-only | 0x0 | - | 1 | 0/13/0 | abi_carparts_shadow_2, abi_carparts_shadow_2b* | 1:1 (build variants shipped 2026-10-13) |
| 15 | CarPartsShadowFix | function-call-only | 0x10006910 | - | 0 | 0/3/1 | abi_carparts_shadow | 1:1 (gating + old-build variant shipped 2026-10-13) |
| 16 | CollisionDeadlockFix | signature-patch | 0x10005e30 | - | 1 | 1/1/1 | abi_collision_deadlock_walk | 1:1 (exact shim) |
| 17 | CombinedRadioFreezeFix | signature-patch | 0x10005ee0 | - | 1 | 1/1/0 | abi_combined_radio_freeze_fix* | 1:1 (re-added 2026-10-13) |
| 18 | CopSirensFlag | signature-patch | 0x10006350 | - | 1 | 2/6/2 | abi_copsirens_ide, abi_copsirens_a, abi_copsirens_b | 1:1 |
| 19 | CrashMsgFix | signature-patch | 0x100371f0 | - | 1 | 0/2/0 | abi_crash_msg_shim | 1:1 |
| 20 | CutsceneFixes | function-call-only | 0x100441a0 | - | 0 | 0/4/0 | abi_cutscene_chain_b* | 1:1 (shipped 2026-10-13) |
| 21 | DebugPrintSpawns | function-call-only | 0x0 | - | 1 | 0/0/0 | - | 1:1 with documented divergence |
| 22 | DoNotPauseOnMinimize | function-call-only | 0x0 | - | 1 | 0/2/0 | - | 1:1 |
| 23 | DrivebyFOVFix | function-call-only | 0x0 | - | 1 | 0/2/1 | abi_driveby_fov_shim | 1:1 with documented guards (registered 2026-10-13) |
| 24 | DualVehicleHeadlights | signature-patch | 0x10006770 | 0x7e | 1 | 1/0/0 | - | build-gated off 1.0.8.0 |
| 25 | EmissiveLerpFix | signature-patch | 0x1001afa0 | - | 1 | 1/3/0 | abi_emissive_lerp_shim | 1:1 |
| 26 | EpisodeSwitchFreezeFix | signature-patch | 0x1001b0e0 | - | 1 | 5/3/5 | abi_episode_switch_store_shim*, abi_episode_switch_shim_b*, abi_episode_switch_sum*, abi_episode_switch_vcall*, abi_episode_switch_shim_c* | 1:1 |
| 27 | EpisodicVehicleSupport | signature-patch | 0x1001b580 | 0x300 | 1 | 25/0/0 | - | 1:1 |
| 28 | EpisodicWeaponSupport | signature-patch | 0x1001b380 | - | 1 | 8/0/0 | - | 1:1 |
| 29 | FastLoading | function-call-only | 0x1001eca0 | - | 1 | 0/1/0 | - | 1:1 |
| 30 | FixPCIDKickbug | function-call-only | 0x0 | - | 1 | 0/0/0 | - | 1:1 (DllMain helper) |
| 31 | ForceCarHeadlightShadows | function-call-only | 0x0 | - | 1 | 0/6/0 | abi_fchs_shim | 1:1 with fault-avoidance guard |
| 32 | ForceDynamicShadowsEverywhere | signature-patch | 0x1001ee50 | - | 1 | 1/0/0 | - | 1:1 |
| 33 | ForceShadowsOnObjects | signature-patch | 0x1001eed0 | - | 1 | 1/0/0 | - | 1:1 |
| 34 | FreezeCarFix | function-call-only | 0x0 | - | 1 | 0/29/0 | abi_spawns_guard*, abi_spawns_case_0x24*, abi_spawns_case_0xCA*, abi_spawns_case_0x6F*, abi_spawns_case_0xE0*, abi_spawns_case_0x3D*, abi_spawns_case_0x56*, abi_spawns_case_0x88*, abi_spawns_case_0x9E*, abi_spawns_case_0xF6*, abi_spawns_case_0xB4*, abi_spawns_probe* | 1:1 |
| 35 | FreezeGunFix | signature-patch | 0x10037070 | - | 1 | 1/2/0 | abi_freeze_gun_shim | 1:1 (exact shim) |
| 36 | GarageModelFix | function-call-only | 0x10050860 | - | 1 | 0/4/0 | abi_garage_body_a, abi_garage_body_b* | 1:1 |
| 37 | GroupHackFix | signature-patch | 0x10036f30 | - | 1 | 1/1/1 | abi_group_hack_shim* | 1:1 (exact shim) |
| 38 | HighFPSBikePhysicsFix | signature-patch | 0x1001efa0 | - | 1 | 1/1/1 | abi_bike_physics_detour | 1:1 |
| 39 | HighFPSSpeedupFix | signature-patch | 0x10023b30 | - | 1 | 9/0/0 | - | 1:1 |
| 40 | HighQualityReflections | function-call-only | 0x10023dc0 | - | 0 | 0/3/0 | abi_hqr_body* | withheld (blocker: pattern string + destination body) |
| 41 | ImprovedShaderStreaming | signature-patch | 0x10045ad0 | - | 1 | 6/4/0 | abi_improved_shader_streaming_find_hpe, abi_improved_shader_streaming_find_v | 1:1 |
| 42 | IncreasePtrNodes | signature-patch | 0x10023080 | - | 1 | 0/0/0 | - | 1:1 (literal stores) |
| 43 | IncreaseVehicleModelLimit | signature-patch | 0x10022fc0 | - | 1 | 0/0/0 | - | 1:1 (indirect write) |
| 44 | IncreaseVehicleStructLimit | signature-patch | 0x10023020 | - | 1 | 0/0/0 | - | 1:1 (literal store) |
| 45 | InfiniteCarBottomDistance | signature-patch | 0x10022d80 | - | 1 | 2/1/0 | - | 1:1 |
| 46 | LaunchWithPlayGTAIV | rva-patch | 0x100441b0 | - | 1 | 0/0/0 | - | build-gated off 1.0.8.0 |
| 47 | LoadDLCs | rva-patch | 0x100234e0 | 0x380 | 1 | 17/0/5+ri18 | abi_load_dlcs_dummy, abi_load_dlcs_struct_fixup | 1:1 |
| 48 | LocalSettingsPath | signature-patch | 0x100513a0 | - | 1 | 2/11/0 | abi_local_settings_detour*, abi_local_settings_fallback_detour* | 1:1 (2 inert loader rows documented) |
| 49 | MPNikoCrashFix | signature-patch | 0x10037290 | - | 1 | 1/0/0 | - | 1:1 |
| 50 | MiscFixes | signature-patch | 0x10023dd0 | - | 1 | 11/0/0 | - | 1:1 (bundle kept) |
| 51 | MouseFix | function-call-only | 0x1003d7a0 | - | 1 | 0/23/0 | abi_mouse_axis_float*, abi_mouse_axis_neg*, abi_mouse_axis_int*, abi_mouse_axis_flag*, abi_mouse_advance*, abi_mouse_angle* | 1:1 (shipped 2026-10-13) |
| 52 | MovingPlaneParts | flag-only | 0x0 | - | 1 | 0/8/0 | abi_copsirens_ide, abi_steer_detour_eax*, abi_steer_detour_edx* | 1:1 |
| 53 | MovingSteeringWheelSupport | flag-only | 0x0 | - | 1 | 0/8/0 | abi_copsirens_ide, abi_steer_detour_eax*, abi_steer_detour_edx* | 1:1 |
| 54 | NewFilesCompatibility | signature-patch | 0x10051920 | - | 1 | 3/3/1 | abi_newfiles_register_body, abi_newfiles_update_a, abi_newfiles_update_b | 1:1 |
| 55 | NewMemorySystem | signature-patch | 0x10038680 | - | 0 | 2/3/0 | abi_nms_apply* | 1:1 (shipped 2026-10-13) |
| 56 | NewSavesCompatibility | signature-patch | 0x10052200 | - | 1 | 1/4/1 | abi_new_saves_compat | 1:1 with fault-avoidance guards |
| 57 | NoImpostorHeightCutoff | function-call-only | 0x0 | - | 1 | 0/0/0+ml2 | - | 1:1 (match-literals) |
| 58 | NoLODLightHeightCutoff | function-call-only | 0x0 | - | 1 | 0/0/0 | - | 1:1 (match-literals) |
| 59 | NoLiveryLimit | signature-patch | 0x1003d420 | - | 1 | 2/5/3 | abi_nolivery_a*, abi_nolivery_b*, abi_nolivery_c*, abi_nolivery_d*, abi_nolivery_e* | 1:1 |
| 60 | NoLoadingSleep | signature-patch | 0x1003d670 | - | 1 | 0/0/0 | - | 1:1 (literal store) |
| 61 | NoPhysicsSleep | function-call-only | 0x0 | - | 1 | 0/0/0 | - | 1:1 (indirect write) |
| 62 | OutOfCommissionFix | function-call-only | 0x10050a20 | - | 1 | 0/5/0 | abi_ooc_wrap, abi_ooc_branch* | 1:1 (shipped 2026-10-13) |
| 63 | OverrideGTADatPath | signature-patch | 0x10022f60 | - | 1 | 0/0/0 | - | 1:1 |
| 64 | OverrideGTADatPathTo | function-call-only | 0x0 | - | common:/data/gta.dat | 0/0/0 | - | 1:1 |
| 65 | Patch4RemoveModChecks | function-call-only | 0x0 | - | 1 | 0/2/0 | abi_gfwl_stub | 1:1 |
| 66 | Patch6and7RemoveModChecks | rva-patch | 0x10044710 | 0x180 | 1 | 13/0/0+ri3 | - | 1:1 |
| 67 | PathNodeCrashFix | signature-patch | 0x1003d7d0 | - | 1 | 1/2/1 | abi_path_node_shim* | 1:1 (exact shim) |
| 68 | PedModelsInCutscenesFix | signature-patch | 0x100508c0 | - | 1 | 1/1/1 | abi_ped_models_in_cutscenes_check | 1:1 |
| 69 | PipeBombDropIconSupport | signature-patch | 0x1001be40 | - | 1 | 1/0/0 | - | 1:1 |
| 70 | PlaneCrashFix | signature-patch | 0x1003d890 | - | 1 | 1/2/1 | abi_plane_crash_shim* | 1:1 (exact shim) |
| 71 | PlayerAsPedComponentsFix | signature-patch | 0x1003d9c0 | - | 1 | 1/2/1 | abi_player_ped_components_shim* | 1:1 (exact shim) |
| 72 | PoliceEpisodicWeaponSupport | signature-patch | 0x1001bec0 | - | 1 | 1/0/0 | - | 1:1 |
| 73 | PopupHeadlightsFlag | signature-patch | 0x10044590 | - | 1 | 2/12/1 | abi_copsirens_ide, abi_popup_headlights, abi_popup_headlights_edx* | 1:1 (EDX twin shipped 2026-10-13) |
| 74 | RemoveAllWeaponsProtection | signature-patch | 0x10037100 | - | 1 | 1/0/0 | - | 1:1 |
| 75 | RemoveBoundingBoxCulling | signature-patch | 0x10038770 | - | 0 | 1/1/0 | - | 1:1 with INI escape hatch |
| 76 | RemoveGFWLUpdatePopup | function-call-only | 0x0 | - | 1 | 0/0/0 | - | 1:1 (DllMain helper) |
| 77 | RemoveRGSCLoginRequirement | signature-patch | 0x10045490 | 0x200 | 1 | 3/0/1+ri1 | abi_rgsc_login_removed | 1:1 |
| 78 | RemoveStaticCarShadows | signature-patch | 0x10038800 | - | 1 | 1/0/0 | - | 1:1 |
| 79 | RemoveStaticCutsceneCarShadows | signature-patch | 0x10038860 | - | 1 | 1/0/0 | - | 1:1 |
| 80 | RemoveUselessChecks | rva-patch | 0x10044bd0 | 0x320 | 1 | 23/0/0+ri9 | - | 1:1 |
| 81 | RestoreDeathMusic | function-call-only | 0x0 | - | 1 | 0/3/0 | abi_death_music_enabled, abi_death_music_install | 1:1 |
| 82 | ReversingLightFix | signature-patch | 0x10045430 | - | 1 | 1/0/0 | - | 1:1 |
| 83 | SCOSignatureFix | signature-patch | 0x10045980 | - | 1 | 1/0/0 | - | 1:1 |
| 84 | SMPA60Fix | signature-patch | 0x100467a0 | - | 1 | 3/4/1 | abi_smpa60_shim*, abi_smpa60_tail_shim* | 1:1 (exact shims) |
| 85 | SavegameFix | signature-patch | 0x10045610 | - | 1 | 3/11/0 | abi_savegame_fix_null*, abi_savegame_fix_close, abi_savegame_fix_copy, abi_savegame_fix_mutex | 1:1 |
| 86 | ShakingEngineBlock | flag-only | 0x0 | - | 1 | 0/8/0 | abi_copsirens_ide, abi_steer_detour_eax*, abi_steer_detour_edx* | 1:1 |
| 87 | SkipIntro | signature-patch | 0x10045c50 | - | 1 | 1/2/0 | abi_skip_intro_body | 1:1 (compiled body) |
| 88 | SkipLauncher | signature-patch | 0x10045ce0 | - | 1 | 8/0/0 | - | 1:1 |
| 89 | SkipMenu | signature-patch | 0x10046640 | - | 1 | 2/6/0 | abi_skip_menu_body | 1:1 (compiled body) |
| 90 | SpeedupBugFix | signature-patch | 0x100506b0 | - | 1 | 1/9/0 | abi_speedup_body | 1:1 |
| 91 | StartupTimeFix | function-call-only | 0x100468d0 | - | 1 | 0/0/0+ml1 | - | 1:1 (match-literals) |
| 92 | SuperLODFix | rva-patch | 0x10046a10 | 0x300 | 1 | 0/6/2+ri0 | abi_super_lod_fix_shim | 1:1 |
| 93 | TBoGTHeliHeightLimit | signature-patch | 0x1001b310 | - | 1 | 1/0/0 | - | 1:1 |
| 94 | TeleportProtection | signature-patch | 0x10037170 | - | 1 | 1/0/0 | - | 1:1 |
| 95 | TryToSkipAllErrors | signature-patch | 0x10046b30 | - | 1 | 1/0/0 | - | 1:1 |
| 96 | VRAMFix | signature-patch | 0x1004aae0 | - | 1 | 1/0/1 | abi_vram_fix_shim | 1:1 |
| 97 | VSyncFix | signature-patch | 0x100503b0 | - | 1 | 0/0/0 | - | 1:1 |
| 98 | WantedLevelCrashFix | signature-patch | 0x100504b0 | - | 1 | 1/1/1 | abi_wanted_level_shim* | 1:1 (exact shim) |

`*` = byte-exact body verified by verify_hook_bodies. Row counts exclude literal stores/indirect writes in the jmp column.

## Per-option audit

### 1. AddOptionsInMenu

- arm: `AddOptionsInMenu` default `1`; handler `0x0`; class `flag-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `0`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**
- note: Menu registration via IVMenuAPI; SetMenuIntegrationAvailable drives RestoreDeathMusic's fall-through. Focus-loss guard documented in §4.

### 2. AimProjectileCrashFix

- arm: `AimProjectileCrashFix` default `1`; handler `0x100055c0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 3. AllowHashesForIDE

- arm: `AllowHashesForIDE` default `1`; handler `0x10022cf0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 4. BadPlayerModelCrashFix

- arm: `BadPlayerModelCrashFix` default `1`; handler `0x10037be0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 3, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C`@0; sig_installs:`83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C`@0; slot_installs:`83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C`@
- status: **1:1 with documented divergence**
- note: Includes the native-scripting call layer and the exact abi_bpm_check body; the MP name-table fallback shares DebugPrintSpawns' "**UNKNOWN**" divergence. Fault-avoidance guards on the model array/object are recorded in §4.

### 5. BenchmarkFix

- arm: `BenchmarkFix` default `1`; handler `0x10005690`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 2, jumps 0/2/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20`@; slot_installs:`E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20`@
- status: **1:1**

### 6. BetterMPSync

- arm: `BetterMPSync` default `0`; handler `0x10005800`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 1, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`8B 90 E8 00 00 00 55 8B CE FF`@6
- status: **1:1 (re-added 2026-10-13)**
- note: Row set re-added in session 8 after the bisect.

### 7. BikeFeetFix

- arm: `BikeFeetFix` default `1`; handler `0x1001bdd0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 8. BikePhoneAnimsFix

- arm: `BikePhoneAnimsFix` default `1`; handler `0x1001bd60`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 9. BlackscreenFix

- arm: `BlackscreenFix` default `1`; handler `0x10036fd0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 2, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`75 54 8B 48 28 8B 40 2C`@82; slot_installs:`75 54 8B 48 28 8B 40 2C`@82
- status: **1:1 (exact shim)**
- note: Exact body; the loader op row that writes inside the original shim's own bytes is withheld by shim_withheld() (our layout differs -> writing there would corrupt our code).

### 10. BorderlessWindowed

- arm: `BorderlessWindowed` default `1`; handler `0x10005970`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 2, jumps 0/2/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_writes:`FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00`@5; sig_writes:`FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00`@5
- status: **1:1**

### 11. BuildingAlphaFix

- arm: `BuildingAlphaFix` default `1`; handler `0x10005ca0`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 2, curated 1, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`F7 46 24 00 00 00 40 0F 84`@7
- status: **1:1 except pattern 2 (withheld)**
- note: Pattern 2 is deliberately left unapplied (crash evidence); documented in parity_and_improvements.md §1/§4.

### 12. BuildingDynamicShadows

- arm: `BuildingDynamicShadows` default `0`; handler `0x10005b20`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 5, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`8B 47 28 C1 E8 06 83 E0 0F 83 C0`@0; slot_installs:`8B 47 28 C1 E8 06 83 E0 0F 83 C0`@0; slot_installs:`8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06`@20; slot_installs:`8B 46 28 83 4E 24 05 25 7F FC FF FF 83 C8 40 89 46 28 C7 06`@20; sig_writes:`50 68 04 01 00 00 6A 02 6A 00 E8`@2
- status: **1:1**

### 13. CarDecalsFix

- arm: `CarDecalsFix` default `1`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 1, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`8B 44 24 08 8B 48 28 81 E1 C0 03 00 00 81 F9 80 00 00 00`@0
- status: **1:1**

### 14. CarDynamicShadowFix

- arm: `CarDynamicShadowFix` default `1`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 13, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8`@0; sig_installs:`56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8`@0; sig_installs:`83 F8 14 7D 05 83 F8 06 7D 14`@3; slot_installs:`56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8`@; slot_installs:`56 6A 02 53 53 57 E8 ?? ?? ?? ?? 56 6A 02 6A 04 6A 05 57 E8`@7; slot_installs:`56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8`@; slot_installs:`56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8`@9; slot_installs:`8B 45 10 8B 16 8B 92 8C 00 00 00`@; (+5)
- status: **1:1 (build variants shipped 2026-10-13)**
- note: Shared installer sub_10010B60: pattern A (1060+) -> E9@0 to 0x100068A0, dword_1015BDCC = match+0xB, dword_1015B5B8 = match + *(match+7) + 0xB; pattern B (1000/1004/1010/1040) -> E9@0 to 0x100068D0 with match+0xD / match + *(match+9) + 0xD; the C/D/E branch (old builds) E9s C+3 -> C+0x1E, publishes dword_1015BDC4 = Dmatch (new rebased global zpatch_car_dynamic_d) and NOP-fills 6 bytes at C+0x3E when pattern E is missing (internal spelling CarDynamicShadowFix@noE, build gate 1000/1040 - E exists only on 1.0.0.4). Pattern F's EB write (1060+, reached only when C is absent) plus the four sub_1000B790/sub_1000EA00 sig_writes. Verified per build in the native tests against GTAIV_1040.exe; the shared installer calls sub_10006910 only when CarDynamicShadowFix==1 && CarPartsShadowFix!=1.

### 15. CarPartsShadowFix

- arm: `CarPartsShadowFix` default `1`; handler `0x10006910`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `0`
- our rows: op 0, curated 3, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`85 DB 7E 41 55 EB 0A`@-6; slot_installs:`85 DB 7E 41 55 EB 0A`@; slot_installs:`85 DB 7E 41 55 EB 0A`@
- status: **1:1 (gating + old-build variant shipped 2026-10-13)**
- note: Loader standalone arm (key==1) runs sub_10006910 + sub_10007E40; the shared CarDynamicShadowFix installer runs only sub_10006910. The curated rows now gate through ini_option_enabled: CarPartsShadowFix rows fire on key==1 OR (CarDynamic==1 && key!=1); sub_10007E40's EB write row uses the internal spelling CarPartsShadowFix@standalone (key==1 only). Regression test: 'CarParts standalone EB row'.

### 16. CollisionDeadlockFix

- arm: `CollisionDeadlockFix` default `1`; handler `0x10005e30`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 1, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66`@91
- status: **1:1 (exact shim)**
- note: Exact body; in-shim op row withheld (shim_withheld).

### 17. CombinedRadioFreezeFix

- arm: `CombinedRadioFreezeFix` default `1`; handler `0x10005ee0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 1, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0`@0
- status: **1:1 (re-added 2026-10-13)**
- note: Row set re-added in session 8.

### 18. CopSirensFlag

- arm: `CopSirensFlag` default `1`; handler `0x10006350`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 2, curated 6, jumps 2/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00`@0; sig_installs:`0F BF 41 2E 39 05`@22; sig_installs:`0C 83 88 94 00 00 00 04`@-10; slot_installs:`E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00`@; slot_installs:`0F BF 41 2E 39 05`@; slot_installs:`0C 83 88 94 00 00 00 04`@
- status: **1:1**

### 19. CrashMsgFix

- arm: `CrashMsgFix` default `1`; handler `0x100371f0`; class `signature-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 2, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`68 6A 10 4E 31`@-4; slot_installs:`68 6A 10 4E 31`@1
- status: **1:1**

### 20. CutsceneFixes

- arm: `CutsceneFixes` default `1`; handler `0x100441a0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `0`
- our rows: op 0, curated 4, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8`@-5; sig_installs:`8B CF 88 44 24 0F E8 ? ? ? ? A1`@-5; slot_installs:`8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8`@-4; slot_installs:`8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8`@-4
- status: **1:1 (shipped 2026-10-13)**
- note: Two chains: chain A (callback 0x10044160 writing EB at the never-published site slot 0x10185F88) is dead, but chain B - the 18 KB worker 0x10038B40 reached through the second forwarder 0x10045420/0x10045410 and slot 0x10164F04 - is the real cutscene fix (the user confirms jittery camera/zoom fixes). Shipped: scan 1 pattern 8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8 (E8 at match-34 -> sub_100388C0, publishes dword_1015BDE4 = match + *(match-4) and dword_1015BDF0 = that + 0x4F) and scan 2 pattern 8B CF 88 44 24 0F E8 ? ? ? ? A1 (E8 at match-5 -> the same body). Both patterns: 1 hit on every classic build + EFLC 1110/1120/1130, absent on 12059. The body 0x100388C0 is a 160-instruction @exact transcription (verify-bodies PASS) that reads a scene float via the loader resolver (salt dword_1015803C), pokes the camera function's mov byte [esp+0x1C],imm immediate (target+0x4E), double-calls the original and keeps the stable result after the 0.3333 s accumulator. sub_10045430 (0x60 over the 0x64 field offset) belongs to ReversingLightFix, not this option.

### 21. DebugPrintSpawns

- arm: `DebugPrintSpawns` default `0`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1 with documented divergence**
- note: 394-instruction probe, exact body. The MP name lookup uses the extracted 38455-entry table at 0x100DE770 and the original first-match hash/fallback behavior (F15 closed; `extract_model_catalog.py --check`).

### 22. DoNotPauseOnMinimize

- arm: `DoNotPauseOnMinimize` default `0`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 2, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_writes:`6A 0A 56 50 FF D2 A1`@1; sig_writes:`75 ? 8B 0D ? ? ? ? 51 FF 15 ? ? ? ? 85 C0 75 ? 8B 15`@0
- status: **1:1**

### 23. DrivebyFOVFix

- arm: `DrivebyFOVFix` default `1`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 2, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`F3 0F 10 46 60 F3 0F 5C CA`@; slot_installs:`08 0B C2 8B F0 E8`@6
- status: **1:1 with documented guards (registered 2026-10-13)**
- note: The arm installs unconditionally when its pattern `F3 0F 10 46 60 F3 0F 5C CA` matches, and that pattern has 1 hit on 1.0.8.0 - the earlier 'not installed on 1080' reading came from the coverage table's handler_va=0 entry and was wrong. The shim (0x100064B0) is registered; it reproduces the original's `pushal; call walker; popal; movss xmm0,[fov]; jmp [resume]` with fault-avoidance guards on the resolver result (the original faults when the handle is unregistered) and keeps the 45.0f default until it resolves. See parity_and_improvements.md sec.4.

### 24. DualVehicleHeadlights

- arm: `DualVehicleHeadlights` default `0`; handler `0x10006770`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: 0x7e; INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **build-gated off 1.0.8.0**
- note: Build mask 0x7E (compact 1000-1060); absent on 1080 - matching the loader.

### 25. EmissiveLerpFix

- arm: `EmissiveLerpFix` default `1`; handler `0x1001afa0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 3, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1`@-24; slot_installs:`F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1`@; slot_installs:`F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1`@
- status: **1:1**

### 26. EpisodeSwitchFreezeFix

- arm: `EpisodeSwitchFreezeFix` default `1`; handler `0x1001b0e0`; class `signature-patch`; loader op rows 0, loader sig ops 5, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 5, curated 3, jumps 5/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84`@; slot_installs:`8B 4B 10 3B 4B 14 7D 0F`@; slot_installs:`56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05`@
- status: **1:1**

### 27. EpisodicVehicleSupport

- arm: `EpisodicVehicleSupport` default `1`; handler `0x1001b580`; class `signature-patch`; loader op rows 0, loader sig ops 25, resolved-on-1080 `true`
- build gate: 0x300; INI value: `1`
- our rows: op 25, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 28. EpisodicWeaponSupport

- arm: `EpisodicWeaponSupport` default `1`; handler `0x1001b380`; class `signature-patch`; loader op rows 0, loader sig ops 8, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 8, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 29. FastLoading

- arm: `FastLoading` default `1`; handler `0x1001eca0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 1, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_writes:`6A 0A E8 ? ? ? ? 83 C7 0A 83 C4 04 81 FF E8 03 00 00 75 02 33 FF`@1
- status: **1:1**

### 30. FixPCIDKickbug

- arm: `FixPCIDKickbug` default `1`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1 (DllMain helper)**
- note: The HKCU PCID deletion is implemented in dllmain.cpp (apply-time, default 1). Safety candidate in parity_and_improvements.md §6.3.

### 31. ForceCarHeadlightShadows

- arm: `ForceCarHeadlightShadows` default `0`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 6, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`3C 01 75 50 84 DB 75 79`@6; sig_installs:`3C 01 75 50 84 DB 75 79`@95; sig_installs:`3C 01 75 50 84 DB 75 79`@379; sig_installs:`3C 01 75 50 84 DB 75 79`@211; sig_installs:`55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B`@0; slot_installs:`55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 8B`@
- status: **1:1 with fault-avoidance guard**
- note: The per-call table walk body is exact; the lookup/object guard is documented in §4 (original can deref a null object).

### 32. ForceDynamicShadowsEverywhere

- arm: `ForceDynamicShadowsEverywhere` default `0`; handler `0x1001ee50`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 33. ForceShadowsOnObjects

- arm: `ForceShadowsOnObjects` default `1`; handler `0x1001eed0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 34. FreezeCarFix

- arm: `FreezeCarFix` default `1`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 29, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0`@7; sig_installs:`8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ? 5F 5E 5D 5B`@11; sig_installs:`E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00`@36; sig_installs:`E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00`@61; sig_installs:`E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00`@86; sig_installs:`E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00`@111; sig_installs:`E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00`@136; sig_installs:`E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00`@158; (+21)
- status: **1:1**
- note: 17 E8 rows + DebugPrintSpawns' 394-insn body; per-call wheelangle/model lookups are perf candidates (Mission C).

### 35. FreezeGunFix

- arm: `FreezeGunFix` default `1`; handler `0x10037070`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 2, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF`@-21; slot_installs:`2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF`@
- status: **1:1 (exact shim)**
- note: Exact body.

### 36. GarageModelFix

- arm: `GarageModelFix` default `1`; handler `0x10050860`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 4, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`8B 57 20 D9 42 34 D9 5E 04 8B 47 20`@0; sig_installs:`0F B7 0E 50 51 E8`@0; slot_installs:`8B 57 20 D9 42 34 D9 5E 04 8B 47 20`@; slot_installs:`0F B7 0E 50 51 E8`@
- status: **1:1**

### 37. GroupHackFix

- arm: `GroupHackFix` default `1`; handler `0x10036f30`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 1, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 04`@
- status: **1:1 (exact shim)**
- note: Exact body; in-shim op row withheld (shim_withheld).

### 38. HighFPSBikePhysicsFix

- arm: `HighFPSBikePhysicsFix` default `1`; handler `0x1001efa0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 1, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00`@
- status: **1:1**

### 39. HighFPSSpeedupFix

- arm: `HighFPSSpeedupFix` default `1`; handler `0x10023b30`; class `signature-patch`; loader op rows 0, loader sig ops 9, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 9, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 40. HighQualityReflections

- arm: `HighQualityReflections` default `0`; handler `0x10023dc0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `0`
- our rows: op 0, curated 3, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8`@0; slot_installs:`8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8`@0; slot_installs:`8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8`@15
- status: **withheld (blocker: pattern string + destination body)**
- note: Arm key default 0 (calls sub_10023DC0 -> sub_10023DB0). Mechanics decoded: module scan (sub_1005B6A0) of the string built in [ebp-0x430] from the literal table at 0x10129F8C; on a match, E9 at the match to [0x10192F18] (the arm-installed second body 0x1001F030), and publishes dword_10159CFC = match+0x13, dword_10159D04 = *(match+0xF)+0x13+match. BLOCKER: the builder is fully inlined (no append/reset calls), and the reconstructed '4' literal + seed string has 0 hits in GTAIV.exe - the pattern is still unknown; the 163-instruction destination body 0x1001F030 is not decompiled/ported. Nothing installs (default 0 anyway).

### 41. ImprovedShaderStreaming

- arm: `ImprovedShaderStreaming` default `1`; handler `0x10045ad0`; class `signature-patch`; loader op rows 0, loader sig ops 6, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 6, curated 4, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24`@85; sig_installs:`33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24`@98; sig_installs:`33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24`@253; sig_installs:`33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 8B 44 24 0C 8B 74 24`@266
- status: **1:1**

### 42. IncreasePtrNodes

- arm: `IncreasePtrNodes` default `1`; handler `0x10023080`; class `signature-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 3, indirect 0, match-literals 0, rva-installs 0
- status: **1:1 (literal stores)**
- note: Two literal stores (recovered_literal_stores.inc) resizing the path-node pools.

### 43. IncreaseVehicleModelLimit

- arm: `IncreaseVehicleModelLimit` default `1`; handler `0x10022fc0`; class `signature-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 1, match-literals 0, rva-installs 0
- status: **1:1 (indirect write)**
- note: Indirect 4-byte write 0x400 at game 0xB3E8E8.

### 44. IncreaseVehicleStructLimit

- arm: `IncreaseVehicleStructLimit` default `1`; handler `0x10023020`; class `signature-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 1, indirect 0, match-literals 0, rva-installs 0
- status: **1:1 (literal store)**
- note: 1-byte literal store 0x64 at game 0x64A2B1.

### 45. InfiniteCarBottomDistance

- arm: `InfiniteCarBottomDistance` default `1`; handler `0x10022d80`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 2, curated 1, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_writes:`0F 2F D0 76 25 0F 28 C2`@3
- status: **1:1**

### 46. LaunchWithPlayGTAIV

- arm: `LaunchWithPlayGTAIV` default `1`; handler `0x100441b0`; class `rva-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **build-gated off 1.0.8.0**
- note: Implemented in dllmain (apply_launch_with_play_gtaiv); the loader excludes 1080.

### 47. LoadDLCs

- arm: `LoadDLCs` default `1`; handler `0x100234e0`; class `rva-patch`; loader op rows 17, loader sig ops 0, resolved-on-1080 `false`
- build gate: 0x380; INI value: `1`
- our rows: op 17, curated 0, jumps 0/0/5, literal stores 0, indirect 0, match-literals 0, rva-installs 27
- status: **1:1**

### 48. LocalSettingsPath

- arm: `LocalSettingsPath` default `0`; handler `0x100513a0`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 2, curated 11, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`53 8B 5C 24 08 56 6A 00 6A 00`@16; sig_installs:`53 8B 5C 24 08 56 6A 00 6A 00`@32; sig_installs:`7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3`@14; sig_installs:`7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3`@-13; slot_installs:`7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3`@-178; slot_installs:`7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3`@-190; slot_installs:`7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3`@-151; slot_installs:`7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3`@-444; (+3)
- status: **1:1 (2 inert loader rows documented)**
- note: Two duplicated bare-E9 op rows (offset -13 against the FF D7 fallback pattern) never fire: that pattern has 0 hits on 1.0.8.0, and the curated sig_install covers the intended write. They cannot be suppressed textually (runtime-built handler patterns); documented instead.

### 49. MPNikoCrashFix

- arm: `MPNikoCrashFix` default `1`; handler `0x10037290`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 50. MiscFixes

- arm: `MiscFixes` default `1`; handler `0x10023dd0`; class `signature-patch`; loader op rows 0, loader sig ops 11, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 11, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1 (bundle kept)**
- note: The original bundles five fixes under one key; the per-fix split was reverted (archived in §1). Rows: 6 writes.

### 51. MouseFix

- arm: `MouseFix` default `1`; handler `0x1003d7a0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 23, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C`@0; sig_installs:`8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8`@0; sig_installs:`83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07`@5; sig_installs:`83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07`@33; sig_installs:`83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07`@3; sig_installs:`83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07`@31; sig_installs:`83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07`@5; sig_installs:`83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07`@33; (+15)
- status: **1:1 (shipped 2026-10-13)**
- note: Nine decode-verified patterns (all unique on 1.0.8.0). Four axis helpers, the in-angle helper and both bodies are byte-exact transcriptions (verify-bodies PASS); the advance helper (sub_100241E0) is a compiled INFO body whose only contract is VirtualProtect on the published resume. The 90-90 / 66 0F 6E C0 in-pattern writes are dest_kind=6 literal_bytes rows applied atomically in one patch-set pass. dword_10159C34 is published unconditionally (original: cmovne keeps 0 when the pattern misses; identical on 1080). Constants transcribed from .rdata (hex floats). INI key =0 with NEW comment.

### 52. MovingPlaneParts

- arm: `MovingPlaneParts` default `1`; handler `0x0`; class `flag-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 8, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@2; sig_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@2; sig_installs:`0C 83 88 94 00 00 00 04`@-10; slot_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@; slot_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@423; slot_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@; slot_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@140; slot_installs:`0C 83 88 94 00 00 00 04`@
- status: **1:1**

### 53. MovingSteeringWheelSupport

- arm: `MovingSteeringWheelSupport` default `1`; handler `0x0`; class `flag-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 8, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@2; sig_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@2; sig_installs:`0C 83 88 94 00 00 00 04`@-10; slot_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@; slot_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@423; slot_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@; slot_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@140; slot_installs:`0C 83 88 94 00 00 00 04`@
- status: **1:1**

### 54. NewFilesCompatibility

- arm: `NewFilesCompatibility` default `1`; handler `0x10051920`; class `signature-patch`; loader op rows 0, loader sig ops 3, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 3, curated 3, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 7B FF FF FF`@0; sig_installs:`E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3F 03 EB 85 FF 0F 85 6C FF FF FF`@0; slot_installs:`68 64 28 C8 67 E8`@6
- status: **1:1**

### 55. NewMemorySystem

- arm: `NewMemorySystem` default `0`; handler `0x10038680`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `0`
- our rows: op 2, curated 3, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`3D 80 99 99 59 73 05`@-34; sig_installs:`3D 80 99 99 59 73 05`@-29; slot_installs:`6A 01 B9 00 00 40 06`@8
- status: **1:1 (shipped 2026-10-13)**
- note: The handler's two scans: E8 at match-34 of 3D 80 99 99 59 73 05 -> the body sub_10038670 (writes the CRageAllocator vtable into the published allocator singleton), E9 at match-29 -> match-16 (skips the game's init/registration tail), and the second scan (6A 01 B9 00 00 40 06) publishes dword_10159CD8 = *(match+8) (the mov esi, 0x19cf9f0 immediate). The vtable's 24 entries are shipped: 10 stubs + the ctor are @exact (verify-bodies PASS) and alloc/free/contains (vtable[2]/[3]/[18]) are compiled ports of the original's HeapAlloc wrapper + registry (0xCD fill, [p-4] base offset, FNV-1a membership under a recursive critical section). INI default 0; the user's INI has 1.

### 56. NewSavesCompatibility

- arm: `NewSavesCompatibility` default `1`; handler `0x10052200`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 4, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`89 84 24 E0 01 00 00 8B 45 08 53 33 DB`@0; slot_installs:`68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08`@1; slot_installs:`89 84 24 E0 01 00 00 8B 45 08 53 33 DB`@; slot_installs:`89 84 24 E0 01 00 00 8B 45 08 53 33 DB`@
- status: **1:1 with fault-avoidance guards**
- note: The fread/size/malloc guards are documented in §4; chunk payloads are read+discarded (perf candidate).

### 57. NoImpostorHeightCutoff

- arm: `NoImpostorHeightCutoff` default `1`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 2, rva-installs 0
- status: **1:1 (match-literals)**
- note: Two 2-byte NOPs (rows in recovered_match_literals.inc, all-match index).

### 58. NoLODLightHeightCutoff

- arm: `NoLODLightHeightCutoff` default `0`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 1, indirect 0, match-literals 0, rva-installs 0
- status: **1:1 (match-literals)**
- note: Same NOP pair as NoImpostorHeightCutoff.

### 59. NoLiveryLimit

- arm: `NoLiveryLimit` default `1`; handler `0x1003d420`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 2, curated 5, jumps 3/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`8B 8C 83 3C 01 00 00 0F BF 53 48`@-237; sig_installs:`8B 8C 83 3C 01 00 00 0F BF 53 48`@-199; slot_installs:`74 52 8D B5 3C 01 00 00 83 FB 04`@-6; slot_installs:`74 52 8D B5 3C 01 00 00 83 FB 04`@; slot_installs:`8B 8C 83 3C 01 00 00 0F BF 53 48`@
- status: **1:1**

### 60. NoLoadingSleep

- arm: `NoLoadingSleep` default `1`; handler `0x1003d670`; class `signature-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 1, indirect 0, match-literals 0, rva-installs 0
- status: **1:1 (literal store)**
- note: 4-byte zero literal store at the loading-sleep branch (recovered_literal_stores.inc) plus the handler op rows.

### 61. NoPhysicsSleep

- arm: `NoPhysicsSleep` default `1`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 1, match-literals 0, rva-installs 0
- status: **1:1 (indirect write)**
- note: Indirect literal write at game 0xC90B18 (recovered_indirect_writes.inc).

### 62. OutOfCommissionFix

- arm: `OutOfCommissionFix` default `1`; handler `0x10050a20`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 5, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`A9 39 44 24 18`@7; sig_installs:`E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56`@0; slot_installs:`A9 39 44 24 18`@0; slot_installs:`E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56`@-50; slot_installs:`E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56`@1
- status: **1:1 (shipped 2026-10-13)**
- note: Scan 1 (A9 39 44 24 18) E9s match+7 to the branch-widening body (byte-exact @exact) and publishes dword_10158034=match+0x11. Scan 2 (E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56) wraps the game's episode/mount call with the compiled abi_ooc_wrap and publishes dword_10159BF8 (the game's mount function 0xA22BE0) / dword_10159C04 (the match E8's original target). The wrapper builds the original's embedded compressed "finale2.sco" resource (src/recovered_ooc_pak.inc, 163796 bytes at 0x100B6798; OOC Adler-32 of the decoded 179536-byte table = 0x41E794AB, proven by the blob trailer), builds the in-memory RPF archive (sub_1004ACE0, byte-golden via tools/extract_ooc_pak.py, FNV-1a 0xB5AF7B10) and mounts it with the game's own mount function via the original's "memory:$<ptr>,<size>,0:" format string (0x1012EB58/0x1012EB0C). Native self-test: zpatch_ooc_selftest(). The only divergence is a fault-avoidance null check on the table/archive malloc (the original would crash); the per-call leak (the guard dword_10159C00 has no writer) is kept 1:1.

### 63. OverrideGTADatPath

- arm: `OverrideGTADatPath` default `0`; handler `0x10022f60`; class `signature-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 1, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**
- note: The detour swaps the pushed gta.dat path for the plugin buffer filled by OverrideGTADatPathTo at load.

### 64. OverrideGTADatPathTo

- arm: `OverrideGTADatPathTo` default `common:/data/gta.dat`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `common:/data/gta.dat`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**
- note: Value-only key (default `common:/data/gta.dat`): the arm copies the string into the plugin buffer OverrideGTADatPath's detour pushes.

### 65. Patch4RemoveModChecks

- arm: `Patch4RemoveModChecks` default `0`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 2, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08`@0; slot_installs:`2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 0C D8 0D ? ? ? ? D9 5C 24 08`@
- status: **1:1**
- note: Four E9 rows.

### 66. Patch6and7RemoveModChecks

- arm: `Patch6and7RemoveModChecks` default `1`; handler `0x10044710`; class `rva-patch`; loader op rows 13, loader sig ops 0, resolved-on-1080 `false`
- build gate: 0x180; INI value: `1`
- our rows: op 13, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 3
- status: **1:1**

### 67. PathNodeCrashFix

- arm: `PathNodeCrashFix` default `1`; handler `0x1003d7d0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 2, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`8B 04 B1 0F B7 D0 81 FA FF FF 00 00`@; slot_installs:`8B 04 B1 0F B7 D0 81 FA FF FF 00 00`@
- status: **1:1 (exact shim)**
- note: Exact body; the loader's in-shim op row is withheld by shim_withheld() (same rationale as BlackscreenFix).

### 68. PedModelsInCutscenesFix

- arm: `PedModelsInCutscenesFix` default `1`; handler `0x100508c0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 1, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E`@
- status: **1:1**

### 69. PipeBombDropIconSupport

- arm: `PipeBombDropIconSupport` default `1`; handler `0x1001be40`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 70. PlaneCrashFix

- arm: `PlaneCrashFix` default `1`; handler `0x1003d890`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 2, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00`@; slot_installs:`83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00`@
- status: **1:1 (exact shim)**
- note: Exact body; in-shim op row withheld (shim_withheld).

### 71. PlayerAsPedComponentsFix

- arm: `PlayerAsPedComponentsFix` default `1`; handler `0x1003d9c0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 2, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`8B 54 24 10 52 0F B6 90 19 02 00 00`@; slot_installs:`8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85 C0 76 3E`@
- status: **1:1 (exact shim)**
- note: Exact body.

### 72. PoliceEpisodicWeaponSupport

- arm: `PoliceEpisodicWeaponSupport` default `0`; handler `0x1001bec0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 73. PopupHeadlightsFlag

- arm: `PopupHeadlightsFlag` default `1`; handler `0x10044590`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 2, curated 12, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1`@0; sig_installs:`0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1`@10; sig_installs:`0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1`@82; sig_installs:`0C 83 88 94 00 00 00 04`@-10; sig_installs:`0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1`@0; sig_installs:`0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1`@10; sig_installs:`0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1`@82; slot_installs:`0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1`@; (+4)
- status: **1:1 (EDX twin shipped 2026-10-13)**
- note: Real flag option: per-call 512-slot scan + popuplights walk are perf candidates. The EDX twin (0x10044560) is now shipped for the builds that have the EDX-form pattern (1000/1004/1010/1040): E9@0 to 0x10044560 + EB@10 + the rebased-global operand at +82 + the two resume publishes (match+10/match+12); the primary (EAX form, 1060+) targets 0x10044530. Verified against GTAIV_1040.exe in the native tests.

### 74. RemoveAllWeaponsProtection

- arm: `RemoveAllWeaponsProtection` default `1`; handler `0x10037100`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 75. RemoveBoundingBoxCulling

- arm: `RemoveBoundingBoxCulling` default `0`; handler `0x10038770`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `0`
- our rows: op 1, curated 1, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`3C 01 57 0F 85`@3
- status: **1:1 with INI escape hatch**
- note: The original branch is kept verbatim; RemoveBoundingBoxCulling=0 selects the guarded variant (documented divergence in parity_and_improvements.md §4).

### 76. RemoveGFWLUpdatePopup

- arm: `RemoveGFWLUpdatePopup` default `0`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1 (DllMain helper)**
- note: The xlive.dll +0x66F770 writes are rebuilt in dllmain/hook bodies; the absent-xlive base guard is recorded in §4.

### 77. RemoveRGSCLoginRequirement

- arm: `RemoveRGSCLoginRequirement` default `1`; handler `0x10045490`; class `signature-patch`; loader op rows 1, loader sig ops 2, resolved-on-1080 `false`
- build gate: 0x200; INI value: `1`
- our rows: op 3, curated 0, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 1
- status: **1:1**

### 78. RemoveStaticCarShadows

- arm: `RemoveStaticCarShadows` default `0`; handler `0x10038800`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 79. RemoveStaticCutsceneCarShadows

- arm: `RemoveStaticCutsceneCarShadows` default `0`; handler `0x10038860`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 80. RemoveUselessChecks

- arm: `RemoveUselessChecks` default `0`; handler `0x10044bd0`; class `rva-patch`; loader op rows 23, loader sig ops 0, resolved-on-1080 `false`
- build gate: 0x320; INI value: `1`
- our rows: op 23, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 9
- status: **1:1**

### 81. RestoreDeathMusic

- arm: `RestoreDeathMusic` default `0`; handler `0x0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 3, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`0F 57 C0 F3 0F 11 86 20 02 00 00`@-13; sig_installs:`C7 86 04 02 00 00 04 00 00 00 E9`@-13; sig_installs:`E8 ? ? ? ? 84 C0 74 C6 8B BE`@0
- status: **1:1**
- note: Three E8 rows + slots; gated by key==1 OR the IVMenuAPI integration being available (the loader's fall-through), mirrored in ini_option_enabled.

### 82. ReversingLightFix

- arm: `ReversingLightFix` default `1`; handler `0x10045430`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 83. SCOSignatureFix

- arm: `SCOSignatureFix` default `1`; handler `0x10045980`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 84. SMPA60Fix

- arm: `SMPA60Fix` default `1`; handler `0x100467a0`; class `signature-patch`; loader op rows 0, loader sig ops 3, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 3, curated 4, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C`@123; slot_installs:`8B 48 70 83 C0 40 50 51 8D 51 10`@; slot_installs:`8B 48 70 83 C0 40 50 51 8D 51 10`@; slot_installs:`01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C`@
- status: **1:1 (exact shims)**
- note: Three exact shims; the loader's in-shim E9/EB op rows are withheld (shim_withheld).

### 85. SavegameFix

- arm: `SavegameFix` default `1`; handler `0x10045610`; class `signature-patch`; loader op rows 0, loader sig ops 3, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 3, curated 11, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`75 ? 83 C1 01 83 C0 01 83 F9 04`@-73; sig_installs:`75 ? 83 C1 01 83 C0 01 83 F9 04`@-143; sig_installs:`75 ? 83 C1 01 83 C0 01 83 F9 04`@-176; sig_installs:`75 ? 83 C1 01 83 C0 01 83 F9 04`@-280; sig_installs:`75 ? 83 C1 01 83 C0 01 83 F9 04`@-240; sig_installs:`8D 4C 24 18 51 6A 00 6A 0C 57`@-86; sig_installs:`8D 4C 24 18 51 6A 00 6A 0C 57`@-47; sig_installs:`8D 4C 24 18 51 6A 00 6A 0C 57`@10; (+3)
- status: **1:1**

### 86. ShakingEngineBlock

- arm: `ShakingEngineBlock` default `1`; handler `0x0`; class `flag-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 8, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@2; sig_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@2; sig_installs:`0C 83 88 94 00 00 00 04`@-10; slot_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@; slot_installs:`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0`@423; slot_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@; slot_installs:`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2`@140; slot_installs:`0C 83 88 94 00 00 00 04`@
- status: **1:1**

### 87. SkipIntro

- arm: `SkipIntro` default `1`; handler `0x10045c50`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 2, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50`@-6; slot_installs:`89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50`@
- status: **1:1 (compiled body)**
- note: Compiled INFO body (reverted shortened-intro variant stays archived in §1).

### 88. SkipLauncher

- arm: `SkipLauncher` default `1`; handler `0x10045ce0`; class `signature-patch`; loader op rows 0, loader sig ops 8, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 8, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**
- note: LaunchWithPlayGTAIV launcher stub path.

### 89. SkipMenu

- arm: `SkipMenu` default `0`; handler `0x10046640`; class `signature-patch`; loader op rows 0, loader sig ops 2, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 2, curated 6, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E`@23; sig_installs:`6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3`@2; sig_installs:`6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3`@2; slot_installs:`83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E`@24; slot_installs:`6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3`@3; slot_installs:`6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3`@3
- status: **1:1 (compiled body)**
- note: Compiled INFO body; INI escape; the reverted savegame guard is archived in §1.

### 90. SpeedupBugFix

- arm: `SpeedupBugFix` default `1`; handler `0x100506b0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 9, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: sig_installs:`50 64 89 25 00 00 00 00 83 EC 34`@-21; slot_installs:`50 64 89 25 00 00 00 00 83 EC 34`@26; slot_installs:`50 64 89 25 00 00 00 00 83 EC 34`@165; slot_installs:`50 64 89 25 00 00 00 00 83 EC 34`@188; slot_installs:`50 64 89 25 00 00 00 00 83 EC 34`@226; slot_installs:`50 64 89 25 00 00 00 00 83 EC 34`@234; slot_installs:`50 64 89 25 00 00 00 00 83 EC 34`@249; slot_installs:`50 64 89 25 00 00 00 00 83 EC 34`@264; (+1)
- status: **1:1**

### 91. StartupTimeFix

- arm: `StartupTimeFix` default `1`; handler `0x100468d0`; class `function-call-only`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 1, rva-installs 0
- status: **1:1 (match-literals)**
- note: Three 4-byte literal writes into the startup timer code (rows in recovered_match_literals.inc).

### 92. SuperLODFix

- arm: `SuperLODFix` default `1`; handler `0x10046a10`; class `rva-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: 0x300; INI value: `1`
- our rows: op 0, curated 6, jumps 0/0/2, literal stores 0, indirect 0, match-literals 0, rva-installs 4
- curated: slot_installs:``@; slot_installs:``@; slot_installs:``@; slot_installs:``@; slot_installs:``@; slot_installs:``@
- status: **1:1**

### 93. TBoGTHeliHeightLimit

- arm: `TBoGTHeliHeightLimit` default `0`; handler `0x1001b310`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 94. TeleportProtection

- arm: `TeleportProtection` default `1`; handler `0x10037170`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 95. TryToSkipAllErrors

- arm: `TryToSkipAllErrors` default `1`; handler `0x10046b30`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 0/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 96. VRAMFix

- arm: `VRAMFix` default `1`; handler `0x1004aae0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 0, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**

### 97. VSyncFix

- arm: `VSyncFix` default `1`; handler `0x100503b0`; class `signature-patch`; loader op rows 0, loader sig ops 0, resolved-on-1080 `false`
- build gate: none (all builds); INI value: `1`
- our rows: op 0, curated 0, jumps 0/0/0, literal stores 2, indirect 0, match-literals 0, rva-installs 0
- status: **1:1**
- note: Two literal stores put the plugin dword 0x10159BC0 into the game's vsync operand slots; runtime.cpp has a dedicated arm for the resolve-once value.

### 98. WantedLevelCrashFix

- arm: `WantedLevelCrashFix` default `1`; handler `0x100504b0`; class `signature-patch`; loader op rows 0, loader sig ops 1, resolved-on-1080 `true`
- build gate: none (all builds); INI value: `1`
- our rows: op 1, curated 1, jumps 1/0/0, literal stores 0, indirect 0, match-literals 0, rva-installs 0
- curated: slot_installs:`8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 00 00 00`@
- status: **1:1 (exact shim)**
- note: Exact body; in-shim op row withheld (shim_withheld).


## 3. Historical divergences and remaining boundaries

This section was an earlier audit snapshot. The current hook-body status is the
generated `PARITY.md`/`docs/parity/ledger.json`: 107/107 PASS, no SANCTIONED or
UNPROVEN body rows, and 0 helper-level open rows. Specific corrections since
that snapshot:

1. The fault-avoidance guards called out for ForceCarHeadlightShadows,
   AddOptionsInMenu, GarageModelFix and OutOfCommissionFix were removed; the
   original call/dereference/fault order is preserved and covered by fault
   probes where reachable.
2. The MP model-name catalog is embedded with all 38,455 names in original
   order. `extract_model_catalog.py --check` verifies the catalog against the
   oracle, and the port returns the original `**UNKNOWN**` sentinel only after
   the same table search misses.
3. CarParts sub_10007E40 over-application (A5) remains fixed: each row uses
   `ini_option_enabled`, and the standalone EB write is separately gated.
4. Build-only candidate rows (trio fallback, CarDynamic patterns B/C/D/E,
   PopupHeadlightsFlag's EDX twin, and the CarParts `83 FA 04 75 0C` variant)
   remain documented as absent on the available executables; see
   `session8_feature_specs.md` §1 and the generated build-gate ledger.
5. The version guard, double-load MessageBox and default-on PCID deletion retain
   their original behavior; safety alternatives remain candidates in §6.3.
6. In-shim op rows are not installed separately because the exact/transcribed
   shim body already executes those instructions; the body proof is the
   evidence for those sites. Ten op corrections remain in §5.
7. OutOfCommissionFix now mirrors the original decoder cursor, fake-pak vector
   publication/reset and archive-offset writes. Its 163796-byte `finale2.sco`
   payload, Adler-32 trailer `0x41E794AB`, and RPF archive FNV-1a
   `0xB5AF7B10` are checked; its body passes the 8/16-seed differential.
8. Reverted improvements in §1 (MiscFixes split, SkipIntro variant, SkipMenu
   savegame check) remain archived and are not shipped.

The broader fix catalogue still records P14/P15 as layout-dependent fault
effects: the original can overrun into adjacent data/heap bytes that the rebuilt
module layout does not share. This is outside the 107-body PASS ledger, not an
unreported body sanction. The three builds without usable oracle binaries
(1061, 1100, 12043) are likewise unverified.

## 4. Withheld / no-op / excluded

| item | status | evidence |
| --- | --- | --- |
| `CutsceneFixes` | no-op in 7.65 | parity_and_improvements.md §5.2 (its slot is never published) |
| `NewMemorySystem` | excluded | needs a game-vtable reimplementation (§5) |
| `DrivebyFOVFix` | not installed on 1.0.8.0 | the original does not install it on 1080; our unresolved destination is the clean no-op |
| Build-only rows (A4) | withheld | 0 hits on the available builds; listed in session8_feature_specs.md §1 |
| `BuildingAlphaFix` pattern 2 | withheld | crash evidence; parity_and_improvements.md §1/§4 |
| `NewSavesCompatibility` guards | documented, not shipped | see §4 record; faithful port |

## 5. Spot checks and method

- **Curated rows**: every row in `evidence/op_corrections.json` is re-verified textually against the saved handler decompile by `apply_op_corrections.py` (it fails on drift). The 10 `suppressions` each record a mechanically-generated row that disagrees with the handler (mis-split fills, off-by-one displacements, bare operands without their opcode, a CRC NOP that would erase the pattern before the install stage) and name the curated row that replaces it.
- **Hook bodies**: `verify_hook_bodies.py` byte-compares all 62 `@exact` bodies (addresses in the original module normalized) - all PASS this session, including the MouseFix axis/angle bodies and the OutOfCommissionFix branch body. Compiled (INFO) bodies were checked against the original disassembly semantics.
- **Patterns vs the game binary**: the 9 MouseFix patterns and both OutOfCommissionFix patterns were re-scanned against `game_refs/GTAIV.exe` this session: unique hits at the reported VAs (0x8A9E10, 0x8AA190, 0xA84C1F, 0xA7CCF7, 0xA9AB43, 0x8AA1B0, 0xB4D230, 0xB4D3A0, 0xA81B1F; 0xA22E90, 0xB70733), and the write offsets' pre-images match the handler reports (`F7 D8`/`F3 0F 2A C0` at +3/+5, `75 0F` at +8, ...).
- **Gating**: `build_allows_option` + `recovered_build_gates.inc` (positive gates only: DualVehicleHeadlights 0x7E, EpisodicVehicleSupport 0x300, ...); `unknown` build installs nothing; every live install is INI-gated through `ini_option_enabled` (including the RestoreDeathMusic/CarParts cross-option rules).
- **INI**: every loader-read key has an entry in `game_refs/ZolikaPatch.ini` except the internal alias `Patch7RemoveModChecks` (documented); the keys added/re-enabled this session (MouseFix, OutOfCommissionFix) are `=0` with NEW comments.
- **Limitations**: the op-row tables are generated from the saved decompiles, not re-derived from the binary tonight; the audit's `status: 1:1` therefore means "generator-verified against the handler + bodies verified or absent", not an independent re-decompilation of every handler. Options whose handlers build patterns at runtime are annotated in `why` fields with the callback VA + report.

