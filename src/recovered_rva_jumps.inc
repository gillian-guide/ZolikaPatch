// Generated from ZolikaPatch handlers; recovered module-RVA relative jumps.
// Fields: option, patch-site module RVA, opcode (E9 jmp / E8 call), jump
// destination VA, handler VA.
// The apply path writes the opcode + rel32 computed from the module base,
// exactly as the handler does (the handler cannot store literal bytes
// because rel32 is runtime).
  {"LoadDLCs", 0x41356du, 0xe8u, 0x10023480u, 0x100234e0u},
  {"LoadDLCs", 0x4137cdu, 0xe8u, 0x10023480u, 0x100234e0u},
  {"LoadDLCs", 0x414274u, 0xe9u, 0x10023490u, 0x100234e0u},
  {"LoadDLCs", 0x4144d4u, 0xe9u, 0x10023490u, 0x100234e0u},
  {"LoadDLCs", 0x48f558u, 0xe8u, 0x10023480u, 0x100234e0u},
  {"LoadDLCs", 0x4903d4u, 0xe9u, 0x10023490u, 0x100234e0u},
  {"RemoveRGSCLoginRequirement", 0x498460u, 0xe9u, 0x10038550u, 0x10045490u},
  {"SuperLODFix", 0x4a3ce9u, 0xe9u, 0x100469e0u, 0x10046a10u},
  {"SuperLODFix", 0x540e19u, 0xe9u, 0x100469e0u, 0x10046a10u},
