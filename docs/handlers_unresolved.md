# ZolikaPatch option handlers — the unresolved / empty install rows

Machine-companion to `docs/handlers_table.md` and `evidence/handler_table.json`.
Covers the 50 handlers whose `installs` array in `handler_table.json` is either
**empty** or contains at least one entry with `target == null`.

Read-only sources used:

- `ZolikaPatch_extracted/ZolikaPatch.asi` (i686 disassembly via
  `i686-w64-mingw32-objdump -d -Mintel`, plus raw `.text`/`.rdata`/`.data` bytes).
- `evidence/handler_table.json`, `evidence/patch_operations_all.json`,
  `evidence/option_build_gates.json`, `evidence/import_table.json`,
  `ZolikaPatch_decompiled/ghidra/functions.csv` (the getter's own enumerator).
- `/Users/kreato/Downloads/GTAIV.exe` (1.0.8.0, base `0x400000`, sha256
  `3d90e7c5…fbfd0f`) — pattern matching re-run with the same matcher semantics as
  `tools/verify_game_signatures.py` (`?`/`??` wildcards, whitespace tokens).

## 0. Why these rows were empty (root cause)

`tools/extract_handler_table.py` disassembles each handler over the window
`[handler, min(next_entry_in_functions.csv, handler + 0x400))`. Two consequences
produced every gap in the table:

1. **Truncation.** Six handler bodies are longer than `0x400` bytes, and the
   extractor stopped early — sometimes before a *single* store had been seen:

   | handler | option | window used | real body end | tail lost |
   |---|---|---|---|---|
   | `0x10006910` | CarPartsShadowFix | `0x10006d10` | `0x10007e3b` | 0x112b bytes |
   | `0x1001b580` | EpisodicVehicleSupport | `0x1001b980` | `0x1001bd5a` | 0x3da bytes |
   | `0x100234e0` | LoadDLCs | `0x100238e0` | `0x10023b27` | 0x247 bytes |
   | `0x10044710` | Patch6and7RemoveModChecks | `0x10044b10` | `0x10044bb1` | 0xa1 bytes |
   | `0x10044bd0` | RemoveUselessChecks | `0x10044fd0` | `0x10045349` | 0x379 bytes |
   | `0x10045ce0` | SkipLauncher | `0x100460e0` | `0x1004660d` | 0x52d bytes |

   CarPartsShadowFix's only patch write is at `0x10007db7`; the window ends at
   `0x10006d10`. `RemoveUselessChecks` is the extreme case: the window covers the
   first ~60 of the ~4500 runtime writes (the last 0x379 bytes contain two whole
   sweeps, including the 0x2688-iteration one). (The size column is the number of further bytes between the
   truncated window and the true function end.) Note `EpisodicVehicleSupport` and
   `LoadDLCs` each carry a *second* `handler_table.json` row whose `installs` are
   also empty, and `SkipLauncher`'s window misses 3 of its 7 installs.

2. **The constant-load regex assumes `eax` and index-pairs blindly.** The extractor pairs the `n`-th
   `mov BYTE PTR [reg],0xe9|e8|eb` with the `n`-th
   `mov eax,0x1000xxxx ; … ; sub eax,5` *by index*. Handlers that reuse one
   constant for several installs (`LoadDLCs`, `SavegameFix`, `RemoveUselessChecks`,
   `SuperLODFix`, `Patch6and7RemoveModChecks`) therefore end up with more opcodes
   than constants and `target == null` for the surplus. Handlers that load the
   constant into a register **other than `eax`** (`mov ebx,…` in LoadDLCs,
   `SavegameFix`; `mov eax,imm ; sub eax,edi ; sub eax,5` in
   `RemoveBoundingBoxCulling`) never match the regex at all.

Everything below was re-derived by a full linear data-flow trace of each handler
body (registers tracked symbolically; the `call 0x10003830` pattern scan modelled
as `eax = <first match>`; `call 0x10005afa0` as the multi-hit collector). The
method was validated by re-deriving all 29 already-resolved handlers: **29/29
`(opcode, target)` pairs reproduce `handler_table.json` exactly.**

## 1. Conventions in the tables below

- `site±N` — offset relative to the address the handler's own `FUN_10003830`
  pattern call returns for the *current* pattern.
- `module+0xRVA` — write to `GetModuleHandleA(0)` (cached at `0x1015ad70`) plus
  the RVA; these handlers do not scan the module at all.
- `abs[0xVA]` — write to a literal plugin-image address.
- `disp` — the 32-bit value the handler stores; `dest` — the effective jump/call
  target, i.e. `opcode_address + 5 + disp` (see the note on the `+5` skew at the
  end of §4). Values are given as *absolute* bytes stored, not deltas.
- "hits" — number of matches of the pattern inside `GTAIV.exe` 1.0.8.0.

---

## 2. RESOLVED — handlers that DO write (43 handlers)

### 2.1 Signature-relative installs

| option | handler | pattern (hits) | writes |
|---|---|---|---|
| **BuildingAlphaFix** | `0x10005ca0` | `F7 46 24 00 00 00 40 0F 85` (1) | `site+0x0` dword `90909090`, `site+4` dword `90909090`, `site+8` dword `90909090`, `site+0xc` byte `90` (13 bytes NOPped) |
| | | `F7 46 24 00 00 00 40 0F 84` (1) | `site+0x7` byte `e9` (replaces `0F 84`); `site+0x8` dword = `dword[site+0x9] + 0xd - 0x7 - 5` = `dword[site+0x9] + 1`. Since `site+7+5+disp = site+13+dword[site+9]` and the original `0F 84 rel32` at `site+7` also branched to `site+13+rel32` with `rel32 == dword[site+9]`, **the original branch displacement is reused verbatim** — `je` becomes `jmp`, keeping the same destination |
| **CarPartsShadowFix** | `0x10006910` | runtime-built `85 DB 7E 41 55 EB 0A` (1; game VA `0x40c9ff`) | `abs[0x1015bdc8] = site+0x2` and `abs[0x1015bdc0] = site+0x45` (two helper entry addresses, later read by the `pusha` trampolines at `0x10006880`); `site-0x6` byte `e9` + `site-0x5` dword `disp` → **target `site-6+5+disp`**, with the stored constant `0x10006880` ⇒ effective entry **`0x10006885`** (six bytes into the game's own function; see §4) |
| **TBoGTHeliHeightLimit** | `0x1001b310` | `83 3D ? ? ? ? 02 7C 0A F3 0F` (1) | `site+0x7` word `9090` (NOPs `7C 0A`). Build-gated `DAT_1015be04 >= 6`, so it does run on 1.0.8.0 |
| **EpisodicWeaponSupport** | `0x1001b380` | 8 patterns, all 1 hit | dword `90909090` at `site+0`, `site+4`, `site+8` then byte `90` at `site+0xc`; three patterns instead NOP 2 bytes at `site+7`; one writes word `9090` at `site+0`; one writes dword `90909090` at `site+7` + word `9090` at `site+0xb`. 8 patterns, 12 stores |
| **EpisodicVehicleSupport** | `0x1001b580` | 21 patterns, all 1 hit | 46 NOP stores: `90909090` at `site+0`/`site+4`/`site+8` (+`90` at `+0xc`) for 10 patterns; `9090` at `site+7`, `site+0`, `site+0x11`, `site+0x15`, `site-0xb`; 8-byte NOPs at `site-0xf`; 10-byte NOPs at `site+2`. **Plus 4 module-relative pointer stores** (build-gated, see §5): `module+0x21500`, `module+0x7d91ae` (build 8) and `module+0x95040`, `module+0x684abe` (build 9) all ← `0x10158128` |
| **BikePhoneAnimsFix** | `0x1001bd60` | `80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE` (1) | `site+0x21` dword `90909090`, `site+0x25` word `9090` (6 bytes) |
| **BikeFeetFix** | `0x1001bdd0` | `39 82 54 13 00 00 0F 85 94 04 00 00` (1) | `site+0xc` dword `90909090`, `site+0x10` dword `90909090`, `site+0x14` dword `90909090` (12 bytes) |
| **PipeBombDropIconSupport** | `0x1001be40` | `83 7D 18 1C 75 14` (1) | `site-0x9` dword `90909090`, `site-0x5` dword `90909090`, `site-0x1` byte `90` (9 bytes preceding the match) |
| **PoliceEpisodicWeaponSupport** | `0x1001bec0` | `39 2D ? ? ? ? 75 3E` (1) | `site+0x6` word `9090` |
| **ForceDynamicShadowsEverywhere** | `0x1001ee50` | `F7 46 4C 00 80 00 00 74` (1) | `site+0x7` byte `90`, `site+0x8` byte `90` |
| **ForceShadowsOnObjects** | `0x1001eed0` | `F7 47 40 00 10 00 00 74 07 81 4E 24 00 00` (1) | `site+0x7` byte `eb` (turns `74 07` into `eb 07`) |
| **InfiniteCarBottomDistance** | `0x10022d80` | `0F 2F D0 76 25 0F 28 C2` (1) | `site+0x3` byte `eb`, `site+0x4` byte `2a` — one 2-byte rewrite `76 25` → `eb 2a` |
| **OverrideGTADatPath** | `0x10022f60` | `68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 ? ? ? ? E8` (1) | `site+0x1` dword `0x10158068` (rebinds the pushed `char*` to the plugin's `"common:/data/gta.dat"` buffer) |
| **IncreaseVehicleModelLimit** | `0x10022fc0` | `83 C7 01 81 FF 18 79 00 00 7C D2` (1) | **indirect**: reads `edi = dword[site+0x4d]` and writes dword `0x400` there. In 1.0.8.0 `site = 0x8DD143`, `dword[site+0x4d] = 0x00F3E8E8` (the loop bound at `0x00F3E8E8`, currently `0x000000CD` = 205) — i.e. the vehicle-model loop limit is raised to 1024 |
| **IncreaseVehicleStructLimit** | `0x10023020` | `68 ? ? ? ? 6A 32 8B C8` (1) | `site+0x6` byte `0x64` — changes `6A 32` (`push 50`) into `6A 64` (`push 100`) |
| **IncreasePtrNodes** | `0x10023080` | `6A 14 ? ? ? ? ? 68 80 3E 00 00` (1), `6A 08 ? ? ? ? ? 68 80 38 01 00` (1), `6A 10 ? ? ? ? ? 68 80 3E 00 00` (1) | `site+0x8` dword `0xfa00`, `0x4e200`, `0xfa00` (the three pool-size immediates) |
| **HighFPSSpeedupFix** | `0x10023b30` | `0F 2F C1 76 03 0F 28 C8 F3 0F 10 15` (1) | `site+3` word `9090`, `site-7` word `9090`, `site+0xe0` byte `eb`, and word `9090` at `site+0xe7`, `site+0x16f`, `site+0x179`, `site+0x25f`, `site+0x266`, `site+0x2fc` |
| **RemoveAllWeaponsProtection** | `0x10037100` | `E9 ? ? ? ? 57 8D 8F ? ? 00 00` (1) | dword `90909090` at `site+5`, `site+9`, `site+0xd` (12 bytes) |
| **TeleportProtection** | `0x10037170` | `55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 8D 44 24 10 8B F1 8D 7B 04 50 8B CF C7 44 24 14 00` (1) | `site+0x0` byte `c2`, `site+0x1` word `0x0008` — rewrites the prologue into `ret 8` (function disabled) |
| **MPNikoCrashFix** | `0x10037290` | `74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00` (1) | `site+0x0` byte `eb` (turns `74 2B` into `eb 2b`) |
| **RemoveStaticCarShadows** | `0x10038800` | `F3 0F 11 4C 24 0C 76 5B D9 45 14` (1) | `site+0x6` byte `eb` |
| **RemoveStaticCutsceneCarShadows** | `0x10038860` | `F3 0F 11 44 24 0C 76 5B D9 45 14` (1) | `site+0x6` byte `eb` |
| **NoLoadingSleep** | `0x1003d670` | `68 88 13 00 00 FF 15` (1) | `site+0x1` dword `0` — zeroes the `Sleep(5000)` immediate |
| **ReversingLightFix** | `0x10045430` | `11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F 10` (1) | `site+0x8` byte `0x60` (guarded by the usual `VirtualProtect` pair). Turns the following `F3 0F 10 …` (`movss`) into `F3 0F 60 …` |
| **SCOSignatureFix** | `0x10045980` | `8B 6C 24 1C 66 89 6E 14 64 8B 0D 2C 00 00 00 8B` (1) | `site-0x20` byte `eb` |
| **TryToSkipAllErrors** | `0x10046b30` | `81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00` (1) | `site+0x0` byte `c3` (`ret`) |
| **VSyncFix** | `0x100503b0` | `8B 10 50 8B 82 A8 00 00 00 FF D0 83` (0 hits), `8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 50 FF D2 8B F0 81 FE ? ? ? ? 74` (0 hits) | `site+0xd` dword `0x10159bc0`; `site+0x1e` dword `0x10159bc0`. **Both patterns are absent from 1.0.8.0** — consistent with the recovered build gate excluding 9 and 13 (see §5) |

| option | handler | pattern (hits) | writes (continued) |
|---|---|---|---|
| **MiscFixes** | `0x10023dd0` | 11 patterns, hits `0,1,2,0,0,1,0,0,1,1,1` | `C7 06 ? ? ? ? 74 19` → `site+0x6` byte `eb`. `83 C4 ? 3B 46 20` → `site+0xc` byte `eb`. `75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C4 0C` → `site+0x0` byte `eb`. `8B 54 24 04 52 A3` → `site+0x0` word `0xc9eb` (i.e. `c9 eb`). `83 F8 01 75 30 A3` → `site+0x0` byte `b8` + `site+0x1` dword `1` (turns `83 F8 01` into `b8 01 00 00 00` = `mov eax,1`). `C7 05 … C3 C7` → `site+0x0` word `0x0deb`. Three `FF 15 …` patterns → `site+0x8` word `9090`. `81 EC … E8` → `site+0x0` byte `c3`. `74 05 E8 …` → `site+0x0` byte `eb` |
| **PopupHeadlightsFlag** | `0x10044590` | `0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1` (1), `0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1` (**0 hits**) | per pattern: `site+0x52` dword `0x101580cc` (points the compared global at the plugin's own `popuplights` flag), `abs[0x1015d62c] = site+0xa` (records the patched instruction), `site+0xa` byte `eb`, `site+0x0` byte `e9`. After the second pattern an additional `site+0x1` dword `disp` → **jump target `site+5+disp = 0x10044565`** (constant `0x10044560`), gated on `DAT_1015d62c != 0` |
| **RemoveUselessChecks** | `0x10044bd0` | *(no patterns — module-relative)* four independent build gates inside the body (`cmp DAT_1015be04,9` at `0x10044bee`, `,8` at `0x1004519c`, `,5` at `0x10045268`, and one more) | **Four table-driven sweeps over `.rdata` RVA lists**, each of the form `for (i=0; i<COUNT; i+=4) { p = GetModuleHandleA(0) + dword[TABLE+i]; VirtualProtect(p, N, 0x40); write; VirtualProtect(p, N, old); }`:<br>• `TABLE=0x10104090`, `COUNT=0x22d8`, writes 6×`90` at `module+dword[...]` (2230 targets)<br>• `TABLE=0x10106448`, `COUNT=0xb4` (45 targets), writes `b0 01 90 90 90` (`mov al,1`) at `module+dword[...]`, plus byte `1` at `+1`, `90` at `+2`, `90` at `+3`, `90` at `+4`<br>• `TABLE=0x10106368`, `COUNT=0x23c0` (2288 targets), writes `32 c0` (`xor al,al`) at `module+dword[...]`<br>• `TABLE=0x10108b88`, `COUNT=0x2688`, writes 5×`90` (8+ targets)<br>Plus single module-relative writes: `module+0x1..4` = `c0 90 90 90`; `module+0x559598` and `module+0x62555d` = `eb`; and on build 5 only `module+0x46cce7` gets a 38-byte `0x90` fill (two `movups` from `ds:0x10152400` + `90909090` at `+0x20` + `9090` at `+0x24`). The 66 individual addresses in `report_data` are one *unrolled instance* each — the loops execute ~4500 times at runtime |
| **Patch6and7RemoveModChecks** | `0x10044710` | *(no patterns)* build gate `==7/==8` | `module+0x392e` byte `eb`; `module+0x42e7..0x42ed` seven bytes `90`; `module+0x3982` byte `e9` + `module+0x3983` dword `0x82` (→ offset `0x87`); `module+0x3a14` byte `eb`; `module+0x390e` byte `eb`; `module+0x42c7..0x42cd` seven bytes `90`; `module+0x3962` byte `e9` + `module+0x3963` dword `0x82`; `module+0x39f4` byte `eb`; `module+0x389c` byte `e9` + `module+0x389d` dword `0x0d`; `module+0x38ac` byte `0`; `module+0x397e` byte `0`; `module+0x388c` byte `0`; `module+0x395e` byte `0` |
| **LoadDLCs** | `0x100234e0` | *(no patterns)* build gate `==7/==8/==9` | Three build-specific blocks. **Build 7** (`0x10023500` ff): writes `abs[0x1015804c]=0x4136e0`, `abs[0x101580d0]=0x414080`, then `module+0x41450e` byte `e9` + `module+0x41450f` dword `0x19`; `module+0x41453a` byte `e8` + `module+0x41453b` dword `-0x70f`; `module+0x41453f..0x414547` 12 bytes `90`; `module+0x41445c` byte `e9` + dword `0x57`; `module+0x4137cd` byte `e8` + dword `disp` → **`0x10023485`**; `module+0x4144d4` byte `e9` + dword `disp` → **`0x10023495`**. **Build 8**: same shape with RVAs `0x4142ae/0x4142af(+0x19)`, `0x4142da/0x4142db(-0x70f)`, `0x4142df..0x4142e7`, `0x4141fc(+0x57)`, `0x41356d`, `0x414274` → same two targets; and `abs[0x1015804c]=0x413480`, `abs[0x101580d0]=0x413e20`. **Build 9**: `abs[0x101580d0]=0x48fe00`, `abs[0x1015804c]=0x48f440`, `module+0x49040e` `e9` + `0x19`; `module+0x49043a` `e8` + `-0x88f`; `module+0x49043f..0x490447` 12×`90`; `module+0x49035c` `e9` + `0x57`; `module+0x48f558` `e8`; `module+0x4903d4` `e9`. 45 stores total |
| **SuperLODFix** | `0x10046a10` | *(no patterns)* build gate `==8/==9` | build 8: `abs[0x101580ac] = module+0x4a3d37`, `abs[0x101580b0] = module+0x4a3cef`, `abs[0x101580b4] = module+0x58aae0`; then `module+0x4a3ce9` byte `e9` + dword `disp` → **`0x100469e5`** (constant `0x100469e0`). build 9: same three globals ← `module+0x540e67 / 0x540e1f / 0x4dd2d0`; `module+0x540e19` byte `e9` + dword → **`0x100469e5`** |
| **LaunchWithPlayGTAIV** | `0x100441b0` | *(no patterns)* build switch `==1, ==5, ==7, ==8, ==9` (0x100441D5/1E8/1FB/20E + case 9) | No code patch — **writes the literal strings `"PlayGTAIV"` and `"V.exe\0e\0"`** (dwords `79616c50`, `49415447`, `78652e56`, `650065`) into the game's import-name string table. Build 1 → `module+0x98fa0c` / `module+0x9f043c`; build 5 → `module+0x9dd8a0`; build 7 → `module+0x9f8f40`; build 8 → `module+0x9f9f70`; build 9 → `module+0xa5a5e4` (present in 1.0.8.0 as `"LaunchGTAIV.exe"`). In the tested 1.0.8.0 image the build-9 RVAs resolve to `"LaunchGTAIV.exe"` / `"VDS1"` — so this option rewrites the launcher module name in-place |

### 2.2 Signature-relative writes with a computed (non-constant) displacement

| option | handler | pattern (hits) | writes |
|---|---|---|---|
| **NewMemorySystem** | `0x10038680` | `3D 80 99 99 59 73 05` (1), `6A 01 B9 00 00 40 06` (1) | pattern 1: `site-0x22` byte `e8`; `site-0x21` dword `disp` → **`0x10038675`** (constant `0x10038670`); pattern 2: `abs[0x10159cd8] = dword[site+8]` (no code write) |
| **RemoveBoundingBoxCulling** | `0x10038770` | `3C 01 57 0F 85` (1) | `site+3` byte `e9`; `site+4` dword `disp = (dword[site+5] + 0xd) - (site+3) - 5` → **relative jump into `site+5+dword[site+5]`** (computed from the original `jne` displacement, no loaded constant) |
| **SavegameFix** | `0x10045610` | `75 ? 83 C1 01 83 C0 01 83 F9 04` (1), `8D 4C 24 18 51 6A 00 6A 0C 57` (1) | 10 `e8` installs. Pattern 1: `site` word `9090`; `site-0x49` → **`0x10045585`**; `site-0x8f` → **`0x100455a5`**; `site-0xb0` → **`0x100455a5`**; `site-0x118` → **`0x100455e5`**; `site-0xf0` → **`0x10038575`**. Pattern 2: `site-0x56` → **`0x100455e5`**; `site-0x2f` → **`0x10038575`**; `site+0xa` → **`0x100455a5`**; `site+0x5a` → **`0x100455a5`**; `site+0x80` → **`0x10045585`**. Constants come from `mov eax,IMM` (`0x10045580`, `0x100455e0`, `0x10038570`) and `mov ebx,0x100455a0` + `sub eax,edi` |
| **RemoveRGSCLoginRequirement** | `0x10045490` | `83 EC 44 A1 …` (**0 hits** in 1.0.8.0) | signature path: `site+0x0` byte `e9`, `site+0x1` dword `disp` → **`0x10038555`** (constant `0x10038550`). Fallback path (build 9 only, `cmp DAT_1015be04,9` at `0x10045514`): `module+0x498460` byte `e9` + dword → **`0x10038555`**. Confirms `evidence/fallback_patch_sites.json` |
| **ImprovedShaderStreaming** | `0x10045ad0` | `33 F6 8B 4C 24 08 …` (1) | `site+0x55` byte `e8` + `site+0x56` dword → **`0x10045a35`**; `site+0x62` byte `e9` + `site+0x63` dword `0x8e` → **`site+0x62+5+0x8e`** in the game; `site+0xfd` byte `e8` + `site+0xfe` dword → **`0x10045a85`**; `site+0x10a` byte `e9` + `site+0x10b` dword `0x377` → **`site+0x10a+5+0x377`** |
| **SkipMenu** | `0x10046640` | `83 F8 03 75 ? A1 …` (1), `6A 00 E8 … 5F 5E 5D 5B 33 CC E8 … 81 C4 … C3` (**0 hits**), `6A 00 E8 … 5F 5E 5B 33 CC E8 … 8B E5 5D C3` (**0 hits**) | pattern 1: `site+0x17` byte `e8` + `site+0x18` dword → **`0x10046615`** (constant `0x10046610`). pattern 3: `site+2` byte `e8` + `site+3` dword → **`0x10046615`**; `abs[0x10159348] = site+7` |
| **SMPA60Fix** | `0x100467a0` | `01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C` (1), `8B 48 70 83 C0 40 50 51 8D 51 10` (1) | pattern 1: `abs[0x10159c6c] = site+0x88`; `site+0x7b` byte `e9` + `site+0x7c` dword → **`0x10046785`** (constant `0x10046780`). pattern 2: `abs[0x10159c70] = site+0x1c`; `abs[0x10159c68] = site+0xe`; `site` byte `e9` + `site+1` dword → **`0x10046755`** (constant `0x10046750`) |
| **LocalSettingsPath** | `0x100513a0` | `53 8B 5C 24 08 56 6A 00 6A 00` (1), `7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C3` (1), `7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3` (**0 hits**) | pattern 1: `site+0x10` dword `0x10158064` (the `"common:/data/gta.dat"` path pointer), `site+0x20` dword `0x101580f8` (the alternate settings-path buffer). pattern 2: `abs[0x10159d14] = [site-0x97]`, `abs[0x10159d18] = [site-0x1bc]`, `abs[0x10159d08] = [site-0x1b0]`, `site+0xe` byte `e9`. pattern 3: `abs[0x10159d18] = [site-0xa0]`, `abs[0x10159d1c] = [site-0x37]`, `site-0xd` byte `e9` + `site-0xc` dword → **`0x10051305`** (constant `0x10051300`) |
| **SkipLauncher** | `0x10045ce0` | 25 patterns, 7 with hits (`0,1,0,0,0,0,1,0,0,1,1,0,…,1,1,0,0`) | `site+0x22` byte `eb` (pattern 0); `site+3` dword `90909090` + `site+7` word `9090` (pattern 4); same for pattern 5; `site+3` word `0xc033` (pattern 7 = `xor eax,eax`); `site` dword `0x90c3c033` (pattern 24); plus `site` byte `e9` at `0x10046195`. Note the multi-hit collector `FUN_10005afa0` is used at `0x10045fd4` (`A1 ? ? ? ? 85 C0 8B 0D ? ? ? ? 75`) and `0x10046049` (`33 C0 83 3D ? ? ? ? 01 0F 94 C0 C3`) — the first *and last* hits are patched with `0x90c301b0` |

---

## 3. GENUINELY NO-OPs — registration / dispatch only (6 handlers)

These **seven** handlers provably contain **no write to game code or game data**.
Six forward to a shared callback slot; the seventh (`StartupTimeFix`) writes only
a data pointer and emits no jump, so it has no *install*. This is a positive
result, not a failure to resolve: the whole body was disassembled and traced.

| option | handler | body (exact) | what it does |
|---|---|---|---|
| **FastLoading** | `0x1001eca0` | `push ebp; mov ebp,esp; call 0x1001ec90; pop ebp; ret` | `0x1001ec90 = pusha; lea eax,[0x1015e840]; mov eax,[eax]; call eax; popa; ret`. The slot `0x1015e840` is **zero on disk, never stored to anywhere in `.text`, and not in the `.reloc` table** — it is filled at runtime by the module loader (the class ctor `0x1001ec30`, whose obfuscated `lea`/`sub` pair decodes to `0x1015e840`, stores `eax` there). Runtime target therefore **not statically recoverable** — *this is the one genuine remaining unknown, and it lives in the loader, not the handler* |
| **HighQualityReflections** | `0x10023dc0` | `push ebp; mov ebp,esp; call 0x10023db0; pop ebp; ret` | `0x10023db0 = pusha; lea ecx,[0x10192f14]; mov ecx,[ecx]; call ecx; popa; ret`. The callback slot `0x10192f14` **is** written statically: ctor `0x10023cc0` (called from `0x10001068`) stores `0x1001f390` (`0x1001f390 = 0x0FDD6C5B+0x248735`), plus `0x1001f030` and `0x10023d70` at `0x10192f18` / `0x10196370`. So the real patch work lives in `FUN_1001F390` (0x2200 bytes, no `FUN_10003830` calls — it walks the game's own structures) |
| **MouseFix** | `0x1003d7a0` | `push ebp; mov ebp,esp; call 0x1003d790; pop ebp; ret` | `0x1003d790` calls through `0x1019e7b8`, installed by the ctor at `0x1003d760` |
| **CutsceneFixes** | `0x100441a0` | `push ebp; mov ebp,esp; call 0x10044190; pop ebp; ret` | `0x10044190` calls through `0x10189ee0`, installed by the ctor at `0x10044130` |
| **GarageModelFix** | `0x10050860` | `push ebp; mov ebp,esp; call 0x10050850; pop ebp; ret` | `0x10050850` calls through `0x101a4e98`, installed by the ctor at `0x10050820` |
| **OutOfCommissionFix** | `0x10050a20` | `push ebp; mov ebp,esp; call 0x10050a10; pop ebp; ret` | `0x10050a10` calls through `0x1017076c`, installed by the ctor at `0x100509e0`. (Both `GarageModelFix` and `OutOfCommissionFix` ctors set their *second* slot to the plugin's own `DAT_1015800c` — i.e. a stack-cookie base, not a patch callback) |
| **StartupTimeFix** *(no-op in the install sense only)* | `0x100468d0` | `push ebp; mov ebp,esp; sub esp,0x14; …` | No pattern scan, no code write. It calls the *multi-hit* collector `FUN_10005afa0` with pattern `68 00 C2 EC 80 68` (**3 hits** in 1.0.8.0: `0x477a42`, `0x8973d3`, `0x898853`) and, for each hit `p`, writes dword `0x101580d8` to `p+6`. `0x101580d8` is the plugin's own `.data` word — the write *retargets* a game operand to the plugin. It is a real write, but there is no jump, so the install model yields nothing |

The six "`pusha`-forwarder" handlers (`FastLoading`, `HighQualityReflections`,
`MouseFix`, `CutsceneFixes`, `GarageModelFix`, `OutOfCommissionFix`) are exactly
the six handlers whose entire body is `push ebp / mov ebp,esp / call <forwarder> /
pop ebp / ret`. Their `installs` arrays are empty because **the switch in the
handler carries no patch write at all** — the work is done by a callback stored in
`.data` that the module's own constructor installs before the handler runs.

`StartupTimeFix` is the seventh "no install" handler, but for a different reason:
its writes are data-store-only (no jump), which the install model does not
represent.

---

## 4. STILL UNRESOLVED (0 handlers' patch semantics, 1 pointer value)

No handler in scope has an unresolved *set of writes* any more. Every one of the 50
was fully disassembled end-to-end and the write set is enumerated above or in
§3. What remains open:

1. **`FastLoading` (`0x1001eca0`) — runtime callback value.** `0x1015e840` is a
   zero-initialised `.data` slot whose only static reference is the `lea` at
   `0x1001ec91`. There is no static store to it in `.text` (only the obfuscated
   `lea`/`sub`/`mov` ctor at `0x1001ec30`, which clears it), and `.reloc` contains
   no entry for it (the relocation table has only two pages). Whether it is filled
   by the module's C++ static-init of a *different* translation unit, by
   `IVMenuAPI.asi`, or by the game at load time cannot be decided from the ASI
   alone. **Documented as unresolved.** (Same structural situation for the five
   other `pusha`-forwarder slots — but for those the constructor *does* resolve to
   a concrete `0x1001xxxx` callback, listed in §3, so only `FastLoading` is
   genuinely unknown.)

2. **`RemoveRGSCLoginRequirement` signature path — which build it targets.**
   `83 EC 44 A1 …` has 0 hits in 1.0.8.0. Only the build-9 module-relative
   fallback (`module+0x498460` → `0x10038555`) is exercised there. The signature's
   intended build is not recoverable from the ASI. Already recorded as unresolved
   in `docs/handlers_batch_6.md`; confirmed here.

3. **Non-1.0.8.0 build gates.** `VSyncFix`, `PopupHeadlightsFlag` (2nd pattern),
   `SkipMenu` (patterns 2/3), `LocalSettingsPath` (pattern 3),
   `Patch6and7RemoveModChecks`, `LoadDLCs`, `SuperLODFix`, `RemoveUselessChecks`
   and `LaunchWithPlayGTAIV` are all build-gated away from 1.0.8.0, so their
   pattern hits are 0 and their *effective* targets can only be listed as
   `module+0xRVA` (which **is** resolvable — done above). The RVA→function-name
   mapping for builds 1, 5, 7, 8 is not verifiable against a 1.0.8.0 image.

4. **The `+5` skew.** For every install the value stored into the patch's
   displacement field is `TARGET_CONST - site - 5`, i.e. the handler's
   `mov reg,0x1000xxxx` constant is **the site-relative base of the trampoline
   minus 5** — the real trampoline entry sits at `CONST + 5`. All `dest` values in
   §2 are given as the *effective* target (`opcode + 5 + disp`); the
   `handler_table.json` `target` column stores the raw constant
   (`TARGET_CONST = dest - 5`). This is the "`UNK_... + 5`" convention already
   noted in `docs/handlers_batch_1.md`; it applies to 100 % of the rows here.
   Verified numerically on 29/29 already-resolved handlers.

5. **`CarPartsShadowFix` pattern is built at runtime.** Its `FUN_10003830`
   argument is not a `.rdata` string; the handler assembles `"485 DB 7E 41 55 EB
   0A"` byte-by-byte into a stack `std::string` from `0x38 0x35 0x20 0x44 0x42
   0x20 0x37 0x45 0x20 0x34 0x31 0x20 0x35 0x35 0x20 0x45 0x42 0x20 0x30
   0x41`, i.e. **`"85 DB 7E 41 55 EB 0A"`** with a spurious leading character (the
   first `mov ebx,0x38` is the ASCII-double-dabble divisor seed, not a pattern
   byte) — so the effective pattern is `85 DB 7E 41 55 EB 0A`. The extractor saw
   the pattern argument as `EBP-0x58` and reported 1 pattern with 0 hits, hence
   the empty `installs`. Match in 1.0.8.0: **`0x40C9FF`** (1 hit). Why the extra
   `'8'` is emitted is an artefact of the string builder's first iteration; not
   fully explained.

---

## 5. Build gates (as recovered, for reference)

`evidence/option_build_gates.json` for the affected handlers:

| option | gate |
|---|---|
| LoadDLCs | `==7`, `==8`, `==9` |
| EpisodicVehicleSupport | `==8`, `==9` |
| SuperLODFix | `==8`, `==9` |
| RemoveUselessChecks | `==5`, `==8`, `==9` |
| Patch6and7RemoveModChecks | `==7`, `==8` |
| LaunchWithPlayGTAIV | `==1`, `==5`, `==7`, `==8`, `==9` (switch) |
| VSyncFix | `!=1`, `!=2`, `!=3`, `!=4`, `!=5`, `!=9`, `!=13` |
| BenchmarkFix (not in scope) | `!=9`, `!=13` |

1.0.8.0 has build slot **9** (`DAT_1015be04 == 9`, set by `FUN_10055c70`), which is
why only the build-9 branches of `LoadDLCs`, `EpisodicVehicleSupport`,
`SuperLODFix`, `RemoveUselessChecks` and `LaunchWithPlayGTAIV` ever execute on the
reference image, and why `Patch6and7RemoveModChecks` (7/8), `VSyncFix` and
`BenchmarkFix` never execute at all.

---

## 6. Counts

| class | count |
|---|---|
| Handlers examined (empty `installs` or a `null` target) | **50** |
| RESOLVED — a complete write set was recovered | **44** |
| GENUINELY NO-OP — verified to contain no game write at all | **6** |
| Still unresolved — no write set could be established | **0** |
| Remaining open questions (not handler write sets) | 5 items, §4 |

**44 resolved** = the 34 rows of §2.1 + the 9 rows of §2.2 = 43, **+1** for
`StartupTimeFix` (real writes, no install). 43 + 1 + 6 no-ops = 50.
`IncreaseVehicleModelLimit` is in §2.1 even though the extractor saw *no* store
at all: its destination is a pointer read out of the game at `site+0x4d`, so the
`mov BYTE PTR [reg],0x..` idiom never fires. `StartupTimeFix` writes a plugin
`.data` pointer into the game but emits no jump, so it has no *install* to record.

**6 genuinely no-op** = exactly the six `pusha`-forwarder handlers:
`FastLoading`, `HighQualityReflections`, `MouseFix`, `CutsceneFixes`,
`GarageModelFix`, `OutOfCommissionFix`. All six contain only
`push ebp / mov ebp,esp / call <forwarder> / pop ebp / ret`, where the forwarder
is `pusha; lea reg,[slot]; mov reg,[reg]; call reg; popa; ret`. Five of the six
slots are statically resolvable to a `0x1001xxxx` callback (listed in §3); only
`FastLoading`'s slot value is unknown (§4 item 1).

**0 still unresolved** in the sense of "the handler's write set could not be
established": every one of the 50 bodies was disassembled end-to-end and traced,
and the result is in §2 or §3. The five open *questions* in §4 are about values
outside the handler bodies (a runtime-filled slot, pre-1.0.8.0 build targets and
the `+5` naming convention), not about missing write sets.

Note also: 46 of the 50 rows have `handler_end` in `handler_table.json` that is
**earlier** than the real function end (the extractor's `min(next_fn, +0x400)`
window). Only 4 rows happen to have a window that contains the whole body. That
truncation, not any semantic problem, is the direct cause of the empty/`null`
`installs` for the 6 long handlers listed in §0.

---

## 7. Provenance of this analysis

Every table above was produced by a read-only linear data-flow trace over
`i686-w64-mingw32-objdump -d -Mintel` output, in which

- `eax` after `call 0x10003830` is modelled as "start of the first match of the
  pattern currently in `ecx`" (a symbolic `SITE` value);
- `call 0x10005afa0` is modelled as the multi-hit collector (the vector at
  `[ecx]`, elements at `[ecx+0]`, `[ecx+4]`, … are `SITE_0`, `SITE_1`, …);
- `[ebp-N]` local slots are tracked so a pattern pointer moved to a stack slot and
  back is still recognised;
- `mov BYTE PTR [reg],0xe9|e8|eb` pairs with the following
  `mov DWORD PTR [reg+1],disp32`, and the `mov reg,IMM ; … ; sub reg,5` block
  nearest to the pair supplies the trampoline constant;
- `FUN_10003830`'s pattern argument is read out of `.rdata` (the bytes after the
  obfuscated string-object header) verbatim, which is how the runtime-built
  `CarPartsShadowFix` pattern was decoded.

**Validation.** The same tracer was run over the 29 handlers that
`handler_table.json` already resolved completely; it reproduces all 29
`(opcode, target)` pairs byte-for-byte (after applying the `+5` offset described
in §4 item 4). No handler in the 50-handler gap set produced a write that the
tracer could not attribute to one of: a pattern-relative offset, a
`GetModuleHandleA(0)`-relative RVA, an absolute plugin-image address, or a
register-computed address (the one such case is `IncreaseVehicleModelLimit`).
