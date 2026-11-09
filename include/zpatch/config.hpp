#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "zpatch/status.hpp"

namespace zpatch {

// Entries retain their file order. Unknown keys and duplicate keys are kept so
// callers can apply their own schema without making the parser version-bound.
struct ConfigEntry final {
  std::string section;
  std::string key;
  std::string value;
};

struct ConfigDocument final {
  std::vector<ConfigEntry> entries;

  // Duplicate keys are retained; lookup follows the common last-value-wins rule.
  const ConfigEntry* find(std::string_view section, std::string_view key) const noexcept;
};

inline constexpr std::string_view kDefaultConfigFile = "ZolikaPatch.ini";
inline constexpr std::string_view kOptionsSection = "Options";
inline constexpr std::string_view kPopupLightAnglesSection = "PopupLightAngles";
inline constexpr std::string_view kHeadlightConeOffsetsSection = "HeadlightConeOffsets";

// Parse an INI document. Blank lines and lines whose first non-space character
// is ';' or '#' are ignored. Names and unquoted values are trimmed; the first
// '=' separates key from value. Unknown and duplicate keys are accepted.
// output is replaced only after the complete input has parsed successfully.
Status parse_config(std::string_view text, ConfigDocument& output) noexcept;

// Read and parse an INI file. path need not be NUL-terminated. As above,
// output is unchanged on failure.
Status load_config(std::string_view path, ConfigDocument& output) noexcept;

// The original loader reads each option through a map getter that substitutes
// the arm's default string when the key is absent, parses it as base-10, and
// applies the option iff the parsed value is exactly 1 (`cmp esi,1`). An absent
// key therefore uses the recovered per-option arm default (usually "1"), and a
// key with a value other than 1 (e.g. 2) disables the option.
bool option_enabled(const ConfigDocument& config, std::string_view option) noexcept;

// The recovered arm-default table (generated from the loader arms; the shipped
// INI carries every key, so these matter for trimmed/hand-edited INIs). The
// log prints them when a key is absent.
struct OptionArmDefault final {
  const char* key;
  const char* value;
};
const OptionArmDefault* option_arm_defaults(std::size_t& count) noexcept;
const char* option_arm_default(std::string_view option) noexcept;

}  // namespace zpatch
