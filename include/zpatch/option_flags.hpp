#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "zpatch/config.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// One recovered option->global-flag binding. `confidence` is true only when the
// bounded-control-flow map produced a single global that is also read elsewhere
// (see evidence/option_flag_map.json).
struct OptionFlagBinding final {
  std::string_view ini_key;
  std::uint32_t global_address;
  bool confidence;
};

constexpr std::size_t kSlotCount = 32;

struct OptionFlagState final {
  std::array<std::uint8_t, kSlotCount> flags{};  // one slot per confirmed binding
  std::size_t bindings_applied{};
};

// Applies INI option values onto the recovered flag slots with the loader arms'
// semantics: the parsed value must be exactly 1, and an absent key uses the
// recovered per-option arm default (MovingPlaneParts/MovingSteeringWheelSupport/
// ShakingEngineBlock/AddOptionsInMenu/DrivebyFOVFix default to "1", the rest to
// "0"). See zpatch::option_enabled.
Status apply_option_flags(const ConfigDocument&, OptionFlagState&) noexcept;

// The confirmed bindings, exposed for tests and inspection.
const OptionFlagBinding* confirmed_option_flags(std::size_t& count) noexcept;

// Reads a recovered flag by its original global address.
bool option_flag_enabled(const OptionFlagState&, std::uint32_t global_address) noexcept;

}  // namespace zpatch
