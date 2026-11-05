# AGENTS.md — readable-fusion

A from-scratch, FusionFix-shaped reimplementation of ZolikaPatch 7.65 for GTA IV
and EFLC. The oracle is `../game_refs/ZolikaPatch_original.asi.off`; the old
`../readable/` tree is a secondary reference only — its source must not be copied.

## Current state

Eleven feature modules are in place (stability, vehicles, graphics, useless_checks,
launcher, audio, misc, options, loader, pattern, shared) and 61 of the 99 catalogue
keys are closed on 1.0.8.0, with 34 hook bodies transcribed and proven
instruction-for-instruction against the original ASI. The shipped-INI cell now shows
zero differing and zero extra bytes: everything the tree writes agrees with the
baseline, and what remains is unimplemented, not wrong.

The harness that produces that evidence: `zp_tests` (contract suite),
`zp_dump_empty` (nine images, every option off), `zp_pe_check`, `zp_body_proof`
(whole-body instruction comparison, symbol resolved from the linker map) with its
self-test, `zp_launcher_slots`, and the drift guards for the generated catalogue and
INI. The measurement tools are `tools/parity_report.py` (per-key scoreboard),
`tools/dump_matrix.py` (9 builds x full/per-option), `tools/full_diff.py` (attributes
a full-cell difference to keys) and `tools/compare_dumps.py`.

## Rules

- **1:1 behaviour, faults included.** Same INI keys, defaults, build gates,
  writes, hook bytes, stage order. A guard that changes a fault/no-fault outcome
  is a bug. Every preserved fault is recorded in `docs/parity.md`.
- **Recovered data may be reused** (signatures, byte rows, offsets, hashes, RVA
  tables) with a provenance comment. Every executable line is written fresh.
- **Nothing is copied out of `../readable/`** — not source, not module layout,
  not names, not tables. Read it to learn what must be reproduced.
- **Startup is synchronous in `DllMain`**, as the original's was: the signatures
  have to be matched before the game runs. Do not move the install path to a
  thread or to `InitializeASI`-only.
- **The shipped ASI is silent** (the original writes no log). Diagnostics exist
  only in the `Trace` configuration, behind `ZP_TRACE`, writing
  `ZolikaPatch.trace.log` next to the ASI in the format the parity tools read.
- **A check is never deleted or weakened to get green.** If a check is wrong,
  say why and strengthen it.

## Build and test

```
cmake --preset win-x86
cmake --build --preset release
ctest --preset release
```

Win32/MSVC only, static CRT, image base `0x10000000`, windowed subsystem. The
artifact is `build/win-x86/Release/ZolikaPatch.asi`; `Trace` builds the same
artifact with tracing on. `zp_tests` covers the INI grammar (including every
quirk), the build classification, the signature normaliser, the scanner and the
event tiers, then loads the built ASI to prove the startup path runs.
