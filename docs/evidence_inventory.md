# ZolikaPatch evidence and inventory

Inspection scope: extracted payload at `/Users/kreato/Sources/decomp/ZolikaPatch_extracted` and existing Ghidra output at `/Users/kreato/Sources/decomp/ZolikaPatch_decompiled/ghidra`. This is an evidence inventory, not a claim that the decompiler output is buildable source.

## Extracted payload

All four binaries are 32-bit Intel 80386 Windows PE files. Sizes and SHA-256 hashes were computed from the files as inspected.

| Relative path under `ZolikaPatch_extracted` | Bytes | Identification | SHA-256 |
|---|---:|---|---|
| `ZolikaPatch.asi` | 1,502,208 | PE32 DLL, Windows GUI | `720fbcebfd563616151560020eea12168115a9183d6813b432b0367d2b702fc5` |
| `GFWLProtectionDisabler2019.asi` | 72,704 | PE32 DLL, Windows GUI | `88b718faf2552572d20f9fffc54418f0ff29ba4720af7ee7de0e776424d783d3` |
| `PlayGTAIV.exe` | 14,938,624 | PE32 executable, Windows GUI | `32c4e55115ac8e8d7ca0b1f5e0dd86fc4382dc455cc21cd4d92ffea0b78c5e03` |
| `for extra menu options (1.0.7.0 and 1.0.8.0 only)/IVMenuAPI.asi` | 184,320 | PE32 DLL, Windows GUI | `17ec9c4c2a9053ae02ed3cf3a97826d00a36f4c3b3212fd1f0a2a03f34d94d83` |
| `ZolikaPatch.ini` | 39,180 | text configuration | `9639b59c24303f11db5e4fcdc044d64302441a7210591b28b68b14b3756ebcb9` |
| `readme.txt` | 223 | ASCII text, CRLF | `0e653bca507264623a29c8b6fbfe5247985d9f175ad48a820e57dfe658b1d2e7` |

PE-header evidence: `ZolikaPatch.asi` has image base `0x10000000`, image size `0x1c1000`, GUI subsystem 6.0, and a linker timestamp decoding to 2023-09-16 06:08:54 UTC. Its imports include `KERNEL32`, `USER32`, `MSVCP140`, `SHLWAPI`, `VERSION`, `VCRUNTIME140`, and Universal CRT components. Embedded strings include `ZolikaPatch.ini`, `IVMenuAPI.asi`, `AddIVMenuOption`, `GetZMenuVersion`, the downgrade URL `https://zolika1351.pages.dev/mods/ivpatch/downgrading`, and the build-path residue `D:\Stuff\Projects\GTAIV\ZPatch\bin\ZolikaPatch.pdb`.

The directory name itself limits the optional `IVMenuAPI.asi` to GTA IV 1.0.7.0/1.0.8.0. `PlayGTAIV.exe` strings also state that ZolikaPatch requires GTA IV 1.0.8.0 and that ZMenuIV requires ZolikaPatch 7.32 or newer. The adjacent archive is named `ZolikaPatch_v7.65.zip`; that filename is version evidence, while no equivalent 7.65 string was found in `ZolikaPatch.asi`.

## Readme and INI evidence

`ZolikaPatch_extracted/readme.txt` contains only installation-specific configuration guidance:

- GTA IV 1.0.4.0: set `Patch6and7RemoveModChecks=0` and `Patch4RemoveModChecks=1`.
- For GFWL multiplayer on 1.0.4.0: set `RemoveGFWLUpdatePopup=1`.

`ZolikaPatch_extracted/ZolikaPatch.ini` has three sections:

- `[Options]`: 98 settings—75 default to `1`, 22 to `0`, and `OverrideGTADatPathTo=common:/data/gta.dat` is the one non-Boolean value. Inline comments document fixes, compatibility patches, limits, rendering/physics changes, multiplayer protections, loading behavior, and optional menu integration. Notable shipped defaults include `AddOptionsInMenu=1`, `LaunchWithPlayGTAIV=1`, `LoadDLCs=1`, `Patch6and7RemoveModChecks=1`, `Patch4RemoveModChecks=0`, and `RemoveGFWLUpdatePopup=0`, consistent with the readme's 1.0.4.0 override instructions.
- `[PopupLightAngles]`: 512 `CarNName`/`CarNAngle` pairs (1,024 keys), all empty placeholders.
- `[HeadlightConeOffsets]`: 512 `CarNName`/`CarNOffset` pairs (1,024 keys); 78 pairs are populated (`Car1` through `Car78`), ending with `comet5=5`; the remaining 434 pairs are empty.

Note: the host `file` utility labels the INI “Microsoft HTML Help Project” because of its bracketed syntax; direct inspection confirms ordinary key/value configuration text.

## Decompiled layout

`/Users/kreato/Sources/decomp/ZolikaPatch_decompiled/ghidra` contains one manifest and five contiguous chunks:

| File | Bytes | Lines | Function records represented | Address span from chunk headers |
|---|---:|---:|---:|---|
| `decompiled_0000.c` | 693,935 | 25,312 | 200 | `0x10001020`–`0x100241e0` |
| `decompiled_0001.c` | 635,090 | 21,444 | 199 | `0x10024440`–`0x10057e60` |
| `decompiled_0002.c` | 223,257 | 8,421 | 200 | `0x10058090`–`0x1005ff77` |
| `decompiled_0003.c` | 56,281 | 3,284 | 200 | `0x1005ffc3`–`0x10061d2f` |
| `decompiled_0004.c` | 36,790 | 2,685 | 195 | `0x10061d3a`–`0x10062ed7` |

The five `.c` chunks total 1,645,353 bytes and 61,146 lines. Each begins with: “Ghidra decompiler output. Not directly compilable; types and names require recovery.” Names are predominantly synthetic `FUN_<address>`, with Ghidra placeholder types/globals and later unwind routines; this is analysis material rather than recovered source.

`functions.csv` is 55,873 bytes and has 995 function rows with columns `address,name,entry_name,thunk,status,chunk`. Distribution is 200/200/200/200/195 rows across chunks 0–4; 35 entries are thunks. Decompilation succeeded for 994 rows. The sole failure is address `0x10024600`, recorded as a process timeout, explaining why chunk 1 contains 199 emitted function blocks. The address base aligns with `ZolikaPatch.asi`'s `0x10000000` image base, supporting that this layout describes that DLL.

## Preservation / verification

No extracted artifact, decompiler artifact, or ZIP was modified. The ZIPs present beside the working directories were read only for preservation verification:

- `/Users/kreato/Sources/decomp/ZolikaPatch_v7.65.zip` — 13,477,120 bytes; SHA-256 `7325e401cb8a2d83caa3a4f6718faacd3d6ef1dd6e13500dbac574d274bb89db`.
- `/Users/kreato/Sources/decomp/ZMenuIV_v23.09.18.2.zip` — 117,259,652 bytes; SHA-256 `49b1718aa9aa82b61966c3fbb183710976fff54c672f52e0652dd65032828bf4`.

Only this report was created under `ZolikaPatch_reconstruction`.
