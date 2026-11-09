# Option handler implementations — batch 2

Recovered from `ZolikaPatch_extracted/ZolikaPatch.asi` (read-only).
Patch site VA = signature match VA + operation offset (offsets can be negative: the
injector writes *before* the match, overwriting the bytes preceding the anchor).
`E9` sites store `dest-(site+5)`; the effective jump target (destination) is reported.
Game-side addresses were resolved against `/Users/kreato/Downloads/GTAIV.exe`
(1.0.8.0, image base 0x400000) with `tools/verify_game_signatures.py`.

All ten handlers live in the option loader `FUN_100523c0`. Each is invoked only when its
INI key (`[Options]`) evaluates to `1`.

| # | handler | option (INI default) | gate | kind |
|---|---------|----------------------|------|------|
| 1 | `1001afa0` | EmissiveLerpFix (`1`) | n/a | E9 jump |
| 2 | `1001b0e0` | EpisodeSwitchFreezeFix (`1`) | n/a | E9 jump |
| 3 | `1001b310` | TBoGTHeliHeightLimit (`0`) | build>=6 | NOP fill |
| 4 | `1001b380` | EpisodicWeaponSupport (`1`) | build>=6 | NOP fill |
| 5 | `1001b580` | EpisodicVehicleSupport (`1`) | build>=6 | NOP fill |
| 6 | `1001bdd0` | BikeFeetFix (`1`) | n/a | NOP fill |
| 7 | `1001bd60` | BikePhoneAnimsFix (`1`) | n/a | NOP fill |
| 8 | `1001bec0` | PoliceEpisodicWeaponSupport (`0`) | build>=6 | NOP fill |
| 9 | `1001be40` | PipeBombDropIconSupport (`1`) | build>=6 | NOP fill |
| 10 | `1001eca0` | FastLoading (`1`) | n/a | wrapper |

> `EpisodicWeaponSupport=1` is the INI key that calls `FUN_1001b380`;
> `EpisodicVehicleSupport=1` calls `FUN_1001b580` (both are distinct keys in the shipped INI).

---

## `FUN_1001afa0` — EmissiveLerpFix

- **INI key / default:** `EmissiveLerpFix` = `1` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** none recovered (handler body has no build-slot check)
- **Feature (one line):** Replaces the emissive-lerp code region with an E9 jump to a build-adaptive trampoline that scales emission by the current game speed.

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `F3 0F 58 C1 F3 0F 2A C8 F3 0F 58 C1` | -24 | 1 | E9 BA 23 82 0F | 0x7f8bb9 | 0x7f8ba1 |

Resolved game site: `0x7F8BA1` (match `0x7F8BB9` minus 0x18), original bytes
`F3 0F 2A C2 F3`; the injector overwrites the first byte with `E9` and stores the
displacement (effective jump target `0x1001AF60`). The handler also caches match-0xC and match-0x10 in
`DAT_10159cc8` / `DAT_10159cc4`.

---

## `FUN_1001b0e0` — EpisodeSwitchFreezeFix

- **INI key / default:** `EpisodeSwitchFreezeFix` = `1` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** none recovered (handler body has no build-slot check)
- **Feature (one line):** Hooks five episode-switch helper functions (episode object list/index/lookup) so switching episodes no longer freezes.

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `89 43 20 8B 10 8B 52 08 57 8D 4C 24 2C 51 8B C8 FF D2 83 F8 FF 89 43 0C 0F 84` | 0 | 1 | E9 F5 C3 BB 0F | 0x45ec46 | 0x45ec46 |
| `8B 4B 10 3B 4B 14 7D 0F` | 0 | 1 | E9 65 AA BB 0F | 0x4605f6 | 0x4605f6 |
| `8B 41 10 03 41 0C C3` | 0 | 1 | E9 BB AB BB 0F | 0x4604c0 | 0x4604c0 |
| `8B C1 8B 08 8B 11 8B 40 04 8B 52 30 50 FF D2 C3` | 0 | 1 | E9 8B AB BB 0F | 0x460510 | 0x460510 |
| `56 8B F1 83 7E 14 00 75 0B 83 7E 10 00 74 05` | 0 | 1 | E9 EB AB BB 0F | 0x4604d0 | 0x4604d0 |

Resolved game sites (each byte overwritten with `E9`; effective jump target shown):

| game match VA | site | effective jump target | full E9 bytes |
|---------------|------|-------------|---------------|
| `0x45ec46` | `0x45ec46` | `0x1001b040` | `E9 F5 C3 BB 0F` |
| `0x4605f6` | `0x4605f6` | `0x1001b060` | `E9 65 AA BB 0F` |
| `0x4604c0` | `0x4604c0` | `0x1001b080` | `E9 BB AB BB 0F` |
| `0x460510` | `0x460510` | `0x1001b0a0` | `E9 8B AB BB 0F` |
| `0x4604d0` | `0x4604d0` | `0x1001b0c0` | `E9 EB AB BB 0F` |

The handler caches match+5/+6/+7 in `DAT_10159bbc` / `DAT_10159bb4` / `DAT_10159bb8`,
used by the trampolines at `0x1001B040`..`0x1001B0D0`.

---

## `FUN_1001b310` — TBoGTHeliHeightLimit

- **INI key / default:** `TBoGTHeliHeightLimit` = `0` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** in-code guard: build >= 6 (cmp ds:0x1015be04,6; jl skip -> 5 < build)
- **Feature (one line):** Removes (NOPs) the build-conditional branch that disabled the TBoGT helicopter height limit.

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `83 3D ? ? ? ? 02 7C 0A F3 0F` | 7 | 2 | 90 90 | 0x8f0071 | 0x8f0078 |

Gate detail: the handler begins with `cmp DWORD PTR ds:0x1015BE04,6` / `jl <epilogue>`;
`0x1015BE04` is the recovered build slot (values 1,5,6,7,8,9,10,11,12,13,15,18 — see
`ZolikaPatch_decompiled/checkpoints/VERSION_SWITCH_DISASM.json`), so the body runs only
when the build ordinal is >= 6. For `EpisodicVehicleSupport` a stricter recovered gate
({8,9}) is also recorded in `evidence/option_build_gates.json`.

---

## `FUN_1001b380` — EpisodicWeaponSupport

- **INI key / default:** `EpisodicWeaponSupport` = `1` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** in-code guard: build >= 6
- **Feature (one line):** Removes episode-build guards in weapon/episode code (episodic weapon support).

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `83 3D ? ? ? ? 02 0F 8C D5 00` | 0 | 13 | 90 90 90 90 90 90 90 90 90 90 90 90 90 | 0x9d25d5 | 0x9d25d5 |
| `83 3D ? ? ? ? 02 7C 32 8B 41` | 7 | 2 | 90 90 | 0x4a9d3a | 0x4a9d41 |
| `75 5B 83 3D ? ? ? ? 00 75` | 0 | 2 | 90 90 | 0x4ab7c7 | 0x4ab7c7 |
| `83 3D ? ? ? ? 02 7C 7D 8B 4C 24 0C` | 7 | 2 | 90 90 | 0x997768 | 0x99776f |
| `83 3D ? ? ? ? 02 75 50` | 7 | 2 | 90 90 | 0xa61409 | 0xa61410 |
| `83 3D ? ? ? ? 02 0F 85 8C 01 00 00` | 7 | 6 | 90 90 90 90 90 90 | 0xaae8e2 | 0xaae8e9 |
| `83 3D ? ? ? ? 02 75 20 8B CD E8` | 7 | 2 | 90 90 | 0x4a6f1e | 0x4a6f25 |
| `83 3D ? ? ? ? 02 7C 21 8B 44 24 0C` | 7 | 2 | 90 90 | 0x9900a6 | 0x9900ad |

Gate detail: the handler begins with `cmp DWORD PTR ds:0x1015BE04,6` / `jl <epilogue>`;
`0x1015BE04` is the recovered build slot (values 1,5,6,7,8,9,10,11,12,13,15,18 — see
`ZolikaPatch_decompiled/checkpoints/VERSION_SWITCH_DISASM.json`), so the body runs only
when the build ordinal is >= 6. For `EpisodicVehicleSupport` a stricter recovered gate
({8,9}) is also recorded in `evidence/option_build_gates.json`.

---

## `FUN_1001b580` — EpisodicVehicleSupport

- **INI key / default:** `EpisodicVehicleSupport` = `1` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** in-code guard: build >= 6; recovered positive build gate (evidence): [["==", 8], ["==", 9]]
- **Feature (one line):** Removes episode-build guards across vehicle code (episodic vehicle support) and rewrites two per-build data pointers to route through the patch-table global.

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `83 3D ? ? ? ? 02 0F 8C 0E 05 00 00` | 0 | 13 | 90 90 90 90 90 90 90 90 90 90 90 90 90 | 0xa530fd | 0xa530fd |
| `83 3D ? ? ? ? 02 7C 35 0F BF 43 2E` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0x8fc61f | 0x8fc61f |
| `83 3D ? ? ? ? 02 8A 0D` | 0 | 7 | 90 90 90 90 90 90 90 | 0x4a86ee | 0x4a86ee |
| `83 3D ? ? ? ? 02 75 21 8B 96 20 08 00 00` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0x835771 | 0x835771 |
| `83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E` | 0 | 13 | 90 90 90 90 90 90 90 90 90 90 90 90 90 | 0x71c81a | 0x71c81a |
| `83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E` | 0 | 8 | 90 90 90 90 90 90 90 90 | 0x71c81a | 0x71c81a |
| `83 3D ? ? ? ? 02 7C 4A 0F BF 46 2E` | 0 | 8 | 90 90 90 90 90 90 90 90 | 0x71c81a | 0x71c81a |
| `83 3D ? ? ? ? 02 75 08 3B 05` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0x965823 | 0x965823 |
| `83 3D ? ? ? ? 02 75 1F 8B 44 24 04` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0x965d30 | 0x965d30 |
| `83 3D ? ? ? ? 02 75 38 8B 16` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0x989974 | 0x989974 |
| `83 3D ? ? ? ? 02 75 24 F6 87 6C 02 00 00 04` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0x98ac20 | 0x98ac20 |
| `74 17 83 3D ? ? ? ? 02 75 0E F3 0F 10 05` | 2 | 9 | 90 90 90 90 90 90 90 90 90 | 0x98c7de | 0x98c7e0 |
| `8B 56 04 8B 4A 0C 89 4C 24 1C 7C 18` | -15 | 7 | 90 90 90 90 90 90 90 | 0x9f21b8 | 0x9f21a9 |
| `83 3D ? ? ? ? 02 75 35 0F BF 46 2E` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0xa7a951 | 0xa7a951 |
| `83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0xaa704d | 0xaa704d |
| `83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0xaa704d | 0xaa704d |
| `83 3D ? ? ? ? 02 7C 26 8B 85 40 0B 00 00` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0xaa704d | 0xaa704d |
| `83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 0C` | 0 | 9 | 90 90 90 90 90 90 90 90 90 | 0xb20f3c | 0xb20f3c |
| `83 3D ? ? ? ? 02 F3 0F 10 05 ? ? ? ? F3 0F 11 44 24 20 75 4C` | 21 | 2 | 90 90 | 0x803035 | 0x80304a |
| `7C 22 83 F8 28 75 1D` | 0 | 2 | 90 90 | 0x801039 | 0x801039 |
| `83 3D ? ? ? ? 02 89 44 24 38` | 11 | 6 | 90 90 90 90 90 90 | 0x801273 | 0x80127e |
| `83 3D ? ? ? ? 02 7C 43` | 7 | 2 | 90 90 | 0x850f03 | 0x850f0a |
| `83 3D ? ? ? ? 02 7C 1F 8B` | 7 | 2 | 90 90 | 0x9d223e | 0x9d2245 |
| `83 3D ? ? ? ? 02 7C 11 8B 4E` | 7 | 2 | 90 90 | 0x9d22e9 | 0x9d22f0 |
| `39 35 ? ? ? ? 75 0E 80 3D` | 6 | 2 | 90 90 | 0xd49b09 | 0xd49b0f |

Two additional per-build rewrites are **not** signature-based. The handler reads the build
slot `DAT_1015be04` and, when it equals 8, patches two 4-byte cells at `module_base + RVA`
with the ASI pointer `&DAT_10158128`; when it equals 9 it patches a different pair:

| build | RVA (exe) | exe VA | value written |
|-------|-----------|--------|---------------|
| 8 | `0x21500` | `0x421500` | `0x10158128` |
| 8 | `0x7D91AE` | `0xBD91AE` | `0x10158128` |
| 9 | `0x95040` | `0x495040` | `0x10158128` |
| 9 | `0x684ABE` | `0xA84ABE` | `0x10158128` |

These are indirect-table slot rewrites (raw 4-byte pointer stores under `VirtualProtect`),
distinct from the NOP fills in the table above. Each pair is written only inside its own
`if (DAT_1015be04 == 8)` / `if (DAT_1015be04 == 9)` block; the whole handler is additionally
front-gated by the top-level `build >= 6` check.

In addition, the handler scans two patterns with `FUN_1005afa0`, which returns *all* matches
(unlike the first-match-only `FUN_10003830`), and NOP-fills each match:

| scan pattern | exe matches | fill |
|--------------|-------------|------|
| `7C 18 0F BF 4E 2E` | `0x8FEC12`, `0x8FEC46` | 8 bytes at (match − 6): `0x8FEC0C`, `0x8FEC40` |
| `83 3D ? ? ? ? 02 75 17 F3 0F 10 44 24 08` | `0xB1D8CB`, `0xB20EB7` | 9 bytes at match: `0xB1D8CB`, `0xB20EB7` |

The recovered `patch_operations` table records only the single-match sibling pattern
`... F3 0F 10 44 24 0C` (`0xB20F3C`, 9 bytes); the two multi-match fills above are read
directly from the handler disassembly and are **not yet present** in the operations table.

Gate detail: the handler begins with `cmp DWORD PTR ds:0x1015BE04,6` / `jl <epilogue>`;
`0x1015BE04` is the recovered build slot (values 1,5,6,7,8,9,10,11,12,13,15,18 — see
`ZolikaPatch_decompiled/checkpoints/VERSION_SWITCH_DISASM.json`), so the body runs only
when the build ordinal is >= 6. For `EpisodicVehicleSupport` a stricter recovered gate
({8,9}) is also recorded in `evidence/option_build_gates.json`.

---

## `FUN_1001bdd0` — BikeFeetFix

- **INI key / default:** `BikeFeetFix` = `1` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** none recovered (handler body has no build-slot check)
- **Feature (one line):** NOPs the guard that disabled bike foot animation.

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `39 82 54 13 00 00 0F 85 94 04 00 00` | 12 | 12 | 90 90 90 90 90 90 90 90 90 90 90 90 | 0xbcb919 | 0xbcb925 |

---

## `FUN_1001bd60` — BikePhoneAnimsFix

- **INI key / default:** `BikePhoneAnimsFix` = `1` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** none recovered (handler body has no build-slot check)
- **Feature (one line):** NOPs the guard that disabled bike phone animations.

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `80 BE 18 02 00 00 00 0F 85 36 01 00 00 80 BE` | 33 | 6 | 90 90 90 90 90 90 | 0xb67f01 | 0xb67f22 |

---

## `FUN_1001bec0` — PoliceEpisodicWeaponSupport

- **INI key / default:** `PoliceEpisodicWeaponSupport` = `0` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** in-code guard: build >= 6
- **Feature (one line):** NOPs a conditional so the police episodic-weapon path is taken.

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `39 2D ? ? ? ? 75 3E` | 6 | 2 | 90 90 | 0x49f18c | 0x49f192 |

Gate detail: the handler begins with `cmp DWORD PTR ds:0x1015BE04,6` / `jl <epilogue>`;
`0x1015BE04` is the recovered build slot (values 1,5,6,7,8,9,10,11,12,13,15,18 — see
`ZolikaPatch_decompiled/checkpoints/VERSION_SWITCH_DISASM.json`), so the body runs only
when the build ordinal is >= 6. For `EpisodicVehicleSupport` a stricter recovered gate
({8,9}) is also recorded in `evidence/option_build_gates.json`.

---

## `FUN_1001be40` — PipeBombDropIconSupport

- **INI key / default:** `PipeBombDropIconSupport` = `1` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** in-code guard: build >= 6
- **Feature (one line):** NOPs the icon-Type comparison so pipe-bomb pickup grants a drop icon.

### Signature strings and writes

| signature | op offset | len | written bytes | ASI match VA | game patch site (VA) |
|-----------|-----------|-----|---------------|--------------|----------------------|
| `83 7D 18 1C 75 14` | -9 | 9 | 90 90 90 90 90 90 90 90 90 | 0x4a6f50 | 0x4a6f47 |

Gate detail: the handler begins with `cmp DWORD PTR ds:0x1015BE04,6` / `jl <epilogue>`;
`0x1015BE04` is the recovered build slot (values 1,5,6,7,8,9,10,11,12,13,15,18 — see
`ZolikaPatch_decompiled/checkpoints/VERSION_SWITCH_DISASM.json`), so the body runs only
when the build ordinal is >= 6. For `EpisodicVehicleSupport` a stricter recovered gate
({8,9}) is also recorded in `evidence/option_build_gates.json`.

---

## `FUN_1001eca0` — FastLoading

- **INI key / default:** `FastLoading` = `1` (section `Options`)
- **Called from:** `FUN_100523c0` at the loader dispatch for this key.
- **Build gate:** none recovered (handler body has no build-slot check)
- **Feature (one line):** No patch site: forwards to FUN_1001ec90, which dispatches an indirect call through the lazily-initialized function pointer DAT_1015e840.

### Signature strings and writes

No signature strings and no protected writes. The handler body is:

```asm
1001eca0: 55                push ebp
1001eca1: 8b ec             mov  ebp,esp
1001eca3: e8 e8 ff ff ff    call 1001ec90
1001eca8: 5d                pop  ebp
1001eca9: c3                ret
```

`FUN_1001ec90` is `pusha; mov eax,ds:0x1015e840; mov eax,[eax]; call eax; popa; ret` —
an indirect call through the pointer global `DAT_1015e840`.

**Unresolved:** `DAT_1015e840` is initialised to `0` in the on-disk image and the only
reference to it is the `lea` at `0x1001ec91`; no store to `0x1015e840` exists in `.text`,
and no relocation targets it, so its runtime value cannot be recovered statically from the
ASI. The runtime target is therefore **unknown**. The feature name (FastLoading) and the
fact that this is the only handler with no signature string are recorded, not explained.

---
