#pragma once
// RemoveUselessChecks' table-driven NOP sites (finding N5).
//
// The original's loader (sub_100523C0, arm gated on `RemoveUselessChecks == 1`;
// the arm's default string is "0") calls sub_10044BD0, which walks per-build
// static RVA tables in the ASI's .rdata and writes five NOP bytes at
// game_base + entry for every entry (plus a few small patches the op/rva
// tables already carry). The phase-3 op extractor recovered only the non-loop
// writes, so the port used to leave ~2,200-2,500 table-driven sites unpatched
// on 1040/1070/1080. The rows here are recovered from the original's executed
// write log (tools/extract_useless_checks.py) and carry the original's build
// ordinal; the non-loop writes stay in their own tables (those sites are
// excluded here).

#include <cstddef>
#include <cstdint>

#include "zpatch/config.hpp"
#include "zpatch/game_image.hpp"
#include "zpatch/patch.hpp"
#include "zpatch/runtime.hpp"
#include "zpatch/status.hpp"

namespace zpatch {

struct UselessCheckSite final {
  std::uint8_t build;         // the original's build ordinal
  std::uint32_t site_rva;     // game module rva
  std::uint8_t length;        // 1, 4 or 5 (the original's dword + optional byte)
  std::uint8_t bytes[5];      // exact bytes the original's handler writes
};

const UselessCheckSite* recovered_useless_checks(std::size_t& count) noexcept;

struct UselessCheckReport final {
  std::size_t applied{};
  std::size_t gated_out{};      // the option is disabled in the INI
  std::size_t skipped_no_build{};  // rows for other builds
  std::size_t out_of_image{};   // rva outside the mapped image
};

// Applies the 5-byte NOPs for `build` when `RemoveUselessChecks` is enabled.
Status apply_recovered_useless_checks(const ConfigDocument& config, MappedImage image,
                                      std::uintptr_t game_module_base, ProtectFn protect,
                                      AppliedWriteLog* log, std::uint8_t build,
                                      UselessCheckReport& report) noexcept;

}  // namespace zpatch
