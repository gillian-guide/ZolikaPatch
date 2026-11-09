// Installer side of the reimplemented hook bodies. See hook_install.hpp.
#include "zpatch/hook_install.hpp"

#include <cstring>

#include "zpatch/hook_bodies.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/recovered_build_gates.hpp"
#include "zpatch/recovered_match_jumps.hpp"
#include "zpatch/recovered_rva_jumps.hpp"
#include "zpatch/recovered_sig_jumps.hpp"

namespace zpatch {
namespace {

const RecoveredHookTarget kRecoveredHookTargets[] = {
#include "recovered_hook_targets.inc"
};

constexpr std::size_t kJumpLength = 5;  // E9/E8 + rel32
constexpr std::uint32_t kPageExecuteReadWrite = 0x40;  // PAGE_EXECUTE_READWRITE

bool in_bounds(MappedImage image, const std::byte* site, std::size_t length) noexcept {
  return site >= image.base && site + length <= image.base + image.size;
}

// Writes opcode + rel32 at `site`, with the displacement computed from the
// site's virtual address exactly as the original handler does. The 5-byte range
// is protected directly (the recovered 1/2/4-byte protected-write helpers do
// not cover jump installs).
Status write_jump(MappedImage image, std::uintptr_t game_module_base, std::byte* site,
                  std::uint8_t opcode, std::uintptr_t destination, ProtectFn protect) noexcept {
  if (!protect) return {StatusCode::invalid_input, "no protection callback"};
  const std::uintptr_t site_va =
      game_module_base + static_cast<std::uintptr_t>(site - image.base);
  const auto displacement =
      static_cast<std::int32_t>(destination - site_va - kJumpLength);
  std::byte patch[kJumpLength];
  patch[0] = static_cast<std::byte>(opcode);
  std::memcpy(patch + 1, &displacement, sizeof(displacement));
  std::uint32_t previous = 0;
  const auto status = protect(site, kJumpLength, kPageExecuteReadWrite, &previous);
  if (!status) return status;
  std::memcpy(site, patch, kJumpLength);
  // Restore is best-effort; the store already happened.
  (void)protect(site, kJumpLength, previous, &previous);
  return {};
}

struct Resolution final {
  std::uintptr_t destination{};
  bool gated{};
};

Resolution resolve_for(GameBuild build, std::string_view option, std::uint32_t reference_va,
                       HookDestinationResolver resolve, void* user) noexcept {
  Resolution result;
  bool known = false;
  if (!build_allows_option(build, option, known)) {
    result.gated = true;
    return result;
  }
  result.destination = resolve ? resolve(reference_va, user) : 0;
  return result;
}

void record(HookInstallReport& report, const HookInstallOutcome& outcome) noexcept {
  report.outcomes.push_back(outcome);
  switch (outcome.status) {
    case HookInstallStatus::installed: report.installed += 1; break;
    case HookInstallStatus::not_found: report.not_found += 1; break;
    case HookInstallStatus::gated_out: report.gated_out += 1; break;
    case HookInstallStatus::no_destination: report.no_destination += 1; break;
    case HookInstallStatus::write_failed: report.write_failed += 1; break;
  }
}

}  // namespace

const RecoveredHookTarget* recovered_hook_targets(std::size_t& count) noexcept {
  count = sizeof(kRecoveredHookTargets) / sizeof(kRecoveredHookTargets[0]);
  return kRecoveredHookTargets;
}

std::uintptr_t resolve_recovered_hook_target(std::uint32_t reference_va) noexcept {
  std::size_t count = 0;
  const RecoveredHookTarget* targets = recovered_hook_targets(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (targets[i].reference_va == reference_va) {
      return targets[i].address ? targets[i].address() : 0;
    }
  }
  return 0;
}

std::uintptr_t default_hook_resolver(std::uint32_t reference_va, void* /*user*/) noexcept {
  return resolve_recovered_hook_target(reference_va);
}

Status install_recovered_sig_jumps(GameBuild build, MappedImage image,
                                   std::uintptr_t game_module_base, ProtectFn protect,
                                   HookDestinationResolver resolve, void* resolver_user,
                                   HookInstallReport& report) noexcept {
  std::size_t count = 0;
  const RecoveredSigJump* jumps = recovered_sig_jumps(count);
  for (std::size_t i = 0; i < count; ++i) {
    const RecoveredSigJump& jump = jumps[i];
    report.candidates += 1;
    HookInstallOutcome outcome;
    outcome.option = jump.option;
    outcome.reference_va = jump.destination;

    const Resolution resolution = resolve_for(build, jump.option, jump.destination, resolve, resolver_user);
    if (resolution.gated) {
      outcome.status = HookInstallStatus::gated_out;
      record(report, outcome);
      continue;
    }
    outcome.resolved_va = resolution.destination;
    if (resolution.destination == 0) {
      outcome.status = HookInstallStatus::no_destination;
      record(report, outcome);
      continue;
    }

    Match match;
    if (!find_pattern(ModuleView{image.base, image.size}, jump.signature, match)) {
      outcome.status = HookInstallStatus::not_found;
      record(report, outcome);
      continue;
    }
    auto* site = const_cast<std::byte*>(match.address) + jump.offset;
    if (!in_bounds(image, site, kJumpLength)) {
      outcome.status = HookInstallStatus::not_found;
      record(report, outcome);
      continue;
    }
    if (!write_jump(image, game_module_base, site, 0xE9, resolution.destination, protect)) {
      outcome.status = HookInstallStatus::write_failed;
      record(report, outcome);
      continue;
    }
    outcome.site = site;
    outcome.status = HookInstallStatus::installed;
    record(report, outcome);
  }
  return {};
}

Status install_recovered_rva_jumps(GameBuild build, MappedImage image,
                                   std::uintptr_t game_module_base, ProtectFn protect,
                                   HookDestinationResolver resolve, void* resolver_user,
                                   HookInstallReport& report) noexcept {
  std::size_t count = 0;
  const RecoveredRvaJump* jumps = recovered_rva_jumps(count);
  for (std::size_t i = 0; i < count; ++i) {
    const RecoveredRvaJump& jump = jumps[i];
    report.candidates += 1;
    HookInstallOutcome outcome;
    outcome.option = jump.option;
    outcome.reference_va = jump.destination;

    const Resolution resolution = resolve_for(build, jump.option, jump.destination, resolve, resolver_user);
    if (resolution.gated) {
      outcome.status = HookInstallStatus::gated_out;
      record(report, outcome);
      continue;
    }
    outcome.resolved_va = resolution.destination;
    if (resolution.destination == 0) {
      outcome.status = HookInstallStatus::no_destination;
      record(report, outcome);
      continue;
    }

    auto* site = image.base + jump.module_rva;
    if (!in_bounds(image, site, kJumpLength)) {
      outcome.status = HookInstallStatus::not_found;
      record(report, outcome);
      continue;
    }
    if (!write_jump(image, game_module_base, site, 0xE9, resolution.destination, protect)) {
      outcome.status = HookInstallStatus::write_failed;
      record(report, outcome);
      continue;
    }
    outcome.site = site;
    outcome.status = HookInstallStatus::installed;
    record(report, outcome);
  }
  return {};
}

Status install_recovered_match_jumps(GameBuild build, MappedImage image,
                                     std::uintptr_t game_module_base, ProtectFn protect,
                                     HookDestinationResolver resolve, void* resolver_user,
                                     HookInstallReport& report) noexcept {
  std::size_t count = 0;
  const RecoveredMatchJump* jumps = recovered_match_jumps(count);
  std::string_view scanned_signature;
  std::vector<const std::byte*> matches;
  for (std::size_t i = 0; i < count; ++i) {
    const RecoveredMatchJump& jump = jumps[i];
    report.candidates += 1;
    HookInstallOutcome outcome;
    outcome.option = jump.option;
    outcome.reference_va = jump.destination;

    const Resolution resolution = resolve_for(build, jump.option, jump.destination, resolve, resolver_user);
    if (resolution.gated) {
      outcome.status = HookInstallStatus::gated_out;
      record(report, outcome);
      continue;
    }
    outcome.resolved_va = resolution.destination;
    if (resolution.destination == 0) {
      outcome.status = HookInstallStatus::no_destination;
      record(report, outcome);
      continue;
    }

    if (jump.signature != scanned_signature) {
      matches.clear();
      find_all_patterns(ModuleView{image.base, image.size}, jump.signature, matches);
      scanned_signature = jump.signature;
    }
    if (jump.match_index >= matches.size()) {
      outcome.status = HookInstallStatus::not_found;
      record(report, outcome);
      continue;
    }
    auto* site = const_cast<std::byte*>(matches[jump.match_index]) + jump.offset;
    if (!in_bounds(image, site, kJumpLength)) {
      outcome.status = HookInstallStatus::not_found;
      record(report, outcome);
      continue;
    }
    const auto opcode = static_cast<std::uint8_t>(jump.opcode & 0xFFu);
    if (!write_jump(image, game_module_base, site, opcode, resolution.destination, protect)) {
      outcome.status = HookInstallStatus::write_failed;
      record(report, outcome);
      continue;
    }
    outcome.site = site;
    outcome.status = HookInstallStatus::installed;
    record(report, outcome);
  }
  return {};
}

Status install_recovered_hooks(GameBuild build, MappedImage image,
                               std::uintptr_t game_module_base, ProtectFn protect,
                               HookDestinationResolver resolve, void* resolver_user,
                               HookInstallReport& report) noexcept {
  Status status = install_recovered_sig_jumps(build, image, game_module_base, protect, resolve,
                                              resolver_user, report);
  if (!status) return status;
  status = install_recovered_rva_jumps(build, image, game_module_base, protect, resolve,
                                       resolver_user, report);
  if (!status) return status;
  return install_recovered_match_jumps(build, image, game_module_base, protect, resolve,
                                       resolver_user, report);
}

}  // namespace zpatch
