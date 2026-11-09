#include "zpatch/useless_checks.hpp"

#include <cstring>

#include "zpatch/log.hpp"

namespace zpatch {
namespace {

#include "recovered_useless_checks.inc"

}  // namespace

const UselessCheckSite* recovered_useless_checks(std::size_t& count) noexcept {
  count = sizeof(kUselessChecks) / sizeof(kUselessChecks[0]);
  return kUselessChecks;
}

Status apply_recovered_useless_checks(const ConfigDocument& config, MappedImage image,
                                      std::uintptr_t game_module_base, ProtectFn protect,
                                      AppliedWriteLog* log, std::uint8_t build,
                                      UselessCheckReport& report) noexcept {
  (void)game_module_base;  // rows are game rvas; the base is applied by the caller
  if (!image.base || image.size == 0 || !protect)
    return {StatusCode::invalid_input, "invalid useless-checks input"};
  if (!option_enabled(config, "RemoveUselessChecks")) {
    ++report.gated_out;
    log_printf("skip phase=useless-checks option=RemoveUselessChecks reason=option-off");
    return {};
  }
  std::size_t count = 0;
  const UselessCheckSite* sites = recovered_useless_checks(count);
  std::size_t applied = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const UselessCheckSite& site = sites[i];
    if (site.build != build) {
      ++report.skipped_no_build;
      continue;
    }
    if (static_cast<std::size_t>(site.site_rva) + site.length > image.size) {
      ++report.out_of_image;
      log_printf("skip phase=useless-checks option=RemoveUselessChecks "
                 "reason=site-outside-image rva=%08X", site.site_rva);
      continue;
    }
    std::byte* where = image.base + site.site_rva;
    AppliedWrite entry;
    entry.option = "RemoveUselessChecks";
    entry.address = where;
    entry.length = site.length;
    std::memcpy(entry.original, where, site.length);
    // Mirror the original's store shapes (a dword plus, for the 5-byte rows, a
    // trailing byte); the protected-write primitive accepts 1/2/4-byte stores.
    const std::size_t dword_len = site.length >= 4u ? 4u : site.length;
    auto status = protected_write(where, reinterpret_cast<const std::byte*>(site.bytes),
                                  dword_len, protect);
    if (status && site.length > 4u)
      status = protected_write(where + 4u,
                               reinterpret_cast<const std::byte*>(site.bytes) + 4u,
                               site.length - 4u, protect);
    if (!status) {
      log_printf("skip phase=useless-checks option=RemoveUselessChecks "
                 "reason=write-failed detail=%s", status.detail.data());
      continue;
    }
    if (log) log->push_back(entry);
    ++applied;
    ++report.applied;
    log_verbose("apply phase=useless-checks option=RemoveUselessChecks rva=%08X len=%u",
                site.site_rva, site.length);
  }
  log_printf("phase=useless-checks build=%u applied=%zu rows=%zu", build, applied, count);
  return {};
}

}  // namespace zpatch
