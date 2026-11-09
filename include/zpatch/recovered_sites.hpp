#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace zpatch {
// A patch site recovered from a ZolikaPatch option handler.
// signature: byte pattern (tokens per the observed strtoul matcher).
// target:    relative-jump destination VA written at the match start (0 if the
//            handler writes literals instead of a jump), for x86 image base
//            0x10000000 used by the original targets.
// targets:   number of distinct jump destinations observed (0 = not a jump site).
struct RecoveredPatchSite final {
  std::string_view option;
  std::string_view signature;
  std::uint32_t target;
  std::uint32_t targets;
};
const RecoveredPatchSite* recovered_patch_sites(std::size_t& count) noexcept;
}  // namespace zpatch
