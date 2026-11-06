# Rebuilt pieces: the least-proven code in the tree

Every other piece of this module is **transcribed**: a body is written instruction for
instruction from the original ASI, and `tools/verify_bodies.py` decodes the same
instruction count on both sides and compares mnemonic, registers, memory shape and
immediate *class*. That proof is what `tests/bodies.tsv` is for, and it currently
covers **131 bodies**.

This file is about the pieces that are **not** in that file. They are rebuilt in C++
because their original instruction stream cannot be compared under the proof's rules,
or because every call they make lands in another rebuild. They are the weakest
evidence in the project: for each of them the tree ships code that *behaves like* the
original in the cases that were reasoned about, not code that *is* the original.

The catalogue of preserved faults and per-key behaviour stays in
[`parity.md`](parity.md); this file owns the question "how much of this is proven?".

---

## 1. What "cannot be transcribed" means here

The comparator (`tools/verify_bodies.py::summarise`) reduces each instruction to

    mnemonic  reg:<name>…  imm  [base+index*scale+disp]  abs

with three consequences that decide every case below:

| operand form | compared as | consequence |
|---|---|---|
| register, or `[ebp+disp]` / `[esp+disp]` / `[reg+disp]` | the displacement **literally** | a transcription must use the same frame offsets, which it does because it writes the same instructions |
| memory with **no base and no index** (`[0x1015AD94]`) | `abs` — a slot | the module may point it at its own global or slot |
| `call`/`jmp`/`push` immediate | `imm` — a slot | the module may name its own function or literal |
| memory **with an index and no base** (`[esi*4 + 0x1015A538]`) | the displacement **literally** | the module cannot match it: the table lives at the original's VA inside the original ASI |

So the blockers are mechanical, and there are exactly four kinds in this tree:

- **A · indexed absolute table access.** The original's 512-slot model tables
  (`0x1015A538`, `0x10159D38`, `0x10159380`, `0x1015CE28`, `0x1015C628`, `0x1015B5C0`,
  the 38,455-entry name table `0x100DE770`) are read as `[reg*4 + 0x10…]`. A rebuilt
  module cannot place a table at the original's VA, and the comparator compares that
  displacement literally, so the walk cannot be proved — and writing it as
  `[base + reg*4]` would change the instruction shape and fail for a different reason.
- **B · exception frame.** A body wrapped in `__try/__except` (`__except_handler4`) or
  in a C++ static-local initialisation (`__CxxFrameHandler3`) is two or three code
  fragments: the registration prologue writes `fs:[0]`, the handler lives in a
  *separate* funclet with its own address, and a scope table / throw-info record in
  `.data` decides what the frame does. The funclet's bytes are not in the linear
  stream, and the table is not an instruction at all. For `0x100038E0`/`0x10003980`
  the frame *is* the feature: the `__except` is what answers 0 instead of crashing on
  a null native handler.
- **C · TLS static-init guard.** A function-local `static` in the original is
  initialised through `_Init_thread_header` / `_Init_thread_footer`
  (`0x1005FFC3` / `0x1005FF77`), which reach the CRT's per-thread block through
  `fs:[2Ch]` and the image's `TlsIndex` word. The guard's data (`dword_101A84C8`
  and friends), the abort funclet and the TLS index are image-internal state a rebuilt
  module cannot name.
- **D · call graph of rebuilds.** The body itself is comparable, but every function it
  calls is a rebuild, so a transcription would prove the branch shape and none of the
  behaviour. This is why the OOC archive builder and the NewFiles updates are still
  rebuilt: their callees are class A–C.

Two things that are **not** blockers, and used to be treated as one:

- **`/GS` frames.** `mov eax,[__security_cookie] / xor eax,ebp` and
  `call __security_check_cookie` are ordinary instructions: the cookie is an absolute
  slot and the check is an immediate slot. The tree has transcribed `/GS` bodies since
  before this pass (`ooc_wrap`, `hqr_body`, `bad_player_model_*`), and this pass adds
  three more (`new_saves_read_hook`, `new_saves_second_hook`, `dlc_discovery`).
  "It is compiler output" is not a reason.
- **SSE double conversions, `cmov`, the `lahf/test ah,44h/jnp` float-`!=` idiom.**
  They are plain instructions. `0x10004C70` is *not* blocked by them (see A).

---

## 2. What the transcription passes did, and what they removed

### The first pass (six bodies)

Six bodies became proven, and five C++ rebuilds disappeared. `tests/bodies.tsv` rows
are the proof; the byte column is from `.scratch/byte_compare.py`, which decodes the
same instruction count on both sides and classifies every differing instruction.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| bounded formatter, wrapper | `0x10001280` | `0x1C` | 13 | `bpm_format` | 12/13 | 1 `call` immediate |
| bounded formatter, engine | `0x10001240` | `0x33` | 21 | `bpm_format_engine` | 19/21 | 1 `call` immediate + 1 `call [slot]` |
| bounded formatter, options | `0x10001230` | `0x06` | 2 | `bpm_format_options` | 1/2 | 1 `mov eax, offset` |
| save-load read hook | `0x10051A90` | `0x39` | 20 | `new_saves_read_hook` | 17/20 | 3 absolute operands |
| save-load second hook | `0x10051AD0` | `0x5E` | 33 | `new_saves_second_hook` | 26/33 | 7 absolute operands |
| DLC discovery | `0x10023150` | `0x32A` | 223 | `dlc_discovery` | 189/223 | 34 absolute/immediate, 0 branch |

Every instruction that is not byte-identical differs in an absolute operand (a slot)
or a `call`/`push` immediate. The bodies' instruction sizes sum to the original's
exactly (810 bytes for the DLC body on both sides), so every instruction sits at the
same relative offset — which is what makes the next check possible.

**A defect the proof does not catch, found and fixed in this pass.** The first
`dlc_discovery` transcription put `dlc_tlad_exists` / `dlc_tbogt_exists` on the
`mov ecx,[game_base]` of the register block, while the original's `jne` lands on the
*second* `PathFileExistsA` probe: a "content" folder that exists is probed once more,
and a failed second probe still skips the registration. The proof passed anyway,
because a `jcc` immediate compares as a slot — the *target* is never compared. The byte
comparison exposed it as two `jne` displacements 18 bytes off. The fix moved both
labels onto the probe instruction, and the check that now guards it compares every
*branch's target offset*:

```
python .scratch/branch_targets.py
131 row(s), 206 internal branch target(s), 0 failure(s)
```

The DLC row was the only failure before the fix; after it, every row agrees on
every internal branch target (154 of them for the 125 rows that existed then; the
six rows §2a adds bring the current total to 206). That check lives in
`tools/verify_bodies.py` now (§4 item 1), so the count moves with the row set.

**The C++ rebuilds this removes**, and the source of the replacement:

| rebuild that is gone | where it was | replaced by |
|---|---|---|
| `BpmFormat` (`_vsnprintf_s`, 3 copies: `BpmFormat`, `NoliveryFormat`, `OocFormat`) | `stability.ixx`, `vehicles.ixx`, `ooc.ixx` | the one transcribed body in `stability.ixx`; the other two modules keep a six-byte `jmp [zpatch_format_entry]` forwarder so their `call` stays an immediate |
| `NewSavesReadHook` (C++, `std::array` scratch) | `loading.ixx` | `new_saves_read_hook` |
| `NewSavesSecondLoadHook` (C++, two `std::array`s) | `loading.ixx` | `new_saves_second_hook` |
| `DlcDiscovery` (C++, `strcpy_s`/`strcat_s`/`PathFileExistsA` + lambdas) | `loading.ixx` | `dlc_discovery` |
| `BuildEpisodeSuffix` (hand-written digit loop) | `loading.ixx` | the original's `call 0x10001280` with `"\\Episode%d\\"` |

**How the upgrade was verified**

1. **Proof rows.** `python tools/verify_bodies.py --original … --candidate … --bodies
   tests/bodies.tsv --map …` → `125 body(ies), 0 failure(s)` for the row set of that
   pass (131 today), including
   `PASS dlc_discovery (223 instruction(s), 23 absolute operand(s))`. The count is the
   body's exact count, so a missing or extra instruction fails the row.
2. **Byte comparison.** The table above: every difference is an absolute operand or a
   `call`/`push` immediate — never an opcode, a register, a frame offset or (after §2's
   label fix) a branch.
3. **The slots hold what the original's operands point at.** `zpatch_format_entry`,
   `zpatch_bpm_stdio_common_vsprintf` and the five DLC API slots are filled from this
   module's own imports in `RegisterStability()` / `RegisterLoading()`, before any
   tier runs. They are module slots because the static CRT has no IAT — the
   instructions keep the original's `call ds:[…]`, and the target is the same function
   the original imported (`GetModuleFileNameA`, `PathRemoveFileSpecA`,
   `PathFileExistsA`, `strcpy_s`, `strcat_s`, `__stdio_common_vsprintf`).
4. **The formatter's semantics were probed, not assumed.** The original's engine call
   is `__stdio_common_vsprintf(options = { 0, 0 } | 2, buffer, size, format, nullptr,
   arg_list)`; the rebuild it replaced was `_vsnprintf_s(…, _TRUNCATE, …)`. A probe
   (`/MT`, x86) of the two against the real format the DLC pass uses:

   ```
   episode 254      size=64 rebuilt=  12 shipped=  12 bytes_same=1 text="\Episode254\"
   episode 3        size=64 rebuilt=  10 shipped=  10 bytes_same=1 text="\Episode3\"
   truncate at 8    size= 8 rebuilt=  14 shipped=  -1 buffer "\Episod\0" in both
   truncate at 5    size= 5 rebuilt=   5 shipped=  -1 buffer "abcd\0"  in both
   exact fit        size= 7 rebuilt=   6 shipped=   6 bytes_same=1 text="abcdef"
   ```

   The **buffer contents are identical in every case**, including truncation. The
   return value differs on truncation — the original's engine answers the untruncated
   length, `_vsnprintf_s` answers `-1` — and no transcribed caller reads the answer
   (each one discards `EAX` after the call), so the old rebuild was not a bug, only an
   equivalent that returned a different unused value. The transcription removes even
   that difference.
5. **The surrounding behaviour is still measured.** The write sets of every key are
   unchanged (`python tools/parity_report.py 9` → `98 key(s) closed, 0 open, 1
   oracle-verified`), `ctest --preset release` is 12/12, the OOC fingerprints still
   pass (`zp_dump --ooc-selftest` → `ooc selftest: PASS`), and the hook *sites* those
   bodies sit on are the sites the arms were already proven to write.
6. **The linker did not redirect a call.** A `call` compares as an immediate in the
   proof, so it cannot see *which* helper a transcription calls; `.scratch/resolve_calls.py`
   decodes the built bodies and maps every emitted call and absolute operand back to
   the linker map. It confirms `call BpmFormatEngine` → `?BpmFormatEngine@@YAXXZ`,
   `call BpmFormatOptions`, `call DlcAlloc` → `?DlcAlloc@?A0x…`, `call DlcFormat`,
   `call __security_check_cookie` → `@__security_check_cookie@4` (LIBCMT's own, the
   routine the original calls), the engine slot → `_zpatch_bpm_stdio_common_vsprintf`,
   the DLC slots → the five `zpatch_dlc_*` words, and the options body's immediate →
   `_zpatch_bpm_format_options`. Two `/OPT:ICF` folds show up and are harmless: the
   three six-byte `jmp [zpatch_format_entry]` forwarders are identical, so one survives
   and all three names resolve to it, and `DlcZeroBlock` is identical to audio's
   `AudioZeroBlock`, so the DLC body's `memset` call lands on the audio module's copy.
   Same instruction sequence, same ABI, same `memset`.
7. **Every internal branch lands where the original's does.** The proof's jump-target
   blind spot is closed for these rows by comparing each `jcc`/`jmp` target's offset
   from the body start on both sides (`.scratch/branch_targets.py`, now inside
   `tools/verify_bodies.py`). This is the check that caught the first DLC
   transcription's mis-attached label (§2); it passes for the 125 rows of that pass
   (154 internal targets) and for the 131 rows of today's set (206).

**What remains unproven even for these six.** The bodies now execute only on the
game's own calls, and no offline harness can make the game call them:

- `0x10001280` runs when the livery helpers, the OOC wrapper or the DLC pass format a
  path. The probe above exercises the engine, not the transcribed frame; a wrong
  frame offset would corrupt the caller's stack and would only show in play.
- `0x10051A90`/`0x10051AD0` run when the game loads a savegame. Their slot values can
  only be non-zero on a run whose three save sites matched, which the dump proves
  happens, but the read itself (a 0xE78-byte block into the hook's own scratch) has
  never executed here.
- `0x10023150` runs on every DLC-manager initialisation. Its two probes and the
  episode loop only walk real directories; the tree has no DLC folder to point them at.

That is the same residual as any transcribed body: the proof is of *code*, the
behaviour is seen in play. `docs/parity.md` carries the per-key play items.

### 2a. This pass: six more bodies

The pieces §4 of the previous revision listed as **transcribable today** are
transcribed. They were left rebuilt only because that pass did not own the modules
they live in, and one of them (`0x10004820`) carried a blocker that is not in the
binary at all — see the note under the table.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| OOC decoder, opcode step | `0x1004B150` | `0x41F` | 406 | `ooc_step` | 357/406 | 46 absolute operands + 3 `call` immediates |
| OOC decode driver + Adler-32 | `0x1004B570` | `0x1D7` | 159 | `ooc_decode` | 149/159 | 8 absolute operands + 2 `call` immediates |
| DFA create-file body | `0x10004820` | `0x2F` | 19 | `dfa_create_file` | 17/19 | 2 absolute operands |
| DFA SetFilePointer body | `0x10004760` | `0x17` | 9 | `dfa_set_file_pointer` | 8/9 | 1 absolute operand |
| DFA GetFileSizeEx body | `0x10004720` | `0x11` | 7 | `dfa_get_file_size_ex` | 6/7 | 1 absolute operand |
| DFA ReadFile body | `0x10004740` | `0x1A` | 10 | `dfa_read_file` | 9/10 | 1 absolute operand |

Every difference is again an absolute operand (a slot) or a `call` immediate — no
opcode, register, frame offset or branch. The OOC pair's instruction sizes sum to
1055 and 471 bytes, the original's exact sizes, so every instruction sits at the same
relative offset and the internal-branch check passes for both (the opcode step alone
has 36 internal branches, the driver 15).

**One correction to the previous revision's premise.** `0x10004820` was filed under
class C with "`__try/__except` (`fs:[0]` registration + funclet)" as its blocker. It
has no exception frame: the original is nineteen instructions, `push ebp; mov ebp,
esp;` seven argument pushes with `mov eax, ds:10159D30` in the middle, `test eax,
eax; je` and either `mov eax, [eax]; call eax` or `call ds:[CreateFileA]`, then `pop
ebp; ret` at `0x1000484E`. Its two absolute operands are the captured slot address
and the `CreateFileA` import, both slots here. Under this file's own rule — a body is
only rebuilt for an indexed absolute table access, an exception frame, a TLS
static-init guard or a call graph of rebuilds — it had no reason to stay rebuilt, so
it is transcribed too. `0x10004790` (close-handle) *does* have the frame (`push -2;
push 0x10153FC0; push __except_handler4; … mov fs:[0], eax`, the filter at
`0x100047EC` and the unwind block at `0x100047F0`, 45 instructions in `0x81` bytes)
and stays a rebuild.

**How this pass was verified**

1. **Proof rows.** `python tools/verify_bodies.py …` → `131 body(ies), 0 failure(s)`,
   with `PASS ooc_step (406 instruction(s), 46 absolute operand(s))`, `PASS
   ooc_decode (159, 6)`, `PASS dfa_create_file (19, 2)`, `PASS dfa_set_file_pointer
   (9, 1)`, `PASS dfa_get_file_size_ex (7, 1)` and `PASS dfa_read_file (10, 1)`.
2. **Byte comparison** (`.scratch/byte_compare.py`, above): 546 of the 610 new
   instructions are byte-identical; the other 64 differ only in a slot or a `call`
   immediate.
3. **The slots hold what the original's operands point at.** `.scratch/resolve_calls.py`
   maps every emitted operand back to the linker map: the four DFA bodies call through
   `_zpatch_dfa_create_file` / `_zpatch_dfa_set_file_pointer` /
   `_zpatch_dfa_get_file_size_ex` / `_zpatch_dfa_read_file`, which `RegisterLauncher()`
   fills with `CreateFileA`, `SetFilePointer`, `GetFileSizeEx` and `ReadFile`; the
   create-file body reads `_zpatch_dfa_create_file_slot` (the address the publish
   tier's IAT capture recorded, the original's `dword_10159D30`); the decoder reads and
   writes `_zpatch_ooc_begin` / `_zpatch_ooc_cursor` / `_zpatch_ooc_end` /
   `_zpatch_ooc_lower`; and `ooc_decode`'s two `call`s land on `?OocStepBody@@YAXXZ`.
4. **The decoder's three `call`s are the CRT's `memcpy`.** The inline assembler
   refuses `call memcpy`, so each site calls `OocCopyBlock`, and `/OPT:ICF` folds it
   into `loading.ixx`'s identical `SavegameCopyBlock` — same three arguments, same
   caller-cleans `add esp, 0Ch`, same `memcpy`. The call still compares as an immediate,
   so the proof is unaffected.
5. **The behaviour is pinned, not assumed.** `zp_dump --ooc-selftest` runs the
   transcribed pair against the recovered resource and still prints `ooc selftest:
   PASS`: the decoded table's `fnv1a 0xC883859F`, the archive's 182,272 bytes and
   `fnv1a 0xB5AF7B10`, and the header's `16`/`0xE9` words. That is a *behavioural*
   proof of a 406-instruction interpreter, not only a shape proof.
6. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1 oracle-verified`,
   and the DFA install rows still write the same five bytes at the same sites (the
   branch targets canonicalise to the original's VAs).

**What stays rebuilt in these two areas, and why.** The OOC archive builder
(`0x1004ACE0`, class D: twelve helper callees), the init (`0x1004B750`, EH + C++
containers), the key's handler (`0x1004C3F0`, EH4 + a 16 KB string builder) and the
DFA close-handle body (`0x10004790`, EH4) — every one of them for a reason this file
lists. The OOC pair's behaviour stays pinned by the selftest; the wrapper and the
branch body were already transcribed.

**What remains unproven for these six.** The decoder pair is now behaviourally proven
offline (item 5), which is more than any other body here; the DFA forwarders are only
shape-proven — they run when the game opens a file through its DFA layer, and a wrong
argument order would show as a file-open failure in play, not as a proof failure.

---

## 3. Every piece that is still rebuilt

`insns` is the linear sweep over the function's own bytes, i.e. what a proof row would
have to cover. IDA's per-function total is sometimes larger because it also counts the
out-of-line EH funclet / thread-abort blocks that belong to the function but do not sit
in its byte range; where the two differ, the total is given in the row's text. `bytes`
is IDA's function size.

### Class A — indexed absolute table operand

| piece | VA | bytes | insns | the operand that blocks it | what the rebuild reproduces | how that was verified | residual risk |
|---|---|---:|---:|---|---|---|---|
| model-name lookup | `0x10003F00` | `0x3E` | 21 | `push ds:off_100DE770[esi*4]` (`0x10003F10`) and `mov eax, ds:off_100DE770[esi*4]` (`0x10003F34`) — the 38,455-entry name table in the ASI's `.data` | walk index 0 → 0x9637, hash each name with the transcribed resolver `0x10003E50`, answer the first match or `"**UNKNOWN**"`; the table order is the recovered `source/model_names.inc` | the table and the loop are exercised by `zp_model_tables` (the shipped INI's 78 offset rows resolve through the same resolver); the *lookup* itself only runs on a BadPlayerModel miss | a name in the table that the original would answer but the rebuild skips (or the reverse) would change only the diagnostic text; low impact |
| steer/render consumer | `0x10004C70` | `0x53F` | 320 | `mov eax, ds:[esi*4+0x1015A538]` (`0x10004CD0`) and `movss xmm0, ds:[esi*4+0x10159D38]` (`0x10004E00`) — the wheelangle model/angle tables | the five keys behind it: steer lock → wheel angle, steering-wheel bone transform, engine-block shake, plane parts, plus the `GarageModelFix`/`NoLiveryLimit` call paths. Every game entry is a published slot, every constant a `.rdata` qword; the rebuild keeps the statement order, the state words and the unguarded reads | the five keys' per-option cells are byte-identical to their baselines on all nine images (`parity_report`, `dump_matrix`); the tables it walks are the ones the transcribed IDE hook fills (`zp_model_tables`) | **highest in this class**: it runs on every frame the player drives, and a wrong wheel-angle index or unit conversion would show as a wrong-looking steering wheel, not a crash; nothing offline pins its arithmetic |
| copsirens checker, first siren | `0x10005F70` | `0x2EE` | 191 | `push ds:[edi+0x10159380]` (`0x100061FA`), `cmp ds:[edi+0x10159380],0` (`0x1000622B`) | 14 hard-coded model names hashed in order, each looked up through `dword_10159C0C`; the tunable list walked slot by slot; `dword_10159B80` set to 1 on the first hit | `CopSirensFlag`'s cell is closed on every image; the hook sites it shares with the IDE hook are transcribed | moderate: a miss leaves siren flags off (cosmetic) or on (cosmetic), it does not fault |
| copsirens checker, second siren | `0x10006260` | `0xAE` | 53 | `push ds:[esi+0x10159380]` (`0x100062BC`), `cmp ds:[esi+0x10159380],0` (`0x100062DB`) | `"police2"` always matches (that is the key), then the same list walk; `dword_10159378` is the result the other checker reads | as above | low |
| IDE parser payload | `0x10003FA0` | `0x13A` | 92 | four indexed stores: `0x1015CE28[ecx*4]`, `0x10159380[ecx*4]`, `0x1015A538[ecx*4]`, `0x10159D38[ecx*4]` | the `popuplights` / `siren` / `wheelangle:<deg>` parser hook's payload: it recognises the key, hashes the model name into a 512-slot list and (for `wheelangle`) converts degrees and stores the float at the same index | the four tables are exported as slots (`zpatch_wheelangle_models`/`_angles`) and the transcribed steer consumer reads the same arrays; `zp_model_tables` drives the reader path | moderate: a wrong slot index here mis-aims a wheel angle for one model |
| popup-headlights checker | `0x10044310` | `0x216` | 125 | eight: `0x1015CE28[esi*4]` in the walk, `0x1015C628[eax]`…`0x1015C644[eax]` (its own 512×8 table) and `0x1015BE28[eax]`…`0x1015BE44[eax]` (the floats) | reset the angle to π/2, resolve the object's model word once, and copy the eight per-model popup floats the `[PopupLightAngles]` reader stored | `PopupHeadlightsFlag`'s cell is closed on every image; the `[PopupLightAngles]` reader is the transcribed `sub_100523C0` (`zp_model_tables`) | low: eight floats that are only read by the popup-light shims |
| headlight-cone offset check | `0x1001AD20` | `0x8B` | 46 | `mov eax, ds:[esi*4+0x1015ADBC]` (`0x1001AD6E`), `movss xmm0, ds:[esi*4+0x1015B5C0]` (`0x1001AD8A`) | seed 2.0f, walk the `[HeadlightConeOffsets]` hashes from slot 0 to the first zero, look each up through `dword_10159C0C` and compare with the object's signed word at `+0x2E`, then store that slot's float | `ForceCarHeadlightShadows`' cell is closed; the arm's shim (`0x1001ADB0`, 9 instructions) is transcribed and proven | moderate: it reads *one word past its 512-hash table* in the original (the `.data` that follows), which the rebuild cannot reproduce; a fully-filled 512-row section would diverge (no shipped INI comes close) |

### Class B — exception frame

| piece | VA | bytes | insns | the frame that blocks it | what the rebuild reproduces | how that was verified | residual risk |
|---|---|---:|---:|---|---|---|---|
| guarded resolve-and-invoke | `0x100038E0` | `0x9E` | 59 | `__try/__except`: `push -1; push 0x10153EE8; push __except_handler4; … mov fs:[0],eax`, two funclets (`mov eax,1; ret` and the unwind path) | resolve a native handler by hash, call it with the one argument, answer `true`; a fault in the call is caught and answered `false` — that is the feature | the *bodies* of the native-call layer that call it are transcribed (`audio_call_*`, `bad_player_model_*`); its consumers' cells are closed; the fault path is unreachable offline | high: the `__except` is the whole point. If the rebuild's frame caught a different range, a game fault would either crash (original: answered 0) or be swallowed (original: crash) |
| guarded resolve-only | `0x10003980` | `0x90` | 52 | same, scope table `0x10153F08` | resolve only, answer the handler pointer or 0 | as above; called from the transcribed steer consumer, the popup shims and `MouseFix`'s helpers | high, same reason |
| DFA close-handle body | `0x10004790` | `0x81` | 45 | `__try/__except` around `CloseHandle`: `push -2; push 0x10153FC0; push __except_handler4 (0x1005FE4C); … mov fs:[0], eax`, with the filter (`or eax, -1; ret` at `0x100047EC`) and the unwind continuation (`0x100047F0`) inside the same byte range | `CloseHandle` behind the guard, answering `-1` when the call faults | the DFA stage's write set and the launcher cell (`oracle-verified`); the body runs when the game closes a file through DFA | low: a close that faults is caught either way. The frame is the feature, so it stays rebuilt; the four forwarders beside it are transcribed (§2a) |
| SpeedupBugFix body | `0x10050540` | `0x16F` | 93 (+funclet at `0x10061981`) | C++ static-init EH: `push -1; push SEH_10050540; … fs:[0]`, funclet `jmp __CxxFrameHandler3` with throw info `0x10154358`, plus the `_Init_thread_header/footer` TLS guard (`0x1005FFC3`/`0x1005FF77`) around the one-time QPC/scan | the clock fix: QPC frequency → one-over-frequency into eight published float slots, then tail-call `sub_10003830`'s resolver result − 0x33 (the game's timer getter) and return its `(eax,edx)` pair in a 64-bit answer | the arm's eight operands are validated per site and the per-key dump is byte-identical to the pre-change dump on all nine images; the *body* never runs offline | moderate: the eight timer slots are the whole fix. A wrong slot or a float/double mix-up would show as a broken game clock, not a crash |
| audio install body | `0x10050AD0` | `0xF9` | 75 | EH4 (`push __except_handler4`, scope `0x10154384`, funclet `0x100619C1`) + the CRT clock/seed block through `fs:[2Ch]` and `TlsIndex` | resolve the `6A 06 68 ? ? ? ? 8D 8C` site once and write `rand() % 5 + 1` into its `push` immediate, gated on the transcribed `0x10050A50` gate | `RestoreDeathMusic`'s cell is closed; the gate and the three native wrappers around it are transcribed and proven | **the one recorded 1:1 divergence in that key**: the original's guard compares `dword_101A84C0` with the CRT per-thread word and may skip the whole block on a fresh process; the rebuild always writes. Recorded in `parity.md` §RestoreDeathMusic |
| save converter | `0x10051B30` | `0x489` | 337 | EH registration (`fs:[0]`) + three `_Init_thread_header/footer` TLS guards + CRT file API (`fopen_s`, `fread`, `fclose`, `operator new`, `free`) + `std::string` (IDA's per-function total is 363: 337 plus the out-of-line funclet/abort blocks) | open the save, check `SAVE` at +0xC, walk eighteen chunks with `fread(9)` / `operator new(size-9)` / `fread(payload)` / `free`, remember chunks 9/13/16 (`0x4C45`/`0x1741`/`0x0E77`), then patch the three sites (`0x872185`, `0x7D8C50`, `0x8DA090`) | the file walk was replayed offline against the two real saves in `game_refs/saves/…` (`SGTA400`/`SGTA401`: `SAVE` at +0xC, 18 `BLOCK` chunks, sizes `0x3AB1`/`0x1729`/`0x0CC7`); the three sites' writes are what the key's cell measures | **high**: a corrupt save already faults (preserved), and the chunk-size conditions decide whether the new-format hooks are installed. A wrong condition converts a save wrongly instead of failing |
| save prepare | `0x10051FD0` | `0x202` | 157 | same EH/TLS + `std::string` construction + the game's save-directory call | ask the game for the save directory, build `"<dir>\\<save name>"` as a `std::string`, call the converter | the directory call site is one of the key's six recovered scans; the *string* shape is not measured offline | moderate: a wrong path means the converter opens nothing (silent no-convert) or the wrong file |
| OOC init (decode → register → build) | `0x1004B750` | `0xBFF` | 991 | EH (`fs:[0]`) + C++ container helpers (`0x10058620`, `0x1005CF20`) + `operator new` | decode the pinned resource, register it as the single fake-pak entry in the module's own table (`0x1016E43C`), then build the 182,272-byte archive | **proven end to end offline**: `zp_dump --ooc-selftest` checks the stream's Adler-32 trailer, the decoded table's `fnv1a 0xC883859F`, the archive's 182,272 bytes and `fnv1a 0xB5AF7B10`, and the header's `16`/`0xE9` words → PASS | low for the transform (fingerprints), **high** for when it runs: the original re-runs it on every call and leaks the previous archive; the rebuild keeps that |
| OOC handler (the key's arm) | `0x1004C3F0` | `0x3FB7` | 4185 (IDA's total 4199) | EH4 + a 16 KB C++ string builder (digit loops, `strtol`, `toupper`) | scan the branch anchor, publish four words, `VirtualProtect` and write the `E9`; scan the wrapper anchor, publish the mount/original call targets and write the `E8` | the arm's four published words and two writes are what the key's cell measures; both written bodies (`0x1004C3C0` 7, `0x1004C350` 41) are transcribed and proven | low: the handler only publishes and writes; a wrong word would show as a wrong mount target |

### Class C — TLS static-init guard

| piece | VA | bytes | insns | what blocks it | what the rebuild reproduces | how verified | residual risk |
|---|---|---:|---:|---|---|---|---|
| NewFiles container scan | `0x10051730` | `0xB0` | 55 | TLS static-init guard (`fs:[0]`, `fs:[2Ch]`, `_Init_thread_header/footer`) around the one-time find of the game's registry global | find the game's `mov esi,<global>` once, dereference the global on every call (the game rebuilds the registry) | the NewFiles arm's writes are the key's cell; the scan result is used by the two rebuilt update bodies | moderate: a miss returns a null the update bodies then call or dereference — the original's fault, preserved |
| NewFiles "is registered" | `0x10051610` | `0x114` | 74 | two TLS guards (two build variants cached) | ask the container whether the hash is registered, trying one signature and then an older build's | as above | moderate |
| NewFiles "append" | `0x100517E0` | `0xB0` | 53 | TLS guard + a call into the game function 20 bytes inside the match | append the name to the container | as above | moderate |
| ROM message service | `0x10004260` (class registration) + `0x10004300` (registrar, `0xC8`) + the window procedure and thread entry they store | — | — | not one instruction stream at all: a thread, a window class, a `WNDPROC`, a message-only window and a `WM_ROM` action table — process-wide state reached only through pointers the original stores | a `std::thread` that registers `"MessageWindow"` (falling back to `"MessageWindow2"`), creates the message-only window, publishes the handle from `WM_NCCREATE` and answers `WM_ROM` (43858) with the recovered action table; `StartRomService` spins on `Sleep(0)` with no bound, as the original does | its *install rows* are transcribed (`rom_set` 4 instructions, `rom_reset` 28, `dfa_init` 2) and the launcher cell is oracle-verified; the service itself is only observable in a running game | low: nothing depends on it except the ROM message window; a failure to create the window hangs the game in both trees (a preserved fault) |

`0x10004300` is the only function in the ASI that references `"MessageWindow"` /
`"MessageWindow2"`; it calls the class registration twice (primary, fallback) and
sets `byte_1015AD9B` on failure. Its shape is transcribable in isolation, but the
WNDPROC and the thread entry it stores are separate functions, so a transcription of
the registrar alone would prove nothing about the service.

### Class D — call graph of rebuilds

| piece | VA | bytes | insns | what it calls | why that keeps it rebuilt | residual risk |
|---|---|---:|---:|---|---|---|
| OOC archive builder | `0x1004ACE0` | `0x461` | 314 | 12 internal helpers (`0x1004AB70`…, `0x10056500`) that are themselves rebuilds | class D: proving the caller of 12 rebuilds proves nothing behavioural | low: the archive is fingerprinted (`fnv1a 0xB5AF7B10`) |
| NewFiles updates (2) | `0x10051890`, `0x100518D0` | `0x3F`, `0x45` | 30, 30 | the three class-C scans above + the transcribed resolver `0x10003E50` | their own stream is transcribable, but three of four callees are class C and two of the four need `__fastcall` entry shims (`ECX` = object/list, stack = hash/name) whose register contract would itself be unproven — that is the register-contract bug class, not a proof. §4 item 1 | moderate: it decides whether a new-format file is registered at all |
| NMS arena methods | `0x10038360` (alloc), `0x10038430` (free), `0x100385A0` (contains) | `0xC2`, `0x10E`, `0x7D` | 69, 91, 40 | CRT allocator helpers `0x1005C610`/`0x1005C5A0`, `memset`, `operator delete` | class D: the vtable's other eleven methods are transcribed, but these three are thin wrappers over CRT allocator internals the module cannot name | low: the vtable methods that matter are transcribed; these three are allocator plumbing whose failure mode is an allocation fault, which the game shows immediately |

---

## 4. Follow-ups, in the order they are worth doing

1. **NewFiles updates** (`0x10051890`/`0x100518D0`, 30 each). Needs two `__fastcall`
   entry shims in front of `NewfilesCallObject`/`NewfilesCallAdd`; once those exist the
   two bodies are a straight transcription. Their call graph stays class C, so the doc
   entry above changes from "blocked" to "proven caller, rebuilt callees".
2. **NMS arena methods** (`0x10038360`/`0x10038430`/`0x100385A0`). Transcribable in
   shape; only worth it together with the CRT allocator helpers they call.
3. **The comparator, and why not to relax it.** Six class-A pieces would become
   transcribable if `summarise()` treated an indexed absolute displacement as a slot
   too. That is a one-line change and it is **not** made here, deliberately: it would
   stop the proof distinguishing `[esi*4+0x1015A538]` from `[esi*4+0x10159D38]`, i.e. a
   transcription could read the wrong table and still pass. The class-A rebuilds are
   the price of keeping table identity inside the proof. If it is ever wanted, the
   honest form is a per-row opt-in flag ("this displacement is a slot") in
   `tests/bodies.tsv`, not a global relaxation.
4. **The internal-branch-target check** (the previous revision's item 1) is **done**:
   it is `tools/verify_bodies.py::internal_branch_mismatch`, it runs on every row, and
   it is what §2a's six transcriptions were proved with. `.scratch/branch_targets.py`
   stays as the independent reference implementation; the two agree at
   `131 row(s), 206 internal branch target(s), 0 failure(s)`.

---

## 5. Check these first in play

Ordered by *how much breaks if the piece is subtly wrong*, and within that by *how
loosely the surrounding proofs pin it*. The first three are the ones to exercise
deliberately after any change to the modules above.

1. **`0x10051B30`/`0x10051FD0`, the save converter and prepare** (class B, high
   impact, no behaviour proof). Exercise: load a save, save a game, then load the
   save just written, in a new session. What to look for: the save loads at all
   (chunk walk and the three site patches), the HUD/clock restored, no "corrupt save"
   dialog, and the file mtime/size changing exactly once. This is the only piece whose
   failure destroys user data rather than a session.
2. **`0x100038E0`/`0x10003980`, the guarded native calls** (class B, high impact,
   fault-parity is the feature). Exercise: any path that calls a native with a slot
   that has not resolved — the audio wrappers, `DrivebyFOVFix`, `BadPlayerModelCrashFix`
   on a non-ped model. What to look for: a clean no-op (original: answered 0), not a
   crash; and the reverse — a genuine game fault must still crash in the same place.
3. **`0x10004C70`, the steer/render consumer** (class A, runs every frame). Exercise:
   drive a car with `MovingSteeringWheelSupport`, then a plane with
   `MovingPlaneParts`, then a car with `ShakingEngineBlock`. What to look for: the
   steering wheel turning with the wheels, the engine block shaking with RPM, the
   plane's parts moving; and nothing rendering at the world origin.
4. **`0x10023150`, the DLC discovery** (now transcribed, but only shape-proven).
   Exercise: start the game with an EFLC install (or with `\dlc\tlad\content\`
   present) and check the episode picker lists the episodes; then with none and check
   nothing is listed. What to look for: a missing or duplicated episode entry.
5. **`0x10050540`, SpeedupBugFix's body** (class B, moderate impact).
   Exercise: enable it, load a save, note the in-game clock and the mission timer.
   What to look for: the clock running at the right rate (a wrong slot = a fast or
   frozen timer).
6. **`0x10001280` + the two forwarders** (now transcribed). Exercise: the livery list
   on a car with `NoLiveryLimit`, then the OOC fix (a save past the last mission), then
   a DLC start. What to look for: correct `"%s_sign_%d"` livery names, a correct
   `memory:$…` mount string (no truncated path), and correct `\Episode<n>\` probes.
7. **`0x10005F70`/`0x10006260`/`0x10003FA0`/`0x10044310`, the misc checkers**
   (class A). Exercise: `CopSirensFlag` (police cars get their flags),
   `PopupHeadlightsFlag` (pop-up lights on the configured model),
   `ForceCarHeadlightShadows`. What to look for: the feature silently not applying to
   one model.
8. **The ROM service and the DFA close-handle body** (classes B/C). Exercise: launch
   through `PlayGTAIV.exe` and watch for the ROM message window to exist (no visible
   symptom); a hang here is the preserved original fault. Then open a save so the DFA
   layer closes a file. Least likely to matter, hardest to see. The other four DFA
   forwarders are transcribed (§2a) and need the same file-open exercise: a wrong
   argument order would fail the open, not crash.

---

## 6. Where the numbers live

- `tests/bodies.tsv` — the 131 proof rows; a row's count is the body's exact
  instruction count, so it cannot silently shrink.
- `tools/verify_bodies.py` — the comparator, including the internal-branch-target
  check; `--selftest` proves it against the original and against a one-byte
  corruption and a moved branch target.
- `.scratch/byte_compare.py` — the byte-level comparison quoted in §2 and §2a
  (absolute operands and `call`/`push` immediates are the only differences, by
  construction). Run it with body names to select rows; with no arguments it prints
  the six of the first pass.
- `.scratch/branch_targets.py` — the internal-branch-target check over every row, the
  independent reference implementation of what `verify_bodies.py` now does.
- `.scratch/resolve_calls.py` — the call/operand-to-symbol check quoted in §2 and §2a
  (what the proof's call-as-immediate rule cannot see).
- `docs/parity.md` — per-key behaviour, the preserved faults, and the oracle-verified
  class; its rebuild notes now point here instead of repeating the reasons.
