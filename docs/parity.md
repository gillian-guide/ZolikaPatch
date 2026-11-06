# Parity

What has to match the original ZolikaPatch 7.65 ASI, and the faults reproduced on
purpose. A guard that turns one of these into a non-fault is a fidelity bug, not a
fix.

## Contracts pinned by tests

| contract | evidence |
| --- | --- |
| INI grammar (feather-ini-parser 1.41): 255-byte line buffer, first-line BOM drops the file, `#`/`//` comments only, strtok-on-`=` keys that keep a trailing space, values that keep their spaces, empty values not stored, last duplicate wins, a re-opened section starts empty, case-sensitive sections and keys | `zp_tests`, `TestConfigGrammar` |
| Read boundary is `std::stoi` on the stored text with the arm's default | `zp_tests`, `TestConfigGrammar` |
| Build classification: the four version fields concatenated (`1.0.7.0` -> 1070, `1.2.0.43` -> 12043) and the original's switch; an unsupported executable is `unknown` and applies nothing | `zp_tests`, `TestBuilds` |
| Option catalogue: 99 keys with the shipped value, the absent-key default and the build mask | `source/options.inc` (generated), `zp_tests` |
| The loader preamble's ten salt words: the shipped `.data` values when its probe hits, the recovered fallback set when it misses | `zp_salt_probe` (1.0.8.0 hit; 1.0.0.0 and 1.0.4.0 miss) |
| With no arm installed the pipeline changes nothing on any supported image | `zp_dump_empty` (9 images) |
| The generated catalogue and INI cannot drift from their sources | `zp_catalogue_drift`, `zp_ini_drift` |

## Preserved faults

| fault | condition | site |
| --- | --- | --- |
| `std::invalid_argument` escapes the loader | a non-numeric `[Options]` value (`Key=abc`, or `Key= ` of spaces only) | `Config::Integer` -> `std::stoi`, uncaught |
| `std::out_of_range` escapes the loader | a numeric value outside `int` | same |
| the whole INI is dropped | the first line starts with a UTF-8 BOM | `Feather::Parse` |
| null section dereference | a key line before the first `[section]` line | `Feather::Parse` |
| a line longer than 255 bytes is split | the parser's 255-byte line buffer | `Feather::Parse` |

Two of these remain assumptions, not measurements: feather 1.41's non-advancing
`getline` on a long line (the original very likely spins; this tree continues), and
CRLF folding (the original reads in text mode). The shipped INI's longest line is
192 bytes, so the first is unreachable from it.

## Status

Measured on 1.0.8.0 with `tools/parity_report.py 9`:

- **84 of 99 option keys closed**;
- **72 hook bodies transcribed, registered and proven whole** (`zp_body_proof`, a
  linear sweep of each body's exact instruction count); the rest of the original's
  bodies are still to transcribe, and the compiled-C bodies listed below are
  rebuilt in C++ instead, with no proof row. The longest two are the
  `DualVehicleHeadlights` pair at 78 instructions / 309 bytes each, which is what
  exposed the proof tool's old 256-byte sweep window: it compared 64 of 78 and
  still reported PASS. The tool now sweeps 4096 bytes and fails a row that
  decodes fewer instructions than its budget, so a truncated sweep can never pass
  again;
- launcher: both arms implemented. `LaunchWithPlayGTAIV` rewrites the two launch
  slots and `zp_launcher_slots` verifies them per build; `SkipLauncher` carries the
  original's 25-row table, both collectors, the DFA file-API stage (IAT capture, six
  installs, two collected jumps) and the ROM message-window stage (two installs, the
  WM_ROM service and its two bodies). The write set matches the original's recovered
  handlers byte for byte on all nine images (see the section below); the key reads
  OPEN only because the stored baseline is a partial recording of this feature;
- the 18 full/per-option per-build dump hashes and the oracle differential are not
  yet produced.

### The native-script call layer, and the key it closed

`DrivebyFOVFix` was blocked on the ASI's own scripting layer: its helper resolves
the drive-by FOV native through the game's native-handle resolver. That layer now
exists as `source/native.ixx`, and the key is closed with it.

| piece | VA | instructions | how |
|---|---:|---:|---|
| zero a pending output record | `0x100038C0` | 5 | transcribed |
| one-argument call, salt `dword_101580B8` | `0x1005B3F0` | 46 | transcribed |
| zero-argument call, hash from `ECX` | `0x1005B150` | 44 | transcribed |
| copy pending records out, dword result | `0x1005DE40` | 21 | transcribed |
| copy pending records out, byte result | `0x1005DE80` | 21 | transcribed |
| guarded resolve-and-invoke | `0x100038E0` | 59 | rebuilt as C |
| guarded resolve-only | `0x10003980` | 52 | rebuilt as C |

The last two are compiler output for a C function with `__try`/`__except`
(`GuardedInvoke` / `GuardedResolve`): their frames carry a cookie-xored scope table
whose handler fields are addresses inside the original image, so there is no
instruction stream to transcribe -- the same reason the launcher's DFA helpers are
rebuilt. Both keep the original ABI and behaviour: on the normal path the invoke
entry answers `1` (a null resolver skips the handler call at `0x10003937`), a fault
anywhere inside the guarded region -- the resolver call or the handler call -- is
caught and answers `0`, and when the resolver answers null the *caller* then faults
on the unresolved handler. That is the original's split, preserved.

The resolver is not invented. The original's loader preamble fills `dword_1015BDE0`
at `0x10052E88..0x10052E9E` by scanning the game module for `08 0B C2 8B F0 E8`
(1.0.8.0 site `0x625300`) and taking the target of the call whose opcode is the
pattern's last byte, `match + 10 + [match + 6]`. That is `0x626AF0`, the game's
native-handle resolver, and `Native::Register()` reproduces the scan with
`find_pattern` at startup. A missing pattern skips the store exactly as the
original's `jz` does, so the preserved fault is a null resolver: the layer's SEH
catches the call and answers zero, then the caller (for `DrivebyFOVFix`, the
`[eax+0Eh]` read at `0x1000646E`) faults where the original faults.

Two ABI details the transcribed pieces rely on. The game resolver reads the hash
from **ESI**, not from the argument registers (`0x626AF0` opens `mov eax, esi`), and
both original entries move the `ECX` argument there around the call, saving and
restoring the register; the C entries call a naked `ResolveInEsi` for that, because
C++ cannot ask for a register. And the one-argument wrapper's first register
argument is unused -- its payload travels in `EDX` -- so `Native::CallBool` is
declared with a discarded `ECX` parameter to make the compiler place the value
there.

Two call targets differ from the original's, both inside the proof's
call-is-an-immediate rule (below): the two wrappers call a local `NativeZeroBlock`
instead of the CRT `memset`, because the inline assembler rejects the intrinsic's
name in a direct call (C2420), and `DrivebyFOVFix`'s helper calls a six-byte `jmp`
forwarder through the layer's published resolve entry instead of the original's
direct `E8` to `0x10003980`, because a module interface cannot export a naked
target and the C entry's decorated name is not nameable from inline asm.

### `BadPlayerModelCrashFix`, closed on the same layer

The key had been reverted as blocked on that library. Its remaining pieces turned
out to be transcribable: every call the installed code makes lands on a piece the
tree already carries or on a piece small enough to transcribe here, and the two
that are not instruction streams are rebuilt with their behaviour intact.

| piece | VA | instructions | how |
|---|---:|---:|---|
| the site's trampoline | `0x10037BA0` | 16 | transcribed |
| the payload it calls | `0x10037AC0` | 65 | transcribed |
| the GTAC.dll "who" call | `0x1005B290` | 61 | transcribed |
| the GTAC.dll chat print | `0x1005B530` | 45 | transcribed |
| its one-argument native wrapper | `0x1005B3F0` | 46 | the layer's copy, called through a forwarder |
| the model-name lookup | `0x10003F00` | 21 | rebuilt |
| the bounded formatter | `0x10001280` (`0x10001240`) | 13 | rebuilt |

The arm jumps to the trampoline at the game's model-resolution entry (game
`0x7CF803`, three bytes into the function, so `EBP` is live) and publishes the
resume `match+10` before the jump, because the resume is the instruction the jump
covers. What the bodies read from the tree is unchanged: `sub_100038C0`,
`sub_1005DE40`, `sub_1005B150` and `sub_1005B3F0` come from
`source/native.ixx`, `sub_10003E50` from `source/shared.ixx`, `sub_100038E0`
through the layer's published invoke entry and `dword_10159C0C` through
`source/shared.ixx`'s slot. The six `jmp` forwarders (one instruction each)
exist for the same reason `DrivebyFOVFix`'s does: a module interface cannot export
a naked function, so the arm fills each slot from the body registry by the piece's
recovered VA.

**The two rebuilds, and why they are not transcriptions.**

- `0x10003F00` walks the original's 38,455-entry name table and hashes each name
  with `sub_10003E50`. Its only data operand is an *indexed* absolute address
  (`push dword ptr [esi*4 + 0x100DE770]`), and the whole-body comparator compares an
  indexed absolute operand's displacement literally -- unlike a plain absolute
  operand, which it reduces to "abs" (below). A rebuilt module cannot place its
  table at the original's VA, and normalising the operand would be a change to the
  comparator, so the loop is written in C++ instead: the same table order, the same
  resolver, the same first-match result and the same `"**UNKNOWN**"` miss answer.
  The table itself is recovered data, not a rebuild: `source/model_names.inc` holds
  the 38,455 names in the table's own order with the extraction recipe and the
  pinned image's SHA-256 in its header.
- `0x10001280` is compiler output for a `vsnprintf`-shaped wrapper, and its callee
  reaches the UCRT's own printf engine -- the `FF 15` at `0x1000125E` goes through
  the IAT slot `0x1006336C`, which imports `__stdio_common_vsprintf`, with the
  `STANDARD_SNPRINTF_BEHAVIOR` option bit (`or ecx, 2`) -- so the rebuild is
  `_vsnprintf_s` with `_TRUNCATE`: the same bound, the same truncate-and-terminate
  result for this `%s ... %s` format. The payload's call to it compares as a call
  immediate either way.

**The preamble's salt block, and what the layer had wrong.** `Native::CallBool`'s
body reads `dword_101580B8`, so the layer has to resolve the original loader
preamble's ten salt words, not seed one constant. The probe is
`0x10052E10..0x10052E1C`:

```
10052e10  mov  ecx, offset a6868A9541451E8 ; "68 ? ? ? ? 68 A9 54 14 51 E8"
10052e15  call sub_10003830
10052e1a  test eax, eax
10052e1c  jnz  short loc_10052E82
```

and `loc_10052E82` is the resolver scan (`GetModuleHandleA(0)` again, then the
`08 0B C2 8B F0 E8` probe at `0x10052E88`). The ten stores at
`0x10052E1E..0x10052E78` are in the fall-through, so they run **only when the
probe misses**. The hit path copies nothing from the game image: `jnz` jumps over
every store, so each slot keeps its shipped `.data` dword. No other code writes
these ten words -- the only xrefs are the preamble store and the readers below.

| store | slot | miss value | shipped `.data` (hit) | consumer |
|---|---:|---:|---:|---|
| `0x10052E1E` | `0x10158054` | `0x6B86410E` | `0x511454A9` | `sub_1005B1E0`, two-argument native wrapper |
| `0x10052E28` | `0x101580C8` | `0x406B4B20` | `0x570F5725` | `sub_1005B290`, the BPM "who" wrapper |
| `0x10052E32` | `0x10158050` | `0x6DCD115A` | `0x3B0C6738` | `sub_10050A50` |
| `0x10052E3C` | `0x101580B8` | `0x62BFDB37` | `0x4E61480A` | `sub_1005B3F0`, `Native::CallBool` |
| `0x10052E46` | `0x10158028` | `0x931F73A2` | `0x6A6B4F18` | `sub_1005B490`, one-argument byte wrapper |
| `0x10052E50` | `0x101580A8` | `0x7F6A60D3` | `0x79A95BF9` | `sub_10050A50` |
| `0x10052E5A` | `0x10158040` | `0xC9DBDA90` | `0x4BF36A32` | `sub_10037AC0`, the BPM payload |
| `0x10052E64` | `0x1015803C` | `0xA4EA0691` | `0x022B2DA9` | `sub_10004C70`, `sub_100388C0` |
| `0x10052E6E` | `0x101580D4` | `0x09399528` | `0x75E005F1` | `sub_10006450`, drive-by FOV helper |
| `0x10052E78` | `0x1015805C` | `0x1CC648EA` | `0x62E319C6` | `sub_10050A50` |

The stores are followed by `dword_1015BDE0`'s resolver sequence at
`0x10052E96` (`mov ecx,[eax+6]; add eax,0Ah; add eax,ecx; mov
dword_1015BDE0,eax` at `0x10052E9E`), which is the scan `Native::Register()` also
reproduces.

On 1.0.8.0 the probe hits (game VA `0xB3878C`), so the runtime values are the
shipped set: `0x4E61480A` for B8, `0x75E005F1` for D4, `0x570F5725` for C8,
`0x4BF36A32` for 40. On 1.0.0.0 and 1.0.4.0 the scan has no hit, so the stores run
and the same slots become `0x62BFDB37` / `0x09399528` / `0x406B4B20` /
`0xC9DBDA90`. `source/native.ixx` now carries the ten words, seeds each with its
shipped value and applies the miss set; `Native::Register()` calls
`Native::ResolveSalts()` before any arm. `zp_salt_probe` pins both branches on
those three real images, through that same function and `find_pattern`.

Two layer bugs came from seeding one slot with the miss constant instead:
`source/native.ixx` set B8 to `0x62BFDB37`, so `Native::CallBool` asked for a
hash 1.0.8.0 does not define; and `source/misc.ixx`'s `DrivebyFOVFix` helper read
its own slot seeded with the D4 miss value `0x09399528`, so on 1.0.8.0 it
resolved the wrong native and then dereferenced the answer at `0x1000646E`
unguarded. The helper now reads the layer's `0x101580D4` slot. The unguarded
dereference stays -- that is the original's.

**The duplicate `0x1005B3F0` body is retired.** With the layer's B8 slot correct,
`BadPlayerModelCrashFix` no longer needs its own 46-instruction copy of the
one-argument wrapper. `BpmCheckBody` calls the layer's body through a six-byte
`jmp [zpatch_bpm_call_bool]` forwarder -- the same pattern the arm already uses
for `sub_100038C0`/`DE40`/`1005B150` -- and the arm's `zpatch_bpm_salt_b8`
slot is gone; its C8 and 40 salts stay local. `BpmCheckBody` keeps its 65
instructions and its proof (the call target is an immediate on both sides), and
`bad_player_model_call_bool` is removed from `tests/bodies.tsv` (73 -> 72
bodies).

**The fault these bodies keep.** On a build whose ped-array anchor is absent, the
original's preamble skips the store into `dword_1015AD78` and leaves it zero, and
the payload's first array read (`mov esi,[ecx+eax*4]`) dereferences it. 1.0.0.0 is
such a build: the anchor `85 C0 C6 44 24 0C 00 C6 44 24 0D 00 75 07` has no hit
there, while the site itself does (`0x89AE53`). The arm keeps the original's shape --
no guard, and the store is skipped when the scan misses -- so it faults in the same
place; the offline dump cannot see it, because nothing dereferences the slot there.

### The vehicle arms closed in this pass

- `IncreaseVehicleModelLimit` (anchor `0x8DD143`): writes the dword `0x400` through
  the operand of the `mov ecx, 0xF3E8E8` at match+77, which is the model-limit
  object the allocator is constructed with. The write target is a *game* address, so
  this arm rebases it through the image's preferred base (`GamePointer`): the
  identity in the game, and what makes the offline dump write the same two bytes.
- `EpisodicVehicleSupport`: on 1.0.7.0 and 1.0.8.0 it retargets two absolute
  episode-flag operands to this module's own word (initial 2, the game's value);
  every gated build then gets the 23 signature-driven NOP rows, in the original's
  row order (two all-hits rows, two rows with a second window).
- `HighFPSBikePhysicsFix` (anchor `0xB9C05B`): jumps to the clamp detour at
  `0x1001EF60`, whose published resume is the site's own `call` (match+9).
- `DualVehicleHeadlights` (anchor `0x8F7CE5`): two 78-instruction bodies --
  `0x100064F0` for the new builds, `0x10006630` for the old ones -- that draw both
  headlights through the site's own calls and tail-jump to the site's `pop edi`
  (match+338).

**A recovered-data conflict, recorded rather than papered over.**
`DualVehicleHeadlights`' catalogue row carries the mask `0x20` (bit 5 alone,
1.0.4.0). The original's own two arm masks are in `recovered_build_gates.inc`:
`@old` = `0x62` (build ids 1, 5, 6 -- 1.0.0.0, 1.0.4.0, 1.0.0.4) and `@new` =
`0x4BF80` (ids 7, 8, 9, 10, 11, 12, 13, 15, 18 -- 1.0.6.0 through 1.2.0.43), and the
original's handler picks the body by the build id it classified (`cmp [build], 1` ..
`cmp [build], 6` at `0x100067F3`). So the arm consults `@old` for the jump to
`0x10006630` and `@new` for the jump to `0x100064F0`; their union, `0x4BFE2`, is
every supported build, and the 1.0.8.0 baseline the scoreboard compares does carry
the write. `Loader::Enabled` would therefore never install this arm on 1.0.8.0, so
this one arm gates on `Catalogue::Read("DualVehicleHeadlights")` plus those two
recovered masks.

The catalogue row is wrong, and the fault is in `tools/gen_options.py`, not in the
inventory's prose: `mask_for()` splits a build cell on `,` and `/` and keeps only the
tokens that are a known version string or a bare integer, silently dropping the
rest. This key's cell is `doc: 1000/1040/1004 — **code: no gate** (§10)`, so the
first and third tokens are dropped (`doc: 1000`, `1004 — ...`) and only the middle
`1040` survives -- `1 << 5` = `0x20`. Two ways out, both outside this tree's feature
code: make `mask_for` reject a cell it cannot parse completely (so the next prose
cell fails loudly instead of picking one build), or move the prose out of the build
column and list the code's own mask (`ALL`, i.e. `0x4BFE2`). Until one lands, the
catalogue and the arms disagree on this key.

Transcribed and available for the arms that need it (`source/shared.ixx`):
`sub_10003E50` (the `hash:`/quote config-value resolver, 67 instructions) and the
1.0.8.0 `CModelInfoStore::getModelByKey` lookup (`dword_10159C0C`, resolved with the
original's own pattern). `AllowHashesForIDE` needs exactly that resolver (its site is
the game's hash function at `0x45CF50`). The native-script call layer is
`source/native.ixx` (above); its public surface is `Native::Call`, `CallBool`,
`Resolve`, `Invoke`, `Resolver`, `Salt`, `ResolveSalts` and `Register`.

### The misc keys closed in this pass

Four keys in `source/misc.ixx` closed with their original arms and bodies. Six of
the seven bodies are transcribed and proven whole; the heavy helpers are compiler
output for C functions and are rebuilt with the same ABI, state words and unguarded
lookups:

| key | writes on 1.0.8.0 | bodies |
|---|---|---|
| `SpeedupBugFix` | `E9` at RVA `0x571B0` (game `0x4571B0`) | `sub_10050540`, 109 instructions, rebuilt in C++ |
| `Patch4RemoveModChecks` | `E9` at RVA `0x3CBF70` (game `0x7CBF70`) | `sub_100054E0`, 8, transcribed |
| `CopSirensFlag` | `E9` at RVAs `0x4FABE6`, `0x4FEBD3`, `0x646546` | `sub_10006310` 8, `sub_10006330` 8, `sub_100040E0` 9, transcribed |
| `PopupHeadlightsFlag` | `E9` at `0x653A64`, `EB` at `0x653A6E`, the `fld` operand at `0x653AB6` (our pi/2 float, the baseline's `0xD0000000` placeholder), `E9` at `0x646546` | `sub_10044530` 10, `sub_10044560` 10, `sub_100040E0` 9, transcribed |

`SpeedupBugFix` is the one key whose body has **no proof row**: `sub_10050540`
(109 instructions) is an SEH frame, a /GS cookie, the MSVC thread-safe static
guard for its one-time clock scan and SSE arithmetic, so it is reimplemented in
C++ rather than transcribed -- the same call the tree makes for the launcher's
DFA helpers. The arm publishes the eight absolute float pointers the original
handler reads from the site's operands (`+26, +165, +188, +226, +234, +249, +264,
+276`: `dword_18CAE9C`, `flt_18CAEA0`, `dword_18CAEA4`, `dword_1752078` and their
four copies), rebased through the image's preferred base as every captured game
pointer is. The arithmetic is the original's: `scale = 1000.0 /
QueryPerformanceFrequency`, then `(float)scale`, `(float)(scale * 1000.0)`,
`(float)(scale * 0.0010000000474974513)` and
`(float)(0.0010000000474974513 / scale)` written to the four pairs, then the
game's own clock function -- found by the original's one-time scan
`1B D9 89 5C 24 24 0F 88`, game `0x456F93` -- called at `match - 51` and its
64-bit result stored in `*self`. A missing scan leaves the slot zero, so the call
faults on `0xFFFFFFCD` exactly where the original's does.

`Patch4RemoveModChecks` and `RemoveGFWLUpdatePopup` are the two flags of the
original's one shared handler (the stub `sub_100054E0` plus the helper
`sub_10005340`, 145 instructions). `loading.ixx` owns the GFWL branch; `misc.ixx`
owns the Patch4 branch: a one-time scan for
`33 C4 89 84 24 A8 01 00 00 53 8B 5D 08 56 57`, then a zero store to the dword
the matched code points at (`site + 30`). **That signature has no hit on
1.0.8.0**, so on the user's build the branch is the scan and nothing else -- the
key's visible effect there is the stub install, which is what the original does
too. Recorded split: the original has one helper reading both flags, while this
tree has one helper per module; only one body can sit at the site and the misc
arm installs later, so a run with *both* keys enabled performs the Patch4 branch
and not the GFWL writes. Each key alone is 1:1, and the shipped INI has both off.

The `CopSirensFlag` checkers (`sub_10005F70`, 191 instructions;
`sub_10006260`, 53) and the `PopupHeadlightsFlag` checker (`sub_10044310`, 125)
are compiled C and rebuilt in C++ against the same state words: a 512-slot
model-hash list per category, filled by the shared IDE hook (`sub_100040E0` plus
the rebuilt `sub_10003FA0`, 92) when the game's parser stores a `popuplights`,
`siren` or `wheelangle:<degrees>` entry. Both keys install that same IDE body at
the same site, as the original's two handlers do. The lookups go through
`dword_10159C0C` and the names through the transcribed `sub_10003E50`, both
unguarded, so a missing resolver faults where the original's indirect call does.
The popup shims' second (EDX) pattern has no hit on 1.0.8.0; its body is still
transcribed and proven. The `[PopupLightAngles]` / `[HeadlightConeOffsets]` INI
tables that the original's loader preamble parses are not parsed by this tree
yet; the shipped INI's two sections are empty, so the checker's default pi/2 is
exactly what the original uses there.

### Still blocked

- `GarageModelFix`, `NoLiveryLimit`, the steer/engine/plane trio and `FreezeCarFix`
  need `sub_10004C70` (the 1,343-byte steer consumer) and, for `FreezeCarFix`, the
  1,421-byte probe;
- `HighQualityReflections` (~200 instructions) and `CutsceneFixes` (~100, with only
  one real baseline byte) are over the transcription budget by design.

`ForceCarHeadlightShadows` has no arm in this tree yet; the published-pointer
canonicalisation its baseline needs now exists (`Bodies::RegisterPublished` plus
`tests/dump_main.cpp`'s rewrite to the baseline's `0xD0000000` placeholder) and
`PopupHeadlightsFlag` is closed through it.

## Transcribing a body

A body-bearing key is committed only when all four parts exist:

1. **The body's asm**, transcribed into the owning module's tail, with the recovered
   VA in a comment. A naked function in a module interface unit must be `static` at
   module scope: in a namespace, or with external linkage, MSVC rejects it.
2. **The published module words** the body reads, written *before* the site is
   installed: the original reads a site's original bytes for its resume, so
   publishing after the write resumes into the jump itself.
3. **The registry entry** `Bodies::Register(&body, recoveredVa)`, so the dump can
   express the branch in the original's coordinates.
4. **The proof row** in `tests/bodies.tsv` — name, recovered VA, symbol resolved from
   the linker map, instruction budget — and `zp_body_proof` must PASS. A body that is
   compiler output for a C function (an SEH frame, a /GS cookie, the thread-safe
   static-init protocol) is rebuilt in C++ instead, and then has no proof row:
   `SpeedupBugFix` is the current example.

## What the scoreboard can and cannot compare

`parity_report.py` compares a key's changed bytes against the stored baseline and
excludes **branch displacement bytes**: those baselines wrote one stand-in body
address for every arm, so the four bytes of a displacement are meaningless there.
The set of displacement RVAs is derived from the pristine game image (an `E8`/`E9`
opcode one to four bytes before the RVA), which is the only source that works for an
arm installed over a site that was already a branch.

Because of that exclusion the scoreboard proves bytes but not body identity: an arm
that registers the wrong reference VA still matches. Body identity comes from
`tests/body_targets.tsv` (the arm names its body, the table names its VA) and from
the instruction-level proof.

`verify_bodies.py` sweeps each body linearly for exactly the row's instruction
count, so the manifest budget is the body's true length, not a cap: the budget is
the count to the first `int3` in the original. A row that is short would silently
truncate the proof on both sides, and a row that is long reads into padding (and
fails if either side decodes fewer instructions), which is why the number is part
of the evidence rather than a hint.

Body identity is instruction-level except for a call's target: the proof compares
a `call` as an immediate, not as the function it names. Several implemented bodies
call our equivalent helper instead of the original's compiled target:

- `ped_models_in_cutscenes` calls audio's `ValueResolverThunk`, a
  one-instruction `jmp` through the published resolver slot, because the resolver
  (`ValueResolverBody`) is `static` in `shared.ixx` and an external-linkage naked
  function in a module interface unit is rejected (C2488), so an importer cannot
  emit the original's direct `E8` to it;
- `local_settings_detour` and `local_settings_fallback_detour` call our
  `LocalSettingsStep`, a reimplementation of the original's compiled helper;
- `savegame_fix_copy` calls `SavegameCopyBlock`, which forwards to `std::memcpy`;
- `native_call_bool` and `native_call_hash` call `NativeZeroBlock`, a cdecl
  forwarder to the CRT `memset`, because the inline assembler rejects the intrinsic
  name in a direct call (C2420);
- `driveby_fov_helper` calls `DrivebyResolveEntry`, a six-byte `jmp` through the
  native layer's published resolve entry, because the layer's C entry is not
  nameable from inline asm in another module unit.
- the `BadPlayerModelCrashFix` bodies call `BpmZeroBlock` (a cdecl forwarder to the
  CRT `memset`), `BpmFormat` (the rebuilt `0x10001280`), `BpmModelName` (the rebuilt
  `0x10003F00`) and five one-instruction `jmp` forwarders into the layer's pieces.
  The forwarders are the same shape as `DrivebyResolveEntry` and for the same
  reason; the two rebuilt helpers are named in the section above.

The stored baselines have one gap of their own: they were produced by the
reference tree, whose `ApplyCarDynamicShadowFixNoE` pass is defined but never
called, so on 1.0.0.0/1.0.4.0 the baseline lacks the six-byte NOP fill this tree
writes from the recovered `@noE` row. `CarDynamicShadowFix` therefore reports six
extra bytes on 1.0.4.0; it is a baseline omission, not a difference in the bytes
both sides write.

## A second incomplete baseline: `SkipLauncher`

`SkipLauncher` reads OPEN on every image and it is **not** a missing arm. The stored
cell is a partial recording of the feature, and the bytes it lacks are ones the
original's own handlers write. Its parity is therefore decided against the oracle —
the recovered handlers at `0x10045CE0` (main table and collectors), `0x10004850`
(DFA) and `0x10004620` (ROM) — not against the baseline.

How that was measured. `ZolikaPatch_reconstruction/evidence/skip_launcher_ops.json`
holds the recovered rows, their signature anchors, offsets, bytes and the hook
targets; the comparison replays those rows on each pristine image (matching the
scanner's executable range, applying the handlers' own guards and the E8/E9
displacements with the recovered body VAs) and diffs the result against this tree's
dump. The row data is the evidence, so the comparison is against the original, not
against a restatement of the transcription under test. On all nine images the two
write sets are identical: no missing byte, no extra byte, no differing byte.

Why the baseline cannot be the oracle here. Its producer is the reference tree's
*other* implementation: `../readable/source/xliveless.cpp` carries this same
25-row table plus the DFA and ROM stages, but nothing in that tree calls
`ApplySkipLauncher`; the live arm `ApplySkipLauncherOption` implements seven of the
25 rows (0, 2, 3, 4, 5, 6, 9) and neither hook stage. The stored cell is that
seven-row arm's output, so it cannot contain the rows the live arm never scanned.

The smallest counter-example is 1.0.0.0, where three different write sets exist for
one key:

| source | bytes | what it writes |
|---|---:|---|
| original handlers | 14 | row 2's two NOP pairs (4), row 10's `90 A3` (2), the second collector's two sites (8) |
| reference live arm | 4 | row 2's two NOP pairs only (it never scans row 10 or the collectors) |
| stored baseline | 2 | row 2's first NOP pair only |

So the baseline is not even self-consistent with the reference implementation that
was supposed to have produced it: even a 7-row arm writes four bytes there. On
1.0.8.0 the original writes 108 bytes, the baseline records 8, and every one of
those 8 is inside the original's set (the baseline never invents a byte).

### Per-image totals

`rows` is the set of main-table entries whose signature matched, `collectors` the two
stub sites per pattern, `DFA` the capture plus installs plus collected jumps that the
stage's gates let through (the four EFLC builds exclude it), `ROM` its two installs.
"baseline" counts the bytes the stored `b{n}-per.txt` cell records for this key.

| image | rows matched | collectors | DFA | ROM | original bytes | baseline bytes | baseline lacks |
|---|---|---:|---:|---:|---:|---:|---:|
| 1.0.0.0 (b1) | 2, 10 | 2 | capture | 0 | 14 | 2 | 12 |
| 1.0.4.0 (b5) | 0, 3, 4, 7, 8, 20 | 4 | capture | 2 | 485 | 9 | 476 |
| 1.0.0.4 (b6) | 0, 1, 5, 9, 10, 11–22 | 4 | capture + 4 installs | 2 | 224 | 9 | 215 |
| 1.0.6.0 (b7) | 1, 6, 9, 10, 21, 22, 23, 24 | 4 | capture + 6 installs + 2 | 2 | 117 | 8 | 109 |
| 1.0.7.0 (b8) | 1, 6, 9, 10, 21, 22, 23, 24 | 4 | capture + 6 installs + 2 | 2 | 117 | 8 | 109 |
| 1.0.8.0 (b9) | 1, 6, 9, 10, 21, 22 | 4 | capture + 6 installs + 2 | 2 | 108 | 8 | 100 |
| 1.1.1.0 (b11) | 1, 6, 9, 10, 21, 22, 23, 24 | 4 | none (EFLC gate) | 2 | 78 | 8 | 70 |
| 1.1.2.0 (b12) | 1, 6, 9, 10, 21, 22, 23, 24 | 4 | none (EFLC gate) | 2 | 78 | 8 | 70 |
| 1.1.3.0 (b13) | 1, 6, 9, 10, 21, 22 | 4 | none (EFLC gate) | 2 | 70 | 8 | 62 |

`0x4BFE2` (every supported build) is the key's own mask, so every image above runs
the arm; the ROM stage runs even on the images whose DFA stage is gated off, and even
when the XLivelessAddon companion is loaded (that flag skips the main table and the
DFA stage only).

### Row sites per image

Each cell is the site the row's signature resolves to (RVA), or `–` where the
signature is absent. A row is only applied where it has a site, exactly like the
original: 19 of the 25 rows have none on 1.0.8.0 because they belong to other
launcher versions, and the original skips them there too — no row is unmatched that
the original matches.

| row | anchor | b1 | b5 | b6 | b7 | b8 | b9 | b11 | b12 | b13 |
|---:|---|---|---|---|---|---|---|---|---|---|
| 0 | 0x1012DC74 | – | 0x071580 | 0x1260C0 | – | – | – | – | – | – |
| 1 | 0x1012DCE0 | – | – | 0x126BC6 | 0x00289E | 0x00287E | 0x0900DE | 0x07359E | 0x07316E | 0x16941E |
| 2 | 0x1012DD5C | 0x00293A | – | – | – | – | – | – | – | – |
| 3 | 0x1012DDB0 | – | 0x071E8A | – | – | – | – | – | – | – |
| 4 | 0x1012DE10 | – | 0x071F1D | – | – | – | – | – | – | – |
| 5 | 0x1012DE60 | – | – | 0x126C5C | – | – | – | – | – | – |
| 6 | 0x1012DEB0 | – | – | – | 0x00293D | 0x00291D | 0x09017D | 0x07363D | 0x07320D | 0x1694BD |
| 7 | 0x1012DF28 | – | 0x071E8E | – | – | – | – | – | – | – |
| 8 | 0x1012DF80 | – | 0x052128 | – | – | – | – | – | – | – |
| 9 | 0x1012E0D0 | – | – | 0x1269B9 | 0x00264D | 0x00262D | 0x08FE8D | 0x07334D | 0x072F1D | 0x1691CD |
| 10 | 0x1012E134 | 0x002946 | 0x071E98 | 0x126BCB | 0x0028A3 | 0x002883 | 0x0900E3 | 0x0735A3 | 0x073173 | 0x169423 |
| 11 | 0x1012E1A0 | – | – | 0x074D0B | – | – | – | – | – | – |
| 12 | 0x1012E1F0 | – | – | 0x081225 | – | – | – | – | – | – |
| 13 | 0x1012E23C | – | – | 0x0812F5 | – | – | – | – | – | – |
| 14 | 0x1012E290 | – | – | 0x081375 | – | – | – | – | – | – |
| 15 | 0x1012E2E8 | – | – | 0x0813EF | – | – | – | – | – | – |
| 16 | 0x1012E340 | – | – | 0x0815E3 | – | – | – | – | – | – |
| 17 | 0x1012E394 | – | – | 0x083BF5 | – | – | – | – | – | – |
| 18 | 0x1012E3F8 | – | – | 0x083EA2 | – | – | – | – | – | – |
| 19 | 0x1012E45C | – | – | 0x1280B0 | – | – | – | – | – | – |
| 20 | 0x1012E4B0 | – | 0x072BA0 | 0x127930 | – | – | – | – | – | – |
| 21 | 0x1012E518 | – | – | 0x126F77 | 0x002D35 | 0x002D15 | 0x0905C5 | 0x073A25 | 0x073605 | 0x169905 |
| 22 | 0x1012E574 | – | – | 0x126E4F | 0x002B32 | 0x002B12 | 0x0903C2 | 0x073832 | 0x073402 | 0x169702 |
| 23 | 0x1012E5EC | – | – | – | 0x003890 | 0x003870 | – | 0x074580 | 0x074160 | – |
| 24 | 0x1012E658 | – | – | – | 0x004270 | 0x004250 | – | 0x074F60 | 0x074B40 | – |

### The 1.0.8.0 cell, byte by byte

"differing" counts only the bytes that differ from the pristine image (a store over
an identical byte is not a dump row, and a displacement can coincide with its
preimage). "baseline" is how many of those bytes the stored cell records.

| row | site | differing bytes | baseline |
|---:|---|---:|---:|
| 1 | 0x0900DE | 0 (jump target only) | 0 |
| 6 | 0x09017D | 6 | 6 |
| 9 | 0x08FE8D | 4 | 2 |
| 10 | 0x0900E3 | 2 | 0 |
| 21 | 0x0905C5 | 16 | 0 |
| 22 | 0x0903C2 | 14 | 0 |
| collector `A1 ? ? ? ? 85 C0 8B 0D ? ? ? ? 75` | 0x74FAA0, 0x74FAD0 | 8 | 0 |
| collector `33 C0 83 3D ? ? ? ? 01 0F 94 C0 C3` | 0x74FAC0, 0x74FB00 | 8 | 0 |
| `dfa_init` | 0x055EFC | 4 | 0 |
| `create_file` | 0x917CD3 | 5 | 0 |
| `close_handle` | 0x917E6B | 4 | 0 |
| `set_file_pointer` | 0x917E92 | 5 | 0 |
| `get_file_size_ex` | 0x917EE1 | 5 | 0 |
| `read_file` | 0x917D38 | 5 | 0 |
| collected jump 0 | 0x0701B7 | 5 | 0 |
| collected jump 1 | 0x07031B | 5 | 0 |
| `rom_reset` | 0x08FE46 | 4 | 0 |
| `rom_set` | 0x090A20 | 5 | 0 |

Total: 108 differing bytes, the baseline records 8 of them (row 6's six NOPs and
row 9's two `xor` bytes) and no byte the original does not write. Hence
`parity_report.py 9` prints `OPEN SkipLauncher: baseline 8, ours 108, missing 0,
extra 57, differing 0`: the 57 "extra" bytes are the ones the baseline omits minus
the 43 displacement bytes the scoreboard excludes by design.

### What the launcher stage reimplements, and what it cannot prove

- The 25-row table, the collectors, the DFA and ROM install rows are in-place
  instruction patches; the eight hook bodies are the callable part. Three are
  transcribed assembly and proven whole (`rom_set` 4 instructions, `rom_reset` 28,
  `dfa_init` 2). The five DFA file-API wrappers (`create_file`, `close_handle`,
  `set_file_pointer`, `get_file_size_ex`, `read_file`) are fresh C++ with the
  original's `__cdecl` ABI, the same argument order and the same fallbacks; the
  original's are compiler output for the same signatures, not hand-written assembly,
  so they are rebuilt rather than transcribed (`close_handle` needs MSVC's own SEH
  frame). `create_file` keeps the original's unguarded indirect call — a slot whose
  target is null faults, as it does in the original.
- The ROM service is a live-process component: a `std::thread` that registers
  `"MessageWindow"` (falling back to `"MessageWindow2"`), creates a message-only
  window, publishes the handle from `WM_NCCREATE` and answers `WM_ROM` (43858) with
  the recovered action table. `StartRomService` spins on `Sleep(0)` until the window
  exists, with no bound: if class registration succeeds and window creation fails,
  the game hangs there. The original does the same, so the fault is preserved rather
  than timed out. Its thread object is a module global that is never joined, also as
  in the original.
- The two collectors keep the original's shape: a non-empty check and then the pair,
  with no `size() > 1` guard. A pattern that matches has at least two hits on every
  supported image (the table above counts 2 and 4–5; 1.0.0.0 has no hit for the first
  pattern and the collector is skipped), so the shape cannot fault here; on an image
  where it did, the original would write through the same out-of-range slot.
- The DFA stage is suppressed when `XLivelessAddon.asi` is loaded, when `GTAC.dll` is
  loaded, and on the four EFLC builds, and it is one-shot; the ROM stage is one-shot
  and runs regardless. Those are the original's own guards (`0x1000487B` onward).
- **Not wired:** the original's *other* DFA call, from the MiscFixes handler
  (`0x10024018`). It is a no-op whenever `SkipLauncher=1` because the DFA one-shot is
  already set, so it cannot change this key's cell or any other cell measured above;
  it would only matter for `SkipLauncher=0, MiscFixes=1` in the game, and that arm
  lives in `misc.ixx` rather than the launcher module.

## A preserved fault that the tools cannot measure

`NoImpostorHeightCutoff` reproduces the original's fault on 1.0.0.0: the pattern
matches once and the original writes `hits[1]` through an uninitialised vector
entry (`mov eax,[ebp-0x6C]; mov esi,[eax+4]` at `0x100536F1`, write `66 89 06`). The
arm keeps that shape - no `pattern.size() > 1` guard - so on 1.0.0.0 the dump writes
the first pair (which is exactly what the stored baseline's single pair records) and
then faults `0xC0000005`, as the original does.

Consequence: `parity_report.py 1` and `dump_matrix.py` fault at that arm on
1.0.0.0, so the 1.0.0.0 cell of the 18-cell matrix cannot be measured for keys that
run after it. That is the fault being faithful, not the tree being broken; the
reference tree guards it, which is why its baseline carries only the first pair.
Guarding it here to make a tool happy would be the fault-avoidance divergence the
policy forbids.
