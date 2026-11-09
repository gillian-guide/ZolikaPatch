#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include "zpatch/vehicle_tables.hpp"
namespace zpatch {
struct SteeringAngleEntry { std::uint32_t model_hash{}; float degrees{}; };
struct VehicleIdeFlags {
  std::array<std::uint32_t,512> popup_models{};
  std::array<std::uint32_t,512> siren_models{};
  std::array<SteeringAngleEntry,512> steering_angles{};
};
enum class IdeFlagResult { ignored, inserted, duplicate, full, invalid_model };
// Reconstructs FUN_10003fa0's token handler. Call with the token after '+' has
// been removed by the IDE reader. No game object or executable memory is used.
IdeFlagResult add_vehicle_ide_flag(VehicleIdeFlags&, std::uint32_t model_hash,
                                  std::string_view token) noexcept;
// Decision portion of FUN_10005f70: built-in emergency models plus custom
// +siren entries. Negative model indices fail closed rather than matching -1.
bool model_has_siren(const VehicleIdeFlags&, int model_index,
                     ModelIndexResolver, void* context=nullptr) noexcept;
struct SteeringWheelRotation { float tilt_radians{}; float turn_radians{}; };
// Pure calculation from FUN_10004c70's steering-wheel branch. Fails closed
// for nonfinite input or a zero steering limit; leaves output unchanged.
bool calculate_steering_wheel(const VehicleIdeFlags&, int model_index,
    float steering, float steering_limit, ModelIndexResolver,
    SteeringWheelRotation& output, void* context=nullptr) noexcept;
}
