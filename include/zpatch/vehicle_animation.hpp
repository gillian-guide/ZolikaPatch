#pragma once
#include <cstdint>
#include <array>
#include <cstddef>
#include <string_view>
namespace zpatch {
struct NamedVehicleBone {
  std::string_view name;
  std::uint16_t bone_id{};
};
// Name-comparison portion of FUN_10003f40: ASCII-insensitive, first match.
// The caller owns records; no game memory or unresolved 0xe0 stride is read.
const NamedVehicleBone* find_vehicle_bone(const NamedVehicleBone* records,
    std::size_t count, std::string_view name) noexcept;
// FUN_10004c70 writes rotation fields 0,1,2 / 4,5,6 / 8,9,10 only.
// Caller-owned 4x4 storage; translation and the fourth lane are preserved.
void reset_vehicle_bone_rotation(std::array<float,16>& matrix) noexcept;
struct EngineAnimationState {
  float phase{};
  bool descending{};
  float previous_timestep{};
};
// Pure state/rotation portion of FUN_10004c70. Same timestep suppresses phase
// advancement; thresholds reverse direction without clamping overshoot.
// Flags 0x08 enable RPM scaling and 0x10 override scaling with 1.0.
bool update_engine_animation(EngineAnimationState&, float timestep,
    float rpm, std::uint32_t vehicle_flags, float& rotation) noexcept;
struct SuspensionBoneCommand {
  std::uint32_t bone_id{};
  int mode{};
  float value{};
  bool relative{};
};
using SuspensionCommands = std::array<SuspensionBoneCommand,9>;
// Reconstructs the nine suspension callback arguments in FUN_10004c70.
// Enabled only for vehicle type 5; rejection leaves commands unchanged.
bool calculate_suspension_commands(bool enabled, int vehicle_type,
    float timestep, float suspension_motion, float pitch, float roll, float heave,
    SuspensionCommands& output) noexcept;
}
