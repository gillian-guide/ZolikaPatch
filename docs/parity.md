# Parity

What has to match the original ZolikaPatch 7.65 ASI, and the faults that are
reproduced on purpose. A guard that turns one of these into a non-fault is a
fidelity bug, not a fix.

## Contracts already pinned by tests

| contract | evidence |
| --- | --- |
| INI grammar (feather-ini-parser 1.41): 255-byte line buffer, first-line BOM drops the file, `#`/`//` comments only, strtok-on-`=` keys that keep a trailing space, values that keep their spaces, empty values not stored, last duplicate wins, a re-opened section starts empty, case-sensitive sections and keys | `zp_tests`, `TestConfigGrammar` |
| Read boundary is `std::stoi` on the stored text with the arm's default | `zp_tests`, `TestConfigGrammar` |
| Build classification: the four version fields concatenated (`1.0.7.0` -> 1070, `1.2.0.43` -> 12043) and the original's switch; an unsupported executable is `unknown` and applies nothing | `zp_tests`, `TestBuilds`, `TestCatalogue` |
| Option catalogue: 99 keys with the shipped value, the absent-key default the arm passes, and the build mask indexed by the original's build id | `source/options.inc` (generated), `zp_tests` |
| With no arm installed the pipeline changes nothing on any supported image | `zp_dump_empty` (9 images) |

## Preserved faults

| fault | condition | site |
| --- | --- | --- |
| `std::invalid_argument` escapes the loader | an `[Options]` value that is not numeric (`Key=abc`, or `Key= ` with only spaces) | `Config::Integer` -> `std::stoi`, uncaught, exactly as the original's `std::stoi(ini.get(...))` |
| `std::out_of_range` escapes the loader | a numeric value outside `int` (`Key=99999999999`) | same |
| the whole INI is dropped | the file's first line starts with a UTF-8 BOM (0xEF) | `Feather::Parse`, the parser returns from its loop |
| null section dereference | a key line before the first `[section]` line | `Feather::Parse`, `(*current)[key] = value` with `current == nullptr` |
| a line longer than 255 bytes is split | the parser's 255-byte line buffer | `Feather::Parse` |

Two of these need care before they are claimed as 1:1:

- **Long lines.** feather 1.41 sets `failbit` when `istream::getline` fills the
  buffer and does not clear it, so its `while (!file.eof())` loop cannot advance:
  the original very likely spins rather than splitting the line. This tree
  continues with the rest of the line, which is the behaviour a caller can
  observe as "the file was read further". Not yet verified against the binary;
  the shipped INI's longest line is 192 bytes, so the path is unreachable from
  the shipped file. Verify with the oracle before this row is closed.
- **CRLF folding.** The original reads in text mode, so the CRT folds CRLF to LF
  before the parser sees the line; this tree folds it explicitly in
  `Feather::Load`. Behaviour is the same for values (`stoi` stops at a stray
  `\r`), but the equivalence is an assumption, not a measurement.

## Not reproduced yet

Everything below is unimplemented, so "parity" for it is unmeasured rather than
deviating:

- 76 of the 99 option keys (23 closed: 14 arms; `compare_dumps.py` against
  `readable/build/p3-final-dumps3/b*-per.txt` is the measure);
- the 107 hook bodies and their proof;
- the launcher / DFA / ROM stage order;
- the 18 full/per-option per-build dump hashes and the oracle differential.

## Transcribing a body

A body-bearing key is only committed when all four parts exist, because each one
changes either the write set or the fault behaviour:

1. **The body's asm**, transcribed into the owning module's tail section, with
   the recovered `BodyId` VA in a comment. A naked function in a module interface
   unit must have internal linkage (`static`); an external-linkage naked function
   is written to the BMI and every importer fails.
2. **The published module words** the body reads (recovered data: model hashes,
   flags, and the resume pointers). The arm publishes them *before* it installs
   the site — the original reads a site's original bytes for its resume, so
   publishing after the write is the CPU-spinning self-jump fault.
3. **The registry entry**: `Bodies::Register(&body, recoveredVa)`, so the dump
   rewrites the branch displacement to the original's coordinates.
4. **The proof manifest row**: `name <recovered VA> <candidate VA> <length>`, and
   `verify_bodies.py` must report PASS for it.

The first body to transcribe is `abi_building_dynamic_shadows` (recovered VA
`0x10005AF0`, `sub_10005af0`): it reads `[edi]`, compares it against two
published model hashes, jumps to `resume_b` on a match, otherwise computes
`([edi+28h] >> 6 & 0xF) - 1` and jumps to `resume_a`. Its arm also writes the
shadow-size operand byte (`0x104` -> `0x100`), which is the half already
understood; the key stays open until the body half exists.

