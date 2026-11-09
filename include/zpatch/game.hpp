#pragma once

#include <cstdint>
#include <string_view>

namespace zpatch {

// FUN_10003e50: RAGE-style name hash used for model/path identifiers.
// A non-zero "hash:" value is returned directly; zero falls back to hashing
// the complete text, matching the observed routine.
std::uint32_t game_hash(std::string_view text) noexcept;

enum class GameBuild : std::uint8_t {
  unknown = 0,
  v1000 = 1,
  v1040 = 5,
  v1004 = 6,
  v1060 = 7,
  v1070 = 8,
  v1080 = 9,
  v1100 = 10,
  v1110 = 11,
  v1120 = 12,
  v1130 = 13,
  v1061 = 15,
  v12043 = 18,
};

// FUN_10003b80 concatenates the four decimal components without separators
// and parses the result as base 10 (1.0.7.0 -> 1070). Overflow/overlong input
// is rejected by this safe reconstruction rather than wrapped.
bool compose_game_version(std::uint16_t major, std::uint16_t minor,
                          std::uint16_t build, std::uint16_t revision,
                          std::uint32_t& output) noexcept;

// Exact switch recovered from FUN_10055c70. Unknown values stay unknown.
GameBuild classify_game_build(std::uint32_t compact_version) noexcept;

struct ProcessEntry { std::string_view executable; std::uint32_t process_id{}; };
// FUN_10003a10 scans the toolhelp snapshot in order for a case-sensitive
// "PlayGTAIV.exe" match and returns its process ID; 0 when absent. On Windows
// the snapshot is built with Toolhelp32; other hosts fail closed.
std::uint32_t find_game_process_id() noexcept;
// Pure selection step, exposed for caller-owned process lists and testing.
std::uint32_t select_game_process_id(const ProcessEntry* entries,
                                     std::size_t count, std::string_view name) noexcept;

// FUN_10003e20: reports whether a loaded compatibility module lacks the known
// signature. Bytes are read from a caller-owned image at fixed offsets; the
// original queries an OS module handle. A null image reports "not present".
bool compatibility_module_lacks_signature(const std::byte* image,
                                          std::size_t image_size) noexcept;

// FUN_10003b80 reads the current executable's VS_FIXEDFILEINFO and composes
// its four product-version words. Available on Windows; other hosts return false.
bool query_current_game_version(std::uint32_t& compact_version,
                                GameBuild& build) noexcept;

// The compact version string for a build (e.g. v1080 -> "1080", unknown ->
// "unknown"); used by the trace log's build classification line.
const char* game_build_name(GameBuild build) noexcept;

}  // namespace zpatch
