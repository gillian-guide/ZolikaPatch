# Rebuilt pieces: the least-proven code in the tree

Every other piece of this module is **transcribed**: a body is written instruction for
instruction from the original ASI, and `tools/verify_bodies.py` decodes the same
instruction count on both sides and compares mnemonic, registers, memory shape and
immediate *class*. That proof is what `tests/bodies.tsv` is for, and it currently
covers **151 bodies**. A transcribed body whose original stream reads a table through
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
  the frame *is* the feature: the `__except` is what answers 0 on a fault inside the
  region (a null native handler is the invoke's skip, not a fault), and
  `zp_native_guard` now runs those outcomes instead of arguing them (§3 class B).
- **C · TLS static-init guard.** A function-local `static` in the original is
  initialised through `_Init_thread_header` / `_Init_thread_footer`
  (`0x1005FFC3` / `0x1005FF77`), which reach the CRT's per-thread block through
  `fs:[2Ch]` and the image's `TlsIndex` word. The guard's data (`dword_101A84C8`
  and friends), the abort funclet and the TLS index are image-internal state a rebuilt
  module cannot name.
- **D · call graph of rebuilds.** The body itself is comparable, but every function it
  calls is a rebuild, so a transcription would prove the branch shape and none of the
  behaviour. This is why the NewFiles updates are still rebuilt: their callees are
  class C. It is **not** a permanent reason: the OOC archive builder was filed here for
  its twelve helper callees, and this pass transcribed the twelve helpers with it
  (§2h), so the piece is proven whole. What is left in the class is the NewFiles pair
  and the NewMemorySystem arena methods, whose callers cannot be transcribed without
  the container internals they call (§3). The arena's three methods are pinned by
  behaviour instead (§2j, `zp_nms_arena`), which is the strongest evidence that class
  can produce while the container stays untranscribable.

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
   (208), for the 133 rows of §2c (228), for the 135 rows of §2b–§2d (249), for the
   138 rows of §2b–§2g (292) and for today's 151 rows (321).

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
and stays a rebuild; §2l identifies the filter's disposition (`-1`,
`EXCEPTION_CONTINUE_EXECUTION`, so a fault is retried rather than answered), pins it in
a child process, and compares the rebuilt body with the oracle byte for byte.

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

**What stays rebuilt in these two areas, and why.** The init (`0x1004B750`, EH +
C++ containers), the key's handler (`0x1004C3F0`, EH4 + a 16 KB string builder) and
the DFA close-handle body (`0x10004790`, EH4 — now behaviourally pinned and byte-compared
with the oracle, §2l) — every one of them for a reason this file lists. The OOC archive
builder that this paragraph used to name is transcribed with its twelve helpers in §2h;
the OOC pair's behaviour stays pinned by the selftest, and the wrapper and the branch body
were already transcribed.

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
   `138 body(ies), 0 failure(s)`.
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
   the original's relative offset (`138 row(s), 292 internal branch target(s), 0
   failure(s)`).
6. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1 oracle-verified`,
   `CopSirensFlag`'s per-option cell is byte-identical to its baseline on all nine
   images, and `zp_dump --ooc-selftest` passes.

**What stays rebuilt here.** Nothing, in this area: the IDE parser payload
(`sub_10003FA0`, 92) is transcribed in §2e, and its four tables are registered.

**What stays unproven.** The checks only run when the game's sites call them, so the
offline proof is of code and wiring; in play a wrong name order or a wrong index slot
would show as `CopSirensFlag` not applying to one wrecked police car, which is
cosmetic.

### 2e. This pass: the IDE parser payload

The payload the two siren checks and the popup checker depend on is the fourth
class-A piece, and the first that both *reads* and *writes* the registered tables: it
appends the site's model word to one of three 512-slot lists and, for
`wheelangle:<degrees>`, stores the parsed float beside the model word.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| IDE parser payload | `0x10003FA0` | `0x13A` | 92 | `copsirens_ide_parse` | 72/92 | 9 absolute operands + 10 table displacements + 1 string offset |

No mnemonic, register, frame offset, size or branch form differs (0 `other`,
0 branch), and the body is 314 bytes on both sides — the original's exact size — so
every instruction sits at the same relative offset and all six internal branch targets
agree. The proof's line is `PASS copsirens_ide_parse (92 instruction(s), 9 absolute
operand(s), 10 table operand(s))`.

**The body.** Three independent tests on the site's string (`dword_1015BE08`), in the
original's order: `"popuplights"` by three dword compares, `"siren"` by a dword and a
word compare, and the `"wheelangle:"` prefix through the CRT's `_strnicmp` followed by
`strtof` of the text eleven bytes in. Each of the first two appends the model word
(`dword_1015BE0C + 60`, read unguarded) to its list when it is not already there; the
third stores the model word and the float's *bits* at the same slot. The two
"append" walks are the original's: slot 0 is checked with a plain absolute, the walk
reads `[eax+table]` and writes `[ecx*4+table]`, and a full list stores nothing (the
`ja` to the shared epilogue past slot 511). The three loop paddings (`66 90`,
`0F 1F 00`, `66 0F 1F 44 00 00`) are emitted as bytes.

**The tables.** Four, all of them the arms' own storage: the +popuplights list
(`0x1015CE28`, newly registered here), the +siren list (`0x10159380`, §2d) and the
wheelangle model/angle pair (`0x1015A538`/`0x10159D38`, §2c). The registration and its
manifest row are the same three facts as before:

```cpp
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popuplights_models),
    0x1015CE28u, sizeof(zpatch_popuplights_models));  // 512 * 4 = 2048
```

```
table 1015CE28 _zpatch_popuplights_models 2048
```

All four are all-zero at load (the original keeps them in `.data`'s uninitialised
tail), so the content check says *this operand reads a zero-filled table registered
for this row*; which of the four a given operand reads is pinned by the manifest
pairing plus the instruction's own semantics (the string test that guards it). The
popuplights list is the one table two pieces share: this payload fills it and the
popup checker (§2f) walks it.

**The two CRT imports.** `_strnicmp` reuses `shared.ixx`'s resolver slot
(`0x100633A0`, already published by `Shared::Register()`); `strtof` (`0x100632D4`) is
a new slot filled in `RegisterMisc()`. Both stay pointer slots because the original's
call is memory indirect (`FF 15`).

**How this pass was verified**

1. **Proof row.** The PASS line above; `python tools/verify_bodies.py …` →
   `136 body(ies), 0 failure(s)`.
2. **Byte comparison** (`.scratch/steer/compare_body.py ide`): 72/92 identical; the
   twenty differences are the nine absolute slots, the ten table displacements and
   the `push` of the `"wheelangle:"` literal.
3. **The operand map.** `.scratch/steer/operand_map.py ide` resolves every absolute
   operand, call immediate and string offset through the linker map against a declared
   pairing: **9 distinct operands, 0 declared but unused, 0 mismatch(es)**. That is
   what says the two site words, the four lists, the literal and the two CRT slots are
   the ones the original's operands name — the proof compares an absolute as `abs`.
4. **The branch targets.** 92 instructions with 6 internal branches, each at the
   original's relative offset.
5. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1
   oracle-verified`, and `zp_dump --ooc-selftest` passes.

**What stays unproven.** The payload runs when the game's IDE parser stores an entry,
so the proof is of code and wiring. The `strtof` is the static CRT's rather than the
dynamic CRT's the original imports: both are MSVC's `strtof` on the same input, and
the value is stored as bits and later multiplied by the recovered `pi`/`180.0` pair in
the steer consumer (§2c), which the operand map pins.

### 2f. This pass: the popup-headlights checker, and the in-table column offset

The checker is the second class-A piece, and it needed one narrow extension of the
registry: it reads its two `[PopupLightAngles]` tables as **eight interleaved
columns**, so seven of each eight displacements are four bytes into the array rather
than at its base.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| popup-headlights checker | `0x10044310` | `0x216` | 125 | `popup_headlights_check` | 77/125 | 19 absolute operands + 26 table displacements + 3 `call` immediates |

No mnemonic, register, frame offset, size or branch form differs (0 `other`,
0 branch), and the body is 534 bytes on both sides — the original's exact size — so
every internal branch sits at the same relative offset. The proof's line is
`PASS popup_headlights_check (125 instruction(s), 19 absolute operand(s), 26 table
operand(s))`.

**The body.** It seeds the site's angle slot (`dword_101580CC`) with pi/2 as a *dword*
store, asks the game's model store for the model the original names by the literal
hash `0xEB9F21D3`, and answers flag 1 when that is the site's model word. Otherwise it
walks the +popuplights list (§2e) with the same lookup, bounded by `cmp edi,7FCh` and
ending at the first zero word, and on a match sets flag 1 and runs the column scan:

```
100443F0  8B 88 28 C6 15 10    mov ecx,ds:[eax+0x1015C628]     ; hashes, column 0
100443FA  39 0C B5 28 CE 15 10 cmp ds:[esi*4+0x1015CE28],ecx   ; the list's slot
10044403  F3 0F 10 80 28 BE 15 10  movss xmm0,ds:[eax+0x1015BE28] ; angles, column 0
...
10044508  83 C0 20             add eax,20h                     ; 64 rows of 8 columns
1004450B  3D 00 08 00 00       cmp eax,800h
10044510  0F 8C DA FE FF FF    jl 0x100443F0
```

Each column keeps the last matching row's angle, which is what the original's in-place
overwrite does; the scan always covers all 512 slots no matter how many the section
filled.

**The registry extension: a displacement inside a registered table.** §2b's recipe
resolved a displacement that *is* a table's VA and §2d's the base-plus-displacement
shape; neither could name `[eax+0x1015C62C]`, which is the same array four bytes in. A
32-bit module cannot make the linker emit a symbol at an array offset — the map's only
name for the storage is the array's base — so the comparator now resolves a
displacement that falls **inside** a registered table to that table plus the offset,
and compares the offset as part of the operand:

    mov reg:ecx [eax+table:867e6dd4bf81dfd6]      ; column 0, offset 0
    mov reg:ecx [eax+table:867e6dd4bf81dfd6+4]    ; column 1, offset 4

The table is still compared by content, and a transcription that read the wrong column
is an *instruction* difference, not a match — the self-test's new case moves one column
by eight bytes and requires exactly that. A displacement outside every registered
table still compares literally, so the relaxation stays opt-in per table and per
shape. The self-test's verdict is in §6.

**The two tables, and why they are separate objects.** `Config::ModelTable` used to be
one struct holding `hashes` and `values`; the map names an object, never a member at
an offset inside one, so neither array could be registered. `source/config.ixx` now
defines the four recovered arrays as `extern "C"` objects of their own and `ModelTable`
is a two-span *view* of them. That is storage-neutral — the loader preamble's reader
still fills the same arrays at the same slots — and it is what lets the arm register
each array with its original VA and length:

```cpp
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popup_angle_hashes),
    0x1015C628u, sizeof(zpatch_popup_angle_hashes));  // 512 * 4 = 2048
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_popup_angle_values),
    0x1015BE28u, sizeof(zpatch_popup_angle_values));  // 512 * 4 = 2048
```

```
table 1015C628 _zpatch_popup_angle_hashes 2048
table 1015BE28 _zpatch_popup_angle_values 2048
```

Both are all-zero at load in the original (`.data` tail) and here, so both sides hash
to `867e6dd4bf81dfd6` and the content check says *this operand reads a zero-filled
table registered for this row*. It cannot tell one zero table from another, which is
why the manifest pairing and the column offsets are the identity: the eight hash
columns are `+0..+0x1C` of the hashes array and the eight angle columns the same
offsets of the values array.

**How this pass was verified**

1. **Proof row.** The PASS line above; `python tools/verify_bodies.py …` →
   `137 body(ies), 0 failure(s)`.
2. **Byte comparison** (`.scratch/steer/compare_body.py popup`): 77/125 identical; the
   forty-eight differences are the nineteen absolute slots, the twenty-six table
   displacements and the three `__security_check_cookie` call immediates.
3. **The operand map.** `.scratch/steer/operand_map.py popup` resolves every absolute
   operand and every table column through the linker map, and requires each column's
   displacement to be its array's symbol **plus exactly that offset**: **24 distinct
   operands, 0 declared but unused, 0 mismatch(es)**. That is what pins all eight
   hash columns, all eight angle columns, the state words and the list.
4. **The branch targets.** 125 instructions with 6 internal branches, each at the
   original's relative offset.
5. **The comparator proves its own mechanism.** `--selftest` gained the in-table case
   (registered → PASS with 26 operands resolved; a column moved by eight bytes → FAIL
   as an instruction difference), and the thirteen earlier cases are unchanged.
6. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1
   oracle-verified`, and `zp_dump --ooc-selftest` passes.

**What stays unproven.** The checker runs only when the game's sites call it, so the
offline proof is of code, tables and column wiring; in play a wrong column would show
as one pop-up-light angle being wrong, which is cosmetic. The `[PopupLightAngles]`
*contents* are the reader's (the same C++ reader in both trees, pinned by
`zp_model_tables`), and the shipped INI's section is absent, which is why both sides
compare as zero here.

### 2g. This pass: the headlight-cone check, and the word past its table

The last class-A piece carried a **behavioural** blocker rather than a mechanical one:
its walk reads one word past its 512-entry hash table. That word is not dead space,
and the whole point of this pass is that it did not have to be faked.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| headlight-cone check | `0x1001AD20` | `0x8B` | 46 | `force_headlight_shadows_check` | 36/46 | 6 absolute operands + 2 table displacements + 2 `call` immediates |

No mnemonic, register, frame offset, size or branch form differs (0 `other`,
0 branch), and the body is 139 bytes on both sides — the original's exact size — so
every internal branch sits at the same relative offset. The proof's line is
`PASS force_headlight_shadows_check (46 instruction(s), 6 absolute operand(s), 2 table
operand(s))`.

**The body.** Seed the offset float (`dword_101580C0`) with 2.0f, load slot 0
(`mov eax,ds:[0x1015ADB8]`), and per iteration ask the game's model store for that
hash's model index and compare it with the object's signed word at `+0x2E`. On a match
publish that slot's float (`movss ds:[0x1015B5C0+esi*4]`); otherwise load the next slot
with `mov eax,ds:[esi*4+0x1015ADBC]` — the table's base **plus four**, because ESI
counts the slot just consumed — and stop at the first zero word. The lookup and the
object are read unguarded, as the original's `call ds:[0x10159C0C]` and `movsx
eax,word [edi+2Eh]` are; the call comes first, so a null lookup faults on the call and
a null object on the load.

**The word past the table, and what it is.** With ESI at 511 the "next slot" load is
`ds:[0x1015B5B8]`, the word immediately after the 512-slot table in the original's
`.data`. The evidence, from the IDB:

- **At load it is zero.** The original's `.data` has a `0x504D4` virtual extent but
  only `0x1000` of raw bytes (`0x10158000..0x10158FFF`), so `0x1015B5B8` is in the
  uninitialised tail the loader maps zeros into; `read_table()` reads it as
  `00 00 00 00`.
- **Something does write it before this check can run.** Two stores, both inside the
  ASI's own car-shadow code (`sub_10010B60`): `mov [0x1015B5B8],ecx` at `0x100128B5`
  with `ecx = eax+0xB+[eax+7]` (the game call target of the recovered
  `56 6A 02 53 53 57 E8 ? ? ? ?` site, whose resume is `eax+0xB`), and
  `mov [0x1015B5B8],eax` at `0x10014F98`. Six more sites *read* it, four of them the
  `call ds:[0x1015B5B8]` pairs in `detour_100068A0` — it is
  `CarDynamicShadowFix`'s shadow-function pointer.
- **So the value is knowable, and this tree already holds it.** `zpatch_carparts_shadow_fn`
  is this module's slot for exactly that value, computed by the same formula
  (`resume + [match+7]`, resume = `match+0xB`) from the same recovered site
  (`source/vehicles.ixx::PublishCarDynamicShadowFix`), and the transcribed car-parts
  bodies already read it. Reproducing the neighbour as a *copy* would diverge the
  moment one side was written; reproducing it as **the same slot** is what makes the
  walk read what the original's does.

**Our layout, and the extended registration.** The four objects are laid out in the
original's order in `source/config.ixx`, so the whole reachable walk matches:

```
0x1017f6a8  zpatch_headlight_cone_hashes[512]   <- dword_1015ADB8
0x1017fea8  zpatch_carparts_shadow_fn           <- dword_1015B5B8  (the neighbour)
0x1017feac  zpatch_fchs_resume                  <- dword_1015B5BC  (this check's resume)
0x1017feb0  zpatch_headlight_cone_values[512]   <- dword_1015B5C0  (+0x808 from the hashes)
```

and the hash table is registered with the original's **extended 2,052-byte range**
(512 words + the neighbour) so the proof compares that read:

```cpp
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_headlight_cone_hashes),
    0x1015ADB8u, sizeof(zpatch_headlight_cone_hashes) + sizeof(uint32_t));  // 2052
Bodies::RegisterTable(reinterpret_cast<uintptr_t>(zpatch_headlight_cone_values),
    0x1015B5C0u, sizeof(zpatch_headlight_cone_values));  // 512 * 4 = 2048
```

```
table 1015ADB8 _zpatch_headlight_cone_hashes 2052
table 1015B5C0 _zpatch_headlight_cone_values 2048
```

Both sides' 2,052-byte ranges are all zero in the shipped image, so both hash to
`02610d9b8ad1da81` (the 2,052-byte zero hash, distinct from the 2,048-byte
`867e6dd4bf81dfd6` the other tables share), and the two 2,048-byte offsets arrays
hash to `867e6dd4bf81dfd6`. `zpatch_fchs_resume` is included because the walk's *next*
read at ESI=512 is `0x1015B5BC`; leaving it out would stop the walk one word earlier
than the original's, which is exactly the guard this pass removes.

**How this pass was verified**

1. **Proof row.** The PASS line above; `python tools/verify_bodies.py …` →
   `138 body(ies), 0 failure(s)`.
2. **Byte comparison** (`.scratch/steer/compare_body.py cone`): 36/46 identical; the
   ten differences are the six absolute slots, the two table displacements (the base
   and the base+4) and the two `__security_check_cookie` call immediates.
3. **The operand map.** `.scratch/steer/operand_map.py cone` resolves every absolute
   operand and call immediate through the linker map: **8 distinct operands, 0
   declared but unused, 0 mismatch(es)**, including the base+4 read that is the
   neighbour word at ESI=511.
4. **The layout is checked, not asserted.** The map puts
   `_zpatch_carparts_shadow_fn` exactly 2,048 bytes after the array and
   `_zpatch_headlight_cone_values` exactly 2,056, which is the original's spacing
   (`0x1015B5B8 - 0x1015ADB8 = 0x800`, `0x1015B5C0 - 0x1015ADB8 = 0x808`).
5. **The branch targets.** 46 instructions with 4 internal branches, each at the
   original's relative offset.
6. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1
   oracle-verified`, and `zp_dump --ooc-selftest` passes.

**What is proven and what is not.** The instructions, both tables and the neighbour
read are proven; what is not is the neighbour's *runtime* value, which depends on
`CarDynamicShadowFix` publishing the same game address through a different code path
than the original's caching function. Both compute `resume + [match+7]` from the same
recovered site, and both are zero when that key is off, so the values agree in every
configuration this tree can produce — but that agreement is an argument from the two
formulas, not something the offline proof can measure. It matters only for a section
that fills all 512 slots with resolvable names, which then makes the original (and
now this tree) hash-lookup a code address and continue; no shipped or sample INI comes
near it. `docs/parity.md` carries the play item.

### 2h. This pass: the OOC archive builder, and the twelve helpers that made it class D

Class D's largest entry is gone, and it is gone for the reason the class exists: the
builder's twelve callees were the only thing keeping it rebuilt, so the twelve were
transcribed with it. `0x1004ACE0` is what turns the registered fake-pak entry into the
in-memory archive the mount string points at — a 20-byte header, one 16-byte record
per entry, the names, then at `0x800` the payloads each padded to the next page — and
it is now the original's 314 instructions.

| piece | recovered VA | size | insns | row | byte-identical | differs only in |
|---|---|---:|---:|---|---:|---|
| archive builder | `0x1004ACE0` | `0x461` | 314 | `ooc_archive_build` | 275/314 | 3 absolute operands + 30 `call` immediates + 6 `mov …, offset Block` immediates |
| zero the 20-byte header | `0x1004AB70` | `0x10` | 5 | `ooc_pak_zero_header` | 5/5 | — |
| zero the 16-byte record | `0x1004AB80` | `0x9` | 4 | `ooc_pak_zero_record` | 4/4 | — |
| merge the record's pad word | `0x1004AB90` | `0x1B` | 9 | `ooc_pak_set_pad` | 9/9 | — |
| pages for one size | `0x1004ABB0` | `0x12` | 6 | `ooc_pak_page_count` | 6/6 | — |
| append a chunk | `0x1004ABD0` | `0x2D` | 20 | `ooc_pak_append` | 17/20 | 2 absolute operands + 1 `call` immediate |
| name bytes | `0x1004AC00` | `0x3B` | 28 | `ooc_pak_name_bytes` | 26/28 | 2 absolute operands |
| payload pages | `0x1004AC40` | `0x42` | 23 | `ooc_pak_page_total` | 21/23 | 2 absolute operands |
| vector begin | `0x100564F0` | `0x3` | 2 | `ooc_vector_begin` | 2/2 | — |
| vector end | `0x100564E0` | `0x4` | 2 | `ooc_vector_end` | 2/2 | — |
| vector size | `0x100564C0` | `0x17` | 9 | `ooc_vector_size` | 9/9 | — |
| vector clear | `0x10056500` | `0x18` | 11 | `ooc_vector_clear` | 10/11 | 1 `call` immediate |
| entry name (std::string c_str) | `0x10057710` | `0xC` | 6 | `ooc_entry_name` | 6/6 | — |

13 bodies, 439 instructions, 1,427 bytes, 392 byte-identical (89.3 %); the 47
differences are 9 absolute operands, 32 `call` immediates and 6 `offset Block`
immediates. Every body's byte size is the original's exactly (`0x461` for the builder),
so every instruction sits at the same relative offset, and there is no `other`
difference (no mnemonic, register, frame offset, size or branch form changed) and no
branch difference.

**What the twelve are.** They are the builder's own private helpers and nothing else
calls them: `0x1004AB70`/`0x1004AB80` zero the stack header/record with one SSE
16-byte store, `0x1004AB90` merges the pad length into the record's `+0x0C` word,
`0x1004ABB0` is the `/0x800` round-up (the compiler's `cmovne`), `0x1004ABD0` is the
append — `memcpy(archive + cursor, src, edx * count)` and advance the cursor at
`dword_10159C78` — `0x1004AC00` sums the name bytes, `0x1004AC40` sums the payload
pages, `0x100564F0`/`0x100564E0`/`0x100564C0`/`0x10056500` are the vector's
begin/end/size/clear, and `0x10057710` is the entry's `std::string::c_str` (the
capacity test at `+0x14`). All of them are plain streams: no exception frame, no TLS
guard, no indexed absolute operand.

**The builder's stream.** One /GS frame (`[__security_cookie]` at entry and
`__security_check_cookie` at exit — ordinary instructions, §1), EBP-relative locals,
and thirty direct `E8`s. Two details are the original's and were kept: the
`imul ecx, eax, 0` at `0x1004AD55` is the compiler's zero-offset multiply that shapes
the following `cmp [edx+ecx], 0x05435352` (it is not dead code), and the record's
index is the running page count plus one with the pad word carrying bit `0x2000` when
the stored offset is non-zero.

**The one new module slot.** `dword_10159C78` is the builder's write cursor, so it is
`zpatch_ooc_offset` here. The transcribed builder zeroes it, moves it to `0x800` for
the payload region and advances it on every append; `OocInitialise` reads it back after
the call for the archive's byte count, which is exactly what `sub_1004B750` does
(`mov ecx, dword_10159C78 / mov dword_10159BFC, ecx`). The vector itself is
`zpatch_ooc_pak`: a real `std::vector<OocPakEntry>` whose first three words are the
original's three pointers at `0x1016E43C/40/44`, declared `extern "C"` so the
transcribed bodies name one short `_name` symbol.

**How this pass was verified**

1. **Proof rows.** The thirteen PASS lines are
   `PASS ooc_archive_build (314 instruction(s), 3 absolute operand(s))`,
   `PASS ooc_pak_append (20, 2)`, `PASS ooc_pak_name_bytes (28, 2)`,
   `PASS ooc_pak_page_total (23, 2)`, `PASS ooc_pak_zero_header (5, 0)`,
   `PASS ooc_pak_zero_record (4, 0)`, `PASS ooc_pak_set_pad (9, 0)`,
   `PASS ooc_pak_page_count (6, 0)`, `PASS ooc_vector_begin (2, 0)`,
   `PASS ooc_vector_end (2, 0)`, `PASS ooc_vector_size (9, 0)`,
   `PASS ooc_vector_clear (11, 0)` and `PASS ooc_entry_name (6, 0)`, and
   `python tools/verify_bodies.py …` → `151 body(ies), 0 failure(s)`.
2. **Byte comparison** (`.scratch/ooc_builder/compare.py`): 392 of 439 instructions
   byte-identical, every body the original's exact byte size, `slot 47, branch 0,
   other 0`.
3. **The operand map.** `.scratch/steer/operand_map.py builder` resolves every
   absolute operand and call immediate through the linker map against a declared
   pairing: **19 distinct operands, 0 declared but unused, 0 mismatch(es)**. That is
   what says `call 0x100564F0` became the vector's *begin* and not its end, that
   `mov ecx, 0x1016E43C` became `_zpatch_ooc_pak` and not the cursor beside it, and
   that the two `operator new`/`free`/`memset` sites reach the CRT through the
   forwarders. Four expected `/OPT:ICF` folds show up and are harmless, because each
   pair is the same bytes with the same behaviour: `?OocVectorBeginBody` folds into
   `Game::Module()`'s `mov eax,[ecx]; ret`, `?OocFreeBlock` into `operator delete`,
   `?OocAllocate` into `loading.ixx`'s `DlcAlloc` (both `::operator new`) and
   `?OocZeroBlock` into `audio.ixx`'s `AudioZeroBlock` (both `memset`).
4. **The behaviour is pinned offline.** `zp_dump --ooc-selftest` → `ooc selftest:
   PASS`: the transcribed builder produces the recovered 182,272-byte archive with
   `fnv1a 0xB5AF7B10` and the header's `16`/`0xE9` words, from the decoded table whose
   `fnv1a` is `0xC883859F`. That is a behavioural proof of the whole builder, not only
   a shape proof — the strongest evidence any piece in this file has.
5. **The branch targets.** `151 row(s), 321 internal branch target(s), 0 failure(s)`.
6. **The surrounding behaviour is unchanged.** `ctest --preset release` is 12/12,
   `python tools/parity_report.py 9` is `98 key(s) closed, 0 open, 1
   oracle-verified`, and `OutOfCommissionFix`'s two arm writes are the same bytes as
   before (the builder is not an install site at all).

**What stays rebuilt, and why.** One callee of the clear: `0x1005CD50`, the
`std::_Destroy_range` loop `0x10056500` calls. It is the CRT's `std::string`
destructor loop — it reads the string's capacity, and past `0x1000` the allocator's
block header, before calling `_invalid_parameter_noinfo_noreturn` — so it is
image-internal CRT state this module cannot name, and it is the one place the class-D
argument still holds. `OocDestroyEntries` performs the same stores on the same 44-byte
stride for the entry layout this module uses (an inline 16-byte name buffer, capacity
`0x0F`), and the free branch is unreachable from the tree's own data in both trees:
the only entry ever registered is `"finale2.sco"`, eleven bytes.

**What remains unproven.** The builder only runs when the game calls the wrapper at
`0xB70733`, so the offline proof is of code, operands and the archive bytes; the mount
string and the device register are seen in play (`docs/parity.md`). The vector's
element layout is a model — the 44-byte struct keeps the original's field offsets,
including the `std::string` header the destroy loop touches — and its `push_back` is
the rebuilt init's, not a transcribed one.

### 2i. This pass: the save converter, behaviourally

The converter (`0x10051B30`, class B) was this catalogue's highest-risk entry and its
weakest evidence: the walk had been replayed once by a throwaway script, nothing in the
tree executed it, and its only measurement was that the key's cell writes the right five
bytes. It is a test now. `tests/save_walk.cpp` (`zp_save_walk`, the third ctest entry)
runs the walk the production path runs and asserts, per file, what it decided — against
the file re-read by the test's own parser.

**The seam.** `source/loading.ixx` grew one exported, test-only entry,
`Loading::WalkSaveFile`. It is not a second implementation: the converter is split into
`NewSavesWalkFile` (open the file, check the magic at `+0xC`, walk the eighteen 9-byte
headers with the original's `fread` / `operator new(size - 9)` / `fread` / `free`
sequence, record the three size tests) and the patch step it already had, and the seam
calls the first of those and then `NewSavesChoosePatch` — the same function the patch
step uses to select its three values, so the choice a test reads cannot drift from the
value that is written. The seam patches nothing, installs nothing and touches none of the
module's published state (`byte_1015AD98/99/9A`), so calling it cannot change the image
or the module. The one production-path difference is *when* the three flags become module
state: after the walk, instead of inside its loop. Nothing can observe it — the throw
that ends a bad walk leaves the converter — and the write set is unchanged
(`parity_report.py 9` is the same line, `ctest`'s dump baseline is unchanged).

**The real saves.** Two, in one account:
`game_refs/saves/GTA IV/savegames/user_ee000000deadc0de/SGTA400` (1,977,934 bytes) and
`SGTA401` (1,978,282). The test finds saves by *content* — any regular file under the
given root whose bytes at `+0xC` are `SAVE` — so a second account would be converted too;
the two accounts under `game_refs/saves` that carry no save (`user_e0001c4aabed3e68`,
`user_invalid`) are `ControlMap.dat` only and are correctly not discovered. Both saves
have the same eighteen chunk headers, read by the test's own `std::ifstream` parser and
answered identically by the walk:

| # | file offset | tag | size | | # | file offset | tag | size |
|---:|---|---|---:|---|---:|---|---|---:|
| 0 | `+0x110` | `BLOCK` | `0xB9` | | 9 | `+0x1D3585` | `BLOCK` | `0x3AB1` |
| 1 | `+0x1C9` | `BLOCK` | `0xD9` | | 10 | `+0x1D7036` | `BLOCK` | `0x2F55` |
| 2 | `+0x2A2` | `BLOCK` | `0x1051` | | 11 | `+0x1D9F8B` | `BLOCK` | `0xF9` |
| 3 | `+0x12F3` | `BLOCK` | `0x1C2B23` | | 12 | `+0x1DA084` | `BLOCK` | `0x549` |
| 4 | `+0x1C3E16` | `BLOCK` | `0x10F8` | | 13 | `+0x1DA5CD` | `BLOCK` | `0x1729` |
| 5 | `+0x1C4F0E` | `BLOCK` | `0xF` | | 14 | `+0x1DBCF6` | `BLOCK` | `0x200` |
| 6 | `+0x1C4F1D` | `BLOCK` | `0xC8D` | | 15 | `+0x1DBEF6` | `BLOCK` | `0x1916` |
| 7 | `+0x1C5BAA` | `BLOCK` | `0xD55A` | | 16 | `+0x1DD80C` | `BLOCK` | `0xCC7` |
| 8 | `+0x1D3104` | `BLOCK` | `0x481` | | 17 | `+0x1DE4D3` | `BLOCK` | `0x34F1` |

The walk's own answers for the same eighteen offsets agree with that table tag for tag and
size for size (36 assertions per save). The three size tests then measure
`0x3AB1`/`0x1729`/`0xCC7` against `0x4C45`/`0x1741`/`0x0E77`, i.e. the *older* layout:
`forceRead=0`, `extraBlock=0`, `newChunk16=0`, so the converter restores `74` at
`0x872185` and writes `0CBEh` and `75 26` at `0x7D8C50` — the three values the patch
step would write, and exactly what the original's three `cmovne` pairs produce. Each
assertion prints its value and its source: the file offset for the chunk table, the
`byte_1015AD9x` slot and the compared size for a flag, the game site for a patch value.

**The synthetic layouts.** Three images written by the test itself (a zeroed 0x110-byte
header with `SAVE` at `+0xC`, then `BLOCK` headers whose sizes the test chooses):

| image | chunk 9 / 13 / 16 | flags | `0x872185` | `0x7D8C50` size | `0x7D8C50` branch |
|---|---|---|---|---|---|
| `new_layout.bin` (every other chunk `0x9`) | `0x4C45` / `0x1741` / `0x0E77` | 1 / 1 / 1 | `EB` | `0E6Eh` | `90 90` |
| `chunk13_only.bin` (every other chunk `0x9`) | `0x9` / `0x1741` / `0x9` | 0 / 1 / 0 | `74` | `0CBEh` | `75 26` |
| `old_layout.bin` (the real save's ids and sizes) | `0x3AB1` / `0x1729` / `0xCC7` | 0 / 0 / 0 | `74` | `0CBEh` | `75 26` |

`chunk13_only.bin` is the extra image, and the reason is worth keeping: chunk 13's flag
decides the second-load hook's twelve-byte block and *must not* move any of the three
patch values, so a rebuild that crossed the two flags would fail there while
`new_layout.bin` and the real saves still passed.

**The faults, and what each one needs.**

- **A chunk size below nine** (`size_zero.bin`: a valid header and one chunk header whose
  size field is 0; `size_eight.bin`: nine good chunks and then a size of 8). The
  original's `add eax,0FFFFFFF7h` (`0x10051C14`) feeds `operator new` (`0x100603F1`)
  unchecked, so the payload underflows and the allocation throws. This is deterministic
  and is asserted **in this process**, with the original's own type: both answer
  `bad_alloc`, and any other exception type (or none) fails the check. The second file
  is the one that matters — it proves the fault is per chunk, not a header check.
- **A truncated header** (0x20 bytes). The `fread(header, 0x110, 1)` at `0x10051BB3` is
  unchecked, so the bytes past the file are whatever the stack held, and the magic test
  reads them. Two consequences, and the test states each as the behaviour it is:
  - a 0x20-byte file *carrying* `SAVE` at `+0xC` is **not** rejected on its header —
    observed answer `bad_alloc` (the chunk it then reads from the stack underflows), and
    either `walked` or `bad_alloc` would satisfy the check, because both are "the header
    was not validated";
  - a 0x20-byte file *without* `SAVE` at `+0xC` is rejected on the magic, deterministically,
    because the bytes the test reads lie inside the file.
- **A file shorter than the magic offset** (4 bytes). Here the magic itself is
  uninitialised, so which of the original's three paths runs is a property of the
  *process*, not of the file. That is the one case a **subprocess** has to carry: the
  test starts itself twice (`--child <file> <report>`), once per 4-byte file, and
  requires the two answers to be equal and to be one of `walked` / `notASave` /
  `bad_alloc` — equal because the two files differ only in bytes the walk never reads.
  Observed: `notASave` in both, i.e. this build's child stack does not spell `SAVE`.

So: the `bad_alloc` fault is expressible as an in-process, typed assertion; the
uninitialised-magic fault is not — not because the throw is uncatchable (it is, and the
test catches it) but because *which* branch runs is not a function of the input, so the
assertion that survives is over a fresh process, and over two inputs that must agree.

**The test can fail.** It was checked against a mutated converter: with chunk 9's size
comparison changed to `0x1741`, `zp_save_walk` fails exactly two lines and no others —
`new_layout.bin: forceRead=0 (byte_1015AD98; chunk 9 size 0x4C45 vs 0x4C45)` and
`new_layout.bin: branch opcode 0x74 at site 872185h (chunk 9 size 0x4C45; newer layout
-> 0xEB)`. The real saves do *not* fail under that mutation (their chunk 9 is `0x3AB1`,
which matches neither constant), which is precisely why the synthetic newer layout
exists. The three file shapes are therefore not redundant: the real saves pin the walk
and the old layout, `new_layout` pins the new one, `chunk13_only` pins the flags'
independence.

The expectations themselves are the test's, not the converter's: a real save's three
values are derived from the sizes the test's parser read (so a newer-layout save dropped
into `game_refs/saves` is converted against the newer triple instead of failing the
build), while each synthetic file names the layout it was written for, so its three
literals are asserted *and* the file's own chunks 9 and 16 have to prove it is that
layout.

**Gates.** `cmake --build --preset release` exit 0; `ctest --preset release` 13/13 (the
new test is `zp_save_walk`, 258 checks, 0 failures); `python tools/parity_report.py 9` →
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`python tools/verify_bodies.py …` → `151 body(ies), 0 failure(s)`;
`zp_dump --ooc-selftest` → `ooc selftest: PASS`.

**What stays unproven, and it is still most of the risk.** The test proves the *decision*,
not the *writes*. The three site patches and the two hooks behind them run only when the
game loads a savegame, so no offline harness executes them: the test shows the converter
would choose `74`/`0CBEh`/`75 26` for both real saves, and nothing shows those bytes
landing at game `0x872185`/`0x7D8C50` correctly, the two hooks' frames behaving, or the
prepare step building the right path — the destination of the write is the key's cell and
the body proof's business, not this test's. Also unproven here: what the game does with
the converted save (whether the walk's answer is the *right* answer for the data behind
each chunk is a game-side question), and the `std::bad_alloc`'s path through the game's
frames, which is only reachable in play. §5 item 1 keeps the in-game exercise.

---

### 2j. This pass: the arena methods, behaviourally

The three arena methods of the `NewMemorySystem` table (§3 class D) were the catalogue's
weakest evidence: an allocator rebuilt from the disassembly, whose only support was that
the rebuild's arithmetic read like the instructions. An allocator's failure mode is
corruption in play, so "the arithmetic matches" is not the property that matters. It is a
test now. `tests/nms_arena.cpp` (`zp_nms_arena`, 246 assertions) drives the module's own
three bodies — the same three `PublishNmsVtable` puts in the table — on the process heap,
with no game image, and pins the scheme clause by clause.

**The seam.** `source/loading.ixx` grew one exported, test-only entry,
`MemoryArena::Get`, which answers the three `__stdcall` entries and runs the registry
lock's one-time setup. It is not a second implementation: it names `NmsAllocateBody`,
`NmsFreeBody` and `NmsContainsBody`, and `PublishNmsVtable` takes the addresses of those
same three functions. It writes no site, installs no arm, and touches neither the
published table nor the object the installer points at; the only module state it can
change is that lock's initialisation, which the game's own allocator-init performs anyway.
The one refactor it needed is the lock: `PublishNewMemorySystem` used to own the
`InitializeCriticalSection`, and both paths now call `PrepareArenaRegistry`, a no-op on
its second call, so the pipeline still initialises the section once, before
`PublishNmsVtable`. The pipeline's write set is unchanged (`parity_report.py 9` is the
same line).

**The scheme, and what each assertion class pins.** From the disassembly (image base
`0x10000000`; the instruction addresses are in the test's own header):

    unit    = align < 0x10 ? 0x10 : align      0x10038378/0x1003837D/0x10038383
    want    = size  < unit ? unit : size       0x10038380/0x10038386/0x10038388
    base    = HeapAlloc(heap, 0, want+2*unit)  0x1003838B, 0x10038398
    aligned = ((base-1)/unit + 2) * unit       0x100383A6-0x100383B0
    [aligned-4] = aligned - base               0x100383B3-0x100383B7
    memset(aligned, 0xCD, want)                0x100383C2-0x100383C9
    registry += aligned                        0x100383D1-0x100383F0

| class | assertion | clause pinned |
|---|---|---|
| scheme, over 42 `(size, align)` pairs: sizes `0`, `1`, `0xF`, `0x10`, `0x11`, `0x1000` against alignments `0`, `1`, `4`, `8`, `0x10`, `0x20`, `0x100` | the pointer is a multiple of `unit` | `unit = max(0x10, align)` and the `* unit` in the rounding |
| | `[pointer-4]` is in `[unit, 2*unit)` | the `(base-1)/unit + 2` rounding. With `base = q*unit + r`, `(base-1)/unit` is `q-1` at `r = 0` and `q` otherwise, so the distance is `unit` or `2*unit - r`, and that single range pins the `-1`, the `+2` and the slack the `+2*unit` request leaves |
| | `HeapSize(base)` is in `[want + 2*unit, want + 2*unit + 8)` | the `lea eax,[edi+esi*2]` request itself (the heap reports it back rounded to its 8-byte granularity) |
| | `size` bytes are writable and read back | the block is the caller's |
| | none of the 42 live `[pointer, pointer+want)` ranges overlap | each pointer sits inside its own block: the distance is at least a unit and the fill fits in the slack |
| scheme limits | `align 3` gives a `0x10`-aligned pointer, `align 0x30` a `0x30`-aligned one, and the flags argument is ignored | the rounding target is `unit`, not the request; `[ebp+10h]` has no reader and the HeapAlloc flags are the literal `0` at `0x1003838F` |
| the distance word and the fill | `[pointer-4]` is the distance to the base and still there after the memset; `[pointer, pointer+want)` is `0xCD` for `size 0x11`, `size 0`, `size 1` | the store at `0x100383B7` happens before the `push 0CDh` at `0x100383C3`; the memset's count is `want`, not `size` |
| the fill's end | a block poisoned `0xAA`, freed and taken again comes back with `want` bytes of `0xCD` and its slack bytes still `0xAA` | the memset stops at `aligned + want`, and `2*unit - distance >= 1` of the block is left over — the fill is the usable region and not past it |
| membership | a live pointer answers `1`; a freed one, `pointer+1`, `0`, a stack address and an address never handed out answer `0`; freeing one leaves the other a member | `0x100385A0`'s `cmp edi,esi / setne al` (only AL is read, as the body sets only AL) and the erase at `0x100384C0` |
| free | `release(nullptr)` is a no-op and leaves the arena usable; the next same-size request gets the freed block back; the pointer stops being a member | `0x10038449`'s null test, `0x10038457`'s base recovery, `0x10038474`'s `HeapFree`, then the erase |
| the failed request | `allocate(0x7FFFFFF0, 0x10, 0)` answers null, throws nothing, and the arena allocates, fills, registers, writes and releases normally afterwards | `0x100383A0`'s `test edx,edx / je` and its zero path |

**The fill's end, made observable.** The bytes after the fill belong to the block but
have no defined value, so "the memset stopped" cannot be read off a fresh block. The test
makes it readable: it poisons the block with `0xAA` (all of it except the four-byte
distance word, which the free needs to find the base), frees it, and asks for the same
`(size, align)` until the heap returns the block. The heap is free to serve that request
from anywhere — it returns the just-freed block most of the time, and sometimes not (a
free block can be coalesced, or the request served from new space) — so the test does not
assume it: it holds whatever else comes back (which also stops a probe from spinning on
its own block) and retries with a fresh block, sixteen attempts of sixteen probes. It was
run 210 times without a miss; a heap that never returned a freed block would be reported
as a failure, not skipped. The pair is `size 0xF, align 1`, one byte below the unit, so
the check still holds under the mutation that fills `size` instead of `want`. The tail it
then reads is the `2*unit - distance` bytes between `aligned + want` and the end of the
block (measured: `0x8` to `0x10` on this heap).

**The test can fail.** Three mutations of the scheme were run and reverted:

- `want + unit * 2u` → `want + unit` (the request's slack). The `HeapSize` check fails for
  the pairs that run before the process dies — ten lines on one run, five on another, one
  on a third, depending on where the heap hands a corrupted block back — and the process
  then exits with `0xC0000374` (`STATUS_HEAP_CORRUPTION`): the `want`-byte fill no longer
  fits the block, so shrinking this term is a memory-safety break, not a size change. That
  is why the check has to name the clause before the fault, and why the clause cannot be
  weakened.
- `want = size < unit ? unit : size` → `want = size` (the floor). 28 failures: 25
  `HeapSize` lines (every pair whose `size < unit`, i.e. `want` is the floor rather than
  the request) and 3 fill lines (`size 0 -> want 0x10`, `size 1, align 4 -> want 0x10`,
  and the poisoned block's `0x10` bytes); 218 pass, exit 1, no fault.
- the memset's count `want` → `size`. 3 failures, all in the fill class (the same three
  lines), 243 pass, exit 1. The `size 0x11` fill check still passes there because for
  that pair `size == want` — which is exactly why the `size < unit` pairs are in the
  table.

**Gates.** `cmake --build --preset release` exit 0; `ctest --preset release` 15/15 twice
(the new test is `zp_nms_arena`, 246 checks, 0 failures);
`python tools/parity_report.py 9` → `build 9: 98 key(s) closed, 0 open, 1
oracle-verified (baseline incomplete)`; `python tools/verify_bodies.py …` → `151
body(ies), 0 failure(s)`; `zp_dump --ooc-selftest` → `ooc selftest: PASS`.

**What stays unproven.**

- **A double free.** Not tested, and it cannot be asserted offline. The original's free
  has no membership check in front of it: `0x10038449` tests the incoming pointer for null
  and nothing else, `0x10038457` recovers the base from `[pointer-4]`, and `0x10038474`
  calls `HeapFree` unconditionally. A second free therefore reads `[pointer-4]` out of a
  block the heap owns again — the process heap writes its free-list links into a freed
  block's first bytes, and the first free's erase removed the registry entry, so the
  second call is not intercepted either. Its target is the heap manager's state, not the
  original's arithmetic, and whether that is a no-op, a `STATUS_HEAP_CORRUPTION`
  termination or silent corruption is decided by the heap implementation. The rebuild
  keeps the missing guard (the same order: free, then erase), and the test asserts only
  the part that is knowable — that the pointer stops being a member after the first free.
- **The container behind the registry.** The original keeps its pointers in a `std::_Hash`
  whose globals the free body inlines; the rebuild keeps a `std::unordered_set` behind an
  equivalent lock. Only membership is observable through the three methods, so no
  assertion can distinguish them: the hash function, the bucket layout, the iteration
  order and the node allocation are invisible to every caller of the table.
- **Concurrency.** The test is single-threaded: it enters and leaves the lock but never
  contends it, so the lock's own behaviour under two threads is not exercised.
- **The wrapping request.** `0x1003838B` is a 32-bit `lea`, so a size at or above
  `2^32 - 2*unit` (`0xFFFFFFE0` at the `0x10` floor) wraps the `want + 2*unit` sum and the
  allocate returns a tiny block for a nearly four-gigabyte memset — the original's fault,
  preserved by the same arithmetic. The test does not run it (it faults, and a fault is
  not an assertion); it is recorded as a preserved fault in `parity.md`. The failure-path
  check uses `0x7FFFFFF0`, whose sum `0x80000010` does not wrap and that no 32-bit heap can
  satisfy.

---

### 2k. This pass: the OOC init, behaviourally

The init (`0x1004B750`, class B, 991 instructions) was the largest rebuilt piece with no
behavioural test. `zp_dump --ooc-selftest` (§2h) pins what the transform *produces* — the
decode's table hash, the archive's 182,272 bytes and `fnv1a 0xB5AF7B10` — but it says
nothing about the init's own semantics. `tests/ooc_init.cpp` (`zp_ooc_init`, 54 checks)
pins the four things that were left: the guard's real behaviour (the body re-runs and
leaks), the cursor it publishes, the registration that happens even when the decode's
checks fail, and the entry's name. No game image.

**The guard, and what it actually does.** The original's first instruction sequence is

    0x1004B77B  a1 00 9c 15 10     mov  eax,[0x10159C00]   ; dword_10159C00
    0x1004B780  85 c0              test eax,eax
    0x1004B782  0f 85 ab 0b 00 00  jne  0x1004C333         ; the shared epilogue

and `0x1004C333` *is* the epilogue that follows the final `mov [0x10159BFC],ecx`: unwind
the EH frame, pop the saved registers, `__security_check_cookie`, `ret`. So the guard is a
skip of the body **after** the byte-count publish, and `EAX` still holds the guard word on
that path — a guard that was ever set would make the function answer that word as if it
were the archive pointer, not just do nothing. The slot is named **once** in the whole
image: at the read itself. Nothing writes it, so the branch is never taken.

The rebuild carries no guard variable at all, and that is that fact in code rather than an
omission: with the slot never written, `if (guard != 0) return guard;` would be dead. The
test pins the observable consequence — the body runs on every call — and its falsification
is the re-introduced guard (§*The test can fail*), so the property is defended rather than
asserted. (One other store of the original's is dead in the same way and is not carried:
`0x1004B7AA mov dword ptr [0x10159C2C], 2BD50h` writes a slot named once in the image, the
store, so nothing reads it. The rebuild's entry gets the same 179,536 as a stack field.)

**The seam.** `source/ooc.ixx` grew one exported, test-only namespace, `OocInit`, whose
entries are all things the unit already has:

| entry | is |
|---|---|
| `Initialise` | the function the mounting wrapper `OocWrapBody` (0x1004C350) calls |
| `Current` | the three module slots, read and not run: `zpatch_ooc_offset`, `zpatch_ooc_state`, `zpatch_ooc_pak.size()` |
| `Decode` | the transcribed driver `0x1004B570` |
| `Register` | the push_back `sub_1004B750` makes at `0x1004C287` |
| `FrontEntry` | the front element of `zpatch_ooc_pak` (the original's `0x1016E43C`) |
| `ClearEntries` | the transcribed vector clear `0x10056500` the builder ends with |
| `ResourceBytes` | `source/ooc_finale2.inc`'s array, address and size |

It is not a second implementation and the init's code is untouched, so what a test drives
is the shipped sequence. It writes no site, installs no arm, scans nothing; the only module
state a call changes is the state the game's own call changes. The clear takes its vector in
`ECX`, which a C++ call cannot set, so the seam enters it through a small naked thunk
(`mov ecx, OFFSET zpatch_ooc_pak; jmp OocVectorClearBody`) exactly as the builder's
`ooc_build_done` does. **None of this reaches the shipped image**: the namespace's entries
have no caller inside the ASI and no PE export, so `/OPT:REF` drops them — the linker map
has no `OocInit` symbol at all — while the test links the same object file and calls them.
The shipped ASI's write set and its 151 proof rows are unchanged with the seam in the tree
(`zp_dump_empty`, `zp_body_proof`).

**The measurement that makes "twice" a count, not an impression.** `OocAllocate` is
`::operator new` (the original calls the CRT's scalar `operator new` thunk at
`0x100603F1`), so the test replaces the global `operator new`/`delete` family — the complete
C++17 set, each entry routing to `std::malloc`/`std::free` — and ledgers every block of
exactly 182,272 bytes, plus every `operator delete` of one. That turns two sentences into
numbers: one archive is allocated per call, and no archive is ever freed. The archives
themselves are also compared: three calls answer three distinct pointers, each byte-identical
to the first, and the first is still intact (and still `fnv1a 0xB5AF7B10`) after the other
two builds — had the init freed it, the next build would have taken the block back or
overwritten its head.

**The assertions.**

| class | assertion | clause pinned |
|---|---|---|
| guard | three calls: three distinct archives, three 182,272-byte allocations, `g_archive_frees == 0`, the first archive still the pinned one | `0x1004B77B`-`0x1004B782` and the absent writer: no call latches anything, and the previous archive leaks |
| cursor | after every call `dword_10159C78` is 182,272 and `dword_10159BFC` is the same word | the builder's final cursor, then `0x1004C327`/`0x1004C32D` |
| the archive | `fnv1a` over exactly the published count is `0xB5AF7B10`; magic `0x86723DFF`, version `3`, entry count `1`, name bytes `28`, the words `16`/`0xE9`; the record's size `179536`, offset `0`, index `1`, pages `88`, pad `688`; the name `"finale2.sco"` at `+0x24`; the payload at `0x800` is the decoded table (memcmp of 179,536 bytes); the 688 bytes after it are zero | the builder `0x1004ACE0` as the init calls it, and the registration it consumed |
| registration | a damaged Adler-32 trailer (the decode answers 0 with the table complete) and a first opcode that can never terminate (the decode answers 0 having written nothing): the init answers a non-null archive, throws nothing, publishes 182,272, and the archive's header/record/name are the registered entry's byte for byte (the first case's archive is byte-identical to the intact run's) | `call sub_1004B570` at `0x1004B798` with **no test of EAX** |
| the entry | the fake-pak vector holds one entry: name `"finale2.sco"` (11 bytes, capacity 15), `file_size` `179536`, the three recomputed offsets 0, `data` = the table it was handed; the builder's clear empties the vector again | the push_back at `0x1004C287`-`0x1004C2A2`, read out of the module's own vector |
| the restore | after both damages a clean call reproduces the pinned archive | the damages were real: they are undone byte for byte and the protection is restored |

**The damaged resource.** The decode body names the resource as an immediate (`mov ecx,
OFFSET kOocResource`), and that body is a *proved transcription*, so a test cannot hand it
a buffer without ceasing to test the shipped code. Instead the test opens the array's
`.rdata` page with `VirtualProtect`, changes one byte, runs both the decode's own verdict and
the init inside that window, and restores the byte and the protection. Two damages, one per
failure class: the last byte of the resource (the Adler-32 trailer's low byte — the stream
still terminates, fills all 179,536 bytes, and only the checksum fails) and the first byte
(`0x05`, the no-progress opcode, so the terminator test reads `0x73` where it wants `0xFA`
and the decode gives up before writing a single byte — asserted by a `0xA5` fill that comes
back untouched). Neither is reachable with the shipped resource, which passes all three
checks; both are what the original's discarded answer makes possible.

**The test can fail.** Four mutations, each reverted:

- a one-shot guard that works (`static uint32_t guard; if (guard) return guard;`): 4
  failures, and they are the guard's class — `runs: two more calls allocated two more
  archives (1 recorded)`, `runs: every call answers a fresh archive
  (0x016435B0, 0x016435B0, 0x016435B0)`, `restore: the resource decodes again after the
  damages`, and `leak: six calls built six archives (1)`.
- a fail-closed decode (`if (OocDecodeBody(table) == 0) return 0;`): 3 failures — both
  `the init answers an archive (0x00000000)` lines, and `leak: six calls built six archives
  (4)`.
- the mount count published as the table's size instead of the cursor: 4 failures, all of
  them the publish clause — `run 1: dword_10159BFC ... is 179536 and the cursor is 182272`,
  both re-publish lines, and the two degraded-run count lines. The cursor checks still pass
  under it, which is the point: the cursor comes from the builder, the mount count from the
  init's own store.
- the entry name changed to `"finale3.sco"`: 5 failures — the archive hash, the name at
  `+0x24`, `the first archive is still the recovered one`, `entry: its name`, and the
  restore run.

**Gates.** `cmake --build --preset release` exit 0; `ctest --preset release` 19/19 twice
(the new test is `zp_ooc_init`, 54 checks, 0 failures); `python tools/parity_report.py 9` →
`build 9: 98 key(s) closed, 0 open, 1 oracle-verified (baseline incomplete)`;
`python tools/verify_bodies.py …` → `151 body(ies), 0 failure(s)`; `zp_dump --ooc-selftest`
→ `ooc selftest: PASS`; `python tools/check_fault_dialogs.py` → `0 failure(s)`.

**What stays unproven.**

- **The mount itself.** The wrapper formats `"memory:<archive>,<bytes>,0:"` and hands it to
  the game's device register; both are game-side calls that only run when the site at
  `0xB70733` fires (the key's cell and §2h cover the writes and the bodies).
- **The guard's taken branch.** Nothing can set `0x10159C00`, and the rebuild has no slot,
  so what the original does on that path — answer the guard word as the archive — is
  documented here and nowhere exercised. It cannot be: the branch is unreachable in both
  trees.
- **The entry's `std::string`.** The rebuild's element is a 44-byte struct with a fixed
  16-byte name buffer and the size/capacity pair of the original's inline string. The
  entry the original registers spells the name through `to_string`-shaped digit loops,
  `strtol` and a `char` cast; the rebuild stores the same eleven bytes, and nothing
  observable distinguishes a fixed buffer from an inline `std::string` at this capacity
  (the only entry ever registered is `"finale2.sco"`, and the destroy loop's free branch is
  unreachable — §2h).
- **Concurrency.** Single-threaded, like every other test here: the init is called once per
  wrapper call in one thread, and the fake-pak vector is not guarded in either tree.
- **The heap's own decision about the two leaked archives.** The test shows they are never
  freed and never returned to a later build on this heap; whether a different heap would
  place a third archive inside a leaked one is not a property of the module.

---

### 2l. This pass: the DFA close-handle body, identified and driven

`0x10004790` was the last DFA body without a behavioural test and the last piece whose
description in this file was wrong. Both are fixed: the body is now **byte-identical to
the oracle except its four relocated operands**, its frame's filter is identified from
the scope table, and `tests/dfa_close.cpp` (`zp_dfa_close`, 27 checks) drives it against
a real file and, in a child process, against a faulting callee.

**The body, quoted.** 45 instructions in `0x81` bytes (`0x10004790`..`0x10004810`):

```
10004790  55                    push ebp
10004791  8b ec                 mov  ebp,esp
10004793  6a fe                 push 0xFFFFFFFE          ; try level -2
10004795  68 c0 3f 15 10        push 0x10153FC0          ; the scope table
1000479A  68 4c fe 05 10        push 0x1005FE4C          ; __except_handler4
1000479F  64 a1 00 00 00 00     mov  eax,fs:[0]          ; the thread's chain
100047A5  50                    push eax
100047A6  83 ec 08              sub  esp,8
100047A9  53 56 57              push ebx/esi/edi
100047AC  a1 0c 80 15 10        mov  eax,[0x1015800C]    ; __security_cookie
100047B1  31 45 f8              xor  [ebp-8],eax
100047B4  33 c5                 xor  eax,ebp
100047B6  50                    push eax                 ; the frame cookie
100047B7  8d 45 f0              lea  eax,[ebp-0x10]
100047BA  64 a3 00 00 00 00     mov  fs:[0],eax
100047C0  89 65 e8              mov  [ebp-0x18],esp
100047C3  c7 45 fc 00 00 00 00  mov  [ebp-4],0           ; state 0: the guarded region
100047CA  ff 75 08              push DWORD PTR [ebp+8]   ; the handle, unexamined
100047CD  ff 15 58 30 06 10     call DWORD PTR ds:[0x10063058]   ; KERNEL32!CloseHandle
100047D3  c7 45 fc fe ff ff ff  mov  [ebp-4],-2          ; state -2: the region is over
100047DA  8b 4d f0              mov  ecx,[ebp-0x10]
100047DD  64 89 0d 00 00 00 00  mov  fs:[0],ecx
100047E4  59                    pop  ecx
100047E5  5f 5e 5b              pop  edi/esi/ebx
100047E8  8b e5                 mov  esp,ebp
100047EA  5d                    pop  ebp
100047EB  c3                    ret
100047EC  83 c8 ff              or   eax,0xFFFFFFFF       ; the FILTER
100047EF  c3                    ret
100047F0  8b 65 e8              mov  esp,[ebp-0x18]       ; the HANDLER
100047F3  c7 45 fc fe ff ff ff  mov  [ebp-4],-2
100047FA  b8 01 00 00 00        mov  eax,1
100047FF  8b 4d f0              mov  ecx,[ebp-0x10]
10004802  64 89 0d 00 00 00 00  mov  fs:[0],ecx
10004809  59 5f 5e 5b           pop  ecx/edi/esi/ebx
1000480D  8b e5                 mov  esp,ebp
1000480F  5d                    pop  ebp
10004810  c3                    ret
```

`0x10063058` is `KERNEL32.dll!CloseHandle` (`../ZolikaPatch_reconstruction/evidence/import_table.json`),
so the guarded region is exactly one push and one call, and the normal path hands back
whatever `CloseHandle` left in `EAX`.

**Which funclet is which.** The scope table at `0x10153FC0` is

```
10153FC0  fe ff ff ff 00 00 00 00   GSCookieOffset -2,  GSCookieXOROffset 0
10153FD0  d8 ff ff ff 00 00 00 00   EHCookieOffset -0x28, EHCookieXOROffset 0
10153FE0  fe ff ff ff ec 47 00 10   { EnclosingLevel -2, FilterFunc 0x100047EC
10153FF0  f0 47 00 10               HandlerFunc 0x100047F0 }
```

so `0x100047EC` — `83 C8 FF; C3` — is the **filter**, and it answers `-1`,
`EXCEPTION_CONTINUE_EXECUTION`. `0x100047F0` is the **except body**, i.e. the
`return TRUE` arm; it is entered only when a filter answers `EXCEPTION_EXECUTE_HANDLER`
(1), which this one never does. The record order is not a guess: the native guard's table
(`0x10153EE8`, §3 class B) has the same layout with the opposite contents — filter
`0x1000395A` = `mov eax,1; ret` and handler `0x10003960` = `xor al,al` — which is what
`__except (EXCEPTION_EXECUTE_HANDLER) { return false; }` compiles to.

**Which C shape that is, checked in bytes.** `.scratch/dfa_close/probe.cpp` compiles four
candidate shapes at `/O2 /MT /GS` (the module's own flags); `.scratch/dfa_close/coff.py`
compares each function's bytes with the oracle's range through the COFF symbol and
relocation tables. Exactly one candidate reproduces the oracle:

```
oracle 0x10004790: 129 byte(s)

?VariantReturnExceptContinue1@@YAHPAX@Z: 129 byte(s), 16 differing byte(s), 4 relocation(s)
  +0x04 oracle fe 68 c0 3f candidate fe 68 00 00
  +0x08 oracle 15 10 68 4c candidate 00 00 68 00
  +0x0C oracle fe 05 10 64 candidate 00 00 00 64
  +0x1C oracle a1 0c 80 15 candidate a1 00 00 00
  +0x20 oracle 10 31 45 f8 candidate 00 31 45 f8
  +0x3C oracle 08 ff 15 58 candidate 08 ff 15 00
  +0x40 oracle 30 06 10 c7 candidate 00 00 00 c7
  relocations at +0x06, +0x0B, +0x1D, +0x3F
  -> the oracle's exact stream; every difference is a relocation
```

i.e. `__try { return CloseHandle(h); } __except (EXCEPTION_CONTINUE_EXECUTION) { return TRUE; }`.
The other three shapes differ **outside** the relocations, which is what makes the
comparison decide rather than suggest: the shape this tree shipped until this pass
(`__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }`) prints
`129 byte(s), 34 differing byte(s), 4 relocation(s)` and
`NOT the oracle's stream (18 byte(s) outside every relocation: +0x5C … +0x6E)` — the two
funclets swapped, a different program. **The rebuild now uses the oracle's shape**, and
`.scratch/dfa_close/module_body.py` repeats the comparison on the module's own object
(`launcher.obj`) rather than on a probe, with the object's four relocation entries read
from its COFF section table:

```
?CloseHandleBody@?A0x5b8f3f28@@YAHPAX@Z
  .text$mn: 129 byte(s), 4 relocation(s) inside the body
  oracle 0x10004790: 129 byte(s)
  16 differing byte(s)
    ... all seven words inside the relocations ...
  relocated operands at +0x06, +0x0B, +0x1D, +0x3F
  45 instruction(s): 45 oracle, 45 shipped
  -> 16 differing byte(s), all relocated; IDENTICAL except the relocated operands
  shipped +0x5C (filter): 83 c8 ff c3 8b 65 e8 c7
  oracle  +0x5C (filter): 83 c8 ff c3 8b 65 e8 c7
```

The four relocated operands are the two pushes (scope table, `__except_handler4`), the
cookie load and the `CloseHandle` slot — every operand the linker owns. The instruction
stream, the branch shape, both funclets and the byte size are the original's.

**What the disposition does, measured.** A filter that answers `-1` resumes the faulting
instruction, so a fault inside the guarded call is **retried, not answered**. The test's
child replaces the module's `CloseHandle` import slot — the same kind of slot the
original's body reads at `0x10063058`, found through the test exe's own PE import
directory, so no instruction is decoded — with a function that writes one byte to a probe
file (opened *before* the patch, so the write uses no `CloseHandle`) and then writes to the
unmapped null page; the child writes its `STARTED` line before the patch, then calls the
body. The parent waits 1000 ms and requires the child to be *still running*, the probe byte
to be there (so the guarded call was really entered, not merely slow) and no `ANSWERED`
line, then terminates it and reaps it:

```
PASS child: the faulting callee was entered through the patched slot (the probe file holds
     "X"), so the guarded call is where the child is stuck
PASS child: the close body was reached after the slot was patched (report "STARTED")
PASS child: the guarded call did not answer within 1000 ms -- the filter's
     EXCEPTION_CONTINUE_EXECUTION (0x100047EC) makes the OS retry the faulting call, so the
     frame has no answer for it (exit 0x0000DEAD; 0xC0000005 would be the fault with no
     frame at all)
PASS child: no ANSWERED line -- the handler at 0x100047F0 (`return TRUE`) is unreachable, so
     the rebuild does not answer where the original retries (report "STARTED")
PASS child: terminated and reaped by the test (exit 0x0000DEAD), so the suite leaves no
     process behind
```

The child writes its `STARTED` line *before* the slot is patched, because the report
stream's own close is a `CloseHandle` call through the import being replaced — writing it
after the patch faults outside the frame under test (that mistake produced
`exit 0xC0000005` on the first run of this test, and is the reason `zp_fault_dialogs`
requires `SetErrorMode` in such a test's `main`).

**What the frame protects, and what is reachable.** The guarded region covers the
`call ds:[CloseHandle]` and nothing else, and no handle value can make `CloseHandle` fault
— it validates the value and returns `FALSE`. So the fault path is **unreachable through
the wrapper's own arguments**, and the reachable half is what the test asserts in this
process:

| piece | assertion | clause pinned |
|---|---|---|
| the round trip | the fixture is opened by `0x10004820`, sized (`TRUE`, 32), seeked (`16`, then `28` from the end with a negative distance), read (`"01234567"`, then `"ghijklmn"`, then `"stuv"`), then closed by the rebuild (`1`) | the four transcribed bodies' argument orders and `0x10004790`'s normal path returning `CloseHandle`'s `EAX` |
| really closed | `DuplicateHandle` on the value answers `FALSE`/`6`; the transcribed reader answers `FALSE`/`6` and `ReadFile` itself answers `FALSE`/`6` for the same value | the close happened in the OS, not just in a return value, and the reader is a forwarder |
| no argument guard | `close(nullptr)` answers `FALSE`/`6`; `close(closed value)` `FALSE`/`6`; `close(0xDEADBEEF)` `FALSE`/`6`; after all three a fresh create/close works | the body's only instructions between the frame and the call are `mov [ebp-4],0` and `push [ebp+8]` (`0x100047C3`, `0x100047CA`): the value reaches `CloseHandle` unchanged. The deciding instruction for a double close is the unconditional `call ds:[0x10063058]` at `0x100047CD`; nothing in the body remembers the first close |
| pass-through | wrapper and `CloseHandle` answer the same value and the same error for two identical handles; a successful close leaves last error untouched (`0xFFFFFFFF` before and after) | the frame adds nothing on the reachable path |
| the frame's registration | `fs:[0]` is the same before the call and after a successful close, and after the null close | the prologue/epilogue pair (`0x100047BA` installs, `0x100047DD` restores); a skipped restore would leave the chain pointing into a dead frame |

Two values pin themselves. `ERROR_INVALID_HANDLE` is 6, asserted as the literal the
checks name. And the never-opened value is `0xDEADBEEF`: both low flag bits are set, so
the kernel rejects it without a table lookup, and the `FALSE` answer proves it was not a
live handle (a closable one answers nonzero). `INVALID_HANDLE_VALUE` is deliberately **not**
used: on Win32 it is `(HANDLE)-1`, the current-process pseudo-handle, and this machine
answers `1` for `CloseHandle(INVALID_HANDLE_VALUE)` — the name is a trap, not an invalid
handle (`.scratch/dfa_close/values.cpp` is the probe that measured the whole answer
matrix: valid `1` with last error untouched, closed `0`/`6`, `NULL` `0`/`6`,
`0xDEADBEEF` `0`/`6`, `0x1234` `0`/`6`, `1` `0`/`6`).

**The seam.** `source/launcher.ixx` grew one exported, test-only namespace, `Launcher`,
with a single entry:

| entry | is |
|---|---|
| `DfaBodiesForTest` | the five body addresses — the four transcribed forwarders (`0x10004820`, `0x10004740`, `0x10004760`, `0x10004720`) and the rebuilt close (`0x10004790`) — typed with the originals' `__cdecl` ABIs |

It is not a second implementation: the entries are the bodies' own addresses. It calls
`PublishDfaForwarders()`, the four stores `RegisterLauncher` makes (the create body's
fallback would otherwise `call` through zero), which is now one function so the shipped
publish step and the seam cannot drift. That is the only change to shipped code besides
the except clause: `.scratch/dfa_close/publish_check.py` disassembles the built
`RegisterLauncher` and finds all four slot stores still there
(`10022841`, `1002284B`, `10022855`, `1002285F`; `4 of 4 slot(s) written`). **Nothing
reaches the shipped image**: the seam has no caller inside the ASI and no PE export, so
`/OPT:REF` drops it — `ZolikaPatch.map` has 0 occurrences of `DfaBodiesForTest`, of the
`DfaBodies` type and of the helper — while the test links the same object file and calls
them. The write set and the 151 proof rows are unchanged.

**The test can fail.** Two mutations, each reverted:

- **the wrapper does not close** (`CloseHandleBody` becomes `return TRUE;`): 12 failures,
  and they are the expected classes — `the handle is really closed: DuplicateHandle refuses
  it…`, `read 0x10004740 after the close…`, `null: …`, `closing the already-closed value
  again…`, `a handle that was never opened…`, the frame's failure-path check, and the whole
  child class (the probe file is empty, the report says `ANSWERED=00000001`, so the retry
  check's exit code is `0`).
- **the divergent guard** (`__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }`, what the
  tree shipped until this pass): 5 failures, all in the child class, with
  `report "ANSWERED=FFFFFFFF"` — the reachable-surface checks still pass, which is exactly
  the point: the divergence lived only on the unreachable path, and only the child sees it.
  The probe byte is present there, which is the right answer: that guard *does* reach the
  fault, it just answers it.

**Gates.** `cmake --build --preset release` exit 0; `ctest --preset release` 20/20 twice
(the new test is `zp_dfa_close`, 27 checks, 0 failures, 1.3 s including the watchdog);
`python tools/parity_report.py 9` → `build 9: 98 key(s) closed, 0 open, 1 oracle-verified
(baseline incomplete)`; `python tools/verify_bodies.py …` → `151 body(ies), 0 failure(s)`;
`zp_dump --ooc-selftest` → `ooc selftest: PASS`; `python tools/check_fault_dialogs.py` →
`12 test source(s), 0 failure(s)`.

**What stays unproven.**

- **The in-game call.** The body runs when the game closes a file through its DFA layer,
  and nothing offline can make it. The test drives the body directly, so what play still
  decides is that the five install rows point at it and that a DFA close in the game takes
  this path at all (the launcher cell is oracle-verified, and the install rows are the
  bytes `parity.md` counts).
- **A real fault inside `CloseHandle`.** There is none to have: `CloseHandle` validates
  and returns. The child's fault is forced through the import slot, which is the only
  mechanism the original's own body offers too (its `ds:[0x10063058]` is a data slot).
- **The 1000 ms window.** The test proves the child had not answered after one second and
  was still in the retry loop; it does not prove the loop is infinite, only that the shape
  the oracle compiled is the shape the rebuild compiles. The scratch probe measured the
  same shape hanging for three seconds.
- **The CRT that interprets the filter.** The original imports `_except_handler4_common`
  (`0x100632B4`) from its CRT; this tree's static CRT provides its own. Both are MSVC's
  handler4 and `-1` is the documented `EXCEPTION_CONTINUE_EXECUTION` disposition, but the
  two implementations are not byte-compared — the *shape* of the frame is, and the
  disposition is measured.

---

## 3. Every piece that is still rebuilt

`insns` is the linear sweep over the function's own bytes, i.e. what a proof row would
have to cover. IDA's per-function total is sometimes larger because it also counts the
out-of-line EH funclet / thread-abort blocks that belong to the function but do not sit
in its byte range; where the two differ, the total is given in the row's text. `bytes`
is IDA's function size.

### Class A — indexed absolute table operand

**Empty.** Every piece that was filed under this class is transcribed and proven:
the model-name lookup (`0x10003F00`, §2b), the steer consumer (`0x10004C70`, §2c),
both siren checkers (`0x10005F70`/`0x10006260`, §2d), the IDE parser payload
(`0x10003FA0`, §2e), the popup-headlights checker (`0x10044310`, §2f) and the
headlight-cone check (`0x1001AD20`, §2g). Each reads a registered table and the
comparator resolves the operand — by content for the table, and by offset for the
seven column displacements the popup checker and the cone check read inside their
arrays. The cone check's one-word-past read is reproduced as the same slot the
original's `.data` holds, not bounded (§2g).

No piece is filed here for the old reasons. What remains in the class is only the
*general* relaxation the comparator still refuses to make (§4 item 3): an
unregistered indexed absolute keeps the literal comparison.

### Class B — exception frame

| piece | VA | bytes | insns | the frame that blocks it | what the rebuild reproduces | how that was verified | residual risk |
|---|---|---:|---:|---|---|---|---|
| guarded resolve-and-invoke | `0x100038E0` | `0x9E` | 59 | `__try/__except`: `push -1; push 0x10153EE8; push __except_handler4; … mov fs:[0],eax`, two funclets (`mov eax,1; ret` and the unwind path) | resolve a native handler by hash, call it with the one argument, answer `true`; a fault in the call is caught and answered `false` — that is the feature | `zp_native_guard` (ctest) runs it against a resolver the test installs: a target that returns 7 answers `1` and receives the block, a target that reads null or writes an unmapped address answers `0` with the process still usable, a *resolver* that faults answers `0` (the region covers `0x1000392B`), and a null handler skips the call and answers `1`. The bodies that call it are transcribed (`audio_call_*`, `bad_player_model_*`), and its consumers' cells are closed | high, by impact; the frame's four outcomes are now measured, so what stays unproven is that a real game fault always raises inside this region, and that the in-game consumer is the one the test models (a stand-in caller does the read) |
| guarded resolve-only | `0x10003980` | `0x90` | 52 | same, scope table `0x10153F08` | resolve only, answer the handler pointer or 0 | `zp_native_guard` as above, plus this entry's own shape: it answers the handler address without calling it (`0x100039D0`..`0x100039EB`), a faulting resolver answers `0`, a null handler is answered as null (this entry has no `test`), and a child process shows a caller's `[eax+0Eh]` read through that null dying with `0xC0000005`. Called from the transcribed steer consumer, the popup shims and `MouseFix`'s helpers | high, same reason: the caller's unguarded read is the original's fault, and the test models that caller rather than running it |
| DFA close-handle body | `0x10004790` | `0x81` | 45 | `__try/__except` around `CloseHandle`: `push -2; push 0x10153FC0; push __except_handler4 (0x1005FE4C); … mov fs:[0], eax`, with the filter (`or eax, -1; ret` at `0x100047EC`) and the unwind continuation (`0x100047F0`) inside the same byte range | `CloseHandle` behind the guard, with the filter's own disposition: `0x100047EC` answers `-1` (`EXCEPTION_CONTINUE_EXECUTION`), so a fault in the guarded call is **retried, not answered**, and the `return TRUE` handler at `0x100047F0` is unreachable; the normal path hands back `CloseHandle`'s `EAX`. The rebuild is byte-identical to the oracle except the four relocated operands (§2l) | `.scratch/dfa_close/module_body.py` compares the module's own object with the oracle: `129 byte(s)`, `45 instruction(s)`, `16 differing byte(s), all relocated` (`+0x06`, `+0x0B`, `+0x1D`, `+0x3F`), funclet bytes identical at `+0x5C`/`+0x60`; `zp_dfa_close` (§2l) drives it — the round trip on a real file through the four transcribed siblings, the closed/null/double/never-opened answers, the pass-through equality, `fs:[0]`, and a child whose faulting callee must never return | low: the frame's retry is measured in a child (1 s watchdog, the same shape measured hanging for 3 s in the probe); the guarded region is only `CloseHandle`, which no handle makes fault. What remains is the in-game call (§2l) |
| SpeedupBugFix body | `0x10050540` | `0x16F` | 93 (+funclet at `0x10061981`) | C++ static-init EH: `push -1; push SEH_10050540; … fs:[0]`, funclet `jmp __CxxFrameHandler3` with throw info `0x10154358`, plus the `_Init_thread_header/footer` TLS guard (`0x1005FFC3`/`0x1005FF77`) around the one-time QPC/scan | the clock fix: QPC frequency → one-over-frequency into eight published float slots, then tail-call `sub_10003830`'s resolver result − 0x33 (the game's timer getter) and return its `(eax,edx)` pair in a 64-bit answer | the arm's eight operands are validated per site and the per-key dump is byte-identical to the pre-change dump on all nine images; the *body* never runs offline | moderate: the eight timer slots are the whole fix. A wrong slot or a float/double mix-up would show as a broken game clock, not a crash |
| audio install body | `0x10050AD0` | `0xF9` | 75 | EH4 (`push __except_handler4`, scope `0x10154384`, funclet `0x100619C1`) + the CRT clock/seed block through `fs:[2Ch]` and `TlsIndex` | resolve the `6A 06 68 ? ? ? ? 8D 8C` site once and write `rand() % 5 + 1` into its `push` immediate, gated on the transcribed `0x10050A50` gate | `RestoreDeathMusic`'s cell is closed; the gate and the three native wrappers around it are transcribed and proven | **the one recorded 1:1 divergence in that key**: the original's guard compares `dword_101A84C0` with the CRT per-thread word and may skip the whole block on a fresh process; the rebuild always writes. Recorded in `parity.md` §RestoreDeathMusic |
| save converter | `0x10051B30` | `0x489` | 337 | EH registration (`fs:[0]`) + three `_Init_thread_header/footer` TLS guards + CRT file API (`fopen_s`, `fread`, `fclose`, `operator new`, `free`) + `std::string` (IDA's per-function total is 363: 337 plus the out-of-line funclet/abort blocks) | open the save, check `SAVE` at +0xC, walk eighteen chunks with `fread(9)` / `operator new(size-9)` / `fread(payload)` / `free`, remember chunks 9/13/16 (`0x4C45`/`0x1741`/`0x0E77`), then patch the three sites (`0x872185`, `0x7D8C50`, `0x8DA090`) | the walk, the three flag tests and the three patch values are pinned by `zp_save_walk` on both real saves and on three synthetic layouts, against the file re-read by the test's own parser (§2i); its two faults are asserted too — `std::bad_alloc` in-process, the uninitialised magic in a child process. What the test cannot run is anything but the decision: the three writes and the two hooks still need a save load | **high**: a corrupt save already faults (preserved), and the chunk-size conditions decide whether the new-format hooks are installed. A wrong condition converts a save wrongly instead of failing |
| save prepare | `0x10051FD0` | `0x202` | 157 | same EH/TLS + `std::string` construction + the game's save-directory call | ask the game for the save directory, build `"<dir>\\<save name>"` as a `std::string`, call the converter | the directory call site is one of the key's six recovered scans; the *string* shape is not measured offline (§2i ends on it: `zp_save_walk` takes a path, so the prepare step is not in its reach) | moderate: a wrong path means the converter opens nothing (silent no-convert) or the wrong file |
| OOC init (decode → register → build) | `0x1004B750` | `0xBFF` | 991 | EH (`fs:[0]`) + C++ container helpers (`0x10058620`, `0x1005CF20`) + `operator new` | decode the pinned resource, register it as the single fake-pak entry in the module's own table (`0x1016E43C`), then call the transcribed builder (§2h) for the 182,272-byte archive and read its write cursor back as the byte count | **proven end to end offline**: `zp_dump --ooc-selftest` checks the stream's Adler-32 trailer, the decoded table's `fnv1a 0xC883859F`, the archive's 182,272 bytes and `fnv1a 0xB5AF7B10`, and the header's `16`/`0xE9` words → PASS; **and its own semantics are a test**: `zp_ooc_init` (§2k) drives the init itself — three calls build three archives and free none (the guard has no writer), the cursor and the published mount count are the archive's 182,272 bytes, a damaged trailer and a dead first opcode still register and build, and the fake-pak entry is `"finale2.sco"` with 179,536 bytes of table | low: the transform is fingerprinted and the init's four semantics are asserted; what remains is the in-game mount (§2k) |
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
| NewFiles updates (2) | `0x10051890`, `0x100518D0` | `0x3F`, `0x45` | 30, 30 | the three class-C scans above + the transcribed resolver `0x10003E50` | their own stream is transcribable, but three of four callees are class C and two of the four need `__fastcall` entry shims (`ECX` = object/list, stack = hash/name) whose register contract would itself be unproven — that is the register-contract bug class, not a proof. §4 item 1 | moderate: it decides whether a new-format file is registered at all |
| NMS arena methods | `0x10038360` (alloc), `0x10038430` (free), `0x100385A0` (contains) | `0xC2`, `0x10E`, `0x7D` | 69, 91, 40 | the original's `std::_Hash` pair `0x1005C610` (emplace) / `0x1005C5A0` (find), `memset`, `operator delete`, and the map's own fields | the three streams are plain — no EH frame, no TLS guard — but their callees are not the builder's kind of helper. `0x1005C610` **has an exception frame** (`push -1; push SEH_1005C610; … mov fs:[0], eax` at `0x1005C613`-`0x1005C632`, the frame's funclet at `SEH_1005C610`), so it is class B and stays rebuilt. `0x1005C5A0` is the `std::_Hash::find` over four map-internal globals (`dword_101A8424`, `dword_101A8418`, `dword_101A8410`), and the free body **inlines** the erase unlink over the same globals (`mov ecx, dword_101A8424` at `0x100384CC`, `mov eax, dword_101A8418` at `0x100384D2`, `and ecx, esi / lea ecx, [eax+ecx*8]` after it), so its own stream reads container internals a module's `std::unordered_set` does not expose. Transcribing the three callers would prove the arithmetic and leave the registry a rebuild — the class-D position this pass removed for the builder, without a way to remove it here | low: the vtable's other eleven methods are transcribed, and these three are now pinned by behaviour instead — `zp_nms_arena` (§2j) asserts the alignment scheme, the distance word, the `0xCD` fill over exactly `want`, membership, free and the null-returning failure path, 246 assertions. What remains unmeasured is only what no caller can observe (the container behind the registry) and what the heap decides (a double free) — §2j |

The NMS methods are the class's *honest* remainder: their own streams are exactly as
transcribable as the builder's, but the registry they call is a `std::_Hash` the
original inlines into them, not a set of private helpers. A transcription of
`0x10038360` would have to call this module's `std::unordered_set::insert` through an
entry shim, and a transcription of `0x10038430` would have to read our set's internals
at the original's offsets — an unchecked layout assumption, not a proof. The rebuild's
reproduction (the alignment scheme, the `[pointer-4]` distance, the `0xCD` fill, the
lock) is unchanged, and it is now measured rather than argued: `tests/nms_arena.cpp`
(ctest `zp_nms_arena`, §2j) drives the three bodies through `MemoryArena::Get` and pins
each clause of the scheme, so what the transcription would add is the call sequence into
the original's own container — and only membership through that container is observable,
which the rebuild already reproduces.

---

## 4. Follow-ups, in the order they are worth doing

1. **NewFiles updates** (`0x10051890`/`0x100518D0`, 30 each). Needs two `__fastcall`
   entry shims in front of `NewfilesCallObject`/`NewfilesCallAdd`; once those exist the
   two bodies are a straight transcription. Their call graph stays class C, so the doc
   entry above changes from "blocked" to "proven caller, rebuilt callees".
2. **NMS arena methods** (`0x10038360`/`0x10038430`/`0x100385A0`). Not transcribable
   without the container: `0x1005C610` carries an EH frame (class B), and `0x10038430`
   inlines the `std::_Hash` erase unlink over the original's map globals. A future pass
   could transcribe `0x10038360`/`0x100385A0` against a shim and leave `0x10038430` a
   rebuild, but that proves the alignment arithmetic only — which `zp_nms_arena` (§2j)
   now proves behaviourally, together with the distance word, the fill, the free path and
   the null-returning failure — and the free path would then be the one method whose
   stream is not the original's. The remaining gain is the registry's own container, and
   nothing outside the three methods observes it.
3. **The comparator, and how far to extend it.** The global relaxation is still
   **not** made, deliberately: treating every indexed absolute displacement as a slot
   would stop the proof distinguishing `[esi*4+0x1015A538]` from `[esi*4+0x10159D38]`,
   i.e. a transcription could read the wrong table and still pass. What replaced it is
   the per-table opt-in §2b describes: `Bodies::RegisterTable` plus a `table` row in
   `tests/bodies.tsv`. A table that is not named still compares literally, and two
   registered tables with different contents fail as `table contents differ`. Four
   properties the class-A tables needed are now in place:

   - a table's bytes are read as the *loaded* image holds them: a section's
     uninitialised tail (where the original keeps every 512-slot model table) reads as
     zeros (§2c);
   - a row may name a C symbol (`_name`) as well as a C++ one (§2c);
   - the registry resolves the **base-plus-displacement** shape as well as the indexed
     absolute: `push ds:[edi+0x10159380]` becomes `[edi+table:<hash>]` with the base
     register still compared, and a base that changed is reported as an *instruction*
     difference, not a table one. `table_read` is the single place that decides which
     shapes resolve; a base with an index stays literal;
   - a displacement **inside** a registered table resolves to that table plus its
     offset, and the offset is compared: `[eax+0x1015C62C]` becomes
     `[eax+table:<hash>+4]` (§2f). That is what the popup checker's eight interleaved
     columns and the cone check's base-plus-four read need, and a wrong column is an
     instruction difference. A 32-bit module cannot make the linker emit a symbol at an
     array offset, which is why the registry — not the manifest — carries the offset.

   The self-test covers all four (its verdict is in §6). Class A is now empty: every
   piece filed there is transcribed (§2b–§2g).
4. **The internal-branch-target check** (the previous revision's item 1) is **done**:
   it is `tools/verify_bodies.py::internal_branch_mismatch`, it runs on every row, and
   it is what §2a's six transcriptions were proved with. `.scratch/branch_targets.py`
   stays as the independent reference implementation (it skips the `table` rows); the
   two agree at `151 row(s), 321 internal branch target(s), 0 failure(s)`.

---

## 5. Check these first in play

Ordered by *how much breaks if the piece is subtly wrong*, and within that by *how
loosely the surrounding proofs pin it*. The first three are the ones to exercise
deliberately after any change to the modules above.

1. **`0x10051B30`/`0x10051FD0`, the save converter and prepare** (class B, high
   impact; the converter's *decision* is now behaviourally proven, §2i, its writes are
   not). Exercise: load a save, save a game, then load the save just written, in a new
   session. What to look for: the save loads at all (the three site patches and the two
   hooks, which `zp_save_walk` cannot reach), the HUD/clock restored, no "corrupt save"
   dialog, and the file mtime/size changing exactly once. This is the only piece whose
   failure destroys user data rather than a session. A newer-layout save (a FusionFix-era
   savegame with chunk 9 `0x4C45`, 16 `0x0E77`) is the case the synthetic images cover
   offline and only play can confirm end to end.
2. **`0x100038E0`/`0x10003980`, the guarded native calls** (class B, high impact,
   fault-parity is the feature). Offline, `zp_native_guard` now pins the four outcomes
   (§3 class B): the invoke's `1` and the resolve's handler address, a fault swallowed
   inside the region (handler or resolver call) with the process still usable, the
   invoke's null-handler skip (`mov al,1`), and the null resolver's `0` that the
   *caller* faults on, in a child process. What play still decides is which real game
   faults land inside the region and whether the in-game caller is the one modelled
   here. Exercise: any path that calls a native with a slot that has not resolved — the
   audio wrappers, `DrivebyFOVFix`, `BadPlayerModelCrashFix` on a non-ped model. What to
   look for: a clean no-op (original: answered 0), not a crash; and the reverse — a
   genuine game fault must still crash in the same place. With GTAC.dll loaded, the same
   path prints the diagnostic, whose model name now comes from the transcribed
   `0x10003F00` walking the registered table (§2b): a wrong name in that text is the
   visible symptom of a bad table, and the proof already compares the table's contents.
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
6. **`0x10001280` + the two forwarders, then the OOC archive builder** (all now
   transcribed, §2h for the builder). Exercise: the livery list on a car with
   `NoLiveryLimit`, then the OOC fix (a save past the last mission), then a DLC start.
   What to look for: correct `"%s_sign_%d"` livery names, a correct `memory:$…` mount
   string (no truncated path), the episode ending mounted from the archive (the
   archive itself is byte-pinned; the mount string and the device register are what
   play tests), and correct `\Episode<n>\` probes.
7. **`0x10003FA0`/`0x10044310`/`0x1001AD20`, the misc and vehicle checkers** (class A,
   now transcribed: §2e, §2f and §2g; the two siren checks `0x10005F70`/`0x10006260`
   are §2d). Exercise: `CopSirensFlag` (police cars get their flags; the transcribed
   pair decides `police`, `police2`, … and the `+siren` list), `PopupHeadlightsFlag`
   (pop-up lights on the configured model), `ForceCarHeadlightShadows`. What to look
   for: the feature silently not applying to one model, which for the siren checks
   would mean a wrong name or index slot — the operand maps (§2d item 3, §2e item 3,
   §2f item 3, §2g item 3) declare every one of them. The one thing no offline check
   can measure is the cone check's *runtime* neighbour word (§2g): enable
   `ForceCarHeadlightShadows` together with `CarDynamicShadowFix` and confirm the
   shadow offsets still look right; a fully-filled 512-row `[HeadlightConeOffsets]`
   section is the only shape that reads it.
8. **The ROM service and the DFA close-handle body** (classes B/C). Exercise: launch
   through `PlayGTAIV.exe` and watch for the ROM message window to exist (no visible
   symptom); a hang here is the preserved original fault. Then open a save so the DFA
   layer closes a file. Least likely to matter, hardest to see. The five DFA bodies are
   now behaviourally tested offline (`zp_dfa_close`, §2l: a real file through the four
   forwarders and the rebuilt close, plus the child that pins the frame's retry), so what
   play adds is only that the game's five patched call sites reach them — the install rows
   are the bytes `parity.md` counts, and a wrong argument order would fail the open, not
   crash.
9. **The arena** (class D). Exercise: any long session with `NewMemorySystem=1` — loading,
   a mission, a save. What to look for: an allocation fault or a heap-corruption crash,
   which the game shows immediately. `zp_nms_arena` (§2j) now pins the scheme, the fill
   and the free path offline, so what play can add is the two things the test cannot
   reach: the lock under real contention, and a double free, which the original does not
   guard and the rebuild does not guard either (`parity.md`, preserved faults). A crash
   whose stack is inside `0x10038360`/`0x10038430`/`0x100385A0` would mean the rebuild's
   arithmetic or its registry diverges from the original's, and §2j names the assertion
   that would have caught it.

---

## 6. Where the numbers live

- `tests/bodies.tsv` — the 151 proof rows; a row's count is the body's exact
  instruction count, so it cannot silently shrink. Its `table` rows are the offline
  projection of the arms' `Bodies::RegisterTable` calls (recovered VA, candidate
  symbol, registered length), one per table a transcribed body reads; a symbol may be
  C++ (`?name@@…`) or C (`_name`), a table may live in a section's uninitialised tail
  (its bytes read as zeros, what the loader maps there), and a registered range may
  extend past the array's last slot to cover the word the body reads there (§2g).
- `tests/save_walk.cpp` — the rebuilt converter's behavioural test (§2i), ctest entry
  `zp_save_walk`: the walk's chunk table, the three flags and the three patch values
  against the file re-read by the test's own parser; three synthetic layouts; and the
  two faults (typed `std::bad_alloc` in-process, the uninitialised magic in a child
  process). It reads saves from `../game_refs/saves` and writes its synthetic inputs
  under `<build>/save_walk_inputs`.
- `tests/native_guard.cpp` — the two guarded entries' behavioural test (§3 class B),
  ctest entry `zp_native_guard` (27 checks, no game image): the invoke's `1` and the
  resolve's handler address (and that the resolve did not call it), a fault swallowed
  inside the region for both the handler call and the resolver call with the guard
  still answering the normal path afterwards, the invoke's null-handler skip versus the
  resolve's null answer, and the null resolver whose `0` the caller's `[eax+0Eh]` read
  faults on — that last one as a child process (`--child <report>`) that must exit
  `0xC0000005` with the report saying `RESOLVED=0`, so "the guard answered 0" and "the
  caller faulted" are distinguished. `Native::SetResolverForTest` is the test-only
  single store that installs the resolver; the fake resolver is naked because the layer
  passes the hash in ESI.
- `tests/dfa_close.cpp` — the DFA close-handle body's behavioural test (§2l, §3 class B),
  ctest entry `zp_dfa_close` (27 checks, no game image): a real 32-byte file opened through
  the transcribed create body, sized, seeked (forward and from the end) and read through
  the transcribed ones, then closed through the rebuilt body; the closed handle's answer
  from `DuplicateHandle` and from the transcribed reader (`FALSE`, `ERROR_INVALID_HANDLE`
  6) and its equality with `ReadFile`'s own answer; null, double close and `0xDEADBEEF`
  (all `FALSE`/`6`, because the body has no argument guard); the pass-through equality
  with `CloseHandle`; `fs:[0]` unchanged across the call; and a child process
  (`--child <report> <probe>`) whose `CloseHandle` import slot is replaced with a faulting
  function that writes one probe byte on entry — the child must be **still running** at the
  1000 ms watchdog, the probe byte must be there (the guarded call was entered) and no
  answer may have been written; the parent then terminates and reaps it, and asserts the
  exit code is its own. It writes one fixture file, one report and one probe file, all in
  the binary directory, and removes them.
- `tests/nms_arena.cpp` — the three rebuilt arena methods' behavioural test (§2j, §3
  class D), ctest entry `zp_nms_arena` (246 checks, no game image): 42 `(size, align)`
  pairs asserted for the `unit` rounding, the distance range, the `want + 2*unit` block,
  usability and pairwise disjointness; the three limit cases (`align 3`, `align 0x30`,
  the ignored flags); the distance word; the `0xCD` fill over exactly `want`, including a
  poisoned, freed and re-taken block whose slack must stay untouched; membership before,
  after and around a free; the null free and the block reused by the next same-size
  request; and the failed request that answers null and leaves the arena usable. It
  writes no files. `MemoryArena::Get` in `source/loading.ixx` is the test-only seam that
  names the same three functions the vtable publishes and prepares the registry lock.
- `tools/verify_bodies.py` — the comparator, including the internal-branch-target
  check and the table registry (`Tables`, `table_content_hash`, `table_mismatch`,
  `table_read`, `read_table`, `read_map`); `table_read` is the one place that decides
  which operand shapes resolve (an indexed absolute, or a base plus displacement) and
  `Tables.resolve` the one that decides *which* table — an exact VA at offset 0, or a
  displacement inside a registered range at that offset.
  `--selftest` proves it against the original, against a one-byte corruption, against a
  moved branch target, against the four table cases (registered → PASS, changed
  contents → FAIL as a content difference, operand moved to another registered table →
  FAIL as a content difference, unregistered operand → FAIL literally), against the
  uninitialised-tail table path, against the base-plus-displacement shape (PASS with
  both operands resolved; a changed base register → FAIL as an instruction difference;
  changed table contents → FAIL as a table difference), against the in-table column
  offset (PASS with all 26 operands of the popup checker resolved; a column moved by
  eight bytes → FAIL as an instruction difference), and against the map's two
  decorations. Its verdict:
  `selftest: identical PASS, corrupted opcode FAIL, moved branch target FAIL, registered
  table PASS (2 operands resolved), changed table contents FAIL, moved table operand
  FAIL, unregistered table operand FAIL, uninitialised-tail table PASS (2 operands
  resolved), one-side-only tail table FAIL, map C and C++ symbols PASS, base+disp table
  PASS (2 operands resolved), changed base register FAIL (instruction), base+disp
  contents differ FAIL (table), in-table column offset PASS (26 operands resolved),
  changed column offset FAIL (instruction)`.
- `.scratch/byte_compare.py` — the byte-level comparison quoted in §2, §2a and §2b
  (absolute operands and `call`/`push` immediates are the only differences, by
  construction). Run it with body names to select rows; with no arguments it prints
  the six of the first pass. It classifies a table displacement as `other`, because it
  has no registry; §2b's 17/21 is that comparison with the two table displacements read
  as such.
- `.scratch/steer/compare_body.py` — §2c's, §2d's, §2e's, §2f's and §2g's byte
  comparison (250/320, 127/191, 40/53, 72/92, 77/125, 36/46); it knows the registered
  table displacements (all three shapes: indexed, base-plus-displacement, and a
  displacement anywhere inside a registered range) and reports a branch whose target
  offset differs. `steer`, `sirens_a`, `sirens`, `ide`, `popup`, `cone` select the body.
- `.scratch/steer/operand_map.py` — §2c item 3 through §2h item 3: every absolute
  operand, call immediate and string offset of a transcription resolved through the
  linker map and checked against a declared pairing (33 distinct for the steer consumer,
  20 for the first siren check, 8 for the second, 9 for the IDE payload, 24 for the
  popup checker including its sixteen columns, 8 for the cone check, 19 for the OOC
  archive builder including its twelve helper calls and three CRT forwarders; 0
  mismatch each). This is the check that says *which* slot an `abs` operand names,
  which the proof cannot — and for a table column it also requires the offset to match
  exactly.
- `.scratch/ooc_builder/compare.py` — §2h's byte comparison over the builder and its
  twelve helpers (275/314, 17/20, 26/28, 21/23, 10/11 and eight bodies 100 %), plus
  each body's byte size against the original's and every branch's target offset.
- `.scratch/table_report.py` — every `table` row's two content hashes and whether the
  range is all zero: the nine registered tables agree on every row (the model-name
  table's `57ad8796b1b2652c`, the cone hash table's 2,052-byte `02610d9b8ad1da81`, and
  `867e6dd4bf81dfd6` for every 2,048-byte zero table).
- `.scratch/steer/dump_body.py`, `.scratch/steer/peek.py`, `.scratch/steer/tables_probe.py`
  — the listing/constant/table probes §2c was written from.
- `.scratch/branch_targets.py` — the internal-branch-target check over every row, the
  independent reference implementation of what `verify_bodies.py` now does (it skips
  the `table` rows).
- `.scratch/resolve_calls.py` — the call/operand-to-symbol check quoted in §2 and §2a
  (what the proof's call-as-immediate rule cannot see).
- `.scratch/dfa_close/` — §2l's whole evidence. `probe.cpp` compiles four candidate C
  shapes at the module's flags; `coff.py` byte-compares each function with the oracle
  through the COFF symbol table (`VariantReturnExceptContinue1` matches: 16 bytes in 7
  words, all relocations). `probe_fault.cpp` measures the disposition in a child: the
  oracle's shape is still running at a 3 s watchdog, the old
  `EXECUTE_HANDLER`/`return -1` shape answers `0xFFFFFFFF`. `probe_slot.cpp` repeats the
  retry with the faulting callee in the exe's own `CloseHandle` IAT slot (the mechanism
  `tests/dfa_close.cpp` uses). `module_body.py` compares the *shipped* `launcher.obj`
  with the oracle and reads the object's relocations; `publish_check.py` disassembles the
  built `RegisterLauncher` and finds the four forwarder-slot stores; `scope.py` dumps the
  EH4 scope tables (`0x10153FC0`, `0x10153EE8`, `0x10153F08`); `values.cpp` is the
  `CloseHandle`/`ReadFile` answer matrix the assertions name.
- `.scratch/table_contents.py` — the registered table checked on its own (§2b): both
  sides' content hash over the registered length, the first and last entry of each, and
  every name in `source/model_names.inc` against the string the original's entry points
  at. It prints `38455 name(s) in the include, 0 mismatch(es)` and the two hashes above.
- `docs/parity.md` — per-key behaviour, the preserved faults, and the oracle-verified
  class; its rebuild notes now point here instead of repeating the reasons.
