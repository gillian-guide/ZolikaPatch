# The 18-cell dump matrix

Nine supported images x two cells -- the **full** cell (the shipped INI) and the
**per-option** cell (every key off, one on) -- measured against the reference
tree's stored dumps in `readable/build/p3-final-dumps3`. Generated, not
hand-written:

```
python tools/dump_matrix.py --markdown docs/matrix.md
```

Each cell is its own subprocess. Every number is the scoreboard's comparison
(`tools/parity_report.py`): a branch displacement -- the four bytes after an
`E8`/`E9`, found in either cell or in the pristine image -- is excluded, because
the baseline recorded one stand-in address for every body target and those bytes
cannot be compared. `differing`, `missing` and `extra` count real bytes only.

## Full cell

`bytes` is what our cell writes; the rest is against the baseline. The residual
column names the keys whose per-key cell writes those bytes (a byte can be named
by more than one key, so the counts need not add up to the byte count).

| build | version | image | bytes | differing | missing | extra | residual |
|---:|---|---|---:|---:|---:|---:|---|
| 1 | 1.0.0.0 | `GTAIV_1000.exe` | 0 | 0 | 154 | 0 | missing 154: `MouseFix` 25 (closed), `HighFPSSpeedupFix` 17 (closed), `BuildingAlphaFix` 14 (closed), `RemoveAllWeaponsProtection` 8 (closed), `CarDynamicShadowFix` 7 (open), `IncreasePtrNodes` 7 (closed), `PopupHeadlightsFlag` 7 (closed), `EpisodeSwitchFreezeFix` 5 (closed), `BorderlessWindowed` 4 (closed), `NoLiveryLimit` 4 (closed), `NoPhysicsSleep` 4 (closed), `StartupTimeFix` 4 (closed), `CopSirensFlag` 3 (closed), `MiscFixes` 3 (closed), `TeleportProtection` 3 (closed), `CarPartsShadowFix` 2 (closed), `GarageModelFix` 2 (closed), `ImprovedShaderStreaming` 2 (closed), `IncreaseVehicleModelLimit` 2 (closed), `InfiniteCarBottomDistance` 2 (closed), `MovingPlaneParts` 2 (closed), `MovingSteeringWheelSupport` 2 (closed), `NoImpostorHeightCutoff` 2 (closed), `SavegameFix` 2 (closed), `ShakingEngineBlock` 2 (closed), `SkipLauncher` 2 (open), `AimProjectileCrashFix` 1 (closed), `AllowHashesForIDE` 1 (closed), `BadPlayerModelCrashFix` 1 (closed), `BuildingDynamicShadows` 1 (closed), `CarDecalsFix` 1 (closed), `CollisionDeadlockFix` 1 (closed), `CutsceneFixes` 1 (closed), `DrivebyFOVFix` 1 (closed), `EmissiveLerpFix` 1 (closed), `FastLoading` 1 (closed), `ForceShadowsOnObjects` 1 (closed), `FreezeGunFix` 1 (closed), `GroupHackFix` 1 (closed), `HighFPSBikePhysicsFix` 1 (closed), `IncreaseVehicleStructLimit` 1 (open), `NewSavesCompatibility` 1 (closed), `PathNodeCrashFix` 1 (closed), `PedModelsInCutscenesFix` 1 (closed), `PlaneCrashFix` 1 (closed), `PlayerAsPedComponentsFix` 1 (closed), `RemoveRGSCLoginRequirement` 1 (closed), `ReversingLightFix` 1 (closed), `SCOSignatureFix` 1 (closed), `SkipIntro` 1 (closed), `WantedLevelCrashFix` 1 (closed), partly from open keys |
| 5 | 1.0.4.0 | `GTAIV_1040.exe` | 951 | 0 | 4 | 408 | missing 4: `AimProjectileCrashFix` 1 (open), `IncreaseVehicleStructLimit` 1 (open), `TryToSkipAllErrors` 1 (open), `VRAMFix` 1 (open), all from open keys; extra 408: `SkipLauncher` 399 (open), `CarDynamicShadowFix` 6 (open), `AimProjectileCrashFix` 1 (open), `MPNikoCrashFix` 1 (open), `TryToSkipAllErrors` 1 (open), all from open keys |
| 6 | 1.0.0.4 | `GTAIV_1004.exe` | 724 | 0 | 4 | 184 | missing 4: `AimProjectileCrashFix` 1 (open), `IncreaseVehicleStructLimit` 1 (open), `TryToSkipAllErrors` 1 (open), `VRAMFix` 1 (open), all from open keys; extra 184: `SkipLauncher` 180 (open), `AimProjectileCrashFix` 1 (open), `BlackscreenFix` 1 (open), `MPNikoCrashFix` 1 (open), `TryToSkipAllErrors` 1 (open), all from open keys |
| 7 | 1.0.6.0 | `GTAIV_1060.exe` | 915 | 0 | 2 | 63 | missing 2: `IncreaseVehicleStructLimit` 1 (open), `VRAMFix` 1 (open), all from open keys; extra 63: `SkipLauncher` 63 (open), all from open keys |
| 8 | 1.0.7.0 | `GTAIV_1070.exe` | 935 | 0 | 2 | 63 | missing 2: `IncreaseVehicleStructLimit` 1 (open), `VRAMFix` 1 (open), all from open keys; extra 63: `SkipLauncher` 63 (open), all from open keys |
| 9 | 1.0.8.0 | `GTAIV.exe` | 883 | 0 | 0 | 57 | extra 57: `SkipLauncher` 57 (open), all from open keys |
| 11 | 1.1.1.0 | `EFLC_1110.exe` | 816 | 0 | 3 | 60 | missing 3: `IncreaseVehicleStructLimit` 1 (open), `TryToSkipAllErrors` 1 (open), `VRAMFix` 1 (open), all from open keys; extra 60: `SkipLauncher` 58 (open), `BlackscreenFix` 1 (open), `TryToSkipAllErrors` 1 (open), all from open keys |
| 12 | 1.1.2.0 | `EFLC_1120.exe` | 813 | 0 | 3 | 60 | missing 3: `IncreaseVehicleStructLimit` 1 (open), `TryToSkipAllErrors` 1 (open), `VRAMFix` 1 (open), all from open keys; extra 60: `SkipLauncher` 58 (open), `BlackscreenFix` 1 (open), `TryToSkipAllErrors` 1 (open), all from open keys |
| 13 | 1.1.3.0 | `EFLC_1130.exe` | 787 | 0 | 1 | 48 | missing 1: `IncreaseVehicleStructLimit` 1 (open), all from open keys; extra 48: `SkipLauncher` 48 (open), all from open keys |

## Per-option cell

`closed / 99` is the key scoreboard: a key is closed when its cell has the same
rows as the baseline's, branch displacements aside. `open` names the keys that
are not; `fault` names the cell that died.

| build | version | closed / 99 | open | fault |
|---:|---|---:|---|---|
| 1 | 1.0.0.0 | 93 (isolated) | `MPNikoCrashFix`, `TryToSkipAllErrors`, `CarDynamicShadowFix`, `IncreaseVehicleStructLimit`, `SkipLauncher` | FAULTED at `NoImpostorHeightCutoff` (0xC0000005 ACCESS_VIOLATION) |
| 5 | 1.0.4.0 | 92 | `AimProjectileCrashFix`, `MPNikoCrashFix`, `TryToSkipAllErrors`, `CarDynamicShadowFix`, `IncreaseVehicleStructLimit`, `VRAMFix`, `SkipLauncher` | - |
| 6 | 1.0.0.4 | 92 | `AimProjectileCrashFix`, `BlackscreenFix`, `MPNikoCrashFix`, `TryToSkipAllErrors`, `IncreaseVehicleStructLimit`, `VRAMFix`, `SkipLauncher` | - |
| 7 | 1.0.6.0 | 95 | `IncreaseVehicleStructLimit`, `VRAMFix`, `SkipLauncher`, `Patch7RemoveModChecks` | - |
| 8 | 1.0.7.0 | 95 | `IncreaseVehicleStructLimit`, `VRAMFix`, `SkipLauncher`, `Patch7RemoveModChecks` | - |
| 9 | 1.0.8.0 | 98 | `SkipLauncher` | - |
| 11 | 1.1.1.0 | 94 | `BlackscreenFix`, `TryToSkipAllErrors`, `IncreaseVehicleStructLimit`, `VRAMFix`, `SkipLauncher` | - |
| 12 | 1.1.2.0 | 94 | `BlackscreenFix`, `TryToSkipAllErrors`, `IncreaseVehicleStructLimit`, `VRAMFix`, `SkipLauncher` | - |
| 13 | 1.1.3.0 | 97 | `IncreaseVehicleStructLimit`, `SkipLauncher` | - |

## The fault

`GTAIV_1000.exe` (1.0.0.0) dies in the per-option cell at
`NoImpostorHeightCutoff`, 0xC0000005 ACCESS_VIOLATION: the original's own fault -- an uninitialised vector entry,
write `66 89 06` at the original's `0x100536F1` -- which `docs/parity.md`
records as deliberate and unguarded. The cell wrote 8192 byte(s) before it died, so its rows are not a scoreboard: the
first 48 keys ran, and the 51 after it
were never reached. It is scored one key per subprocess instead, and
`NoImpostorHeightCutoff` is the only key whose cell faults alone -- so no other cell faults on
that build. The isolated cells are not a different measurement: on builds 5
and 9 they are byte-identical to the per-option rows for all 99 keys
(`python tools/dump_matrix.py --selfcheck-isolated --only 5`).

## What the matrix proves

18 cell(s) ran for 9 build(s). A per-option cell that
completes does so over all 99 keys, so any fault inside it would have shown up
as an abort; the one fault recorded is the reproduced original fault, not a
harness failure. Every residual full-cell byte has a key named behind it, and
the per-option column is the scoreboard that says whether that key is done.
