# ZolikaPatch v7.65 x FusionFix cross-reference (feature set 2)

Read-only static cross-reference. No ZIP, extracted original, or game binary was modified.

## Provenance

| Item | Value |
|---|---|
| ZolikaPatch artifact | `ZolikaPatch_extracted/ZolikaPatch.asi` (sha256 `720fbceb...02fc5`) |
| ZolikaPatch evidence | `ZolikaPatch_reconstruction/evidence/*.json` (read-only) |
| ZolikaPatch decompiled | `ZolikaPatch_decompiled/ghidra/decompiled_*.c`, `checkpoints/option_handlers/` |
| Game binary | `/Users/kreato/Downloads/GTAIV.exe`, sha256 `3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f`, image base `0x400000`, 1.0.8.0 (build 9) |
| FusionFix | `https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix` @ `bea25bd848cf` (`2026-10-04`) |
| FusionFix working copy | `/tmp/FusionFix/source` (read-only) |

## Method

- Imported tools/verify_game_signatures.py; scanned GTAIV.exe 1.0.8.0 .text with load_pe/parse_signature/find_all.
- Reconstructed the ZolikaPatch operation map from evidence/patch_operations_all.json + option_handlers.json, applying the dispatcher-proven handler correction below.
- Extracted quoted pattern literals and the following pattern.get_first(N) call from FusionFix sources; FusionFix patch address = signature_start + N.
- Cross-referenced each ZolikaPatch patch address against FusionFix patch addresses: exact first, then delta<=7 (used when a get_first offset lands past a preceding instruction of the same site).
- Validated byte-level equality at exact-match sites directly against the game image.

### Scope limits

- Static only: neither ASI was executed.
- FusionFix targets the Complete Edition (1.2.0.x). Its patterns were re-resolved against the local 1.0.8.0 image; a FusionFix pattern that does not resolve here indicates a build difference, not a defect.
- "Substring containment" of two signature literals is deliberately not counted as a match: generic `83 3D ......` prefixes produce many spurious hits.

## Episodic handler correction (evidence-verified)

`evidence/option_handlers.json` and `option_signatures.json` fold both episodic handlers into `EpisodicVehicleSupport` and leave `EpisodicWeaponSupport` empty. The dispatcher in `ZolikaPatch_decompiled/ghidra/decompiled_0001.c` proves otherwise:

- line 18537: `"EpisodicVehicleSupport","1"` -> `FUN_1001b580()` (21 patterns)
- line 18545: `"EpisodicWeaponSupport","1"` -> `FUN_1001b380()` (8 patterns)

The union of the two handler signature sets equals the recorded `EpisodicVehicleSupport` list exactly, with zero overlap, so the split is unambiguous. Confidence: high.

## Summary table

| ZolikaPatch option | ZP handlers | ZP ops | ZP ops sig absent in 1.0.8.0 | Ops with a FusionFix site | exact addr | near addr (<=7B) | ZP handler patterns | handler patterns with FF site |
|---|---|---|---|---|---|---|---|---|
| EpisodicVehicleSupport | 0x1001b580 | 25 | 0 | 14 | 7 | 7 | 23 | 11 |
| EpisodicWeaponSupport | 0x1001b380 | 8 | 0 | 8 | 7 | 1 | 8 | 8 |
| PoliceEpisodicWeaponSupport | 0x1001bec0 | 1 | 0 | 1 | 1 | 0 | 1 | 1 |
| TBoGTHeliHeightLimit | 0x1001b310 | 1 | 0 | 1 | 1 | 0 | 1 | 1 |
| PipeBombDropIconSupport | 0x1001be40 | 1 | 0 | 1 | 0 | 1 | 1 | 1 |
| EpisodeSwitchFreezeFix | 0x1001b0e0 | 5 | 0 | 0 | 0 | 0 | 5 | 0 |
| CombinedRadioFreezeFix | 0x10005ee0 | 1 | 0 | 0 | 0 | 0 | 1 | 0 |
| RemoveRGSCLoginRequirement | 0x10045490 | 3 | 2 | 0 | 0 | 0 | 1 | 0 |
| NoLiveryLimit | 0x1003d420 | 2 | 1 | 1 | 1 | 1 | 3 | 2 |
| PopupHeadlightsFlag | 0x10044590 | 2 | 2 | 0 | 0 | 0 | 2 | 0 |
| CopSirensFlag | 0x10006350 | 2 | 0 | 0 | 0 | 0 | 2 | 0 |
| VSyncFix | 0x100503b0 | 0 | 0 | 0 | 0 | 0 | 2 | 0 |
| VRAMFix | 0x1004aae0 | 1 | 1 | 0 | 0 | 0 | 1 | 0 |
| FastLoading | 0x1001eca0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| NoLoadingSleep | 0x1003d670 | 0 | 0 | 0 | 0 | 0 | 1 | 1 |
| BenchmarkFix | 0x10005690 | 0 | 0 | 0 | 0 | 0 | 1 | 0 |
| MiscFixes | 0x10023dd0 | 11 | 5 | 0 | 0 | 0 | 11 | 0 |
| LoadDLCs | 0x100234e0 | 17 | 0 | 0 | 0 | 0 | 0 | 0 |

## Per-feature findings and confidence

### EpisodicVehicleSupport

- ZolikaPatch handlers: 0x1001b580; build gates: [["==", 8], ["==", 9]]
- Confidence: high - 23/23 handler patterns resolve; 11 have a FusionFix site (7 exact). Same "cmp [episode],2 / jl" idiom and identical MakeNOP offsets.
- FusionFix sites reached:

  | ZP site | delta | FusionFix file:line | FusionFix statement |
  |---|---|---|---|
  | `0xa530fd` | +7 | `episodiccontent.ixx:46` | `injector::MakeNOP(pattern.get_first(7), 6, true);` |
  | `0x8fc61f` | +7 | `episodiccontent.ixx:62` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0x835771` | +7 | `episodiccontent.ixx:106` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0x965823` | +7 | `episodiccontent.ixx:83` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0xaa704d` | +7 | `episodiccontent.ixx:54` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0x80304a` | +0 | `episodiccontent.ixx:78` | `injector::MakeNOP(pattern.get_first(21), 2, true);` |
  | `0x801039` | +0 | `episodiccontent.ixx:70` | `injector::MakeNOP(pattern.get_first(8), 2, true);` |
  | `0x80127e` | +0 | `episodiccontent.ixx:143` | `injector::MakeNOP(pattern.get_first(11), 6, true);` |
  | `0x850f0a` | +0 | `episodiccontent.ixx:114` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0x9d2245` | +0 | `episodiccontent.ixx:122` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0x9d22f0` | +0 | `episodiccontent.ixx:130` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0xd49b0f` | +0 | `episodiccontent.ixx:507` | `injector::MakeNOP(pattern.get_first(6), 2, true);` |

### EpisodicWeaponSupport

- ZolikaPatch handlers: 0x1001b380; build gates: null
- Confidence: high - 8/8 handler patterns resolve and all 8 have a FusionFix site (7 exact), matching FusionFix EPISODICCONTENT/EpisodicWeapons.
- FusionFix sites reached:

  | ZP site | delta | FusionFix file:line | FusionFix statement |
  |---|---|---|---|
  | `0x9d25d5` | +7 | `episodiccontent.ixx:237` | `injector::MakeNOP(pattern.get_first(7), 6, true);` |
  | `0x4a9d41` | +0 | `episodiccontent.ixx:253` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0x4ab7c7` | +0 | `episodiccontent.ixx:261` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0x99776f` | +0 | `episodiccontent.ixx:285` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0xa61410` | +0 | `episodiccontent.ixx:293` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0xaae8e9` | +0 | `episodiccontent.ixx:301` | `injector::MakeNOP(pattern.get_first(7), 6, true);` |
  | `0x4a6f25` | +0 | `episodiccontent.ixx:277` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |
  | `0x9900ad` | +0 | `episodiccontent.ixx:245` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |

### PoliceEpisodicWeaponSupport

- ZolikaPatch handlers: 0x1001bec0; build gates: null
- Confidence: high - single pattern 39 2D ? ? ? ? 75 3E resolves at 0x49f18c; FusionFix TBoGTPoliceWeapons uses the identical fallback pattern and NOP at +6.
- FusionFix sites reached:

  | ZP site | delta | FusionFix file:line | FusionFix statement |
  |---|---|---|---|
  | `0x49f192` | +0 | `episodiccontent.ixx:587` | `injector::MakeNOP(pattern.get_first(6), 2, true);` |

### TBoGTHeliHeightLimit

- ZolikaPatch handlers: 0x1001b310; build gates: null
- Confidence: high - pattern 83 3D ? ? ? ? 02 7C 0A F3 0F resolves at 0x8f0071; FusionFix TBoGTHelicopterHeightLimit uses the same pattern and NOP at +7.
- FusionFix sites reached:

  | ZP site | delta | FusionFix file:line | FusionFix statement |
  |---|---|---|---|
  | `0x8f0078` | +0 | `episodiccontent.ixx:577` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |

### PipeBombDropIconSupport

- ZolikaPatch handlers: 0x1001be40; build gates: null
- Confidence: medium-high - FusionFix EpisodicWeapons "Pipe bomb drop hud" NOPs at +7 of 83 3D ? ? ? ? 74 1A 83 7D 18 1C; ZolikaPatch NOPs 9 bytes before 83 7D 18 1C 75 14, i.e. the same instruction pair, reached from the opposite side.
- FusionFix sites reached:

  | ZP site | delta | FusionFix file:line | FusionFix statement |
  |---|---|---|---|
  | `0x4a6f47` | +7 | `episodiccontent.ixx:269` | `injector::MakeNOP(pattern.get_first(7), 2, true);` |

### EpisodeSwitchFreezeFix

- ZolikaPatch handlers: 0x1001b0e0; build gates: null
- Confidence: unresolved - none of the 5 patterns or jump targets match a FusionFix patch site; FusionFix does not ship an equivalent option.
- No FusionFix patch site found at or near any ZolikaPatch site.

### CombinedRadioFreezeFix

- ZolikaPatch handlers: 0x10005ee0; build gates: null
- Confidence: unresolved - single call-site pattern has no FusionFix counterpart; the nearest FusionFix radio fix (fixes.ixx "streamed radio stations resetting") is at a different address (0x7f144d vs 0x895f59).
- No FusionFix patch site found at or near any ZolikaPatch site.

### RemoveRGSCLoginRequirement

- ZolikaPatch handlers: 0x10045490; build gates: [["==", 9]]
- Confidence: unresolved - the signature is absent from 1.0.8.0 (build-gated to build 9, and ZolikaPatch falls back to a module-relative RVA 0x498460). No FusionFix site.
- No FusionFix patch site found at or near any ZolikaPatch site.

### NoLiveryLimit

- ZolikaPatch handlers: 0x1003d420; build gates: null
- Confidence: medium-high - one ZolikaPatch pattern (8B 8C 83 3C 01 00 00 0F BF 53 48) matches FusionFix limits.ixx LiveryAccessHook3 exactly (same address 0xb97a29, same bytes). Other ZolikaPatch livery patterns are build-8 fallbacks not present in 1.0.8.0.
- FusionFix sites reached:

  | ZP site | delta | FusionFix file:line | FusionFix statement |
  |---|---|---|---|
  | `0xb97a29` | +0 | `limits.ixx:362` | `}; injector::MakeInline<LiveryAccessHook3>(pattern.get_first(0), pattern.get_first(7));` |
  | `0xa4a53b` | +2 | `limits.ixx:279` | `}; injector::MakeInline<CVehicleModelInfoSetVehicleDrawableHook>(pattern.get_first(0), pattern.get_first(6));` |

### PopupHeadlightsFlag

- ZolikaPatch handlers: 0x10044590; build gates: null
- Confidence: unresolved - the two patterns are build-specific and absent from 1.0.8.0. FusionFix has no popup-headlight vehicles.ide flag option (it reads no popuplights token).
- No FusionFix patch site found at or near any ZolikaPatch site.

### CopSirensFlag

- ZolikaPatch handlers: 0x10006350; build gates: null
- Confidence: unresolved - both patterns resolve in 1.0.8.0 but no FusionFix site exists; FusionFix has no +siren vehicles.ide flag option.
- No FusionFix patch site found at or near any ZolikaPatch site.

### VSyncFix

- ZolikaPatch handlers: 0x100503b0; build gates: [["!=", 1], ["!=", 2], ["!=", 3], ["!=", 4], ["!=", 5], ["!=", 9], ["!=", 13]]
- Confidence: unresolved - ZolikaPatch carries 2 signatures but neither resolves in 1.0.8.0, consistent with the recovered build gate excluding build 9. FusionFix has no VSync option (its framelimit/refresh-rate code is different).
- No FusionFix patch site found at or near any ZolikaPatch site.

### VRAMFix

- ZolikaPatch handlers: 0x1004aae0; build gates: null
- Confidence: unresolved - the single signature is absent from 1.0.8.0 and there is no FusionFix site. FusionFix fixes VRAM detection differently (memory.ixx GetProcessPreferredGPUMemory, pattern 8B 0D ? ? ? ? 83 EC ? 33 C0 at 0x402a20) - conceptually related, mechanically unrelated.
- No FusionFix patch site found at or near any ZolikaPatch site.

### FastLoading

- ZolikaPatch handlers: 0x1001eca0; build gates: null
- Confidence: unresolved - no ZolikaPatch patch operations recovered; the FastLoading dispatcher calls FUN_1001eca0 which only forwards to FUN_1001ec90 (an indirect call through 0x1015e840). No FusionFix counterpart was found by pattern comparison; FusionFix speeds loading via frame-limiting (framelimit.ixx) instead.
- No FusionFix patch site found at or near any ZolikaPatch site.

### NoLoadingSleep

- ZolikaPatch handlers: 0x1003d670; build gates: null
- Confidence: high - ZolikaPatch handler 0x1003d670 pattern 68 88 13 00 00 FF 15 resolves at 0x401854 and zeroes the immediate at +1; FusionFix loadingdelays.ixx:35 uses the identical pattern and WriteMemory(get_first(1),0). Byte-level identical (68 88 13 00 00 ff 15).
- FusionFix sites reached:

  | ZP site | delta | FusionFix file:line | FusionFix statement |
  |---|---|---|---|
  | `0x401854` | +1 | `loadingdelays.ixx:35` | `injector::WriteMemory(pattern.get_first(1), 0, true);` |

### BenchmarkFix

- ZolikaPatch handlers: 0x10005690; build gates: [["!=", 9], ["!=", 13]]
- Confidence: unresolved - handler 0x10005690 carries pattern E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20 which resolves at 0x47f600, but no FusionFix patch site matches. FusionFix ships no benchmark fix.
- No FusionFix patch site found at or near any ZolikaPatch site.

### MiscFixes

- ZolikaPatch handlers: 0x10023dd0; build gates: null
- Confidence: unresolved - 11 patterns, 5 absent from 1.0.8.0, none matched to a FusionFix site. FusionFix has no single equivalent (this is a ZolikaPatch XLivelessAddon-derived bundle).
- No FusionFix patch site found at or near any ZolikaPatch site.

### LoadDLCs

- ZolikaPatch handlers: 0x100234e0; build gates: [["==", 7], ["==", 8], ["==", 9]]
- Confidence: unresolved - 17 module-relative RVAs (build 7/8/9 branches); the build-9 set (0x48f558..0x490460) is in-image but none matches a FusionFix patch site. FusionFix loads DLC through its IMG loader, not this hook set.
- No FusionFix patch site found at or near any ZolikaPatch site.

## Unresolved items

- `EpisodeSwitchFreezeFix`, `CombinedRadioFreezeFix`, `RemoveRGSCLoginRequirement`, `PopupHeadlightsFlag`, `CopSirensFlag`, `VSyncFix`, `VRAMFix`, `FastLoading`, `BenchmarkFix`, `MiscFixes`, `LoadDLCs`: no FusionFix counterpart was established by address cross-reference. These are recorded as unresolved, not as mismatches.
- `RemoveRGSCLoginRequirement`, `PopupHeadlightsFlag`, `VSyncFix`, `VRAMFix`, and 5 of 11 `MiscFixes` patterns do not resolve in the local 1.0.8.0 image at all; this matches the recovered per-option build gates and the ZolikaPatch fallback to a module-relative RVA (`0x498460` for RGSC).
- FusionFix source was read from a local `/tmp/FusionFix` clone at commit `bea25bd`; a different FusionFix revision may shift line numbers and pattern sets.

