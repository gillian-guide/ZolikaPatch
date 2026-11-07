# Review

A single page to review the tree against what it set out to be. Everything below is
reproducible with the commands shown; nothing in it is an estimate.

## What this is

A from-scratch, FusionFix-shaped reimplementation of **ZolikaPatch 7.65**, in two
sibling repositories:

| repository | purpose | state |
|---|---|---|
| `../fusionfix-skeleton/` | the featureless infrastructure image: FusionFix's `Event<Args...>` tiers, `Log`, `Game`, module/path helpers, the INI settings boundary, image-ranged scan/patch primitives, and a driver that installs nothing | complete, 6 commits, `ctest` 3/3 Release + Debug, artifact verified PE32/i386, base `0x10000000`, GUI subsystem, static CRT |
| this one | the 1:1 replacement, built on the same shape | 98 of 99 catalogue keys, 151 bodies, 15 tests |

The oracle for behaviour is `../game_refs/ZolikaPatch_original.asi.off`; `../readable/`
is a reference to read, never to copy.

## How to reproduce every number

```
cmake --preset win-x86
cmake --build --preset release
ctest --preset release                                   # 15/15

python tools/parity_report.py 9                          # per-key scoreboard
python tools/verify_bodies.py --original ../game_refs/ZolikaPatch_original.asi.off \
    --candidate build/win-x86/Release/ZolikaPatch.asi \
    --bodies tests/bodies.tsv --map build/win-x86/Release/ZolikaPatch.map
python tools/single_key_matrix.py                        # 9 builds x 99 keys
python tools/dump_matrix.py --markdown docs/matrix.md    # the 18-cell table
python tools/full_diff.py 9                              # attributes a full-cell difference
python tools/check_oracle_verified.py                    # guards the oracle-verified class
```

Current output:

```
build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)
151 body(ies), 0 failure(s)
single-key matrix: 891 cell(s): 890 exit 0, 1 expected fault (NoImpostorHeightCutoff, 1.0.0.0)
18-cell matrix: differing 0 on all nine images
```

## Modules

| module | keys | note |
|---|---|---|
| `stability` | 18 | crash fixes; hosts `BadPlayerModelCrashFix` with the recovered 38,455-entry name table |
| `vehicles` | 21 | includes the transcribed steer consumer that five keys depend on |
| `graphics` | 16 | includes the 146-instruction cutscene chain and the 157-instruction reflection body |
| `loading` | 18 | includes the save converter and the DLC discovery |
| `audio`, `misc`, `launcher`, `useless_checks` | 25 | native-layer consumers, launcher/DFA/ROM stages, the recovered RVA tables |
| `options`, `loader`, `config`, `patch`, `common`, `bodies`, `bytes`, `shared`, `native`, `ooc` | — | infrastructure, the recovered script payload, the native-call layer |

## What the evidence actually establishes

- **Write sets**: for every key, on every image with a baseline, the bytes this tree
  writes equal the original's, with branch displacements excluded because the baselines
  recorded one stand-in body address for all of them. `differing` is 0 on all nine
  images, full and per-option.
- **Bodies**: 151 transcribed bodies are compared instruction for instruction against
  the original ASI, whole, with immediates, displacements and lengths compared as
  values. Four also carry a byte-diff audit (`.agents/rewrite/05-body-audit.md`).
- **Faults**: each preserved fault is recorded in `docs/parity.md` with VA, instruction
  and what triggers it - the INI grammar faults, the 1.0.0.0 fault (which is why one
  matrix cell is partial), the corrupt-save paths, and the unguarded lookups.
- **Builds**: nine images, 891 single-key cells; the only fault is the original's own.

## What is *not* established, and cannot be from here

1. **Nothing has run against a live game.** The native-call layer, the rebuilt C++
   consumers (`SpeedupBugFix`, the save converter, the ROM service, the OOC init and
   handler, the NewFiles updates, the NewMemorySystem arena methods) are pinned by
   disassembly, instruction proofs and write-set equality - not by observation. The
   pieces transcribed since this page was first written (the steer consumer, the misc
   checkers, the DLC discovery, the OOC decoder and archive builder) are pinned by
   their proof rows and, for the OOC pair, by the offline archive fingerprints.
2. **`SkipLauncher`'s baseline is incomplete.** Its 57-byte difference is the reference
   tree's 7-row arm versus the original's 25 rows; the key is measured against the
   oracle and reported as `oracle-verified`, never as closed.
3. **The 1.0.0.0 full cell is partial.** The original faults at `NoImpostorHeightCutoff`
   there, so the dump stops; the per-option table carries that build instead.

## Deliberate divergences, each with its reason recorded

- Rebuilt-in-C++ bodies (no proof row, named in `docs/parity.md`): `SpeedupBugFix`'s
  body, the save converter and prepare, the OOC init and handler, the ROM service, the
  two NewFiles updates and the three NewMemorySystem arena methods. Each is compiler
  output (an exception frame, a TLS static-init guard or a call graph whose callees are
  container internals), and each is documented with the call sequence it reproduces.
- Published addresses: the mod writes real addresses; the *tools* substitute the
  baseline's placeholder, so the arms carry no knowledge of the harness.
- One behavioural note that is *not* a divergence: the headlight-cone check is
  transcribed and reads the word past its 512-slot table as the same published slot
  the original's `.data` holds (`docs/rebuilt.md` §2g), so the walk ends where the
  original's ends.

## Where to look first

- `docs/parity.md` - contracts, preserved faults, the baseline-incompleteness cases,
  and the rebuild/transcription decision for every body.
- `docs/matrix.md` - the generated 18-cell table with per-key attribution.
- `.agents/rewrite/05-body-audit.md` - the independent audit of all bodies.
- `source/stability.ixx`, `vehicles.ixx`, `graphics.ixx`, `loading.ixx` - the four
  largest feature modules, in the house shape.
