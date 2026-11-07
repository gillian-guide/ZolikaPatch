# readable-fusion

A from-scratch reimplementation of **ZolikaPatch 7.65** for GTA IV and EFLC,
written in the shape of
[GTAIV.EFLC.FusionFix](https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix)
(GPL-3.0, ThirteenAG): C++23 module interface units under `source/`,
`source/common.hxx` as the shared include header, `source/dllmain.cpp` as the
single plain translation unit, and the vendored libraries pinned under
`external/`.

The mod has to be a drop-in replacement: the same INI file, sections, keys and
defaults, the same installs and hook bytes on every supported game build, the
same launcher stages, and the original's faults.

## Status

Complete against the oracle as far as static evidence reaches: **98 of 99 catalogue
keys** closed on 1.0.8.0 (the 99th is implemented in full and measured against the
original rather than a baseline that records a partial arm), **151 hook bodies**
transcribed and proven instruction for instruction, **18 tests**, and every differing
byte of every proof row classified as a slot, an immediate or a registered table with
nothing unclassified.

| path | responsibility |
| --- | --- |
| source/common.ixx | FusionFix::Event<Args...> and its tiers, the trace Log, Game (module, executable, image, version, paths), module/path helpers |
| source/patch.ixx | image-ranged ind_pattern, recovered-signature normalisation, pattern_str, 	o_bytes/rom_bytes, IATHook, 
aw_mem |
| source/config.ixx | the original's INI grammar, the per-model light tables, and the std::stoi read boundary - including the faults |
| source/game.ixx | build classification, build masks, the image scan range |
| source/loader.ixx | the publish / install / post tiers and the option gate |
| source/options.ixx | the generated catalogue of 99 keys (source/options.inc) |
| source/stability.ixx, ehicles.ixx, graphics.ixx, loading.ixx | the four largest feature modules: crash fixes, vehicle and steer, rendering and lights, loading and saves |
| source/audio.ixx, misc.ixx, launcher.ixx, useless_checks.ixx | audio keys, the misc fixes, the launcher stage (table, DFA, ROM), the recovered useless-check tables |
| source/native.ixx | the original's script-native call layer and its salt-probe rule |
| source/ooc.ixx | the OutOfCommissionFix payload: the decoder, the archive builder and the mount |
| source/shared.ixx, odies.ixx, ytes.ixx | the recovered config-value resolver, the body and table registries, the opcode constants |
| 	ests/*.cpp | the contract suite, the PE checks, and the behavioural harnesses (bodies, saves, arena, native guards, speedup clock, INI faults, matrix, oracle-verified) |

## Evidence

Everything claimable here is reproducible; start with the review page, then the two
measurements behind the numbers.

| document | what it establishes |
| --- | --- |
| docs/review.md | the one-page review: what exists, how to reproduce every number, what is *not* established |
| docs/matrix.md | the generated 18-cell per-build dump table, every residual attributed to a key |
| docs/parity.md | contracts, preserved faults, the baseline-incompleteness cases, and the transcription decision for every body |
| docs/rebuilt.md | the pieces that are rebuilt rather than transcribed, each with the instruction that blocks it, how it was verified, and what stays unproven |

## Build and test

```
cmake --preset win-x86
cmake --build --preset release
ctest --preset release
```

Win32/MSVC only, static CRT, base `0x10000000`, windowed subsystem. `Release`
produces the shipped ASI (silent, like the original); `Trace` produces the same
binary with `ZolikaPatch.trace.log` enabled for the parity tooling.

## Licence

GPL-3.0, see `LICENSE`. The layout and the `Event<Args...>` shape follow
GTAIV.EFLC.FusionFix by ThirteenAG (GPL-3.0). The INI grammar reproduced here is
the contract of feather-ini-parser 1.41 by Turbine1991, which the original mod
linked; the implementation is new. Vendored libraries under `external/` keep
their own licences.
