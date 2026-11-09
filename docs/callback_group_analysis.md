# Dynamic-callback group — offline RE report (for the main agent)

Repo: D:\GitHub\OpenZDecomp\decomp — READ-ONLY analysis. Original ASI base 0x10000000
(.text raw 0xC00), game GTAIV.exe 1.0.8.0 (file offset == RVA, base 0x400000).
All raw bytes below were produced with capstone from `game_refs/ZolikaPatch_original.asi.off`;
every pattern string was reconstructed by emulating the plugin's char-by-char string builder
(see §1) and then re-verified by scanning GTAIV.exe.

## 0. The loader arms — exact answer to the sharing question

`sub_100523C0` (called from DllMain 0x10055E1D) reads each INI key (reversed .rdata literals
reversed back by `sub_10003B20`), then calls a per-option thunk. Verified line numbers in
`ZolikaPatch_decompiled/ida/100523C0_sub_100523C0.c`:

| option | loader line | key literal in binary | thunk called | default |
|---|---|---|---|---|
| HighQualityReflections | 2127 | `"HighQualityReflections"` (plain literal, 2120) | `sub_10023DC0()` | "0" |
| CutsceneFixes | 2237 | `"sexiFenecstuC"` (reversed, 2224) | `sub_100441A0()` | "1" |
| GarageModelFix | 2388 | `"xiFledoMegaraG"` (reversed, 2376) | `sub_10050860()` | "1" |
| MouseFix | 2441 | `"xiFesuoM"` (reversed, 2429) | `sub_1003D7A0()` | "1" |
| OutOfCommissionFix | 2499 | `"xiFnoissimmoCfOtuO"` (reversed, 2486) | `sub_10050A20()` | "1" |
| PathNodeCrashFix | 1668 | `"PathNodeCrashFix"` (plain literal, 1661) | `sub_1003D7D0()` | "1" |
| PedModelsInCutscenesFix | 2549 | `"PedModelsInCutscenesFix"` (plain, 2542) | `sub_100508C0()` | "1" |

**MouseFix and PathNodeCrashFix do NOT share an installer.** The arm at line ~2440 reads
`MouseFix` (literal `xiFesuoM`) and calls `sub_1003D7A0`; PathNodeCrashFix has its own arm at
line 1661-1668 calling `sub_1003D7D0` (already implemented: E9 at the `8B 04 B1 ...` match to
`path_node_shim` 0x1003D7B0). `PedModelsInCutscenesFix` → `sub_100508C0` (E9 at its match to
0x10050870; already implemented). The `if (v402 == 1) sub_1003D7A0();` at line 2441 is the
MouseFix arm.

## 1. The forwarding mechanism (verified)

Each of these thunks is `push ebp; mov ebp,esp; call FWD; pop ebp; ret`, where FWD is
`pushal; lea eax,[slot]; mov eax,[eax]; call eax; popal; ret` — i.e. an indirect call through a
BSS function-pointer slot. The slot value is installed by a static ctor (`this`-relative stores,
obfuscated `lea+add`); a second static "clear" function zeroes the slot(s); the callbacks call
their clear function at the end, so each is a **one-shot self-clearing callback** that runs once
during config load:

| option | thunk | forwarder | slot | callback (real worker) | clear fn (2nd slot) |
|---|---|---|---|---|---|
| MouseFix | 0x1003D7A0 | 0x1003D790 | 0x1019E7B8 | **0x10024600** (0x12882 bytes, 22683 insns) | 0x1003D760 (also clears 0x101A0804) |
| GarageModelFix | 0x10050860 | 0x10050850 | 0x101A4E98 | **0x10046CC0** (0x3DBD, 4077 insns) | 0x10050820 (also clears 0x101A8300) |
| OutOfCommissionFix | 0x10050A20 | 0x10050A10 | 0x1017076C | **0x1004C3F0** (0x3FB7, 4185 insns) | 0x100509E0 (also clears 0x101806F0) |
| CutsceneFixes | 0x100441A0 | 0x10044190 | 0x10189EE0 | **0x10044160** (10 insns) | 0x10044130 (also clears 0x1018C22C, 0x10185F88) |
| HighQualityReflections | 0x10023DC0 | 0x10023DB0 | 0x10192F14 | **0x1001F390** (0x3952, 3685 insns) + **0x1001F030** (0x360) | 0x10023D70 (also clears 0x10192F18, 0x10196370) |

Ctor evidence (raw): MouseFix ctor `sub_1003D6D0`: `lea eax,[0xFF9B2BD]; add eax,0x89343` →
0x10024600 stored at `[0x1019E7B8]` (`lea eax,[0xFFBB41B]; add eax,0x82345` → 0x1003D760 at
[0x101A0804]). Garage ctor `sub_10050790`: 0xFD0155B+0x345765 = 0x10046CC0 → [0x101A4E98].
OOC ctor `sub_10050950`: 0xF817419+0x834FD7 = 0x1004C3F0 → [0x1017076C].
Cutscene ctor `sub_100440A0`: 0xFFCED3D+0x75423 = 0x10044160 → [0x10189EE0].
HQR ctor `sub_10023CC0`: `this[2325]=0x1001F390` (this=0x10190AC0, +0x2454=0x10192F14),
`this[2326]=0x1001F030` (+0x2458=0x10192F18), `this[3350]=0x10023D70` (+0x58B0=0x10196370).

### The obfuscated string builder (how the pattern strings were recovered)

Inside each callback every pattern string is assembled one character at a time:
- a literal (`push <rdata>` + `call sub_10057870/0x10057840/0x10058620` = std::string ctor/assign),
- then per character: a seed char code is loaded (`mov ebx,0xNN`), converted to decimal text by
  the magic div-10 loop (`mov eax,0xCCCCCCCD; ... mul ebx; shr edx,3`), parsed back by
  `strtol(text,&end,10)` (IAT 0x100632D8), and appended/reset:
  - `sub_100585D0` = `push_back(char)` (append),
  - `sub_100577C0` = `append(char)` (append, with capacity growth),
  - `sub_1005CF20` = `append(char)` (capacity-growth helper),
  - **`sub_10057810` = `operator=(char)` — a RESET to a single char** (this was the key to
    decoding: the strings are NOT cumulative).
The seed value IS the ASCII code of the emitted character. That means the strings can be
reconstructed by reading the `mov ebx,imm` seeds in address order. All strings below were
reconstructed this way and verified by scanning the game.

### The scan/patch helper vocabulary (all verified by decompile)

- `sub_10003830(ecx=pattern cstr, edx=module)` → first match VA (0 if none). Internally
  `sub_1005AFA0(&vec, pattern, module, 1)` + take `*vec.begin()`.
- `sub_1005B7D0(ctx, pattern, module)` / `sub_1005B6A0(ctx, pattern)` — build a scan context over
  the module (K32GetModuleInformation + pattern compile). Callers then use `sub_10056F80`
  (has-match) / `sub_10056F70` (first match) — used for the "module-relative" installs.
- `sub_1005B750(site)` = write **E9** at site, dest **0x10024060**, return `site + *(site+1) + 5`.
- `sub_1005B980(site)` = write **E8** at site, dest **0x10024120**.
- `sub_1005BA80(site)` = write **E8** at site, dest **0x1004C350**, return original call target.
- `sub_1005B5D0(site, dest)` = write **E9** at site to arbitrary dest.
- `sub_1005B650(site, dword)` / `sub_1005ADB0(site, word)` / `sub_1005AD60(site, byte)` =
  VirtualProtect + raw write.
- `sub_1005AD50(this)` = `this+0x15` (accessor), `sub_10001670(p)` = `p + *(p+1) + 5`
  (resolve an E8/E9 at p to its target).

## 2. MouseFix — callback 0x10024600 (the big one)

### 2.1 Write set (all verified; match = first match of the pattern in GTAIV.exe 1.0.8.0)

| # | scan VA (in cb) | pattern string (verified match VA) | installs / publishes |
|---|---|---|---|
| 1 | 0x10027C2E | `51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C` → **0x8A9E10** (1 hit) | publish `[0x10159C50] = *(match+0xD)`; `[0x10159C3C] = *(match+0x14)`; **E9 at match+0 → 0x10024060** |
| 2 | 0x1002976E | `8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8` → **0x8AA190** (1 hit) | **E9 at match+0 → 0x100240C0** |
| 3 | 0x1002B6EE | `83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07` → **0xA84C1F** (1 hit) | raw writes (direct VirtualProtect): **+3 = `90 90`**, **+5 = `66 0F 6E C0`**, **+0x1F = `90 90`**, **+0x21 = `66 0F 6E C0`** |
| 4 | 0x1002D9B2 | `83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07` → **0xA7CCF7** (1 hit) | same 4 writes via helpers: +3 `90 90`, +5 `66 0F 6E C0`, +0x1F `90 90`, +0x21 `66 0F 6E C0` |
| 5 | 0x1002F442 | `83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46` → **0xA9AB43** (1 hit) | +3 `90 90`, +5 `66 0F 6E C8` (0xC86E0F66) |
| 6 | 0x10033CD2 | `8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D` → **0x8AA1B0** (1 hit) | publish `[0x10159C5C] = *(match+0x14)` (= game global 0x010FC0D0); **E9 at match+0 → 0x10024170** |
| 7 | 0x10035D62 | `56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00` → **0xB4D230** (1 hit) | **E8 at match+0x33 → 0x10024120**; **E8 at match+0x5A → 0x10024120** |
| 8 | 0x10036E30 | `83 EC 08 DB 05 ? ? ? ? 68` → **0xB4D3A0** (1 hit) | publish `[0x10159C34] = match` (conditional: `cmovne`, only if found) |
| M | 0x10031666 (`sub_1005B7D0` module scan; pattern built in slot [ebp-0x29B0]) | `53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46` → **0xA81B1F** (1 hit) | publish `[0x10159C60] = match`; **E9 at match-0xF → 0x10024560**; publish `[0x10159C40] = match-0xF+0x765` = **match+0x756** (game 0xA82275); **E9 at match-0xF+0x755 = match+0x746 → 0x100243A0** (game 0xA82265) |

Note for #8: the scan result only overwrites `[0x10159C34]` when found; body 0x10024440 calls
`[0x10159C34]`, so the original tolerates a 0 (call null would crash — in practice the pattern
matches on 1.0.8.0).

Cross-check with the *old* ZolikaPatch source (`ZolikaPatch/dllmain.cpp:1854-1880`): MouseFix
there reads `ADDRESS_MOUSE_X/Y` from the same pattern at +13/+20 — identical semantics
(match+0xD/+0x14), confirming the decode.

### 2.2 Destination bodies (small, compiled C — port as compiled helpers)

All of these read the published mouse pointers `[0x10159C50]` (X) / `[0x10159C3C]` (Y), the
float constant `[0x10152318]` (1/65536 or similar in .rdata), `[0x10152378]`, `[0x1015238C]`,
`[0x10158044]` (sign bit / float in .data), `[0x101523F0]` (xmm sign-flip mask),
`[0x10159C5C]` and `[0x10159C34]`.

- **0x10024060** (0x5A bytes) — `float axis(int which)`: which==0 → `*(float*)X * [0x10152318]`
  (via cvtdq2pd → mulsd → fld); which==1 → same for Y; else `fldz` (0.0).
- **0x100240C0** (0x51) — same as above but multiplies by `-[0x10158044]` (`xorps [0x101523F0]`)
  and returns a float in xmm0/eax.
- **0x10024120** (0x4A) — `int axis(int which)`: `(int)(val * [0x10152318] * [0x10152378])`.
- **0x10024170** (0x68) — float variant; additionally, if `*[0x10159C5C] != 0` multiplies by
  `[0x1015238C]`.
- **0x100243A0** (0x9D, naked, byte-transcribable) — saves xmm0..7 to [0x10159C4C,0x64,0x48,
  0x30,0x38,0x54,0x58,0x44], `call 0x100241E0`, restores xmm0..7, then
  `mulss xmm0,xmm2; addss xmm0,xmm3; movss xmm3,[esi+0x150]; jmp [0x10159C40]`.
  Exact bytes:
  ```
  60 F3 0F 11 05 4C 9C 15 10 F3 0F 11 0D 64 9C 15 10 F3 0F 11 15 48 9C 15 10
  F3 0F 11 1D 30 9C 15 10 F3 0F 11 25 38 9C 15 10 F3 0F 11 2D 54 9C 15 10
  F3 0F 11 35 58 9C 15 10 F3 0F 11 3D 44 9C 15 10 E8 FA FD FF FF
  F3 0F 10 05 4C 9C 15 10 F3 0F 10 0D 64 9C 15 10 F3 0F 10 15 48 9C 15 10
  F3 0F 10 1D 30 9C 15 10 F3 0F 10 25 38 9C 15 10 F3 0F 10 2D 54 9C 15 10
  F3 0F 10 35 58 9C 15 10 F3 0F 10 3D 44 9C 15 10 61
  F3 0F 59 C2 F3 0F 58 C3 F3 0F 10 9E 50 01 00 00 FF 25 40 9C 15 10
  ```
- **0x10024560** (0x9C, naked, byte-transcribable) — same xmm save/restore around
  `call 0x10024440`, then replays the hooked function prologue and resumes:
  ```
  60 <8x movss saves as above> E8 9A FE FF FF <8x movss restores> 61
  55 8B EC 83 E4 F0 0F 57 C0 81 EC 94 00 00 00 FF 25 60 9C 15 10
  ```
- **0x100241E0** (0x1BB) and **0x10024440** (0x114) — larger math/helper bodies called by the
  naked ones (mouse delta → angle/velocity math); port as compiled helpers, structure:
  read [0x10159C50]/[0x10159C3C], [0x10159C34] (time), [0x10159C5C], [0x10159C30..0x10159C64]
  saved xmm slots, constants at 0x10152318/28/30/60/68/A0/B0/E0/F0 and writes `[esi+0x144/0x148]`.
  Size estimate: ~0x2D0 bytes total of C.

### 2.3 Ready-to-paste naked bodies (with rebased globals)

Following the repo convention (C-linkage `zpatch_*` globals + `rebased_slot_for` for published
values), the two naked bodies become:

```cpp
// MouseFix: resume slot published as match (game 0xA81B1F); dest of E9 at match-0xF.
extern "C" { extern void* zpatch_mf_resume_a; }      // 0x10159C60 -> game 0xA81B1F
extern "C" { extern void* zpatch_mf_resume_b; }      // 0x10159C40 -> game 0xA82275
extern "C" { extern float zpatch_mf_xmm_save[8]; }   // 0x10159C4C.. (8 dwords, not contiguous!)

// @exact 0x10024560 (byte transcription; keep the 8 movss saves/restores exactly)
__declspec(naked) void mf_body_a() {
    __asm {
        pushal
        movss dword ptr[zpatch_mf_xmm_save[0]], xmm0
        movss dword ptr[zpatch_mf_xmm_save[1]], xmm1
        movss dword ptr[zpatch_mf_xmm_save[2]], xmm2
        movss dword ptr[zpatch_mf_xmm_save[3]], xmm3
        movss dword ptr[zpatch_mf_xmm_save[4]], xmm4
        movss dword ptr[zpatch_mf_xmm_save[5]], xmm5
        movss dword ptr[zpatch_mf_xmm_save[6]], xmm6
        movss dword ptr[zpatch_mf_xmm_save[7]], xmm7
        call mf_angle_body            // 0x10024440, compiled helper
        movss xmm0, dword ptr[zpatch_mf_xmm_save[0]]
        /* ... xmm1..xmm7 restores ... */
        popal
        push ebp
        mov ebp, esp
        and esp, -16
        xorps xmm0, xmm0
        sub esp, 0x94
        jmp dword ptr[zpatch_mf_resume_a]   // 0x10159C60 = match (0xA81B1F)
    }
}
```
(0x100243A0 likewise: saves/restores around `call 0x100241E0`, then
`mulss xmm0,xmm2; addss xmm0,xmm3; movss xmm3,[esi+0x150]; jmp [zpatch_mf_resume_b]`.)

The other four destinations are ordinary C functions — reimplement as compiled helpers, e.g.:

```cpp
extern "C" int* zpatch_mf_mouse_x;   // 0x10159C50
extern "C" int* zpatch_mf_mouse_y;   // 0x10159C3C
extern "C" const double zpatch_mf_scale;      // 0x10152318
extern "C" const double zpatch_mf_scale2;     // 0x10152378
extern "C" const float  zpatch_mf_scale3;     // 0x1015238C
extern "C" const float  zpatch_mf_sign;       // 0x10158044 (xorps [0x101523F0] flips it)
extern "C" char* zpatch_mf_flag;     // 0x10159C5C (points at game 0x010FC0D0)
```

### 2.4 MouseFix JSON rows

```json
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C",
 "site_offset":0,"opcode":"0xE9","dest_kind":"module_va","dest":"0x10024060","dest_symbol":"sub_10024060",
 "why":"cb 0x10024600 scans this (1 hit game 0x8A9E10), E9s the game function entry to the axis-float body"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8",
 "site_offset":0,"opcode":"0xE9","dest_kind":"module_va","dest":"0x100240c0","dest_symbol":"sub_100240C0",
 "why":"1 hit game 0x8AA190 (caller of 0x8A9E10)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":3,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x90","why":"NOP the neg eax (game 0xA84C22)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":4,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x90","why":"second NOP byte"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":5,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x66",
 "why":"replace cvtsi2ss with movd (bytes 66 0F 6E C0 at +5..+8)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":6,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x0f"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":7,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x6e"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":8,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0xc0"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":31,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x90"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":32,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x90"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":33,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x66"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":34,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x0f"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":35,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x6e"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07",
 "site_offset":36,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0xc0"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07",
 "site_offset":3,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x90",
 "why":"second site (game 0xA7CCF7): same 4-write pattern; rows +3/+4/+5/+6/+7/+8 and +0x1F..+0x24 as above"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07",
 "site_offset":5,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x66",
 "why":"(expand to 66 0F 6E C0; +0x1F/+0x21 pairs likewise)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46",
 "site_offset":3,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x90",
 "why":"third site (game 0xA9AB43): +3 90 90; +5 66 0F 6E C8 (0xC86E0F66)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46",
 "site_offset":5,"opcode":"0x0","dest_kind":"literal_byte","dest_byte":"0x66"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D",
 "site_offset":0,"opcode":"0xE9","dest_kind":"module_va","dest":"0x10024170","dest_symbol":"sub_10024170",
 "why":"game 0x8AA1B0; publishes [0x10159C5C]=*(match+0x14)=game 0x010FC0D0"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00",
 "site_offset":51,"opcode":"0xE8","dest_kind":"module_va","dest":"0x10024120","dest_symbol":"sub_10024120",
 "why":"game 0xB4D230; E8 at match+0x33 (row opcode lowercase e8 also accepted by the applier)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00",
 "site_offset":90,"opcode":"0xE8","dest_kind":"module_va","dest":"0x10024120","dest_symbol":"sub_10024120",
 "why":"E8 at match+0x5A"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46",
 "site_offset":-15,"opcode":"0xE9","dest_kind":"module_va","dest":"0x10024560","dest_symbol":"sub_10024560",
 "why":"module-relative path (sub_1005B7D0): match 0xA81B1F, E9 at match-0xF (function entry) to the naked body; publishes [0x10159C60]=match"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix",
 "signature":"53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46",
 "site_offset":1862,"opcode":"0xE9","dest_kind":"module_va","dest":"0x100243a0","dest_symbol":"sub_100243A0",
 "why":"E9 at match+0x746 (game 0xA82265); publishes [0x10159C40]=match-0xF+0x765 (game 0xA82275, resume)"}
```

Slot rows:

```json
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix","signature":"51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C",
 "slot_name":"dword_10159C50","kind":"match_deref","field_offset":13,"add":0,"why":"publishes *(match+0xD) = game ADDRESS_MOUSE_X (0x017945D0)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix","signature":"51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C",
 "slot_name":"dword_10159C3C","kind":"match_deref","field_offset":20,"add":0,"why":"publishes *(match+0x14) = game ADDRESS_MOUSE_Y (0x017945C0)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix","signature":"8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D",
 "slot_name":"dword_10159C5C","kind":"match_deref","field_offset":20,"add":0,"why":"publishes *(match+0x14) = game 0x010FC0D0 (negate flag read by body 0x10024170)"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix","signature":"83 EC 08 DB 05 ? ? ? ? 68",
 "slot_name":"dword_10159C34","kind":"match","add":0,"why":"publishes the match (game 0xB4D3A0, frame-time fn called by body 0x10024440); cmovne = only when found"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix","signature":"53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46",
 "slot_name":"dword_10159C60","kind":"match","add":0,"why":"publishes match (0xA81B1F) = resume for naked body 0x10024560"},
{"handler":"0x1003d7a0","callback":"0x10024600","option":"MouseFix","signature":"53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46",
 "slot_name":"dword_10159C40","kind":"match","add":1878,"why":"publishes match-0xF+0x765 = match+0x756 (0xA82275) = resume for naked body 0x100243A0"}
```

## 3. GarageModelFix — callback 0x10046CC0 (2 sites, both fully decoded)

**Scan 1** @0x100499E1: pattern `8B 57 20 D9 42 34 D9 5E 04 8B 47 20` → **0xC68BB0** (1 hit):
```
publish [0x10159CD4] = match+6            ; 0xC68BB6
VirtualProtect(match,1,0x40); *(BYTE*)match = 0xE9
VirtualProtect(match+1,4,0x40); *(DWORD*)(match+1) = 0x10046C80 - match - 5
```
Destination body **0x10046C80** (0x17 bytes, naked, byte-exact):
```
60 8B CE 8B D7 E8 06 FF FF FF 61 8B 57 20 D9 42 34 FF 25 D4 9C 15 10
pushal; mov ecx,esi; mov edx,edi; call 0x10046B90; popal;
mov edx,[edi+0x20]; fld [edx+0x34]; jmp [0x10159CD4]   ; replays 6 original bytes, resumes at match+6
```
(`0x10046B90` is a compiled helper, port as a function.)

**Scan 2** @0x1004AA38: the distinctive tail `0F B7 0E 50 51 E8` verifies to exactly 1 hit
**0x9659C3** (the callback's string slot accumulates, but the matching string is this tail; the
`8B 57 20...` prefix does NOT precede it in the game):
```
lea ecx,[match+5]; mov [0x10159CD0],ecx   ; publish resume = match+5 (0x9659C8)
mov edx,0x10046CA0; mov ecx,match; call sub_1005B5D0   ; E9 at match -> 0x10046CA0
```
Destination body **0x10046CA0** (0x12 bytes, naked, byte-exact):
```
60 E8 2A FF FF FF 61 0F B7 0E 50 51 FF 25 D0 9C 15 10
pushal; call 0x10046BD0; popal; movzx ecx,word ptr [esi]; push eax; push ecx; jmp [0x10159CD0]
```
(`0x10046BD0`, 0xA3 bytes, compiled helper; the body replays the 5 original bytes at the match
and resumes at match+5 = the call 0x8B4D60.)

The callback ends by calling its clear fn `[0x101A8300]` (self-clear). JSON rows:
```json
{"handler":"0x10050860","callback":"0x10046cc0","option":"GarageModelFix",
 "signature":"8B 57 20 D9 42 34 D9 5E 04 8B 47 20","site_offset":0,"opcode":"0xE9",
 "dest_kind":"module_va","dest":"0x10046c80","dest_symbol":"sub_10046C80",
 "why":"scan 1 (1 hit 0xC68BB0): E9 at match to naked body; publishes [0x10159CD4]=match+6"},
{"handler":"0x10050860","callback":"0x10046cc0","option":"GarageModelFix",
 "signature":"8B 57 20 D9 42 34 D9 5E 04 8B 47 20",
 "slot_name":"dword_10159CD4","kind":"match","add":6,
 "why":"publishes match+6 (0xC68BB6) = resume for body 0x10046C80"},
{"handler":"0x10050860","callback":"0x10046cc0","option":"GarageModelFix",
 "signature":"0F B7 0E 50 51 E8","site_offset":0,"opcode":"0xE9","dest_kind":"module_va",
 "dest":"0x10046ca0","dest_symbol":"sub_10046CA0","why":"scan 2: E9 at match (1 hit 0x9659C3) to the naked body"},
{"handler":"0x10050860","callback":"0x10046cc0","option":"GarageModelFix",
 "signature":"0F B7 0E 50 51 E8","slot_name":"dword_10159CD0","kind":"match","add":5,
 "why":"publishes match+5 (0x9659C8) = resume for body 0x10046CA0 (publish at 0x1004AA49, immediately before the E9)"}
```
Ready-to-paste naked bodies (rebase slots via `rebased_slot_for("dword_10159CD4"/"dword_10159CD0")`):
```cpp
// @exact 0x10046C80
__declspec(naked) void garage_body_a() {
    __asm { pushal
            mov ecx, esi
            mov edx, edi
            call garage_helper_b90      // 0x10046B90
            popal
            mov edx, dword ptr [edi + 0x20]
            fld dword ptr [edx + 0x34]
            jmp dword ptr [zpatch_garage_resume_a] }   // dword_10159CD4
}
// @exact 0x10046CA0
__declspec(naked) void garage_body_b() {
    __asm { pushal
            call garage_helper_bd0      // 0x10046BD0
            popal
            movzx ecx, word ptr [esi]
            push eax
            push ecx
            jmp dword ptr [zpatch_garage_resume_b] }   // dword_10159CD0
}
```

## 4. OutOfCommissionFix — callback 0x1004C3F0

- Two module-scan contexts: `sub_1005B7D0` @0x1004D5CC with pattern `a9 39 44 24 18` (verified:
  **1 hit 0xA22E90**) and `sub_1005B6A0` @0x100502F8 with the longer string built in slot
  [ebp-0x520] (`a9 39 44 24 18 E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56` as reconstructed — the
  non-wildcard prefix matches, but the reconstructed continuation does not exist verbatim at
  0xA22E90; the exact tail bytes still need one more pass. The *mechanics* are verified.)
- On match (0x100502FD..0x1005031B):
  ```
  mov edx,eax                     ; match
  lea ecx,[edx-0x33]              ; site = match-0x33 (an E8 call in the game)
  call sub_10001670               ; eax = target of that call
  mov [0x10159BF8],eax            ; publish original target #1
  mov ecx,edx
  call sub_1005BA80               ; E8 at match -> 0x1004C350; returns original call target
  mov [0x10159C04],eax            ; publish original target #2
  ```
- Destination **0x1004C350** (0x70 bytes, compiled, port as helper): if `sub_1004B750(arg)`
  (0xBFF bytes, a lookup) returns non-null it formats a string via `sub_10001280` with the
  literals at 0x1012EB0C/0x1012EB58 and calls `[0x10159BF8]` (a MessageBox-style logger via
  `[0x10159BF8]`/`[0x10159BF8]`); then tail-calls `[0x10159C04]` (the original game function) with
  the original arguments `[ebp+8]`, `[ebp+0xC]`. So OOC wraps one game function with a
  "log/fix then call original" trampoline.
- JSON rows (mechanics verified, pattern tail pending):
```json
{"handler":"0x10050a20","callback":"0x1004c3f0","option":"OutOfCommissionFix",
 "signature":"A9 39 44 24 18 E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56",
 "site_offset":0,"opcode":"0xE8","dest_kind":"module_va","dest":"0x1004c350","dest_symbol":"sub_1004C350",
 "why":"module scan (match 0xA22E90 candidate); E8 replaces the game call; publishes [0x10159BF8]=target of call at match-0x33 and [0x10159C04]=original call target"},
{"handler":"0x10050a20","callback":"0x1004c3f0","option":"OutOfCommissionFix",
 "signature":"A9 39 44 24 18 E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56",
 "slot_name":"dword_10159BF8","kind":"match_field","field_offset":-50,"add":-46,
 "why":"publishes match + *(match-0x32) + 5 ... i.e. the target of the E8 at match-0x33"},
{"handler":"0x10050a20","callback":"0x1004c3f0","option":"OutOfCommissionFix",
 "signature":"A9 39 44 24 18 E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56",
 "slot_name":"dword_10159C04","kind":"match_field","field_offset":1,"add":5,
 "why":"publishes match + *(match+1) + 5 = the original E8 target (saved before the E8 is overwritten)"}
```

## 5. CutsceneFixes — callback 0x10044160 (VERIFIED: effectively a no-op in 7.65)

Callback (10 insns, all raw bytes):
```
10044160: 55                push ebp
10044161: 8b ec             mov ebp, esp
10044163: 83 3d 88 5f 18 10 00   cmp dword ptr [0x10185F88], 0
1004416a: 74 0d             je 0x10044179
1004416c: b2 eb             mov dl, 0xeb
1004416e: 8b 0d 88 5f 18 10      mov ecx, dword ptr [0x10185F88]
10044174: e8 e7 6b 01 00    call 0x1005ad60        ; VirtualProtect+write byte 0xEB at *[0x10185F88]
10044179: ff 15 2c c2 18 10      call dword ptr [0x1018c22c]   ; = clear fn 0x10044130 (self-clear)
1004417f: 5d                pop ebp
10044180: c3                ret
```
- `0x10185F88` is written ONLY by the clear fn (zeroes it, obfuscated `lea/sub` at 0x1004414F)
  and read only here — **nothing ever publishes a site into it**, so the byte write is dead code
  and the callback only self-clears. The arm still runs when CutsceneFixes=1 but performs **no
  game write on 1.0.8.0**. (If a future build/loader sets 0x10185F88 externally, the intent is:
  write 0xEB (unconditional jmp) over a `74/75` opcode at the published site.)
- JSON: no sig_installs (empty write set); slot row is optional/diagnostic only:
```json
{"handler":"0x100441a0","callback":"0x10044160","option":"CutsceneFixes","signature":"",
 "slot_name":"dword_10185F88","kind":"match","add":0,
 "why":"NEVER published by the original 7.65: callback is a self-clearing no-op on 1.0.8.0 (site slot stays 0)"}
```

## 6. HighQualityReflections — feasibility / port plan (callback 0x1001F390)

- Installer arm: line 2120-2127, key `HighQualityReflections`, default "0", calls `sub_10023DC0`
  → forwarder → slot 0x10192F14 = callback **0x1001F390** (0x3952 bytes, 3685 insns).
- Second callback **0x1001F030** (0x360 bytes, 163 insns) is called from the same object
  (`[0x10192F18]`); it reads/writes `[0x10159D04]` (12 refs inside 0x1001F030 + 1 in 0x1001F390)
  and ends by reading `[0x10159CFC]`.
- Callback mechanics (from 0x10022C89 block, verified):
  - builds a pattern string char-by-char in slot [ebp-0x430] (seed list recoverable with the
    same builder emulation; first seeds give `4B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8 ...`
    — reconstruction incomplete, needs one more builder pass),
  - `sub_1005B6A0(ctx, pattern)` → module scan; if found (`esi`):
    `mov edx,[ebp-0x8F0]` (destination VA computed earlier in the function), `mov ecx,esi`,
    `call sub_1005B5D0` → **E9 at match → dest**, then publishes `[0x10159CFC] = match+0x13` and
    `[0x10159D04] = *(match+0xF) + 0x13 + match`.
- Size/feasibility: the two callbacks are ~15 KB of code but the structure is the same builder +
  scans + `sub_1005B5D0`/`sub_1005B7D0`/`sub_1005B6A0` helpers already implemented in the
  reconstruction for other options. Shared helpers with implemented features: the whole
  `sub_1005Bxxx`/`sub_10003830` family, `sub_1005AD50`, the reversed-key/INI readers.
  Unique work: 1 pattern string + the destination VA computation (`[ebp-0x8F0]`, module-relative
  RVA math) + the second callback's ~163 instructions (reads `[0x10159CFC]`/`[0x10159D04]`).
  Recommendation: port after the four above; it is mostly a copy of the OOC-style
  "module-scan → E9 to a plugin body + publish resumes" pattern, and the destination body
  (0x1001F030 / the E9 target) is where the real logic lives — that body must be decompiled
  (not yet done).

## 7. Tractability ordering and recommendation

| rank | feature | work (lines) | risk | why |
|---|---|---|---|---|
| 1 | **CutsceneFixes** | ~5 | none | verified no-op; register a no-op arm (or skip) and document. Zero game writes. |
| 2 | **GarageModelFix** | ~90 | low | 2 patterns (1 hit each) + 2 E9s + 2 naked bodies (0x17 + 0x12 bytes, byte-exact) + 2 small helpers (0x9E/0xA3) + 2 resume publishes. |
| 3 | **MouseFix** | ~400-600 | medium | 9 patterns all verified to unique game sites, 6 small destination bodies (2 naked byte-exact, 4 compiled), 8 publishes. Mechanical but large; every piece is verified above. |
| 4 | **OutOfCommissionFix** | ~150 | medium | mechanics verified (E8 wrap + 2 publishes + trampoline body); the pattern tail needs one more builder pass; body 0x1004C350 is small but calls 0x1004B750 (0xBFF) for its lookup. |
| 5 | **HighQualityReflections** | ~300+ | higher | 2 callbacks (~15 KB), pattern string + destination-VA math still to decode; plan in §6. |

**Recommendation: implement in that order, starting with CutsceneFixes (no-op) then
GarageModelFix** — Garage is the smallest real feature (one 12-byte naked body + one E9) and it
also exercises the module-scan path that OOC/HQR will need. MouseFix should follow immediately
after (highest user value, everything verified). Do NOT start with HQR.

## 8. Verification notes for the main agent

- All pattern strings above were verified by regex-scanning `game_refs/GTAIV.exe` with `?` =
  one byte; "1 hit" means unique. The two rows marked with a reconstructed tail (OOC, HQR) must
  be re-derived with the builder emulation (temp-dir scripts `zp_dynA_*.py` reproduce every
  step; `zp_dynA_pat.py` prints the per-scan reconstructed strings).
- Raw helper bytes: `sub_1005B750` writes `E9` + `sub_10024060-site-5`; `sub_1005B980` writes
  `E8` to 0x10024120; `sub_1005BA80` writes `E8` to 0x1004C350; `sub_1005B5D0(site,dest)` writes
  `E9` to dest — all read the original displacement before overwriting (matches rule #3).
- The callbacks run once at config load and self-clear (call `[clear_fn]`); a faithful port must
  reproduce the one-shot semantics (or simply apply the same writes once).
- The MouseFix naked bodies end with `jmp [slot]` (tail-jump, never call) — rule #2.
