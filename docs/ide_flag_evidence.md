# IDE flag evidence: `FUN_10003fa0`

## Scope and evidence

Read-only review of `ZolikaPatch_decompiled/ghidra/decompiled_0000.c`,
`FUN_10003fa0` at `0x10003fa0` (lines 3363–3433). Line references below
refer to that file. This document describes the token handler associated with
vehicles.ide flags, not a complete IDE lexer or file loader. The function itself
does not contain the filename `vehicles.ide`, split fields, trim tokens, or
compute a hash. It takes its token pointer from `DAT_1015be08` and reads the
current vehicle key (referred to here as the hash) from
`*(int *)(DAT_1015be0c + 0x3c)`.

Conclusions are from the decompiled code and standard library call contracts;
no original binary was executed. Packed comparisons assume the target's
little-endian representation and readable token storage.

## Token spelling and case

| Token | Evidence | Matching behavior | Destination |
| --- | --- | --- | --- |
| `popuplights` | Lines 3375–3376: words `0x75706f70`, `0x67696c70`, `0x00737468` decode to `popu`, `plig`, `hts\0` | Exact lowercase, NUL-terminated spelling; not a case-insensitive or prefix match | Hashes at `DAT_1015ce28` |
| `siren` | Line 3394: word `0x65726973` and low short `0x006e` decode to `sire`, `n\0` | Exact lowercase, NUL-terminated spelling | Hashes at `DAT_10159380` |
| `wheelangle:` | Line 3411: `_strnicmp(token, "wheelangle:", 0xb)` | Case-insensitive comparison of the first 11 bytes, including the colon; suffix is not part of this comparison | Hashes at `DAT_1015a538`, parallel float values at `DAT_10159d38` |

Thus `PopupLights`, `SIREN`, `popuplightsExtra`, and `siren ` do not match
these exact token comparisons. `WHEELANGLE:30` does match the wheel-angle
prefix. `wheelangle` without the colon and `wheelangle :30` do not.
Leading whitespace is not skipped before any token comparison here; upstream
normalization, if any, is outside this function's evidence.

## Wheel-angle numeric parsing

Lines 3413 and 3430 perform:

```c
strtof(token + 11, NULL);  /* decompiler renders the pointer as an integer */
/* later: */
angle_values[index] = (float)parsed_value;
```

The entire suffix is **not** validated. No end pointer, conversion-success
check, `errno` check, finiteness check, range clamp, or fallback to a previous
value appears. Parsing precedes the duplicate/capacity scan, even when that
scan subsequently prevents storage. The decompiler's `float10` temporary is
not evidence of an extended-precision table: the final store casts to `float`.

Consequences under the standard `strtof` contract:

- `wheelangle:30` stores 30; `wheelangle: -12.5` accepts the suffix's leading
  whitespace and sign.
- `wheelangle:12.5junk` stores 12.5: unconsumed trailing characters are ignored.
- `wheelangle:` and `wheelangle:junk` yield zero on no conversion, and that zero
  is stored if insertion succeeds; malformed numeric input is not rejected.
- Exponent forms and supported special/hexadecimal floating forms follow the
  linked CRT's `strtof` implementation. NaN, infinity, overflow, and underflow
  are not explicitly filtered here. Exact locale-sensitive spelling and CRT
  edge behavior cannot be established from this call site alone.

## Duplicate handling

Each token category has its own zero-terminated hash table. The scan compares
only the hash, not a model name or the token's numeric value (lines 3381,
3399, 3418). Consequently equal nonzero hashes deduplicate within a category,
including collisions between different model names. The same hash can exist
independently in all three categories.

- `popuplights`: an existing hash jumps to `LAB_1000400a`, skipping insertion
  but continuing with the later token checks (lines 3381–3382).
- `siren`: an existing hash jumps to `LAB_1000405a` (line 3399).
- `wheelangle:`: an existing hash returns before either store (lines 3418–3419).
  **First stored angle wins** for a nonzero hash: a later angle does not update
  it, even if the later suffix is valid or the first suffix parsed as zero.

No duplicate increments a count or consumes another slot. There is no explicit
count in this handler: occupancy is inferred from the first zero hash.

## The 512-entry bound

All three insertion loops use four-byte steps and the same check
`if (0x7fc < offset + 4) return;` (lines 3384, 3401, 3422).
`0x7fc / 4 = 511`, so the valid indices are **0 through 511 inclusive**:

1. Slot 0 is inserted directly when its hash is zero.
2. From occupied slot 510 (offset `0x7f8`), the next offset is `0x7fc`,
   which is allowed; slot 511 is read and can be filled.
3. At occupied slot 511 (offset `0x7fc`), a matching hash is detected first.
   Otherwise the next offset would be `0x800`, and the function returns before
   reading or writing slot 512.

A full table therefore rejects a new distinct hash silently, but still detects
a duplicate in its final slot. It can hold 512 nonzero hashes, not 511; it does
not reserve the last slot for a sentinel or write a new sentinel after insertion.
The tables rely on existing zero slots until full. A full-table return exits the
whole handler, rather than merely skipping that category.

Supporting consumer evidence: `FUN_10004c70`, lines 4075–4084, scans the wheel
hash table until zero or `iVar4 == 0x200` (512), and reads the parallel angle at
the same index. This agrees with the insertion bound.

## Hash zero is an empty-slot sentinel, not a stored key

There is **no explicit rejection of hash zero** at this call site. Nevertheless,
zero cannot be represented as an occupied entry:

- In an empty table, insertion writes zero to slot 0 and leaves it logically
  empty. Repeated zero-hash tokens take the same path, without deduplication.
- In a partially filled table, the scan stops at the first zero before comparing
  that slot to the incoming hash. Inserting hash zero writes zero back to that
  same slot; it does not extend the occupied prefix.
- For `wheelangle:`, the parallel float **is still written** at that empty slot.
  Repeated zero-hash inputs can overwrite that float. A later nonzero insertion
  uses the same slot and overwrites its angle again. The wheel consumer stops
  at the zero hash (line 4076), so the zero-hash angle is not discoverable there.
- In a table full of nonzero hashes, an incoming zero hash is rejected by the
  capacity check with no write.

Thus “first angle wins” applies to stored **nonzero** keys, not hash-zero inputs.
Nothing here establishes whether the upstream hash producer can actually emit
zero; these are the handler's behaviors if it does.

## Review verification and limitations

Packed constants were decoded as little-endian bytes. A separate in-memory
model of the shown insertion loop checked duplicate handling, insertion into
slot 511, full-table rejection, duplicate detection in a full table, and
zero-hash angle overwrite/reuse. These checks validate the reading of the
control flow, not execution of the original program or its CRT.

Only this evidence document was written. No source, build output, or original
file was modified. Existing broader analysis documents were left untouched.
