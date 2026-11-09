#include "zpatch/patch_catalog.hpp"

namespace zpatch {
namespace {

// Transcribed from [Options] in the inspected ZolikaPatch.ini. Defaults are
// strings on purpose; this table is evidence, not an implementation schema.
// Patch7RemoveModChecks is the one undocumented loader key (read at 0x100553A9,
// default "0"; it shares the 6&7 handler 0x10044710 and enables the same set).
constexpr FeatureEvidence kFeatures[] = {
    {"AddOptionsInMenu", "1"},
    {"AimProjectileCrashFix", "1"},
    {"AllowHashesForIDE", "1"},
    {"BadPlayerModelCrashFix", "1"},
    {"BenchmarkFix", "1"},
    {"BetterMPSync", "0"},
    {"BikeFeetFix", "1"},
    {"BikePhoneAnimsFix", "1"},
    {"BlackscreenFix", "1"},
    {"BorderlessWindowed", "1"},
    {"BuildingAlphaFix", "1"},
    {"BuildingDynamicShadows", "0"},
    {"CarDecalsFix", "1"},
    {"CarDynamicShadowFix", "1"},
    {"CarPartsShadowFix", "1"},
    {"CollisionDeadlockFix", "1"},
    {"CombinedRadioFreezeFix", "1"},
    {"CopSirensFlag", "1"},
    {"CrashMsgFix", "1"},
    {"CutsceneFixes", "1"},
    {"DebugPrintSpawns", "0"},
    {"DoNotPauseOnMinimize", "0"},
    {"DrivebyFOVFix", "1"},
    {"DualVehicleHeadlights", "0"},
    {"EmissiveLerpFix", "1"},
    {"EpisodeSwitchFreezeFix", "1"},
    {"EpisodicVehicleSupport", "1"},
    {"EpisodicWeaponSupport", "1"},
    {"FastLoading", "1"},
    {"FixPCIDKickbug", "1"},
    {"ForceCarHeadlightShadows", "0"},
    {"ForceDynamicShadowsEverywhere", "0"},
    {"ForceShadowsOnObjects", "1"},
    {"FreezeCarFix", "1"},
    {"FreezeGunFix", "1"},
    {"GarageModelFix", "1"},
    {"GroupHackFix", "1"},
    {"HighFPSBikePhysicsFix", "1"},
    {"HighFPSSpeedupFix", "1"},
    {"HighQualityReflections", "0"},
    {"ImprovedShaderStreaming", "1"},
    {"IncreasePtrNodes", "1"},
    {"IncreaseVehicleModelLimit", "1"},
    {"IncreaseVehicleStructLimit", "1"},
    {"InfiniteCarBottomDistance", "1"},
    {"LaunchWithPlayGTAIV", "1"},
    {"LocalSettingsPath", "0"},
    {"LoadDLCs", "1"},
    {"MiscFixes", "1"},
    {"MouseFix", "1"},
    {"MovingPlaneParts", "1"},
    {"MovingSteeringWheelSupport", "1"},
    {"MPNikoCrashFix", "1"},
    {"NewFilesCompatibility", "1"},
    {"NewSavesCompatibility", "1"},
    {"NewMemorySystem", "0"},
    {"NoImpostorHeightCutoff", "1"},
    {"NoLiveryLimit", "1"},
    {"NoLoadingSleep", "1"},
    {"NoLODLightHeightCutoff", "0"},
    {"NoPhysicsSleep", "1"},
    {"OutOfCommissionFix", "1"},
    {"OverrideGTADatPath", "0"},
    {"OverrideGTADatPathTo", "common:/data/gta.dat"},
    {"Patch4RemoveModChecks", "0"},
    {"Patch6and7RemoveModChecks", "1"},
    {"Patch7RemoveModChecks", "0"},  // undocumented loader key (F7)
    {"PathNodeCrashFix", "1"},
    {"PedModelsInCutscenesFix", "1"},
    {"PipeBombDropIconSupport", "1"},
    {"PlaneCrashFix", "1"},
    {"PlayerAsPedComponentsFix", "1"},
    {"PoliceEpisodicWeaponSupport", "0"},
    {"PopupHeadlightsFlag", "1"},
    {"RemoveAllWeaponsProtection", "1"},
    {"RemoveBoundingBoxCulling", "0"},
    {"RemoveGFWLUpdatePopup", "0"},
    {"RemoveRGSCLoginRequirement", "1"},
    {"RemoveStaticCarShadows", "0"},
    {"RemoveStaticCutsceneCarShadows", "0"},
    {"RemoveUselessChecks", "0"},
    {"RestoreDeathMusic", "0"},
    {"ReversingLightFix", "1"},
    {"SavegameFix", "1"},
    {"SCOSignatureFix", "1"},
    {"ShakingEngineBlock", "1"},
    {"SkipIntro", "1"},
    {"SkipLauncher", "1"},
    {"SkipMenu", "0"},
    {"SMPA60Fix", "1"},
    {"SpeedupBugFix", "1"},
    {"StartupTimeFix", "1"},
    {"SuperLODFix", "1"},
    {"TBoGTHeliHeightLimit", "0"},
    {"TeleportProtection", "1"},
    {"TryToSkipAllErrors", "1"},
    {"VRAMFix", "1"},
    {"VSyncFix", "1"},
    {"WantedLevelCrashFix", "1"},
};
static_assert(sizeof(kFeatures) / sizeof(kFeatures[0]) == 99);

}  // namespace

FeatureCatalogView observed_feature_catalog() noexcept {
  return {kFeatures, sizeof(kFeatures) / sizeof(kFeatures[0])};
}

namespace {
constexpr RecoveredFeature kRecovered[] = {
#include "recovered_feature_coverage.inc"
};
static_assert(sizeof(kRecovered) / sizeof(kRecovered[0]) == 99);
bool equals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) return false;
  return true;
}
}  // namespace

RecoveredFeatureCatalogView recovered_feature_catalog() noexcept {
  return {kRecovered, sizeof(kRecovered) / sizeof(kRecovered[0])};
}

const RecoveredFeature* find_recovered_feature(std::string_view key) noexcept {
  for (const auto& row : kRecovered) if (equals(row.ini_key, key)) return &row;
  return nullptr;
}

bool recovered_feature_allows_build(std::string_view key, std::uint8_t ordinal) noexcept {
  const auto* row = find_recovered_feature(key);
  if (row == nullptr) return true;             // unknown key: no recovered restriction
  if (!row->has_build_gate) return true;       // no gate recovered: unconditional
  if (ordinal >= 32u) return false;
  return (row->build_mask & (1u << ordinal)) != 0;
}

}  // namespace zpatch
