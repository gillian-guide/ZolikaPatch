#include "zpatch/ide_flags.hpp"
#include "zpatch/game.hpp"
#include <cstdlib>
#include <cmath>
#include <string>
namespace zpatch {
namespace {
IdeFlagResult add_hash(std::array<std::uint32_t,512>& hashes, std::uint32_t hash) noexcept {
  for (auto& slot : hashes) {
    if (slot==hash) return IdeFlagResult::duplicate;
    if (!slot) { slot=hash; return IdeFlagResult::inserted; }
  }
  return IdeFlagResult::full;
}
bool wheel_prefix(std::string_view token) noexcept {
  constexpr std::string_view prefix="wheelangle:";
  if (token.size()<prefix.size()) return false;
  for (std::size_t i=0;i<prefix.size();++i) {
    unsigned char c=static_cast<unsigned char>(token[i]);
    if (c>='A' && c<='Z') c=static_cast<unsigned char>(c+32);
    if (c!=static_cast<unsigned char>(prefix[i])) return false;
  }
  return true;
}
}
IdeFlagResult add_vehicle_ide_flag(VehicleIdeFlags& flags, std::uint32_t hash,
                                  std::string_view token) noexcept {
  if (!hash) return IdeFlagResult::invalid_model; // zero is the original empty sentinel
  if (token=="popuplights") return add_hash(flags.popup_models,hash);
  if (token=="siren") return add_hash(flags.siren_models,hash);
  if (!wheel_prefix(token)) return IdeFlagResult::ignored;
  try {
    // The original strtof supplies no end pointer: invalid text yields zero,
    // numeric prefixes/trailing text and non-finite values are accepted.
    const std::string value(token.substr(11));
    const float angle=std::strtof(value.c_str(),nullptr);
    for (auto& entry : flags.steering_angles) {
      if (entry.model_hash==hash) return IdeFlagResult::duplicate;
      if (!entry.model_hash) { entry={hash,angle}; return IdeFlagResult::inserted; }
    }
    return IdeFlagResult::full;
  } catch (...) { return IdeFlagResult::ignored; }
}

bool model_has_siren(const VehicleIdeFlags& flags, int model_index,
                     ModelIndexResolver resolve, void* context) noexcept {
  if (!resolve || model_index<0) return false;
  constexpr const char* emergency_models[] = {
    "police", "police2", "police3", "police4", "noose", "pstockade",
    "nstockade", "polpatriot", "fbi", "n_police", "n_police2",
    "n_polpatriot", "n_pstockade"
  };
  for (const auto* name : emergency_models)
    if (resolve(game_hash(name),context)==model_index) return true;
  // Original custom table is zero-terminated and bounded to 512 hashes.
  bool found=false;
  for (auto hash : flags.siren_models) {
    if (!hash) break;
    if (resolve(hash,context)==model_index) found=true;
  }
  return found;
}

bool calculate_steering_wheel(const VehicleIdeFlags& flags, int model_index,
    float steering, float steering_limit, ModelIndexResolver resolve,
    SteeringWheelRotation& output, void* context) noexcept {
  if (!resolve || model_index<0 || !std::isfinite(steering) ||
      !std::isfinite(steering_limit) || steering_limit==0.0f) return false;
  SteeringWheelRotation result;
  for (const auto& entry : flags.steering_angles) {
    if (!entry.model_hash) break;
    if (resolve(entry.model_hash,context)==model_index) {
      result.tilt_radians=static_cast<float>(static_cast<double>(entry.degrees)*3.141592653589793/180.0);
      break;
    }
  }
  // Preserve the observed float division before multiplication by double -pi.
  result.turn_radians=static_cast<float>(static_cast<double>(steering/steering_limit)*-3.141592653589793);
  if (!std::isfinite(result.tilt_radians) || !std::isfinite(result.turn_radians)) return false;
  output=result;
  return true;
}
}
