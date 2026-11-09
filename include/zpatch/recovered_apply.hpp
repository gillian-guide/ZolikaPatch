#pragma once
#include <cstddef>
#include <string_view>
#include <vector>
#include "zpatch/patch.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/recovered_ops.hpp"
#include "zpatch/game.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

struct RecoveredApplyOutcome final {
  std::string_view option;
  Status status;
  std::byte* address{};
  std::size_t bytes_written{};
};

// Applies the recovered literal operations for the enabled options. Each
// operation locates its signature, writes `replacement` at signature+offset
// through the protection path, and reports the outcome. Operations for
// disabled or unknown options are skipped. This mirrors the observed handler
// behaviour (find signature, protect, write) without inventing expected bytes:
// the original does not verify site contents, so neither does this.
Status apply_recovered_operations(ModuleView module, std::string_view enabled_options,
                                  ProtectFn protect,
                                  std::vector<RecoveredApplyOutcome>& output) noexcept;

// As above, but also enforces the recovered per-option build gate (if any).
// Operations whose option is gated out for `build` are skipped.
Status apply_recovered_operations_for_build(ModuleView module, GameBuild build,
                                            std::string_view enabled_options,
                                            ProtectFn protect,
                                            std::vector<RecoveredApplyOutcome>& output) noexcept;

}  // namespace zpatch
