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

- **78 of 99 option keys closed**;
- **55 hook bodies transcribed, registered and proven whole** (`zp_body_proof`, a
  linear sweep of each body's exact instruction count); the rest of the original's
  bodies are still to transcribe. The longest two are the `DualVehicleHeadlights`
  pair at 78 instructions / 309 bytes each, which is what exposed the proof tool's
  old 256-byte sweep window: it compared 64 of 78 and still reported PASS. The tool
  now sweeps 4096 bytes and fails a row that decodes fewer instructions than its
  budget, so a truncated sweep can never pass again;
- launcher: both arms implemented. `LaunchWithPlayGTAIV` rewrites the two launch
  slots and `zp_launcher_slots` verifies them per build; `SkipLauncher` carries the
  original's 25-row table, both collectors, the DFA file-API stage (IAT capture, six
  installs, two collected jumps) and the ROM message-window stage (two installs, the
  WM_ROM service and its two bodies). The write set matches the original's recovered
  handlers byte for byte on all nine images (see the section below); the key reads
  OPEN only because the stored baseline is a partial recording of this feature;
- the 18 full/per-option per-build dump hashes and the oracle differential are not
  yet produced.

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
the game's hash function at `0x45CF50`).

Still blocked:

- `BadPlayerModelCrashFix` needs the ASI's **native-call layer**, not the shared
  helpers: `sub_1005B3F0`/`sub_1005B150`, `sub_100038E0` (an SEH frame around a call
  through `dword_1015BDE0`), `sub_1005DE40`/`80`, plus the GTAC.dll branch
  (`sub_1005B290`, `sub_1005B530`, the sprintf chain) and the original's own
  38,455-entry model-name table at `0x100DE770`;
- `GarageModelFix`, `NoLiveryLimit`, the steer/engine/plane trio and `FreezeCarFix`
  need `sub_10004C70` (the 1,343-byte steer consumer) and, for `FreezeCarFix`, the
  1,421-byte probe;
- `HighQualityReflections` (~200 instructions) and `CutsceneFixes` (~100, with only
  one real baseline byte) are over the transcription budget by design.

`ForceCarHeadlightShadows` cannot match while its baseline cell carries the
reference dump's `0xD0000000` placeholder for a published float pointer; closing it
needs that canonicalisation in `tests/dump_main.cpp`.

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
   the linker map, instruction budget — and `zp_body_proof` must PASS.

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
a `call` as an immediate, not as the function it names. Four implemented bodies
call our equivalent helper instead of the original's compiled target:

- `ped_models_in_cutscenes` calls audio's `ValueResolverThunk`, a
  one-instruction `jmp` through the published resolver slot, because the resolver
  (`ValueResolverBody`) is `static` in `shared.ixx` and an external-linkage naked
  function in a module interface unit is rejected (C2488), so an importer cannot
  emit the original's direct `E8` to it;
- `local_settings_detour` and `local_settings_fallback_detour` call our
  `LocalSettingsStep`, a reimplementation of the original's compiled helper;
- `savegame_fix_copy` calls `SavegameCopyBlock`, which forwards to `std::memcpy`.

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
