# Session 8 overnight feature specs (evidence packs for the remaining ports)

Written 2026-10-13 ~04:00. Companion to the "Session 8" sections in `HANDOFF.md` and the
dead-code / divergence notes in `parity_and_improvements.md` §4-5. Everything here was verified
against `game_refs/ZolikaPatch_original.asi.off` (base 0x10000000) and `game_refs/GTAIV.exe`
(1.0.8.0, base 0x400000, file offset == RVA) during that session; the implemented parts are listed
with their live rows so a future session can extend them.

## 1. CarDynamicShadowFix / CarPartsShadowFix shared installer (`sub_10010B60`)

**Implementation status:** the P0 steps are shipped (option `CarDynamicShadowFix`, default 1;
its INI key stays at the original's value): pattern-A hook + body `0x100068A0`, the pattern-F
byte write, `sub_1000B790`'s three writes (the `ForceCarHeadlightShadows != 1` half) and the
`sub_1000EA00` byte write (shared with `BuildingDynamicShadows`, both rows present). The
CarParts hook rows (`sub_10006910`) are shipped and, like the original, also fire when
`CarDynamicShadowFix == 1 && CarPartsShadowFix != 1` (see the CarParts OR rule in
`runtime.cpp::ini_option_enabled`). `sub_10007E40`'s byte write (CarParts standalone arm) is
shipped too.

**Correction to earlier notes:** there is **no JIT and no generated tables**. `dword_1015B5B8`
and `dword_1015BDCC` are two 4-byte game pointers published from the scanned site (callee and
resume); the "two tables + JIT code" premise in the session-6/7 notes was wrong (the earlier
sessions likely saw the slot stores at 0x100128A7/B5 and 0x10014F82/98).

Build-9 write set (all patterns unique where noted):

| step | gate | write | game site (1.0.8.0) |
|---|---|---|---|
| S0 | `a1` = CarPartsShadowFix != 1 | `sub_10006910` (E9 + `BDC8=match+2`, `BDC0=match+0x45` rows) | 0x40C9F9 (already in the tables) |
| S1 | `a2` = ForceCarHeadlightShadows != 1 | `sub_1000B790`: `8F73EC: push 0x504 -> 0x100`, `8F7422: push 0x104 -> 0x100`, `8F719F: jz -> jmp +0x9F` | shipped |
| S4 | pattern A found | `E9@0 -> 0x100068A0`, `B5B8 = m + *(m+7) + 11` (0x959FF0), `BDCC = m+11` (0x95ADDA) | **0x95ADCF**, 1 hit |
| S5 | pattern A absent (other builds only) | `E9@0 -> 0x100068D0`, `BDCC = m2+0xD`, `B5B8 = (m2+9)+*(m2+9)+4` | 0 hits on IV 1.0/1.0.8 |
| S6 | none | `sub_1000EA00`: byte `0` at match+2 (`8F7422: 04->00`, i.e. push 0x104 -> 0x100) | shipped (also for BuildingDynamicShadows) |
| S9 | pattern C absent | pattern F byte `0xEB` at match+6 (`95A41E: 75->EB`, same target 0x95A437) | shipped |
| S7/C/D/E, `BDC4` | other builds | pattern C/D/E branch (publishes `dword_1015BDC4`, write-only) | 0 hits on IV builds; omitted |

Pattern strings (forward form; `?` = wildcard): A `56 6A 02 53 53 57 E8 ? ? ? ? 56 6A 02 6A 04
6A 05 57 E8`; B `56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8`; C `83 F8 14 7D
05 83 F8 06 7D 14`; D `8B 45 10 8B 16 8B 92 8C 00 00 00`; E `F6 86 68 02 00 00 04 0F 85 E6 00 00
00`; F `83 E0 0F 83 F8 03 75 17 F6 86 6C 02 00 00 04`. The 0-hit patterns cannot be validated
(no matching build available) — withhold per the fidelity rule.

Bodies: `0x10006880` (13 insns, shipped @exact), `0x100068A0` (shipped @exact), `0x100068D0`
(shipped @exact as `abi_carparts_shadow_2b`, unused on 1.0.8.0 because pattern A is the one
found). `ForceCarHeadlightShadows`'s own installer (`sub_1001ADD0`) additionally calls
`sub_1000E870` when its arg is set — re-asserts the same three constants, a byte-identical no-op.

## 2. Movings* trio (MovingSteeringWheelSupport / ShakingEngineBlock / MovingPlaneParts)

**Implementation status: shipped** (all three INI keys `= 0` with NEW comments). One shared
installer `sub_10005210` invoked by three independent loader arms (each sets its own flag and
calls it; the 2nd/3rd calls are no-ops because the site already has the E9). Primary pattern
`32 DB 8B 16 8B 82 28 01 00 00 8B CE FF D0` (game 0xA5389E, 1 hit): E9 at match+2 ->
`0x100051B0`, `dword_1015AD44 = match+16`, `dword_1015AD4C = match + *(match+423) + 427`
(game 0x8F65F0, the plane part-animation call). Fallback pattern
`7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2` (0 hits on 1.0.8.0) -> `0x100051E0`.
Every arm also calls `sub_10004110` (the IDE parser hook, already installed by
CopSirensFlag/PopupHeadlightsFlag) — the trio rows add it per option and the wheelangle tables
(`dword_1015A538`/`dword_10159D38`) are filled from `wheelangle:` IDE lines by the existing
`sub_10003FA0` parser.

Bodies: naked detours `0x100051B0`/`0x100051E0` (shipped @exact, cache ESI, `pushad`/consumer/
`popad`, vfunc+0x128 call, `test al,al`, tail-jump) and the compiled consumer
`abi_steer_body` (steering-wheel angle from `[PopupLightAngles]`-style degree tables converted
to radians, engine-block shake with the 0.75/0.25+0.5 phase model, plane parts 89-97 through
`dword_1015AD4C`). The two preamble game functions `dword_1015D63C`/`dword_10159CDC` are the
matrix setters (resolved by `apply_loader_preamble`).

## 3. MouseFix (callback `0x10024600`) — spec complete, not yet integrated

9 verified unique patterns and their installs (see `docs/callback_group_analysis.md` §2 for the
full table and the destination-body descriptions):

- `51 8B 44 24 0C 85 C0 0F 57 D2 56 8B 35 ? ? ? ? 57 8B 3D ? ? ? ? F3 0F 11 54 24 08 75 0C`
  (game 0x8A9E10) -> E9@0 to `0x10024060`; publishes `dword_10159C50 = *(match+0xD)`,
  `dword_10159C3C = *(match+0x14)` (the mouse X/Y globals; the old source confirms +13/+20).
- `8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? D8` (0x8AA190) -> E9@0 to `0x100240C0`.
- `83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07` (0xA84C1F) -> +3 `90 90`, +5 `66 0F 6E
  C0`, +0x1F `90 90`, +0x21 `66 0F 6E C0`.
- `83 C4 08 F7 D8 F3 0F 2A C0 6A 01 8B CB F3 0F 11 07` (0xA7CCF7) -> same four writes.
- `83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46` (0xA9AB43) -> +3 `90 90`, +5 `66 0F 6E C8`.
- `8B 44 24 08 8B 4C 24 04 50 51 E8 ? ? ? ? 83 C4 08 83 3D` (0x8AA1B0) -> E9@0 to `0x10024170`;
  publishes `dword_10159C5C = *(match+0x14)`.
- `56 8B 74 24 08 57 8B 7C 24 10 C7 06 00 00 00 00` (0xB4D230) -> E8@0x33 and E8@0x5A to
  `0x10024120`.
- `83 EC 08 DB 05 ? ? ? ? 68` (0xB4D3A0) -> publishes `dword_10159C34 = match` (conditional).
- `53 56 8B F1 57 8B BE 40 01 00 00 85 FF F3 0F 11 46` (0xA81B1F) -> E9@(match-0xF) to
  `0x10024560`, E9@(match+0x746) to `0x100243A0`, publishes `dword_10159C60 = match`,
  `dword_10159C40 = match+0x756`.

**Integration blocker:** patterns 3-5 need four separate multi-byte writes at +3/+5/+0x1F/+0x21
inside their own pattern. The current `SigInstall` patch-set writes one opcode/branch per row; a
new `dest_kind 6 = literal_bytes` (with a byte string) inside the patch set is the clean way to
express them atomically (the existing `sig_writes` stage re-scans per row, so a write at +3 would
break the scan for the write at +0x1F). The two naked bodies (`0x10024560`, `0x100243A0`) are
byte-transcribed in `docs/callback_group_analysis.md` §2.3; the four compiled helpers
(`0x10024060`, `0x100240C0`, `0x10024120`, `0x10024170`) and the two math helpers (`0x100241E0`
0x1BB, `0x10024440` 0x114) still need C transcriptions.

## 4. OutOfCommissionFix / HighQualityReflections / CutsceneFixes

- **OutOfCommissionFix** (callback `0x1004C3F0`): module scan -> E8 at match to `0x1004C350`;
  publishes `dword_10159BF8` and `dword_10159C04`; the body wraps the original call. The pattern
  tail needs one more builder pass (`docs/callback_group_analysis.md` §4).
- **HighQualityReflections** (callback `0x1001F390`, 0x3952 bytes + second callback
  `0x1001F030`, 0x360 bytes): pattern string + destination-VA math still undecoded; plan in
  `docs/callback_group_analysis.md` §6. Largest remaining item.
- **CutsceneFixes** (callback `0x10044160`): verified **no-op** in 7.65 (its slot is never
  published). Documented in `parity_and_improvements.md` §5 — nothing to implement.

## 5. Loader-arm audit results (session 8)

- 91 option keys are read by `sub_100523C0` (76 via `sub_1005C160`, 15 via reversed literals).
  All are present in the shipped INI except **`Patch7RemoveModChecks`** — the undocumented
  companion key that ORs into the Patch6and7 install set (aliased in `runtime.cpp`).
- All module/registry/filesystem imports are accounted for (see `HANDOFF.md` session 8).
- The only remaining zero-row option with no spec here is `NewMemorySystem` (excluded from the
  target: its handler needs a game-vtable reimplementation).
