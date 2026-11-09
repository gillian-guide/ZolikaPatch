# Original ZolikaPatch ASI — obfuscation layers (reference)

Consolidated notes on how the original `ZolikaPatch.asi` (7.65) hides its data and code. Written
2026-10-13 (session 10) from the recovery work; each claim names the evidence. This is a *reference*
for future sessions — nothing here changes the shipped reimplementation.

## 1. Strings

- **Reversed INI keys**: the loader stores most option names reversed in `.rdata` (`"xiFesuoM"` =
  `MouseFix`, `"sexiFenecstuC"` = `CutsceneFixes`, ...) and flips them in place at runtime with
  `sub_10003B20` before calling the shared INI reader `sub_1005C160`. A few keys are plain literals
  (`HighQualityReflections`, `PathNodeCrashFix`, `PedModelsInCutscenesFix`). Evidence: session-8
  loader audit (`docs/callback_group_analysis.md` §0 table; `evidence/ini_records.json`).
- **Runtime-built pattern strings**: the callbacks/installers almost never store a pattern as text.
  Each pattern is assembled one character at a time: a seed byte (`mov ebx,0xNN`) is converted to
  decimal text by a `mov eax,0xCCCCCCCD` div-10 loop, parsed back with `strtol`, and appended
  (`sub_100577C0`) or used to reset the string (`sub_10057810`). Only the preamble/loader table
  keeps literal pattern strings, interleaved into a packed model-name string pool
  (`docs/callback_group_analysis.md` §"The obfuscated string builder"; `tools/ooc_decode_ref.py`
  reproduces the same builder semantics for the OOC callback).
- **Payload compression**: OutOfCommissionFix's replacement `finale2.sco` is an LZ-compressed
  163796-byte blob (custom opcode set, `05 FA` end marker, big-endian Adler-32 trailer) at
  `0x100B6798`; see `tools/extract_ooc_pak.py` + `docs/parity_and_improvements.md` §4.

## 2. Addresses

- Own function pointers and slot addresses are materialized with `lea reg,[X]` + `add/sub reg,Y`
  arithmetic so the plain value never appears in the binary:
  - HQR callback = `0x0FDD6C5B + 0x248735` (= `0x1001F390`), dest = `0x0FDD68FB + 0x248735`
    (= `0x1001F030`) — ctor `0x10023CC0`.
  - Reset stubs: `lea ecx,[0x1020B548]; sub ecx,0x78634` (= `0x10192F14`) — `sub_10023D70` and
    friends.
  - FastLoading callback = `0x0F89B808 + 0x783458` (= `0x1001EC60`) — ctor `0x1001EBA0`.
- Consequence: byte-searching for a slot address or a callback address finds nothing except the
  arithmetic chains; `tools/read_asi.py` + the chain scanners (`s9_obfuscated*.py` in the session
  scratch dir) were written for this.

## 3. Registration indirection

- Each "pusha-forwarder" option has a per-option **registration ctor** (called with `this`/module
  base in ECX) that stores `{callback, aux/dest, reset-stub}` into plugin BSS slots with
  **`this`-relative** stores, so no absolute reference to the slot exists.
- The loader arm calls the callback through a `pusha; call [slot]; popa; ret` trampoline.
- The callback runs once and then calls its reset stub, which zeroes the slots (one-shot
  self-clearing) — so a post-init memory read sees zero and a static scan sees "never written".
- Evidence: `docs/parity_and_improvements.md` §5 (the six-option mechanism note) and §4 (HQR).

## 4. Data / tables

- The loader's op table is a data blob of pattern/op rows (recovered by
  `tools/extract_rva_installs.py` / `tools/attribute_rva_builds.py`); build-specific constants are
  stored as 32-bit **hash salts** (e.g. `0x09399528` for the drive-by handle) that a build probe
  substitutes at load (`evidence/salt_probe.json`, `evidence/preamble_resolutions.json`).
- The salts are *functional* (per-build adaptation), not obfuscation; the reversed names, the
  char-built patterns and the `lea/sub` arithmetic are the deliberate obfuscation (no compiler
  would generate them).

## Gotchas learned from this

- The ASI's `.rdata` file offset is `VA - 0x10000000 - 0xC00`; `.text` likewise `VA - 0x10000000 -
  0xC00`. Two early literal reads were wrong because of this (use `tools/read_asi.py`).
- `GTAIV_12059.exe` has file offset == VA (base 0), unlike the other exes (`VA - 0x400000`); the
  scan tool now maps through the PE sections.
- A "never published" slot in a static scan is not proof of a no-op: check every `feature_callee`
  of the arm and prefer a live write-watchpoint (see the CutsceneFixes chain-B correction,
  `parity_and_improvements.md` §5).
