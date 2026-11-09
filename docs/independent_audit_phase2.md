# Independent adversarial audit — ZolikaPatch reimplementation, phase 2 (top-down game-side inventory)

Auditor: independent verification session (audit-only; no `src/`, `include/`, `evidence/`,
`tests/` or pre-existing-doc edits). Artifact: this file.

Revision audited (working tree, uncommitted; re-hashed before publishing):

| file | sha256 (first 16) | note |
|---|---|---|
| `game_refs/ZolikaPatch_original.asi.off` | 720fbcebfd563616 | 1,502,208 B; phase-1 table printed `bc01bcbc…` — that value is not reproducible on this file; every byte-level phase-1 claim re-read here matches this file (e.g. `[0x101580D4]=0x75E005F1`, `100523C0: 53 8B DC …`), so the phase-1 hash line was wrong, not the file |
| `src/hook_bodies_win.cpp` | 24b60f809fcb18bc | changed since phase 1 (driveby diag added; F1/F8/F19 unfixed) |
| `src/runtime.cpp` | 57b592aad5e1243c | changed since phase 1 |
| `src/config.cpp` | 796b17f30385b07e | unchanged since phase 1 |
| `src/recovered_patch_ops.inc` | 3584972a0cb515d6 | unchanged |
| `evidence/op_corrections.json` | 7e796d5d6e040f33 | changed since phase 1 |
| `src/recovered_op_corrections.inc` | 0275a4bcb4dae96b | changed |
| `src/recovered_build_gates.inc` | a2737d8834e933fb | — |
| `src/recovered_feature_coverage.inc` | 3f7ef28bf86a8e75 | — |

Git HEAD `43c6cc5`, tree dirty. Hashes re-checked immediately before publishing (all unchanged); no concurrent edit
landed during the audit (files stable). The original ASI was not modified.

> **Status superseded (2026-10-21).** This phase-2 snapshot predates the final
> parity closure. Its F1/F8/F12/F13/F14/F19/F22 open findings are closed in the
> current tree: resolver slots use the recovered salts and P13 SEH wrapper,
> NMS pointer arithmetic is width-safe, FusionFix/NewFiles gates are wired,
> LoadDLCs RVA jumps are correctly paired, HighFPSSpeedupFix has all nine
> distinct writes, and the spawn-name helper reads the mutable salt slot. See
> `PARITY.md`, `docs/parity/ledger.json`, and the current `recovered_*` tables.
> F17 remains measured harmless on available builds; F27 is implemented by
> `enforce_zmenu_version()` in ZolikaPatch DllMain and threshold-tested. The
> separate ZMenuIV plugin's internal bodies remain outside the 107-hook audit.

## Executive summary

* **Positive results confirmed at this revision** (independent re-derivation, §3.7): the OOC
  payload hash; all 98 option keys are loader-read (incl. the sprintf-composed
  `CarDynamicShadowFix`); the loader preamble/salt mechanism; the ten-salt fallback and probe
  build sets; the vehicle-light table parsing; IVMenuAPI registrations; the garage/OOC/car-shadow
  one-shot registrars; the RVA data rows; and all 76 @exact hook bodies (fresh `msvc-asi` build +
  verify-bodies: PASS). `CrashProbe` is provably inert with shipped INIs.
* **New findings** (all at the pinned revision; no phase-1 fix has landed):
  * **F23** EpisodicVehicleSupport: port gate 0x300 ({1070,1080}) vs original `build>=6` → 108
    writes missing on 1060/EFLC; plus missing second writes, both find-all fills, and the tail
    pointer stores (G1–G4, 124 byte-verified misses).
  * **F24** RemoveRGSCLoginRequirement: port gate 0x200 blocks the primary E9 on 7 builds where
    the original installs it.
  * **F25** EpisodicWeaponSupport over-applies on 1.0.0.0 (missing `build>=6` gate; same class
    latent in Police/PipeBomb).
  * **F26** the shared mod-check bypass (0x10005500) installs in the original when EITHER
    Patch4RemoveModChecks OR RemoveGFWLUpdatePopup is on; the port gates it only on Patch4.
  * **F22** the spawns "who" helper hard-codes salt 0x570F5725 (= GET_PLAYER_NAME) instead of
    reading `zpatch_salt_c8`; wrong on builds 1000/1040 (probe-miss).
  * **F27** the ZMenuIV `GetZMenuVersion` guard (abort on outdated ZMenuIV.asi) is not implemented.
  * **F28** MiscFixes M5's companion dword write missing on 1060/1070 (malformed `mov eax,imm`).
  * **F29/F30** the DFA installer (`0x10004850`): the port doesn't trigger it via MiscFixes, and its
    own gate misses the original's EFLC-build skip and XLA check (over-application on EFLC).
  * **F31** TeleportProtection writes only `C2` where the original writes `C2 08 00` (`ret 8`) —
    crash-class on every build (default 1).
  * **F32** NoLiveryLimit P3-variant slot publishes missing on 1000/1040 (`jmp 0` body).
  * **F33** Patch6and7: 5 byte writes missing on 1060/1070. **F34** SkipLauncher path: XLivelessAddon
    flag not honored + one over-write. **F36** GFWL version constant 0x10000E83 written as 0x10000003
    by the port. **F37** sub_1000EA00 write gated `@noFCHS` though the original calls it
    unconditionally. (F35: E870 restore omission adjudicated equivalent — see §2.)
  * Plus the phase-1 leftovers re-confirmed open: F19 (HighFPSSpeedupFix patches 1 of 9 sites;
    now reproduced live in the write log), F8 (`zpatch-test` still crashes on x64), F1/F12/F13,
    and the stale docs (AGENTS.md BuildingAlphaFix example; F21 chain-A docs).
* **Coverage diff** (§1): 983 byte-effects MATCH the port on all 9 builds; every MISSING/CONFLICT
  row is triaged to one of the findings above or to a decoder artifact. The per-option × per-build
  matrix (§1.3) shows 513 agreeing cells and 17 real orig-only cells (F23×4, F24×7, F26×6); F25
  (over-application) is invisible to the matrix because both sides write the byte — it was found
  by handler-head re-derivation.
* **Unverifiable**: builds 1100/1061/12043 (no exe), live game run, OOC payload decode, ZMenuIV
  (C6).
* **Completeness / version gates (the user's question)**: the per-option × per-build matrix (§1.3)
  plus the byte-level diff say: with the findings above fixed (F23, F24, F25, F26; and F27 for the
  ZMenuIV guard), every patch the original applies on a supported build is reproduced by the port
  on exactly the builds the original applies it. The 17 orig-only cells are F23×4 + F24×7 +
  F26×6; the 1 over-application is F25×1 (1000); everything else agrees (513 cells) or writes
  nothing on both sides (72 cells). All 76 hook bodies install identically on all builds
  (verify-bodies PASS).

## 0. Method (differs from phase 1: top-down from the game)

1. Enumerated **every write/scan primitive call site** in the original ASI by opcode search:
   225 direct `call 0x10003830`, 3 `call 0x10003810`, 14 `call 0x1005AFA0`, 5 `call 0x1005B6A0`,
   4 `call 0x1005AD60` (byte), 3 `0x1005ADB0` (word), 3 `0x1005B650` (dword), 8 `0x1005B5D0`
   (E9 install), 210 refs to the VirtualProtect IAT `[0x10063010]` (inline writers), attributed to
   the containing function via the IDA function table (`funcs_raw.json`).
2. Decoded each site with my own capstone-based mini-interpreter (`p2_decode3.py`): the scanned
   pattern (literal, char-built, or runtime-formatted seed-decimals), the writes that consume the
   match (offset/bytes/size, E9 installs, ASI-self-pointer stores), and the first-hit semantics.
   204/225 patterns auto-decoded; 21 runtime-built sites decoded separately (see §3).
3. Symbolically applied every decoded row to **every available game exe** (1000, 1004, 1010, 1040,
   1060, 1070, 1080/GTAIV.exe, 12059, EFLC 1110/1120/1130; `p2_scan_all.py` → `p2_scan_matrix.json`).
4. Produced the **port's complete applied-write log per build**: isolated scratch copy of the tree
   with the F8 `nms_alloc` 64-bit fix only, an all-options=1 INI, and a driver that runs
   `apply_recovered_patches_logged` against each mapped exe with its build
   (`p2lead/p2_all_writes.txt`, 1805 writes across 9 supported builds; the shared scratch dir was
   concurrently used by another session, so the isolated tree `p2lead` is the pinned source for
   every number in this report). The ASI build itself was not modified. No phase-1 fix has landed
   in the repo (all required fixes still open — see §5), so the scratch-fixed driver was the only
   way to obtain the port's full write set.
5. Diffed at byte granularity with opcode-level matching for jumps (`p2_diff2.py`,
   `p2_diff2_rows.json`): MATCH (bytes equal), MATCH_BYTE (position matches, port wrote a larger
   span), MISSING, CONFLICT, plus the reverse direction (port writes with no inventory row).
6. The MISSING/CONFLICT candidates were partitioned by handler and re-derived **from the handler's
   disassembly** by five parallel sub-audits (episodic, low-VA region, savegame/freeze,
   limits/shadow/menu, misc+§C), whose verdicts are folded in below.

Limitations (stated up front): the mini-interpreter is a hypothesis generator; every row it emits
was either self-validated against the pattern bytes (page-scalar check, `p2_validate.py`: 123/123
offset/preimage agreements, 0 mismatches) or re-derived by a sub-audit. Rows where register
tracking breaks (stack round-trips) are invisible to the diff; for those the sub-audits re-read the
handler. 1100/1061/12043 have no exe; 12059 is not a build either side supports (dropped from the
diff); live behavior remains unverifiable.

## 1. Game-write inventory (Section A)

### 1.1 Mechanism taxonomy

| mechanism | original implementation | rows/effects | port counterpart |
|---|---|---|---|
| per-option code scans | handler `call 0x10003830` (first hit) + stores/E9/helper writes | 225 sites (204 literal/char-built patterns + 21 runtime-built) | `recovered_patch_ops.inc` + `recovered_op_corrections.inc` + hook registries |
| pattern-scan installers at load | ctor-called `0x1003DA50` (CutsceneFixes chain A), `0x1001BF30` (FastLoading) | 2 installers, slot publishes | `recovered_op_corrections.inc` rows 139/150 |
| index-vector scans | `call 0x1005AFA0`/`0x1005B6A0` (multi-hit vector; e.g. BorderlessWindowed uses match[0] and [1]) | 19 sites | `recovered_match_jumps.inc`, sig_writes |
| data-table RVA installs | op bytes + per-build RVAs burned into the loader's data blob (LoadDLCs, RemoveUselessChecks fills, Patch6and7, SuperLOD, specular) | 44+ per-build rows | `recovered_rva_installs.inc` |
| salt rewrite (preamble) | probe `68 ? ? ? ? 68 A9 54 14 51 E8`; on miss writes ten salt dwords | 10 dwords | `runtime.cpp:326-338` |
| rebased self-pointers | stores of the ASI's own `.data` addresses into game code (0x10158068, 0x101580C0/CC, 0x10158128, 0x10159BC0, 0x10159C08…) | ≥6 sites | `rebased_values.cpp` table |
| loader inline installers | live in the loader body itself: DrivebyFOVFix (0x10053A86: pattern 0x10129ED0 → E9→0x100064B0 + slot 0x1015BDF4), NoImpostorHeightCutoff (0x10053519-756, 2 word NOPs), NoLODLightHeightCutoff (0x1005375E-90F) | 3 blocks | sig_jumps:14 + slot rows + match_literals/literal_stores — verified §3.4 |

### 1.2 Coverage diff (per build, byte-level)

Status counts over the inventory's byte-effects vs the 9 supported builds (definitive run:
complete all-option INI, 1805 port writes with FULL byte payloads; `p2_diff2_rows.json`):
**MATCH 983, MATCH_BYTE 121, MISSING 597, CONFLICT 55** before per-handler triage.
(Self-caught artifacts fixed along the way: the first log run omitted 41 option keys whose rows
live outside `recovered_patch_ops.inc`; the second truncated port write payloads to 8 bytes, which
produced phantom MISSING rows for the tails of 9/12/13/38-byte fills; the VA<0x400000/>0x8000000
decoder garbage is filtered.) The remaining MISSING groups are exactly the per-handler cases
triaged in §2/§3 (HighFPSSpeedupFix F19 63, Episodic G1–G4 ~250 distinct (build,VA) pairs,
0x1000E870 72, SavegameFix 36, ImprovedShaderStreaming 27, FreezeCarFix residue 9, the
10045CE0-family 17, sub_10004850/4620 36, RemoveAllWeaponsProtection 9, RemoveRGSC F24 7 …).

Per-build port write counts and report counters (all options on, `p2lead/p2_all_writes.txt`):
1000: 156 applied · 1004: 178 · 1040: 174 · 1060: 224 · 1070: 243 · 1080: 250 · eflc1110: 199 ·
eflc1120: 199 · eflc1130: 195. (An earlier run with an INI missing 41 option names gave a
misleadingly small log — options whose rows live outside `recovered_patch_ops.inc` were disabled
by the absent-key rule. The definitive log uses the union
of every table's option names.)

### 1.3 Per-option × per-build completeness (the "every patch, every build" view)

Merging variant names (DualVehicleHeadlights@old/@new → DualVehicleHeadlights, etc.), the
(inventory-hit, port-write) agreement matrix over 9 builds × 82 option names is:

**agree 513, orig-only 18, port-only 225, neither 72.**

* The 18 orig-only cells: RemoveRGSCLoginRequirement ×7 (**F24**), RemoveGFWLUpdatePopup ×6
  (**F26** — the port installs the shared bypass only under Patch4RemoveModChecks),
  EpisodicVehicleSupport ×4 (**F23**), VSyncFix on 1040 (**inventory artifact** — the handler
  skips build 5 by its own early-out list; the port's mask 0xFFFFDDC1 is correct).
* The 225 port-only cells are mechanism mismatches, not over-applications: each option there is
  applied by the port through non-scan tables (match_jumps/literal stores/ctor bodies), whose
  builds all lie inside the original's own gate domains (checked per group in §1.3). Two
  exceptions were examined against the handler heads: EpisodicWeaponSupport on 1000 (**F25**,
  over-application; masked by a missing gate row) and FCHS (the old-log artifact; verified to
  match in the definitive run).
* The 72 "neither" cells are option/build pairs where no pattern hits at all (no write either
  side).

### 1.4 Reverse direction: port writes the scan-site inventory does not cover

These are not defects per se: they are writes delivered by the port's other mechanism tables
(match_jumps, literal_stores, indirect_writes, match_literals, slot publishes, ctor bodies), which
the scan-site inventory does not model. Bytes = total over 9 builds (final log):

| option | bytes | disposition |
|---|---|---|
| MouseFix | 585 | literal constants (phase-1 sub4) |
| FreezeCarFix | 435 | 15 sig_installs + 11 slot_installs (handler 0x10037C70) — §3.3 |
| SavegameFix | 360 | sig_installs + sig_write — §3.3 |
| RemoveUselessChecks | 336 | data-table fills — verified §3.1/§3.7 |
| LoadDLCs | 136 | data-table rows — verified §3.7 |
| ImprovedShaderStreaming | 135 | sig_installs — §3.3 |
| BorderlessWindowed | 108 | match_jumps + sig_writes — verified §3.1 |
| CutsceneFixes | 99 | chain A/B (phase-1 F21) |
| NoLiveryLimit / CarPartsShadowFix / GarageModelFix | 90 each | sig_installs/mixed — §3.2/§3.3 |
| StartupTimeFix | 80 | match_literals (phase-1 sub4) |
| OutOfCommissionFix | 70 | ctor body — §3.5 |
| CarDynamicShadowFix | 66 | shared installer + @noE 12B — §3.2 |
| SkipLauncher | 58 | arm 0x10055504 → 0x10045CE0 — §3.3 |
| BenchmarkFix 50 / CarDecalsFix 45 / HighQualityReflections 45 / NewMemorySystem 40 | | corrections/literal stores — §3.3 |
| LocalSettingsPath 36 / IncreaseVehicleModelLimit 36 / NoPhysicsSleep 36 / NoImpostorHeightCutoff 34 | | corrections/literal stores (§3.4) |
| Patch6and7RemoveModChecks 25 | | data-table rows — verified §3.1 |
| DoNotPauseOnMinimize 16 / CarDynamicShadowFix@noE 12 / NoLODLightHeightCutoff 12 / SuperLODFix 10 / FastLoading 9 / CarPartsShadowFix@standalone 9 / SMPA60Fix 8 | | phase-1 sub4/sub5 + §3.3/§3.4 |
| SkipMenu 5 / RemoveRGSCLoginRequirement 5 | | SkipMenu inert (F20); RGSC = the 1080 fallback (F24) |

## 2. Findings (new, phase 2)

### F22 (low) — the spawns "who" helper hard-codes salt 0x570F5725; the original reads `[0x101580C8]`

* Original `sub_1005B290` (the spawns-probe "who" helper, called from 19 sites in
  0x100375E9–0x10037B15): `1005B2AA A1 C8 80 15 10` = `mov eax, [0x101580C8]`; the value is the
  native hash passed to the invoker `0x100038E0`. The loader's probe-miss fallback writes
  `0x406B4B20` to that slot (`10052E28 C7 05 C8 80 15 10 20 4B 6B 40`).
* Port: `hook_bodies_win.cpp:304` (`zpatch_spawns_who_c`) calls
  `zpatch_script_call(0x570F5725u, …)` — the on-disk default — instead of reading
  `zpatch::zpatch_salt_c8`, which `runtime.cpp:335` correctly rewrites to `0x406B4B20` on
  probe-miss builds. On 1000/1010/1040 (probe miss) the port's spawns diagnostic would resolve a
  different (unregistered) hash than the original.
* Impact: DebugPrintSpawns/FreezeCarFix diagnostic text path only (option also requires the
  FreezeCarFix arm); cosmetic, but the same class as F1 and fixable identically
  (`mov eax, [zpatch_salt_c8]`). The sub-audit (§3.5) confirmed the identity of the hash:
  **0x570F5725 = GET_PLAYER_NAME** (FusionFix `natives.ixx:1235`) and that the probe misses only
  on builds **1000 and 1040** (supported!), where the original installs the fallback 0x406B4B20.
  Note the sibling helper 0x1005B530 hard-codes `0x79023CFC` (`1005B58F B9 FC 3C 02 79`), which the
  port already mirrors (line 325) — only the C8 helper misreads.

### F23 (medium; lead + sub-audit confirmed) — `EpisodicVehicleSupport` build mask 0x300 restricts the port to 1070/1080

* Arm 0x10054D3B: kind read + `cmp esi,1; jne; call 0x1001B580` — no build gate.
* Handler 0x1001B580 head: `1001B5AB 83 3D 04 BE 15 10 06` (`cmp [0x1015BE04],6`) `1001B5B2 0F 8C …` (`jl` skip) — applies on every build >= 6 (1004/1060/1070/1080/1110/1120/1130/12043).
* Inventory hits (scan matrix): the handler's patterns hit on 1060/1070/1080/eflc1110/1120/1130 (not 1004).
* Port: `recovered_build_gates.inc:4` `{"EpisodicVehicleSupport", 0x300u}` = bits 8+9 = 1070/1080 only; write log confirms writes only on 1070/1080 (21 each). No writes on 1060/eflc1110/1120/1130 → port gap on four builds.

### F24 (medium; lead + sub-audit (savegame) confirmed) — `RemoveRGSCLoginRequirement` gate 0x200 blocks the primary install on 7 builds

* Handler 0x10045490: scans pattern 0x1012D990; **if found** → E9 at match → 0x10038550 (`100454D1 C6 03 E9`, dest computed `mov eax,0x10038550; sub eax,ebx; sub eax,5`). **If not found** → `10045514 83 3D 04 BE 15 10 09` (`cmp [0x1015BE04],9`) `jne` → build-9 fallback writes (E9 at base+0x498460 ≠ pattern-based).
* Pattern hits: 1000/1004/1010/1040/1060/1070/eflc1110/1120 (absent 1080/12059/1130; 1010 is not a supported build ordinals-wise, both sides skip).
* Port: gate 0x200 (v1080 only) + rows `recovered_sig_jumps.inc:32` (→0x10038550), `recovered_patch_ops.inc:143-4`, `recovered_patch_sites.inc:49`; on 1080 the pattern is absent so only the fallback `recovered_rva_installs.inc:41` fires (rva 0x498460). Net: the primary E9 never installs anywhere; the original installs it on 1000/1004/1040/1060/1070/eflc1110/1120 → port gap on seven builds.

(Note: the gate table's comment says "positive equality gates only"; both masks look like the extractor took the handler's *fallback-branch* build constant as the option gate.)

### F25 (low; lead + sub-audit confirmed) — `EpisodicWeaponSupport` over-applies on 1.0.0.0

* Handler 0x1001B380: entire body inside `if (5 < DAT_1015be04)` — Ghidra text
  (`decompiled_1001b380.c` line `if (5 < DAT_1015be04) {`), disasm head
  `1001B390 83 3D 04 BE 15 10 06 / 7E ...` — applies only on builds ≥ 6 (v1004+).
* The 1000 ordinals: 1000→1 (`classify_game_build`), so `5 < 1` false — no write on 1.0.0.0.
* Port: feature mask 0xFFFFFFFF (`recovered_feature_coverage.inc:EpisodicWeaponSupport`) and the
  pattern `83 3D ? ? ? ? 02 7C 32 8B 41` hits on 1000 at rva 0x3A8310; write log shows
  `W 1000 EpisodicWeaponSupport rva=0x3a8317 len=2 bytes=9090` — the original writes nothing there.
* Same head shape on PoliceEpisodicWeaponSupport (0x1001BEC0) and PipeBombDropIconSupport
  (0x1001BE40) with masks 0xFFFFFFFF, but their patterns only hit on 1060+ where both sides agree.
* Root cause shared with F23: the gate extractor (`tools/extract_build_gates.py`) only recognizes
  `DAT_1015be04 == N` (operator-first) and skips reversed heads like `5 < DAT_1015be04`; inner `==`
  branches then become the mask (F23) or no mask is emitted at all (F25).
* Systematic sweep of the decompiled handlers (`p2_revheads.py`): exactly five handlers use a
  reversed head test, all `>= 6` — TBoGTHeliHeightLimit (0x1001B310), EpisodicWeaponSupport
  (0x1001B380), EpisodicVehicleSupport (0x1001B580), PipeBombDropIconSupport (0x1001BE40),
  PoliceEpisodicWeaponSupport (0x1001BEC0). Cross-checked against per-build pattern hits: only
  EpisodicVehicleSupport (F23) and EpisodicWeaponSupport (F25) produce a divergence; the other
  three options' patterns do not hit on the excluded builds (1000/1040), so they agree in practice.
  TBoGTHeliHeightLimit is additionally handled correctly by its op row + no mask.

### F26 (medium, lead-verified) — the shared mod-check bypass (0x10005500) is gated in the port only by `Patch4RemoveModChecks`

* Original loader arms:
  * `0x10055464` (Patch4RemoveModChecks): `cmp esi,1; jne` → `C6 05 14 9C 15 10 01`
    (`mov byte [0x10159C14],1`) `E8 63 00 FB FF` (`call 0x10005500`).
  * `0x100554B4` (RemoveGFWLUpdatePopup): `cmp esi,1; jne` → `C6 05 87 9B 15 10 01`
    (`mov byte [0x10159B87],1`) `E8 13 00 FB FF` (`call 0x10005500`).
  * `0x10055504` (SkipLauncher): `call 0x10045CE0` (a different function).
* `0x10005500` scans pattern 0x101297E0 (`2B C1 1B D6 …`), writes E9 at match → 0x100054E0 and
  publishes `[0x10159C18] = match+0xC`.
* Port: the install rows are labelled **Patch4RemoveModChecks** only
  (`recovered_op_corrections.inc:69` E9→0x100054E0; `:220` slot 0x10159C18) and the flag table
  covers both keys (`option_flags.cpp:15-16`). With `RemoveGFWLUpdatePopup=1`,
  `Patch4RemoveModChecks=0` the original installs the bypass (its own arm calls it) while the port
  installs nothing (write log has no RemoveGFWLUpdatePopup writes on any build). Both are shipped
  default 0, so this only bites users who enable RemoveGFWL without Patch4.
  Fix shape: either give the two rows a shared key set (apply if either option is on) or duplicate
  the rows under both names.

### F27 (low, new) — CLOSED 2026-10-21: ZMenuIV version guard

The original loader calls `sub_10052300` unconditionally at `0x1005527B`: it
looks up `GetZMenuVersion` from `ZMenuIV.asi`, rejects versions below `0x35DD1`
with the recovered message/flags, and terminates with code 0. The current
ZolikaPatch DllMain invokes `enforce_zmenu_version()` immediately after config
initialization, before checking its status or applying option arms. The helper
mirrors module/export-absent behavior, the output-parameter call convention,
threshold, message, caption, flags, and exit code. `test_main.cpp` checks the
threshold boundary; this check is part of ZolikaPatch (not the separate ZMenuIV
plugin's internal feature audit).

### F28 (medium, new) — MiscFixes M5 companion write missing on 1060/1070

* Original: `C6 06 B8` @`10023EE7` (byte B8 at match) **and** `C7 06 01 00 00 00` @`10023F02`
  (dword 1 at match+1). Port row `recovered_patch_ops.inc:104` writes only `B8` → the game code
  becomes `mov eax,0x307501F8` instead of `mov eax,1`; the following `mov [global],eax` stores the
  bogus value (1060 `0x8133A1`, 1070 `0x813161`). 1080 is unaffected (pattern absent).

### F29 (low, conditional, new) — MiscFixes should also trigger the DFA installer (0x10004850)

* Original: MiscFixes handler calls `0x10004850` at `10024018` (guards: `[0x10159B84]`=XLA unset,
  GTAC.dll absent, build-id ∉ {0xA,0xB,0xC,0xD}, one-shot `[0x10159C16]`). Port: DFA installs run
  only under `SkipLauncher` (`dllmain.cpp:42-62`). Config `MiscFixes=1, SkipLauncher=0`, non-EFLC,
  no GTAC/XLA → original installs DFA hooks, port doesn't.

### F30 (medium, new) — the port's DFA path is missing the original's build/XLA gates (over-application)

* Original `sub_10004850` guards: `1000487B` (`[0x10159B84]`=XLivelessAddon), `1000488E`
  (GetModuleHandleA("GTAC.dll")), `1000489D-…C0` (build-id ∈ {0xA,0xB,0xC,0xD} ⇒ skip — EFLC).
  Port `dllmain.cpp:59` only checks `GTAC.dll == nullptr` (its comment claims the build check
  exists — code-vs-comment bug). With `SkipLauncher=1` (default!) on EFLC 1110/1120/1130 the port
  writes DFA hooks the original skips → violates the AGENTS.md build-gating rule.

### F31 (high, new) — TeleportProtection writes only `C2`; the original writes `C2 08 00` (`ret 8`)

* Original `sub_10037170`: `100371B0 C6 06 C2` (`mov byte [esi],0xC2`), then `100371BF 46` (inc esi),
  `100371C6` VirtualProtect(esi,2,0x40), `100371C8 B8 08 00 00 00`, `100371CD 66 89 06`
  (`mov word [esi],8`).
* Port: one row, one byte (`recovered_patch_ops.inc:191`, value "C2"); the live log writes
  `len=1 bytes=C2` on all 9 builds. The game's function prologue `55 8B EC 83 E4 F8` becomes
  `C2 8B EC …` = `ret 0xEC8B` (stack garbage) instead of `C2 08 00` (`ret 8`). TeleportProtection
  ships default 1 → crash-class on every build. Same extractor class as F28 (16-bit `mov [esi],ax`
  stores are dropped).

### F32 (medium, new) — NoLiveryLimit P3-variant slot publishes missing on 1000/1040

* Sub-audit: on 1000/1040 the P3 variant publishes `[0x10159BA4]`/`[0x10159BAC]`; the port has no
  rows for them → `zpatch_nolivery_fn`/`zpatch_nolivery_resume_d` stay 0 → the hook body jumps to 0.
  (Phase-2 diff had shown `NoLiveryLimit` MISSING 18 B + the log lacks those slots.)

### F33 (low, new) — Patch6and7: 5 byte writes missing on 1060/1070

* Handler sub_10044710 has additional `add esi,imm` byte writes: 1060: `00`@+0x38AC, `00`@+0x397E;
  1070: `EB`@+0x390E, `00`@+0x388C, `00`@+0x395E (verified the `add esi` sites at 0x100448F8/0x1004491E/
  0x10044952/0x10044B62/0x10044B88); none are in the port's op/rva rows or the log.

### F34 (low, new) — SkipLauncher path divergences

* The port's `skip_launcher.cpp` does not honor the original's XLivelessAddon flag
  (`[0x10159B84]`) and its entry-9 (`0xe0d0`) writes `33 C0` even when the original skips the whole
  block (its dword-dependent target match absent). Details in `p2sub/savegame/report.md` §item 20.



### F35 (resolved, correction) — sub_1000E870 restore writes: omission is equivalent; the 9 CONFLICT rows are attribution artifacts

* `sub_1000E870` is the FCHS-absent restore path: the FCHS handler `sub_1001ADD0` calls it only
  when its flag byte is non-zero (`1001AF04 80 7D F7 00 / 74 05 / 1001AF0A E8 61 39 FF FF`), which
  the sub-audit decoded as "FCHS key absent". It re-writes the pattern bytes
  (`[m+6]=04,[m+7]=05`; `[m+6]=04,[m+7]=01`; `0F 84 9E 00 00` at m+6..+0xA) to undo the
  `sub_1000B790` reductions when FCHS is off.
* With FCHS=1 (the definitive all-on run) E870 is **not** called → the original's final byte at
  1080 0x8F7422 equals BuildingDynamicShadows' `00` (`10005C39 C6 06 00`) → the port's `00` is
  correct. The port's omission of E870 is documented (`shadow_audit_handoff.md` P4) and
  behaviorally equivalent. The 9× `1000E8EE` CONFLICT rows (inventory `04` vs port `00`) are
  inventory-vs-port attribution artifacts: both target the same byte from different option paths.
* The only real divergence in this area is F37 (the `sub_1000EA00` gating).

### F36 (low-medium, new, lead-verified) — GFWL version constant divergence

* Original `sub_10005500` writes `mov dword [esi], 0x10000E83` at xlive+0x66F770/0x66F774/0x6B55D0/
  0x6B55F0 (`1000542D`, `10005456`, `1000547F`, `100054A8`; `C7 06 83 0E 00 10`). The original's
  own source mirror shows this is the `getnewver()` magic (`ZolikaPatch/dllmain.cpp:379-382`).
* Port: `hook_bodies_win.cpp:915-918` writes `0x10000003` at the same four offsets. Under the 1:1
  policy this should be `0x10000E83` (or a documented justification).

### F37 (medium, new, sub-audit-verified) — `sub_1000EA00` gated `@noFCHS` though the original calls it unconditionally

* Original `sub_10010B60` calls `sub_1000EA00` unconditionally on the common tail (`10014FB4`,
  both A/B scan outcomes converge at `10014FA2`) → byte `[m+2]=0` of pattern
  `50 68 04 01 00 00 6A 02 6A 00 E8` (`10010AF3`), every build.
* Port: `recovered_patch_ops.inc:148` row is gated `CarDynamicShadowFix@noFCHS` → gated out when
  FCHS=1 (live log `gated CarDynamicShadowFix@noFCHS sig-write 2`). Net effect masked when
  BuildingDynamicShadows=1 (its row writes the same byte) but visible with BDS=0.


## 3. Per-handler verification results (Section A + B integrated)

### 3.1 Lead-verified sections (independent of the sub-audits)

* **BorderlessWindowed** (handler 0x10005970): re-derived end-to-end. Scans pattern 0x101299A4
  (`FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00`, 2 hits on 1080 at 0x5BB10E/0x5BB563 via the
  find-vector 0x1005AFA0), writes 6×90 at each match (`100059D3 B8 90909090; 89 06; 66 89 46 04`
  and `10005A40` for match[1]), then overwrites match[0]/match[1] with `E8` + rel32 to
  0x10005890/0x10005900 (`100059FD C6 07 E8` … `10005A85 mov eax,0x10005900; sub eax,edi; sub
  eax,5`). Net effect per site: E8 rel32 + one 90. Port: match_jumps (E8 → the two bodies) +
  sig_writes (90 at +5, indices 0/1) = net-identical. The intermediate 6-NOP fill is fully
  overwritten by the E8 install; its omission is faithful. Gate: arm `test al,0x10` on the
  BorderlessWindowed bitmap value (0x10055805); port `recovered_op_corrections.inc:139-140` +
  `recovered_match_jumps.inc:7-8`. (F12 FusionFix gate still missing — unchanged.)
* **SkipIntro** (handler 0x10045C50): pattern 0x1012DC08 (`89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50`);
  `10045C81 lea edi,[eax-6]`, `10045C84 mov [0x1015AD74],eax` (slot publish = match), `10045C97
  C6 07 E9` + rel32 `mov eax,0x10045C30; sub eax,edi; sub eax,5` → E9 at match−6 → 0x10045C30.
  Port rows (sig_installs site_offset −6 dest 0x10045C30 + slot match 0x1015AD74) confirmed;
  write log has the E9 per build.
* **TBoGTHeliHeightLimit** (handler 0x1001B310): head `cmp [0x1015BE04],6; jl skip`; pattern
  0x1012A440 (`83 3D ? ? ? ? 02 7C 0A F3 0F`, hit 0x8F0071 on 1080); `1001B342 lea edi,[eax+7]`;
  two-byte 90 write via `mov word [edi],ax` (0x90909090). Port op row offset 7 `90 90` ✓; write log
  matches on 1060/1070/1080/eflc1110/1120/1130 and not on 1000/1040 (build>=6) — correct.
* **RemoveAllWeaponsProtection / BikeFeetFix** and other MATCH_BYTE groups: the port's single
  larger fills cover the per-store rows (13-byte fill vs 4×4+1 rows) — matched by position.
* **Patch6and7RemoveModChecks** (handler 0x10044710): build-branch constant writer
  (`mov esi,[0x1015AD70]; add esi,imm`), no pattern scans; port covers via op rows
  (`recovered_patch_ops.inc:119-131`, EB/90/E9 with per-build masking 0x180) + rva_installs
  38-40 (E9 displacements). Spot-checked 1060 (EB@0x392E, 7×90@0x42E7-0x42ED, E9@0x3982) and 1070
  (EB@0x39F4, E9@0x389C/0x3962) against the write log — agree.
* **LoadDLCs** (handler 0x100234E0): per-build constant rows; port write log 1080 matches
  `recovered_rva_installs.inc` rows 11-19 exactly (E8@0x48F558→0x10023480, E9@0x49035C/0x4903D4/
  0x49040E, E8@0x49043A, 12×90@0x49043F).
* **Preamble salt mechanism** (loader 0x10052E00-0x10052F60): 8 pattern scans; probe
  `mov ecx,0x1012B710; call 0x10003830` → if null, ten `mov dword [0x101580xx],imm` salt writes
  (`10052E1E..10052E78`, incl. `d4←0x09399528`, `c8←0x406B4B20`). The port mirrors this in
  `runtime.cpp:326-338` (same probe, same ten values — re-checked value-by-value). Consequence for
  F1/F22: on probe-HIT builds the on-disk salts (`d4=0x75E005F1`, `c8=0x570F5725`) stay, and hooks
  must read the slots; hard-coding the fallback values is wrong exactly on the supported 1080-class
  builds. F1 (driveby) and F22 (who) are the two hard-code sites.
* **VSyncFix** (handler 0x100503B0): the handler skips builds 1,2,3,4,5,9,13
  (`100503C7 cmp eax,9 / 0x100503D0 cmp 0xD / 0x100503D9 cmp 1 / … 0x100503F9 cmp 5` → `je skip`);
  the port models this as the feature mask 0xFFFFDDC1 (`recovered_feature_coverage.inc:101`), and
  the write log matches on 1004/1060/1070/eflc1110/1120 — correct; the diff's 1040 "orig-only" is
  an inventory artifact (my scan-application ignores handler skip lists).

### 3.2 Episodic cluster (sub-audit `p2sub/episodic/report.md`, 43 sites re-derived)

All 43 sites re-derived from bytes; pattern buffers verified static (one xref each — the "runtime
built pattern" hypothesis was false for this family); per-build expected-write sets compared with
the port tables + a focused harness run.

* **F23 (G1, real, medium-high): EpisodicVehicleSupport missing entirely on 1060/eflc1110/1120/1130
  (27 writes/build) and partially on 1070/1080.** Port gate 0x300 ({8,9}) vs original `build >= 6`.
  Examples (1080): 0xBFECDD 13×90 (site 1001B5C9), 0x9D22EF 9×90 (1001B614), 0x4FC73A 13×90
  (1001B6ED). Root cause: `tools/extract_build_gates.py` maps the tail `cmp [0x1015BE04],8/9`
  equalities to the whole option.
* **G2 (real): missing second writes even where the port applies.** Site 1001B652: original also
  writes 2×90 at M+0x11 (1080 target 0x4A86FF, preimage `7C 2F`); port row ops:47 covers M..M+7 only.
  Site 1001B8FB: 2×90 at M+0xA (1080 target 0x9F21C2, preimage `7C 18`); port row ops:49 covers only
  M−0xF..M−8.
* **G3 (real): both find-all sites have no port rows at all.** 1001B745 (`7C 18 0F BF 4E 2E`):
  8×90 at M0−6/M1−6 on every hit build (1080: 0x8FEC0C/0x8FEC40). 1001B9DF: 9×90 at both matches
  (1080: 0xB1D8CB/0xB20EB7). `handlers_batch_2.md:185-195` admits the fills are absent from the ops
  table.
* **G4 (real): tail pointer stores absent.** build 1070: `[exe+0x21500]`/`[exe+0x7D91AE] ← 0x10158128`
  (gate ==8); build 1080: `[exe+0x95040]`/`[exe+0x684ABE] ← 0x10158128` (gate ==9). The ASI global
  0x10158128 = int 2; effect: two `cmp [episode_global],2` operands are re-pointed at the ASI flag.
* **F25 (confirmed): EpisodicWeaponSupport site 1001B49D applies on 1000 in the port only**
  (`W 1000 EpisodicWeaponSupport rva=0x3A8317`; original gate `build>=6` skips id 1). Latent for
  PoliceEpisodic/PipeBomb (no pattern hit on 1000/1040).
* **Verified covered**: EpisodeSwitchFreezeFix (5 sites ×9 builds, E9s + 3 slot publishes),
  EpisodicWeaponSupport (8 sites, 1060+), BikeFeetFix, BikePhoneAnimsFix, PipeBombDropIconSupport,
  PoliceEpisodicWeaponSupport, EmissiveLerpFix (E9@M−0x18 + 2 slots).
* **FCHS**: the p2-log "no writes" was the missing-key artifact only; a focused run reproduces all
  five writes per build at the original's VAs (incl. the original's own 0x101580C0 pointer →
  the port's 2.0f global). Not a gap.
* Extra config-semantics divergence (supporting F2/F3/F6 class): FCHS's arm reads the key twice
  (parse1 default "1", parse2 default "0", `setne cl` → handler param); the original runs the
  handler + `call 0x1000E870` when the key is ABSENT. The port defaults absent=off and has no
  0x1000E870 rows (see F35 in §2 for the 0x1000E870 conflict rows).

### 3.3 Low-VA region + car-shadow (sub-audit `p2sub/lowregion/report.md`, 43 sites)

All runtime-built patterns decoded (seeds = pattern-text chars) and scanned per build; every
scoped function re-derived. Verdicts:

* **COVERED (all 9 builds unless noted)**: sub_10004110 (IDE-parser hook installer, shared by 5
  arms — slot [0x1015BE10]=m−1 + E9 at m−0xA → 0x100040E0); sub_10004620 (SkipLauncher ROM
  message window, 1004+); sub_10004850's nine DFA site rows (under SkipLauncher, incl. the two
  collected targets at 10004B5F/10004BC6); moving parts sub_10005210 (1000524C/100052B5 with
  their slot math); Aim (100055E2), BetterMPSync (10005822), CollisionDeadlock (10005E52),
  CombinedRadioFreeze (10005F02), CopSirens (10006372/100063DB), DualVehicleHeadlights
  (10006792 with the build≤6/else destination split — port gates verified); BuildingDynamicShadows
  (10005B6F 2-hit collector + 10005BAD E9/slots + 10005C1F byte 00 at m+2); BuildingAlphaFix both
  patterns; CarPartsShadowFix sub_10006910/10007E40 (10007D85 E9+slots; 1000A4C1/1000B749
  mutually exclusive); **CarDynamicShadowFix's shared installer sub_10010B60 A–F** (all six
  runtime patterns match the port's curated rows at the same unique VAs on every build; E-variant
  proxy mask {1000,1040} verified correct because pattern E exists only on 1004).
* **F26 confirmed (G3)** — RemoveGFWLUpdatePopup alone misses the shared stub (row labelled Patch4).
* **F29 confirmed (G1)** — MiscFixes=1 (with SkipLauncher=0) does not trigger the DFA installer
  (original: tail call at 10024018 under the XLA/GTAC/build guards).
* **F30 confirmed (G2)** — the port's DFA path over-applies on **eflc1110** (all DFA patterns
  exist there; the original skips builds 0xA–0xD at 1000489D-100048C0): port installs dfa_init
  0x45466D, create_file 0xD2C593, close_handle 0xD2C72B, set_file_pointer 0xD2C752,
  get_file_size_ex 0xD2C7A1, read_file 0xD2C5F8, capture 0x465EAC, collector 2 hits.
* **F36/F37** as above; F35 adjudicated as an artifact (E870 restore omission equivalent).
* CopSirensFlag CONFLICT rows resolved: the VA/pattern belongs to **PopupHeadlightsFlag**
  (`sub_10044590`, `mov dword [esi],0x101580CC` at m+0x52) — the port's rebased
  `&zpatch_popup_angle` (π/2) is the correct equivalent.
* Note: the port's CarDynamicShadowFix pattern A row is 3 bytes longer than the original's
  17-byte pattern (both match the same unique VA on all six 1060+ builds → no behavioral
  difference; fidelity note only).
* **G6 (absent-key class, F2/F3)**: the original treats an ABSENT `CarPartsShadowFix`
  (100544D1/1005466F) or `ForceCarHeadlightShadows` (100545BD) key as default "1" (read with
  default); the port's `option_enabled` defaults absent to off. Affects hand-edited INIs only
  (shipped/user INIs carry all keys).

### 3.4 Limits / shadow / menu-tail handlers (sub-audit `p2sub/limits/report.md`)

* **F19 re-confirmed with full bytes (G1)**: the handler writes 9 sites per build —
  `−7:9090, +3:9090, +0xE0:EB, +0xE7:9090, +0x16F:9090, +0x179:9090, +0x25F:9090, +0x266:9090,
  +0x2FC:9090` (1080 match 0x7CBFE9; missing targets 0x7CBFEC/0x7CC0D0/0x7CC158/0x7CC162/0x7CC248/
  0x7CC24F/0x7CC2E5). The port ships `-7 ×8 duplicates` + the EB at +224 (`recovered_patch_ops.inc:63-71`)
  → 7 writes missing on every build. Note +0x25F receives a NOP in the original, not an EB.
* **F28 (new, real, 1060/1070)**: MiscFixes M5 (`sub_10023DD0` scan 10023ECE) must write `B8` at
  match **and dword 1 at match+1** (`10023EE7 C6 06 B8`, `10023F02 C7 06 01 00 00 00`); the port row
  (`recovered_patch_ops.inc:104`) writes only the byte, leaving `mov eax,0x307501F8` instead of
  `mov eax,1`. 1080 unaffected (pattern absent); 1060/1070 are supported builds.
* **F29 (new, conditional)**: the original's MiscFixes handler also calls the DFA installer
  `sub_10004850` (`10024018`) when its guards pass; the port triggers the DFA installs only under
  `SkipLauncher` (`dllmain.cpp:42-62`). Config `MiscFixes=1, SkipLauncher=0, GTAC absent, non-EFLC`
  → original installs, port doesn't.
* **F30 (new, port-only over-application)**: `sub_10004850`'s own guards (`1000487B` XLA,
  `1000488E` GTAC, `1000489D-…C0` build-id ∈ {0xA,0xB,0xC,0xD} ⇒ skip) are not modelled by the
  port's DFA path (`dllmain.cpp:59`: only `GTAC==nullptr`; the comment claims otherwise). With
  `SkipLauncher=1` on EFLC 1110/1120/1130 (or XLivelessAddon present) the port writes DFA hooks the
  original skips — breaks the AGENTS.md build-gating rule.
* **F13 confirmed + located (G5)**: NewFilesCompatibility's handler gates ALL its installs on
  `GetModuleHandleA("GTAC.dll")==nullptr && GetModuleHandleA("JacksIVFixes.asi")==nullptr`
  (`10051939`/`10051948`); the port has no such gate.
* **Loader inline blocks mapped**: DrivebyFOVFix is the `0x10053A86` block (key 0x10130428 default
  "1"; E9→0x100064B0, resume slot 0x1015BDF4=match+5) — port covers via
  `recovered_sig_jumps.inc:14` + slot + `abi_driveby_fov_shim`; NoImpostorHeightCutoff
  (10053519-756, 2 word writes) and NoLODLightHeightCutoff (1005375E-90F) are inline too;
  DoNotPause/RestoreDeathMusic set their flags inline before calling their handlers.
* **Increase*/Override rows are covered** (literal/indirect tables; verified in the definitive run —
  the old p2 log's "MISSING" was the INI-key artifact).
* **G6 (minor)**: `OverrideGTADatPathTo=0` → original leaves the default buffer, port writes `"0"`.
* **G8 (original instability, document-only)**: NoImpostorHeightCutoff block A writes `vec[1]`
  without a bounds check; on 1000 the pattern matches once → the original VirtualProtects/writes a
  garbage pointer. The port's match-literals skip absent indices (1 write on 1000, 2 elsewhere) —
  a safe divergence to record in `parity_and_improvements.md` §4.
* All other scoped sites COVERED per build (WantedLevelCrashFix, PedModelsInCutscenesFix,
  RestoreDeathMusic, LocalSettingsPath, NewSavesCompatibility, DoNotPauseOnMinimize,
  ForceShadowsOnObjects, BuildingAlphaFix both patterns, VSyncFix + its build-skip mask, etc.),
  with the pointer-store CONFLICTs verified value-equivalent (FCHS/LocalSettings/Override/VSync).



### 3.5 Misc handlers + Section C (sub-audit `p2sub/misc/report.md`)

* **Curated rows for 0x1001EFA0 (HighFPSBikePhysicsFix), 0x10036F30 (GroupHackFix —
  the "BlackscreenFix" label was wrong), 0x10036FD0 (BlackscreenFix, both slots incl.
  `match+*(match+82)+86` and the deref +87), 0x10046A10 (SuperLODFix six base+adds + two E9s),
  0x10005CA0 (BuildingAlphaFix): 10/10 CONFIRMED from bytes, 0 refuted.** Publish-before-install
  order verified (`[0x1015AD84]`, `[0x1015AD60]`, `[0x1015AD58]`, `[0x1015AD5C]`).
* **C8 BuildingAlphaFix resolved**: both handler writes are applied by the port — the 13×90 fill
  (op row, not suppressed; write log `rva=0x559cd3 len=13` on 1080) and the match_branch E9
  (→ the je's own target). The AGENTS.md "pattern 2 left unapplied" example is **stale docs**
  (superseded 2026-10-13 per parity_and_improvements.md:240); no code divergence.
* **C2 vehicle-light sections resolved**: `[PopupLightAngles]`/`[HeadlightConeOffsets]` parsing is
  1:1 (Car1..512 + Name/Angle/Offset, `hash:`+joaat, ×π÷180, raw offset; bases 0x1015C628,
  0x1015BE28, 0x1015ADB8, 0x1015B5C0). `0x101580CC` is a *popup-lights body* store (value +
  address-retarget at match+0x52), not a loader store. Two edge nuances: invalid/ERANGE float
  values → original throws (catch behaviour CANNOT-VERIFY) vs port `parse_error`; empty
  `AddOptionsInMenu=` → original throws vs port disables.
* **C3 IVMenuAPI resolved**: GetModuleHandleA("IVMenuAPI.asi")/"AddIVMenuOption" and the two
  registrations (Death Music, Pause Game On Focus Loss) with wide titles, anchors, callbacks
  0x10050A30/40 and 0x1001EDB0/C0 — the port matches exactly (incl. gating via `sub_10052380`
  equivalent). **New gap: F27.**
* **C4 resolved**: `0x570F5725` = **GET_PLAYER_NAME** (FusionFix `natives.ixx:1235`), read from
  the mutable `dword_101580C8` at call time in `sub_1005B290`; the probe misses only on **1000 and
  1040** (supported builds!) where the fallback 0x406B4B20 is installed. The port's resolver path
  is correct; F22 is the one hardcode.
* **C5 resolved**: garage (0x10050820), OutOfCommission (0x100509E0) and car-shadow (0x10022ED0)
  registrars/callbacks/resets re-derived; all three callbacks self-clear mid-body; the port's
  omission of the car-shadow reset is documented and provably unobservable (all zeroed slots have
  no readers after install).
* **C7 CrashProbe resolved (inert)**: six probe bodies, armed only by `[Debug] CrashProbe` at
  dllmain.cpp:246-254; the shipped/repo INIs have no `[Debug]` key, `g_probe_va` stays 0, and the
  only other arming calls are in the native test. Cannot fire in a normal run.

### 3.6 Savegame / MP-freeze / launcher tails (sub-audit `p2sub/savegame/report.md`)

Per-site verdicts (all re-derived from bytes; port compared via tables + the definitive log):

| option/site | verdict |
|---|---|
| SavegameFix sub_10045610 | **COVERED** — 11 writes/build (10 E8 + 1 word NOP) byte-equal on all 9 builds; the diff's 36 MISSING rows were interpreter cumulative-offset artifacts (the handler reloads `edi` from the match before every block); the "42 B port-only" was the same accounting error |
| FreezeCarFix / DebugPrintSpawns sub_10037C70 | **COVERED** — 17 E8 installs + 12 slot publishes; handler is called iff `FreezeCarFix==1` (DebugPrintSpawns alone never calls it); the definitive log has all 435 B |
| BadPlayerModelCrashFix / FreezeGunFix / RemoveAllWeaponsProtection / MPNikoCrashFix / NewMemorySystem / RemoveBoundingBoxCulling / NoLoadingSleep / PathNodeCrashFix / PlaneCrashFix / PlayerAsPedComponentsFix / SMPA60Fix / ImprovedShaderStreaming / SkipMenu / CrashMsgFix(prod) | **COVERED** per build (details in the sub-audit; e.g. NewMemorySystem `10038680` E8@M−0x22 + E9@M−0x1D rel=8 + slot 0x10159CD8; ISH reproduces the literal rel32s 0x8E/0x377; PathNode slots BDFC/BE00; Plane BE14/BE18) |
| TeleportProtection sub_10037170 | **F31** |
| NoLiveryLimit sub_1003D420 | **F32** |
| RemoveRGSCLoginRequirement sub_10045490 | **F24 confirmed independently** (E9→0x10038550 on 7 builds blocked by the 0x200 gate) |
| Patch6and7RemoveModChecks sub_10044710 | **F33** |
| sub_10045CE0 / SkipLauncher | mostly covered via `skip_launcher.cpp`; **F34** (minor) |
| CopSirens/PopupHeadlights pointer store | CONFLICT resolved (port stores `&zpatch_popup_angle`, same π/2 value) |

### 3.7 Phase-1 positive-claim confirmations (top-down re-derivation, user-requested)

Re-derived independently with the phase-2 machinery (not by re-reading phase-1's method):

1. **OOC payload byte-identical** — `zpatch.asi().read(0x100B6798, 163796)` →
   sha256 `6dc0e5fe174de83d…` ✓ (matches phase 1).
2. **All 98 options are loader-read** — re-derived from the loader's own reads:
   * 84 direct arm keys (`arms3.txt`), plus
   * 13 keys read as REVERSED literals pushed to 0x1005BFF0/0x1005C160 (AddOptionsInMenu
     0x1005327B, CarDecalsFix 0x10052FBE, CutsceneFixes 0x10055048, DrivebyFOVFix 0x10053978,
     GarageModelFix 0x10055536, MouseFix 0x1005571B, MovingPlaneParts 0x10054A86,
     MovingSteeringWheelSupport 0x10054A18, NewMemorySystem 0x10053B4E, NoImpostorHeightCutoff
     0x10053582, NoLODLightHeightCutoff 0x100537C7, NoPhysicsSleep 0x100533DA, PopupHeadlightsFlag
     0x100549B1, ShakingEngineBlock 0x10054AF4 — 14 lines, one per key), and
   * **CarDynamicShadowFix, whose key is composed at run time**: `1005444D-10054485`
     `push 0x10130FB8,0x10130F74,0x10130F20,0x10130ECC,0x10130E7C` =
     `sprintf(buf,0x40,"Ca%snam%s%swF%s","rDy","icSh","ado","ix")` → "CarDynamicShadowFix", read via
     0x1005C270 at `10054485`. (This is the string family phase 1 recovered as the bare format
     `"Ca%snam%s%swF%s"`; it is a KEY builder, not a pattern.)
3. **Arm topology confirmed for the car-shadow cluster**: `CarDynamicShadowFix==1` → arm reads
   `CarPartsShadowFix` (default "1") and `ForceCarHeadlightShadows` (default "0") → `call 0x10022F40(a,b)`;
   separate arms for `CarPartsShadowFix` (0x10054658) and FCHS (0x100545A2) follow. The port's
   variant model (`CarDynamicShadowFix@noE`, `...@noFCHS`, standalone rows) is the right shape
   (per-row verdicts from §3.2).
4. **Loader preamble** — 8 scans + the ten-salt fallback block re-derived byte-wise (§3.1).
5. **Data-table RVA rows** — re-derived from the handlers' compiled base-relative constants and
   compared with `recovered_rva_installs.inc`: all 27 LoadDLCs rows' RVAs appear in handler
   0x100234E0's per-build blocks (e.g. build 7: 0x4137CD/0x4141FC/0x414274/0x4142AE/0x4142DA/
   0x4142DF/0x414300/0x41445C/0x4144D4/0x41450E/0x41453A/0x41453F; build 8: 0x41356D/0x4141FC/…;
   build 9: 0x48F558/0x49035C/0x4903D4/0x49040E/0x49043A/0x49043F + the three slot stores), plus
   Patch6and7 1060/1070, RemoveUselessChecks 1040/1070/1080 (19 writes on 1080 match the build-9
   branch's fills), SuperLODFix 1070/1080 (six `lea` adds match), RemoveRGSC 1080 fallback
   (0x498460). F14's latent `recovered_rva_jumps` mispairing remains open (does not fire).
6. **74 @exact hook bodies** — re-verified by building the 32-bit ASI from the pinned tree and
   running `make msvc-asi` (includes `verify_hook_bodies.py`); result recorded in §3.7.1 below.
7. **183/191 op rows** — superseded by the phase-2 byte-level diff (983 MATCH byte-effects); the
   8 exceptions are the findings in §2/§3 (F19, Episodic G1-G4, RemoveRGSC, ISV, ISH, SavegameFix
   residue).

8. **MouseFix's runtime-built patterns decoded** (handler `sub_10024600`, the 75 KB function with
   no direct callers; 8 scan sites): the seeds are `mov ebx,imm` in the div-10 decimal builder →
   strtol → append. Decoded and scanned on 1080: `8B 44 24 0C 85 C0 0F 57 D2 …` → 0x4A9E11 (E9),
   `8B 44 24 08 8B 4C 24 04 50 51 E8 …` → 0x4AA190/0x4AA1B0 (E9×2), `83 C4 08 F7 D8 F3 0F 2A C0
   6A 00 …` → 0x684C1E (66 0F 6E C0 + 90 90), `… 6A 01 …` → 0x67CCF6, `… F3 0F 2A C8 …` → 0x69AB42,
   `56 8B 74 24 08 …` → 0x74D230+0x33 (E8), `F3 0F 2A …` → 0x74D3A1+… (E8). Every one has a
   matching MouseFix write in the definitive log (17 writes on 1080) — covered.

#### 3.9.1 msvc-asi / verify-bodies result

Re-run from a pristine copy at the pinned revision (`p2verify`, src hashes identical to the repo:
`hook_bodies_win.cpp` 24b60f80…):

```
make msvc-asi      # builds 32-bit ZolikaPatch.asi + ZMenuIV.asi with MSVC x86 (VS18)
python tools/verify_asi_abi.py --candidate-dir build/msvc-x86
  PASS: ZMenuIV.asi abi ABI
  PASS: ZolikaPatch.asi abi ABI
make verify-bodies (tools/verify_hook_bodies.py)
  PASS abi_* … (76 bodies)
  aligned SSE scan: clean (ZolikaPatch.asi)
  PASS: all 76 exact ABI hook bodies match the original trampolines
```

So phase-1's "all @exact hook bodies match, to their true ends and internal rets" is confirmed on
this revision (76 by the tool's current count; two are compiled-body INFOs: `abi_ooc_wrap`,
`abi_driveby_fov_shim`). The ASI itself builds clean and the ABI exports verify. Caveat: this is a
re-execution of the port's own verifier, not an independent disassembler comparison; my own spot
disassembly of sampled bodies (e.g. `abi_hqr_body` ref 0x1001F030) agrees.

## 4. Section C items

| item | status | evidence/ref |
|---|---|---|
| C1 suppressions 5-11 individually | DONE via handler owners | Episodic #1 (`0x1001b580` 8-byte row mis-split → the 13-byte fill; episodic §1a ops:45), NMS/ISH×4/SMPA60/SavegameFix (falsified as covered, §3.6), BuildingAlpha (not suppressed, §3.3/§3.5); InfiniteCarBottomDistance re-decoded EB/2A (offsets 3/4) and phase-1-verified |
| C2 menu/INI-section semantics | RESOLVED | §3.5 B1: [PopupLightAngles]/[HeadlightConeOffsets] 1:1; 0x101580CC is a popup-body store; AddOptionsInMenu via strtol + `[0x10158038] != 0` |
| C3 IVMenuAPI registration | RESOLVED + F27 | §3.5 B2: names/callbacks exactly match; ZMenuIV version guard missing (F27) |
| C4 native script-call hash 0x570F5725 | RESOLVED + F22 | §3.5 B3: = GET_PLAYER_NAME; read from mutable slot; port's resolver correct, one hardcode (F22) |
| C5 one-shot/reset for OOC, garage, CarDynamic | RESOLVED | §3.5 B4: registrars/callbacks/resets re-derived; port equivalent; car-reset omission documented + unobservable |
| C6 ZMenuIV.asi separate audit | BLOCKED (out of scope per handoff) | named artifact: ZMenuIV_reconstruction/ (not audited) |
| C7 CrashProbe/debug-only | RESOLVED (inert) | §3.5 B5 |
| C8 BuildingAlphaFix pattern-2 | RESOLVED (no divergence) | §3.5; AGENTS.md example stale |
| C9 post-fix re-verification (F1/F12/F13/F19/F8) | NOT PERFORMED — no fix landed | §5 state table; the scratch-fixed write log was used instead |

## 5. Phase-1 findings re-check (no fix landed during this audit)

| id | state at this revision | evidence |
|---|---|---|
| F1 | OPEN | `hook_bodies_win.cpp:3254-3255` still `mov esi,9399528h / mov ecx,9399528h`; a diagnostic (`abi_driveby_fov_note`, lines 3108-3224) was added that scans the resolver registry for the key |
| F2/F3 | OPEN | `config.cpp` unchanged (hash 796B17F3… = phase-1 hash); absent key still disabled, value != "0" accepted |
| F4 | OPEN | `recovered_feature_coverage.inc` still lists shipped-INI defaults (`BuildingDynamicShadows "0"`, `LaunchWithPlayGTAIV "1"`, `NoLoadingSleep "1"`, `VSyncFix "1"`) |
| F5 | OPEN | `recovered_hook_targets.inc` still maps `0x1001f030 → bike_physics_loop_body` under HighFPSBikePhysicsFix |
| F7 | OPEN | no `Patch7RemoveModChecks` in coverage/catalog |
| F8 | OPEN | current `build/native-win/zpatch-test.exe` still exits 0xC0000005; `zpatch_nms_alloc` still casts to `std::uint32_t` (`hook_bodies_win.cpp:5704+`) |
| F12 | OPEN (re-located, full bytes) | The original's gate is `sub_10003E20`: `GetModuleHandleW(L"GTAIV.EFLC.FusionFix.asi")` (UTF-16 at 0x10128B88, IAT 0x10063074), then `cmp dword [mod+0x20D3],0xC7C47589` / `cmp dword [mod+0x20D7],0x2446` → returns 1 when the module is present but its bytes differ. Called from exactly three arms — SkipIntro (`1005563F`), SkipMenu (`100556D1`), BorderlessWindowed (`10055836`) — where `[ebp-0xD1] = (probe==0)` gates the install. The port defines `compatibility_module_lacks_signature` (`game.cpp:148`) but nothing calls it. Note when wiring: the checked bytes are the **FusionFix module's**, not the game exe's. |
| F13 | OPEN (re-confirmed with gate bytes by §3.4/G5) | NewFilesCompatibility must be skipped when `GetModuleHandleA("GTAC.dll")` or `GetModuleHandleA("JacksIVFixes.asi")` is non-null (`10051939`/`10051948`); the port has no such gate |
| F14 | OPEN | `recovered_rva_jumps.inc` still ships the mispaired LoadDLCs rows (0x414274/0x4144D4 → 0x10023480 duplicates) |
| F15 | CLOSED (2026-10-14) | `model_catalog_name` preserves all 38455 table entries/order; `model_name_from_hash` performs the original first-match `game_hash` scan and returns the same `**UNKNOWN**` miss sentinel; extractor verifies against the pinned ASI |
| F17 | OPEN | moving-parts first-scan gate still unmodelled |
| F19 | OPEN (re-confirmed, full bytes, §3.4/G1) | Original writes 9 sites/build: −7, +3, +0xE0(EB), +0xE7, +0x16F, +0x179, +0x25F, +0x266, +0x2FC (1080 match 0x7CBFE9). Port ships `recovered_patch_ops.inc:63-70` (8 rows collapsed to −7) + `:71` (+224 EB); the live log writes −7 eight times + EB → 7 NOP sites missing on every build; +0x25F is a NOP in the original, not an EB |
| F21 | doc-only, docs not yet corrected | port row correct; `parity_and_improvements.md` §5 unchanged |
| F22 (new) | OPEN | see §2 |

## 6. Unverifiable / blockers

1. EFLC 1.1.0.0 — no exe (SecuROM); build-ordinal-10 rows unverifiable.
2. 1.0.6.1 — no exe (AGENTS.md says 1070 code).
3. 1.2.0.43 — only `GTAIV_12059.exe`; 12059 is unsupported by both sides, so no build-18 rows were exercised.
4. Live game run — not performed; all conclusions are static + the scratch x64 pipeline.
5. OOC payload decode; ZMenuIV.asi (separate audit, not attempted).

## Appendix A — reproduction commands (all scripts in `%TEMP%\kilo\audit_indep\`)

```
python p2_callsites.py        # enumerate every primitive call site (opcode search, 1031 refs)
python p2_decode3.py          # per-site pattern + effect decode -> p2_sites3.json
python p2_scan_all.py         # scan every decoded pattern on 11 exes -> p2_scan_matrix.json
python p2_inventory.py        # per-(site,effect) rows with per-build VA -> p2_inventory_rows.json
python p2_validate.py         # self-check: preimage byte == pattern byte at each store offset
python p2_diff2.py            # mechanism-aware per-byte diff vs the port log -> p2_diff2_rows.json
python p2_md_table.py         # Appendix B table
python p2_option_matrix.py    # per-option, per-build agree/delta matrix
# port write log (isolated scratch tree p2lead, F8 nms_alloc 64-bit fix only):
#   build/native-win/zpatch-test.exe > p2_all_writes.txt   (driver in tests/test_main.cpp;
#   the driver's INI = union of every port table's option names, all =1)
# body/ABI re-verification (pristine tree p2verify):
#   make msvc-asi        (builds 32-bit ZolikaPatch.asi + ZMenuIV.asi)
#   python tools/verify_asi_abi.py --candidate-dir build/msvc-x86
#   make verify-bodies   (tools/verify_hook_bodies.py)
# sub-audit scripts: p2sub/{episodic,lowregion,savegame,limits,misc}/*.py + report.md
```

## Appendix B — full scan-site inventory (per site/effect; builds: 1k=1000, 1k4=1004,
1k40=1040, 1k60=1060, 1k70=1070, 1k80=1080, e1110/e1120/e1130 = EFLC; Y=1 hit, digit=n hits,
-=no hit; `[char-built]` = pattern recovered from a character builder — verify before trusting)

| site | function | option(s) | pattern | kind | off | value | hits per build (1k 1k4 1k40 1k60 1k70 1k80 e1110 e1120 e1130) |
|---|---|---|---|---|---|---|---|
| 10004132 | sub_10004110 | - | 0C 83 88 94 00 00 00 04 | store | -10 | E9(1) | YYYYYYYYY |
| 10004656 | sub_10004620 | - | 89 46 3C 89 4E 40 E8 | store | 6 | E8(1) | -YYYYYYYY |
| 100046B7 | sub_10004620 | - | 0F 85 ? ? ? ? 68 03 01 00 00 68 ? ? ? ? 68 0 | store | -67 | E9(1) | -YYYYYYYY |
| 10004901 | sub_10004850 | - | E8 ? ? ? ? 83 C4 0C 83 F8 01 74 11 6A 00 6A  | store | 0 | E8(1) | ---YYYY-- |
| 1000495F | sub_10004850 | - | 55 8B EC 6A 00 E8 | store | 0 | E9(1) | -Y-YYYY-- |
| 100049B7 | sub_10004850 | - | 6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? | store | 0 | E9(1) | -Y-YYYY-- |
| 10004A0F | sub_10004850 | - | 6A 00 E8 ? ? ? ? 83 F8 01 59 75 17 FF 15 ? ? | store | 0 | E9(1) | -Y-YYYY-- |
| 10004A67 | sub_10004850 | - | 56 8B 74 24 0C 8D 46 04 50 FF 74 24 0C E8 ?  | store | 0 | E9(1) | -Y-YYYY-- |
| 10004ABF | sub_10004850 | - | FF 50 60 83 C4 14 39 75 F8 89 45 FC | store | -165 | E9(1) | ---YYYY-- |
| 10004ABF | sub_10004850 | - | FF 50 60 83 C4 14 39 75 F8 89 45 FC | store | -165 | E9(1) | ---YYYY-- |
| 10004ABF | sub_10004850 | - | FF 50 60 83 C4 14 39 75 F8 89 45 FC | store | -165 | E9(1) | ---YYYY-- |
| 10004B5F | sub_10004850 | - | 80 BC 24 ? ? ? ? ? 6A 00 | **no-effect-decoded** |  |  | ? |
| 10004BC6 | sub_10004850 | - | 6A 00 68 80 00 00 00 6A 03 6A 00 6A 01 68 00 | **no-effect-decoded** |  |  | ? |
| 10005234 | sub_10005210 | - | A1 ? ? ? ? 8B 0D ? ? ? ? 8B 15 ? ? ? ? A3 ?  | **no-effect-decoded** |  |  | ? |
| 1000524C | sub_10005210 | - | 32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0 | store | 2 | E9(1) | ---YYYYYY |
| 100052B5 | sub_10005210 | - | 7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2 | store | 2 | E9(1) | YYY------ |
| 10005522 | sub_10005500 | - | 2B C1 1B D6 89 44 24 0C 89 54 24 10 DF 6C 24 | store | 0 | E9(1) | YYYYYYYYY |
| 100055E2 | sub_100055C0 | AimProjectileCrashFix | 8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 | store | 0 | E9(1) | YYYYYYYYY |
| 10005822 | sub_10005800 | BetterMPSync | 8B 90 E8 00 00 00 55 8B CE FF | store | 6 | E8(1) | YYYYYYYYY |
| 10005BAD | sub_10005B20 | BuildingDynamicShadows | 8B 47 28 C1 E8 06 83 E0 0F 83 C0 | store | 0 | E9(1) | YYYYYYYYY |
| 10005C1F | sub_10005B20 | BuildingDynamicShadows | 50 68 04 01 00 00 6A 02 6A 00 E8 | store | 2 | 00(1) | YYYYYYYYY |
| 10005CC4 | sub_10005CA0 | BuildingAlphaFix | F7 46 24 00 00 00 40 0F 85 | store | 0 | 90909090(4) | YYYYYYYYY |
| 10005CC4 | sub_10005CA0 | BuildingAlphaFix | F7 46 24 00 00 00 40 0F 85 | store | 4 | 90909090(4) | YYYYYYYYY |
| 10005CC4 | sub_10005CA0 | BuildingAlphaFix | F7 46 24 00 00 00 40 0F 85 | store | 8 | 90909090(4) | YYYYYYYYY |
| 10005CC4 | sub_10005CA0 | BuildingAlphaFix | F7 46 24 00 00 00 40 0F 85 | store | 12 | 90(1) | YYYYYYYYY |
| 10005D0F | sub_10005CA0 | BuildingAlphaFix | F7 46 24 00 00 00 40 0F 84 | store | 7 | E9(1) | YYYYYYYYY |
| 10005E52 | sub_10005E30 | CollisionDeadlockFix | 55 8B EC 83 E4 F8 51 8B 45 0C 0F B7 48 66 | store | 0 | E9(1) | YYYYYYYYY |
| 10005F02 | sub_10005EE0 | CombinedRadioFreezeFix | E8 ? ? ? ? 8B 35 ? ? ? ? 3B F0 | store | 0 | E8(1) | YYYYYYYYY |
| 10006372 | sub_10006350 | CopSirensFlag | E8 ? ? ? ? C0 E0 ? 32 86 6F 0F 00 | store | 0 | E9(1) | YYYYYYYYY |
| 100063DB | sub_10006350 | CopSirensFlag | 0F BF 41 2E 39 05 | store | 22 | E9(1) | YYYYYYYYY |
| 10006792 | sub_10006770 | DualVehicleHeadlights | 8B 17 8B 42 64 8B CF FF D0 8B 54 | store | 0 | E9(1) | YYYYYYYYY |
| 10007D85 | sub_10006910 | ?? |  | **runtime-pattern** |  |  | ? |
| 1000A4C1 | sub_10007E40 | - |  | **runtime-pattern** |  |  | ? |
| 1000B749 | sub_10007E40 | - |  | **runtime-pattern** |  |  | ? |
| 1000C69F | sub_1000B790 | - |  | **runtime-pattern** |  |  | ? |
| 1000D596 | sub_1000B790 | - |  | **runtime-pattern** |  |  | ? |
| 1000E7A1 | sub_1000B790 | - |  | **runtime-pattern** |  |  | ? |
| 1000E894 | sub_1000E870 | - | 8D 4C 24 50 51 68 04 05 00 00 | store | 6 | 04(1) | YYYYYYYYY |
| 1000E894 | sub_1000E870 | - | 8D 4C 24 50 51 68 04 05 00 00 | store | 13 | 05(1) | YYYYYYYYY |
| 1000E8EE | sub_1000E870 | - | 8D 44 24 50 50 68 04 01 00 00 | store | 6 | 04(1) | YYYYYYYYY |
| 1000E8EE | sub_1000E870 | - | 8D 44 24 50 50 68 04 01 00 00 | store | 13 | 01(1) | YYYYYYYYY |
| 1000E942 | sub_1000E870 | - | F3 0F 11 44 24 1C 0F 84 9E 00 00 00 | store | 6 | 0F(1) | YYYYYYYYY |
| 1000E942 | sub_1000E870 | - | F3 0F 11 44 24 1C 0F 84 9E 00 00 00 | store | 7 | 84(1) | YYYYYYYYY |
| 1000E942 | sub_1000E870 | - | F3 0F 11 44 24 1C 0F 84 9E 00 00 00 | store | 8 | 9E(1) | YYYYYYYYY |
| 1000E942 | sub_1000E870 | - | F3 0F 11 44 24 1C 0F 84 9E 00 00 00 | store | 9 | 00(1) | YYYYYYYYY |
| 1000E942 | sub_1000E870 | - | F3 0F 11 44 24 1C 0F 84 9E 00 00 00 | store | 10 | 00(1) | YYYYYYYYY |
| 10010AD3 | sub_1000EA00 | - |  | **runtime-pattern** |  |  | ? |
| 10014F74 | sub_10010B60 | - |  | **runtime-pattern** |  |  | ? |
| 100162A1 | sub_10010B60 | - |  | **runtime-pattern** |  |  | ? |
| 10017779 | sub_10010B60 | - |  | **runtime-pattern** |  |  | ? |
| 10019039 | sub_10010B60 | - |  | **runtime-pattern** |  |  | ? |
| 1001ADF5 | sub_1001ADD0 | ForceCarHeadlightShadows,ForceCarHeadlightShadows | 3C 01 75 50 84 DB 75 79 | store | 6 | EB(1) | YYYYYYYYY |
| 1001ADF5 | sub_1001ADD0 | ForceCarHeadlightShadows,ForceCarHeadlightShadows | 3C 01 75 50 84 DB 75 79 | store | 95 | E9(1) | YYYYYYYYY |
| 1001ADF5 | sub_1001ADD0 | ForceCarHeadlightShadows,ForceCarHeadlightShadows | 3C 01 75 50 84 DB 75 79 | store | 379 | EB(1) | YYYYYYYYY |
| 1001ADF5 | sub_1001ADD0 | ForceCarHeadlightShadows,ForceCarHeadlightShadows | 3C 01 75 50 84 DB 75 79 | store | 211 | 101580C0(4) | YYYYYYYYY |
| 1001AEB4 | sub_1001ADD0 | ForceCarHeadlightShadows,ForceCarHeadlightShadows | 55 8B EC 83 E4 F0 81 EC D4 00 00 00 8B 45 20 | store | 0 | E9(1) | YYYYYYYYY |
| 1001AFC2 | sub_1001AFA0 | ??,??,EmissiveLerpFix | F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1 | store | -24 | E9(1) | YYYYYYYYY |
| 1001B102 | sub_1001B0E0 | EpisodeSwitchFreezeFix | 89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B | store | 0 | E9(1) | YYYYYYYYY |
| 1001B16F | sub_1001B0E0 | EpisodeSwitchFreezeFix | 8B 4B 10 3B 4B 14 7D 0F | store | 0 | E9(1) | YYYYYYYYY |
| 1001B1CE | sub_1001B0E0 | EpisodeSwitchFreezeFix | 8B 41 10 03 41 0C C3 | store | 0 | E9(1) | YYYYYYYYY |
| 1001B224 | sub_1001B0E0 | EpisodeSwitchFreezeFix | 8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 | store | 0 | E9(1) | YYYYYYYYY |
| 1001B27A | sub_1001B0E0 | EpisodeSwitchFreezeFix | 56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05 | store | 0 | E9(1) | YYYYYYYYY |
| 1001B339 | sub_1001B310 | TBoGTHeliHeightLimit | 83 3D ? ? ? ? 02 7C 0A F3 0F | store | 7 | 90909090(2) | ---YYYYYY |
| 1001B3B1 | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 0F 8C D5 00 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B3B1 | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 0F 8C D5 00 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B3B1 | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 0F 8C D5 00 | store | 8 | 90909090(4) | ---YYYYYY |
| 1001B3B1 | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 0F 8C D5 00 | store | 12 | 90(1) | ---YYYYYY |
| 1001B3FC | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 7C 32 8B 41 | store | 7 | 90909090(2) | ---YYYYYY |
| 1001B432 | sub_1001B380 | EpisodicWeaponSupport | 75 5B 83 3D ? ? ? ? 00 75 | store | 0 | 90909090(2) | ---YYYYYY |
| 1001B467 | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C | store | 7 | 90909090(2) | ---YYYYYY |
| 1001B49D | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 75 50 | store | 7 | 90909090(2) | Y--YYYYYY |
| 1001B4D3 | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 0F 85 8C 01 00 00 | store | 7 | 90909090(4) | ---YYYYYY |
| 1001B4D3 | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 0F 85 8C 01 00 00 | store | 11 | 90909090(2) | ---YYYYYY |
| 1001B50C | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 75 20 8B CD E8 | store | 7 | 90909090(2) | ---YYYYYY |
| 1001B542 | sub_1001B380 | EpisodicWeaponSupport | 83 3D ? ? ? ? 02 7C 21 8B 44 24 0C | store | 7 | 90909090(2) | ---YYYYYY |
| 1001B5C9 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 0F 8C 0E 05 00 00 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B5C9 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 0F 8C 0E 05 00 00 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B5C9 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 0F 8C 0E 05 00 00 | store | 8 | 90909090(4) | ---YYYYYY |
| 1001B5C9 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 0F 8C 0E 05 00 00 | store | 12 | 90(1) | ---YYYYYY |
| 1001B614 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 35 0F BF 43 2E | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B614 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 35 0F BF 43 2E | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B614 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 35 0F BF 43 2E | store | 8 | 90(1) | ---YYYYYY |
| 1001B652 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 8A 0D | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B652 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 8A 0D | store | 4 | 90909090(2) | ---YYYYYY |
| 1001B652 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 8A 0D | store | 17 | 90909090(2) | ---YYYYYY |
| 1001B6AF | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B6AF | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B6AF | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00 | store | 8 | 90(1) | ---YYYYYY |
| 1001B6ED | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B6ED | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B6ED | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E | store | 8 | 90909090(4) | ---YYYYYY |
| 1001B6ED | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E | store | 12 | 90(1) | ---YYYYYY |
| 1001B6ED | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E | store | -6 | 90909090(4) | ---YYYYYY |
| 1001B6ED | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E | store | -2 | 90909090(4) | ---YYYYYY |
| 1001B6ED | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E | store | -12 | 90909090(4) | ---YYYYYY |
| 1001B6ED | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E | store | -8 | 90909090(4) | ---YYYYYY |
| 1001B7C4 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 08 3B 05 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B7C4 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 08 3B 05 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B7C4 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 08 3B 05 | store | 8 | 90(1) | ---YYYYYY |
| 1001B802 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 1F 8B 44 24 04 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B802 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 1F 8B 44 24 04 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B802 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 1F 8B 44 24 04 | store | 8 | 90(1) | ---YYYYYY |
| 1001B840 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 38 8B 16 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B840 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 38 8B 16 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B840 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 38 8B 16 | store | 8 | 90(1) | ---YYYYYY |
| 1001B87E | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B87E | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B87E | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04 | store | 8 | 90(1) | ---YYYYYY |
| 1001B8BC | sub_1001B580 | EpisodicVehicleSupport | 74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05 | store | 2 | 90909090(4) | ---YYYYYY |
| 1001B8BC | sub_1001B580 | EpisodicVehicleSupport | 74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05 | store | 6 | 90909090(4) | ---YYYYYY |
| 1001B8BC | sub_1001B580 | EpisodicVehicleSupport | 74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05 | store | 10 | 90(1) | ---YYYYYY |
| 1001B8FB | sub_1001B580 | EpisodicVehicleSupport | 8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18 | store | -15 | 90909090(4) | ---YYYYYY |
| 1001B8FB | sub_1001B580 | EpisodicVehicleSupport | 8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18 | store | -11 | 90909090(2) | ---YYYYYY |
| 1001B8FB | sub_1001B580 | EpisodicVehicleSupport | 8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18 | store | -5 | 90909090(2) | ---YYYYYY |
| 1001B95F | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 35 0F BF 46 2E | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B95F | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 35 0F BF 46 2E | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B95F | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 35 0F BF 46 2E | store | 8 | 90(1) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 8 | 90(1) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 8 | 90(1) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 0 | 90909090(4) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 4 | 90909090(4) | ---YYYYYY |
| 1001B99D | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00 | store | 8 | 90(1) | ---YYYYYY |
| 1001BA9E | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C | store | 0 | 90909090(4) | ---YYYYYY |
| 1001BA9E | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C | store | 4 | 90909090(4) | ---YYYYYY |
| 1001BA9E | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C | store | 8 | 90(1) | ---YYYYYY |
| 1001BADC | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 1 | store | 21 | 90909090(2) | ---YYYYYY |
| 1001BB12 | sub_1001B580 | EpisodicVehicleSupport | 7C 22 83 F8 28 75 1D | store | 0 | 90909090(2) | ---YYYYYY |
| 1001BB47 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 89 44 24 38 | store | 11 | 90909090(4) | ---YYYYYY |
| 1001BB47 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 89 44 24 38 | store | 15 | 90909090(2) | ---YYYYYY |
| 1001BB80 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 43 | store | 7 | 90909090(2) | ---YYYYYY |
| 1001BBB6 | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 1F 8B | store | 7 | 90909090(2) | ---YYYYYY |
| 1001BBEC | sub_1001B580 | EpisodicVehicleSupport | 83 3D ? ? ? ? 02 7C 11 8B 4E | store | 7 | 90909090(2) | ---YYYYYY |
| 1001BC22 | sub_1001B580 | EpisodicVehicleSupport | 39 35 ? ? ? ? 75 0E 80 3D | store | 6 | 90909090(2) | ---YYYYYY |
| 1001BC22 | sub_1001B580 | EpisodicVehicleSupport | 39 35 ? ? ? ? 75 0E 80 3D | store | 136454 | 10158128(4) | ---YYYYYY |
| 1001BC22 | sub_1001B580 | EpisodicVehicleSupport | 39 35 ? ? ? ? 75 0E 80 3D | store | 8365748 | 10158128(4) | ---YYYYYY |
| 1001BC22 | sub_1001B580 | EpisodicVehicleSupport | 39 35 ? ? ? ? 75 0E 80 3D | store | 8976116 | 10158128(4) | ---YYYYYY |
| 1001BC22 | sub_1001B580 | EpisodicVehicleSupport | 39 35 ? ? ? ? 75 0E 80 3D | store | 15810994 | 10158128(4) | ---YYYYYY |
| 1001BD80 | sub_1001BD60 | BikePhoneAnimsFix | 80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE | store | 33 | 90909090(4) | -Y-YYYYYY |
| 1001BD80 | sub_1001BD60 | BikePhoneAnimsFix | 80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE | store | 37 | 90909090(2) | -Y-YYYYYY |
| 1001BDF0 | sub_1001BDD0 | BikeFeetFix | 39 82 54 13 00 00 0F 85 94 04 00 00 | store | 12 | 90909090(4) | -Y-YYYYYY |
| 1001BDF0 | sub_1001BDD0 | BikeFeetFix | 39 82 54 13 00 00 0F 85 94 04 00 00 | store | 16 | 90909090(4) | -Y-YYYYYY |
| 1001BDF0 | sub_1001BDD0 | BikeFeetFix | 39 82 54 13 00 00 0F 85 94 04 00 00 | store | 20 | 90909090(4) | -Y-YYYYYY |
| 1001BE69 | sub_1001BE40 | PipeBombDropIconSupport | 83 7D 18 1C 75 14 | store | -9 | 90909090(4) | ---YYYYYY |
| 1001BE69 | sub_1001BE40 | PipeBombDropIconSupport | 83 7D 18 1C 75 14 | store | -5 | 90909090(4) | ---YYYYYY |
| 1001BE69 | sub_1001BE40 | PipeBombDropIconSupport | 83 7D 18 1C 75 14 | store | -1 | 90(1) | ---YYYYYY |
| 1001BEE9 | sub_1001BEC0 | PoliceEpisodicWeaponSupport | 39 2D ? ? ? ? 75 3E | store | 6 | 90909090(2) | ---YYYYYY |
| 1001EE05 | sub_1001EDE0 | - | 6A 0A 56 50 FF D2 A1 | store | 1 | 06(1) | YYYYYYYYY |
| 1001EE72 | sub_1001EE50 | ForceDynamicShadowsEverywhere | F7 46 4C 00 80 00 00 74 | store | 7 | 90(1) | YYYYYYYYY |
| 1001EE72 | sub_1001EE50 | ForceDynamicShadowsEverywhere | F7 46 4C 00 80 00 00 74 | store | 8 | 90(1) | YYYYYYYYY |
| 1001EEF0 | sub_1001EED0 | ForceShadowsOnObjects | F7 47 40 00 10 00 00 74 07 81 4E 24 00 00 | store | 7 | EB(1) | YYYYYYYYY |
| 1001EFC2 | sub_1001EFA0 | HighFPSBikePhysicsFix | D9 45 08 51 8B CE D9 1C 24 E8 ? ? ? ? 8A 86  | store | 0 | E9(1) | YYYYYYYYY |
| 10022D12 | sub_10022CF0 | AllowHashesForIDE | 8B 4C 24 08 56 8B 74 24 08 80 3E 22 | store | 0 | E9(1) | YYYYYYYYY |
| 10022DA2 | sub_10022D80 | InfiniteCarBottomDistance | 0F 2F D0 76 25 0F 28 C2 | store | 3 | EB(1) | YYYYYYYYY |
| 10022DA2 | sub_10022D80 | InfiniteCarBottomDistance | 0F 2F D0 76 25 0F 28 C2 | store | 4 | 2A(1) | YYYYYYYYY |
| 10022F80 | sub_10022F60 | OverrideGTADatPath | 68 ? ? ? ? E8 ? ? ? ? 83 C4 04 E8 ? ? ? ? E8 | store | 1 | 10158068(4) | 422YYYYYY |
| 10022FE0 | sub_10022FC0 | OverrideGTADatPathTo,OverrideGTADatPathTo,IncreaseVehicleModelLimit | 83 C7 01 81 FF 18 79 00 00 7C D2 | **no-effect-decoded** |  |  | ? |
| 10023040 | sub_10023020 | IncreaseVehicleStructLimit | 68 ? ? ? ? 6A 32 8B C8 | store | 6 | 64(1) | YYYYYYYYY |
| 100230A4 | sub_10023080 | IncreasePtrNodes | 6A 14 ? ? ? ? ? 68 80 3E 00 00 | store | 8 | 0000FA00(4) | YYYYYYYYY |
| 100230DE | sub_10023080 | IncreasePtrNodes | 6A 08 ? ? ? ? ? 68 80 38 01 00 | store | 8 | 0004E200(4) | YYYYYYYYY |
| 10023112 | sub_10023080 | IncreasePtrNodes | 6A 10 ? ? ? ? ? 68 80 3E 00 00 | store | 8 | 0000FA00(4) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | 3 | 90909090(2) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | -7 | 90909090(2) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | 224 | EB(1) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | 231 | 90909090(2) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | 367 | 90909090(2) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | 377 | 90909090(2) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | 607 | 90909090(2) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | 614 | 90909090(2) | YYYYYYYYY |
| 10023B52 | sub_10023B30 | HighFPSSpeedupFix | 0F 2F C1 76 03 0F 28 C8 F3 0F 10 15 | store | 764 | 90909090(2) | YYYYYYYYY |
| 10023E01 | sub_10023DD0 | MiscFixes | C7 06 ? ? ? ? 74 19 | store | 6 | EB(1) | --Y------ |
| 10023E38 | sub_10023DD0 | MiscFixes | 83 C4 ? 3B 46 20 | store | 12 | EB(1) | -Y-YYYYYY |
| 10023E69 | sub_10023DD0 | MiscFixes | 75 28 6A 00 6A 00 68 ? ? ? ? E8 ? ? ? ? 83 C | store | 0 | EB(1) | ---222222 |
| 10023E99 | sub_10023DD0 | MiscFixes | 8B 54 24 04 52 A3 | store | 0 | C9EB(2) | ---YY---- |
| 10023ECE | sub_10023DD0 | MiscFixes | 83 F8 01 75 30 A3 | store | 0 | B8(1) | ---YY---- |
| 10023F1C | sub_10023DD0 | MiscFixes | C7 05 ? ? ? ? 03 00 00 00 33 C0 5E 59 C3 C7 | store | 0 | 0DEB(2) | ---YYYYYY |
| 10023F51 | sub_10023DD0 | MiscFixes | FF 15 ? ? ? ? 85 C0 75 18 | store | 8 | 90909090(2) | Y-------- |
| 10023F87 | sub_10023DD0 | MiscFixes | FF 15 ? ? ? ? 85 C0 75 04 32 | store | 8 | 90909090(2) | -YY------ |
| 10023FBD | sub_10023DD0 | MiscFixes | FF 15 ? ? ? ? 85 C0 75 07 A0 | store | 8 | 90909090(2) | ---YYYYYY |
| 10023FF3 | sub_10023DD0 | MiscFixes | 81 EC ? ? ? ? A1 ? ? ? ? 33 C4 89 84 24 ? ?  | store | 0 | C3(1) | -YYYYYYYY |
| 10024028 | sub_10023DD0 | MiscFixes | 74 05 E8 ? ? ? ? 6A 05 E8 ? ? ? ? 83 C4 04 B | store | 0 | EB(1) | YYYYYYYYY |
| 10027C2E | sub_10024600 | - |  | **runtime-pattern** |  |  | ? |
| 1002976E | sub_10024600 | - |  | **runtime-pattern** |  |  | ? |
| 1002B6EE | sub_10024600 | - |  | **runtime-pattern** |  |  | ? |
| 1002D9B2 | sub_10024600 | - |  | **runtime-pattern** |  |  | ? |
| 1002F442 | sub_10024600 | - |  | **runtime-pattern** |  |  | ? |
| 10033CD2 | sub_10024600 | - |  | **runtime-pattern** |  |  | ? |
| 10035D62 | sub_10024600 | - |  | **runtime-pattern** |  |  | ? |
| 10036E30 | sub_10024600 | - |  | **runtime-pattern** |  |  | ? |
| 10036F52 | sub_10036F30 | GroupHackFix | 33 C0 83 C1 04 8D 50 07 83 39 00 75 06 83 79 | store | 0 | E9(1) | YYYYYYYYY |
| 10036FF2 | sub_10036FD0 | BlackscreenFix | 75 54 8B 48 28 8B 40 2C | store | 81 | E9(1) | Y-YYYY--Y |
| 10037092 | sub_10037070 | FreezeGunFix | 2B C2 D1 F8 39 3C C6 7D 0D 83 CA FF | store | -21 | E9(1) | YYYYYYYYY |
| 10037120 | sub_10037100 | RemoveAllWeaponsProtection | E9 ? ? ? ? 57 8D 8F ? ? 00 00 | store | 5 | 90909090(4) | YYYYYYYYY |
| 10037120 | sub_10037100 | RemoveAllWeaponsProtection | E9 ? ? ? ? 57 8D 8F ? ? 00 00 | store | 9 | 90909090(4) | YYYYYYYYY |
| 10037120 | sub_10037100 | RemoveAllWeaponsProtection | E9 ? ? ? ? 57 8D 8F ? ? 00 00 | store | 13 | 90909090(4) | YYYYYYYYY |
| 10037191 | sub_10037170 | TeleportProtection | 55 8B EC 83 E4 F8 51 53 8B 5D 08 56 57 6A 08 | store | 0 | C2(1) | YYYYYYYYY |
| 10037212 | sub_100371F0 | CrashMsgFix | 68 6A 10 4E 31 | **no-effect-decoded** |  |  | ? |
| 100372B0 | sub_10037290 | MPNikoCrashFix | 74 2B 57 E8 ? ? ? ? 8B BE A8 0E 00 00 | store | 0 | EB(1) | ---YYYYYY |
| 10037C02 | sub_10037BE0 | BadPlayerModelCrashFix | 83 E4 F0 83 EC 54 0F 57 C0 53 8B 5D 0C | store | 0 | E9(1) | YYYYYYYYY |
| 10037C92 | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | 32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0 | store | 7 | E8(1) | ---YYYYYY |
| 10037D05 | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | 8B 4C 24 28 8B 54 24 24 50 51 52 E8 ? ? ? ?  | store | 11 | E8(1) | YYYYYYYYY |
| 10037D6E | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00 | store | 246 | E8(1) | YYYYYYYYY |
| 10037D6E | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00 | store | 426 | E8(1) | YYYYYYYYY |
| 100380FA | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | 88 87 40 01 00 00 E8 | store | 6 | E8(1) | ---YYYYYY |
| 10038155 | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | 88 5C 24 1F 8B 54 24 28 52 E8 | store | 9 | E8(1) | ---YYYYYY |
| 10038155 | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | 88 5C 24 1F 8B 54 24 28 52 E8 | store | 263 | E8(1) | ---YYYYYY |
| 10038200 | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | 38 98 8C 00 00 00 75 2D 57 E8 | store | 9 | E8(1) | ---YYYYYY |
| 1003825B | sub_10037C70 | FreezeCarFix,DebugPrintSpawns | 32 DB 8B 54 24 24 52 E8 | store | 7 | E8(1) | -Y-2YY2YY |
| 100386A2 | sub_10038680 | - | 3D 80 99 99 59 73 05 | store | -34 | E8(1) | -YYYYYYYY |
| 100386A2 | sub_10038680 | - | 3D 80 99 99 59 73 05 | store | -62 | E9(1) | -YYYYYYYY |
| 10038792 | sub_10038770 | RemoveBoundingBoxCulling | 3C 01 57 0F 85 | store | 3 | E9(1) | YYYYYYYYY |
| 10038820 | sub_10038800 | RemoveStaticCarShadows | F3 0F 11 4C 24 0C 76 5B D9 45 14 | store | 6 | EB(1) | YYYYYYYYY |
| 10038880 | sub_10038860 | RemoveStaticCutsceneCarShadows | F3 0F 11 44 24 0C 76 5B D9 45 14 | store | 6 | EB(1) | YYYYYYYYY |
| 1003D444 | sub_1003D420 | NoLiveryLimit | 74 52 8D B5 3C 01 00 00 83 FB 04 | store | 2 | E9(1) | -Y-YYYYYY |
| 1003D4C4 | sub_1003D420 | NoLiveryLimit | 8B 8C 83 3C 01 00 00 0F BF 53 48 | **no-effect-decoded** |  |  | ? |
| 1003D4EC | sub_1003D420 | NoLiveryLimit | 8D AF 3C 01 00 00 8D 54 24 20 | store | 0 | E9(1) | Y-Y------ |
| 1003D567 | sub_1003D420 | NoLiveryLimit | 8B 8C 83 3C 01 00 00 0F BF 53 48 | store | 0 | E9(1) | YYYYYYYYY |
| 1003D567 | sub_1003D420 | NoLiveryLimit | 8B 8C 83 3C 01 00 00 0F BF 53 48 | store | 4294967097 | E9(1) | YYYYYYYYY |
| 1003D567 | sub_1003D420 | NoLiveryLimit | 8B 8C 83 3C 01 00 00 0F BF 53 48 | store | 8589934156 | E9(1) | YYYYYYYYY |
| 1003D690 | sub_1003D670 | NoLoadingSleep | 68 88 13 00 00 FF 15 | store | 1 | 00000000(4) | -YYYYYYYY |
| 1003D7F2 | sub_1003D7D0 | PathNodeCrashFix | 8B 04 B1 0F B7 D0 81 FA FF FF 00 00 | store | 53 | E9(1) | YYYYYYYYY |
| 1003D8B2 | sub_1003D890 | PlaneCrashFix | 83 C4 04 88 58 26 8B 16 8B 82 F4 00 00 00 | store | 3 | E9(1) | YYYYYYYYY |
| 1003D9E2 | sub_1003D9C0 | PlayerAsPedComponentsFix | 8B 54 24 10 52 0F B6 90 19 02 00 00 | store | 5 | E9(1) | YYYYYYYYY |
| 100445B4 | sub_10044590 | CopSirensFlag | 0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1 | store | 82 | 101580CC(4) | ---YYYYYY |
| 100445B4 | sub_10044590 | CopSirensFlag | 0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1 | store | 10 | EB(1) | ---YYYYYY |
| 100445B4 | sub_10044590 | CopSirensFlag | 0F BF 46 2E 3B 05 ? ? ? ? 75 ? A1 | store | 0 | E9(1) | ---YYYYYY |
| 10044651 | sub_10044590 | CopSirensFlag | 0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1 | store | 82 | 101580CC(4) | YYY------ |
| 10044651 | sub_10044590 | CopSirensFlag | 0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1 | store | 10 | EB(1) | YYY------ |
| 10044651 | sub_10044590 | CopSirensFlag | 0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1 | store | 0 | E9(1) | YYY------ |
| 10045450 | sub_10045430 | ReversingLightFix | 11 01 00 00 8B 16 8B 42 64 8B CE FF D0 F3 0F | store | 8 | 60(1) | YYYYYYYYY |
| 100454B2 | sub_10045490 | RemoveRGSCLoginRequirement | 83 EC 44 A1 ? ? ? ? 33 C4 89 44 24 40 53 8B  | store | 0 | E9(1) | YYYYY-YY- |
| 10045632 | sub_10045610 | SavegameFix | 75 ? 83 C1 01 83 C0 01 83 F9 04 | store | 0 | 90909090(2) | YYYYYYYYY |
| 10045632 | sub_10045610 | SavegameFix | 75 ? 83 C1 01 83 C0 01 83 F9 04 | store | -73 | E8(1) | YYYYYYYYY |
| 10045632 | sub_10045610 | SavegameFix | 75 ? 83 C1 01 83 C0 01 83 F9 04 | store | 4294967080 | E8(1) | YYYYYYYYY |
| 10045632 | sub_10045610 | SavegameFix | 75 ? 83 C1 01 83 C0 01 83 F9 04 | store | 8589934200 | E8(1) | YYYYYYYYY |
| 10045632 | sub_10045610 | SavegameFix | 75 ? 83 C1 01 83 C0 01 83 F9 04 | store | 12884901216 | E8(1) | YYYYYYYYY |
| 10045632 | sub_10045610 | SavegameFix | 75 ? 83 C1 01 83 C0 01 83 F9 04 | store | 17179868272 | E8(1) | YYYYYYYYY |
| 100457ED | sub_10045610 | SavegameFix | 8D 4C 24 18 51 6A 00 6A 0C 57 | store | -86 | E8(1) | YYYYYYYYY |
| 100457ED | sub_10045610 | SavegameFix | 8D 4C 24 18 51 6A 00 6A 0C 57 | store | -133 | E8(1) | YYYYYYYYY |
| 100457ED | sub_10045610 | SavegameFix | 8D 4C 24 18 51 6A 00 6A 0C 57 | store | -123 | E8(1) | YYYYYYYYY |
| 100457ED | sub_10045610 | SavegameFix | 8D 4C 24 18 51 6A 00 6A 0C 57 | store | -33 | E8(1) | YYYYYYYYY |
| 100457ED | sub_10045610 | SavegameFix | 8D 4C 24 18 51 6A 00 6A 0C 57 | store | 95 | E8(1) | YYYYYYYYY |
| 100459A0 | sub_10045980 | SCOSignatureFix | 8B 6C 24 1C 66 89 6E 14 64 8B 0D 2C 00 00 00 | store | -32 | EB(1) | YYYYYYYYY |
| 10045AF2 | sub_10045AD0 | ImprovedShaderStreaming | 33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF | store | 85 | E8(1) | YYYYYYYYY |
| 10045AF2 | sub_10045AD0 | ImprovedShaderStreaming | 33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF | store | 183 | E9(1) | YYYYYYYYY |
| 10045AF2 | sub_10045AD0 | ImprovedShaderStreaming | 33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF | store | 338 | E8(1) | YYYYYYYYY |
| 10045AF2 | sub_10045AD0 | ImprovedShaderStreaming | 33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF | store | 605 | E9(1) | YYYYYYYYY |
| 10045C72 | sub_10045C50 | SkipIntro | 89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50 | store | -6 | E9(1) | YYYYYYYYY |
| 10045D3C | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1 | store | 34 | EB(1) | -YY------ |
| 10045D3C | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1 | store | 257 | EB(1) | -YY------ |
| 10045D3C | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1 | store | 257 | 90909090(2) | -YY------ |
| 10045D3C | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 84 DB 0F 85 43 01 00 00 8D 4C 24 28 8B C1 | store | 262 | 90909090(2) | -YY------ |
| 10045D9F | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 68 ? ? ? ? 89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 | **no-effect-decoded** |  |  | ? |
| 10045DBC | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 74 EC 38 5E 06 74 E7 68 | **no-effect-decoded** |  |  | ? |
| 10045E1D | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 74 C4 8B 07 3B 46 18 75 BD 68 | **no-effect-decoded** |  |  | ? |
| 10045E56 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 3B 56 18 0F 85 | store | 3 | 90909090(4) | --Y------ |
| 10045E56 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 3B 56 18 0F 85 | store | 7 | 90909090(2) | --Y------ |
| 10045E95 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 3B 4E 18 0F 85 | store | 3 | 90909090(4) | -Y------- |
| 10045E95 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 3B 4E 18 0F 85 | store | 7 | 90909090(2) | -Y------- |
| 10045ED4 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 0F 85 ? ? ? ? 8B 8C 24 ? ? ? ? 5F 5E 5B 33 C | **no-effect-decoded** |  |  | ? |
| 10045F10 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 3B 46 18 75 BD | store | 3 | C033(2) | --Y------ |
| 10045F10 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 3B 46 18 75 BD | store | 13 | A390(2) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 1 | 90C301B0(4) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 1 | 90C301B0(4) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 1 | 90C301B0(4) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 1 | 90C301B0(4) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 1 | C033(2) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 3 | E9(1) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 4 | C033(2) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 6 | 90909090(4) | --Y------ |
| 10045F74 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 57 51 C7 44 24 ? ? ? ? ? C7 44 24 ? ? ? ? ?  | store | 6 | 90909090(2) | --Y------ |
| 1004614A | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 56 1C 3B 56 20 ? ? 6A 00 6A 00 | **no-effect-decoded** |  |  | ? |
| 100461E0 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 89 35 ? ? ? ? E8 ? ? ? ? 83 C4 04 84 C0 | **no-effect-decoded** |  |  | ? |
| 10046219 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 50 2C 3B 50 30 | **no-effect-decoded** |  |  | ? |
| 10046278 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 48 2C 83 EC 78 | **no-effect-decoded** |  |  | ? |
| 100462E5 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 48 2C 83 EC 74 | **no-effect-decoded** |  |  | ? |
| 10046352 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 48 2C 3B 48 30 74 03 | **no-effect-decoded** |  |  | ? |
| 10046394 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 48 2C 3B 48 30 53 | **no-effect-decoded** |  |  | ? |
| 100463D3 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 48 2C 3B 48 30 74 11 | **no-effect-decoded** |  |  | ? |
| 10046417 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 48 2C 3B 48 30 75 DC 8D 54 24 18 | **no-effect-decoded** |  |  | ? |
| 10046455 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 48 2C 3B 48 30 75 DC 8D 54 24 1C | **no-effect-decoded** |  |  | ? |
| 10046493 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 8B 0D ? ? ? ? 8B 51 2C | **no-effect-decoded** |  |  | ? |
| 100464D5 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 51 53 55 56 57 8B 3D ? ? ? ? 8B 47 2C | **no-effect-decoded** |  |  | ? |
| 10046509 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 3B C7 0F 85 ? ? ? ? 85 C0 | **no-effect-decoded** |  |  | ? |
| 10046544 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 0F 85 ? ? ? ? 85 C0 0F 84 ? ? ? ? E8 ? ? ? ? | **no-effect-decoded** |  |  | ? |
| 10046586 | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 83 EC 24 A1 ? ? ? ? 8B 48 14 53 55 56 57 | **no-effect-decoded** |  |  | ? |
| 100465BD | ___std_stacktrace_to_string@16 | Patch4RemoveModChecks,RemoveGFWLUpdatePopup,SkipLauncher | 83 EC 08 8B 0D ? ? ? ? 8B 51 14 8D 04 24 50  | store | 0 | 90C3C033(4) | ---YY-YY- |
| 10046664 | sub_10046640 | SkipMenu | 83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84  | store | 23 | E8(1) | -YYYYYYYY |
| 100466C7 | sub_10046640 | SkipMenu | 6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5 | **no-effect-decoded** |  |  | ? |
| 100466DA | sub_10046640 | SkipMenu | 6A 00 E8 ? ? ? ? 83 C4 04 8B 8C 24 ? ? ? ? 5 | store | 2 | E8(1) | --------- |
| 100467C2 | sub_100467A0 | SMPA60Fix | 01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C | store | 123 | E9(1) | -YYYYYYYY |
| 100467C2 | sub_100467A0 | SMPA60Fix | 01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C | store | 86 | EB(1) | -YYYYYYYY |
| 10046856 | sub_100467A0 | SMPA60Fix | 8B 48 70 83 C0 40 50 51 8D 51 10 | store | 0 | E9(1) | YYYYYYYYY |
| 10046B50 | sub_10046B30 | TryToSkipAllErrors | 81 EC 40 08 00 00 53 55 8B AC 24 4C 08 00 00 | store | 0 | C3(1) | -YYYYYYYY |
| 100499E1 | sub_10046CC0 | - |  | **runtime-pattern** |  |  | ? |
| 1004AA38 | sub_10046CC0 | - |  | **runtime-pattern** |  |  | ? |
| 1004AB02 | sub_1004AAE0 | VRAMFix | 8B 44 24 10 D9 2C 24 83 C4 1C C3 | store | 0 | E9(1) | -YYYY-YY- |
| 1005040D | sub_100503B0 | VSyncFix | 8B 10 50 8B 82 A8 00 00 00 FF D0 83 | store | 13 | 10159BC0(4) | -YYYY-YY- |
| 1005044B | sub_100503B0 | VSyncFix | 8B 08 8B 51 0C 6A 01 6A 00 6A 00 6A 00 6A 00 | store | 30 | 10159BC0(4) | -YYYY-YY- |
| 100504D2 | sub_100504B0 | WantedLevelCrashFix | 8B 01 8B 90 28 01 00 00 FF D2 84 C0 0F 84 88 | store | 0 | E9(1) | YYYYYYYYY |
| 100506D2 | sub_100506B0 | SpeedupBugFix | 50 64 89 25 00 00 00 00 83 EC 34 | store | -21 | E9(1) | -YYYYYYYY |
| 100508E2 | sub_100508C0 | PedModelsInCutscenesFix | 56 8B F1 E8 ? ? ? ? 85 C0 74 0E 66 8B 40 2E | store | 0 | E9(1) | YYYYYYYYY |
| 10050BF2 | sub_10050BD0 | - | 0F 57 C0 F3 0F 11 86 20 02 00 00 | store | -13 | E8(1) | YYYYYYYYY |
| 10050C53 | sub_10050BD0 | - | C7 86 04 02 00 00 04 00 00 00 E9 | store | -13 | E8(1) | ---YYYYYY |
| 10050CAE | sub_10050BD0 | - | E8 ? ? ? ? 84 C0 74 C6 8B BE | store | 0 | E8(1) | ---YYYYYY |
| 10051439 | sub_100513A0 | LocalSettingsPath | 53 8B 5C 24 08 56 6A 00 6A 00 | store | 16 | 10158064(4) | YYYYYYYYY |
| 100514BC | sub_100513A0 | LocalSettingsPath | 7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D6 5F 5E 5B C | store | 14 | E9(1) | -Y-YYYYYY |
| 1005154A | sub_100513A0 | LocalSettingsPath | 7C 0C 68 ? ? ? ? 68 ? ? ? ? FF D7 5F C3 | store | -13 | E9(1) | Y-Y------ |
| 10051961 | sub_10051920 | NewFilesCompatibility | 68 64 28 C8 67 E8 | store | 28 | E9(1) | -Y-YYYYYY |
| 100519D2 | sub_10051920 | NewFilesCompatibility | E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3 | store | 0 | E8(1) | YYYYYYYYY |
| 10051A2A | sub_10051920 | NewFilesCompatibility | E8 ? ? ? ? 8B 36 85 F6 75 ED E8 ? ? ? ? 8B 3 | store | 0 | E8(1) | YYYYYYYYY |
| 10052240 | sub_10052200 | NewSavesCompatibility | 89 84 24 E0 01 00 00 8B 45 08 53 33 DB | store | 0 | E9(1) | YYYYYYYYY |
| 10052E15 | sub_100523C0 | - | 68 ? ? ? ? 68 A9 54 14 51 E8 | **no-effect-decoded** |  |  | ? |
| 10052E8D | sub_100523C0 | - | 08 0B C2 8B F0 E8 | **no-effect-decoded** |  |  | ? |
| 10052ECA | sub_100523C0 | - | 8B 44 24 04 89 44 24 04 66 A1 ? ? ? ? 66 85  | **no-effect-decoded** |  |  | ? |
| 10052F07 | sub_100523C0 | - | 56 8B F1 8B 06 8B 90 A0 00 00 00 57 FF D2 85 | **no-effect-decoded** |  |  | ? |
| 100534E8 | sub_100523C0 | - | 0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 | **no-effect-decoded** |  |  | ? |
| 100538D5 | sub_100523C0 | - | 76 08 F3 0F 11 4C 24 1C EB 06 F3 0F 11 44 24 | **no-effect-decoded** |  |  | ? |
| 10053A86 | sub_100523C0 | - | F3 0F 10 46 60 F3 0F 5C CA | store | 0 | E9(1) | YYYYYYYYY |
