// Curated op corrections. See op_corrections.hpp.
#include "zpatch/op_corrections.hpp"

#include "zpatch/log.hpp"
#include "zpatch/recovered_build_gates.hpp"
#include "zpatch/recovered_sig_jumps.hpp"

#include <cstring>
#include <string>

#include "zpatch/pattern.hpp"
#include "zpatch/rebased_values.hpp"

namespace zpatch {
namespace {

#include "recovered_op_corrections.inc"

constexpr std::size_t kLogChunk = sizeof(AppliedWrite::original);
constexpr std::uint32_t kPageExecuteReadWrite = 0x40;

std::string normalized(std::string_view values) {
  std::string out;
  out.reserve(values.size());
  for (char ch : values) {
    if (ch == ' ' || ch == '\t') continue;
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
  }
  return out;
}

std::vector<std::byte> parse_hex_bytes(std::string_view text) {
  std::vector<std::byte> bytes;
  std::size_t cursor = 0;
  while (cursor < text.size()) {
    const auto space = text.find(' ', cursor);
    const auto token = text.substr(
        cursor, space == std::string_view::npos ? std::string_view::npos : space - cursor);
    if (!token.empty()) {
      const int high = (token.size() > 1) ? (token[0] <= '9' ? token[0] - '0' : token[0] - 'A' + 10) : 0;
      const char low_char = (token.size() > 1) ? token[1] : token[0];
      const int low = low_char <= '9' ? low_char - '0' : low_char - 'A' + 10;
      bytes.push_back(static_cast<std::byte>((high << 4) | low));
    }
    if (space == std::string_view::npos) break;
    cursor = space + 1;
  }
  return bytes;
}

bool in_bounds(MappedImage image, const std::byte* site, std::size_t length) noexcept {
  return site >= image.base && site + length <= image.base + image.size;
}

// Per-pass anchor cache: the anchored options' rows repeat across a pass, and a
// full pattern scan per row would re-scan the image dozens of times.
struct AnchorCache final {
  std::vector<std::pair<std::string_view, bool>> entries;

  bool ok(MappedImage image, std::string_view option) noexcept {
    for (const auto& entry : entries) {
      if (entry.first == option) return entry.second;
    }
    const bool result = op_anchor_ok(image, option);
    entries.emplace_back(option, result);
    return result;
  }
};

Status write_bytes(MappedImage image, ProtectFn protect, AppliedWriteLog* log,
                   std::string_view option, std::byte* site, const std::byte* bytes,
                   std::size_t length) noexcept {
  // Fault-preserving (finding N4, 2026-10-16): the original handlers write their
  // sites without validating them and ignore the VirtualProtect result. The
  // rebuilt gate used to skip an out-of-image site and abort the pass on a
  // failed VirtualProtect - both fault-avoidance. In the shipped flow the sites
  // are in-image and VirtualProtect succeeds, so this changes nothing there; a
  // foreign/rebased site now writes (and faults) exactly like the original
  // (evidence: tools/phase3/p3_probe.cpp - the CrashMsgFix `*(match-4)` row
  // applies byte-identically when the image sits at its live base, and the old
  // no_destination skip only appeared on the harness's mismatched heap mapping).
  if (in_bounds(image, site, length)) {
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
  } else {
    log_printf("guard-removed phase=write file=%s line=%d site=%p outside-image "
               "fault=original unconditional write",
               __FILE__, __LINE__, static_cast<void*>(site));
  }
  std::uint32_t previous = 0;
  const auto status = protect(site, length, kPageExecuteReadWrite, &previous);
  if (!status) {
    log_printf("guard-removed phase=write site=%p protect-failed detail=%s "
               "fault=original ignores the VirtualProtect result",
               static_cast<void*>(site), status.detail.data());
  }
  std::memcpy(site, bytes, length);
  if (status) (void)protect(site, length, previous, &previous);
  return {};
}

}  // namespace

const OpSuppression* op_suppressions(std::size_t& count) noexcept {
  count = sizeof(kOpSuppressions) / sizeof(kOpSuppressions[0]);
  return kOpSuppressions;
}

const SigInstall* sig_installs(std::size_t& count) noexcept {
  count = sizeof(kSigInstalls) / sizeof(kSigInstalls[0]);
  return kSigInstalls;
}

const SlotInstall* slot_installs(std::size_t& count) noexcept {
  count = sizeof(kSlotInstalls) / sizeof(kSlotInstalls[0]);
  return kSlotInstalls;
}

const SigWrite* sig_writes(std::size_t& count) noexcept {
  count = sizeof(kSigWrites) / sizeof(kSigWrites[0]);
  return kSigWrites;
}

const OpAnchor* op_anchors(std::size_t& count) noexcept {
  count = sizeof(kOpAnchors) / sizeof(kOpAnchors[0]);
  return kOpAnchors;
}

bool op_anchor_ok(MappedImage image, std::string_view option) noexcept {
  if (!image.base || image.size == 0) return true;
  std::size_t count = 0;
  const OpAnchor* anchors = op_anchors(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (anchors[i].option != option) continue;
    Match match;
    return static_cast<bool>(find_pattern({image.base, image.size}, anchors[i].signature, match));
  }
  return true;
}

Status apply_sig_writes(const ConfigDocument& config, MappedImage image, GameBuild build,
                        ProtectFn protect, AppliedWriteLog* log, SigWriteReport& report) noexcept {
  if (!image.base || image.size == 0 || !protect)
    return {StatusCode::invalid_input, "invalid sig write input"};
  const ModuleView view{image.base, image.size};
  // The original's find-all handlers scan once and write every site relative to
  // the match vector; an earlier row's write can destroy a match (the fills sit
  // inside their own pattern), so reuse the first scan per signature for the
  // rest of this pass.
  struct MatchCacheEntry final {
    std::string_view signature;
    std::vector<const std::byte*> matches;
  };
  std::vector<MatchCacheEntry> match_cache;
  AnchorCache anchor_cache;
  for (const SigWrite& write : kSigWrites) {
    std::string option(write.option);
    if (!ini_option_enabled(config, write.option)) {
      ++report.gated_out;
      log_printf("skip phase=sig-writes option=%s reason=option-off offset=%d", option.c_str(), write.site_offset);
      continue;
    }
    bool known = false;
    if (!build_allows_option(build, write.option, known)) {
      ++report.gated_out;
      log_printf("skip phase=sig-writes option=%s reason=build offset=%d", option.c_str(), write.site_offset);
      continue;
    }
    if (!anchor_cache.ok(image, write.option)) {
      // The original handler's first scan missed, so it never reached this
      // write (its whole write set sits inside `if (result)`).
      ++report.anchor_missing;
      log_printf("skip phase=sig-writes option=%s reason=handler-anchor-absent offset=%d", option.c_str(),
                 write.site_offset);
      continue;
    }
    std::vector<const std::byte*> matches;
    bool have_cached = false;
    for (const MatchCacheEntry& cached : match_cache) {
      if (cached.signature == write.signature) {
        matches = cached.matches;
        have_cached = true;
        break;
      }
    }
    if (!have_cached) {
      find_all_patterns(view, write.signature, matches);
      if (write.match_index >= matches.size() && !write.fallback_signature.empty()) {
        // The original's second scan: CutsceneFixes chain A's worker resets the
        // pattern text and scans a build-variant pattern when the first one has
        // no match (the EB then lands on that match instead).
        matches.clear();
        find_all_patterns(view, write.fallback_signature, matches);
      }
      match_cache.push_back({write.signature, matches});
    }
    if (write.match_index >= matches.size()) {
      ++report.missing;
      log_printf("skip phase=sig-writes option=%s reason=match-absent index=%u", option.c_str(),
                 write.site_offset, write.match_index);
      continue;
    }
    const std::byte* match_address = matches[write.match_index];
    const std::vector<std::byte> bytes = parse_hex_bytes(write.bytes);
    auto* site = const_cast<std::byte*>(match_address) + write.site_offset;
    const auto status = write_bytes(image, protect, log, write.option, site, bytes.data(),
                                    bytes.size());
    if (!status) return status;
    log_write("write", option.c_str(), reinterpret_cast<std::uintptr_t>(site), bytes.data(),
              bytes.size());
    ++report.applied;
  }
  return {};
}

Status apply_slot_installs(const ConfigDocument& config, MappedImage image, GameBuild build,
                           ProtectFn protect, AppliedWriteLog* log,
                           SlotInstallReport& report) noexcept {
  if (!image.base || image.size == 0 || !protect)
    return {StatusCode::invalid_input, "invalid slot install input"};
  const ModuleView view{image.base, image.size};
  AnchorCache anchor_cache;
  for (const SlotInstall& install : kSlotInstalls) {
    std::string option(install.option);
    if (!ini_option_enabled(config, install.option)) {
      ++report.gated_out;
      log_printf("skip phase=slots option=%s reason=option-off slot=%s", option.c_str(), std::string(install.slot_name).c_str());
      continue;
    }
    if (install.build != 0u && install.build != static_cast<std::uint8_t>(build)) {
      ++report.gated_out;
      log_printf("skip phase=slots option=%s reason=build slot=%s build=%u", option.c_str(),
                 std::string(install.slot_name).c_str(), install.build);
      continue;
    }
    // The handler-level recovered build gate applies to every pass, including
    // slot publishes: the original only runs a handler's arm on the builds its
    // switch allows (BenchmarkFix is gated off 1080/1130, where the original
    // publishes nothing - the port used to publish anyway).
    {
      bool known = false;
      if (!build_allows_option(build, install.option, known)) {
        ++report.gated_out;
        log_printf("skip phase=slots option=%s reason=build-gate slot=%s", option.c_str(),
                   std::string(install.slot_name).c_str());
        continue;
      }
    }
    if (!anchor_cache.ok(image, install.option)) {
      ++report.anchor_missing;
      log_printf("skip phase=slots option=%s reason=handler-anchor-absent slot=%s", option.c_str(),
                 std::string(install.slot_name).c_str());
      continue;
    }
    const std::uintptr_t slot = rebased_slot_for(install.slot_name);
    if (!slot) {
      ++report.skipped_no_slot;
      log_printf("skip phase=slots option=%s reason=no-rebased-global slot=%s", option.c_str(),
                 std::string(install.slot_name).c_str());
      continue;
    }
    if (install.kind == 3u) {  // rva: publish image base + add (no signature)
      const std::uint32_t value = static_cast<std::uint32_t>(
          reinterpret_cast<std::uintptr_t>(image.base) + install.add);
      *reinterpret_cast<std::uint32_t*>(slot) = value;
      log_printf("publish phase=slots option=%s slot=%s value=%08X source=rva", option.c_str(),
                 std::string(install.slot_name).c_str(), value);
      ++report.applied;
      continue;
    }
    std::vector<const std::byte*> matches;
    find_all_patterns(view, install.signature, matches);
    if (install.match_index >= matches.size()) {
      ++report.missing;
      log_printf("skip phase=slots option=%s reason=match-absent slot=%s index=%u", option.c_str(),
                 std::string(install.slot_name).c_str(), install.match_index);
      continue;
    }
    const std::byte* match_address = matches[install.match_index];
    const auto match_va = reinterpret_cast<std::uintptr_t>(match_address);
    std::uint32_t value = static_cast<std::uint32_t>(match_va + install.add);
    if (install.kind == 1u || install.kind == 2u) {
      std::uint32_t field = 0;
      std::memcpy(&field, match_address + install.field_offset, sizeof(field));
      value = static_cast<std::uint32_t>(match_va + field + install.add);
      if (install.kind == 2u) {
        // call_target_deref: the value is the dword at the computed address
        // (the original's *(resume + 1) value pointer). Only read when the
        // address lies inside the image.
        const auto address = static_cast<std::uintptr_t>(value);
        if (address < reinterpret_cast<std::uintptr_t>(image.base) ||
            address + sizeof(std::uint32_t) >
                reinterpret_cast<std::uintptr_t>(image.base) + image.size) {
          ++report.skipped_no_slot;
          log_printf("skip phase=slots option=%s reason=deref-outside-image slot=%s", option.c_str(),
                     std::string(install.slot_name).c_str());
          continue;
        }
        std::uint32_t deref = 0;
        std::memcpy(&deref, reinterpret_cast<const void*>(address), sizeof(deref));
        value = deref;
      }
    } else if (install.kind == 4u) {
      // ptr_site_field: the site address is a pointer stored at match +
      // site_field_offset; the value is site + *(site + field_offset) + add.
      std::uint32_t site32 = 0;
      std::memcpy(&site32, match_address + install.site_field_offset, sizeof(site32));
      const auto site = static_cast<std::uintptr_t>(site32);
      const auto image_lo = reinterpret_cast<std::uintptr_t>(image.base);
      if (site < image_lo || site + sizeof(std::uint32_t) > image_lo + image.size) {
        ++report.skipped_no_slot;
        log_printf("skip phase=slots option=%s reason=site-outside-image slot=%s", option.c_str(),
                   std::string(install.slot_name).c_str());
        continue;
      }
      std::uint32_t field = 0;
      std::memcpy(&field, reinterpret_cast<const void*>(site + install.field_offset),
                  sizeof(field));
      value = static_cast<std::uint32_t>(site + field + install.add);
    } else if (install.kind == 5u) {
      // match_deref: the value is the dword stored at match + field_offset (no
      // base is added; the original reads a model hash out of the matched code).
      std::uint32_t field = 0;
      std::memcpy(&field, match_address + install.field_offset, sizeof(field));
      value = field;
    }
    *reinterpret_cast<std::uint32_t*>(slot) = value;
    log_printf("publish phase=slots option=%s slot=%s value=%08X", option.c_str(),
               std::string(install.slot_name).c_str(), value);
    ++report.applied;
  }
  (void)protect;
  (void)log;
  return {};
}

bool op_row_suppressed(std::uint32_t handler, std::string_view signature, std::int32_t offset,
                       std::string_view replacement) noexcept {
  for (const OpSuppression& row : kOpSuppressions) {
    if (row.handler != handler || row.offset != offset) continue;
    if (row.signature != signature) continue;
    if (normalized(row.values) != normalized(replacement)) continue;
    return true;
  }
  return false;
}

Status apply_sig_installs(const ConfigDocument& config, MappedImage image, GameBuild build,
                          std::uintptr_t game_module_base, ProtectFn protect,
                          AppliedWriteLog* log, RuntimeDestinationResolver resolve,
                          void* resolver_user, SigInstallReport& report) noexcept {
  if (!image.base || image.size == 0 || !protect)
    return {StatusCode::invalid_input, "invalid sig install input"};
  const ModuleView view{image.base, image.size};
  constexpr std::size_t kCount = sizeof(kSigInstalls) / sizeof(kSigInstalls[0]);
  AnchorCache anchor_cache;

  // A handler's installs for one signature form a patch set: applying the
  // skip-jump without its companion call (whose body may be missing) breaks the
  // game - the NewMemorySystem startup failure. Sets are all-or-nothing for
  // their *enabled* members: one signature may carry rows for several options
  // (the shared IDE collector), and each row is gated on its own option *and*
  // on its handler's first-scan anchor (a handler whose guard scan misses never
  // reaches any of its rows).
  for (std::size_t i = 0; i < kCount; ++i) {
    const SigInstall& install = kSigInstalls[i];
    bool seen = false;
    for (std::size_t j = 0; j < i; ++j) {
      if (kSigInstalls[j].signature == install.signature) {
        seen = true;
        break;
      }
    }
    if (seen) continue;

    std::string option(install.option);
    bool any_enabled = false;
    for (std::size_t j = i; j < kCount; ++j) {
      const SigInstall& member = kSigInstalls[j];
      if (member.signature != install.signature) continue;
      bool known = false;
      if (!ini_option_enabled(config, member.option) ||
          !build_allows_option(build, member.option, known)) {
        ++report.gated_out;
        continue;
      }
      if (!anchor_cache.ok(image, member.option)) {
        ++report.anchor_missing;
        continue;
      }
      any_enabled = true;
    }
    if (!any_enabled) {
      log_printf("skip phase=sig-installs option=%s reason=option-off offset=%d", option.c_str(), install.site_offset);
      continue;
    }

    Match match;
    const bool found = static_cast<bool>(find_pattern(view, install.signature, match));
    if (!found) {
      for (std::size_t j = i; j < kCount; ++j) {
        if (kSigInstalls[j].signature != install.signature) continue;
        bool known = false;
        if (!ini_option_enabled(config, kSigInstalls[j].option) ||
            !build_allows_option(build, kSigInstalls[j].option, known))
          continue;
        if (!anchor_cache.ok(image, kSigInstalls[j].option)) continue;
        ++report.missing;
      }
      log_printf("skip phase=sig-installs option=%s reason=signature-absent offset=%d", option.c_str(),
                 install.site_offset);
      continue;
    }
    bool complete = true;
    for (std::size_t j = i; j < kCount; ++j) {
      const SigInstall& member = kSigInstalls[j];
      if (member.signature != install.signature) continue;
      bool known = false;
      if (!ini_option_enabled(config, member.option) ||
          !build_allows_option(build, member.option, known))
        continue;
      if (!anchor_cache.ok(image, member.option)) continue;
      if (member.dest_kind == 0u && (!resolve || !resolve(member.dest, resolver_user))) {
        complete = false;
        break;
      }
    }
    for (std::size_t j = i; j < kCount; ++j) {
      const SigInstall& member = kSigInstalls[j];
      if (member.signature != install.signature) continue;
      bool known = false;
      if (!ini_option_enabled(config, member.option) ||
          !build_allows_option(build, member.option, known))
        continue;
      if (!anchor_cache.ok(image, member.option)) continue;
      if (!complete) {
        ++report.skipped_no_destination;
        log_printf("skip phase=sig-installs option=%s reason=companion-body-missing offset=%d",
                   option.c_str(), member.site_offset);
        continue;
      }
      auto* site_ptr = const_cast<std::byte*>(match.address) + member.site_offset;
      auto* site = site_ptr;
      if (member.site_deref != 0u) {
        // The site is a pointer stored at match + site_offset (CrashMsgFix's
        // `*(match-4)`), not a match-relative offset. Fault-preserving (N4): the
        // original writes to that pointer's target without validating it, so
        // there is no image gate here (see write_bytes above).
        std::uint32_t site32 = 0;
        std::memcpy(&site32, site_ptr, sizeof(site32));
        site = reinterpret_cast<std::byte*>(static_cast<std::uintptr_t>(site32));
      } else if (site < image.base || site + 5u > image.base + image.size) {
        ++report.skipped_no_destination;
        log_printf("skip phase=sig-installs option=%s reason=site-outside-image offset=%d", option.c_str(),
                   member.site_offset);
        continue;
      }
      if (member.dest_kind == 6u) {
        // literal_bytes: a plain byte string written at match + site_offset
        // inside this patch-set pass (the scan already happened, so in-pattern
        // rewrites like MouseFix's 90 90 / 66 0F 6E C0 are applied atomically
        // with the rest of the set).
        const std::vector<std::byte> literal = parse_hex_bytes(member.bytes);
        if (literal.empty() || !in_bounds(image, site, literal.size())) {
          ++report.skipped_no_destination;
          log_printf("skip phase=sig-installs option=%s reason=literal-out-of-bounds offset=%d", option.c_str(),
                     member.site_offset);
          continue;
        }
        const auto status = write_bytes(image, protect, log, member.option, site,
                                        literal.data(), literal.size());
        if (!status) return status;
        log_write("write", option.c_str(), reinterpret_cast<std::uintptr_t>(site),
                  literal.data(), literal.size());
        ++report.applied;
        continue;
      }
      if (member.dest_kind == 3u) {
        // rebased_global: store the ADDRESS of our equivalent global as a 4-byte
        // value at match + site_offset (e.g. PopupHeadlightsFlag's
        // `*(match+82) = &dword_101580CC`, which redirects a game operand to our
        // variable). No opcode/displacement is written.
        const std::uintptr_t value = rebased_value_for(member.option);
        if (value == 0) {
          ++report.skipped_no_destination;
          log_printf("skip phase=sig-installs option=%s reason=no-rebased-global offset=%d", option.c_str(),
                     member.site_offset);
          continue;
        }
        if (site + 4u > image.base + image.size) {
          ++report.skipped_no_destination;
          continue;
        }
        std::byte vbytes[4];
        std::uint32_t v32 = static_cast<std::uint32_t>(value);
        std::memcpy(vbytes, &v32, sizeof(v32));
        const auto status = write_bytes(image, protect, log, member.option, site, vbytes, 4);
        if (!status) return status;
        log_write("write", option.c_str(), reinterpret_cast<std::uintptr_t>(site), vbytes, 4);
        ++report.applied;
        continue;
      }
      if (member.dest_kind == 4u) {
        // literal_byte: a plain 1-byte write with no displacement (the handler's
        // 75->EB opcode swaps, e.g. PopupHeadlightsFlag's `*(site+10) = 0xEB`).
        const std::byte value = static_cast<std::byte>(member.dest & 0xffu);
        const auto status = write_bytes(image, protect, log, member.option, site, &value, 1);
        if (!status) return status;
        log_write("write", option.c_str(), reinterpret_cast<std::uintptr_t>(site), &value, 1);
        ++report.applied;
        continue;
      }
      if (member.dest_kind == 5u) {
        // plugin_data_ptr: store the ADDRESS of a named rebuilt-module global
        // as a 4-byte value at match + site_offset (LocalSettingsPath's
        // "\GTA IV" suffix buffer replacing a pushed game literal).
        const std::uintptr_t value = rebased_symbol_for(member.dest_symbol);
        if (value == 0) {
          ++report.skipped_no_destination;
          log_printf("skip phase=sig-installs option=%s reason=no-rebased-symbol offset=%d symbol=%.*s", option.c_str(),
                     member.site_offset, static_cast<int>(member.dest_symbol.size()),
                     member.dest_symbol.data());
          continue;
        }
        if (site + 4u > image.base + image.size) {
          ++report.skipped_no_destination;
          continue;
        }
        std::byte vbytes[4];
        std::uint32_t v32 = static_cast<std::uint32_t>(value);
        std::memcpy(vbytes, &v32, sizeof(v32));
        const auto status = write_bytes(image, protect, log, member.option, site, vbytes, 4);
        if (!status) return status;
        log_write("write", option.c_str(), reinterpret_cast<std::uintptr_t>(site), vbytes, 4);
        ++report.applied;
        continue;
      }
      std::uintptr_t destination = 0;
      if (member.dest_kind == 2u) {
        // match_branch: the conditional branch the site replaces is 6 bytes
        // (0F 8x rel32); its target is site + 6 + rel32, so writing E9 there
        // keeps the same destination (je becomes jmp).
        const auto* rel_ptr = match.address + member.site_offset + 2;
        if (rel_ptr < image.base || rel_ptr + 4 > image.base + image.size) {
          ++report.skipped_no_destination;
          log_printf("skip phase=sig-installs option=%s reason=branch-outside-image offset=%d",
                     option.c_str(), member.site_offset);
          continue;
        }
        std::int32_t rel = 0;
        std::memcpy(&rel, rel_ptr, sizeof(rel));
        destination = reinterpret_cast<std::uintptr_t>(match.address) +
                      static_cast<std::uintptr_t>(member.site_offset) + 6u +
                      static_cast<std::uintptr_t>(static_cast<std::intptr_t>(rel));
      } else if (member.dest_kind == 0u) {
        destination = resolve(member.dest, resolver_user);
      } else {
        destination = reinterpret_cast<std::uintptr_t>(match.address) + member.dest;
      }
      const auto origin = reinterpret_cast<std::uintptr_t>(site);
      const auto displacement = static_cast<std::int32_t>(destination - (origin + 5u));
      std::byte bytes[5];
      bytes[0] = static_cast<std::byte>(member.opcode);
      std::memcpy(bytes + 1, &displacement, sizeof(displacement));
      const auto status = write_bytes(image, protect, log, member.option, site, bytes, 5);
      if (!status) return status;
      log_write("write", option.c_str(), reinterpret_cast<std::uintptr_t>(site), bytes, 5);
      ++report.applied;
    }
  }
  (void)game_module_base;  // sig installs are match-relative; the base is not needed
  return {};
}

Status apply_sig_jump_installs(const ConfigDocument& config, MappedImage image,
                               std::uintptr_t game_module_base, ProtectFn protect,
                               AppliedWriteLog* log, RuntimeDestinationResolver resolve,
                               void* resolver_user, SigJumpReport& report) noexcept {
  if (!image.base || image.size == 0 || !protect)
    return {StatusCode::invalid_input, "invalid sig jump input"};
  const ModuleView view{image.base, image.size};
  std::size_t count = 0;
  const RecoveredSigJump* jumps = recovered_sig_jumps(count);
  AnchorCache anchor_cache;
  for (std::size_t i = 0; i < count; ++i) {
    const RecoveredSigJump& jump = jumps[i];
    std::string option(jump.option);
    if (!ini_option_enabled(config, jump.option)) {
      ++report.gated_out;
      continue;
    }
    if (!anchor_cache.ok(image, jump.option)) {
      ++report.anchor_missing;
      log_printf("skip phase=sig-jumps option=%s reason=handler-anchor-absent offset=%d", option.c_str(),
                 jump.offset);
      continue;
    }
    bool duplicate = false;
    for (std::size_t j = 0; j < i; ++j) {
      if (jumps[j].signature == jump.signature && jumps[j].offset == jump.offset &&
          jumps[j].destination == jump.destination && jumps[j].option == jump.option) {
        duplicate = true;
        break;
      }
    }
    if (duplicate) continue;
    Match match;
    if (!find_pattern(view, jump.signature, match)) {
      ++report.missing;
      log_printf("skip phase=sig-jumps option=%s reason=signature-absent offset=%d", option.c_str(), jump.offset);
      continue;
    }
    const std::uintptr_t destination =
        resolve ? resolve(jump.destination, resolver_user) : 0;
    if (!destination) {
      ++report.skipped_no_destination;
      log_printf("skip phase=sig-jumps option=%s reason=no-destination offset=%d", option.c_str(),
                 jump.offset);
      continue;
    }
    auto* site = const_cast<std::byte*>(match.address) + jump.offset;
    if (site < image.base ||
        site + 5 > image.base + image.size) {
      ++report.missing;
      log_printf("skip phase=sig-jumps option=%s reason=site-outside-image offset=%d", option.c_str(),
                 jump.offset);
      continue;
    }
    // Displacement from the live site VA (identical to the buffer address in
    // production, and the only form a 32-bit test buffer can validate).
    const auto origin = game_module_base +
                        static_cast<std::uintptr_t>(site - image.base);
    const auto displacement = static_cast<std::int32_t>(destination - (origin + 5u));
    std::byte bytes[5];
    bytes[0] = std::byte{0xE9};
    std::memcpy(bytes + 1, &displacement, sizeof(displacement));
    const auto status = write_bytes(image, protect, log, jump.option, site, bytes, 5);
    if (!status) return status;
    log_write("write", option.c_str(), origin, bytes, 5);
    ++report.applied;
  }
  return {};
}

}  // namespace zpatch
