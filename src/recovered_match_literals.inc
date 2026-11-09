// Generated from ZolikaPatch handlers; Nth-match literal/NOP writes.
// Fields: option, signature, match index (-1 = every match), relative offset,
// width, value. The apply path scans for ALL matches and writes at
// matches[match_index] + offset (or every match when the index is -1).
  {"NoImpostorHeightCutoff", "76 0B F3 0F 11 8C 24 88 00 00 00 EB 09", 0, 0, 2u, 0x9090u},
  {"NoImpostorHeightCutoff", "76 0B F3 0F 11 8C 24 88 00 00 00 EB 09", 1, 0, 2u, 0x9090u},
  {"StartupTimeFix", "68 00 C2 EC 80 68", -1, 6, 4u, 0x101580d8u},
