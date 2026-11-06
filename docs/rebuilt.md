# Rebuilt pieces: the least-proven code in the tree

Every other piece of this module is **transcribed**: a body is written instruction for
instruction from the original ASI, and `tools/verify_bodies.py` decodes the same
instruction count on both sides and compares mnemonic, registers, memory shape and
immediate *class*. That proof is what `tests/bodies.tsv` is for, and it currently
covers **135 bodies**. A transcribed body whose original stream reads a table through
an indexed absolute operand is provable too, because the tree registers the table and
the comparator compares its *contents*: §1 and §2b describe that, and the model-name
lookup (`0x10003F00`) is the first piece transcribed that way. §2c is the largest: the
steer consumer (`0x10004C70`, 320 instructions), whose two tables are also the reason
the registry now reads a section's uninitialised tail and accepts C symbol names.

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

    mnemonic  reg:<name>…  imm  [base+index*scale+disp]  abs  [index*scale+table:<hash>]

with four consequences that decide every case below:

| operand form | compared as | consequence |
|---|---|---|
| register, or `[ebp+disp]` / `[esp+disp]` / `[reg+disp]` | the displacement **literally** | a transcription must use the same frame offsets, which it does because it writes the same instructions |
| memory with **no base and no index** (`[0x1015AD94]`) | `abs` — a slot | the module may point it at its own global or slot |
| `call`/`jmp`/`push` immediate | `imm` — a slot | the module may name its own function or literal |
| memory **with an index and no base** (`[esi*4 + 0x1015A538]`) | the displacement **literally**, unless a manifest table row names it | the module cannot match the original's VA; if the table is registered the displacement becomes `table:<content hash>` and the body is provable (§2b, §2c). The row may name a C symbol (`_name`), and the table may live in a section's uninitialised tail (read as zeros) — both properties the 512-slot model tables need |

So the blockers are mechanical, and there are exactly four kinds in this tree:

- **A · indexed absolute table access.** The original's 512-slot model tables
  (`0x1015A538`, `0x10159D38`, `0x10159380`, `0x1015CE28`, `0x1015C628`, `0x1015B5C0`,
  the 38,455-entry name table `0x100DE770`) are read as `[reg*4 + 0x10…]`. A rebuilt
  module cannot place a table at the original's VA, and the comparator compares that
  displacement literally, so the walk cannot be proved — and writing it as
  `[base + reg*4]` would change the instruction shape and fail for a different reason.
  **This is now solvable per table.** `Bodies::RegisterTable` names a table (its
  address here, the original's VA and its length) and `tests/bodies.tsv` carries the
  offline projection of that call as a `table` row; the comparator then canonicalises
  only that displacement to `table:<content hash>` and compares the two tables'
  contents. `0x10003F00` is transcribed that way (§2b). A table no row names keeps the
  literal comparison, so the proof is not relaxed by accident, and a body that reads a
  *different* registered table fails with `table contents differ` rather than as an
  instruction difference.
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
   pass (131 after §2a), including
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
   (154 internal targets), for the 131 rows of §2a (206), for the 132 rows of §2b
   (208), for the 133 rows of §2c (228) and for today's 135 rows (249).

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

### 2b. This pass: the model-name lookup, and the table registry

The first class-A piece is transcribed. `0x10003F00` is the smallest of the seven
(62 bytes, 21 instructions) and its blocker is exactly one table: the original ASI's
38,455-entry `char*` name table at `0x100DE770`, read twice through
`push ds:[table + esi*4]` and `mov eax, ds:[table + esi*4]`.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| model-name lookup | `0x10003F00` | `0x3E` | 21 | `bpm_model_name` | 17/21 | 2 table displacements + 1 `call` immediate + 1 `mov eax, offset` immediate |

**The registry.** `source/bodies.ixx` gained a second registry beside the body one:

```cpp
struct Table { uintptr_t address; uint32_t reference; size_t bytes; };
void RegisterTable(uintptr_t ourTable, uint32_t originalVa, size_t bytes);
std::span<const Table> Tables();
```

`address` is where the table lives in this process, `reference` the VA it has in the
original ASI, `bytes` its length there. The arm that owns the table registers it at
startup next to its `Bodies::Register` calls; `BadPlayerModelCrashFix` does, because
the lookup is its piece:

```cpp
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(kBadPlayerModelNames),
    0x100DE770u, sizeof(kBadPlayerModelNames));   // 38455 * 4 = 153820
```

The body keeps the original's operand shape — `push DWORD PTR [kBadPlayerModelNames +
esi*4]`, `mov eax, DWORD PTR [kBadPlayerModelNames + esi*4]` — so the instruction
stream is the original's, including the 10-byte `66 66 0F 1F 84 00 00 00 00 00` loop
padding (emitted as bytes: the inline assembler's `nop` is one byte, and the body's
internal branch targets are compared by offset).

**The offline projection.** A 32-bit module's runtime registry is a vector in the
candidate's memory; the proof is an offline file comparison, and the only offline
bridge from a symbol to its address is the linker map. So the registration has a
manifest twin — `tests/bodies.tsv`:

```
table 100DE770 ?kBadPlayerModelNames@@3QBQBDB 153820
```

recovered VA, the candidate symbol the map resolves, the registered length. It is the
same three facts as the `RegisterTable` call; the comparator reads the row, resolves
the symbol to this module's own copy, and never needs the runtime vector.

**What the comparator does with it.** `summarise()` takes the table registry for its
side of the comparison. An operand that has an index and **no base register** is an
indexed absolute; if its displacement is exactly a registered VA on that side, the
displacement becomes `table:<content hash>` and the index register and scale are still
compared (reading the same table through a different register still fails). A
displacement that is not registered keeps today's literal comparison. When the two
sides differ, `table_mismatch()` names the finding:

- both sides resolve, hashes differ → `FAIL … (table contents differ: 0x100de770
  (57ad8796b1b2652c) against 0x100de770 (3d5b51a4a421d49c))`
- one side resolves → `table 0x… is registered on the original side only`

**What the content hash covers, and why it is not the raw bytes.** The hash is over
exactly the registered length, but a 4-byte word that addresses that side's image is
replaced by the bytes it names (a C string, NUL included). The name table is an array
of pointers: hashing the pointer values would compare the two images' string-pool
layouts, which is the same mistake as comparing the table's address. Hashing the
pointed-to strings compares the table's *contents* — the names, in the table's order.
Every other word is hashed as it stands, so a table of hashes, floats or ints (the
512-slot model tables) compares by value. The two sides here hash to the same
`57ad8796b1b2652c` although their entries point at `0x100651e0` and `0x10117bdc`
respectively — that equality is the proof that all 38,455 names are present, in order.

**How this pass was verified**

1. **Proof row.** `PASS bpm_model_name (21 instruction(s), 0 absolute operand(s), 2
   table operand(s))`, and `python tools/verify_bodies.py …` →
   `132 body(ies), 0 failure(s)`.
2. **Byte comparison.** 17 of 21 instructions are byte-identical and the body is 62
   bytes on both sides, so every instruction sits at the same relative offset; the four
   differences are the two table displacements, the resolver `call` (which lands on a
   six-byte `jmp` forwarder, because a module interface cannot export a naked function)
   and the `mov eax, offset` of the module's own `"**UNKNOWN**"` literal.
3. **The table is checked, not assumed.** The candidate table's content hash equals the
   original's (above), over the full registered length. The names themselves are the
   recovered `source/model_names.inc`; the array is `static_assert`ed to 38,455 entries,
   and `.scratch/table_contents.py` compares every include name with the string the
   original's entry points at (`38455 name(s), 0 mismatch(es)`).
4. **The comparator proves its own mechanism.** `--selftest` gained four table cases:
   registered and identical → PASS; one name byte changed → FAIL as
   `table contents differ`; the body's operand moved to a *different* registered table
   → FAIL as a content difference (not an instruction one); and the same moved operand
   with nothing registered → FAIL literally, which is the opt-in guarantee. The
   selftest's full verdict is quoted in §6.
5. **The branch targets.** The body has two internal branches; `132 row(s), 208
   internal branch target(s), 0 failure(s)`.
6. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1 oracle-verified`,
   and `zp_dump --ooc-selftest` still passes.

**What is proven and what is not.** The body's instructions and its table are proven;
the *call* is not — it goes through `BpmResolverEntry`, a six-byte `jmp` through
`zpatch_bpm_resolver`, because `ValueResolverBody` is `static` in `source/shared.ixx`
and cannot be named from another unit's inline asm. That is the same
call-as-immediate gap as every other forwarded call in the tree (`parity.md`
lists them). The lookup itself only runs on a BadPlayerModel miss with GTAC.dll
loaded, so in play the diagnostic's text is the observable.

**The recipe for the remaining class-A pieces.** For each one:

1. Transcribe the body with the table operand absolute (`[table + reg*4]`), keeping
   every instruction's size — the branch check compares offsets.
2. Recover the table's contents (or reuse the arm's published copy), put them in the
   module in the original's order, and `static_assert` the entry count.
3. `Bodies::RegisterTable(&table, originalVa, sizeof table)` next to the arm's
   `Bodies::Register` calls, and add the `table` row to `tests/bodies.tsv` with the
   symbol the map resolves.
4. Add the body row with the exact instruction count, remove the C++ rebuild, and run
   the proof. A content mismatch is reported as `table contents differ`, which means
   the table, not the body, is wrong.

Two caveats for the next piece. The registry resolves only operands with **no base
register**; the copsirens checkers read `push ds:[edi+0x10159380]` and
`cmp ds:[edi+0x10159380],0`, which is an absolute *with* a base and no index, and needs
the same canonicalisation extended deliberately (a base register plus a registered
displacement) — that is a separate, narrow change, not a relaxation of this one. And a
table whose entries are pointers needs the pointer-dereference hash above; a table of
plain 4-byte values compares by value already.

The headlight-cone check (`0x1001AD20`, class A) is the next piece the current rule
covers — `[esi*4+0x1015ADBC]` and `[esi*4+0x1015B5C0]` are both indexed absolutes with
no base — but it carries a *behavioural* caveat the registry does not solve: its walk
reads one word past the 512-entry hash table, into whatever `.data` follows it in the
original. A transcription reads our array, so the word after it has to be the
original's next word for that pathological case to match; the registered length covers
the table, not its neighbour.

### 2c. This pass: the steer consumer, and the two table properties the registry needed

`0x10004C70` is the largest piece transcribed so far and the highest-risk one: it runs
every frame the player drives. Its two table operands also exposed two gaps in the
per-table registry §2b introduced, both now closed.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| steer/engine/plane consumer | `0x10004C70` | `0x53F` | 320 | `steer_consumer` | 250/320 | 56 absolute operands + 10 `call` immediates + 2 string offsets + 2 table displacements |

Every non-identical instruction differs in an operand the proof already reduces to a
slot, a call target, a string address or a registered table. No instruction's
mnemonic, register, frame offset or size differs (0 `other`), no branch changed form
(0 branch), and the body is 1,343 bytes on both sides — the original's exact size — so
every instruction sits at the same relative offset and all 20 internal branch targets
agree. The proof's line is `PASS steer_consumer (320 instruction(s), 56 absolute
operand(s), 2 table operand(s))`.

**The two gaps the tables exposed.** The original keeps both wheelangle tables in its
`.data` at `0x1015A538` / `0x10159D38`, past the raw size of that section (`0x1000` of
a `0x504D4` virtual extent): the file holds none of their bytes, and the loader maps
zeros there. `read_table()` used to answer "not in the image" for those addresses, so
no `table` row could name any of the 512-slot model tables — every class-A table except
the model-name one is in some section's uninitialised tail. And the arrays belong to
`source/misc.ixx` (the transcribed IDE parser hook is what fills them), declared
`extern "C"`, so the map's name for them is `_zpatch_wheelangle_models` rather than a
`?`-mangled one, which `read_map()` did not record. Both are now handled:

- `read_table()` reads the file-backed prefix of the range and fills the section's
  uninitialised tail with zeros — what the loaded image holds, which is what the proof
  compares. A range that leaves its section is still rejected, and a table with real
  content on either side still hashes differently, so the change cannot turn a real
  difference into a PASS.
- `read_map()` records `_name` symbols as well as `?name@@…` ones. This is symbol
  resolution, not comparison: `summarise` and `table_mismatch` are untouched.

The self-test grew a case for each: the consumer compared against itself with both
tables registered must PASS with **2 resolved table operands** (a file-only read would
raise `registered table … is not in the image`); the same comparison with the tables
registered on the original side only must FAIL as `table 0x1015a538 is registered on
the original side only`; and a synthetic map must resolve both decorations. Its verdict
is quoted in §6.

**The tables and how they were verified.** Nothing new was added to the module: the
tables are the IDE hook's own arrays, in the original's slot order by construction
(the hook stores the model word at the slot it found and the angle bits beside it).
The arm registers them next to its `Bodies::Register` calls:

```cpp
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_wheelangle_models),
    0x1015A538u, sizeof(zpatch_wheelangle_models));   // 512 * 4 = 2048
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_wheelangle_angles),
    0x10159D38u, sizeof(zpatch_wheelangle_angles));   // 512 * 4 = 2048
```

and `tests/bodies.tsv` carries the offline projection, one row per table, each pairing
the recovered VA with the candidate symbol explicitly:

```
table 1015A538 _zpatch_wheelangle_models 2048
table 10159D38 _zpatch_wheelangle_angles 2048
```

**What the content check does and does not prove for these two tables.** Both are
all-zero at load — the original's sit in the uninitialised tail, ours are
zero-initialised — so both hash to the same value (`707280d9b4da7bba`, 2,048 zero
bytes). The PASS therefore says *this operand reads a zero-filled table registered for
this row*; it **cannot distinguish the two tables from each other**. A transcription
that pointed the model operand at the angle table would pass the content check. What
pins the identity is the manifest pairing above (each row declares one original VA ↔
one symbol) together with the instruction's own semantics: the slot that goes through
`dword_10159C0C` and is compared with the model index is the model table, and the value
multiplied by `pi` and divided by `180` is the angle table. No sentinel can strengthen
this: the first zero word is what ends the walk and what the IDE hook treats as a free
slot, so any non-zero content in our copy would change behaviour.

**How this pass was verified**

1. **Proof row.** `PASS steer_consumer (320 instruction(s), 56 absolute operand(s), 2
   table operand(s))`; `python tools/verify_bodies.py …` → `133 body(ies), 0
   failure(s)`. The row's count is exact, so a missing or extra instruction fails it.
2. **Byte comparison.** 250 of the 320 instructions are byte-identical (the table
   above); the 70 differences are operand slots, call targets, the two string offsets
   and the two table displacements.
3. **The operand map.** The proof cannot see *which* slot an absolute operand names —
   an absolute is compared as `abs`. `.scratch/steer/operand_map.py` decodes the
   transcription against the original instruction by instruction, resolves every
   absolute operand, call immediate and string offset through the linker map, and
   checks it against a declared expected pairing: **33 distinct original operands
   exercised, 0 declared but unused, 0 mismatch(es)**. That is what says
   `mulsd xmm0,[0x101523a8]` became `zpatch_steer_neg_pi` (the original's exact bit
   pattern) and not another qword, that `[0x1015A538]`/`[0x10159D38]` became the model
   and angle arrays and not each other, and that the four `call 0x10003980` sites go
   through the resolve forwarder. One alias is expected: `/OPT:ICF` folds the six-byte
   `jmp [zpatch_native_resolve_entry]` forwarder into `loading.ixx`'s identical
   `CutsceneResolveEntry`, so the call resolves to that name — same bytes, same target,
   the same fold the formatter forwarders show (§2 item 6).
4. **The constants are the original's.** Each `.rdata` qword/dword was read out of the
   image at the VA named in its comment: `-pi`, `+pi`, `180.0`, `±0.5`, `0.75`, `0.25`,
   `±0.01`, `1.0f` and `100.0f`, plus the two bone names (`"steeringwheel"` at
   `0x10129518`, `"engineblock"` at `0x10129570`). The proof compares these operands as
   `abs` and cannot see the values; the comment and the bit pattern are the record.
5. **The branch targets.** 320 instructions with 23 internal branches, every one at
   the original's relative offset (0 branch differences in the byte comparison).
6. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1
   oracle-verified`, the five keys' per-option cells stay byte-identical to their
   baselines on all nine images, and `zp_dump --ooc-selftest` passes.

**What stays unproven.** The body only runs in play (the detours are installed at the
game's vtable call), so nothing here executes it: the proof is of code and operand
wiring, not of a wheel turning. The two things that could still be wrong are a slot the
operand map declares correctly but whose runtime value differs (all of them are
published by arms the dump proves), and the wheelangle tables' *runtime* contents,
which the IDE hook fills from the INI in both trees by the same transcribed body.
`docs/parity.md` §"The five keys behind `sub_10004C70`" records the play item.

### 2d. This pass: the two siren checkers, and the base-plus-displacement shape

`CopSirensFlag`'s two model checks read their 512-slot siren table as
`push ds:[esi+0x10159380]` — an absolute *with* a base register and no index, which is
the shape §2b's recipe left out. The extension in §4 item 3 resolves it: the
displacement becomes `table:<content hash>` and the base register is still compared, so
a changed base is an *instruction* difference and not a table one. Both checkers are
then transcribable, and both are transcribed.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| first siren check | `0x10005F70` | `0x2EE` | 191 | `copsirens_check_a` | 127/191 | 34 absolute operands + 15 `call` immediates + 13 name offsets + 2 table displacements |
| second siren check | `0x10006260` | `0xAE` | 53 | `copsirens_check_b` | 40/53 | 7 absolute operands + 3 `call` immediates + 1 name offset + 2 table displacements |

Every difference is an operand the proof reduces to a slot, a call target, a string
address or a registered table; no mnemonic, register, frame offset, size or branch form
differs (0 `other`, 0 branch), and both bodies are the original's exact size (750 and
174 bytes), so every internal branch sits at the same relative offset. The proof's
lines are `PASS copsirens_check_a (191 instruction(s), 34 absolute operand(s), 2 table
operand(s))` and `PASS copsirens_check_b (53 instruction(s), 7 absolute operand(s), 2
table operand(s))`.

**The bodies, and what they read.** The first check resolves thirteen built-in police
model names through the original's own resolver (`sub_10003E50`) and the game's
model-index lookup, in the original's order (`police`, `police2`, `police3`, `police4`,
`noose`, `pstockade`, `nstockade`, `polpatriot`, `fbi`, `n_police`, `n_police2`,
`n_polpatriot`, `n_pstockade`), sets the flag on the first match and otherwise walks the
IDE parser's `+siren` list; the second resolves `"police2"` first, then walks the same
list. Each name is the original's own `.rdata` string, pushed as an `imm32` offset of a
module literal; the walk's `cmove` keeps the flag byte it read, and it stops at the
first zero word or past slot 511. The 3-byte `nop dword ptr [eax]` at `0x100061ED` is
emitted as bytes. Both /GS frames are the originals' (cookie slot, check call slot); no
exception frame, no TLS guard.

**The table.** `zpatch_copsirens_models` is the IDE hook's `+siren` list, 512 words,
and the arm registers it beside the existing `Bodies::Register` calls:

```cpp
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_copsirens_models),
    0x10159380u, sizeof(zpatch_copsirens_models));   // 512 * 4 = 2048
```

with its manifest row `table 10159380 _zpatch_copsirens_models 2048` (a C symbol, as
in §2c) and two body rows.

**What the content check does and does not prove for this table.** Like the wheelangle
tables it is all-zero at load — the original's copy is in the `.data` tail past the
file's raw size, ours is zero-initialised — so both sides hash to the same value and the
check says *this operand reads a zero-filled table registered for this row*. It
**cannot tell one zero table from another** (the two wheelangle tables and this one all
hash alike at load), so a transcription that read the wrong zero table would pass. That
this table is *filled at run time* by the IDE hook does not change the comparison: the
proof compares the shipped state, and the fill path is the transcribed
`IdeHookBody`/`IdeParse` pair on both sides. What pins the identity is the manifest
pairing (each row declares one original VA ↔ one symbol) plus the instruction's
semantics: this walk's slot goes through `dword_10159C0C` and is compared with the site's
model index, and the IDE hook's `siren` branch is what writes this array.

**How this pass was verified**

1. **Proof rows.** The two PASS lines above, and `python tools/verify_bodies.py …` →
   `135 body(ies), 0 failure(s)`.
2. **Byte comparison.** 127/191 and 40/53 identical; the differences are operand slots,
   call targets, the thirteen/two string offsets and the two table displacements each.
   `.scratch/steer/compare_body.py sirens_a|sirens` is the run.
3. **The operand maps.** `.scratch/steer/operand_map.py sirens_a|sirens` resolves every
   absolute operand, call immediate and string offset against a declared pairing:
   **20 distinct operands, 0 mismatch** for the first check (including all thirteen
   names in their order and the `dword_10159370` index slot) and **8 distinct operands,
   0 mismatch** for the second. That is what says `[edi+0x10159370]` became
   `zpatch_copsirens_index` and not the `_b` slot 24 bytes away, and that the walk reads
   `zpatch_copsirens_models`.
4. **The table is the one the arm registers** — same array the IDE hook writes — and its
   length is `sizeof` the array (2,048 bytes), not a literal.
5. **The branch targets.** 191 + 53 instructions with 6 + 4 internal branches, each at
   the original's relative offset (`135 row(s), 249 internal branch target(s), 0
   failure(s)`).
6. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1 oracle-verified`,
   `CopSirensFlag`'s per-option cell is byte-identical to its baseline on all nine
   images, and `zp_dump --ooc-selftest` passes.

**What stays rebuilt here.** The IDE parser payload (`sub_10003FA0`, 92) still is: its
four indexed stores into four different tables (`0x1015CE28`, `0x10159380`,
`0x1015A538`, `0x10159D38`) are transcribable under the registry now, but its payload
is `strcmp`/`strtof`-shaped C++ that this pass did not take. It is the next class-A
piece by the §4 item 3 list.

**What stays unproven.** The checks only run when the game's sites call them, so the
offline proof is of code and wiring; in play a wrong name order or a wrong index slot
would show as `CopSirensFlag` not applying to one wrecked police car, which is
cosmetic.

---

## 3. Every piece that is still rebuilt

`insns` is the linear sweep over the function's own bytes, i.e. what a proof row would
have to cover. IDA's per-function total is sometimes larger because it also counts the
out-of-line EH funclet / thread-abort blocks that belong to the function but do not sit
in its byte range; where the two differ, the total is given in the row's text. `bytes`
is IDA's function size.

### Class A — indexed absolute table operand

The model-name lookup (`0x10003F00`, §2b), the steer consumer (`0x10004C70`, §2c) and
both siren checkers (`0x10005F70`/`0x10006260`, §2d) have left this class: they are
transcribed, their tables are registered and their operands are compared by content —
the checkers through the base-plus-displacement shape. What remains is a mechanical
blocker on tables that are not yet registered (the IDE payload and the popup-headlights
checker) and one behavioural blocker the registry does not solve (the headlight-cone
check's read one word past its table).

| piece | VA | bytes | insns | the operand that blocks it | what the rebuild reproduces | how that was verified | residual risk |
|---|---|---:|---:|---|---|---|---|
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
3. **The comparator, and how far to extend it.** The global relaxation is still
   **not** made, deliberately: treating every indexed absolute displacement as a slot
   would stop the proof distinguishing `[esi*4+0x1015A538]` from `[esi*4+0x10159D38]`,
   i.e. a transcription could read the wrong table and still pass. What replaced it is
   the per-table opt-in §2b describes: `Bodies::RegisterTable` plus a `table` row in
   `tests/bodies.tsv`. A table that is not named still compares literally, and two
   registered tables with different contents fail as `table contents differ`. Three
   properties the class-A tables needed are now in place:

   - a table's bytes are read as the *loaded* image holds them: a section's
     uninitialised tail (where the original keeps every 512-slot model table) reads as
     zeros (§2c);
   - a row may name a C symbol (`_name`) as well as a C++ one (§2c);
   - the registry resolves the **base-plus-displacement** shape as well as the indexed
     absolute: `push ds:[edi+0x10159380]` becomes `[edi+table:<hash>]` with the base
     register still compared, and a base that changed is reported as an *instruction*
     difference, not a table one. `table_read` is the single place that decides which
     shapes resolve; a base with an index stays literal.

   The self-test covers all three (its verdict is in §6). What that leaves in class A is
   no longer a *comparator* problem: the IDE payload `0x10003FA0` (92) is transcribable
   as it stands (its four indexed stores all resolve now), and the popup-headlights
   checker `0x10044310` (125) after its eight-operand table set is registered. The
   headlight-cone check `0x1001AD20` (46) still carries the behavioural caveat above
   (one word past its table), which the registry does not solve.
4. **The internal-branch-target check** (the previous revision's item 1) is **done**:
   it is `tools/verify_bodies.py::internal_branch_mismatch`, it runs on every row, and
   it is what §2a's six transcriptions were proved with. `.scratch/branch_targets.py`
   stays as the independent reference implementation (it skips the `table` rows); the
   two agree at `135 row(s), 249 internal branch target(s), 0 failure(s)`.

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
   With GTAC.dll loaded, the same path prints the diagnostic, whose model name now comes
   from the transcribed `0x10003F00` walking the registered table (§2b): a wrong name in
   that text is the visible symptom of a bad table, and the proof already compares the
   table's contents.
3. **`0x10004C70`, the steer/render consumer** (now transcribed, §2c — 320
   instructions, but they only run in play). Exercise:
   drive a car with `MovingSteeringWheelSupport`, then a plane with
   `MovingPlaneParts`, then a car with `ShakingEngineBlock`. What to look for: the
   steering wheel turning with the wheels, the engine block shaking with RPM, the
   plane's parts moving; and nothing rendering at the world origin. A wrong wheel-angle
   slot would show as a wrong-looking wheel rather than a crash, which is why the
   operand map (§2c item 3) exists.
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
7. **`0x10003FA0`/`0x10044310`, the misc checkers** (class A; the two siren checks
   `0x10005F70`/`0x10006260` are transcribed now, §2d). Exercise: `CopSirensFlag`
   (police cars get their flags; the transcribed pair decides `police`, `police2`, …
   and the `+siren` list), `PopupHeadlightsFlag` (pop-up lights on the configured
   model), `ForceCarHeadlightShadows`. What to look for: the feature silently not
   applying to one model, which for the siren checks would mean a wrong name or index
   slot — the operand map (§2d item 3) declares every one of them.
8. **The ROM service and the DFA close-handle body** (classes B/C). Exercise: launch
   through `PlayGTAIV.exe` and watch for the ROM message window to exist (no visible
   symptom); a hang here is the preserved original fault. Then open a save so the DFA
   layer closes a file. Least likely to matter, hardest to see. The other four DFA
   forwarders are transcribed (§2a) and need the same file-open exercise: a wrong
   argument order would fail the open, not crash.

---

## 6. Where the numbers live

- `tests/bodies.tsv` — the 135 proof rows; a row's count is the body's exact
  instruction count, so it cannot silently shrink. Its `table` rows are the offline
  projection of the arms' `Bodies::RegisterTable` calls (recovered VA, candidate
  symbol, registered length), one per table a transcribed body reads; a symbol may be
  C++ (`?name@@…`) or C (`_name`), and a table may live in a section's uninitialised
  tail (its bytes read as zeros, what the loader maps there).
- `tools/verify_bodies.py` — the comparator, including the internal-branch-target
  check and the table registry (`Tables`, `table_content_hash`, `table_mismatch`,
  `table_read`, `read_table`, `read_map`); `table_read` is the one place that decides
  which operand shapes resolve (an indexed absolute, or a base plus displacement).
  `--selftest` proves it against the original, against a one-byte corruption, against a
  moved branch target, against the four table cases (registered → PASS, changed
  contents → FAIL as a content difference, operand moved to another registered table →
  FAIL as a content difference, unregistered operand → FAIL literally), against the
  uninitialised-tail table path, against the base-plus-displacement shape (PASS with
  both operands resolved; a changed base register → FAIL as an instruction difference;
  changed table contents → FAIL as a table difference), and against the map's two
  decorations. Its verdict:
  `selftest: identical PASS, corrupted opcode FAIL, moved branch target FAIL, registered
  table PASS (2 operands resolved), changed table contents FAIL, moved table operand
  FAIL, unregistered table operand FAIL, uninitialised-tail table PASS (2 operands
  resolved), one-side-only tail table FAIL, map C and C++ symbols PASS, base+disp table
  PASS (2 operands resolved), changed base register FAIL (instruction), base+disp
  contents differ FAIL (table)`.
- `.scratch/byte_compare.py` — the byte-level comparison quoted in §2, §2a and §2b
  (absolute operands and `call`/`push` immediates are the only differences, by
  construction). Run it with body names to select rows; with no arguments it prints
  the six of the first pass. It classifies a table displacement as `other`, because it
  has no registry; §2b's 17/21 is that comparison with the two table displacements read
  as such.
- `.scratch/steer/compare_body.py` — §2c's and §2d's byte comparison (250/320,
  127/191, 40/53); it knows the registered table displacements (both shapes) and reports
  a branch whose target offset differs. `steer`, `sirens_a`, `sirens` select the body.
- `.scratch/steer/operand_map.py` — §2c item 3 and §2d item 3: every absolute operand,
  call immediate and string offset of a transcription resolved through the linker map
  and checked against a declared pairing (33 distinct for the steer consumer, 20 for the
  first siren check, 8 for the second; 0 mismatch each). This is the check that says
  *which* slot an `abs` operand names, which the proof cannot.
- `.scratch/steer/dump_body.py`, `.scratch/steer/peek.py`, `.scratch/steer/tables_probe.py`
  — the listing/constant/table probes §2c was written from.
- `.scratch/branch_targets.py` — the internal-branch-target check over every row, the
  independent reference implementation of what `verify_bodies.py` now does (it skips
  the `table` rows).
- `.scratch/resolve_calls.py` — the call/operand-to-symbol check quoted in §2 and §2a
  (what the proof's call-as-immediate rule cannot see).
- `.scratch/table_contents.py` — the registered table checked on its own (§2b): both
  sides' content hash over the registered length, the first and last entry of each, and
  every name in `source/model_names.inc` against the string the original's entry points
  at. It prints `38455 name(s) in the include, 0 mismatch(es)` and the two hashes above.
- `docs/parity.md` — per-key behaviour, the preserved faults, and the oracle-verified
  class; its rebuild notes now point here instead of repeating the reasons.
