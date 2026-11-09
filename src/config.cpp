#include "zpatch/config.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace zpatch {
namespace {

constexpr bool is_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

std::string_view trim(std::string_view value) noexcept {
  while (!value.empty() && is_space(value.front())) value.remove_prefix(1);
  while (!value.empty() && is_space(value.back())) value.remove_suffix(1);
  return value;
}

Status parse_failure() noexcept {
  // Status deliberately owns no storage, so keep diagnostics static. Callers
  // can distinguish malformed input from I/O and allocation failures by code.
  return {StatusCode::parse_error, "malformed INI input"};
}

// The per-option string the original's map getter substitutes when the INI key
// is absent (each arm pushes it before the key; sub_1005C160/1005BFF0 return it
// on a miss). Generated from the loader arms; the shipped INI carries every
// key, so these matter only for trimmed/hand-edited INIs.
constexpr OptionArmDefault kArmDefaults[] = {
#include "recovered_arm_defaults.inc"
};

const char* arm_default(std::string_view option) noexcept {
  for (const OptionArmDefault& row : kArmDefaults) {
    const char* key = row.key;
    std::size_t i = 0;
    for (; i < option.size(); ++i)
      if (key[i] == '\0' || key[i] != option[i]) break;
    if (i == option.size() && key[i] == '\0') return row.value;
  }
  return nullptr;
}

}  // namespace

const OptionArmDefault* option_arm_defaults(std::size_t& count) noexcept {
  count = sizeof(kArmDefaults) / sizeof(kArmDefaults[0]);
  return kArmDefaults;
}

const char* option_arm_default(std::string_view option) noexcept {
  return arm_default(option);
}

const ConfigEntry* ConfigDocument::find(std::string_view section_name,
                                        std::string_view key_name) const noexcept {
  for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
    if (entry->section == section_name && entry->key == key_name) return &*entry;
  return nullptr;
}

Status parse_config(std::string_view text, ConfigDocument& output) noexcept {
  try {
    ConfigDocument parsed;
    std::string section;
    bool first_line = true;
    std::size_t offset = 0;

    while (offset <= text.size()) {
      const std::size_t end = text.find('\n', offset);
      std::string_view line = text.substr(
          offset, end == std::string_view::npos ? text.size() - offset : end - offset);
      offset = end == std::string_view::npos ? text.size() + 1 : end + 1;

      if (first_line) {
        first_line = false;
        constexpr std::string_view bom{"\xEF\xBB\xBF", 3};
        if (line.substr(0, bom.size()) == bom) line.remove_prefix(bom.size());
      }

      line = trim(line);
      if (line.empty() || line.front() == ';' || line.front() == '#') continue;

      if (line.front() == '[') {
        const std::size_t close = line.find(']');
        if (close == std::string_view::npos) return parse_failure();
        const std::string_view name = trim(line.substr(1, close - 1));
        const std::string_view suffix = trim(line.substr(close + 1));
        if (name.empty() || (!suffix.empty() && suffix.front() != ';' && suffix.front() != '#')) {
          return parse_failure();
        }
        section.assign(name);
        continue;
      }

      const std::size_t equals = line.find('=');
      if (equals == std::string_view::npos) return parse_failure();
      const std::string_view key = trim(line.substr(0, equals));
      const std::string_view value = trim(line.substr(equals + 1));
      if (key.empty() || key.front() == '[') return parse_failure();
      parsed.entries.push_back({section, std::string(key), std::string(value)});
    }

    output = std::move(parsed);
    return {};
  } catch (...) {
    return {StatusCode::invalid_input, "unable to allocate INI document"};
  }
}

Status load_config(std::string_view path, ConfigDocument& output) noexcept {
  if (path.empty()) return {StatusCode::invalid_input, "empty INI path"};
  try {
    const std::string owned_path(path);
    std::ifstream input(owned_path, std::ios::binary);
    if (!input) return {StatusCode::io_error, "unable to open INI file"};
    std::string contents((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
    if (input.bad()) return {StatusCode::io_error, "unable to read INI file"};
    return parse_config(contents, output);
  } catch (...) {
    return {StatusCode::invalid_input, "unable to allocate INI input"};
  }
}

bool option_enabled(const ConfigDocument& config, std::string_view option) noexcept {
  // The original's arms read the key through sub_1005C160/1005BFF0, which
  // substitutes the arm's default string on a miss, then parse it (radix 10)
  // and run the install iff the parsed value is exactly 1 (`cmp esi,1`).
  // Absent keys therefore use the per-option arm default, not "off".
  const ConfigEntry* entry = config.find(kOptionsSection, option);
  const char* text = entry ? entry->value.c_str() : arm_default(option);
  if (text == nullptr) return false;  // no arm reads this key
  return std::strtol(text, nullptr, 10) == 1;
}

}  // namespace zpatch
