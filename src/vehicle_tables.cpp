#include "zpatch/vehicle_tables.hpp"
#include "zpatch/game.hpp"
#include <cerrno>
#include <cstdlib>
#include <string>
namespace zpatch {
Status load_vehicle_light_tables(const ConfigDocument& config, VehicleLightTables& output) noexcept {
  try {
    VehicleLightTables parsed;
    const char* sections[] = {"PopupLightAngles", "HeadlightConeOffsets"};
    const char* suffixes[] = {"Angle", "Offset"};
    for (unsigned table=0; table<2; ++table) {
      auto& slots = table==0 ? parsed.popup_angles : parsed.headlight_offsets;
      for (unsigned i=0; i<slots.size(); ++i) {
        const auto prefix = "Car" + std::to_string(i+1);
        const auto* name = config.find(sections[table], prefix+"Name");
        if (!name || name->value.empty()) continue;
        const auto* value = config.find(sections[table], prefix+suffixes[table]);
        if (!value) return {StatusCode::parse_error, "missing vehicle light value"};
        char* end=nullptr; errno=0;
        const float numeric = std::strtof(value->value.c_str(), &end);
        if (end==value->value.c_str() || errno==ERANGE)
          return {StatusCode::parse_error, "invalid vehicle light value"};
        auto& slot=slots[i]; slot.model_hash=game_hash(name->value); slot.populated=true;
        slot.value= table==0 ? static_cast<float>(static_cast<double>(numeric)*3.141592653589793/180.0) : numeric;
      }
    }
    output=parsed;
    return {};
  } catch (...) { return {StatusCode::invalid_input, "vehicle table allocation failed"}; }
}

PopupLightSelection select_popup_light(const VehicleLightTables& tables,
    const std::array<std::uint32_t,512>& supported_models, int model_index,
    ModelIndexResolver resolve, void* context) noexcept {
  PopupLightSelection result;
  if (!resolve || model_index < 0) return result;
  // FUN_10044310 handles the built-in special model before the +popuplights list.
  const int special_index=resolve(0xeb9f21d3u, context);
  if (special_index >= 0 && special_index==model_index) {
    result.supported=true;
    return result;
  }
  for (auto hash : supported_models) {
    if (!hash) break;
    if (resolve(hash,context)!=model_index) continue;
    result.supported=true;
    // The original scans all 512 slots; later duplicate hashes overwrite earlier ones.
    for (const auto& entry : tables.popup_angles)
      if (entry.populated && entry.model_hash!=0 && entry.model_hash==hash)
        result.angle_radians=entry.value;
    break;
  }
  return result;
}

float select_headlight_offset(const VehicleLightTables& tables, int model_index,
    ModelIndexResolver resolve, void* context) noexcept {
  if (!resolve || model_index < 0) return 2.0f;
  // FUN_1001ad20 stops at the first zero hash rather than skipping holes.
  for (const auto& entry : tables.headlight_offsets) {
    if (!entry.model_hash) break;
    if (entry.populated && resolve(entry.model_hash,context)==model_index)
      return entry.value;
  }
  return 2.0f;
}
}
