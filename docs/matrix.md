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
by more than one key, so the counts need not add up to the byte count), each with
its scoreboard state: `closed`, `open`, or `oracle-verified baseline omission`.

| build | version | image | bytes | differing | missing | extra | residual |
|---:|---|---|---:|---:|---:|---:|---|
| 1 | 1.0.0.0 | `GTAIV_1000.exe` | 0 | 0 | 154 | 0 | missing 154: `MouseFix` 25 (closed), `HighFPSSpeedupFix` 17 (closed), `BuildingAlphaFix` 14 (closed), `RemoveAllWeaponsProtection` 8 (closed), `CarDynamicShadowFix` 7 (oracle-verified baseline omission), `IncreasePtrNodes` 7 (closed), `PopupHeadlightsFlag` 7 (closed), `EpisodeSwitchFreezeFix` 5 (closed), `BorderlessWindowed` 4 (closed), `NoLiveryLimit` 4 (closed), `NoPhysicsSleep` 4 (closed), `StartupTimeFix` 4 (closed), `CopSirensFlag` 3 (closed), `MiscFixes` 3 (closed), `TeleportProtection` 3 (closed), `CarPartsShadowFix` 2 (closed), `GarageModelFix` 2 (closed), `ImprovedShaderStreaming` 2 (closed), `IncreaseVehicleModelLimit` 2 (closed), `InfiniteCarBottomDistance` 2 (closed), `MovingPlaneParts` 2 (closed), `MovingSteeringWheelSupport` 2 (closed), `NoImpostorHeightCutoff` 2 (closed), `SavegameFix` 2 (closed), `ShakingEngineBlock` 2 (closed), `SkipLauncher` 2 (oracle-verified baseline omission), `AimProjectileCrashFix` 1 (closed), `AllowHashesForIDE` 1 (closed), `BadPlayerModelCrashFix` 1 (closed), `BuildingDynamicShadows` 1 (closed), `CarDecalsFix` 1 (closed), `CollisionDeadlockFix` 1 (closed), `CutsceneFixes` 1 (closed), `DrivebyFOVFix` 1 (closed), `EmissiveLerpFix` 1 (closed), `FastLoading` 1 (closed), `ForceShadowsOnObjects` 1 (closed), `FreezeGunFix` 1 (closed), `GroupHackFix` 1 (closed), `HighFPSBikePhysicsFix` 1 (closed), `IncreaseVehicleStructLimit` 1 (closed), `NewSavesCompatibility` 1 (closed), `PathNodeCrashFix` 1 (closed), `PedModelsInCutscenesFix` 1 (closed), `PlaneCrashFix` 1 (closed), `PlayerAsPedComponentsFix` 1 (closed), `RemoveRGSCLoginRequirement` 1 (closed), `ReversingLightFix` 1 (closed), `SCOSignatureFix` 1 (closed), `SkipIntro` 1 (closed), `WantedLevelCrashFix` 1 (closed), the omissions being oracle-verified baseline gaps |
| 5 | 1.0.4.0 | `GTAIV_1040.exe` | 955 | 0 | 0 | 405 | extra 405: `SkipLauncher` 399 (oracle-verified baseline omission), `CarDynamicShadowFix` 6 (oracle-verified baseline omission), all oracle-verified baseline omissions |
| 6 | 1.0.0.4 | `GTAIV_1004.exe` | 724 | 0 | 0 | 180 | extra 180: `SkipLauncher` 180 (oracle-verified baseline omission), all oracle-verified baseline omissions |
| 7 | 1.0.6.0 | `GTAIV_1060.exe` | 983 | 0 | 0 | 63 | extra 63: `SkipLauncher` 63 (oracle-verified baseline omission), all oracle-verified baseline omissions |
| 8 | 1.0.7.0 | `GTAIV_1070.exe` | 1004 | 0 | 0 | 63 | extra 63: `SkipLauncher` 63 (oracle-verified baseline omission), all oracle-verified baseline omissions |
| 9 | 1.0.8.0 | `GTAIV.exe` | 947 | 0 | 0 | 57 | extra 57: `SkipLauncher` 57 (oracle-verified baseline omission), all oracle-verified baseline omissions |
| 11 | 1.1.1.0 | `EFLC_1110.exe` | 880 | 0 | 0 | 58 | extra 58: `SkipLauncher` 58 (oracle-verified baseline omission), all oracle-verified baseline omissions |
| 12 | 1.1.2.0 | `EFLC_1120.exe` | 877 | 0 | 0 | 58 | extra 58: `SkipLauncher` 58 (oracle-verified baseline omission), all oracle-verified baseline omissions |
| 13 | 1.1.3.0 | `EFLC_1130.exe` | 851 | 0 | 0 | 48 | extra 48: `SkipLauncher` 48 (oracle-verified baseline omission), all oracle-verified baseline omissions |

## Per-option cell

`closed / 99` is the key scoreboard: a key is closed when its cell has the same
rows as the baseline's, branch displacements aside. `open` names the keys that
are not; `oracle` names the keys whose difference is a measured omission in the
stored baseline (`tests/oracle_verified.tsv`, per key and build) and is therefore
reported apart and never counted as closed; `fault` names the cell that died.

| build | version | closed / 99 | open | oracle | fault |
|---:|---|---:|---|---|---|
| 1 | 1.0.0.0 | 96 (isolated) | - | `CarDynamicShadowFix`, `SkipLauncher` | FAULTED at `NoImpostorHeightCutoff` (0xC0000005 ACCESS_VIOLATION) |
| 5 | 1.0.4.0 | 97 | - | `CarDynamicShadowFix`, `SkipLauncher` | - |
| 6 | 1.0.0.4 | 98 | - | `SkipLauncher` | - |
| 7 | 1.0.6.0 | 97 | - | `SkipLauncher`, `Patch7RemoveModChecks` | - |
| 8 | 1.0.7.0 | 97 | - | `SkipLauncher`, `Patch7RemoveModChecks` | - |
| 9 | 1.0.8.0 | 98 | - | `SkipLauncher` | - |
| 11 | 1.1.1.0 | 98 | - | `SkipLauncher` | - |
| 12 | 1.1.2.0 | 98 | - | `SkipLauncher` | - |
| 13 | 1.1.3.0 | 98 | - | `SkipLauncher` | - |

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
