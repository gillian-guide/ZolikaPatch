// SkipLauncher patch application. See skip_launcher.hpp and
// evidence/skip_launcher_ops.json for provenance. The feature is
// XLivelessAddon's launcher/DRM bypass as re-implemented by ZolikaPatch 7.65:
// the main signature table, two multi-hit collectors, the DFA file-API hook
// installs (0x10004850) and the ROM message-window patches (0x10004620).
#include "zpatch/skip_launcher.hpp"

#include <cstring>
#include <vector>

#include "zpatch/pattern.hpp"

namespace zpatch {
namespace {

#include "recovered_skip_launcher.inc"

constexpr std::size_t kLogChunk = sizeof(AppliedWrite::original);
constexpr std::uint32_t kPageExecuteReadWrite = 0x40;

bool in_bounds(MappedImage image, const std::byte* site, std::size_t length) noexcept {
  return site >= image.base && site + length <= image.base + image.size;
}

// Writes `length` bytes at `site` through the protection callback and records
// the preimage in the log (in 64-byte chunks, the log's entry size).
Status write_at(MappedImage image, ProtectFn protect, AppliedWriteLog* log,
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

std::uint32_t read_u32(const std::byte* site) noexcept {
  std::uint32_t value = 0;
  std::memcpy(&value, site, sizeof(value));
  return value;
}

}  // namespace

const SkipLauncherEntry* skip_launcher_entries(std::size_t& count) noexcept {
  count = sizeof(kSkipLauncherEntries) / sizeof(kSkipLauncherEntries[0]);
  return kSkipLauncherEntries;
}

const SkipLauncherCollector* skip_launcher_collectors(std::size_t& count) noexcept {
  count = sizeof(kSkipLauncherCollectors) / sizeof(kSkipLauncherCollectors[0]);
  return kSkipLauncherCollectors;
}

const SkipLauncherInstall* skip_launcher_dfa_installs(std::size_t& count) noexcept {
  count = sizeof(kSkipLauncherDfaInstalls) / sizeof(kSkipLauncherDfaInstalls[0]);
  return kSkipLauncherDfaInstalls;
}

const SkipLauncherInstall* skip_launcher_rom_installs(std::size_t& count) noexcept {
  count = sizeof(kSkipLauncherRomInstalls) / sizeof(kSkipLauncherRomInstalls[0]);
  return kSkipLauncherRomInstalls;
}

const SkipLauncherCollectedInstall* skip_launcher_dfa_collected(std::size_t& count) noexcept {
  count = sizeof(kSkipLauncherDfaCollected) / sizeof(kSkipLauncherDfaCollected[0]);
  return kSkipLauncherDfaCollected;
}

Status apply_skip_launcher(MappedImage image, ProtectFn protect, AppliedWriteLog* log,
                           SkipHookResolver resolve, void* resolver_user,
                           SkipLauncherReport& report,
                           SkipLauncherStage stages) noexcept {
  if (!image.base || image.size == 0 || !protect)
    return {StatusCode::invalid_input, "invalid skip launcher input"};

  const ModuleView view{image.base, image.size};
  std::size_t entry_count = 0;
  const SkipLauncherEntry* entries = skip_launcher_entries(entry_count);
  std::vector<const std::byte*> matches(entry_count, nullptr);

  if (has_stage(stages, SkipLauncherStage::main)) {
    for (std::size_t i = 0; i < entry_count; ++i) {
      Match match;
      if (!find_pattern(view, entries[i].signature, match)) {
        ++report.entries_missing;
        continue;
      }
      matches[i] = match.address;
      ++report.entries_found;
      // The original's jump-installing entry requires its target entry's match
      // (the dce0 scan); when absent it skips the whole block, not just the
      // jump (33 C0 + E9 + rel32 are all inside the same `[ebp-0x1c]` guard).
      bool target_absent = false;
      for (std::size_t w = 0; w < entries[i].write_count; ++w) {
        const std::uint32_t target_entry = entries[i].writes[w].jump_to_entry;
        if (target_entry != kNoJumpTarget &&
            (target_entry >= matches.size() || matches[target_entry] == nullptr)) {
          target_absent = true;
          break;
        }
      }
      if (target_absent) continue;
      for (std::size_t w = 0; w < entries[i].write_count; ++w) {
        const SkipLauncherWrite& write = entries[i].writes[w];
        auto* site = const_cast<std::byte*>(matches[i]) + write.offset;
        if (write.jump_to_entry != kNoJumpTarget) {
          const std::byte* target = write.jump_to_entry < matches.size()
                                        ? matches[write.jump_to_entry]
                                        : nullptr;
          if (!target) continue;  // original skips when the target entry was absent
          // The E9 opcode sits one byte before the recorded rel32 site:
          // displacement = target - (site + 4).
          const auto displacement = static_cast<std::int32_t>(
              reinterpret_cast<std::uintptr_t>(target) -
              (reinterpret_cast<std::uintptr_t>(site) + 4u));
          const std::byte bytes[4] = {
              static_cast<std::byte>(displacement & 0xFF),
              static_cast<std::byte>((displacement >> 8) & 0xFF),
              static_cast<std::byte>((displacement >> 16) & 0xFF),
              static_cast<std::byte>((displacement >> 24) & 0xFF),
          };
          const auto status = write_at(image, protect, log, "SkipLauncher", site, bytes, 4);
          if (!status) return status;
          report.jump_installed = true;
          ++report.writes;
          continue;
        }
        std::vector<std::byte> filled;
        const std::byte* bytes = nullptr;
        if (write.bytes) {
          bytes = reinterpret_cast<const std::byte*>(write.bytes);
        } else {
          filled.assign(write.length, static_cast<std::byte>(write.fill));
          bytes = filled.data();
        }
        const auto status = write_at(image, protect, log, "SkipLauncher", site, bytes, write.length);
        if (!status) return status;
        ++report.writes;
      }
    }

    std::size_t collector_count = 0;
    const SkipLauncherCollector* collectors = skip_launcher_collectors(collector_count);
    for (std::size_t i = 0; i < collector_count; ++i) {
      const SkipLauncherCollector& collector = collectors[i];
      std::vector<const std::byte*> hits;
      find_all_patterns(view, collector.pattern, hits);
      if (hits.empty()) continue;
      ++report.collectors_found;
      const auto* bytes = reinterpret_cast<const std::byte*>(collector.bytes);
      std::vector<const std::byte*> targets;
      if (collector.target == SkipLauncherCollector::Target::first_two) {
        if (hits.size() > 0) targets.push_back(hits[0]);
        if (hits.size() > 1) targets.push_back(hits[1]);
      } else {
        if (hits.size() > 1) targets.push_back(hits[hits.size() - 2]);
        if (hits.size() > 0) targets.push_back(hits[hits.size() - 1]);
      }
      for (const std::byte* target : targets) {
        auto* site = const_cast<std::byte*>(target);
        const auto status = write_at(image, protect, log, "SkipLauncher", site, bytes,
                                     collector.length);
        if (!status) return status;
        ++report.collector_writes;
      }
    }
  }

  if (has_stage(stages, SkipLauncherStage::dfa)) {
    // DFA captures: remember the CreateFile IAT slot the hook forwards through.
    Match capture;
    if (find_pattern(view, kSkipLauncherDfaCapture, capture))
      report.create_file_slot = read_u32(capture.address + kSkipLauncherDfaCaptureOffset);
  }

  if (resolve) {
    // DFA + ROM hook installs: opcode + rel32 to the reimplemented body.
    const auto install_hooks = [&](const SkipLauncherInstall* installs, std::size_t count,
                                   std::size_t& applied, std::size_t& missing) -> Status {
      for (std::size_t i = 0; i < count; ++i) {
        const SkipLauncherInstall& install = installs[i];
        const std::uintptr_t destination = resolve(install.hook, resolver_user);
        if (!destination) {
          ++missing;
          continue;
        }
        Match match;
        if (!find_pattern(view, install.signature, match)) {
          ++missing;
          continue;
        }
        auto* site = const_cast<std::byte*>(match.address) + install.offset;
        const auto displacement = static_cast<std::int32_t>(
            destination - (reinterpret_cast<std::uintptr_t>(site) + 5u));
        std::byte bytes[5];
        bytes[0] = static_cast<std::byte>(install.opcode);
        std::memcpy(bytes + 1, &displacement, sizeof(displacement));
        const auto status = write_at(image, protect, log, "SkipLauncher", site, bytes, 5);
        if (!status) return status;
        ++applied;
      }
      return {};
    };

    if (has_stage(stages, SkipLauncherStage::dfa)) {
      std::size_t dfa_count = 0;
      const SkipLauncherInstall* dfa = skip_launcher_dfa_installs(dfa_count);
      const auto dfa_status = install_hooks(dfa, dfa_count, report.dfa_installs, report.dfa_missing);
      if (!dfa_status) return dfa_status;

      // DFA collected installs: jumps from collected match sites to other
      // patterns' matches (the multi-hit collector tail of 0x10004850).
      std::size_t collected_count = 0;
      const SkipLauncherCollectedInstall* collected =
          skip_launcher_dfa_collected(collected_count);
      for (std::size_t i = 0; i < collected_count; ++i) {
        std::vector<const std::byte*> hits;
        find_all_patterns(view, collected[i].collector, hits);
        if (hits.empty()) continue;
        for (std::size_t t = 0; t < collected[i].target_count; ++t) {
          const SkipLauncherCollectedTarget& target = collected[i].targets[t];
          if (target.index >= hits.size()) continue;
          Match target_match;
          if (!find_pattern(view, target.target_signature, target_match)) continue;
          auto* site = const_cast<std::byte*>(hits[target.index]);
          const auto displacement = static_cast<std::int32_t>(
              reinterpret_cast<std::uintptr_t>(target_match.address) -
              (reinterpret_cast<std::uintptr_t>(site) + 5u));
          std::byte bytes[5];
          bytes[0] = std::byte{0xE9};
          std::memcpy(bytes + 1, &displacement, sizeof(displacement));
          const auto status = write_at(image, protect, log, "SkipLauncher", site, bytes, 5);
          if (!status) return status;
          ++report.collected_installs;
        }
      }
    }

    if (has_stage(stages, SkipLauncherStage::rom)) {
      std::size_t rom_count = 0;
      const SkipLauncherInstall* rom = skip_launcher_rom_installs(rom_count);
      const auto rom_status = install_hooks(rom, rom_count, report.rom_installs, report.rom_missing);
      if (!rom_status) return rom_status;
    }
  }

  return {};
}

}  // namespace zpatch
