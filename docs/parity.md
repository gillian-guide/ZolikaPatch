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
- **52 hook bodies transcribed, registered and proven whole** (`zp_body_proof`, a
  linear sweep of each body's exact instruction count); the rest of the original's
  bodies are still to transcribe. The longest two are the `DualVehicleHeadlights`
  pair at 78 instructions / 309 bytes each, which is what exposed the proof tool's
  old 256-byte sweep window: it compared 64 of 78 and still reported PASS. The tool
  now sweeps 4096 bytes and fails a row that decodes fewer instructions than its
  budget, so a truncated sweep can never pass again;
- launcher: `LaunchWithPlayGTAIV` implemented and its slots verified per build
  (`zp_launcher_slots`); SkipLauncher, DFA and ROM are not started;
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
