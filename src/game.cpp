#include "zpatch/game.hpp"

#include <limits>
#include <new>
#include <cstddef>
#include <cstring>
#include <cwchar>

#if defined(_WIN32)
// windows.h defines max/min as macros, which breaks every std::max in this file.
# ifndef NOMINMAX
#  define NOMINMAX
# endif

# include <windows.h>
# include <winver.h>
# include <tlhelp32.h>
#endif

namespace zpatch {
namespace {
std::uint32_t parse_ulong32(std::string_view text) noexcept {
  std::size_t pos = 0;
  while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' ||
         text[pos] == '\r' || text[pos] == '\n' || text[pos] == '\f' ||
         text[pos] == '\v')) ++pos;
  bool negative = false;
  if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
    negative = text[pos] == '-'; ++pos;
  }
  unsigned base = 10;
  if (pos < text.size() && text[pos] == '0') {
    base = 8;
    if (pos + 2 < text.size() && (text[pos + 1] == 'x' || text[pos + 1] == 'X') &&
        ((text[pos + 2] >= '0' && text[pos + 2] <= '9') ||
         (text[pos + 2] >= 'a' && text[pos + 2] <= 'f') ||
         (text[pos + 2] >= 'A' && text[pos + 2] <= 'F'))) pos += 2, base = 16;
  }
  std::uint32_t value = 0;
  bool any = false, overflow = false;
  for (; pos < text.size(); ++pos) {
    const unsigned char c = static_cast<unsigned char>(text[pos]);
    unsigned digit = c >= '0' && c <= '9' ? c - '0' :
                     c >= 'a' && c <= 'z' ? c - 'a' + 10 :
                     c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 36;
    if (digit >= base) break;
    any = true;
    if (value > (std::numeric_limits<std::uint32_t>::max() - digit) / base)
      overflow = true;
    else if (!overflow) value = value * base + digit;
  }
  if (!any) return 0;
  if (overflow) return std::numeric_limits<std::uint32_t>::max();
  return negative ? 0u - value : value;
}

std::uint32_t hash_text(std::string_view text) noexcept {
  bool quoted = !text.empty() && text.front() == '"';
  if (quoted) text.remove_prefix(1);
  std::uint32_t value = 0;
  for (unsigned char c : text) {
    if (quoted && c == '"') break;
    if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c + ('a' - 'A'));
    else if (c == '\\') c = '/';
    value = (value + c) * 0x401u;
    value = (value >> 6u) ^ value;
  }
  value *= 9u;
  value = (value >> 11u) ^ value;
  value *= 0x8001u;
  return value > 1u ? value : value + 2u;
}
}  // namespace

std::uint32_t game_hash(std::string_view text) noexcept {
  if (text.size() >= 5 && (text[0] == 'h' || text[0] == 'H') &&
      (text[1] == 'a' || text[1] == 'A') &&
      (text[2] == 's' || text[2] == 'S') &&
      (text[3] == 'h' || text[3] == 'H') && text[4] == ':') {
    const std::uint32_t parsed = parse_ulong32(text.substr(5));
    // The observed 32-bit strtoul call accepts whitespace/signs and a numeric
    // prefix, and only a zero result falls back to hashing the complete text.
    if (parsed != 0) return parsed;
  }
  return hash_text(text);
}

bool compose_game_version(std::uint16_t major, std::uint16_t minor,
                          std::uint16_t build, std::uint16_t revision,
                          std::uint32_t& output) noexcept {
  const std::uint16_t parts[] = {major, minor, build, revision};
  std::uint64_t value = 0;
  for (const auto part : parts) {
    std::uint32_t scale = 10;
    for (std::uint16_t n = part; n >= 10; n = static_cast<std::uint16_t>(n / 10))
      scale *= 10;
    value = value * scale + part;
    if (value > std::numeric_limits<std::uint32_t>::max()) return false;
  }
  output = static_cast<std::uint32_t>(value);
  return true;
}

GameBuild classify_game_build(std::uint32_t version) noexcept {
  switch (version) {
    case 1000: return GameBuild::v1000;
    case 1004: return GameBuild::v1004;
    case 1040: return GameBuild::v1040;
    case 1060: return GameBuild::v1060;
    case 1061: return GameBuild::v1061;
    case 1070: return GameBuild::v1070;
    case 1080: return GameBuild::v1080;
    case 1100: return GameBuild::v1100;
    case 1110: return GameBuild::v1110;
    case 1120: return GameBuild::v1120;
    case 1130: return GameBuild::v1130;
    case 12043: return GameBuild::v12043;
    default: return GameBuild::unknown;
  }
}

const char* game_build_name(GameBuild build) noexcept {
  switch (build) {
    case GameBuild::v1000: return "1000";
    case GameBuild::v1004: return "1004";
    case GameBuild::v1040: return "1040";
    case GameBuild::v1060: return "1060";
    case GameBuild::v1061: return "1061";
    case GameBuild::v1070: return "1070";
    case GameBuild::v1080: return "1080";
    case GameBuild::v1100: return "1100";
    case GameBuild::v1110: return "1110";
    case GameBuild::v1120: return "1120";
    case GameBuild::v1130: return "1130";
    case GameBuild::v12043: return "12043";
    default: return "unknown";
  }
}

std::uint32_t select_game_process_id(const ProcessEntry* entries,
                                     std::size_t count, std::string_view name) noexcept {
  if(!entries)return 0;
  for(std::size_t i=0;i<count;++i)
    if(entries[i].executable.size()==name.size() &&
       std::memcmp(entries[i].executable.data(),name.data(),name.size())==0)
      return entries[i].process_id;
  return 0;
}
std::uint32_t find_game_process_id() noexcept {
#if defined(_WIN32)
  // UNICODE builds map the unsuffixed names to the W variants, matching the
  // original ANSI-name call site's case-sensitive full-name comparison.
  PROCESSENTRY32W entry{};
  entry.dwSize=sizeof(entry);
  const HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
  if(snapshot==INVALID_HANDLE_VALUE)return 0;
  std::uint32_t found=0;
  if(Process32FirstW(snapshot,&entry)) {
    do {
      if(std::wcscmp(entry.szExeFile,L"PlayGTAIV.exe")==0) {
        found=entry.th32ProcessID;break;
      }
    } while(Process32NextW(snapshot,&entry));
  }
  CloseHandle(snapshot);
  return found;
#else
  return 0;
#endif
}
bool compatibility_module_lacks_signature(const std::byte* image,
                                          std::size_t image_size) noexcept {
  if(!image)return false;
  // Disassembly (0x10003e2f / 0x10003e3b) shows BOTH comparisons are 32-bit:
  //   cmp DWORD PTR [eax+0x20d3],0xC7C47589
  //   cmp DWORD PTR [eax+0x20d7],0x00002446
  constexpr std::size_t kFirst=0x20d3, kSecond=0x20d7;
  if(image_size<kSecond+4)return false;
  const auto read_dword=[&](std::size_t offset) noexcept {
    std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)
      value|=static_cast<std::uint32_t>(static_cast<unsigned>(image[offset+i]))<<(8u*i);
    return value;
  };
  return read_dword(kFirst)!=0xc7c47589u || read_dword(kSecond)!=0x00002446u;
}
bool query_current_game_version(std::uint32_t& compact_version,
                                GameBuild& build) noexcept {
  compact_version = 0;
  build = GameBuild::unknown;
#if defined(_WIN32)
  char executable[MAX_PATH]{};
  const DWORD path_length = GetModuleFileNameA(nullptr, executable, MAX_PATH);
  if (path_length == 0 || path_length >= MAX_PATH) return false;

  DWORD ignored = 0;
  const DWORD info_size = GetFileVersionInfoSizeA(executable, &ignored);
  if (info_size == 0) return false;
  // Allocate bytes directly instead of reproducing the original CRT allocation.
  auto* info = new (std::nothrow) std::byte[info_size];
  if (!info) return false;
  const BOOL loaded = GetFileVersionInfoA(executable, ignored, info_size, info);
  VS_FIXEDFILEINFO* fixed = nullptr;
  UINT fixed_size = 0;
  const BOOL queried = loaded && VerQueryValueA(info, "\\",
      reinterpret_cast<void**>(&fixed), &fixed_size);
  bool result = false;
  if (queried && fixed && fixed_size >= sizeof(VS_FIXEDFILEINFO) &&
      fixed->dwSignature == 0xfeef04bdu) {
    const auto major = static_cast<std::uint16_t>(fixed->dwProductVersionMS >> 16u);
    const auto minor = static_cast<std::uint16_t>(fixed->dwProductVersionMS);
    const auto patch = static_cast<std::uint16_t>(fixed->dwProductVersionLS >> 16u);
    const auto revision = static_cast<std::uint16_t>(fixed->dwProductVersionLS);
    result = compose_game_version(major, minor, patch, revision, compact_version);
    if (result) build = classify_game_build(compact_version);
  }
  delete[] info;
  return result;
#else
  return false;
#endif
}
}  // namespace zpatch
