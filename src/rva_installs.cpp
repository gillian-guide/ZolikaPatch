// Recovered per-build module-rva installs. See rva_installs.hpp.
#include "zpatch/rva_installs.hpp"

#include <cstring>
#include <vector>

#include "zpatch/log.hpp"
#include "zpatch/rebased_values.hpp"
#include "zpatch/op_corrections.hpp"

namespace zpatch {
namespace {

#include "recovered_rva_installs.inc"

constexpr std::size_t kLogChunk = sizeof(AppliedWrite::original);
constexpr std::uint32_t kPageExecuteReadWrite = 0x40;

// A row with build 0 is not build-specific and applies everywhere.
bool install_applies(const RvaInstall& install, std::uint8_t build) noexcept {
  return install.build == 0 || install.build == build;
}

bool in_bounds(MappedImage image, const std::byte* site, std::size_t length) noexcept {
  return site >= image.base && site + length <= image.base + image.size;
}

Status write_bytes(MappedImage image, ProtectFn protect, AppliedWriteLog* log,
                   std::string_view option, std::byte* site, const std::byte* bytes,
                   std::size_t length) noexcept {
  if (!in_bounds(image, site, length)) return {StatusCode::not_reconstructed, "site out of bounds"};
  if (log) {
    for (std::size_t done = 0; done < length; done += kLogChunk) {
      const std::size_t chunk = (length - done) < kLogChunk ? (length - done) : kLogChunk;
      AppliedWrite entry;
      entry.option = option;
      entry.address = site + done;
      entry.length = chunk;
      std::memcpy(entry.original, site + done, chunk);
      log->push_back(entry);
    }
  }
  std::uint32_t previous = 0;
  const auto status = protect(site, length, kPageExecuteReadWrite, &previous);
  if (!status) return status;
  std::memcpy(site, bytes, length);
  (void)protect(site, length, previous, &previous);
  return {};
}

// Applies one rva install (jump, fill or slot publish).
Status apply_one_rva_install(const RvaInstall& install, MappedImage image,
                             std::uintptr_t game_module_base, ProtectFn protect,
                             AppliedWriteLog* log, RuntimeDestinationResolver resolve,
                             void* resolver_user, RvaInstallReport& report) noexcept {
  if (install.kind == 3u) {
    // Slot publish: our global <- game address.
    const std::uintptr_t slot = rebased_slot_for(install.slot_name);
    if (!slot) {
      ++report.skipped_no_slot;
      log_printf("skip phase=rva-installs option=%.*s reason=no-rebased-global slot=%s",
                 static_cast<int>(install.option.size()), install.option.data(),
                 install.slot_name.data());
      return {};
    }
    *reinterpret_cast<std::uint32_t*>(slot) =
        static_cast<std::uint32_t>(game_module_base + install.operand);
    ++report.slots;
    log_printf("publish phase=rva-installs option=%.*s slot=%s value=%08X source=game-address",
               static_cast<int>(install.option.size()), install.option.data(),
               install.slot_name.data(), game_module_base + install.operand);
    return {};
  }
  if (install.kind == 4u) {
    // Constant slot: our global <- the handler's stored constant (a game RVA
    // the routine adds to the module base at use time).
    const std::uintptr_t slot = rebased_slot_for(install.slot_name);
    if (!slot) {
      ++report.skipped_no_slot;
      log_printf("skip phase=rva-installs option=%.*s reason=no-rebased-global slot=%s",
                 static_cast<int>(install.option.size()), install.option.data(),
                 install.slot_name.data());
      return {};
    }
    *reinterpret_cast<std::uint32_t*>(slot) = install.operand;
    ++report.slots;
    log_printf("publish phase=rva-installs option=%.*s slot=%s value=%08X source=constant",
               static_cast<int>(install.option.size()), install.option.data(),
               install.slot_name.data(), install.operand);
    return {};
  }
  if (install.kind == 5u) {
    // rebased_ptr: the ADDRESS of our global is stored over a 4-byte game
    // operand slot at site_rva (EpisodicVehicleSupport re-points two
    // `cmp [episode_global],2` operands at its own int-2 flag).
    const std::uintptr_t value = rebased_symbol_for(install.slot_name);
    if (!value) {
      ++report.skipped_no_slot;
      log_printf("skip phase=rva-installs option=%.*s reason=no-rebased-global slot=%s",
                 static_cast<int>(install.option.size()), install.option.data(),
                 install.slot_name.data());
      return {};
    }
    if (install.site_rva + 4u > image.size) return {};
    std::byte vbytes[4];
    std::uint32_t v32 = static_cast<std::uint32_t>(value);
    std::memcpy(vbytes, &v32, sizeof(v32));
    const auto status = write_bytes(image, protect, log, install.option,
                                    image.base + install.site_rva, vbytes, 4);
    if (!status) return status;
    ++report.slots;
    log_printf("apply phase=rva-installs option=%.*s rva=%08X kind=rebased-ptr",
               static_cast<int>(install.option.size()), install.option.data(),
               install.site_rva);
    return {};
  }
  if (install.site_rva + 16u > image.size) {
    log_printf("skip phase=rva-installs option=%.*s reason=site-outside-image rva=%08X",
               static_cast<int>(install.option.size()), install.option.data(),
               install.site_rva);
    return {};
  }
  std::byte* site = image.base + install.site_rva;

  if (install.kind == 2u) {
    std::vector<std::byte> fill(install.operand, static_cast<std::byte>(install.opcode));
    const auto status =
        write_bytes(image, protect, log, install.option, site, fill.data(), fill.size());
    if (!status) return status;
    ++report.fills;
    log_printf("apply phase=rva-installs option=%.*s rva=%08X kind=fill len=%u",
               static_cast<int>(install.option.size()), install.option.data(),
               install.site_rva, install.operand);
    return {};
  }

  const std::uintptr_t destination =
      install.kind == 0u ? game_module_base + install.operand
                         : (resolve ? resolve(install.operand, resolver_user) : 0);
  if (!destination) {
    ++report.skipped_no_destination;
    log_printf("skip phase=rva-installs option=%.*s reason=no-destination rva=%08X dest=%08X",
               static_cast<int>(install.option.size()), install.option.data(),
               install.site_rva, install.operand);
    return {};
  }
  const auto origin = game_module_base + static_cast<std::uintptr_t>(install.site_rva);
  const auto displacement = static_cast<std::int32_t>(destination - (origin + 5u));
  std::byte bytes[5];
  bytes[0] = static_cast<std::byte>(install.opcode);
  std::memcpy(bytes + 1, &displacement, sizeof(displacement));
  const auto status = write_bytes(image, protect, log, install.option, site, bytes, 5);
  if (!status) return status;
  ++report.jumps;
  log_printf("apply phase=rva-installs option=%.*s rva=%08X kind=jump dest=%08X",
             static_cast<int>(install.option.size()), install.option.data(),
             install.site_rva, destination);
  return {};
}

}  // namespace

const RvaInstall* recovered_rva_installs(std::size_t& count) noexcept {
  count = sizeof(kRvaInstalls) / sizeof(kRvaInstalls[0]);
  return kRvaInstalls;
}

bool rva_install_covers(std::uint32_t site_rva, std::uint8_t build) noexcept {
  for (const RvaInstall& install : kRvaInstalls) {
    if (!install_applies(install, build)) continue;
    if (install.kind != 3u && install.kind != 5u && install.site_rva == site_rva) return true;
  }
  return false;
}

Status apply_recovered_rva_installs(const ConfigDocument& config, MappedImage image,
                                    std::uintptr_t game_module_base, ProtectFn protect,
                                    AppliedWriteLog* log, RuntimeDestinationResolver resolve,
                                    void* resolver_user, std::uint8_t build,
                                    RvaInstallReport& report) noexcept {
  if (!image.base || image.size == 0 || !protect)
    return {StatusCode::invalid_input, "invalid rva install input"};

  constexpr std::size_t kCount = sizeof(kRvaInstalls) / sizeof(kRvaInstalls[0]);
  // A feature's installs form one patch set: a skip-jump without its companion
  // call (whose body may be missing) breaks the game, so sets are
  // all-or-nothing per option and build. A disabled option's set is never
  // applied, and rows for other builds never participate.
  for (std::size_t i = 0; i < kCount; ++i) {
    const RvaInstall& install = kRvaInstalls[i];
    if (!install_applies(install, build)) continue;
    bool seen = false;
    for (std::size_t j = 0; j < i; ++j) {
      if (!install_applies(kRvaInstalls[j], build)) continue;
      if (kRvaInstalls[j].option == install.option) {
        seen = true;
        break;
      }
    }
    if (seen) continue;
    if (!option_enabled(config, install.option)) {
      for (std::size_t j = i; j < kCount; ++j) {
        if (!install_applies(kRvaInstalls[j], build)) continue;
        if (kRvaInstalls[j].option == install.option) {
          ++report.gated_out;
          log_printf("skip phase=rva-installs option=%.*s reason=option-off rva=%08X kind=%u",
                     static_cast<int>(kRvaInstalls[j].option.size()),
                     kRvaInstalls[j].option.data(), kRvaInstalls[j].site_rva,
                     kRvaInstalls[j].kind);
        }
      }
      continue;
    }
    if (!op_anchor_ok(image, install.option)) {
      // The handler's first scan missed, so it never ran any of this option's
      // installs (finding N1).
      for (std::size_t j = i; j < kCount; ++j) {
        if (!install_applies(kRvaInstalls[j], build)) continue;
        if (kRvaInstalls[j].option == install.option) {
          ++report.anchor_missing;
          log_printf("skip phase=rva-installs option=%.*s reason=handler-anchor-absent "
                     "rva=%08X kind=%u",
                     static_cast<int>(kRvaInstalls[j].option.size()),
                     kRvaInstalls[j].option.data(), kRvaInstalls[j].site_rva,
                     kRvaInstalls[j].kind);
        }
      }
      continue;
    }
    bool complete = true;
    for (std::size_t j = i; j < kCount; ++j) {
      const RvaInstall& member = kRvaInstalls[j];
      if (!install_applies(member, build)) continue;
      if (member.option != install.option) continue;
      if (member.kind == 1u && (!resolve || !resolve(member.operand, resolver_user))) {
        complete = false;
        break;
      }
    }
    for (std::size_t j = i; j < kCount; ++j) {
      const RvaInstall& member = kRvaInstalls[j];
      if (!install_applies(member, build)) continue;
      if (member.option != install.option) continue;
      if (member.kind == 1u && !complete) {
        ++report.skipped_no_destination;
        log_printf("skip phase=rva-installs option=%.*s reason=companion-body-missing "
                   "rva=%08X",
                   static_cast<int>(member.option.size()), member.option.data(),
                   member.site_rva);
        continue;
      }
      const auto status = apply_one_rva_install(member, image, game_module_base, protect, log,
                                                resolve, resolver_user, report);
      if (!status) return status;
    }
  }
  return {};
}

}  // namespace zpatch
