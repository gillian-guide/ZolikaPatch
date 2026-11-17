# AGENTS.md — readable-fusion

A from-scratch, FusionFix-shaped reimplementation of ZolikaPatch 7.65 for GTA IV
and EFLC. The oracle is `../game_refs/ZolikaPatch_original.asi.off`; the old
`../readable/` tree is a secondary reference only — its source must not be copied.

## Current state

The tree is part-way through a recategorisation into one module per grouped INI
section. The infrastructure modules stay as they are (`common`, `bytes`, `bodies`,
`patch`, `config`, `game`, `options`, `loader`, `shared`, `native`), and the feature
modules are now `stability`, `vehicles`, `graphics`, `launcher`, `misc`, `menu`,
`episodic`, `performance`, `compatibility`, `multiplayer` and `fixes`. The `audio`,
`loading`, `ooc` and `useless_checks` modules are gone: their arms were distributed to
the section modules that own them. A section module owns its arms' whole footprint -
published state, bodies and tables - and registers them through
`Loader::AddPublish`/`Loader::AddInstall` with the driver position each arm had before
the move, so the tier order is independent of which module file an arm lives in.

The recategorisation is complete: the six GRAPHICS-category vehicle light and shadow
arms (`CarPartsShadowFix`, `CarDynamicShadowFix`, `ForceCarHeadlightShadows`,
`DualVehicleHeadlights`, `InfiniteCarBottomDistance`, `ReversingLightFix`) and their
`[HeadlightConeOffsets]` registrations live in `graphics.ixx`, and the end of this
section lists the module of every key.

98 of the 99
catalogue keys are closed on 1.0.8.0, with 164 hook bodies transcribed and proven
instruction-for-instruction against the original ASI. The shipped-INI cell now shows
zero differing and zero extra bytes: everything the tree writes agrees with the
baseline, and what remains is unimplemented, not wrong. The pieces that are *not*
transcribed are rebuilt in C++ instead, and `docs/parity.md` is their catalogue: the
recovered VA and size, the exact instruction pattern that blocks the transcription,
what the rebuild reproduces, how that was verified and what stays unproven. Class A of
that catalogue is empty: a table operand no longer justifies a rebuild, because the
registry compares it by content. A body is only rebuilt for an exception frame, a TLS
static-init guard or a call graph of rebuilds - a `/GS` cookie is not a reason, and
neither is SSE.

One key is open by construction, not by omission, and it is recorded in
`docs/parity.md`: `SkipLauncher` implements the original's full 25-row launcher stage
plus the DFA and ROM stages, but its stored baseline records only the reference's
7-row arm - an arm nothing in that tree calls - so the scoreboard shows the bytes the
original writes and the baseline omits, instead of closure. Its parity is decided
against the oracle: the recovered rows replay to an identical write set on all nine
images. Separately, `NoImpostorHeightCutoff` reproduces the original's 1.0.0.0 fault
instead of guarding it, so the 1.0.0.0 dump faults past that arm, as the original does.

`tools/gen_options.py::mask_for()` refuses a build cell it cannot parse; the two cells
that used to be silently mis-parsed are machine-readable and their masks regenerated.

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
- **Where the material comes from.** The bodies, body VAs and pattern strings are
  already recovered: `../readable/source/*.ixx` has the arms and their recovered
  signatures, `../readable/source/hook_bodies.cpp` has every body as naked asm with
  its original VA in an `@exact 0x100…` comment and the `BodyId` table around line
  4860, and `../ZolikaPatch_reconstruction/` carries the same material in another
  layout. Read those, rewrite them here, and prove them against
  `../game_refs/ZolikaPatch_original.asi.off` with `tools/verify_bodies.py`. Do not
  re-disassemble a body that already exists there; use the IDA databases only to
  settle a question the references cannot answer or to confirm that a signature is
  unique.
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
- **Readable first, assembly in a marked tail.** Each module reads: header, published
  state with the original's addresses, plain C++ helpers, Publish<Key>, Install<Key>,
  Register<Key>, then a Transcribed bodies section holding every __asm block and
  naked body. A body used above its definition gets a plain static prototype - MSVC
  rejects __declspec(naked) on a declaration (C2488) - and only the definition in the
  tail keeps the attribute. Registration order inside Register<Key> is behavioural and
  never changes: arms that touch the same site depend on it.

## Dependencies

`external/injector` and `external/hooking` are pinned upstream submodules, not
committed copies. `git submodule update --init --recursive` checks out the
injector's own nested trees; without them the configure step has no headers or
sources to build.

| path | URL | pin |
| --- | --- | --- |
| `external/injector` | https://github.com/SandeMC/injector | `c196f90` |
| `external/injector/safetyhook` | https://github.com/cursey/safetyhook | `f44cc07` |
| `external/injector/minhook` | https://github.com/TsudaKageyu/minhook | `8af6b4a` |
| `external/injector/bddisasm` | https://github.com/bitdefender/bddisasm | `709fd97` |
| `external/injector/kananlib` | https://github.com/cursey/kananlib | `114de17` |
| `external/injector/zydis` | vendored inside the injector tree | Zydis amalgamation |
| `external/hooking` | https://github.com/ThirteenAG/Hooking.Patterns | `460a47b` |

The injector pin is the owner's fork. Upstream `ThirteenAG/injector`'s `MakeRET`
deduces an `int` for the opcode and writes four bytes for both the one-byte `ret`
and the three-byte `ret imm16` forms; the fork writes a `uint8_t` opcode and, for
the immediate form, a `uint16_t`, so `injector::MakeRET` emits the instruction at
its exact width. The tree calls `injector::MakeRET` directly and keeps no width
workaround. `CMakeLists.txt` compiles safetyhook's sources and the injector's
Zydis amalgamation into `zp_modules`, with `external/injector/include`,
`external/injector/safetyhook/include` and `external/injector/zydis` on the
include path. safetyhook at this pin carries its x86 mid-hook thunk as assembled
bytes in `mid_hook.cpp` and builds no `.asm`, so there is no MASM step.

Recovered signature text stays verbatim in the feature modules: pinned
Hooking.Patterns consumes one `?` per wildcard byte, so `NormaliseSignature` at
the `patch` boundary is what turns an IDA-style `??` token into one wildcard byte.
Do not move the normalisation into the call sites, and do not assume the scanner
accepts IDA spelling. CI checks the submodules out recursively for the same
reason.

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
