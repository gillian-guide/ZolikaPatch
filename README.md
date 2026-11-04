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

Foundation, no arms yet:

| path | responsibility |
| --- | --- |
| `source/common.ixx` | `FusionFix::Event<Args...>` and its tiers, the trace `Log`, `Game` (module, executable, image, version, paths), module/path helpers |
| `source/patch.ixx` | image-ranged `find_pattern`, recovered-signature normalisation, `pattern_str`, `to_bytes`/`from_bytes`, `IATHook`, `raw_mem` |
| `source/config.ixx` | the original's INI grammar (feather-ini-parser 1.41) and the `std::stoi` read boundary |
| `source/game.ixx` | build classification, build masks, the image scan range |
| `source/dllmain.cpp` | synchronous startup: context, INI, build, init tier |
| `tests/test_main.cpp` | the offline contract suite (63 checks) |
| `tests/pe_check.cpp` | PE32 / base / subsystem / static-CRT checks on the artifact |

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
