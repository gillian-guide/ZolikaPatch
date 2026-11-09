#pragma once

#include <cstdint>
#include <string_view>

namespace zpatch::evidence {

// Values below describe the inspected artifact; they are not runtime addresses.
inline constexpr std::string_view kBinary = "ZolikaPatch_extracted/ZolikaPatch.asi";
inline constexpr std::string_view kIni = "ZolikaPatch_extracted/ZolikaPatch.ini";
inline constexpr std::string_view kDecompilerDir = "ZolikaPatch_decompiled/ghidra";
inline constexpr std::uint32_t kPreferredImageBase = 0x10000000u;
inline constexpr std::uint32_t kPeEntryPoint = 0x1006075du;
inline constexpr std::uint32_t kConfigConsumer = 0x100523c0u;
inline constexpr std::uint32_t kPatternScanWrapper = 0x10003830u;
inline constexpr std::uint32_t kIniObjectConstructor = 0x10056130u;
inline constexpr std::uint32_t kIniSectionLookup = 0x10055f10u;
inline constexpr std::uint32_t kObservedOptionCount = 98u;
inline constexpr std::uint32_t kIndexedEntryCount = 1024u;

}  // namespace zpatch::evidence
