# ZolikaPatch v7.65 reconstruction

A clean-room, C++17 reconstruction of the safely recoverable infrastructure.
It is **not source-equivalent** and deliberately does not apply binary patches.
Original ZIP/extracted artifacts are read-only evidence and are never build outputs.

## Implemented

- Portable INI parser/file loader: BOM and CRLF support, comments, trimming,
  sections, empty values, retained duplicates (last value wins on lookup).
- Bounded read-only byte-pattern scanner using hex bytes and `?`/`??` wildcards.
- Exact, portable primitives recovered from short decompiled routines: RAGE-style
  model/path hashing (`0x10003e50`), dotted-version decimal composition
  (`0x10003b80`), and the observed build classification switch (`0x10055c70`).
- A caller-owned-buffer patch primitive that refuses unknown/mismatched builds,
  out-of-range writes, unequal replacement lengths, and expected-byte mismatches;
  rollback is guarded too. It is not connected to process/game memory.
- Thread-safe runtime state which loads `ZolikaPatch.ini` and explicitly reports
  that no patches were applied.
- Windows ASI DLL entry target. Initialization runs on a worker after `DllMain`
  to avoid file I/O under loader lock. It loads the INI beside the ASI.
- Portable tests and native Make/CMake builds.

## Build and test

Native clang++/g++ without CMake:

```sh
make test
# or explicitly: make CXX=clang++ test
```

CMake (portable library/tests):

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

32-bit MinGW ASI (required because the observed binary is PE32/i386):

```sh
cmake -S . -B build-mingw \
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DZPATCH_BUILD_ASI=ON
cmake --build build-mingw
# Alternatively, when i686-w64-mingw32-g++ is on PATH: make mingw
```

The generated Windows target is `ZolikaPatch.asi`. It only loads configuration; it performs no code/data writes. The tested guarded
buffer primitive is intentionally not invoked by runtime initialization and does
not alter page protections. No game patch sites, hooks, signatures, expected
bytes, or replacements are claimed recovered, so all 98 catalogued gameplay
features remain unimplemented.

Evidence regeneration remains available with `python3 tools/build_evidence.py --check`.
