> **Historical document (macOS-era, pre-2026-10-11).** It is kept for provenance only.
> The current, generated parity statement is `PARITY.md` at the repository root
> (source of truth: `docs/parity/ledger.json`, produced by `tools/parity/ledger.py`).
> Do not read status claims from this file.

# Verification log

Verified on the available macOS host:

```text
clang++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -Iinclude \
  src/config.cpp src/patch_catalog.cpp src/pattern.cpp src/runtime.cpp \
  tests/test_main.cpp -o /tmp/zpatch_tests
/tmp/zpatch_tests
make clean test CXX=clang++
python3 tools/build_evidence.py --check
```

All commands passed. Apple clang 21 was used. The native Make test exercises INI
parsing, transactional parse failure, duplicate lookup, wildcard scanning,
invalid/missing patterns, file loading, and runtime lifecycle/state safety.

CMake and a MinGW cross compiler are not installed on this host, so those paths
could not be executed here. CMake defines portable `zpatch`/`zpatch_tests`
targets and, with a 32-bit Windows toolchain, `ZolikaPatch.asi`. Configuration
rejects a non-Windows or 64-bit ASI build. The native library does not compile
`dllmain.cpp`.

No original ZIP or extracted input is a build output. Evidence consistency was
rechecked after implementation. No patch-writing API exists and tests confirm
runtime state keeps `patches_applied == false`.


## Recovered primitive verification

The portable test executable now covers the following bounded transcriptions:

- `FUN_10003e50`: golden hash `police -> 0x79fbb0c5`, ASCII case folding,
  backslash normalization, quoted termination, empty input, numeric `hash:` and
  the observed zero-literal fallback.
- `FUN_10003b80`: component concatenation for `1.0.7.0`, `1.0.4.0`, and
  `1.2.0.43`; the reconstruction additionally rejects `uint32_t` overflow.
- `FUN_10055c70`: representative known/unknown classification, with the full
  recovered switch expressed directly in `src/game.cpp`.
- Guarded replacement: build mismatch, expected-byte mismatch, range failure,
  successful replacement, and guarded rollback.

These tests operate only on local `std::array` storage. They do not validate any
GTA IV patch site and do not activate process-memory writes.
