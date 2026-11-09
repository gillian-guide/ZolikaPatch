// Generated from ZolikaPatch handlers; Nth-match jump installs (multi-hit
// collector 0x1005afa0). Fields: option, signature, match index, relative
// offset, jump destination VA, opcode (e8 = call, e9 = jmp). The apply path
// scans for ALL matches and writes opcode+rel32 at matches[match_index]+offset.
  {"BenchmarkFix", "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20", 0, 8, 0x10005650u, 0xe9u},
  {"BenchmarkFix", "E8 ? ? ? ? 83 C4 04 57 8B D8 FF D6 8B 77 20", 1, 8, 0x10005670u, 0xe9u},
  {"BorderlessWindowed", "FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00", 0, 0, 0x10005890u, 0xe8u},
  {"BorderlessWindowed", "FF 15 ? ? ? ? 5F 5E 5D 5B 83 C4 10 C2 10 00", 1, 0, 0x10005900u, 0xe8u},
