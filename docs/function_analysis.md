# ZolikaPatch function analysis

## Scope and method

This analysis is based on Ghidra's function index at `ZolikaPatch_decompiled/ghidra/functions.csv` and representative code from all five `decompiled_000*.c` chunks. The path named in the request resolves under the `ghidra/` subdirectory; no ZIP archive was opened or modified.

The sample emphasized executable feature code in chunks 0–1, support/template code in chunk 2, DLL/CRT startup in chunk 3, and exception-unwind fragments in chunks 3–4. Names beginning with `FUN_` are Ghidra placeholders, so proposed names below describe observed behavior rather than recovered source symbols.

### Inventory

- **995 function records**, spanning `0x10001020`–`0x10062ED7`.
- Chunks contain 200, 200, 200, 200, and 195 records respectively.
- **994 decompiled successfully**; only `FUN_10024600` timed out.
- **35 import thunks** are marked explicitly.
- Approximate index composition: 595 unlabeled `FUN_*` records, 326 `Catch@`/`Unwind@` fragments, 35 import thunks, and a small set of recognized C++/CRT routines. Consequently, raw function count substantially overstates the amount of product logic.
- The binary is a **32-bit Windows DLL/ASI plugin**, compiled with MSVC and using C++ exceptions, STL strings/containers, Win32 APIs, and runtime code recognizable as Visual Studio 2019.

## Architectural picture

The plugin is primarily a configurable binary-patching layer for GTA IV/EFLC. Its recurring mechanism is:

1. Determine the executable/build and detect companion modules.
2. Read `ZolikaPatch.ini` into feature flags and data tables.
3. Scan the main executable or another loaded module for byte signatures.
4. Temporarily make code writable with `VirtualProtect`.
5. Replace instructions with jumps/calls, immediate values, NOPs, or early returns.
6. Route selected game behavior through small replacement hooks and compatibility adapters.

This is strong evidence that a reconstruction should preserve **behavioral feature boundaries and signature-based resolution**, not attempt a line-for-line translation of the decompiler output.

## Subsystems and important functions

Confidence means confidence in the stated role, not in the original source-level function name or exact prototype.

### 1. DLL startup, build identification, and bootstrap

| Address | Proposed name / known name | Evidence | Confidence |
|---|---|---|---|
| `0x1006075D` | `entry` | Initializes the security cookie on process attach and enters `dllmain_dispatch`. Recognized CRT routine. | High |
| `0x10060627` | `dllmain_dispatch` | Runs CRT/raw DLL initialization and calls `FUN_10055C70` for each DLL reason. | High |
| `0x10055C70` | `ZPatchDllMain` / bootstrap | On process attach checks `dsound.dll`, obtains the game version, maps known build numbers, detects `CitizenIV.exe`, then calls `FUN_100523C0`. | High |
| `0x10003B80` | `GetGameBuildNumber` | Reads the host EXE version resource with `GetFileVersionInfo*`/`VerQueryValueA` and constructs a numeric version/build result. | High |
| `0x10003A10` | `IsPlayGTAIVRunning` | Enumerates processes with Toolhelp and compares names to `PlayGTAIV.exe`. | High |
| `0x10003E20` | `CheckFusionFixCompatibility` | Detects `GTAIV.EFLC.FusionFix.asi` and checks fixed offsets/values in that module. Exact version predicate needs validation. | Medium–high |

`FUN_10055C70` recognizes multiple executable build values (including 1000, 1004, 1040, 1060, 1070, 1080, 1100 and 1200-series values) and stores an internal build enum in `DAT_1015BE04`. Old/unsupported versions trigger the project downgrade URL and process termination. This version table should be reconstructed explicitly and covered by tests before any patches are enabled.

### 2. Configuration and feature orchestration

| Address | Proposed name | Evidence | Confidence |
|---|---|---|---|
| `0x100523C0` | `LoadConfigAndInstallPatches` | A very large orchestrator (~64 KiB decompiled text) opens `ZolikaPatch.ini`, reads tables/options, detects loaders, and conditionally invokes patch routines. | High |
| `0x10056130` | `IniDocument::load` | Builds a UTF-8/UTF-16 conversion and stream/parser state plus a map-like section index. | Medium–high |
| `0x10055F10` | `IniDocument::selectSection` | Looks up a string in a tree/map and records the selected section/value pointer. | Medium |
| `0x100522B0` | `AddIVMenuToggle` | Calls exported `AddIVMenuOption` from `IVMenuAPI.asi` with `MENU_DISPLAY_ON_OFF`. | High |
| `0x10052380` | `EnsureIVMenuAPILoaded` | Finds or conditionally loads `IVMenuAPI.asi`, with a one-attempt guard. | High |
| `0x10052300` | `ValidateZMenuVersion` | Calls `GetZMenuVersion` from `ZMenuIV.asi`; exits for versions below `0x35DD1`. | High |

The orchestrator exposes the actual product decomposition through INI keys. Major groups include:

- **Stability/crash/deadlock:** `PathNodeCrashFix`, `CollisionDeadlockFix`, `AimProjectileCrashFix`, `FreezeCarFix`, `BadPlayerModelCrashFix`, `MPNikoCrashFix`, `WantedLevelCrashFix`, `PlaneCrashFix`, `CrashMsgFix`, `FreezeGunFix`, and `GroupHackFix`.
- **Timing/physics/high FPS:** `NoPhysicsSleep`, `HighFPSBikePhysicsFix`, `HighFPSSpeedupFix`, `SpeedupBugFix`, `NoLoadingSleep`, `StartupTimeFix`, and `BenchmarkFix`.
- **Rendering/lighting/shadows:** `NoImpostorHeightCutoff`, `NoLODLightHeightCutoff`, `BuildingAlphaFix`, `BuildingDynamicShadows`, `ForceDynamicShadowsEverywhere`, `InfiniteCarBottomDistance`, `RemoveStaticCarShadows`, `RemoveStaticCutsceneCarShadows`, `EmissiveLerpFix`, `ForceShadowsOnObjects`, `HighQualityReflections`, `RemoveBoundingBoxCulling`, `SuperLODFix`, `VSyncFix`, and `ImprovedShaderStreaming`.
- **Vehicles/weapons/episodes:** `PoliceEpisodicWeaponSupport`, `PipeBombDropIconSupport`, `LoadDLCs`, `IncreaseVehicleModelLimit`, `IncreaseVehicleStructLimit`, `MovingSteeringWheelsSupport`, `MovingPlaneParts`, `EpisodicVehicleSupport`, `EpisodicWeaponSupport`, and `TBoGTHeliHeightLimit`.
- **Input/camera/window/UI:** `DrivebyFOVFix`, `MouseFix`, `DoNotPauseOnMinimize`, `BorderlessWindowed`, `SkipIntro`, `SkipMenu`, and `AddOptionsInMenu`.
- **Launcher/platform/mod compatibility:** `RemoveRGSCLoginRequirement`, `Patch6and7RemoveModChecks`, `Patch7RemoveModChecks`, `Patch4RemoveModChecks`, `RemoveGFWLUpdatePopup`, `SkipLauncher`, `LaunchWithPlayGTAIV`, and `VRAMFix`.
- **Files/saves/content:** `OverrideGTADatPath`, `OverrideGTADatPathTo`, `SavegameFix`, `NewFilesCompatibility`, `NewSavesCompatibility`, `LocalSettingsPath`, and `RestoreDeathMusic`.
- **Multiplayer/security/synchronization:** `BetterMPSync`, `FixPCIDKickbug`, `TeleportProtection`, `RemoveAllWeaponsProtection`, `SCOSignatureFix`, and `PlayerAsPedComponentsFix`.

Several keys are stored reversed or assembled dynamically (for example `gnidaoLtsaF`, `sthgildaeHelciheVlauD`, and `xiFthgiLgnisreveR`). These should not be “corrected” blindly: reconstruct the string transformation first and test it against an actual INI.

Two data-driven sections are especially clear:

- `PopupLightAngles`: up to `0x200` entries with `Name` and floating-point `Angle`, names hashed through `FUN_10003E50`.
- `HeadlightConeOffsets`: similarly indexed names and floating-point offsets.

### 3. Pattern scanning and patch application

| Address | Proposed name | Evidence | Confidence |
|---|---|---|---|
| `0x10003830` | `FindFirstPattern` | Delegates to `FUN_1005AFA0`, returning the first address in the resulting vector or zero. | High |
| `0x1005AFA0` | `FindPatternsInModule` | Parses a textual signature, uses `K32GetModuleInformation`, scans module memory, and returns a vector of matches. | High |
| `0x10003290` | `ScanMemoryForPattern` | Called by `FUN_1005AFA0` with parsed pattern, module base, image size, and a first/all-match flag. | Medium–high |
| `0x10004850` | `InstallFileOrArchiveCompatibilityPatches` | Large guarded patch set, skipped for `GTAC.dll` and selected builds; rewrites multiple call/jump sites and file-related paths. Exact feature label remains uncertain. | Medium |
| `0x10004620` | patch installer for two game sites | One-time guarded signature scans that install a call and a jump hook. Exact feature unknown. | Medium–low |
| `0x100241E0` | runtime instruction toggle | Rewrites three instruction ranges based on two configured numeric values. | Medium |

The decompiled code repeatedly emits `E8`/`E9`, relative displacements, NOP runs, conditional-branch changes, `RET`, and immediate constants under `VirtualProtect(PAGE_EXECUTE_READWRITE)`. A reconstruction should centralize these operations. It should also call `FlushInstructionCache`; that is not apparent in the sampled output but is advisable for a robust implementation.

### 4. Game model hashing, lookup, and spawn diagnostics

| Address | Proposed name | Evidence | Confidence |
|---|---|---|---|
| `0x10003E50` | `rageHash` / `HashGameName` | Accepts `hash:` numeric syntax; otherwise lowercases ASCII, normalizes `\` to `/`, and applies the observed RAGE-style hash mixing. | High |
| `0x10003F00` | `ModelHashToName` | Linear search over `0x9637` model-name strings and returns `**UNKNOWN**` if absent. | High |
| `0x10037510` | `ValidateAndLogSpawnedModel` | Resolves a model, verifies car/heli/boat/bike/train/plane/ped/object type, and produces `GTAC.dll` diagnostics such as “spawned a non-car car”. | High |
| `0x10037AC0` | `RepairBadPlayerModel` | Detects a non-ped player model, logs through `GTAC.dll`, requests `m_y_multiplayer`, and substitutes its index. | High |
| `0x10005F70` | `IsCurrentModelPoliceVehicle` | Compares against police/noose/stockade/patriot hashes and an additional configured list. | High |

The array access in `FUN_10037510` iterates as many as 31,000 model slots, which likely relates to increased model limits. Reconstructing typed game structures and pool accessors is critical; the raw decompiler’s offsets (`+0x3C`, `+0x6C`, and virtual call offsets) should remain version-specific until verified.

### 5. Vehicle animation and per-frame fixes

| Address | Proposed name | Evidence | Confidence |
|---|---|---|---|
| `0x10004C70` | `UpdateVehicleMovingParts` | Updates bones named `steeringwheel` and `engineblock`; also drives multiple plane-control channels when the vehicle subtype is 5. | High |
| `0x10003FA0` | steering-wheel angle helper | Contains the diagnostic/string `wheelangle:` and participates in vehicle/bone processing. Exact contract is unclear. | Medium |
| `0x10024060`, `0x100240C0`, `0x10024120`, `0x10024170` | paired-value scaling hooks | Select one of two global values and convert/scale it for hook consumers. Likely part of a rendering or input feature. | Medium–low |

This area contains dense accesses to reverse-engineered game objects. Implement it only after defining minimal typed wrappers for vehicle/model/skeleton data and validating offsets for each supported executable build.

### 6. Episode/DLC and save-path compatibility

| Address | Proposed name | Evidence | Confidence |
|---|---|---|---|
| `0x10023150` | `DiscoverAndRegisterEpisodeContent` | Searches `\dlc\tlad\content\`, `\TLAD\`, `\dlc\TBoGT\content\`, `\TBoGT\`, then `\Episode%d\` through episode 254 and registers existing paths. | High |
| `0x10050D10` | `BuildEpisodeSavePaths` | Selects `GTA IV`, `TLaD`, or `TBoGT` and constructs narrow/wide save-directory variants under `\saves\`. | High |
| `0x10051920` | compatibility patch around resource/file processing | Avoids duplicate behavior with `GTAC.dll` and `JacksIVFixes.asi`, then installs three signature-based hooks. Exact fix name is unresolved. | Medium |

This subsystem should use `std::filesystem` internally but preserve the original alternate directory search order and ANSI/wide outputs at game API boundaries.

### 7. Window/message and borderless behavior

| Address | Proposed name | Evidence | Confidence |
|---|---|---|---|
| `0x10004300` | `MessageWindowThread` | Registers/uses `MessageWindow` or `MessageWindow2`, runs a normal `GetMessage` loop, and unregisters the class. | High |
| nearby hook routines around `0x100057xx` | borderless window hooks | Call `SetWindowPos`, clear style/ex-style bits with `Get/SetWindowLongA`, then forward to `DefWindowProcA/W`. | Medium–high |

Keep Win32 window subclassing and patch installation separate. Support both ANSI and Unicode window procedures as the original does.

### 8. Compatibility integrations and duplicate-load defense

The configuration/bootstrap code explicitly detects or integrates with:

- `GTAC.dll`
- `GTAIV.EFLC.FusionFix.asi`
- `JacksIVFixes.asi`
- `IVMenuAPI.asi`
- `ZMenuIV.asi`
- `XLivelessAddon.asi` / `xlive.dll`
- legacy `dsound.dll`
- `CitizenIV.exe`

`FUN_100523C0` also reports a fatal “ZPatch was loaded twice” error, identifying simultaneous ASI loaders as a likely cause. Compatibility checks are not incidental: they gate several patches and must be represented explicitly in the reconstructed feature dependency/conflict model.

### 9. Support code, CRT, and decompiler noise

- Chunk 2 (`0x10058090` onward) is dominated by STL strings, streams, maps, futures, pattern-scanner support, RTTI, and allocation helpers. Reuse the C++ standard library rather than translating these functions.
- Chunk 3 contains recognized CRT initialization, `find_pe_section` (`0x100600C9`), security-cookie setup, DLL dispatch, import thunks, and the beginning of exception fragments.
- Most of chunk 4 is `Unwind@...` metadata/code fragments. These are compiler products, not independent reconstruction units.
- `FUN_10024600` is the sole decompilation timeout. Its body is absent from the chunks and needs targeted re-decompilation/disassembly before deciding whether it contains feature logic.

## Recommended reconstruction plan

### Phase 1: safe core

1. Create a 32-bit Windows ASI DLL target with a very small `DllMain`.
2. Move real initialization to a worker/bootstrap path to avoid loader-lock hazards. The original performs file I/O, module loading, message boxes, `system`, and extensive patching from process attach; reproducing that literally is risky.
3. Implement structured logging, module inventory, executable version/resource parsing, and an explicit `GameBuild` enum.
4. Reimplement `HashGameName` and pattern parsing/scanning first; add golden-vector tests from observed strings and byte patterns.

### Phase 2: declarative patch engine

Define reusable primitives such as:

- `Pattern { bytes, wildcard_mask }`
- `ModuleRange { base, image_size }`
- `PatchSite { build, module, pattern, match_index, offset, expected_bytes }`
- patch operations: `write`, `nop`, `replace_call`, `replace_jump`, `write_pointer`, and `write_immediate`
- RAII page-protection restoration plus instruction-cache flushing

Require exactly the expected number of matches and validate original bytes before writing. Record every applied/skipped/failed patch with its feature and build. Never treat “pattern not found” as permission to patch an offset derived from null.

### Phase 3: typed configuration and feature registry

Split `FUN_100523C0` into:

- INI parsing and defaults
- compatibility/module detection
- build capability selection
- one installer per feature
- runtime hooks/state
- optional menu registration

Give each feature metadata: supported builds, prerequisite symbols, conflicting modules/features, default value, hot-toggle safety, and failure policy. Preserve custom sections such as popup-light angles as typed arrays with strict bounds.

### Phase 4: game-facing types and hooks

Reconstruct only the fields each feature needs. Store offsets in per-build layouts rather than one giant guessed class. Use wrapper APIs for model pool lookup, model typing, skeleton/bone access, episode state, and save paths. Verify calling conventions (`__cdecl`, `__stdcall`, `__thiscall`, register-based hooks) against assembly before implementing each trampoline.

### Phase 5: feature migration order

A low-risk order is:

1. Path construction, build detection, companion-module checks, and menu exports.
2. Simple byte patches with unambiguous signatures and expected-byte checks.
3. Save/DLC compatibility and isolated call replacements.
4. Window/input fixes.
5. Rendering/timing fixes.
6. Vehicle/model/game-object hooks.
7. Multiplayer/security behavior last, because incorrect pool/type assumptions can corrupt shared state.

## Specific cautions

- **Do not encode current absolute addresses as game addresses.** `0x100...` values in this document identify functions in the plugin image; game patch sites are discovered dynamically by signatures.
- **Treat decompiled prototypes as provisional.** Many hook stubs appear as `UNK_...`, indirect calls have lost types, and some apparent floating-point parameters are artifacts of register/stack recovery.
- **Preserve patch atomicity.** Suspend or otherwise coordinate with game threads when modifying multi-byte live code; a sequence of individual `VirtualProtect`/stores is not inherently safe.
- **Keep build-specific signatures together.** The bootstrap’s internal build enum and numerous conditionals show that one signature set does not cover every GTA IV/EFLC executable.
- **Separate compatibility policy from feature code.** Existing checks for GTAC, FusionFix, JacksIVFixes, loaders, and menu plugins are essential to avoiding duplicate hooks.
- **Do not reconstruct CRT/STL internals.** Replace recognized library bodies and unwind fragments with compiler-generated equivalents.
- **Resolve the timeout.** Re-run Ghidra on `0x10024600` with a larger timeout or inspect its callers and assembly; flag it as unknown until then.

## Suggested recovered-name map

These names are suitable as an initial symbol map while retaining original addresses in comments or metadata:

```text
10003290  ScanMemoryForPattern
10003830  FindFirstPattern
10003A10  IsPlayGTAIVRunning
10003B80  GetGameBuildNumber
10003E20  CheckFusionFixCompatibility
10003E50  HashGameName
10003F00  ModelHashToName
10004300  MessageWindowThread
10004C70  UpdateVehicleMovingParts
10005F70  IsCurrentModelPoliceVehicle
10023150  DiscoverAndRegisterEpisodeContent
10037510  ValidateAndLogSpawnedModel
10037AC0  RepairBadPlayerModel
10050D10  BuildEpisodeSavePaths
10051920  InstallResourceCompatibilityHooks   (provisional)
100522B0  AddIVMenuToggle
10052300  ValidateZMenuVersion
10052380  EnsureIVMenuAPILoaded
100523C0  LoadConfigAndInstallPatches
10055C70  ZPatchDllMain
10055F10  IniDocument_SelectSection            (provisional)
10056130  IniDocument_Load                     (provisional)
1005AFA0  FindPatternsInModule
10060627  dllmain_dispatch
1006075D  entry
```

## Verification targets for future work

- Hash outputs for representative model/path strings and `hash:` literals.
- Exact version-to-`GameBuild` mapping from `FUN_10055C70`.
- Pattern uniqueness and expected bytes for every supported executable.
- Calling conventions and overwritten instruction lengths for all `UNK_...` trampolines.
- INI defaults, dynamically reversed/assembled keys, and behavior when sections/values are absent or malformed.
- Model-pool limits and object layout offsets per build.
- Save/DLC search precedence for base game, TLAD, TBoGT, and numbered episodes.
- Compatibility behavior when each known companion module is present.
