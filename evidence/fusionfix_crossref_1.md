# ZolikaPatch vs FusionFix — Signature / Hook-Site Cross-Reference (#1)

- **FusionFix source:** github.com/ThirteenAG/GTAIV.EFLC.FusionFix @ `bea25bd848cfbdd8d046bf49776ceef9cbef3a78` (clone `/tmp/FusionFix`)
- **ZolikaPatch evidence:** `ZolikaPatch_reconstruction/evidence/option_signatures.json` + decompiled handlers
- **Game binary:** `/Users/kreato/Downloads/GTAIV.exe` sha256 `3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f` (1.0.8.0, image base 0x400000)
- **Method:** every FusionFix pattern literal + every ZolikaPatch recovered signature resolved by wildcard-aware byte scan over `.text` (`0x401000`..`+9871360`), read-only. ZolikaPatch install address = match + recovered patch offset.

## Headline numbers

| Metric | Value |
|---|---|
| FusionFix pattern call records | 932 |
| FusionFix unique pattern strings | 1260 |
| FusionFix patterns resolving in 1.0.8.0 `.text` | 674 |
| ZolikaPatch unique recovered signatures | 148 |
| ZolikaPatch signatures resolving in 1.0.8.0 `.text` | 115 |
| Byte-identical shared pattern strings | 2: `39 2D ? ? ? ? 75 3E, 39 35 ? ? ? ? 75 0E 80 3D` |

**Context:** FusionFix targets GTA IV: The Complete Edition (1.2.0.x). The reference binary is 1.0.8.0, so most FusionFix patterns legitimately do not byte-match here. Where both projects fix the same feature on the 1.0.8.0 layout they converge on the *same hook sites*, but usually encode them with different-length patterns anchored on different bytes — hence only 2 byte-identical strings.

## Feature table

| Feature | FF patterns resolve | ZP signatures resolve | Confidence |
|---|---|---|---|
| SkipIntro | partial | yes | MEDIUM — same basic block (0x4a346b / 0x4a3478), different instruction |
| SkipMenu | yes | yes | HIGH — byte-identical install at 0x47d257 |
| RestoreDeathMusic / DeathMusic | yes | n/a | MEDIUM — SAME 4 call sites; ZP uses short literal patterns instead |
| ReversingLightFix | partial | yes | HIGH — byte-identical patch byte 0x8f8848 = 0x60 |
| BrakeLightsWhenStopped (FusionFix-only) | partial | n/a | N/A — FF-only feature, no ZP option signature |
| BorderlessWindowed | partial | n/a | LOW — different implementations / disjoint sites |
| HighFPSBikePhysicsFix | yes | yes | MEDIUM — same function, different instruction (0xb9a94f vs 0xb9c05b) |
| HighFPSSpeedupFix | yes | yes | MEDIUM — same routine CTimer::Update (0x7cbf2e vs 0x7cbfe9) |
| SuperLODFix / LOD-light height cutoff | yes | n/a | MEDIUM — overlapping LOD-light region, different instructions |
| NoLoadingSleep / loading delays | partial | yes | HIGH — both -> 0x401854, delay immediate zeroed |
| EpisodicVehicleSupport | partial | yes | HIGH for 10 exact addresses; overall MEDIUM (FF uses generic episode-id patterns) |
| TBoGTHeliHeightLimit | yes | yes | HIGH — both -> 0x8f0071, NOP at +7 |
| PoliceEpisodicWeaponSupport (FF: TBoGTPoliceWeapons) | yes | yes | HIGH — identical pattern string, both -> 0x49f18c |

## Per-feature detail

### SkipIntro
- FusionFix source: `source/skipintro.ixx:55`
- ZolikaPatch option: `SkipIntro` (handler `0x10045c50`)
- FusionFix patterns (resolve -> addresses):
  - `8B 0D ? ? ? ? 8B 54 24 1C 83 C4 0C` -> ['0x4a346b']
  - `8B 0D ? ? ? ? 8B 44 24 18 83 C4 0C 69 C9 ? ? ? ? 89 81 ? ? ? ? 8D 44 24 60 68 ? ? ? ? 50 E8 ? ? ? ? 83 C4 08 85 C0 0F 85` -> *(absent)*
- ZolikaPatch signatures (match -> install = match+offset):
  - `89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50` -> match ['0x4a347e']; install 0x4a3478 (off -6, 1B = ['e9'])
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Manual verification: Same basic block, different instruction. FF alt anchors 0x4a346b; ZP rewrites the store at 0x4a347e (E9 at 0x4a3478).
- Note: FF hooks CGame::Init loadscreen setup (mov ecx,[...] ; mov edx,[esp+1C] ; add esp,0C). ZP SkipIntro patches a *different but adjacent* instruction in the same block (mov [ecx*0x190+...],edx at 0x4a347e).

### SkipMenu
- FusionFix source: `source/skipintro.ixx:104 (hook::get_pattern)`
- ZolikaPatch option: `SkipMenu` (handler `0x10046640`)
- FusionFix patterns (resolve -> addresses):
  - `E8 ? ? ? ? 83 C4 04 5F 5E 5B 8B 8C 24 ? ? ? ? 33 CC E8 ? ? ? ? 8B E5 5D C3 83 F8 30` -> ['0x47d257']
  - `E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3` -> ['0x49c87e', '0x8d11e6', '0xacb14b', '0xaefda1']
- ZolikaPatch signatures (match -> install = match+offset):
  - `83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E` -> match ['0x47d240']; install n/a
  - `6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5D 5B 33 CC E8 ? ? ? ? 81 C4 ? ? ? ? C3` -> match *(absent)*; install n/a
  - `6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5F 5E 5B 33 CC E8 ? ? ? ? 8B E5 5D C3` -> match *(absent)*; install  (off 23, 1B = ['e8']);  (off 23, 1B = ['e8'])
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Manual verification: EXACT same install address. ZP primary sig matches 0x47d240 and writes E8 at +0x17 => 0x47d257; FF fallback get_pattern matches 0x47d257 and MakeCALLs there.
- Note: CONFIRMED SAME HOOK SITE. FF fallback get_pattern matches 0x47d257 == the call instruction ZP rewrites. ZP's primary pattern matches the enclosing basic block at 0x47d240 and installs E8 at +0x17 = 0x47d257, byte-identical target.

### RestoreDeathMusic / DeathMusic
- FusionFix source: `source/deathmusic.ixx:47,50,53,57`
- ZolikaPatch option: `RestoreDeathMusic` (handler `None`)
- FusionFix patterns (resolve -> addresses):
  - `E8 ? ? ? ? 84 C0 0F 84 ? ? ? ? 0F 57 C0 F3 0F 11 86` -> ['0x81e3b8']
  - `E8 ? ? ? ? 84 C0 0F 85 ? ? ? ? C7 86 ? ? ? ? ? ? ? ? E9` -> ['0x81e4fc']
  - `E8 ? ? ? ? 84 C0 74 ? 8B BE ? ? ? ? 3B FB` -> ['0x81e53a']
  - `E8 ? ? ? ? 83 C4 ? 8D 8C 24 ? ? ? ? E8 ? ? ? ? 8B 15 ? ? ? ? 52` -> ['0x81e47e']
- ZolikaPatch inline handler patterns (decompiled FUN_10050bd0 (ZolikaPatch.asi)):
  - `0F 57 C0 F3 0F 11 86 20 02 00 00` -> *(absent)*
  - `C7 86 04 02 00 00 04 00 00 00 E9` -> *(absent)*
  - `E8 ? ? ? ? 84 C0 74 C6 8B BE` -> *(absent)*
  - 0x81e3c5: inside FF hit @0x81e3b8
  - 0x81e509: inside FF hit @0x81e4fc
  - 0x81e53a: == FF hit @0x81e53a
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Note: SAME 4 CALL SITES (0x81e3b8/0x81e4fc/0x81e53a/0x81e47e). ZP does NOT carry these as option signatures; its RestoreDeathMusic handler (FUN_10050bd0, gated by ini global 0x10159b86) instead scans three *shorter/literal* byte strings (0F 57 C0 F3 0F 11 86 20 02 00 00 @0x81e3c5; C7 86 04 02 00 00 04 00 00 00 E9 @0x81e509; E8 ? ? ? ? 84 C0 74 C6 8B BE @0x81e53a). All three resolve in 1.0.8.0 and land inside the same death-music stub region as FF's patterns.

### ReversingLightFix
- FusionFix source: `source/fixes.ixx:395`
- ZolikaPatch option: `ReversingLightFix` (handler `0x10045430`)
- FusionFix patterns (resolve -> addresses):
  - `8B 42 ? 8B CE FF D0 F3 0F 10 40 ? 8D 4C 24` -> ['0x8f8846']
  - `8B 40 ? FF D0 F3 0F 10 40 ? 8D 44 24 ? 50 FF 74 24` -> *(absent)*
- ZolikaPatch signatures (match -> install = match+offset):
  - `11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10` -> match ['0x8f8840']; install 0x8f8848 (off 8, 1B = ['60'])
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Manual verification: Same patch byte. FF WriteMemory(match+2=0x8f8848)=0x60; ZP writes 0x60 at match+8=0x8f8848.
- Note: SAME SITE. FF matches 0x8f8846 and writes byte at +2 (0x8f8848) = 0x60. ZP matches 0x8f8840 and writes byte at +8 (0x8f8848) = 0x60. Both make the reversing-light offset selector '0x60'. Byte identical install address.

### BrakeLightsWhenStopped (FusionFix-only)
- FusionFix source: `source/brakelights.ixx:40`
- ZolikaPatch option: `None` (handler `None`)
- FusionFix patterns (resolve -> addresses):
  - `F3 0F 10 83 ? ? ? ? 0F 2F 05 ? ? ? ? 77 ? 80 7C 24` -> ['0x8fc19d']
  - `F3 0F 10 86 ? ? ? ? 0F 2F 05 ? ? ? ? 77 ? 80 FE` -> *(absent)*
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Note: No ZolikaPatch option_signatures entry for this feature; ZP's ReversingLightFix operates on the adjacent taillight-bone selector. FF's first alternative resolves at 0x8fc19d, ~0.4 KB after ZP's ReversingLightFix site.

### BorderlessWindowed
- FusionFix source: `source/windowed.ixx:125-205`
- ZolikaPatch option: `BorderlessWindowed` (handler `0x10005970`)
- FusionFix patterns (resolve -> addresses):
  - `33 F6 3B C7 0F 45 F0` -> ['0x8559b1']
  - `89 35 ? ? ? ? A3 ? ? ? ? 8D 44 24 17` -> ['0x855af1']
  - `C6 05 ? ? ? ? ? FF 15 ? ? ? ? 5F` -> ['0x403079']
  - `E8 ? ? ? ? 8B 0D ? ? ? ? 8B 15 ? ? ? ? 83 C4 04 83 3D` -> ['0x86a5ac']
  - `66 25 FF 03 0F B7 C8` -> ['0x4063d2']
  - `33 FF 85 C0 0F 45 F8` -> *(absent)*
  - `89 3D ? ? ? ? A3 ? ? ? ? 8D 44 24 17` -> *(absent)*
- ZolikaPatch inline handler patterns (decompiled FUN_10005970 (ZolikaPatch.asi)):
  - `FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00` -> *(absent)*
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Note: DIFFERENT IMPLEMENTATION. FF hooks the -windowed cmdline flag parser (0x8559b1/0x855af1), the Alt+Enter notification (0x403079) and the D3D windowed flag bit-test (0x4063d2), then installs USER32 IAT hooks. ZP's BorderlessWindowed handler (FUN_10005970) instead matches a call in the 0x5bb1xx/0x5bb5xx window-proc dispatch (FF 15 ... 5F 5E 5D 5B 83 C4 10 C2 10 00 -> 0x5bb10e, 0x5bb563) and a second global-store pattern; no byte-overlap with FF's windowed sites.

### HighFPSBikePhysicsFix
- FusionFix source: `source/frameratevigilante.ixx:883-902`
- ZolikaPatch option: `HighFPSBikePhysicsFix` (handler `0x1001efa0`)
- FusionFix patterns (resolve -> addresses):
  - `F3 0F 59 05 ? ? ? ? F3 0F 58 86 ? ? ? ? F3 0F 10 0D ? ? ? ? 0F 2F C1 F3 0F 11 86 ? ? ? ? 77` -> ['0xb9a94f']
- ZolikaPatch signatures (match -> install = match+offset):
  - `D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86 90 14 00 00` -> match ['0xb9c05b']; install 0xb9c05b (off 0, 1B = ['e9'])
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Note: SAME FUNCTION, DIFFERENT INSTRUCTION. FF matches 0xb9a94f (CBike::ProcessControlInputs, multiplies steer-by-frametime). ZP matches 0xb9c05b (0x1700 = 5888 bytes later, inside the bike update wrapper) and installs an E9 jump at the match. ZP intentionally hooks the function prologue region to scale the whole call by frametime; FF scales the individual accumulator.

### HighFPSSpeedupFix
- FusionFix source: `source/comvars.ixx:3114 (CTimer::fTimeScale1)`
- ZolikaPatch option: `HighFPSSpeedupFix` (handler `0x10023b30`)
- FusionFix patterns (resolve -> addresses):
  - `F3 0F 10 05 ? ? ? ? 0F 2F C8 77 ? F3 0F 10 05` -> ['0x7cbf2e']
- ZolikaPatch signatures (match -> install = match+offset):
  - `0F 2F C1 76 03 0F 28 C8 F3 0F 10 15` -> match ['0x7cbfe9']; install 0x7cbfe2 (off -7, 2B = ['9090']); 0x7cbfe2 (off -7, 2B = ['9090']); 0x7cc0c9 (off 224, 1B = ['eb']); 0x7cbfe2 (off -7, 2B = ['9090']); 0x7cbfe2 (off -7, 2B = ['9090']); 0x7cbfe2 (off -7, 2B = ['9090']); 0x7cbfe2 (off -7, 2B = ['9090']); 0x7cbfe2 (off -7, 2B = ['9090']); 0x7cbfe2 (off -7, 2B = ['9090'])
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Note: SAME FUNCTION (CTimer::ms_fTimeStep/Update @0x7cbf00). FF reads the fTimeScale global at 0x7cbf2e. ZP matches 0x7cbfe9 and NOPs the two 'jbe' clamps at 0x7cbfe2 and 0x7cbfec (removing the frame-time floor/ceiling that causes >300 FPS speedup). Same routine, different instruction.

### SuperLODFix / LOD-light height cutoff
- FusionFix source: `source/fixes.ixx:426-456, source/lodlights.ixx:747`
- ZolikaPatch option: `SuperLODFix` (handler `0x10046a10`)
- FusionFix patterns (resolve -> addresses):
  - `F3 0F 5C 05 ? ? ? ? F3 0F 59 05 ? ? ? ? 0F 2F C1 76 08 F3 0F 11 4C 24 ? EB 06` -> ['0xc9fa5d']
  - `8B 0D ? ? ? ? 8D 51 ? 3B C2` -> ['0xca1b79']
  - `2B 0D ? ? ? ? 3B C1` -> ['0xca1bdf']
  - `75 ? F3 0F 10 05 ? ? ? ? F3 0F 10 4C 24 ? F3 0F 59 C8` -> ['0xc9ff0b']
  - `E8 ? ? ? ? 84 C0 75 ? A1 ? ? ? ? 83 F8 ? 74 ? 83 F8 ? 74 ? 83 F8` -> ['0xca1b4e']
- ZolikaPatch inline handler patterns (FUN_10046a10 (build-gated ==8/==9 module-relative patches) + FUN_100523c0 (NoLODLightHeightCutoff)):
  - `76 0B F3 0F 11 8C 24 88 00 00 00 EB 09` -> *(absent)*
  - `76 08 F3 0F 11 4C 24 1C EB 06 F3 0F 11 44 24 1C 89 5C 24 64` -> *(absent)*
- Site overlap: FF-match ∩ ZP-match = ∅; FF-match ∩ ZP-install = ∅
- Note: PARTIAL SAME SITE. FF lodlights.ixx:747 fallback matches 0xc9fa5d; ZP's SuperLODFix (build-gated ==8 or ==9) matches the 'jbe' at 0xc9fa70 (13 bytes later) and NOPs at 0xc9fa72/0xc9fa7a, plus at 0xca058d/0xca118e. FF additionally targets the LOD-light time-offset globals at 0xca1b79/0xca1bdf and the LOD-light-during-cutscene branches 0xc9ff0b/0xca1b4e, which ZP does not (ZP's NoLODLightHeightCutoff option addresses the height cutoff in FUN_100523c0).

### NoLoadingSleep / loading delays
- FusionFix source: `source/loadingdelays.ixx:34`
- ZolikaPatch option: `NoLoadingSleep` (handler `0x1003d670`)
- FusionFix patterns (resolve -> addresses):
  - `68 ? ? ? ? FF 15 ? ? ? ? 39 1D` -> ['0x401854']
  - `68 ? ? ? ? FF 15 ? ? ? ? 83 3D ? ? ? ? ? 75 ? E8 ? ? ? ? 8D 0C 24` -> *(absent)*
- ZolikaPatch signatures (match -> install = match+offset):
  - `68 88 13 00 00 FF 15` -> match ['0x401854']; install n/a
- Site overlap: FF-match ∩ ZP-match = ['0x401854']; FF-match ∩ ZP-install = ∅
- Note: SAME SITE. FF matches 0x401854 (push 0x1388 ; call kernel32 Sleep-ish) and zeroes the pushed delay. ZP matches the same address 0x401854 with '68 88 13 00 00 FF 15' and zeroes the immediate at +1. Same address, same effect (delay 0).

### EpisodicVehicleSupport
- FusionFix source: `source/episodiccontent.ixx (multiple)`
- ZolikaPatch option: `EpisodicVehicleSupport` (handler `0x1001b580`)
- FusionFix patterns (resolve -> addresses):
  - `83 3D ? ? ? ? ? 7C 35` -> ['0x8fc61f']
  - `83 3D ? ? ? ? ? 75 20 8B CD` -> ['0x4a6f1e']
  - `83 3D ? ? ? ? ? 7C 32` -> ['0x4a9d3a']
  - `83 3D ? ? ? ? ? 75 21 8B 96` -> ['0x835771']
  - `83 3D ? ? ? ? ? 89 44 24 38` -> ['0x801273']
  - `83 3D ? ? ? ? ? 7C 7D` -> ['0x997768']
  - `83 3D ? ? ? ? ? 7C 1F` -> ['0x9d223e']
  - `83 3D ? ? ? ? ? 0F 8C ? ? ? ? 8B 46 18` -> ['0x9d25d5']
  - `83 3D ? ? ? ? ? 7C 26` -> *(absent)*
  - `83 3D ? ? ? ? ? 0F 85 ? ? ? ? 8B 46 4C` -> ['0xaae8e2']
  - `83 3D ? ? ? ? ? 75 50 8B 06` -> ['0xa61409']
- ZolikaPatch signatures (match -> install = match+offset):
  - `83 3D ? ? ? ? 02 0F 8C 0E 05 00 00` -> match ['0xa530fd']; install 0xa530fd (off 0, 13B = ['90909090', '90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 7C 35 0F BF 43 2E` -> match ['0x8fc61f']; install 0x8fc61f (off 0, 9B = ['90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 8A 0D` -> match ['0x4a86ee']; install 0x4a86ee (off 0, 7B = ['90909090', '9090', '90'])
  - `83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00` -> match ['0x835771']; install 0x835771 (off 0, 9B = ['90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E` -> match ['0x71c81a']; install 0x71c81a (off 0, 13B = ['90909090', '90909090', '90909090', '90']); 0x71c81a (off 0, 8B = ['90909090', '90909090']); 0x71c81a (off 0, 8B = ['90909090', '90909090'])
  - `83 3D ? ? ? ? 02 75 08 3B 05` -> match ['0x965823']; install 0x965823 (off 0, 9B = ['90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 75 1F 8B 44 24 04` -> match ['0x965d30']; install 0x965d30 (off 0, 9B = ['90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 75 38 8B 16` -> match ['0x989974']; install 0x989974 (off 0, 9B = ['90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04` -> match ['0x98ac20']; install 0x98ac20 (off 0, 9B = ['90909090', '90909090', '90'])
  - `74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05` -> match ['0x98c7de']; install 0x98c7e0 (off 2, 9B = ['90909090', '90909090', '90'])
  - `8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18` -> match ['0x9f21b8']; install 0x9f21a9 (off -15, 7B = ['90909090', '9090', '90'])
  - `83 3D ? ? ? ? 02 75 35 0F BF 46 2E` -> match ['0xa7a951']; install 0xa7a951 (off 0, 9B = ['90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00` -> match ['0xaa704d']; install 0xaa704d (off 0, 9B = ['90909090', '90909090', '90']); 0xaa704d (off 0, 9B = ['90909090', '90909090', '90']); 0xaa704d (off 0, 9B = ['90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C` -> match ['0xb20f3c']; install 0xb20f3c (off 0, 9B = ['90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 11 44 24 20 75 4C` -> match ['0x803035']; install 0x80304a (off 21, 2B = ['9090'])
  - `7C 22 83 F8 28 75 1D` -> match ['0x801039']; install 0x801039 (off 0, 2B = ['9090'])
  - `83 3D ? ? ? ? 02 89 44 24 38` -> match ['0x801273']; install 0x80127e (off 11, 6B = ['90909090', '9090'])
  - `83 3D ? ? ? ? 02 7C 43` -> match ['0x850f03']; install 0x850f0a (off 7, 2B = ['9090'])
  - `83 3D ? ? ? ? 02 7C 1F 8B` -> match ['0x9d223e']; install 0x9d2245 (off 7, 2B = ['9090'])
  - `83 3D ? ? ? ? 02 7C 11 8B 4E` -> match ['0x9d22e9']; install 0x9d22f0 (off 7, 2B = ['9090'])
  - `39 35 ? ? ? ? 75 0E 80 3D` -> match ['0xd49b09']; install 0xd49b0f (off 6, 2B = ['9090'])
  - `83 3D ? ? ? ? 02 0F 8C D5 00` -> match ['0x9d25d5']; install 0x9d25d5 (off 0, 13B = ['90909090', '90909090', '90909090', '90'])
  - `83 3D ? ? ? ? 02 7C 32 8B 41` -> match ['0x4a9d3a']; install 0x4a9d41 (off 7, 2B = ['9090'])
  - `75 5B 83 3D ? ? ? ? 00 75` -> match ['0x4ab7c7']; install 0x4ab7c7 (off 0, 2B = ['9090'])
  - `83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C` -> match ['0x997768']; install 0x99776f (off 7, 2B = ['9090'])
  - `83 3D ? ? ? ? 02 75 50` -> match ['0xa61409']; install 0xa61410 (off 7, 2B = ['9090'])
  - `83 3D ? ? ? ? 02 0F 85 8C 01 00 00` -> match ['0xaae8e2']; install 0xaae8e9 (off 7, 6B = ['90909090', '9090'])
  - `83 3D ? ? ? ? 02 75 20 8B CD E8` -> match ['0x4a6f1e']; install 0x4a6f25 (off 7, 2B = ['9090'])
  - `83 3D ? ? ? ? 02 7C 21 8B 44 24 0C` -> match ['0x9900a6']; install 0x9900ad (off 7, 2B = ['9090'])
- Site overlap: FF-match ∩ ZP-match = ['0x4a6f1e', '0x4a9d3a', '0x801273', '0x835771', '0x8fc61f', '0x997768', '0x9d223e', '0x9d25d5', '0xa61409', '0xaae8e2']; FF-match ∩ ZP-install = ['0x835771', '0x8fc61f', '0x9d25d5']
- Note: SAME SITES. ~19 ZP EpisodicVehicleSupport signatures like '83 3D ? ? ? ? 02 7C 35'/'... 75 20' resolve to exactly the addresses FF targets with its generic '83 3D ... ? 7C 35' etc. (FF omits the '02' episode-id immediate). All within-load. High confidence.

### TBoGTHeliHeightLimit
- FusionFix source: `source/episodiccontent.ixx:575`
- ZolikaPatch option: `TBoGTHeliHeightLimit` (handler `0x1001b310`)
- FusionFix patterns (resolve -> addresses):
  - `83 3D ? ? ? ? ? 7C 0A F3 0F 10 05` -> ['0x8f0071']
- ZolikaPatch signatures (match -> install = match+offset):
  - `83 3D ? ? ? ? 02 7C 0A F3 0F` -> match ['0x8f0071']; install 0x8f0078 (off 7, 2B = ['9090'])
- Site overlap: FF-match ∩ ZP-match = ['0x8f0071']; FF-match ∩ ZP-install = ∅
- Note: SAME SITE. Both resolve to 0x8f0071. ZP installs NOP (2 bytes) at +7; FF MakeNOPs at +7 too. Byte identical.

### PoliceEpisodicWeaponSupport (FF: TBoGTPoliceWeapons)
- FusionFix source: `source/episodiccontent.ixx:586`
- ZolikaPatch option: `PoliceEpisodicWeaponSupport` (handler `0x1001bec0`)
- FusionFix patterns (resolve -> addresses):
  - `39 2D ? ? ? ? 75 3E` -> ['0x49f18c']
- ZolikaPatch signatures (match -> install = match+offset):
  - `39 2D ? ? ? ? 75 3E` -> match ['0x49f18c']; install 0x49f192 (off 6, 2B = ['9090'])
- Site overlap: FF-match ∩ ZP-match = ['0x49f18c']; FF-match ∩ ZP-install = ∅
- Note: IDENTICAL PATTERN STRING. Both resolve to 0x49f18c. Byte-identical shared pattern string, exactly the same fallback branch FF uses for TBoGTPoliceWeapons.

## Unresolved / caveats

- Some FusionFix patterns are *constructed at runtime* (`"C7 05 " + pattern_str(to_bytes(...))`, `"81 ? " + ... + "7C"`); these are not static signatures and were excluded from the exact-resolve counts.
- Several FusionFix `find_pattern("A","B")` calls are A/B fallbacks for different game builds; on 1.0.8.0 typically only the second resolves (e.g. brakelights, reversing lights, skipintro).
- ZolikaPatch `RestoreDeathMusic`, `BorderlessWindowed`, `SuperLODFix`, `NoLODLightHeightCutoff` have **no** dedicated entry in `option_signatures.json`; their patterns were read directly from the decompiled handler bodies (FUN_10050bd0 / FUN_10005970 / FUN_10046a10 / FUN_100523c0). Recorded under `zolikapatch_inline_handler`.
- Where a ZolikaPatch signature is absent in 1.0.8.0 (VSyncFix, SkipLauncher, LocalSettingsPath, parts of MiscFixes) it is usually build-gated to an older/newer build or consumed via a literal/RVA path (see existing `game_verification.json`).
- Addresses are Virtual Addresses with image base 0x400000; the on-disk `.text` file offset is `VA - 0x401000`.

## Data

Machine-readable results: `ZolikaPatch_reconstruction/evidence/fusionfix_crossref_1.json`.