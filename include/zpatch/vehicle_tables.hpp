#pragma once
#include <array>
#include <cstdint>
#include "zpatch/config.hpp"
namespace zpatch {
struct VehicleLightValue { std::uint32_t model_hash{}; float value{}; bool populated{}; };
struct VehicleLightTables {
  std::array<VehicleLightValue,512> popup_angles{}; // radians
  std::array<VehicleLightValue,512> headlight_offsets{};
};
// Reconstructs the two 512-slot loops in FUN_100523c0. Missing/empty model
// names leave holes; numeric parsing follows stof's prefix acceptance.
// Invalid/range-error values fail atomically instead of throwing from startup.
Status load_vehicle_light_tables(const ConfigDocument&, VehicleLightTables&) noexcept;
// A pure adapter for the original hash-to-model-index callback. Return -1 for
// unknown models. No pointer into GTA IV is required by these lookups.
using ModelIndexResolver = int (*)(std::uint32_t, void*);
struct PopupLightSelection { float angle_radians{1.5707963705062866f}; bool supported{}; };
PopupLightSelection select_popup_light(const VehicleLightTables&,
    const std::array<std::uint32_t,512>& supported_models, int model_index,
    ModelIndexResolver, void* context=nullptr) noexcept;
float select_headlight_offset(const VehicleLightTables&, int model_index,
    ModelIndexResolver, void* context=nullptr) noexcept;
}
