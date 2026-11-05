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

- **61 of 99 option keys closed**;
- **34 hook bodies transcribed, registered and proven whole** (`zp_body_proof`, a
  linear sweep of each body's exact instruction count); the rest of the original's
  bodies are still to transcribe;
- launcher: `LaunchWithPlayGTAIV` implemented and its slots verified per build
  (`zp_launcher_slots`); SkipLauncher, DFA and ROM are not started;
- the 18 full/per-option per-build dump hashes and the oracle differential are not
  yet produced.

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

`verify_bodies.py` stops at the first terminal instruction, so a body with an
internal jump is proven up to that jump, not through it. That is a real limit, not
an implied pass.
## Known proof gap (next tool fix)

`verify_bodies.py` stops at the first terminal instruction and counts `jmp` as
terminal, so:

- a body whose *entry* is a jump (`super_lod_shim`, original first byte `EB 06`) is
  proven as one instruction;
- a body with a `ret` in the middle has its tail after that `ret` unproven
  (`shader_streaming_find_hpe` 30 of 36, `find_v` 30 of 36, `filter_hpe` 13 of 16).

Those full streams were checked out of band (a linear sweep that also compares
instruction lengths) and are equal: 14 / 21 / 36 / 36 / 16 / 9. The fix is to sweep
linearly for the exact instruction count instead of stopping at a terminator, which
also means every `tests/bodies.tsv` budget becomes the body's true count rather than
a cap. Until that is in, treat a PASS whose count is below the row's budget as
partial - the row's budget is the number to compare against.
