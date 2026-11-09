#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include "zpatch/game.hpp"
#include "zpatch/patch.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

// One versioned patch site. `expected` must match the module bytes before the
// replacement is written; `options` lists INI option keys that gate the patch.
struct PatchSite final {
  std::string_view name;
  std::string_view signature;
  std::size_t signature_offset{};      // bytes before the write site within the match
  ByteView expected{};
  ByteView replacement{};
  std::uint32_t required_builds{};     // bit i == GameBuild ordinal i enabled
  std::string_view options;            // comma-separated option keys, empty = always
};

struct PatchManifestView final {
  const PatchSite* sites{};
  std::size_t size{};
};

struct PatchOutcome final {
  std::string_view name;
  Status status;
  std::byte* address{};          // resolved write site when status succeeded
  ByteView expected{};           // recorded for rollback
  ByteView replacement{};
};

// Resolves each site's signature inside the module, gates on build and options,
// validates expected bytes, then writes through the protection path. Successful
// outcomes record the resolved address so rollback does not need to rescan
// (the signature no longer exists after patching).
Status apply_patch_manifest(ModuleView module, GameBuild build,
                            const PatchManifestView& manifest,
                            std::string_view enabled_options,
                            ProtectFn protect,
                            std::vector<PatchOutcome>& output) noexcept;

// Rolls back previously applied outcomes in reverse order using their recorded
// addresses. Skips outcomes that did not apply and reports the rest.
Status rollback_patch_outcomes(GameBuild build, ProtectFn protect,
                               std::vector<PatchOutcome>& applied) noexcept;

}  // namespace zpatch
