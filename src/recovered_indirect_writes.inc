// Generated from ZolikaPatch handlers and the loader; indirect-literal writes.
// Fields: option, signature, pointer offset (site = match + offset, then the
// dword there is the target), width in bytes, value written.
// Each store is bracketed by a VirtualProtect call in the recovered body.
  {"IncreaseVehicleModelLimit", "83 C7 01 81 FF 18 79 00 00 7C D2", 77, 4u, 0x400u},
  {"NoPhysicsSleep", "0F 00 00 00 C7 41 14 14 00 00 00 C7 41 1C 00 00", -38, 4u, 0x0u},
