// Generated from option handler build gates (head gates; per-build branch unions).
// Fields: option, build mask (bit i set == GameBuild ordinal i enabled).
  {"CarDynamicShadowFix@noE", 0x22u},
  {"DualVehicleHeadlights@new", 0x4bf80u},
  {"DualVehicleHeadlights@old", 0x62u},
  {"EpisodicVehicleSupport", 0x4bfc0u},
  {"EpisodicWeaponSupport", 0x4bfc0u},
  {"LoadDLCs", 0x380u},
  {"Patch6and7RemoveModChecks", 0x180u},
  {"Patch7RemoveModChecks", 0x180u},
  {"PipeBombDropIconSupport", 0x4bfc0u},
  {"PoliceEpisodicWeaponSupport", 0x4bfc0u},
  {"RemoveUselessChecks", 0x320u},
  {"SuperLODFix", 0x300u},
  {"TBoGTHeliHeightLimit", 0x4bfc0u},
  // Relational gates from the same evidence JSON; the first extraction only
  // emitted positive equality checks. Masks are the complement of the excluded
  // ordinals: Benchmark excludes 9 (1080) and 13 (1130); VSync excludes
  // 1..5, 9 and 13.
  {"BenchmarkFix", 0x49de2u},
  {"VSyncFix", 0x49dc0u},
  // LaunchWithPlayGTAIV switches on the build id (cases 1, 5, 7, 8 and 9 at
  // 0x100441D5/1E8/1FB/20E plus the case-9 branch). The old "!=9" reading
  // came from the switch dispatch and wrongly disabled 1.0.8.0, which has
  // its own RVA pair.
  {"LaunchWithPlayGTAIV", 0x3A2u},
