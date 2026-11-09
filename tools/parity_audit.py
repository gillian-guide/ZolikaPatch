#!/usr/bin/env python3
"""Generate ZolikaPatch_reconstruction/docs/parity_audit.md (Mission B).

Reads the recovered evidence tables plus the shipped sources and emits one
section per INI option with: loader arm facts, build gate, our rows, resolved
destination bodies, and a status. Hand-written notes live in NOTES below.
"""
from __future__ import annotations

import json
import pathlib
import re
from collections import defaultdict

ROOT = pathlib.Path(r"D:\GitHub\OpenZDecomp\decomp")
SRC = ROOT / "ZolikaPatch_reconstruction/src"
EV = ROOT / "ZolikaPatch_reconstruction/evidence"
DOCS = ROOT / "ZolikaPatch_reconstruction/docs"

# ---------------------------------------------------------------------------
# Hand-written per-option notes/status overrides (Mission B evidence).
# ---------------------------------------------------------------------------
NOTES: dict[str, tuple[str, str]] = {
    "MouseFix": ("1:1 (shipped 2026-10-13)", (
        "Nine decode-verified patterns (all unique on 1.0.8.0). Four axis helpers, the in-angle "
        "helper and both bodies are byte-exact transcriptions (verify-bodies PASS); the advance "
        "helper (sub_100241E0) is a compiled INFO body whose only contract is VirtualProtect on "
        "the published resume. The 90-90 / 66 0F 6E C0 in-pattern writes are dest_kind=6 "
        "literal_bytes rows applied atomically in one patch-set pass. dword_10159C34 is published "
        "unconditionally (original: cmovne keeps 0 when the pattern misses; identical on 1080). "
        "Constants transcribed from .rdata (hex floats). INI key =0 with NEW comment.")),
    "OutOfCommissionFix": ("1:1 (shipped 2026-10-13)", (
        "Scan 1 (A9 39 44 24 18) E9s match+7 to the branch-widening body (byte-exact @exact) and "
        "publishes dword_10158034=match+0x11. Scan 2 (E8 ? ? ? ? 8B 74 24 14 83 C4 08 6A 00 56) "
        "wraps the game's episode/mount call with the compiled abi_ooc_wrap and publishes "
        "dword_10159BF8 (the game's mount function 0xA22BE0) / dword_10159C04 (the match E8's "
        "original target). The wrapper builds the original's embedded compressed "
        "\"finale2.sco\" resource (src/recovered_ooc_pak.inc, 163796 bytes at 0x100B6798; OOC "
        "Adler-32 of the decoded 179536-byte table = 0x41E794AB, proven by the blob trailer), "
        "builds the in-memory RPF archive (sub_1004ACE0, byte-golden via "
        "tools/extract_ooc_pak.py, FNV-1a 0xB5AF7B10) and mounts it with the game's own mount "
        "function via the original's \"memory:$<ptr>,<size>,0:\" format string "
        "(0x1012EB58/0x1012EB0C). Native self-test: zpatch_ooc_selftest(). The only divergence is "
        "a fault-avoidance null check on the table/archive malloc (the original would crash); the "
        "per-call leak (the guard dword_10159C00 has no writer) is kept 1:1.")),
    "CarPartsShadowFix": ("1:1 (gating + old-build variant shipped 2026-10-13)", (
        "Loader standalone arm (key==1) runs sub_10006910 + sub_10007E40; the shared "
        "CarDynamicShadowFix installer runs only sub_10006910. The curated rows now gate through "
        "ini_option_enabled: CarPartsShadowFix rows fire on key==1 OR (CarDynamic==1 && key!=1); "
        "sub_10007E40's EB write row uses the internal spelling CarPartsShadowFix@standalone "
        "(key==1 only). Regression test: 'CarParts standalone EB row'.")),
    "CarDynamicShadowFix": ("1:1 (build variants shipped 2026-10-13)", (
        "Shared installer sub_10010B60: pattern A (1060+) -> E9@0 to 0x100068A0, dword_1015BDCC = "
        "match+0xB, dword_1015B5B8 = match + *(match+7) + 0xB; pattern B (1000/1004/1010/1040) "
        "-> E9@0 to 0x100068D0 with match+0xD / match + *(match+9) + 0xD; the C/D/E branch "
        "(old builds) E9s C+3 -> C+0x1E, publishes dword_1015BDC4 = Dmatch (new rebased global "
        "zpatch_car_dynamic_d) and NOP-fills 6 bytes at C+0x3E when pattern E is missing "
        "(internal spelling CarDynamicShadowFix@noE, build gate 1000/1040 - E exists only on "
        "1.0.0.4). Pattern F's EB write (1060+, reached only when C is absent) plus the four "
        "sub_1000B790/sub_1000EA00 sig_writes. Verified per build in the native tests against "
        "GTAIV_1040.exe; the shared installer calls sub_10006910 only when CarDynamicShadowFix==1 "
        "&& CarPartsShadowFix!=1.")),
    "CutsceneFixes": ("1:1 (shipped 2026-10-13)", (
        "Two chains: chain A (callback 0x10044160 writing EB at the never-published site slot "
        "0x10185F88) is dead, but chain B - the 18 KB worker 0x10038B40 reached through the second "
        "forwarder 0x10045420/0x10045410 and slot 0x10164F04 - is the real cutscene fix (the user "
        "confirms jittery camera/zoom fixes). Shipped: scan 1 pattern 8B 4C 24 2C 5F 5E 5B 33 CC "
        "B0 01 E8 (E8 at match-34 -> sub_100388C0, publishes dword_1015BDE4 = match + *(match-4) "
        "and dword_1015BDF0 = that + 0x4F) and scan 2 pattern 8B CF 88 44 24 0F E8 ? ? ? ? A1 "
        "(E8 at match-5 -> the same body). Both patterns: 1 hit on every classic build + EFLC "
        "1110/1120/1130, absent on 12059. The body 0x100388C0 is a 160-instruction @exact "
        "transcription (verify-bodies PASS) that reads a scene float via the loader resolver "
        "(salt dword_1015803C), pokes the camera function's mov byte [esp+0x1C],imm immediate "
        "(target+0x4E), double-calls the original and keeps the stable result after the 0.3333 s "
        "accumulator. sub_10045430 (0x60 over the 0x64 field offset) belongs to ReversingLightFix, "
        "not this option.")),
    "NewMemorySystem": ("1:1 (shipped 2026-10-13)", (
        "The handler's two scans: E8 at match-34 of 3D 80 99 99 59 73 05 -> the body sub_10038670 "
        "(writes the CRageAllocator vtable into the published allocator singleton), E9 at match-29 "
        "-> match-16 (skips the game's init/registration tail), and the second scan "
        "(6A 01 B9 00 00 40 06) publishes dword_10159CD8 = *(match+8) (the mov esi, 0x19cf9f0 "
        "immediate). The vtable's 24 entries are shipped: 10 stubs + the ctor are @exact "
        "(verify-bodies PASS) and alloc/free/contains (vtable[2]/[3]/[18]) are compiled ports of "
        "the original's HeapAlloc wrapper + registry (0xCD fill, [p-4] base offset, FNV-1a "
        "membership under a recursive critical section). INI default 0; the user's INI has 1.")),
    "DrivebyFOVFix": ("1:1 with documented guards (registered 2026-10-13)", (
        "The arm installs unconditionally when its pattern `F3 0F 10 46 60 F3 0F 5C CA` matches, "
        "and that pattern has 1 hit on 1.0.8.0 - the earlier 'not installed on 1080' reading came "
        "from the coverage table's handler_va=0 entry and was wrong. The shim (0x100064B0) is "
        "registered; it reproduces the original's `pushal; call walker; popal; movss xmm0,[fov]; "
        "jmp [resume]` with fault-avoidance guards on the resolver result (the original faults when "
        "the handle is unregistered) and keeps the 45.0f default until it resolves. See "
        "parity_and_improvements.md sec.4.")),
    "DualVehicleHeadlights": ("build-gated off 1.0.8.0", (
        "Build mask 0x7E (compact 1000-1060); absent on 1080 - matching the loader.")),
    "LaunchWithPlayGTAIV": ("build-gated off 1.0.8.0", (
        "Implemented in dllmain (apply_launch_with_play_gtaiv); the loader excludes 1080.")),
    "Patch7RemoveModChecks": ("1:1 (aliased key)", (
        "The loader reads a second spelling of Patch6RemoveModChecks from the old INI; aliased in "
        "runtime.cpp. Only internal/hidden key found in the audit.")),
    "RemoveBoundingBoxCulling": ("1:1 with INI escape hatch", (
        "The original branch is kept verbatim; RemoveBoundingBoxCulling=0 selects the guarded "
        "variant (documented divergence in parity_and_improvements.md §4).")),
    "BuildingAlphaFix": ("1:1 except pattern 2 (withheld)", (
        "Pattern 2 is deliberately left unapplied (crash evidence); documented in "
        "parity_and_improvements.md §1/§4.")),
    "DebugPrintSpawns": ("1:1 with documented divergence", (
        "394-instruction probe, exact body. The MP model-name lookup returns \"**UNKNOWN**\" "
        "because the original's 38455-entry name table (0x100DE770) is not embedded (shared with "
        "BadPlayerModelCrashFix). Perf candidate: 31000-entry linear hash scan per spawn.")),
    "BadPlayerModelCrashFix": ("1:1 with documented divergence", (
        "Includes the native-scripting call layer and the exact abi_bpm_check body; the MP "
        "name-table fallback shares DebugPrintSpawns' \"**UNKNOWN**\" divergence. Fault-avoidance "
        "guards on the model array/object are recorded in §4.")),
    "RestoreDeathMusic": ("1:1", (
        "Three E8 rows + slots; gated by key==1 OR the IVMenuAPI integration being available "
        "(the loader's fall-through), mirrored in ini_option_enabled.")),
    "LocalSettingsPath": ("1:1 (2 inert loader rows documented)", (
        "Two duplicated bare-E9 op rows (offset -13 against the FF D7 fallback pattern) never fire: "
        "that pattern has 0 hits on 1.0.8.0, and the curated sig_install covers the intended write. "
        "They cannot be suppressed textually (runtime-built handler patterns); documented instead.")),
    "ForceCarHeadlightShadows": ("1:1 with fault-avoidance guard", (
        "The per-call table walk body is exact; the lookup/object guard is documented in §4 "
        "(original can deref a null object).")),
    "FreezeCarFix": ("1:1", (
        "17 E8 rows + DebugPrintSpawns' 394-insn body; per-call wheelangle/model lookups are perf "
        "candidates (Mission C).")),
    "BlackscreenFix": ("1:1 (exact shim)", (
        "Exact body; the loader op row that writes inside the original shim's own bytes is "
        "withheld by shim_withheld() (our layout differs -> writing there would corrupt our code).")),
    "PathNodeCrashFix": ("1:1 (exact shim)", (
        "Exact body; the loader's in-shim op row is withheld by shim_withheld() (same rationale as "
        "BlackscreenFix).")),    "PlaneCrashFix": ("1:1 (exact shim)", "Exact body; in-shim op row withheld (shim_withheld)."),
    "WantedLevelCrashFix": ("1:1 (exact shim)",
        "Exact body; in-shim op row withheld (shim_withheld)."),
    "GroupHackFix": ("1:1 (exact shim)", "Exact body; in-shim op row withheld (shim_withheld)."),
    "SMPA60Fix": ("1:1 (exact shims)", (
        "Three exact shims; the loader's in-shim E9/EB op rows are withheld (shim_withheld).")),
    "CollisionDeadlockFix": ("1:1 (exact shim)",
        "Exact body; in-shim op row withheld (shim_withheld)."),
    "FreezeGunFix": ("1:1 (exact shim)", "Exact body."),
    "PlayerAsPedComponentsFix": ("1:1 (exact shim)", "Exact body."),
    "BlackscreenFix2": ("", ""),
    "Extra1": ("", ""),
    "WantedLevelCrashFix2": ("", ""),
    "Patch6RemoveModChecks": ("1:1", (
        "Two E9 rows + a body; locally verifiable. Includes the version gate that can only fire on "
        "1201+ (faithful TerminateProcess/system(\"start\") path; documented).")),
    "SkipIntro": ("1:1 (compiled body)",
        "Compiled INFO body (reverted shortened-intro variant stays archived in §1)."),
    "SkipMenu": ("1:1 (compiled body)",
        "Compiled INFO body; INI escape; the reverted savegame guard is archived in §1."),
    "SkipLauncher": ("1:1", "LaunchWithPlayGTAIV launcher stub path."),
    "AddOptionsInMenu": ("1:1", (
        "Menu registration via IVMenuAPI; SetMenuIntegrationAvailable drives RestoreDeathMusic's "
        "fall-through. Focus-loss guard documented in §4.")),
    "MiscFixes": ("1:1 (bundle kept)", (
        "The original bundles five fixes under one key; the per-fix split was reverted (archived in "
        "§1). Rows: 6 writes.")),
    "NewSavesCompatibility": ("1:1 with fault-avoidance guards",
        "The fread/size/malloc guards are documented in §4; chunk payloads are read+discarded (perf candidate)."),
    "Patch4RemoveModChecks": ("1:1", "Four E9 rows."),
    "Patch5RemoveModChecks": ("1:1", ""),
    "PopupHeadlightsFlag": ("1:1 (EDX twin shipped 2026-10-13)", (
        "Real flag option: per-call 512-slot scan + popuplights walk are perf candidates. The EDX "
        "twin (0x10044560) is now shipped for the builds that have the EDX-form pattern "
        "(1000/1004/1010/1040): E9@0 to 0x10044560 + EB@10 + the rebased-global operand at +82 "
        "+ the two resume publishes (match+10/match+12); the primary (EAX form, 1060+) targets "
        "0x10044530. Verified against GTAIV_1040.exe in the native tests.")),
    "BadPlayerModelCrashFix2": ("", ""),
    "RemoveGFWLUpdatePopup": ("1:1", ""),
    "FixPCIDKickbug": ("1:1", ""),
    "InstallerFixes": ("1:1", ""),
    "CombineSoundFix": ("1:1", ""),
    "BetterMPSync": ("1:1 (re-added 2026-10-13)", "Row set re-added in session 8 after the bisect."),
    "CombinedRadioFreezeFix": ("1:1 (re-added 2026-10-13)", "Row set re-added in session 8."),
    "BuildOnlyPatterns": ("withheld (A4)", (
        "Trio fallback, CarDynamic patterns B/C/D/E, PopupHeadlightsFlag's EDX twin, "
        "DrivebyFOVFix's shim (installed only on other builds), CarParts' 83 FA 04 75 0C variant: "
        "all 0-hit on the IV builds available. One-of group semantics cannot be expressed by the "
        "row machinery; kept documented. See session8_feature_specs.md §1.")),
    "HighQualityReflections": ("withheld (blocker: pattern string + destination body)", (
        "Arm key default 0 (calls sub_10023DC0 -> sub_10023DB0). Mechanics decoded: module scan "
        "(sub_1005B6A0) of the string built in [ebp-0x430] from the literal table at 0x10129F8C; on "
        "a match, E9 at the match to [0x10192F18] (the arm-installed second body 0x1001F030), and "
        "publishes dword_10159CFC = match+0x13, dword_10159D04 = *(match+0xF)+0x13+match. BLOCKER: "
        "the builder is fully inlined (no append/reset calls), and the reconstructed '4' literal + "
        "seed string has 0 hits in GTAIV.exe - the pattern is still unknown; the 163-instruction "
        "destination body 0x1001F030 is not decompiled/ported. Nothing installs (default 0 anyway).")),
    "FixPCIDKickbug": ("1:1 (DllMain helper)", (
        "The HKCU PCID deletion is implemented in dllmain.cpp (apply-time, default 1). Safety "
        "candidate in parity_and_improvements.md §6.3.")),
    "RemoveGFWLUpdatePopup": ("1:1 (DllMain helper)", (
        "The xlive.dll +0x66F770 writes are rebuilt in dllmain/hook bodies; the absent-xlive base "
        "guard is recorded in §4.")),
    "StartupTimeFix": ("1:1 (match-literals)", (
        "Three 4-byte literal writes into the startup timer code (rows in "
        "recovered_match_literals.inc).")),
    "VSyncFix": ("1:1", (
        "Two literal stores put the plugin dword 0x10159BC0 into the game's vsync operand slots; "
        "runtime.cpp has a dedicated arm for the resolve-once value.")),
    "NoImpostorHeightCutoff": ("1:1 (match-literals)", (
        "Two 2-byte NOPs (rows in recovered_match_literals.inc, all-match index).")),
    "NoLODLightHeightCutoff": ("1:1 (match-literals)", "Same NOP pair as NoImpostorHeightCutoff."),
    "NoLoadingSleep": ("1:1 (literal store)", (
        "4-byte zero literal store at the loading-sleep branch (recovered_literal_stores.inc) plus "
        "the handler op rows.")),
    "NoPhysicsSleep": ("1:1 (indirect write)", (
        "Indirect literal write at game 0xC90B18 (recovered_indirect_writes.inc).")),
    "IncreasePtrNodes": ("1:1 (literal stores)", (
        "Two literal stores (recovered_literal_stores.inc) resizing the path-node pools.")),
    "IncreaseVehicleModelLimit": ("1:1 (indirect write)",
        "Indirect 4-byte write 0x400 at game 0xB3E8E8."),
    "IncreaseVehicleStructLimit": ("1:1 (literal store)",
        "1-byte literal store 0x64 at game 0x64A2B1."),
    "OverrideGTADatPath": ("1:1", (
        "The detour swaps the pushed gta.dat path for the plugin buffer filled by "
        "OverrideGTADatPathTo at load.")),
    "OverrideGTADatPathTo": ("1:1", (
        "Value-only key (default `common:/data/gta.dat`): the arm copies the string into the plugin "
        "buffer OverrideGTADatPath's detour pushes.")),
}


def parse_inc_rows(path: pathlib.Path):
    rows = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = line.strip()
        if line.startswith("{"):
            rows.append(line.rstrip(","))
    return rows


def option_of(row: str) -> str | None:
    m = re.match(r'\{\"([^\"]+)\"', row)
    return m.group(1) if m else None


def main() -> int:
    coverage = parse_inc_rows(SRC / "recovered_feature_coverage.inc")
    gate_rows = parse_inc_rows(SRC / "recovered_build_gates.inc")
    op_rows = parse_inc_rows(SRC / "recovered_patch_ops.inc")
    sig_jumps = parse_inc_rows(SRC / "recovered_sig_jumps.inc")
    match_jumps = parse_inc_rows(SRC / "recovered_match_jumps.inc")
    lit_stores = parse_inc_rows(SRC / "recovered_literal_stores.inc")
    ind_writes = parse_inc_rows(SRC / "recovered_indirect_writes.inc")
    rva_jumps = parse_inc_rows(SRC / "recovered_rva_jumps.inc")
    match_literals = parse_inc_rows(SRC / "recovered_match_literals.inc")
    patch_sites = parse_inc_rows(SRC / "recovered_patch_sites.inc")
    extra_sigs = parse_inc_rows(SRC / "recovered_extra_signatures.inc")
    rva_installs = parse_inc_rows(SRC / "recovered_rva_installs.inc")
    corrections = json.loads((EV / "op_corrections.json").read_text(encoding="utf-8"))
    handler_table = json.loads((EV / "handler_table.json").read_text(encoding="utf-8"))
    bodies_src = (SRC / "hook_bodies_win.cpp").read_text(encoding="utf-8", errors="replace")

    registered = {int(m.group(1), 16): m.group(2)
                  for m in re.finditer(r"\{(0x[0-9A-Fa-f]+)u,\s*\[\]\(\) -> std::uintptr_t\s*\{\s*"
                                       r"return reinterpret_cast<std::uintptr_t>\(&(\w+)\);", bodies_src)}
    exact = {int(m.group(1), 16): m.group(2)
             for m in re.finditer(r"@exact\s+(0x[0-9A-Fa-f]+)[^\n]*\n(?:[^\n]*\n){0,4}?"
                                  r"[^\n]*?(abi_\w+)\s*\(", bodies_src)}

    ini_values: dict[str, str] = {}
    in_options = False
    for line in (ROOT / "game_refs/ZolikaPatch.ini").read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        if stripped.startswith("["):
            in_options = stripped.lower() == "[options]"
            continue
        if not in_options or not stripped or stripped.startswith(";"):
            continue
        if "=" in stripped:
            key, _, value = stripped.partition("=")
            ini_values[key.strip()] = value.strip()

    by_option = defaultdict(lambda: {
        "op": [], "sig": [], "match": [], "lit": [], "ind": [], "rva": [], "curated": [],
        "mlit": [], "rva2": []})
    for row in op_rows:
        by_option[option_of(row)]["op"].append(row)
    for row in sig_jumps:
        by_option[option_of(row)]["sig"].append(row)
    for row in match_jumps:
        by_option[option_of(row)]["match"].append(row)
    for row in lit_stores:
        by_option[option_of(row)]["lit"].append(row)
    for row in ind_writes:
        by_option[option_of(row)]["ind"].append(row)
    for row in rva_jumps:
        by_option[option_of(row)]["rva"].append(row)
    for row in match_literals:
        by_option[option_of(row)]["mlit"].append(row)
    for row in rva_installs:
        o = option_of(row)
        if o:
            by_option[o]["rva2"].append(row)
    for table in ("sig_installs", "slot_installs", "sig_writes"):
        for row in corrections[table]:
            by_option[row["option"]]["curated"].append((table, row))

    gate_text = {}
    for row in gate_rows:
        m = re.match(r'\{\"([^\"]+)\",\s*(0x[0-9a-fA-F]+)u\}', row)
        if m:
            gate_text[m.group(1)] = m.group(2)

    handlers = {}
    for entry in handler_table:
        if isinstance(entry, dict) and entry.get("option"):
            handlers[entry["option"]] = entry

    lines: list[str] = []
    lines.append("# ZolikaPatch 1:1 parity audit (Mission B)")
    lines.append("")
    lines.append("Generated 2026-10-13 (session 9) by `tools/`-adjacent script "
                 "`s9_audit.py` (scratch) from the shipped evidence tables; per-option notes are "
                 "hand-written. Scope: every option in `src/recovered_feature_coverage.inc` "
                 f"({len(coverage)} rows).")
    lines.append("")
    lines.append("Evidence sources: `evidence/op_corrections.json` (curated rows; each row's "
                 "`why` names the handler/disassembly site and `tools/apply_op_corrections.py` "
                 "re-verifies every row textually against the saved handler decompile), "
                 "`src/recovered_*.inc` (generated from the handlers), `evidence/handler_table.json`, "
                 "`evidence/option_build_gates.json`, `src/hook_bodies_win.cpp` (kAbiBodies + "
                 "`@exact` markers; `tools/verify_hook_bodies.py` byte-compares every exact body), "
                 "`game_refs/ZolikaPatch.ini`.")
    lines.append("")
    lines.append("Legend: **op** = generated literal op rows; **cur** = curated rows; "
                 "**jmp** = sig/match/rva jump rows; **lit** = literal stores; **ind** = indirect "
                 "writes; **gate** = build mask from `recovered_build_gates.inc`; bodies list "
                 "`VA (symbol)` with EXACT (byte-verified), INFO (compiled, semantics checked), "
                 "or UNRESOLVED (clean no-op). Statuses: 1:1, no-op, excluded, withheld, "
                 "divergence (see §4 of parity_and_improvements.md).")
    lines.append("")

    # Summary table.
    lines.append("## Summary")
    lines.append("")
    lines.append("| # | option | class | handler | gate | INI | rows (op/cur/jmp) | bodies | status |")
    lines.append("|---|--------|-------|---------|------|-----|-------------------|--------|--------|")
    for i, row in enumerate(coverage, 1):
        m = re.match(r'\{\"([^\"]+)\",\s*\"([^\"]*)\",\s*(0x[0-9a-fA-F]+)u,\s*(true|false),\s*'
                     r'(true|false),\s*(0x[0-9a-fA-F]+)u,\s*(\d+)u,\s*(\d+)u,\s*(true|false),\s*\"([^\"]*)\"',
                     row)
        if not m:
            continue
        option, default, handler, _sig, _op, mask, _n1, _n2, resolved, cls = m.groups()
        data = by_option[option]
        gate = gate_text.get(option, "-")
        ini = ini_values.get(option, "MISSING")
        dests = []
        for table, r in data["curated"]:
            if table == "sig_installs" and r.get("dest_kind") == "module_va":
                dests.append(int(r["dest"], 16))
        for r in data["sig"]:
            mm = re.search(r",\s*(0x[0-9a-fA-F]+)u\}", r)
            if mm:
                dests.append(int(mm.group(1), 16))
        for r in data["match"]:
            mm = re.search(r",\s*(0x[0-9a-fA-F]+)u,\s*0x[0-9a-fA-F]+u\}", r)
            if mm:
                dests.append(int(mm.group(1), 16))
        for r in data["rva"]:
            mm = re.search(r",\s*(0x[0-9a-fA-F]+)u,\s*0x[0-9a-fA-F]+u\}", r)
            if mm:
                dests.append(int(mm.group(1), 16))
        for r in data["rva2"]:
            # {option, site_rva, opcode, kind, build, operand, reserved, slot};
            # only kind 1 (jump to a module VA) needs a destination body.
            mm = re.match(r'\{"[^"]+",\s*\d+u,\s*(0x[0-9a-fA-F]+)u,\s*(\d+)u,\s*\d+u,\s*(\d+)u',
                          r)
            if mm and int(mm.group(1), 16) != 0 and int(mm.group(2), 10) == 1:
                dests.append(int(mm.group(3), 10))
        body_bits = []
        for va in sorted(set(dests)):
            if va in registered:
                body_bits.append(f"{registered[va]}{'*' if va in exact else ''}")
            else:
                body_bits.append(f"0x{va:X}:UNRESOLVED")
        status, _note = NOTES.get(option, ("1:1", ""))
        if not status:
            status = "1:1"
        lines.append(f"| {i} | {option} | {cls} | {handler} | {gate} | {ini} | "
                     f"{len(data['op'])}/{len(data['curated'])}/{len(data['sig'])+len(data['match'])+len(data['rva'])}"
                     f"{('+ml' + str(len(data['mlit']))) if data['mlit'] else ''}"
                     f"{('+ri' + str(len([r for r in data['rva2'] if re.match(r'.*, 0x[1-9a-fA-F]', r)]))) if data['rva2'] else ''} | "
                     f"{', '.join(body_bits) if body_bits else '-'} | {status} |")
    lines.append("")
    lines.append("`*` = byte-exact body verified by verify_hook_bodies. "
                 "Row counts exclude literal stores/indirect writes in the jmp column.")
    lines.append("")

    # Per-option sections.
    lines.append("## Per-option audit")
    lines.append("")
    for i, row in enumerate(coverage, 1):
        m = re.match(r'\{\"([^\"]+)\",\s*\"([^\"]*)\",\s*(0x[0-9a-fA-F]+)u,\s*(true|false),\s*'
                     r'(true|false),\s*(0x[0-9a-fA-F]+)u,\s*(\d+)u,\s*(\d+)u,\s*(true|false),\s*\"([^\"]*)\"',
                     row)
        if not m:
            continue
        option, default, handler, has_sig, has_op, mask, nsig, nop, resolved, cls = m.groups()
        data = by_option[option]
        status, note = NOTES.get(option, ("1:1", ""))
        if not status:
            status = "1:1"
        lines.append(f"### {i}. {option}")
        lines.append("")
        lines.append(f"- arm: `{option}` default `{default}`; handler `{handler}`; class `{cls}`; "
                     f"loader op rows {nop}, loader sig ops {nsig}, resolved-on-1080 `{resolved}`")
        lines.append(f"- build gate: {gate_text.get(option, 'none (all builds)')}; "
                     f"INI value: `{ini_values.get(option, 'MISSING')}`")
        lines.append(f"- our rows: op {len(data['op'])}, curated {len(data['curated'])}, "
                     f"jumps {len(data['sig'])}/{len(data['match'])}/{len(data['rva'])}, "
                     f"literal stores {len(data['lit'])}, indirect {len(data['ind'])}, "
                     f"match-literals {len(data['mlit'])}, rva-installs {len(data['rva2'])}")
        if data["curated"]:
            sigs = []
            for table, r in data["curated"][:8]:
                sig = r.get("signature", r.get("sig", ""))
                off = r.get("site_offset", r.get("field_offset", r.get("offset", "")))
                sigs.append(f"{table}:`{sig}`@{off}")
            if len(data["curated"]) > 8:
                sigs.append(f"(+{len(data['curated']) - 8})")
            lines.append(f"- curated: {'; '.join(sigs)}")
        lines.append(f"- status: **{status}**")
        if note:
            lines.append(f"- note: {note}")
        lines.append("")
    lines.append("")

    # ---- Hand-written audit sections -------------------------------------
    lines.append("## 3. Divergences and gaps (cross-referenced to parity_and_improvements.md §4)")
    lines.append("")
    lines.append("Every deliberate deviation from the original is recorded in "
                 "`parity_and_improvements.md` §4; the audit re-confirmed each one:")
    lines.append("")
    lines.append("1. **Fault-avoidance guards** (shipped, only reachable when the original would fault): "
                 "ForceCarHeadlightShadows' lookup/object checks, AddOptionsInMenu's focus-site guard "
                 "(setter), GarageModelFix's `hash == 0` + resolved-index range checks, OutOfCommissionFix's "
                 "null logger/original slots.")
    lines.append("2. **MP model-name table** (`BadPlayerModelCrashFix`, `DebugPrintSpawns`): the only "
                 "MP-name divergence; the 556 KB table is not embedded, so the diagnostics print "
                 "`\"**UNKNOWN**\"` exactly like the original's not-found path.")
    lines.append("3. **CarParts sub_10007E40 over-application (A5)**: fixed this session. The shared-vs-"
                 "standalone decision now runs through `ini_option_enabled` for every row; the EB sig_write "
                 "uses the internal `CarPartsShadowFix@standalone` spelling. Regression test added.")
    lines.append("4. **Withheld build-only rows (A4)**: trio fallback (`7C E0 8B 06 ...` → 0x100051E0), "
                 "CarDynamic patterns B/C/D/E, PopupHeadlightsFlag's EDX twin (0x10044560), CarParts' "
                 "`83 FA 04 75 0C` variant; all 0 hits on the available IV builds, and the row machinery "
                 "cannot express the one-of groups. `session8_feature_specs.md` §1 lists them.")
    lines.append("5. **Version guard** (`DllMain`): faithful `TerminateProcess` + `system(\"start …\")` for "
                 "compact >= 1201; cannot fire on any supported build.")
    lines.append("6. **Double-load MessageBox** and the **default-on `SHDeleteValueW` PCID delete**: kept "
                 "faithful (safety candidates in §6.3).")
    lines.append("7. **`shim_withheld()`**: the loader's in-shim op rows (destinations inside the original "
                 "shim bodies' own bytes) are withheld because our module layout differs there; the shims "
                 "install via the curated/sig-jump rows.")
    lines.append("8. **MouseFix** compiled advance helper + unconditional `dword_10159C34` publish "
                 "(identical where the pattern exists); **LocalSettingsPath**'s two inert duplicated "
                 "loader rows; the **10 op-row suppressions** (see §5).")
    lines.append("9. **OutOfCommissionFix's \"finale2.sco\" payload** is ported in full: the embedded "
                 "compressed resource is decoded by the transcribed decoder (verified by its own "
                 "Adler-32 trailer 0x41E794AB) and wrapped in the golden RPF archive "
                 "(tools/extract_ooc_pak.py; FNV-1a 0xB5AF7B10) that the wrapper mounts through the "
                 "game's own mount function. The only divergence is the malloc null-check guard.")
    lines.append("10. **Reverted improvements** in §1 (MiscFixes split, SkipIntro variant, SkipMenu "
                 "savegame check) stay archived; nothing re-shipped.")
    lines.append("")
    lines.append("## 4. Withheld / no-op / excluded")
    lines.append("")
    lines.append("| item | status | evidence |")
    lines.append("| --- | --- | --- |")
    lines.append("| `CutsceneFixes` | no-op in 7.65 | parity_and_improvements.md §5.2 (its slot is never published) |")
    lines.append("| `NewMemorySystem` | excluded | needs a game-vtable reimplementation (§5) |")
    lines.append("| `DrivebyFOVFix` | not installed on 1.0.8.0 | the original does not install it on 1080; our unresolved destination is the clean no-op |")
    lines.append("| Build-only rows (A4) | withheld | 0 hits on the available builds; listed in session8_feature_specs.md §1 |")
    lines.append("| `BuildingAlphaFix` pattern 2 | withheld | crash evidence; parity_and_improvements.md §1/§4 |")
    lines.append("| `NewSavesCompatibility` guards | documented, not shipped | see §4 record; faithful port |")
    lines.append("")
    lines.append("## 5. Spot checks and method")
    lines.append("")
    lines.append("- **Curated rows**: every row in `evidence/op_corrections.json` is re-verified textually "
                 "against the saved handler decompile by `apply_op_corrections.py` (it fails on drift). "
                 "The 10 `suppressions` each record a mechanically-generated row that disagrees with the "
                 "handler (mis-split fills, off-by-one displacements, bare operands without their opcode, "
                 "a CRC NOP that would erase the pattern before the install stage) and name the curated "
                 "row that replaces it.")
    lines.append("- **Hook bodies**: `verify_hook_bodies.py` byte-compares all 62 `@exact` bodies "
                 "(addresses in the original module normalized) - all PASS this session, including the "
                 "MouseFix axis/angle bodies and the OutOfCommissionFix branch body. Compiled (INFO) "
                 "bodies were checked against the original disassembly semantics.")
    lines.append("- **Patterns vs the game binary**: the 9 MouseFix patterns and both OutOfCommissionFix "
                 "patterns were re-scanned against `game_refs/GTAIV.exe` this session: unique hits at the "
                 "reported VAs (0x8A9E10, 0x8AA190, 0xA84C1F, 0xA7CCF7, 0xA9AB43, 0x8AA1B0, 0xB4D230, "
                 "0xB4D3A0, 0xA81B1F; 0xA22E90, 0xB70733), and the write offsets' pre-images match the "
                 "handler reports (`F7 D8`/`F3 0F 2A C0` at +3/+5, `75 0F` at +8, ...).")
    lines.append("- **Gating**: `build_allows_option` + `recovered_build_gates.inc` (positive gates only: "
                 "DualVehicleHeadlights 0x7E, EpisodicVehicleSupport 0x300, ...); `unknown` build installs "
                 "nothing; every live install is INI-gated through `ini_option_enabled` (including the "
                 "RestoreDeathMusic/CarParts cross-option rules).")
    lines.append("- **INI**: every loader-read key has an entry in `game_refs/ZolikaPatch.ini` except the "
                 "internal alias `Patch7RemoveModChecks` (documented); the keys added/re-enabled this "
                 "session (MouseFix, OutOfCommissionFix) are `=0` with NEW comments.")
    lines.append("- **Limitations**: the op-row tables are generated from the saved decompiles, not "
                 "re-derived from the binary tonight; the audit's `status: 1:1` therefore means "
                 "\"generator-verified against the handler + bodies verified or absent\", not an "
                 "independent re-decompilation of every handler. Options whose handlers build patterns at "
                 "runtime are annotated in `why` fields with the callback VA + report.")
    lines.append("")

    out = DOCS / "parity_audit.md"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {out} ({len(lines)} lines)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
