> **Historical document (macOS-era, pre-2026-10-11).** It is kept for provenance only.
> The current, generated parity statement is `PARITY.md` at the repository root
> (source of truth: `docs/parity/ledger.json`, produced by `tools/parity/ledger.py`).
> Do not read status claims from this file.

# Reconstruction status

## Scope and safety

This pass organizes facts recoverable from the extracted v7.65 package and the
existing Ghidra export. Decompiled C is deliberately not copied into `src/`:
it contains unresolved Ghidra types, overlapping globals, compiler/runtime
routines, and guesses that would falsely imply buildability. Original ZIP files
and extracted artifacts are inputs only.

## Confidence policy

- **Confirmed:** directly visible in PE metadata, INI/readme text, or a short,
  unambiguous decompiled routine.
- **Probable:** behavior is structurally clear but types/names are not recovered.
- **Speculative:** inferred from feature names or call context; keep out of source.

Reconstructed source should cite a virtual address and evidence file for every
nontrivial behavior. Preserve raw addresses in comments until symbols and calling
conventions are independently verified.

## Completed in this pass

1. Created an isolated reconstruction tree and reproducible evidence generator.
2. Captured SHA-256 hashes and PE/Ghidra/INI summary data.
3. Exported every INI assignment with its section and line number.
4. Exported the Ghidra function index unchanged plus a coarse classification that
   separates anonymous candidates from obvious CRT/thunk/exception scaffolding.
5. Implemented and tested portable INI loading, bounded pattern scanning, and safe runtime state.
6. Added native Make/CMake tests and a 32-bit Windows ASI entry target that loads configuration only.
7. Recovered and tested the exact name-hash transform at `0x10003e50`, including
   ASCII case folding, slash normalization, quoted input, and nonzero `hash:` literals.
8. Recovered the decimal file-version composition at `0x10003b80`, implemented
   safe Windows version-resource discovery, and recovered the complete known-version
   switch at `0x10055c70` (12 values, including internal enum values).
9. Added an inert guarded-buffer patch/rollback primitive. It requires an explicit
   matching known build and exact expected bytes and has no page-protection or
   process-memory integration.

## Important limitations

- Only the 995 functions selected/exported by the existing Ghidra workflow are
  indexed; this does not prove complete function discovery.
- `functions.csv` reports one timeout at `0x10024600`; re-decompile it separately.
- 72 rows have names not beginning with common anonymous/handler prefixes, but
  most are MSVC/CRT/STL identifications rather than product-level symbols.
- The INI has an apparent repeated typo, `Car2591Name`, in both vehicle sections;
  preserve it as evidence rather than silently correcting it.
- Runtime testing requires a legitimate 32-bit GTA IV environment and must not be
  conflated with static verification.

## Next bounded checkpoints

1. Split `0x100523c0` into documented phases by call graph and global writes.
2. Recover the caller(s) of the five patch primitives and build a patch-site
   manifest: signature, offset, original bytes, replacement, option gate/version.
3. Re-run the single timeout with a larger per-function decompiler budget.
4. Identify the user `DllMain` target behind the CRT dispatch pointer rather than
   naming CRT entry stubs as application initialization.
5. Recover and test a versioned patch manifest (signature, unique match, expected
   bytes, replacement, rollback) before any in-process patch activation.

`query_current_game_version` is now the bounded Windows implementation of the
`0x10003b80` evidence: it reads `VS_FIXEDFILEINFO` from the host executable,
validates signature `0xfeef04bd`, composes the four product-version words, and
classifies the result. Non-Windows builds fail closed with zero/unknown outputs.

## Feature coverage

The 98 `[Options]` keys are an evidence catalog only: **0/98 gameplay patches are
implemented or enabled**. Implemented behavior is limited to INI infrastructure,
read-only pattern matching, hashing, compact-version/build classification, an
inert caller-owned-buffer guarded replacement primitive, and the portable
reimplementations of all 67 recovered hook bodies (see "Hook trampolines and
detour stubs recovered" below). The ASI path loads the INI and leaves
`patches_applied == false`. No executable-memory write path exists.

## Vehicle light tables
Implemented the two 512-slot table loaders from `FUN_100523c0`: nonempty model
names are hashed, popup degrees become radians, and headlight offsets remain
floats. Empty names preserve holes and malformed numeric values reject startup
atomically. Shipped INI verification found 0 popup rows and 78 offset rows.
No game-memory writes or vehicle hooks are enabled by this loader.

### Vehicle-table consumers
Pure model-index resolver adapters now reconstruct the selection portions of
`FUN_10044310` and `FUN_1001ad20`. Popup support defaults to pi/2, scans all
512 override slots (last matching duplicate wins), and preserves the special
built-in hash path. Headlight offsets default to 2.0 and stop at the first zero
hash (first matching entry wins). Tests cover holes, duplicates, defaults, and
unresolved models. Negative model indices are rejected as a deliberate safety
difference from original sentinel comparisons. These are not game hooks.

## IDE custom-flag decoder
`ide_flags.cpp` reconstructs the isolated token behavior of `FUN_10003fa0`:
`popuplights` and `siren` are exact lowercase tokens, `wheelangle:` is
case-insensitive, numeric prefixes are accepted, duplicates keep the first value,
and each table is capped at 512 entries. Input model hash zero is rejected as a
documented safety adaptation. Tokens are passed without the leading `+`; the
original IDE-reader hook and steering-wheel animation remain unimplemented.

## Embedded model-name catalog
Recovered all 38,455 names from the pointer table at `0x100de770`, retaining
original order. `model_name_from_hash` reproduces `FUN_10003f00` first-match
lookup and `**UNKNOWN**` fallback. The generator validates the evidence ASI
SHA-256 before extraction and has a deterministic `--check` mode.

### Siren eligibility
`model_has_siren` reconstructs the decision portion of `FUN_10005f70`, checking
13 built-in emergency models in original order and then the zero-terminated,
512-entry custom siren list. Tests cover built-in and custom matches, first-zero
termination, absent callbacks, and invalid model indices. Original game-global
state reads/writes and the eligibility hook remain disconnected.

### Steering-wheel calculation
The steering branch of `FUN_10004c70` is reconstructed as a pure calculation:
custom tilt uses the first model-index match before a zero sentinel and converts
degrees to radians; turn uses float steering/limit division multiplied by double
`-pi` (constant at `0x101523a8`). Invalid indices, zero divisors, and nonfinite
results fail atomically as deliberate safety differences. Bone lookup, transform
reset, rotation callbacks, engine animation, and suspension remain unimplemented.

### Engine animation state machine
`vehicle_animation.cpp` reconstructs the state/math branch of `FUN_10004c70`:
phase advances by timestep times +/-0.5 only when timestep differs from the
previous value; crossing +/-0.01 reverses direction without clamping. Rotation
scale is zero unless flag 0x08 enables RPM*0.75+0.25; flag 0x10 overrides with
1.0. Nonfinite values fail atomically. Original bone transform callbacks and
option-gated game update scheduling remain disconnected.

### Suspension callback arguments
`calculate_suspension_commands` recovers the nine command records from the
suspension branch of `FUN_10004c70`: vehicle type 5 and option gating, four
travel commands scaled by float 100.0 at `0x10152388`, four signed half-scale
commands, and one final half-scale command. IDs, order, modes and relative flags
are regression tested. Source field names are provisional; original repeatedly
reads timestep and invokes game callbacks, whereas this helper takes one stable
input snapshot. No bone transforms or game callback invocation are implemented.

### Named vehicle-bone lookup
`find_vehicle_bone` reconstructs the case-insensitive first-match search from
`FUN_10003f40` over caller-owned name/ID records. Original records have observed
stride 0xe0 and ushort count; no raw-memory layout adapter is asserted. ASCII
comparison is deliberate and may differ from locale-sensitive MSVC `_stricmp`
for non-ASCII names. Tests cover steeringwheel/engineblock, duplicates, misses,
empty record ranges, and null inputs.

### Bone rotation reset
`reset_vehicle_bone_rotation` reproduces the nine float writes before steering
and engine rotation callbacks in `FUN_10004c70`. Only indices 0/1/2, 4/5/6,
and 8/9/10 become identity; fourth lanes and translation are untouched. Tests
check every written/preserved lane and idempotence over caller-owned storage.
No assumptions about callback axis orientation are made and no game-owned
transform pointers are accepted.

### Guarded NOP fill and relative jump
`fill_nop` reproduces `FUN_10001680`'s 0x90 fill and `install_relative_jump`
reproduces the installer at 0x10004110: `E9` plus little-endian
`destination - (site + 5)`, verified against read-only disassembly. Both operate
only on caller-owned buffers; page-protection toggling (VirtualProtect) and
in-process address resolution are intentionally outside this API. Tests cover
fill bounds, jump encoding target arithmetic, null destination and short ranges.

### Signature-token semantics correction
Read-only decompilation of `FUN_10003290`/`FUN_100030a0` shows signature tokens
are split on spaces and converted with `strtoul(token, nullptr, 16)`; the low byte
is used and only exact `?`/`??` become wildcards. `pattern.cpp` now mirrors that:
single digits, `0x` prefixes, sign wrap, and non-hex tokens such as `GG` reduce to
0 rather than being rejected. Tests cover these cases (including `GG` matching a
zero byte) plus 0x-prefixed and wrapped values.

### FusionFix compatibility probe
`compatibility_module_lacks_signature` reconstructs `FUN_10003e20`: it reads a
32-bit value at image offset 0x20d3 and a 16-bit value at 0x20d7, returning true
when the module is present but the signature is absent. Disassembly corrected the
decompiler offsets (0x20d3/0x20d7, values 0xc7c47589/0x2446). Tests cover absent,
too-small and malformed images plus both mismatch directions. Module lookup itself
(GetModuleHandleW) stays caller-side; no OS handle is touched here.

### Game process detection
`select_game_process_id` reconstructs the ordered, case-sensitive
`PlayGTAIV.exe` scan in `FUN_10003a10`; `find_game_process_id` wraps Windows
Toolhelp32 (`CreateToolhelp32Snapshot`/`Process32First`/`Process32Next`) and
fails closed elsewhere. Tests cover first-match precedence, case sensitivity,
empty lists, null input, and the non-Windows result. The Windows branch is
cross-compiled but not executed on this host.

### Game hash verified against disassembly
Read-only disassembly of `0x10003e50` confirms the implemented transform exactly:
optional `hash:`-prefixed `strtoul(...,0)`, single quoted-string handling, ASCII
uppercase folding and backslash normalization, `(h+c)*0x401` then `h^(h>>6)`,
then `h*9`, `h^(h>>11)`, `h*0x8001`, and the `>=2` correction. `FUN_10003f00`
confirms the 0x9637-entry table scan returning the first match or `**UNKNOWN**`.
Evidence: ZolikaPatch_decompiled/checkpoints/HASH_DISASM.json.

### Build-switch values verified
Disassembly of `0x10055c70` writes 1,5,6,7,8,9,0xa,0xb,0xc,0xd,0xf,0x12 for the
known compact versions and 0 for unknown, matching `GameBuild` ordinal values
exactly. The gate `(version-0x4b1) <= 0x2e16` marks the supported window; 12043 is
handled as an exact special case. Evidence: checkpoints/VERSION_SWITCH_DISASM.json.

### Protected write primitives
Checkpoint `protected_writes` confirms FUN_1005ad60 (1 byte), FUN_1005adb0
(2 bytes) and FUN_1005b650 (4 bytes) all set PAGE_EXECUTE_READWRITE (0x40)
around a single store, then restore the saved protection. `protected_write`
reproduces that sequence with a caller-supplied protection callback, so it is
testable and never modifies its own process pages. Tests verify restore target,
size handling, byte preservation, and rejection of unsupported sizes/null input.

### Protected patch application
`apply_protected_patch` combines the build/expected-byte gates with the recovered
protection-toggle write path: after validation it writes each replacement byte
through the caller-supplied protection callback. Tests confirm both bytes land,
the callback is invoked per byte (twice each), expected-byte mismatch rejects,
build mismatch rejects, and null/empty input rejects. Target address resolution
and live game memory remain the caller's responsibility.

### Versioned patch manifest
`patch_manifest.hpp/.cpp` implements the missing link between the recovered
primitives and the 98 options: each `PatchSite` carries a name, signature,
signature offset, expected bytes, replacement bytes, a 32-bit build mask and
comma-separated option gates. `apply_patch_manifest` resolves signatures, gates
on build and options, verifies expected bytes and writes through the protection
path, recording the resolved address so `rollback_patch_outcomes` can restore
bytes in reverse without rescanning (the patched signature no longer matches).
Tests cover apply, rollback, build gating, option gating (including substring
keys), and address recording. The manifest is a data structure only; no game
addresses or replacement bytes have been asserted as recovered yet.

### Option-to-code map
`tools/extract_option_map.py` (with `--check`) maps all 98 `[Options]` keys to
the code locations that reference their string constants, writing
`evidence/option_references.json` for the SHA-pinned evidence ASI. 96 keys are mapped (MSVC string pooling stores many literals reversed, which the
extractor now detects); all resolve to the central `FUN_100523c0` loader. Two
keys (`CarDynamicShadowFix`, `EpisodicWeaponSupport`) have no standalone pooled
literal and remain recorded as unresolved rather than guessed. This map
is the input needed to build real PatchSite entries instead of invented ones.

### Option-to-flag mapping: not attempted automatically
A heuristic scan (push of each option string VA, then a fixed 60-instruction
window for global stores) was evaluated and rejected: windows cross into
neighbouring option handling, producing conflicting candidate flags
(e.g. 0x10159c15 for one key and 0x10159c16 for its sibling) that cannot be
distinguished without following real control flow. No flag mapping was committed,
because committing it would fabricate behavior. `evidence/option_references.json`
remains the only option map, and it records locations rather than claiming
malformed-window guesses. `option_flag_candidates.json` is kept as rejected work.

### Option-to-flag mapping (bounded control flow)
Improved mapping: each option-string push starts a segment that ends at the
*next* option-string push, which is real control-flow adjacency in the linear
loader, not a fixed window. Across these bounded segments 10 keys resolve to a
single global flag; the rest reference several. Cross-validation is decisive:
`MovingSteeringWheelSupport`->0x1015ad40, `ShakingEngineBlock`->0x1015ad41 and
`MovingPlaneParts`->0x10159c17 are exactly the globals read by the already
reconstructed `FUN_10004c70`, and `AddOptionsInMenu`->0x10158038 matches the
`setne [0x10158038]` seen in disassembly. Full map in evidence/option_flag_map.json.
Keys with multiple candidate globals remain provisional and are not asserted.

### Option-flag runtime gating
`option_flags.cpp` turns the recovered mapping into runtime gates. Ten confirmed
bindings (each cross-validated by an independent read site) drive 64 stable flag
slots via hashed global addresses. `apply_option_flags` seeds defaults to enabled
then applies present INI values ("0" disables, any other non-empty value enables,
matching the loader's `setne`/`atoi` behaviour). Tests cover disable/enable/
nonzero-enable, absent-key default, binding count and confidence. Provisional
multi-global options are deliberately excluded from the confirmed table.

### Option handling taxonomy (bounded segments)
Refined rule: within the segment from an option's string push to the next option
push, take the first instruction that *writes* a recovered global (setXX or a
mov/and/or/xor with the global as destination). This reproduces every
cross-validated case exactly. Result: 11 options set a global flag, while 85 call
dedicated functions (`FUN_1004ac90` etc.) instead. So most features are function
calls, not flag writes; only the 11 global-flag options are exposed through
`option_flags.cpp`. A Python parity test asserts the code table matches
`evidence/option_flag_map.json`, preventing drift. An earlier opcode/address bug
for mov-style stores was found and fixed by that same parity check.

### Option dispatch architecture (robust finding)
After each option value is parsed with `atoi`, the loader either writes a single
recovered global (11 options, listed above) or calls a dedicated handler. The
remaining options therefore gate via function entry points rather than flags.
A per-option handler list was generated but rejected: after filtering CRT string
builders, `operator_new`, `memset` and security helpers, the residue still mixes
CRT and unresolved code, so it cannot be asserted as recovered behaviour. Only
the 11 global-flag bindings are treated as confirmed; handler mapping requires
control-flow analysis (call graph from the loader's branch targets), not linear
proximity. No handler mapping was committed.

### Option handlers and recovered patch signatures (major)
Decoded the loader's dispatch pattern: after parsing an option value the loader
compares it to 1 (`cmp esi,0x1` / `jne`) and conditionally calls a feature
handler. This yields **74 of 98** options mapped to distinct handler functions
(evidence/option_handlers.json). Decompiling sample handlers (`FUN_10006350`
CopSirensFlag, `FUN_10044590` PopupHeadlightsFlag, `FUN_1003d670` NoLoadingSleep)
shows each calls `FUN_10003830("SIG", GetModuleHandleA(0))` and then writes via
`VirtualProtect`+store, i.e. these are the real patch sites. Joining the handler
map with signature literals from the existing decompilation recovers signatures
for **61 of 74** handled options (evidence/option_signatures.json). Reproducible
via tools/extract_option_signatures.py --check. Replacement bytes and match
offsets are not yet transcribed, and no live game memory is touched.

### Handler patch data extraction
All 75 handler functions were decompiled (checkpoints/option_handlers/batch_0..3,
75/75 ok). Parsing them yields, for 62 options, the signature plus the write
behaviour: 30 handlers install a relative `E9` jump to a fixed ZolikaPatch
trampoline address (e.g. AimProjectileCrashFix -> site +0, jump to 0x1000558b),
and others write literals. Full parsed data in evidence/option_patch_data.json.
This is the real patch payload the manifest needed. Offsets are relative to the
signature match start; several handlers use the match start directly.

### Recovered patch-site table (generated + tested)
`src/recovered_patch_sites.inc` is generated from the handler evidence by
`tools/extract_patch_sites.py --check`: 61 options with a recovered signature,
a resolved relative-jump target where the handler installs one, and the count of
distinct targets. `recovered_sites.cpp` exposes the table and a unit test asserts
every signature parses under the observed matcher (never `parse_error`) and that
jump sites carry a nonzero target. Image base for these targets is the original
0x10000000; translating them into the running game module requires the live base,
which is the caller's responsibility.

### Recovered literal patch operations (generated + tested)
Parsing all 75 handler bodies recovered **127 concrete write operations**: the
signature, the offset from the match start, the write length and the replacement
bytes. Breakdown: 57 pure NOP fills, 38 `E9`/`E8`/`EB` jump installs and 32 other
writes. `src/recovered_patch_ops.inc` is generated by
`tools/extract_patch_ops.py --check` and exposed via `recovered_ops.cpp`; a unit
test asserts every signature parses under the observed matcher, every replacement
token is a valid byte, the token count equals the declared length, and offsets
stay in range. These are the actual bytes the original writes, recovered
read-only. No live game memory is touched and the 24 handler-less options remain
unmapped.

### Recovered-operation application path
`recovered_apply.cpp` applies the recovered literal operations to a module
image: for each enabled option it locates the signature, writes the replacement
bytes at `signature + offset` through the caller-supplied protection callback and
records address/byte count. The original handlers do not verify site contents
before writing, so this path deliberately omits an expected-byte gate (unlike the
versioned manifest). Tests build a synthetic module containing a recovered
signature, verify the write lands at the expected address and that a disabled
option performs no protection call. This is the recovered write mechanism applied
to caller-owned memory; live game translation and version gating remain external.

### Handler recovery for flag-only options
Four options that set globals also install patches through a second dispatch
shape (`cmp [ebp-0xd1],0`/`je` plus `cmp eax,0x1`): Patch6and7RemoveModChecks
-> FUN_10044710, SkipIntro -> FUN_10045c50, SkipMenu -> FUN_10046640,
BorderlessWindowed -> FUN_10005970. These are now added to the handler map
(78 of 98). Their bodies reveal two new details: SkipIntro patches at a
*negative* offset from the match (`match-6`), and Patch6and7RemoveModChecks
patches by module-relative offsets (`DAT_1015ad70 + 0x392e`) rather than a
signature. The current recovered-ops extractor only handles non-negative
signature offsets, so these are recorded but not yet emitted as operations.

### Evidence parity test and handler count
Handler map is now 78 of 98 options and the signature table 63. Added a Python
parity test asserting the generated tables (`recovered_patch_ops.inc`,
`recovered_patch_sites.inc`) match their JSON evidence counts, so they cannot
drift silently; the C++ test now asserts a lower bound rather than a brittle
exact. extract_option_signatures.py was widened to also read the handler
checkpoint decompilations.

### Expanded operation model: signed offsets and module-relative writes
Confirmed `DAT_1015ad70 = GetModuleHandleA(0)` in the loader, so handler writes
like `DAT_1015ad70 + 0x392e` are module-relative RVA patches. The extractor now
recovers three shapes: signature + non-negative offset, signature + *negative*
offset (decompiled as `base + -6`, e.g. SkipIntro), and signature-less
module-relative RVA writes. Total recovered operations rose from 127 to **192**
(138 signature-based including 23 negative-offset, plus 54 RVA).
`recovered_ops.hpp` now carries a signed offset and a `module_rva` field;
`recovered_apply.cpp` handles negative offsets (with an underflow guard) and RVA
writes (with a bounds guard). The Python parity test now reads the new evidence
file; it caught this exact staleness when the table changed.

### Option classification audit (88 of 98)
Full classification of the 98 `[Options]` keys: 10 set a single global flag,
77 have a recovered handler, 1 does both, and **10 remain unresolved**
(CarDecalsFix, CarDynamicShadowFix, EpisodicWeaponSupport, FixPCIDKickbug,
ForceCarHeadlightShadows, FreezeCarFix, NoImpostorHeightCutoff,
NoLODLightHeightCutoff, NoPhysicsSleep, OverrideGTADatPathTo). Those 10 are not
cleanly resolvable by linear segment analysis: several set a local option bitmask
(`or DWORD PTR [ebp-0xd8]`), two have no standalone pooled string, and the rest
defy boundary detection. Their attempted classification is recorded in
evidence/option_alt_consumption.json but **not** asserted as recovered behaviour.
Proper resolution needs per-option control-flow tracing. This is the honest state;
no mapping was fabricated to reach a round number.

### Bitmask/local-consumption analysis
Most options are read and applied inline; the high-value remaining unknown is the
10 unresolved keys. Their segments set a local temp (`or DWORD PTR [ebp-0xd8]`)
that the loader does not persist as a named global, and two keys have no
standalone pooled string. Store-site scan also surfaced `0x10159b85` at the
loader epilogue (a module-presence check, not an option) and confirmed`0x1015be04`
is the build slot. No additional option->flag bindings were asserted, because the
local-temp path cannot be attributed to a specific key by linear analysis.

### Negative-offset and RVA path tests
Added targeted tests for the two newly added apply paths that were previously
uncovered: a negative-offset signature operation must write *before* the match
address (using the recovered SkipIntro signature), and a module-relative RVA
operation beyond the module must be rejected by the bounds guard with the
"rva outside module" status. Also hardened the signature-token decoder used by
the tests to tolerate single-character tokens. All 6 Python tests and the native
suite pass; sanitized build passes.

### Option build gates recovered
Handlers that check the build slot (`DAT_1015be04`) before patching are now
extracted into evidence/option_build_gates.json: 10 options, e.g. LoadDLCs
(builds 7/8/9), Patch6and7RemoveModChecks (7/8), EpisodicVehicleSupport (8/9),
VSyncFix (excludes 1-5/9/13), LaunchWithPlayGTAIV (1/5/7/8 and not 9). The gate
comparisons map to `GameBuild` ordinals (v1060=7, v1070=8, v1080=9), so these can
drive the manifest's per-site build mask. Also confirmed the loader builds a
separate bitfield struct (`[ebp-0xd8]` -> `[ebp-0xf8]`) for four options
(Patch6and7RemoveModChecks=1, SkipIntro=4, SkipMenu=8, BorderlessWindowed=0x10).

### Build-gated apply path
Recovered positive build masks are generated into `src/recovered_build_gates.inc`
(7 options: DualVehicleHeadlights {1..6}, EpisodicVehicleSupport {8,9}, LoadDLCs
{7,8,9}, Patch6and7RemoveModChecks {7,8}, RemoveUselessChecks {5,8,9},
RemoveRGSCLoginRequirement {9}, SuperLODFix {8,9}) and exposed via
`recovered_build_gates.hpp`. `apply_recovered_operations_for_build` skips
operations whose option is gated out for the current build; options with no
recovered positive gate are allowed and reported as `known=false` rather than
silently restricted. Mixed `!=` exclusion gates (BenchmarkFix, LaunchWithPlayGTAIV,
VSyncFix) are recorded but intentionally not turned into masks. Tests cover
allowed/denied/unknown gating and end-to-end gated application.

### Inline patch installs for bitfield options
Traced the loader's present-bitfield struct: options set bits in `[ebp-0xd8]`
(copied to `[ebp-0xf8]`), and the loader itself consumes that bitfield with an
*inline* jump installer around 0x1005316e (writes `E9` + relative displacement to
a ZolikaPatch trampoline, e.g. 0x1001b2e0), rather than calling a named handler.
This is why the remaining keys (CarDecalsFix, NoImpostorHeightCutoff,
NoLODLightHeightCutoff, NoPhysicsSleep and the bitfield set) resisted the
handler scan: their patch data lives inside the loader function body and must be
extracted from that region, not from a separate handler. Known bits:
Patch6and7RemoveModChecks=0x1, SkipIntro=0x4, SkipMenu=0x8, BorderlessWindowed=0x10.

### Inline patch sites decoded
Two loader-inline jump installs are now fully decoded:
- DrivebyFOVFix: finds signature `F3 0F 10 46 60 F3 0F 5C CA` (via
the shared match helper), writes `E9` + (`0x100064b0 - site - 5`) at the match
and stores the trampoline pointer at global 0x1015bdf4.
- CarDecalsFix: obtains its site from a vector's first element (built by
FUN_1005dec0), writes `E9` + (`0x1001b2e0 - site - 5`) at that address.
Both share the exact write sequence recovered in fun_10004110. 
Now generated reproducibly by tools/extract_inline_sites.py --check, which detects
loader-inline E9 installs, their signature literal and their computed jump target
(mov eax,TARGET ... sub eax,5). Both entries carry a resolved jump target; the
DrivebyFOVFix signature is recovered automatically.

### Inline literal-write sites
A second inline shape exists: handlers/loader blocks that find a signature then
write a literal (not a jump). NoPhysicsSleep writes a zero dword at match-0x26
with signature `0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 00`. A further
inline literal write at 0x10052f6e (signature `0F 10 16 F3 0F 10 5E 04 ...`)
precedes the first option push, so it belongs to the loader preamble and is *not*
attributed to any option. Both are recorded in evidence/inline_patch_sites.json;
the preamble site is noted but not claimed as an option binding.

### Loader preamble: signature-based game address resolution
Before any option runs, the loader resolves game internals by signature and
stores pointers/salts into its own globals: 0x1015bde0 (table entry via
match+0xa+[match+6]), 0x10159c74, 0x10159c0c (with cmovne fallback),
0x1015ad78 (match-4), 0x10159b88, 0x1015d63c and 0x10159cdc (both match-0x30).
When its first signature fails, it seeds ten fallback hash salts
(0x10158054=0x6b86410e and peers). This is the mechanism behind the previously
opaque globals read by FUN_10004c70 (steering/engine). 
Now generated reproducibly by tools/extract_preamble.py --check (8 resolutions
and the 10 fallback salts), with the addresses being original-image VAs.

### Preamble extraction hardened
Fixed a generator bug: a fixed-width window made each resolution absorb its
neighbour's writes (e.g. the 0x1015bde0 entry appeared to also write
0x10159c74). Each block is now bounded at the next signature load, and cmovne
fallback stores are captured. Added a Python test asserting the 8-entry shape,
signature syntax and that the fallback entry carries exactly ten salt writes.

### Preamble resolutions as a tested table
Generated `src/recovered_preamble.inc` (17 rows: 7 resolve targets, 10 fallback
hash salts) from the preamble evidence, exposed via `recovered_preamble.hpp`,
and asserted in a unit test (7 resolves with valid global addresses, 10 nonzero
salts). extract_preamble.py now verifies *both* the JSON and the C++ table in
--check mode, so they cannot drift. A neighbouring-entry contamination bug in
the table builder was found and fixed by bounding each block at the next
signature load.

### Functional identification of resolved globals
Cross-referencing consumer sites identifies what each preamble resolution is:
- 0x10159c0c = **model lookup function pointer**, called as `(*fn)(hash,&out)`
  from FUN_10005f70, FUN_10004c70, FUN_10006260, FUN_1001ad20, FUN_10037ac0,
  FUN_1003d930 and the loader; this is the single hook every name/hash-based
  feature depends on (26 references).
- 0x1015ad78 = model-info table base, indexed by short model id.
- 0x10159b88 = component resolver function pointer.
- 0x1015d63c = steering-wheel transform setter callback (matches the steering
  branch of FUN_10004c70).
- 0x10159cdc = engine/suspension transform setter callback.
This links the preamble, the vehicle functions and the recovered resolution
semantics into one coherent picture. Recorded in evidence/preamble_usage.json.

## Verification against a real GTAIV.exe (major)
A user-provided `GTAIV.exe` (1.0.8.0; sha256 3d90e7c516fa450ca002e5031e62c0f66b404590f33e6cc9793b0da4fffbfd0f)
is available at ~/Downloads and is used **read-only** for signature verification;
it is never copied into the repository.

`tools/verify_game_signatures.py` applies the recovered matcher semantics to the
game's `.text` and reports, per signature, the match count and the resolved
original-image RVA. Results against 1.0.8.0:
- preamble signatures: 6/7 unique, 0 absent
- option signatures: 114/148 unique, 33 absent
- patch-operation signatures: 116/138 unique, 21 absent

Patterns that resolve uniquely confirm both the recovered bytes and their
original RVAs. Patterns absent from 1.0.8.0 correspond to other supported builds,
consistent with the recovered build gates (VSyncFix explicitly excludes 1.0.8.0;
several SkipLauncher/MiscFixes patterns are build-variant), i.e. an absence is
expected rather than an extraction error. A gated test
(`GameSignatureVerificationTest`) asserts >=100 recovered operations resolve
uniquely and that every resolved RVA lies inside the game `.text`; it skips
cleanly when the executable is not present.

## End-to-end verification against a mapped real game image
The user provided a real GTA IV 1.0.8.0 executable. A mapped copy of its image
(sections placed at their virtual addresses, image base 0x400000) was produced in
/tmp (never in the repository) and the reconstruction's **own** adapter
(`zpatch::apply_mapped_operation`) was run over all 192 recovered operations:

    192 operations -> 166 applied (54 module-relative + 112 signature-relative),
    26 not found, 0 errors

This is the first time recovered patch data has been applied through the
reconstructed code path against the real game image. It exercises the recovered
matcher, the signed-offset write path, the module-RVA write path and the
replacement-byte decoding end to end. It is a *static* result: it proves the
recovered bytes land where the original would have written them, not that GTA IV
exhibits the intended behaviour.

The 54 module-relative writes land at their recovered RVAs; one of them
(0x498460, the RemoveRGSCLoginRequirement build-9 fallback) begins immediately
after `C3 CC CC` padding, consistent with a function entry.

### Inline-site extractor corrected (false positive removed)
A subagent review flagged a suspicious entry. Verification confirmed the
extractor was emitting a std::string zero-initialization (`mov DWORD PTR
[ecx],0` during string construction, no page-protection calls around it) as if it
were a patch write. The generator now requires at least two nearby protected-write
call sites before treating a literal store as a patch. The genuine
NoPhysicsSleep literal site (0x10053506) is bracketed by VirtualProtect calls and
is retained; the false positive (0x10052f6e) was removed. This is an example of
the false-positive risk in automated extraction and why the protection-call
requirement is now explicit.

### Independent game verification of the preamble (subagent A2)
A subagent independently parsed `GTAIV.exe` and confirmed:
- VS_FIXEDFILEINFO at rsrc offset 0xdfb5f4: signature 0xfeef04bd,
  FileVersion/ProductVersion 1.0.8.0 (dwFileVersionMS 0x00010000,
  LS 0x00080000) -> build v1080.
- All 8 preamble signatures match .text exactly once (one matches twice).
- The "68 ? ? ? ? 68 A9 54 14 51 E8" entry is a **build probe**, not a site
  resolver: it matches game code that embeds the hash constant 0x511454a9
  (`push imm32; push 0x511454a9; call`) at VA 0xb3878c. On 1.0.8.0 the probe
  *hits*, so the ten *substituted* salt constants
  (0x6b86410e, 0x406b4b20, ...) are NOT applied at runtime on this build; they
  are the fallback for builds where the probe misses. Each of the ten .data
  defaults is itself a 32-bit constant embedded exactly once in the game .text,
  corroborating the interpretation.
- `RemoveRGSCLoginRequirement` (FUN_10045490): primary signature has 0 hits in
  1.0.8.0; the handler guards `[0x1015be04]==9` and falls back to RVA 0x498460.
  That target is a clean 16-byte-aligned cdecl function entry (RVA
  0x498460..0x498481, preceded by `c3 cc cc`), has 5 direct callers, and the
  redirect stub is `mov al,1; ret`. Repointing with E9+rel32 there is
  ABI-compatible. Verdict: sane.
Evidence: evidence/verify_preamble_vs_game.json.

### Independent verification of all 192 operations (subagent A1)
A subagent verified every recovered patch operation against GTAIV.exe 1.0.8.0:

| kind | total | present | absent | unique hit |
|---|---:|---:|---:|---:|
| signature | 138 | 117 | 21 | 116 |
| rva | 54 | 54/54 inside .text | 0 | - |

- Of the 54 module-relative writes, **41** apply to build 9 and **13** are
  per-build branches excluded on 1.0.8.0.
- Anomaly scan: 2 signature-absent-but-gate-includes-1080 cases (both are
  `RemoveRGSCLoginRequirement`, already explained by the RVA fallback), and 1
  signature-present-but-gate-excludes-1080 case.

**Important correction to the build-gate interpretation.** The
`DualVehicleHeadlights` gate `[0x1015be04]==1..6` does **not** mean the option is
inapplicable on other builds: the handler pattern-scans its signature about
*unconditionally* and the build value only selects which trampoline target is
written. So a `!=-style or equality gate can describe *target selection*, not
applicability. This means the generated `recovered_build_gates.inc` masks must be
read as "the build's branch that was recovered", not as an enable/disable
predicate. `build_allows_option()` in the reconstruction still treats them as
enable gates; that assumption is now documented as unproven and the test that
asserts the mask semantics is marked as such rather than as recovered behaviour.
Evidence: evidence/verify_ops_vs_game.json.

### Loader-inline sites expanded to five (A3 follow-up)
The A3 subagent ran out of budget before writing its output, so its bounded task
was completed here. A more reliable criterion was adopted: a write to a computed
address is an inline patch only when it is bracketed by >=2 page-protection calls
(`ds:0x10063010`). This both removed the std::string false positive and revealed
two sites the earlier scan missed because they store a *register*:
- NoImpostorHeightCutoff: two NOP fills (2 bytes `90 90`) at two match results of
  signature `76 0B F3 0F 11 8C 24 88 00 00 00 EB 09` (multi-result scan via
  FUN_1005afa0), at sites 0x100536e5 and 0x1005370b.
- CarDecalsFix jump (0x10053184 -> 0x1001b2e0), DrivebyFOVFix jump (0x10053ab4
  -> 0x100064b0), NoPhysicsSleep zero-dword write (0x10053506).
The extractor now derives all five automatically and is part of --check.

### Build-probe salt semantics implemented (from subagent A2)
The preamble "68 ? ? ? ? 68 A9 54 14 51 E8" entry is modelled correctly now:
it is a **build probe**, not a site resolver. `preamble_salt_substitutions(matched,
&out)` returns zero substitutions when the probe matched (the game's own salts
stand, which is the case on the verified 1.0.8.0 build) and the ten recovered
fallback salts when it missed. The fallback values were corrected from the
subagent's analysis to the values actually written on a miss
(0x6b86410e, 0x406b4b20, 0x6dcd115a, 0x62bfdb37, 0x931f73a2, 0x7f6a60d3,
0xc9dbda90, 0xa4ea0691, 0x09399528, 0x1cc648ea), which are distinct from the
.globals' .data defaults. Evidence: evidence/salt_probe.json,
evidence/verify_preamble_vs_game.json. Tests cover matched/missed.

### Systematic jump-target bug found and fixed (from subagent B1)
B1's handler documentation flagged that Ghidra's `UNK_...`/`LAB_...` labels land on
int3 padding and that the real trampoline entry is `UNK_... + 5`. Verified by
disassembling the handlers: the code computes `rel32 = TARGET - site - 5`, and the
decompiler folds the constant `0x100057f0` into a label named `UNK_100057eb`
(i.e. exactly 5 low) for BetterMPSync. Therefore **every** jump target in
`evidence/option_jump_sites.json` (and the derived `recovered_patch_sites.inc`)
that came from a Ghidra `UNK_`/`LAB_` label was 5 bytes too low.

The file has been regenerated directly from disassembly (`mov eax,TARGET` followed
by `sub eax,0x5`, bounded by the next recovered function), recovering 45 targets
across 30 handlers. Two further numeric targets that did not match any real
constant (NoLiveryLimit 0x1003d340, SavegameFix 0x100455a0) were wrong for the
same reason and now read the true constants (0x1003d3a0/0x1003d3e0/0x1003d310 and
0x10045580/0x100455e0).

Important scope note: the *applied* operations (`recovered_patch_ops.inc`,
`patch_operations_all.json`) record only the jump **opcode** byte, not the
displacement, so the wrong labels never affected `apply_recovered_operations`.
The bug was confined to the site/target tables used for documentation and for the
manifest's informational `target` field.

### Consolidated handler table (replaces flaky per-handler docs)
Two of the documentation subagents failed to write output (one looped, one ran
out of budget). Rather than depend on non-deterministic prose, a deterministic
generator was written: `tools/extract_handler_table.py` builds
`evidence/handler_table.json` and `docs/handlers_table.md` for all **79** mapped
handlers from the verified evidence files plus read-only disassembly of the ASI,
including per-handler signature counts, real-game match counts, recovered build
branches and disassembly-derived jump installs (opcode + target). The check is
wired into `make test` (skipped when objdump is unavailable). This supersedes the
partial `handlers_batch_*.md` notes.

### External cross-validation against GTAIV.EFLC.FusionFix (subagent D1)
FusionFix (commit bea25bd8) scans 1260 unique patterns; 674 resolve in GTA IV
1.0.8.0. ZolikaPatch has 148 recovered signatures, 115 resolving. For **11
features** both projects hook the *same* subsystem on this build, e.g. SkipIntro,
SkipMenu, ReversingLightFix, BorderlessWindowed, HighFPSBikePhysicsFix,
HighFPSSpeedupFix, SuperLODFix, NoLoadingSleep, EpisodicVehicleSupport,
TBoGTHeliHeightLimit and PoliceEpisodicWeaponSupport (FusionFix:
"TBoGTPoliceWeapons", where ZolikaPatch's signature and FusionFix's are
byte-identical: `39 2D ? ? ? ? 75 3E`). This is independent external
corroboration of the recovered hook targets by a separate, actively maintained
project. Note FusionFix targets The Complete Edition (1.2.0.x), so most of its
patterns not matching 1.0.8.0 is expected. Evidence:
evidence/fusionfix_crossref_1.json, evidence/fusionfix_overlap.json.

### Fidelity audit corrections (subagent E1)
E1's audit found real defects, now fixed:

**O-1 (fidelity bug, fixed).** Disassembly shows `FUN_10003e20` performs TWO
32-bit compares (`cmp DWORD PTR [eax+0x20d3],0xC7C47589` at 0x10003e2f and
`cmp DWORD PTR [eax+0x20d7],0x2446` at 0x10003e3b). The reconstruction read only
a 16-bit value at +0x20d7 and required only 0x20d9 bytes, silently ignoring the
high word. `compatibility_module_lacks_signature` now reads both as dwords, and a
test writes the high half (0x20d9) to prove the difference.

**O-2 (fixed).** The `LocalSettingsPath -> 0x1015be04` binding was a boundary
artefact. Two separate causes were found and fixed: (a) the option-flag extractor
had swallowed the build classifier `FUN_10055c70`, whose jump table writes the
build slot 0x1015be04 - the extractor now refuses to bind the known build slot and
bounds each option segment at the end of its containing function; (b) the entry
it then produced (0x10159b85) is the loader EPILOGUE (module-presence flag set
before calling FUN_100523c0, at 0x10055e17, outside the loader function
0x100523c0..0x10055c70) and is likewise not an option flag. The confirmed binding
set is therefore **10** (not 11). Separately, `slot_for()` hashed globals modulo
64 and mapped DebugPrintSpawns and DrivebyFOVFix to the SAME slot; the flag store
is now an exact per-binding index with no hashing, and a test asserts the two
options are independently readable.

**O-3 (fixed).** Corrected operation counts: the evidence has 57 NOP fills,
59 jump-opcode writes (38 E9 + 13 EB + 8 E8) and 11 other writes across 27
handlers; earlier prose said "38 jump installs ... 32 other" and "30 handlers".

**O-5/O-6 (recorded).** `expected-byte-validation` is an invented safety
primitive with no recovered counterpart (the original writes without checking), so
it is now marked `partial` with that note. Features whose only evidence is the
reconstruction's own test were re-labelled accordingly.

### Build-system defects found by subagent E2 and fixed
1. **`tests/test_coverage.py` was failing and never run.** `make test` invoked
   only `generate_coverage.py --check`, so a real assertion failure stayed
   invisible. The expected counters were stale after the coverage rewrite; they
   are corrected (zmenuiv 23/1/2/6, zolikapatch 995/5/9/1) and the test is now
   wired into the `coverage` target, so `make test` runs it.
2. **Non-reproducible MinGW link.** `ld` chose a random `ImageBase`
   (observed 0x6b7c0000 vs 0x64800000), so two builds of the same source
   differed by 104,189 bytes. Both the Make and CMake links now pass
   `--image-base=0x10000000` (the original image base) plus
   `--no-insert-timestamp`; repeated builds are now byte-identical for both ASIs.
3. **Missing `.inc` dependencies.** The Makefiles tracked only
   `model_catalog.inc`; the four generated `recovered_*.inc` files were not
   dependencies, so editing them would not rebuild. `PATCH_HEADERS`/`MENU_HEADERS`
   now glob all `src/*.inc`.
4. **`ZMenuIV_reconstruction/build.sh` was broken** (compiled only 4 sources, so
   `zmenu::tuning_*` symbols were unresolved, and it did not `cd` to its own
   directory). It now builds all six sources with `-Werror`, `cd`s to its
   directory, and passes.
5. **`verify_asi_abi.py` hard-coded one output directory.** Added
   `--candidate-dir` so relocated/CMake builds can be verified in place.

### Episodic handler attribution corrected (subagent D2)
`evidence/option_handlers.json` previously attributed BOTH Episodic handlers to
`EpisodicVehicleSupport` (as `FUN_1001b580` and `FUN_1001b380`), leaving
`EpisodicWeaponSupport` with zero handlers and zero signatures. The dispatcher in
`decompiled_0001.c` is unambiguous:

    FUN_1005c160(...,"EpisodicVehicleSupport","1"); if (v==1) FUN_1001b580();
    FUN_1005c160(...,"EpisodicWeaponSupport","1"); if (v==1) FUN_1001b380();

Confirmed by disassembly: `FUN_1001b580` loads 22 signature literals and
`FUN_1001b380` loads 8. The handler map is corrected
(`EpisodicVehicleSupport -> 0x1001b580`, `EpisodicWeaponSupport -> 0x1001b380`),
and every downstream table was regenerated: signatures are now 21 for the vehicle
tier and 8 for the weapon tier (previously 29 / 0), with patch operations split
25 / 8 accordingly. The union property the subagent noticed (the combined
signature list equals the old vehicle list, zero overlap) is exactly what the
correction predicts.

Note: D2's write-up said the weapon tier maps to `FUN_1001b380` in the
"decisive evidence" line but `FUN_1001bence0`-style text elsewhere; the
verification above (both the dispatcher text and the literal counts) is the
authoritative form.

### IVMenuAPI integration recovered and implemented (last unimplemented)
`FUN_10052380` is the menu integration. ZolikaPatch does **not** link the
third-party `IVMenuAPI.asi`; when the `AddOptionsInMenu` option (default 1, global
`0x10158038`) is set it:
1. returns 0 immediately when the option is clear,
2. tries `GetModuleHandleA("IVMenuAPI.asi")` and returns 1 if present,
3. otherwise - only if the one-shot byte `DAT_101a843c` is clear - tries
   `LoadLibraryA("IVMenuAPI.asi")`, returning 1 on success,
4. sets `DAT_101a843c = 1` and returns 0 when the module cannot be obtained.

So it is a **load-once, cached probe with a one-shot attempt flag**, gated by an
INI option. Implemented and tested in `include/zpatch/menu_integration.hpp` +
`src/menu_integration.cpp` (the decision function and the stateful probe).

Corroboration: a resolved import table (`evidence/import_table.json`) shows the
ASI imports `GetModuleHandleA`/`LoadLibraryA` from KERNEL32 and nothing from
IVMenuAPI, confirming dynamic loading. Building that table also caught a bug in an
ad-hoc IAT walker I had written mid-analysis (it used the import Name field
instead of FirstThunk, mislabelling `VirtualProtect` as `fread`); the previous
`VirtualProtect` identification was in fact correct.

Registering individual options with the loaded API is not visible in this
function, so the feature is recorded as partial.

### The long-standing decompilation failure is now characterised
`FUN_10024600` has failed to decompile since the first pass (originally a timeout,
later "response buffer size exceeded"). It is not an analysis gap - it is shape:

| property | value |
|---|---|
| size | 75,906 bytes |
| instructions | 22,782 |
| stack frame | 0x5bf0 bytes (via `__alloca_probe`) |
| dominant callees | `FUN_10057710` x451, `FUN_1004ac90` x445, `FUN_1005dc50` x442 |
| `0xcccccccd` occurrences | 442, each `mov eax,0xcccccccd; mul; shr edx,3` |
| string-literal pushes | 3 (2 unique) |
| top immediates after the magic | 0x20 ' ', 0x30 '0', 0x38 '8', 0x34 '4' |

That combination is unambiguous: 442 inlined MSVC divide-by-10 sequences with
ASCII digit constants inside a 23 KB frame is a **compiler-generated bulk numeric
formatter** - it converts many values to decimal text, with each conversion
inlined. Only three string pushes means it is not literal-driven.

So the file's single unresolved function is CRT-style formatting scaffolding, not
product logic; its caller `FUN_1003d6d0` is the meaningful target. The underlying
primitive is now implemented and tested in
`include/zpatch/number_format.hpp` + `src/number_format.cpp` - the exact
magic-constant division (verified against true `/10` for all inputs including
`UINT32_MAX`), the recovered least-significant-first digit loop, and
signed/unsigned formatting including `INT32_MIN`.
Evidence: ZolikaPatch_decompiled/checkpoints/fun_10024600/characterization.json.

### Runtime patch application wired in
The recovered pipeline is now connected to the runtime:
`apply_recovered_patches(build, image, protect, report)` applies the recovered
patch operations to a caller-owned mapped module image, gating each operation on
the recovered per-build gate and the loaded configuration, and writing through
`apply_mapped_operation`. It reports candidates / applied / not-found / gated-out
counts and per-operation outcomes, and sets `patches_applied` in the runtime
state when at least one write succeeds.

This is a real application path against supplied memory - the reconstruction now
*applies* its recovered patches rather than only describing them. It is explicitly
**not** a claim of working in a live GTA IV process: no OS call, page protection
change or process-memory access happens inside this code; the caller owns mapping,
protection and the real module. Tests exercise the path against a synthetic image
containing a recovered signature (verifying a write lands and the state updates),
and confirm an uninitialised runtime refuses to patch.

## Patch rollback and the recovered feature catalog

**Rollback mirror.** `apply_recovered_patches_logged` (src/runtime.cpp) applies
the recovered operations exactly like `apply_recovered_patches` but captures the
preimage of every write through `apply_mapped_operation_capture`
(src/game_image.cpp). `rollback_recovered_patches` restores those preimages in
reverse application order through the same caller-supplied protection toggle
(`restore_mapped_bytes`), clears `patches_applied` when anything was restored, and
keeps entries it could not restore so the caller can retry/report. Tests prove a
full apply/rollback round trip returns the mapped image byte-for-byte, that one
protect + one restore happen per write, and that an empty log is a no-op.

**Recovered feature catalog.** `src/patch_catalog.cpp` now carries a second table
(`recovered_feature_coverage.inc`, generated by
tools/extract_feature_coverage.py) with one row per the 98 `[Options]` keys:
observed default, recovered handler VA, whether signatures were recovered, the
recovered build-gate mask (positive `==` gate; `!=`-only gates stored as their
complement, with `has_build_gate` distinguishing "no gate" from a gate that
excludes everything), the signature/rva operation counts, whether the option
resolved against GTA IV 1.0.8.0, and the recovered classification. Cross-checked
in tests against the evidence: 79 handlers, 64 with signatures, 10 gates, and
exactly 138 signature + 54 rva operations. The generator is wired into `make
evidence` via `--check`.

**Recovered extra signatures.** `option_coverage_gaps.json` identifies 15
`[Options]` keys whose patterns exist in the evidence but which the primary
signature extractor cannot see (an extractor coverage gap, not a behavioural gap).
`tools/extract_extra_signatures.py` reconstructs those 24 signatures from the
recorded `pattern_evidence`, re-checks every one against GTA IV 1.0.8.0 with the
recovered matcher semantics (hit count and hit VAs must match the stored values),
and emits `src/recovered_extra_signatures.inc`. `recovered_extra_signatures()` and
`scan_extra_signatures()` expose and scan them; the write payload for those
options was not recovered, so they are located but not written. The generator is
wired into `make evidence` via `--check`.

**Recovered module-RVA relative jumps.** The literal op table cannot express a
handler that writes `E9` + a rel32 computed from the live game module base, so
those ops were silently missing - SuperLODFix, for example, recovered through
*zero* op rows despite being classified `rva-patch`. `tools/extract_rva_jumps.py`
scans every recovered handler body (read-only, objdump) and reconstructs the
pattern `mov ecx,[0x1015ad70]; lea r32,[ecx+RVA]; ... mov byte [r32],0xe9;
mov eax,0x100xxxxx; mov [r32+1],eax`, emitting `(option, module_rva, destination)`.
Each recovery is sanity-checked (destination is a plugin VA, RVA is inside the
15.6 MB game image). Result: **7 entries** - SuperLODFix x2 (RVAs 0x4a3ce9 /
0x540e19 -> 0x100469e0) and LoadDLCs x5. `recovered_rva_jumps.{hpp,cpp}` expose
them, `apply_rva_jump` writes the jump into caller-owned memory using the
`module_base_va` (the value `GetModuleHandleA(0)` returns) so the output is
byte-identical to the live handler regardless of host pointer width or heap
address, and `apply_recovered_rva_jumps` gates on the recovered build gate. The
runtime `apply_recovered_patches_logged` now applies them and records their
preimage, so `rollback_recovered_patches` undoes them too. The generator is wired
into `make evidence` via `--check`.

**Recovered signature-relative jumps.** The literal op table stored only the fixed
`E9` byte for many handlers, because their rel32 is computed from the plugin's
trampoline address at run time - leaving those jumps incomplete (e.g.
AimProjectileCrashFix wrote `E9` with no displacement). `tools/extract_sig_jumps.py`
recovers the whole install: it scans each handler for the shape
`mov edx,<pattern>; call 0x10003830; add reg,off; mov byte [reg],0xe9; mov
eax,<trampoline>; sub eax,reg; sub eax,5`, yielding (option, signature, offset,
destination) - **30 entries** (28 cross-validated against the literal table, plus
DrivebyFOVFix and NoLiveryLimit recovered from the loader-inline path, where the
option is attributed by the most recent key-string push). Only the single-hit
scanner is modelled; the multi-hit collector (BenchmarkFix) is excluded because it
installs at the Nth match, which this (signature, offset, destination) model cannot
express. The recovery is cross-validated against the trusted
literal table: of the signatures both see, 28/28 relative offsets agree and the
ambiguous candidates whose linear walk crossed interleaved control flow are dropped
(the trusted table wins), reported to stderr. `recovered_sig_jumps.{hpp,cpp}` expose
them; `apply_sig_jump` scans, writes `E9` + rel32 using the `module_base_va`, and
the runtime `apply_recovered_patches_logged` now applies them (completing the
`E9`-only literal rows) and logs their preimage for rollback. Wired into
`make evidence` via `--check`.

### Remaining loader-inline op shapes (analysed, not yet modelled)

Beyond the signature-relative and module-RVA jumps, the loader dispatch arm
(`FUN_100523c0`) patches a handful of options inline. A full linear scan of the
loader body (12 single-hit `0x10003830` calls, 2 `mov BYTE [reg],0xe9` E9 stores)
finds exactly one further inline write shape still unmodelled:

```
mov edx, <pattern>;  call 0x10003830        ; single-hit scan -> eax = match
mov esi, DWORD PTR [eax+off]                ; esi = the pointer the match names
VirtualProtect(esi, 4, 0x40, &old)
mov DWORD PTR [esi], <value>                ; indirect literal write
```

This is now a modelled op kind. `tools/extract_indirect_writes.py` recovers
`(option, signature, pointer_offset, width, value)` for every such write, verifying
each store is bracketed by a VirtualProtect call, and yields **2 entries**:
**NoPhysicsSleep** (signature `0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 00`,
pointer at `match-0x26`, dword `0x0`, matching `evidence/inline_patch_sites.json`)
and **IncreaseVehicleModelLimit** (signature `83 C7 01 81 FF 18 79 00 00 7C D2`,
pointer at `match+0x4d`, dword `0x400`). `recovered_indirect_writes.{hpp,cpp}`
expose them; `apply_indirect_write` scans, dereferences the pointer word and writes
the value through it (the target must be inside the caller-owned image), and the
runtime `apply_recovered_patches_logged` applies them and logs their preimage for
rollback.

The other still-zero-op options do **not** go through this shape:
`CarDecalsFix` and `DrivebyFOVFix` are signature jumps (now recovered - the latter
inline, the former in a handler), `BenchmarkFix` uses the multi-hit collector
(installs at the Nth match, unrepresentable as one signature), and
`FastLoading`/`HighQualityReflections`/`MouseFix`/`CutsceneFixes`/`GarageModelFix`/
`OutOfCommissionFix` are thin `pusha`-forwarders whose real work runs through
runtime-filled `.data` callback slots (their targets are not statically resolvable
from the ASI - `evidence/handlers_unresolved.md` documents the same for
`FastLoading`). `StartupTimeFix`, `DebugPrintSpawns`, `NoImpostorHeightCutoff` and
`NoLODLightHeightCutoff` write data through a register-held site with no
VirtualProtect bracket, or via the multi-hit collector.

**Recovered Nth-match jump installs.** A fourth op kind. Most handlers locate one
match with the single-hit scanner, but a few use the multi-hit collector
(0x1005afa0), which returns a *vector* of matches, and install a jump at the k-th
match. That is not expressible as one (signature, offset, destination) - the same
signature maps to several sites - so `tools/extract_match_jumps.py` recovers
(option, signature, match_index, offset, destination). Result: **2 entries**,
BenchmarkFix (`E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20`, offset +8): match[0] ->
0x10005650 and match[1] -> 0x10005670. BenchmarkFix previously had *zero* op rows.
`find_all_patterns()` was added for the multi-match scan; `recovered_match_jumps.*`
applies them and the runtime `apply_recovered_patches_logged` logs their preimage for
rollback. Wired into `make evidence` via `--check`.

**Recovered literal stores the op table missed.** Five options had a recovered
signature but no op row because their writes were not in
`evidence/patch_operations_all.json`. `tools/extract_literal_stores.py` recovers
the same shape as the literal table (scan -> site -> VirtualProtect -> store) but
emits only an explicit, disassembly-confirmed allowlist (a candidate that the tool
cannot reproduce from the allowlist aborts the build). Result: **9 stores** -
IncreasePtrNodes x3, IncreaseVehicleStructLimit (`push 50` -> `push 100`), NoLoadingSleep,
OverrideGTADatPath, VSyncFix x2, and NoLODLightHeightCutoff (a loader-inline word NOP
fill, recovered via a spill-slot + sub-register NOP pass). `recovered_literal_stores.*` apply them, gated on the
recovered build gate. Wired into `make evidence` via `--check`.

### Remaining shapes (analysed, not modelled)

The 28 options with no recovered ops fall into groups, all analysed:

1. **Flag-only options** (AddOptionsInMenu, MovingPlaneParts,
   MovingSteeringWheelSupport, ShakingEngineBlock, and the global-flag set
   DebugPrintSpawns / DoNotPauseOnMinimize / Patch4RemoveModChecks /
   RemoveGFWLUpdatePopup / RestoreDeathMusic): they set a plugin global flag and do
   no patch of their own. `option_flag_map.json` documents the binding; they are
   correctly zero-op.
2. **`pusha`-forwarder options** (FastLoading, HighQualityReflections, MouseFix,
   CutsceneFixes, GarageModelFix, OutOfCommissionFix, CarDynamicShadowFix,
   CarPartsShadowFix): the handler body is `push ebp / mov ebp,esp / call
   <forwarder> / pop ebp / ret` and the real work runs through a callback in a
   `.data` slot the module constructor fills. `handlers_unresolved.md` documents
   this; for FastLoading the target is genuinely runtime-filled and not statically
   resolvable.
3. **Multi-hit Nth-match literal writes** (StartupTimeFix, NoImpostorHeightCutoff,
   NoLODLightHeightCutoff): the collector `0x1005afa0` returns a vector of matches
   and the handler writes a literal at match[k] - StartupTimeFix writes dword
   `0x101580d8` at `match[k]+6` for every match; NoImpostorHeightCutoff NOPs a word
   (`0x9090`) at `match[0]` and `match[1]`. These need a multi-match *literal* op
   kind (the Nth-match jump kind covers jumps only); they are recovered here and
   left for that op kind rather than half-mapped.
4. **Multi-hit E8-call installs** (BorderlessWindowed): now recovered by the
   Nth-match jump kind, which was extended to carry the opcode - `E9` for jumps,
   `E8` for calls. BorderlessWindowed installs `E8 rel32` calls to `0x10005890`
   (match[0]) and `0x10005900` (match[1]).
5. **Remaining singular shapes** (CarDecalsFix register-held E9 jump,
   LaunchWithPlayGTAIV writes the string "Play"/"V.exe" into a runtime buffer,
   CrashMsgFix writes a fixed call/jump into a site computed with a data offset,
   NoImpostorHeightCutoff loader-inline NOPs, StartupTimeFix-style data writes) are
   each one option; they are catalogued with their disassembly here as confirmed.

**Recovered Nth-match literal writes.** A fifth op kind. StartupTimeFix uses the
multi-hit collector and loops over its vector, writing dword `0x101580d8` at
`match+6` for **every** match (pattern `68 00 C2 EC 80 68`, 3 hits in 1.0.8.0). The
single-hit literal table cannot express one signature with several sites, so
`tools/extract_match_literals.py` recovers `(option, signature, match_index, offset,
width, value)` where `match_index == -1` means "every match". It emits only
disassembly-confirmed sites (loose auto-recovery produced unverifiable candidates
that were excluded). `recovered_match_literals.*` apply them, and the runtime
`apply_recovered_patches_logged` applies them and logs their preimage so
`rollback_recovered_patches` reverses them; tests place two signature copies, assert
both get the dword, and run a full runtime apply/rollback round trip that returns
the image byte-for-byte. Wired into `make evidence` via
`--check`. NoImpostorHeightCutoff uses the same shape inline in the loader (no handler); the extractor's loader pass now recovers its two word NOPs (0x9090 at match[0] and match[1]), attributed by the most-recent key-string push. NoLODLightHeightCutoff is a single-hit word NOP recovered by the literal-store extractor.

**CarPartsShadowFix (runtime-built pattern).** Its handler builds its pattern with a
hex-digit loop, so there is no `mov ecx,<imm>` to read - the signature comes from
`recovered_extra_signatures.inc` instead. `tools/extract_sig_jumps.py` now supports
a disassembly-confirmed explicit list for such handlers (the signature must also be
present in the extra-signatures table, or the build aborts). CarPartsShadowFix
installs `E9` at `match-6` -> `0x10006880` (the handler writes two helper-entry
pointers to `DAT_1015bdc8`/`DAT_1015bdc0` as well, which are data, not code). The
offset walker now handles negative `lea` displacements (`lea edi,[eax-0x6]`).

### CarDynamicShadowFix: not statically resolvable

CarDynamicShadowFix has no pushed key literal (the loader composes its name at run
time from `Ca%snam%s%swF%s`), and its install route is the argument-driven helper
FUN_10022f40 -> thunk `0x10022f30 => jmp DWORD PTR ds:0x1018e574`. That slot is not
an import (absent from `evidence/import_table.json`), is referenced by exactly one
instruction (the thunk), and has no static writer anywhere in `.text` - so the final
target is a runtime-filled `.data` pointer, the same class as the `pusha`-forwarder
slots (`FastLoading` etc.). The option's *route* is recovered; its *target* is not
statically recoverable from the supplied artifacts, and it is recorded as such
rather than guessed.

### IDA pipeline and full function inventory (2026-10-11)

The project switched decompilers: all new analysis uses IDA Pro 9.1 through idalib
(`tools/ida/`), while the Ghidra export stays as historical evidence. The database
`ida_projects/ZolikaPatch.asi.i64` (regenerable via `tools/ida/make_idb.py`) holds
**722 functions** with **99.4 % `.text` coverage**. Ghidra's 995 "functions" were
inflated by a mis-split tail chunk of the loader `FUN_100523c0`
(`0x10060f23..0x10062cd0`, one 11-byte `lea ecx,[ebp-X]; jmp std::string_dtor` stub
per option-name string); IDA models that region correctly as a chunk.

Full Hex-Rays export: `ZolikaPatch_decompiled/ida/` — 712 bodies plus 10
disassembly fallbacks (all `popa` + `push args` + `jmp ds:[slot]` stubs that
Hex-Rays rejects on stack-pointer analysis; `FUNC_NORET` does not rescue them, and
their disassembly is small and authoritative). `sub_10024600` remains the single
un-decompilable function (the 75 KB inlined MSVC decimal formatter already
reimplemented as `number_format.cpp`).

### Hook trampolines and detour stubs recovered (major, 2026-10-11)

`ZolikaPatch_analysis/checkpoint_0002_trampolines/` closes the largest known parity
gap: the code the original *installs jumps to*. All **55 jump destinations** from the
recovered tables plus **12 detour stubs** that are installed through runtime-filled
`.data` slots (no static xrefs) are now identified, created as functions in the IDA
database, decompiled, and classified:

- **Replacement bodies** (whole-function takeover): AllowHashesForIDE, AimProjectileCrashFix,
  BetterMPSync, CombinedRadioFreezeFix, LoadDLCs, RemoveRGSCLoginRequirement, SavegameFix
  (3 bodies), BorderlessWindowed, ImprovedShaderStreaming (2), SpeedupBugFix,
  PedModelsInCutscenesFix, NewFilesCompatibility (2), CollisionDeadlockFix, NewMemorySystem,
  EpisodeSwitchFreezeFix sum.
- **Tail-call shims** (pre/post work then `jmp` through a slot): BenchmarkFix (2),
  BuildingDynamicShadows, CopSirensFlag, DrivebyFOVFix, CarPartsShadowFix,
  EpisodeSwitchFreezeFix (4 more), GroupHackFix, FreezeGunFix, BlackscreenFix, CrashMsgFix,
  LoadDLCs, PathNodeCrashFix, PlaneCrashFix, PlayerAsPedComponentsFix, PopupHeadlightsFlag,
  SMPA60Fix (2), SuperLODFix, VRAMFix, WantedLevelCrashFix, NewSavesCompatibility,
  NewFilesCompatibility, detour_100068A0, detour_100512F0.
- **Register-context detours** (save the full register set, run an ASI helper, resume):
  vehicle animation (detour_100051B0, installed by sub_10005210), EmissiveLerpFix,
  BadPlayerModelCrashFix, HighFPSBikePhysicsFix (entry + 12-phase loop body calling
  `[0x10159D04]` for phases 0..0x0B then resuming through `[0x10159CFC]`),
  CopSirensFlag, NoLiveryLimit (4 entry bodies + tail).
- **State-reset stubs** (zero N `.data` slots and return): 8 stubs with exact slot
  sets recovered from the pusha bodies.

All 67 bodies are reimplemented in `src/hook_bodies.cpp` /
`include/zpatch/hook_bodies.hpp` as portable functions taking explicit
`GameCallSlots`/context parameters, with tests in `tests/test_main.cpp` mirroring the
recovered behaviour (field writes, dispatch conditions, phase loops, reset slot
counts, collision walk bounds, shader-name filters). `evidence/hook_reimplementation_map.json`
maps every reference VA to its body symbol, and
`tools/ida/check_hook_reimplementation.py` (wired into `make evidence`) fails the
build if any recovered destination lacks a reimplementation.

Installer coupling is also documented: `sub_10005210` writes `E9` over two matched
game sites and stores `dword_1015AD44 = match+16` plus a game vtable pointer at
`0x1015AD4C`; `sub_100513A0` patches a game function to `detour_100512F0` and fills
`0x10159D08..0x10159D1C`. A rebuilt ASI must reproduce these slot writes and install
jumps to its own bodies (rel32 computed at run time, so no address fixups are
needed). Live installation remains unwired.

### Hook installation path implemented (2026-10-11)

`src/hook_install.cpp` (+ `include/zpatch/hook_install.hpp`) installs the recovered
jump ops into a caller-owned mapped image while resolving each destination through
the reimplemented bodies instead of the original plugin trampoline VAs:

- `src/recovered_hook_targets.inc` is generated from the reimplementation map by
  `tools/ida/generate_hook_targets_inc.py` and maps all 67 reference VAs to their
  body addresses (`resolve_recovered_hook_target`); the generator's `--check` runs
  in `make evidence`.
- `install_recovered_sig_jumps` / `_rva_jumps` / `_match_jumps` mirror the three
  recovered install kinds: signature scan + offset, module-RVA site, and
  Nth-match selection with the recovered opcode (E9/E8). All three honor the
  recovered build gate, compute the rel32 from the runtime module base
  (`destination - (module_base + site) - 5`), protect the 5-byte site through the
  caller's `ProtectFn` (the recovered 1/2/4-byte protected-write helpers do not
  cover jump installs), and report per-entry outcomes
  (`installed`/`not_found`/`gated_out`/`no_destination`/`write_failed`).
- Tests cover table coverage and resolution, the exact written bytes and
  displacement, match-index selection, both opcodes, gate skips, missing
  destinations, and protection denial.

Remaining for live parity: wiring the installers into the runtime apply/rollback
path and reproducing the per-option installer-side `.data` slot writes.

### SkipLauncher live in the real game (2026-10-11)

The complete XLivelessAddon-derived launcher/DRM bypass is implemented and
verified in the real game: **GTA IV 1.0.8.0 boots with only the rebuilt ASI
present** (no XLivelessAddon, no PlayGTAIV shims beyond the launcher itself).
Two root causes were fixed on the way:

- **Timing**: the original runs its entire loader synchronously inside
  `DllMain` (`DllMain -> 0x100523c0`). Applying from a worker thread loses the
  race against the DRM's import-time code modification - the first live run
  matched only 8 of ~35 signatures (`entries_found=0` for the main table).
  The loader now runs synchronously, matching the original and FusionFix's
  load-time discipline (`source/dllmain.cpp`).
- **Calling conventions**: the five file-API hook bodies are `__cdecl` in the
  originals (plain `ret`, caller cleans); our first bodies were `__stdcall`
  (stack corruption). `rom_set` (`0x10004610`) takes the object in **ESI** and
  two callee-popped stack arguments (`mov ecx,[esi+1c]; mov [esi+20],ecx;
  mov al,1; ret 8`) and is now reproduced instruction-for-instruction.

Live log (written next to the ASI): `entries_found=6`, `entries_missing=19`
(other builds), `writes=7`, `collectors_found=2`, `collector_writes=4`,
`jump=yes`, `DFA installs=6`, `collected=2`, `ROM installs=2`. Provenance and
licensing for every reference is recorded in `CREDITS.md` (XLivelessAddon has
no license and was used as a behavioral reference only; FusionFix is GPL-3.0
and was consulted for injection discipline - no code copied from either).

### Live-apply correctness sweep (2026-10-11, night session)

The first general live applies crashed the game twice; both crash dumps were
decisive and the sweep that followed found four classes of live-unsafe writes,
all now guarded and regression-tested:

1. **Bare jump opcodes** (crash: CRT initializer walk calling 0xFFB85E40). The
   op table's ~42 single-byte `E9`/`E8` rows are only the first byte of a jump
   the handler writes together with its displacement; writing one alone leaves
   the following bytes as the displacement. The runtime never writes a bare
   jump opcode: covered sites belong to the jump installers, uncovered ones are
   counted as `jump_opcode_pending`.
2. **Dangling jump destinations** (crash: DEP at 0x10003E50, the original
   module's hash function). `resolve_destination` fell back to the recovered
   reference VA when a resolver had no reimplementation, installing jumps to
   the ORIGINAL module's addresses. Live mode (resolver present) now skips such
   installs and counts `no_destination`; evidence mode (no resolver) keeps the
   reference VA.
3. **Dangling data values**: StartupTimeFix's match-literal stores a pointer to
   the original module's timing dword (0x101580d8). Values inside the original
   module's range are rebased through `rebased_values.cpp` to our own globals
   (StartupTimeFix's timing dword is rebased); unrebased options are skipped
   and counted as `module_address_pending`. The literal-store ops
   (OverrideGTADatPath 0x10158068, VSyncFix 0x10159bc0) are not live-applied
   yet and await the same treatment.
4. **Wrong-build patches** (corruption source): the op table's 54 module-rva
   rows were recovered from ALL of each handler's build branches without
   attribution, and every one was applied to 1.0.8.0. A blind write at
   base+rva from a build-7/8 branch patches whatever lives at that RVA in
   build 9. Attribution (`tools/attribute_rva_builds.py`, wired into make
   evidence) shows the split: build 5: 2 rows, build 7: 17, build 8: 12,
   **build 9: 28**; the rva-jump table mixed builds too (SuperLODFix's
   0x4a3ce9 is build 7, 0x540e19 build 8). The runtime now skips rva rows whose
   branch is not this build (`build_mismatch`). All 138 signature rows are
   either unconditional (102) or `>= build 6` (36), so none are excluded for
   build 9.

**Recovered build-9 module-rva installs** (`tools/extract_rva_installs.py`):
the writes the literal table cannot express are now recovered from the
handlers' build-9 branches - full E9/E8 installs with game-rva or module
destinations, NOP fills and slot publishes. On 1.0.8.0: LoadDLCs (5 jumps +
12-byte fill + callback-slot publish), RemoveRGSCLoginRequirement (jump to our
`rgsc_login_removed`), RemoveUselessChecks (5 NOP fills), SuperLODFix (2 slot
publishes, pending its ABI body). `rva_installs.cpp` applies them; slot
publishes land in our rebased globals; the tests verify every write
byte-for-byte against the real game image.

The live log now reports every skip class
(`jump_opcode_pending`, `no_destination`, `module_address_pending`,
`build_mismatch`) so the remaining parity gaps are visible in one place.

### Runtime wiring, and the signature-jump dead path fixed (2026-10-11)

`apply_recovered_patches_logged` now accepts an optional
`RuntimeDestinationResolver`. When one is supplied (a rebuilt ASI passes
`default_hook_resolver`), every jump install targets the resolved reimplemented
body instead of the recovered trampoline VA, and rollback restores the true
preimages. The 32-bit destination contract mirrors the x86 ASI layout.

Wiring this up exposed a real defect in the runtime: the literal `E9` op row and
the signature-jump entry describe the *same write*, and the literal row ran
first, overwriting the site's signature before the jump installer could scan for
it - so the runtime's signature-jump path was effectively dead (every sig jump
resolved as `not_found`), and when it did run its rollback preimage was captured
*after* the write. Both are fixed:

- a literal row is skipped when a recovered jump entry covers the same
  `(option, signature, offset)` - the jump installer owns that write, exactly
  like the original handler (scan, then one E9 + rel32 write);
- the runtime scans, captures the preimage and writes the signature jump itself,
  so rollback restores the original bytes.

Tests cover the resolved displacement, the covering rule, and full
apply/rollback byte-exactness.

