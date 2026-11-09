#include "zpatch/recovered_extra_signatures.hpp"
#include "zpatch/recovered_rva_jumps.hpp"
#include "zpatch/recovered_sig_jumps.hpp"
#include "zpatch/recovered_indirect_writes.hpp"
#include "zpatch/recovered_match_jumps.hpp"
#include "zpatch/recovered_literal_stores.hpp"
#include "zpatch/recovered_match_literals.hpp"
#include "zpatch/config.hpp"
#include "zpatch/game.hpp"
#include "zpatch/model_catalog.hpp"
#include "zpatch/patch.hpp"
#include "zpatch/patch_manifest.hpp"
#include "zpatch/option_flags.hpp"
#include "zpatch/recovered_sites.hpp"
#include "zpatch/recovered_apply.hpp"
#include "zpatch/game_image.hpp"
#include "zpatch/menu_integration.hpp"
#include "zpatch/zmenu_guard.hpp"
#include "zpatch/number_format.hpp"
#include <cstring>
#include "zpatch/recovered_build_gates.hpp"
#include "zpatch/recovered_preamble.hpp"
#include "zpatch/preamble_apply.hpp"
#include "zpatch/recovered_ops.hpp"
#include "zpatch/patch_catalog.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/runtime.hpp"
#include "zpatch/vehicle_tables.hpp"
#include "zpatch/ide_flags.hpp"
#include "zpatch/log.hpp"
#include "zpatch/vehicle_animation.hpp"
#include "zpatch/hook_bodies.hpp"
#include "zpatch/hook_bodies_win.hpp"
#include "zpatch/hook_install.hpp"
#include "zpatch/op_corrections.hpp"
#include "zpatch/rebased_values.hpp"
#include "zpatch/rva_installs.hpp"
#include "zpatch/useless_checks.hpp"
#include "zpatch/skip_launcher.hpp"
#include <limits>
#include <cmath>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Runs one fault probe in a child copy of this executable and returns the
// child's *exact* exit status via GetExitCodeProcess. The old std::system()
// path went through cmd.exe, which made a Defender-blocked child (immediate
// 0xC0000005 on load) indistinguishable from a clean `return 1`; session 17
// recorded a phantom `exit=1` because of it.
static int run_fault_probe_child(const char* probe, bool& launched) {
  char exe[MAX_PATH] = {};
  if (::GetModuleFileNameA(nullptr, exe, MAX_PATH) == 0) {
    launched = false;
    return -1;
  }
  char cmd[MAX_PATH + 64] = {};
  std::snprintf(cmd, sizeof(cmd), "\"%s\" --fault-probe %s", exe, probe);
  STARTUPINFOA si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  launched = ::CreateProcessA(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr,
                              nullptr, &si, &pi) != 0;
  if (!launched) return -1;
  ::WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 0;
  ::GetExitCodeProcess(pi.hProcess, &code);
  ::CloseHandle(pi.hThread);
  ::CloseHandle(pi.hProcess);
  return static_cast<int>(code);
}
#endif

#include <array>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iterator>
#include <vector>

// Maps a PE32 file into a contiguous image (headers plus sections at their
// virtual addresses) so pattern scans and patches see the same layout the
// loader creates. Used to exercise SkipLauncher against the real game image.
static bool map_pe_image(const char* path, std::vector<std::byte>& image,
                         std::uint32_t& image_base, std::uint32_t& size_of_image) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  const std::vector<char> raw{std::istreambuf_iterator<char>(in),
                              std::istreambuf_iterator<char>()};
  std::vector<std::byte> file(raw.size());
  for (std::size_t i = 0; i < raw.size(); ++i)
    file[i] = static_cast<std::byte>(static_cast<unsigned char>(raw[i]));
  if (file.size() < 0x100) return false;
  const auto rd16 = [&](std::size_t off) {
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(file[off]) |
        (std::to_integer<std::uint8_t>(file[off + 1]) << 8));
  };
  const auto rd32 = [&](std::size_t off) {
    return static_cast<std::uint32_t>(
        std::to_integer<std::uint8_t>(file[off]) |
        (std::to_integer<std::uint8_t>(file[off + 1]) << 8) |
        (std::to_integer<std::uint8_t>(file[off + 2]) << 16) |
        (std::to_integer<std::uint8_t>(file[off + 3]) << 24));
  };
  if (std::to_integer<std::uint8_t>(file[0]) != 'M' ||
      std::to_integer<std::uint8_t>(file[1]) != 'Z')
    return false;
  const std::size_t e_lfanew = rd32(0x3C);
  if (e_lfanew + 0x60 > file.size()) return false;
  const std::size_t coff = e_lfanew + 4;
  const std::uint16_t sections = rd16(coff + 2);
  const std::uint16_t optional_size = rd16(coff + 16);
  const std::size_t optional = coff + 20;
  image_base = rd32(optional + 28);
  size_of_image = rd32(optional + 56);
  const std::uint32_t size_of_headers = rd32(optional + 60);
  if (size_of_image == 0 || size_of_image > 0x10000000u) return false;
  image.assign(size_of_image, std::byte{0});
  std::memcpy(image.data(), file.data(),
              size_of_headers < file.size() ? size_of_headers : file.size());
  const std::size_t section_table = optional + optional_size;
  for (std::uint16_t i = 0; i < sections; ++i) {
    const std::size_t sh = section_table + i * 40u;
    if (sh + 40 > file.size()) return false;
    const std::uint32_t vsize = rd32(sh + 8);
    const std::uint32_t vaddr = rd32(sh + 12);
    const std::uint32_t rsize = rd32(sh + 16);
    const std::uint32_t rptr = rd32(sh + 20);
    const std::uint32_t count = rsize < vsize ? rsize : vsize;
    if (count && vaddr + count <= size_of_image && rptr + count <= file.size())
      std::memcpy(image.data() + vaddr, file.data() + rptr, count);
  }
  return true;
}

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, __LINE__, #x); return 1; } } while (false)

// Builds a config with every option the recovered tables reference explicitly
// set to "0". With the loader's arm-default semantics an absent key is NOT
// disabled, so single-option tests must switch every other option off.
static zpatch::ConfigDocument all_options_off() {
  zpatch::ConfigDocument config;
  const auto disable = [&](std::string_view option) {
    for (const zpatch::ConfigEntry& e : config.entries)
      if (e.key == option) return;
    config.entries.push_back({"Options", std::string(option), "0"});
  };
  std::size_t count = 0;
  const auto* si = zpatch::sig_installs(count);
  for (std::size_t i = 0; i < count; ++i) disable(si[i].option);
  const auto* sl = zpatch::slot_installs(count);
  for (std::size_t i = 0; i < count; ++i) disable(sl[i].option);
  const auto* sw = zpatch::sig_writes(count);
  for (std::size_t i = 0; i < count; ++i) disable(sw[i].option);
  const auto* sj = zpatch::recovered_sig_jumps(count);
  for (std::size_t i = 0; i < count; ++i) disable(sj[i].option);
  const auto* mj = zpatch::recovered_match_jumps(count);
  for (std::size_t i = 0; i < count; ++i) disable(mj[i].option);
  const auto* ml = zpatch::recovered_match_literals(count);
  for (std::size_t i = 0; i < count; ++i) disable(ml[i].option);
  const auto* iw = zpatch::recovered_indirect_writes(count);
  for (std::size_t i = 0; i < count; ++i) disable(iw[i].option);
  const auto* ls = zpatch::recovered_literal_stores(count);
  for (std::size_t i = 0; i < count; ++i) disable(ls[i].option);
  const auto* rj = zpatch::recovered_rva_jumps(count);
  for (std::size_t i = 0; i < count; ++i) disable(rj[i].option);
  const auto* ri = zpatch::recovered_rva_installs(count);
  for (std::size_t i = 0; i < count; ++i) disable(ri[i].option);
  const auto* po = zpatch::recovered_patch_ops(count);
  for (std::size_t i = 0; i < count; ++i) disable(po[i].option);
  return config;
}

// Appends a key/value; lookup is last-value-wins.
static void set_option(zpatch::ConfigDocument& config, std::string_view key,
                       std::string_view value) {
  config.entries.push_back({"Options", std::string(key), std::string(value)});
}

static zpatch::ConfigDocument single_option_config(std::string_view keep) {
  auto config = all_options_off();
  set_option(config, keep, "1");
  return config;
}

int main(int argc, char** argv) {
#if defined(_WIN32)
  // Fault-probe child mode (fault-inclusive policy): run one forced condition
  // through the de-guarded body and die at the same instruction the original
  // dies at. The parent below asserts the STATUS_ACCESS_VIOLATION exit code.
  if (argc >= 3 && std::strcmp(argv[1], "--fault-probe") == 0) {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX |
                   SEM_NOOPENFILEERRORBOX);
    std::fprintf(stderr, "fault-probe: %s (the forced condition must fault)\n", argv[2]);
    std::fflush(stderr);
    return zpatch::zpatch_fault_probe(argv[2]);
  }
#endif
  // Development aid: apply the whole recovered patch set to a mapped copy of
  // game_refs/GTAIV.exe and write "RVA byte" lines. The readable port's
  // installers are diffed against this reference output.
  if (argc >= 3 && std::strcmp(argv[1], "--dump-writes") == 0) {
    std::vector<std::byte> game;
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    if (!(map_pe_image("game_refs/GTAIV.exe", game, base, size) ||
          map_pe_image("../game_refs/GTAIV.exe", game, base, size) ||
          map_pe_image("../../game_refs/GTAIV.exe", game, base, size))) {
      std::fprintf(stderr, "dump-writes: GTAIV.exe not found\n");
      return 2;
    }
    const char* ini = argc >= 4 ? argv[3] : "game_refs/ZolikaPatch.ini";
    if (!zpatch::initialize(ini) && !zpatch::initialize("../game_refs/ZolikaPatch.ini") &&
        !zpatch::initialize("../../game_refs/ZolikaPatch.ini")) {
      std::fprintf(stderr, "dump-writes: cannot read %s\n", ini);
      return 3;
    }
    // Map at the preferred base: rows that store absolute module addresses must
    // see the same addresses the game would.
    auto* mapped = static_cast<std::byte*>(
        ::VirtualAlloc(reinterpret_cast<void*>(static_cast<std::uintptr_t>(base)), game.size(),
                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!mapped) {
      std::fprintf(stderr, "dump-writes: cannot map at 0x%X\n", base);
      return 5;
    }
    std::memcpy(mapped, game.data(), game.size());
    const std::vector<std::byte> pristine(mapped, mapped + game.size());
    const auto protect = +[](void*, std::size_t, std::uint32_t, std::uint32_t*) -> zpatch::Status {
      return {};
    };
    zpatch::apply_loader_preamble({mapped, game.size()});
    zpatch::RuntimePatchReport report;
    const auto status = zpatch::apply_recovered_patches(
        zpatch::GameBuild::v1080, {mapped, game.size()}, protect, report);
    FILE* out = std::fopen(argv[2], "wb");
    if (!out) return 4;
    std::size_t changed = 0;
    for (std::size_t i = 0; i < game.size(); ++i) {
      if (mapped[i] == pristine[i]) continue;
      std::fprintf(out, "%08zX %02X\n", i, std::to_integer<unsigned>(mapped[i]));
      ++changed;
    }
    std::fclose(out);
    std::fprintf(stderr, "dump-writes: ok=%d applied=%zu not_found=%zu gated=%zu changed=%zu\n",
                 int(static_cast<bool>(status)), report.applied, report.not_found,
                 report.gated_out, changed);
    return 0;
  }

  zpatch::ConfigDocument config;
  CHECK(zpatch::parse_config("\xEF\xBB\xBF; hello\r\n[ Options ]\r\nFlag = 1\nFlag=2\nEmpty=\n", config));
  CHECK(config.entries.size() == 3);
  CHECK(config.find("Options", "Flag") && config.find("Options", "Flag")->value == "2");
  CHECK(config.find("Options", "Empty") && config.find("Options", "Empty")->value.empty());
  zpatch::ConfigDocument unchanged; unchanged.entries.push_back({"x", "y", "z"});
  CHECK(!zpatch::parse_config("[broken\n", unchanged));
  CHECK(unchanged.entries.size() == 1);

  const std::array<std::byte, 7> bytes{std::byte{0x90}, std::byte{0x48}, std::byte{0x8b}, std::byte{0x7f}, std::byte{0x05}, std::byte{0x90}, std::byte{0x00}};
  zpatch::Match match;
  CHECK(zpatch::find_pattern({bytes.data(), bytes.size()}, "48 8B ?? 05", match));
  CHECK(match.address == bytes.data() + 1);
  // strtoul("GG",16) yields 0: non-hex tokens become a zero byte, not an error.
  CHECK(zpatch::find_pattern({bytes.data(), bytes.size()}, "GG", match));
  CHECK(match.address == bytes.data() + 6);
  // Observed token semantics: single digits, 0x prefixes, sign wrap and unknown
  // text all reduce through strtoul(base 16) to a low-byte value.
  CHECK(zpatch::find_pattern({bytes.data(), bytes.size()}, "48 8b ? 5", match));
  CHECK(match.address == bytes.data() + 1);
  CHECK(zpatch::find_pattern({bytes.data(), bytes.size()}, "0x48 0x8b 0x7f", match));
  // "junk" parses as 0, so "7F junk" requires bytes 7F 00 which are absent here.
  CHECK(!zpatch::find_pattern({bytes.data(), bytes.size()}, "48 8b 7F junk", match));
  CHECK(zpatch::find_pattern({bytes.data(), bytes.size()}, "90 48 8b 7f", match));
  CHECK(!zpatch::find_pattern({bytes.data(), bytes.size()}, "48 8b 7e", match));
  const std::array<std::byte, 2> wrapped{std::byte{0x90}, std::byte{0xff}};
  CHECK(zpatch::find_pattern({wrapped.data(), wrapped.size()}, "90 -1", match));
  CHECK(!zpatch::find_pattern({bytes.data(), bytes.size()}, "AA BB", match));

  CHECK(zpatch::model_catalog_size()==38455);
  CHECK(zpatch::model_catalog_name(0)=="01_wires_mh1");
  CHECK(zpatch::model_catalog_name(38454)=="ztype");
  CHECK(zpatch::model_catalog_name(38455)=="**UNKNOWN**");
  CHECK(zpatch::model_name_from_hash(zpatch::game_hash("police"))=="police");
  CHECK(zpatch::model_name_from_hash(zpatch::game_hash("ztype"))=="ztype");
  CHECK(zpatch::model_name_from_hash(0)=="**UNKNOWN**");
  CHECK(zpatch::game_hash("police") == 0x79fbb0c5u);
  CHECK(zpatch::game_hash("POLICE") == zpatch::game_hash("police"));
  CHECK(zpatch::game_hash("a\\b") == zpatch::game_hash("a/b"));
  CHECK(zpatch::game_hash("\"POLICE\"ignored") == zpatch::game_hash("police"));
  CHECK(zpatch::game_hash("") == 2u);
  CHECK(zpatch::game_hash("hash:0x1234") == 0x1234u);
  CHECK(zpatch::game_hash("HASH:123trailing") == 123u);
  CHECK(zpatch::game_hash("hash:  -1tail") == 0xffffffffu);
  CHECK(zpatch::game_hash("hash:077") == 63u);
  CHECK(zpatch::game_hash("hash:0") == 0xf4856ffeu);

  std::uint32_t version = 0;
  CHECK(zpatch::compose_game_version(1, 0, 7, 0, version) && version == 1070u);
  CHECK(zpatch::compose_game_version(1, 0, 4, 0, version) && version == 1040u);
  CHECK(zpatch::compose_game_version(1, 0, 6, 1, version) && version == 1061u);
  CHECK(zpatch::compose_game_version(1, 1, 0, 0, version) && version == 1100u);
  CHECK(zpatch::compose_game_version(1, 2, 0, 43, version) && version == 12043u);
  CHECK(zpatch::classify_game_build(1070) == zpatch::GameBuild::v1070);
  CHECK(zpatch::classify_game_build(1061) == zpatch::GameBuild::v1061);
  CHECK(zpatch::classify_game_build(1100) == zpatch::GameBuild::v1100);
  CHECK(zpatch::classify_game_build(12043) == zpatch::GameBuild::v12043);
  CHECK(zpatch::classify_game_build(9999) == zpatch::GameBuild::unknown);
  // These three builds are recognized, but lack pinned executables. Only the
  // recovered generic patterns/relational gates are used for them; exact RVA
  // rows remain limited to the original handler's disassembled equality arms.
  // 12059 is not a product version recognized by the original classifier.
  CHECK(zpatch::classify_game_build(12059) == zpatch::GameBuild::unknown);
  CHECK(!zpatch::rva_build_allows(0x100234e0u, 4785166u, 18));   // eq-gated to build 9
  CHECK(!zpatch::rva_build_allows(0x100234e0u, 4785166u, 15));   // no adjacent alias for 1061
  CHECK(!zpatch::rva_build_allows(0x100234e0u, 4785166u, 10));   // no adjacent alias for EFLC 1100
  CHECK(!zpatch::rva_build_allows(0x100234e0u, 4785166u, 0));    // unknown never blind-writes
  CHECK(!zpatch::recovered_feature_allows_build("SuperLODFix", 18));
  CHECK(!zpatch::recovered_feature_allows_build("SuperLODFix", 0));
  CHECK(zpatch::recovered_feature_allows_build("SkipIntro", 18));  // ungated arms run
#if !defined(_WIN32)
  version = 123;
  auto detected_build = zpatch::GameBuild::v1070;
  CHECK(!zpatch::query_current_game_version(version, detected_build));
  CHECK(version == 0 && detected_build == zpatch::GameBuild::unknown);
#endif

  std::array<std::byte, 5> writable{std::byte{1}, std::byte{2}, std::byte{3},
                                    std::byte{4}, std::byte{5}};
  const std::array<std::byte, 2> expected{std::byte{2}, std::byte{3}};
  const std::array<std::byte, 2> replacement{std::byte{0x90}, std::byte{0x90}};
  CHECK(!zpatch::apply_guarded_patch(zpatch::GameBuild::v1080, zpatch::GameBuild::v1070,
      {writable.data(), writable.size()}, 1, {expected.data(), expected.size()},
      {replacement.data(), replacement.size()}));
  CHECK(zpatch::apply_guarded_patch(zpatch::GameBuild::v1070, zpatch::GameBuild::v1070, {writable.data(), writable.size()}, 1,
      {expected.data(), expected.size()}, {replacement.data(), replacement.size()}));
  CHECK(writable[1] == std::byte{0x90} && writable[2] == std::byte{0x90});
  auto rejected = zpatch::apply_guarded_patch(zpatch::GameBuild::v1070, zpatch::GameBuild::v1070, {writable.data(), writable.size()}, 1,
      {expected.data(), expected.size()}, {replacement.data(), replacement.size()});
  CHECK(!rejected && rejected.code == zpatch::StatusCode::evidence_mismatch);
  CHECK(!zpatch::apply_guarded_patch(zpatch::GameBuild::v1070, zpatch::GameBuild::v1070, {writable.data(), writable.size()}, 5,
      {expected.data(), expected.size()}, {replacement.data(), replacement.size()}));
  CHECK(zpatch::rollback_guarded_patch(zpatch::GameBuild::v1070, zpatch::GameBuild::v1070, {writable.data(), writable.size()}, 1,
      {expected.data(), expected.size()}, {replacement.data(), replacement.size()}));
  CHECK(writable[1] == std::byte{2} && writable[2] == std::byte{3});

  zpatch::ConfigDocument lights;
  CHECK(zpatch::parse_config("[PopupLightAngles]\nCar1Name=police\nCar1Angle=90\nCar512Name=hash:123\nCar512Angle=-180suffix\n[HeadlightConeOffsets]\nCar2Name=POLICE\nCar2Offset=3.5\n", lights));
  zpatch::VehicleLightTables tables;
  CHECK(zpatch::load_vehicle_light_tables(lights,tables));
  CHECK(tables.popup_angles[0].model_hash==zpatch::game_hash("police"));
  CHECK(std::fabs(tables.popup_angles[0].value-1.57079633f)<0.00001f);
  CHECK(tables.popup_angles[511].model_hash==123);
  CHECK(!tables.popup_angles[1].populated);
  CHECK(tables.headlight_offsets[1].value==3.5f);
  auto resolver = +[](std::uint32_t hash, void*) -> int {
    if (hash==zpatch::game_hash("police")) return 7;
    if (hash==0xeb9f21d3u) return 9;
    return -1;
  };
  std::array<std::uint32_t,512> supported{};
  supported[0]=zpatch::game_hash("police");
  CHECK(zpatch::select_popup_light(tables,supported,7,resolver).supported);
  CHECK(std::fabs(zpatch::select_popup_light(tables,supported,7,resolver).angle_radians-1.57079633f)<0.00001f);
  tables.popup_angles[511]={supported[0],0.75f,true};
  CHECK(zpatch::select_popup_light(tables,supported,7,resolver).angle_radians==0.75f);
  CHECK(zpatch::select_popup_light(tables,supported,9,resolver).supported);
  CHECK(!zpatch::select_popup_light(tables,supported,8,resolver).supported);
  CHECK(!zpatch::select_popup_light(tables,supported,-1,resolver).supported);
  CHECK(zpatch::select_headlight_offset(tables,7,resolver)==2.0f); // first slot is empty
  tables.headlight_offsets[0]={supported[0],-5.0f,true};
  CHECK(zpatch::select_headlight_offset(tables,7,resolver)==-5.0f); // first duplicate wins
  CHECK(zpatch::select_headlight_offset(tables,8,resolver)==2.0f);
  CHECK(zpatch::select_headlight_offset(tables,7,nullptr)==2.0f);
  CHECK(zpatch::parse_config("[PopupLightAngles]\nCar1Name=police\nCar1Angle=invalid\n", lights));
  CHECK(!zpatch::load_vehicle_light_tables(lights,tables));
  CHECK(tables.headlight_offsets[1].value==3.5f); // atomic rejection

  zpatch::VehicleIdeFlags ide;
  using IR=zpatch::IdeFlagResult;
  CHECK(zpatch::add_vehicle_ide_flag(ide,123,"popuplights")==IR::inserted);
  CHECK(zpatch::add_vehicle_ide_flag(ide,123,"popuplights")==IR::duplicate);
  CHECK(zpatch::add_vehicle_ide_flag(ide,456,"siren")==IR::inserted);
  CHECK(zpatch::add_vehicle_ide_flag(ide,123,"POPUPLIGHTS")==IR::ignored);
  CHECK(zpatch::add_vehicle_ide_flag(ide,123,"WHEELANGLE:12.5tail")==IR::inserted);
  CHECK(ide.steering_angles[0].degrees==12.5f);
  CHECK(zpatch::add_vehicle_ide_flag(ide,123,"wheelangle:90")==IR::duplicate);
  CHECK(ide.steering_angles[0].degrees==12.5f); // first value wins
  CHECK(zpatch::add_vehicle_ide_flag(ide,456,"wheelangle:bad")==IR::inserted);
  CHECK(ide.steering_angles[1].degrees==0.0f);
  CHECK(zpatch::add_vehicle_ide_flag(ide,0,"siren")==IR::invalid_model);
  for (unsigned i=0;i<512;++i) ide.siren_models[i]=i+1;
  CHECK(zpatch::add_vehicle_ide_flag(ide,600,"siren")==IR::full);
  CHECK(zpatch::add_vehicle_ide_flag(ide,512,"siren")==IR::duplicate);
  auto siren_resolver=+[](std::uint32_t hash,void*) -> int {
    if (hash==zpatch::game_hash("police")) return 1;
    if (hash==zpatch::game_hash("n_pstockade")) return 13;
    if (hash==600u) return 42;
    return -1;
  };
  zpatch::VehicleIdeFlags siren_flags;
  CHECK(zpatch::model_has_siren(siren_flags,1,siren_resolver));
  CHECK(zpatch::model_has_siren(siren_flags,13,siren_resolver));
  CHECK(!zpatch::model_has_siren(siren_flags,42,siren_resolver));
  CHECK(!zpatch::model_has_siren(siren_flags,-1,siren_resolver));
  CHECK(!zpatch::model_has_siren(siren_flags,1,nullptr));
  siren_flags.siren_models[0]=600;
  CHECK(zpatch::model_has_siren(siren_flags,42,siren_resolver));
  siren_flags.siren_models[0]=0;
  siren_flags.siren_models[1]=600;
  CHECK(!zpatch::model_has_siren(siren_flags,42,siren_resolver));
  std::array<std::uint32_t,512> ide_supported=ide.popup_models;
  CHECK(ide_supported[0]==123 && ide_supported[1]==0);

  zpatch::SteeringWheelRotation rotation;
  zpatch::VehicleIdeFlags steering_flags;
  steering_flags.steering_angles[0]={600u,90.0f};
  CHECK(zpatch::calculate_steering_wheel(steering_flags,42,0.5f,1.0f,siren_resolver,rotation));
  CHECK(std::fabs(rotation.tilt_radians-1.57079633f)<0.00001f);
  CHECK(std::fabs(rotation.turn_radians+1.57079633f)<0.00001f);
  CHECK(zpatch::calculate_steering_wheel(steering_flags,1,-1.0f,1.0f,siren_resolver,rotation));
  CHECK(rotation.tilt_radians==0.0f);
  CHECK(std::fabs(rotation.turn_radians-3.14159265f)<0.00001f);
  const auto saved_rotation=rotation;
  CHECK(!zpatch::calculate_steering_wheel(steering_flags,1,1.0f,0.0f,siren_resolver,rotation));
  CHECK(rotation.turn_radians==saved_rotation.turn_radians);
  steering_flags.steering_angles[0]={};
  steering_flags.steering_angles[1]={600u,90.0f};
  CHECK(zpatch::calculate_steering_wheel(steering_flags,42,0.0f,1.0f,siren_resolver,rotation));
  CHECK(rotation.tilt_radians==0.0f); // stop at hole

  const zpatch::NamedVehicleBone bones[]={{"SteeringWheel",17},{"engineblock",22},{"steeringwheel",99}};
  CHECK(zpatch::find_vehicle_bone(bones,3,"STEERINGWHEEL")==bones);
  CHECK(zpatch::find_vehicle_bone(bones,3,"EngineBlock")->bone_id==22);
  CHECK(!zpatch::find_vehicle_bone(bones,3,"wheel"));
  CHECK(!zpatch::find_vehicle_bone(bones,0,"engineblock"));
  CHECK(!zpatch::find_vehicle_bone(nullptr,3,"engineblock"));

  std::array<float,16> bone_matrix;
  for(unsigned i=0;i<bone_matrix.size();++i) bone_matrix[i]=static_cast<float>(i+10);
  const auto original_bone_matrix=bone_matrix;
  zpatch::reset_vehicle_bone_rotation(bone_matrix);
  for(unsigned row=0;row<3;++row)
    for(unsigned col=0;col<3;++col)
      CHECK(bone_matrix[row*4+col]==(row==col?1.0f:0.0f));
  const unsigned preserved_lanes[]={3,7,11,12,13,14,15};
  for(auto lane:preserved_lanes) CHECK(bone_matrix[lane]==original_bone_matrix[lane]);
  const auto reset_bone_matrix=bone_matrix;
  zpatch::reset_vehicle_bone_rotation(bone_matrix);
  CHECK(bone_matrix==reset_bone_matrix);

  zpatch::EngineAnimationState engine;
  float engine_rotation=99.0f;
  CHECK(zpatch::update_engine_animation(engine,0.04f,1.0f,8u,engine_rotation));
  CHECK(engine.descending && std::fabs(engine.phase-0.02f)<0.000001f);
  const float phase=engine.phase;
  CHECK(zpatch::update_engine_animation(engine,0.04f,0.0f,8u,engine_rotation));
  CHECK(engine.phase==phase); // repeated timestep does not advance oscillator
  CHECK(std::fabs(engine_rotation-0.005f)<0.000001f);
  CHECK(zpatch::update_engine_animation(engine,0.08f,0.0f,0x10u,engine_rotation));
  CHECK(!engine.descending && std::fabs(engine_rotation+0.02f)<0.000001f);
  CHECK(zpatch::update_engine_animation(engine,0.08f,1.0f,0u,engine_rotation));
  CHECK(engine_rotation==0.0f);
  const auto saved_engine=engine;
  CHECK(!zpatch::update_engine_animation(engine,std::numeric_limits<float>::infinity(),1.0f,8u,engine_rotation));
  CHECK(engine.phase==saved_engine.phase && engine.previous_timestep==saved_engine.previous_timestep);

  zpatch::SuspensionCommands suspension;
  CHECK(zpatch::calculate_suspension_commands(true,5,0.02f,3.0f,4.0f,6.0f,8.0f,suspension));
  const unsigned expected_ids[]={0x59,0x5b,0x5a,0x5c,0x60,0x61,0x5e,0x5f,0x5d};
  for(unsigned i=0;i<9;++i) CHECK(suspension[i].bone_id==expected_ids[i]);
  for(unsigned i=0;i<4;++i) {
    CHECK(suspension[i].mode==4 && !suspension[i].relative);
    CHECK(std::fabs(suspension[i].value-6.0f)<0.00001f);
  }
  CHECK(suspension[4].value==2.0f && suspension[5].value==-2.0f);
  CHECK(suspension[6].value==-3.0f && suspension[7].value==-3.0f);
  CHECK(suspension[8].value==4.0f && suspension[8].mode==5 && suspension[8].relative);
  CHECK(!zpatch::calculate_suspension_commands(false,5,0.0f,0.0f,0.0f,0.0f,0.0f,suspension));
  CHECK(!zpatch::calculate_suspension_commands(true,4,0.0f,0.0f,0.0f,0.0f,0.0f,suspension));
  CHECK(suspension[8].value==4.0f); // rejected commands retain previous data

  std::array<std::byte, 8> nop_target{};
  CHECK(zpatch::fill_nop({nop_target.data(), nop_target.size()}, 2, 4));
  for (std::size_t i = 0; i < nop_target.size(); ++i)
    CHECK((i >= 2 && i < 6) ? nop_target[i] == std::byte{0x90} : nop_target[i] == std::byte{0});
  CHECK(!zpatch::fill_nop({nop_target.data(), nop_target.size()}, 6, 3));
  CHECK(!zpatch::fill_nop({nop_target.data(), nop_target.size()}, 0, 0));
  std::array<std::byte, 8> jump_target{};
  const auto base_address = reinterpret_cast<std::uintptr_t>(jump_target.data());
  CHECK(zpatch::install_relative_jump({jump_target.data(), jump_target.size()}, 1,
      reinterpret_cast<const std::byte*>(base_address + 0x40)));
  CHECK(jump_target[1] == std::byte{0xe9});
  std::uint32_t encoded = 0;
  for (int i = 0; i < 4; ++i)
    encoded |= static_cast<std::uint32_t>(std::to_integer<unsigned>(jump_target[2 + static_cast<std::size_t>(i)])) << (8 * i);
  CHECK(base_address + 1 + 5 + encoded == base_address + 0x40);
  CHECK(!zpatch::install_relative_jump({jump_target.data(), jump_target.size()}, 4, nullptr));
  CHECK(!zpatch::install_relative_jump({jump_target.data(), jump_target.size()}, 5,
      reinterpret_cast<const std::byte*>(base_address + 0x40)));

  std::vector<std::byte> fusionfix(0x20e0,std::byte{0});
  CHECK(!zpatch::compatibility_module_lacks_signature(nullptr,fusionfix.size()));
  CHECK(!zpatch::compatibility_module_lacks_signature(fusionfix.data(),0x20d3));
  // The second comparison is a 32-bit compare of 0x00002446, so the high half matters.
  fusionfix[0x20d9]=std::byte{0x01};
  CHECK(zpatch::compatibility_module_lacks_signature(fusionfix.data(),fusionfix.size()));
  fusionfix[0x20d9]=std::byte{0x00};
  CHECK(zpatch::compatibility_module_lacks_signature(fusionfix.data(),fusionfix.size()));
  const std::uint32_t fusion_signature=0xc7c47589u;
  for(unsigned i=0;i<4;++i)fusionfix[0x20d3+i]=static_cast<std::byte>((fusion_signature>>(8u*i))&0xffu);
  fusionfix[0x20d7]=std::byte{0x46};fusionfix[0x20d8]=std::byte{0x24};
  CHECK(!zpatch::compatibility_module_lacks_signature(fusionfix.data(),fusionfix.size()));
  fusionfix[0x20d8]=std::byte{0x25};
  CHECK(zpatch::compatibility_module_lacks_signature(fusionfix.data(),fusionfix.size()));
  fusionfix[0x20d8]=std::byte{0x24};fusionfix[0x20d3]=std::byte{0x88};
  CHECK(zpatch::compatibility_module_lacks_signature(fusionfix.data(),fusionfix.size()));

  const zpatch::ProcessEntry processes[]={{"other.exe",5},{"PlayGTAIV.exe",4321},{"PlayGTAIV.exe",7}};
  CHECK(zpatch::select_game_process_id(processes,3,"PlayGTAIV.exe")==4321);
  CHECK(zpatch::select_game_process_id(processes,3,"playgtaiv.exe")==0);
  CHECK(zpatch::select_game_process_id(processes,0,"PlayGTAIV.exe")==0);
  CHECK(zpatch::select_game_process_id(nullptr,3,"PlayGTAIV.exe")==0);
#if !defined(_WIN32)
  CHECK(zpatch::find_game_process_id()==0);
#endif

  static std::size_t protect_calls = 0;
  static std::uint32_t protect_last_new = 0;
  static std::uint32_t protect_last_restore = 0;
  struct FakeProtect {
    static zpatch::Status protect(void*, std::size_t, std::uint32_t new_protect, std::uint32_t* previous) {
      if (protect_calls == 0) protect_last_new = new_protect;
      else protect_last_restore = new_protect;
      ++protect_calls; *previous = 0x04u; return {};
    }
  };
  std::array<std::byte,4> write_target{};
  const std::array<std::byte,1> byte_value{std::byte{0xAB}};
  CHECK(zpatch::protected_write(write_target.data(), byte_value.data(), 1, &FakeProtect::protect));
  CHECK(write_target[0]==std::byte{0xAB} && write_target[1]==std::byte{0});
  CHECK(protect_calls==2 && protect_last_new==0x40u && protect_last_restore==0x04u);
  const std::array<std::byte,4> dword_value{std::byte{1},std::byte{2},std::byte{3},std::byte{4}};
  CHECK(zpatch::protected_write(write_target.data(), dword_value.data(), 4, &FakeProtect::protect));
  CHECK(write_target[0]==std::byte{1} && write_target[3]==std::byte{4});
  CHECK(!zpatch::protected_write(write_target.data(), dword_value.data(), 3, &FakeProtect::protect));
  CHECK(!zpatch::protected_write(nullptr, dword_value.data(), 1, &FakeProtect::protect));

  std::array<std::byte,6> protected_target{std::byte{0},std::byte{0x11},std::byte{0x22},std::byte{0x33},std::byte{0},std::byte{0}};
  const std::array<std::byte,2> protected_expected{std::byte{0x11},std::byte{0x22}};
  const std::array<std::byte,2> protected_replacement{std::byte{0xAA},std::byte{0xBB}};
  protect_calls = 0;
  CHECK(zpatch::apply_protected_patch(zpatch::GameBuild::v1070, zpatch::GameBuild::v1070,
      protected_target.data(), 1, {protected_expected.data(), protected_expected.size()},
      {protected_replacement.data(), protected_replacement.size()}, &FakeProtect::protect));
  CHECK(protected_target[1]==std::byte{0xAA} && protected_target[2]==std::byte{0xBB});
  CHECK(protect_calls==4); // two protected writes, each toggling protection twice
  auto protected_mismatch = zpatch::apply_protected_patch(zpatch::GameBuild::v1070,
      zpatch::GameBuild::v1070, protected_target.data(), 1,
      {protected_expected.data(), protected_expected.size()},
      {protected_replacement.data(), protected_replacement.size()}, &FakeProtect::protect);
  CHECK(!protected_mismatch && protected_mismatch.code==zpatch::StatusCode::evidence_mismatch);
  CHECK(!zpatch::apply_protected_patch(zpatch::GameBuild::v1080, zpatch::GameBuild::v1070,
      protected_target.data(), 1, {protected_expected.data(), protected_expected.size()},
      {protected_replacement.data(), protected_replacement.size()}, &FakeProtect::protect));
  CHECK(!zpatch::apply_protected_patch(zpatch::GameBuild::v1070, zpatch::GameBuild::v1070,
      nullptr, 0, {protected_expected.data(), protected_expected.size()},
      {protected_replacement.data(), protected_replacement.size()}, &FakeProtect::protect));

  // Manifest over caller-owned bytes: match, gate, apply, rollback.
  std::array<std::byte,8> manifest_module{std::byte{0x48},std::byte{0x8B},std::byte{0x05},std::byte{0x90},
                                          std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
  const std::array<std::byte,2> manifest_expected{std::byte{0x8B},std::byte{0x05}};
  const std::array<std::byte,2> manifest_replacement{std::byte{0x90},std::byte{0x90}};
  const zpatch::PatchSite manifest_sites[1]={{ "patch_a", "48 8B 05", 1,
      {manifest_expected.data(), manifest_expected.size()},
      {manifest_replacement.data(), manifest_replacement.size()},
      (1u << static_cast<unsigned>(zpatch::GameBuild::v1070)), "" }};
  const zpatch::PatchManifestView manifest{manifest_sites, 1};
  std::vector<zpatch::PatchOutcome> manifest_outcomes;
  protect_calls = 0;
  CHECK(zpatch::apply_patch_manifest({manifest_module.data(), manifest_module.size()},
      zpatch::GameBuild::v1070, manifest, "", &FakeProtect::protect, manifest_outcomes));
  CHECK(manifest_outcomes.size()==1 && manifest_outcomes[0].status);
  CHECK(manifest_module[1]==std::byte{0x90} && manifest_module[2]==std::byte{0x90});
  CHECK(zpatch::rollback_patch_outcomes(zpatch::GameBuild::v1070, &FakeProtect::protect, manifest_outcomes));
  CHECK(manifest_module[1]==std::byte{0x8B} && manifest_module[2]==std::byte{0x05});
  // Build gate skips.
  CHECK(zpatch::apply_patch_manifest({manifest_module.data(), manifest_module.size()},
      zpatch::GameBuild::v1080, manifest, "", &FakeProtect::protect, manifest_outcomes));
  CHECK(manifest_outcomes.size()==1 && !manifest_outcomes[0].status);
  // Option gate skips.
  const zpatch::PatchSite gated_sites[1]={{ "patch_b", "48 8B 05", 1,
      {manifest_expected.data(), manifest_expected.size()},
      {manifest_replacement.data(), manifest_replacement.size()},
      (1u << static_cast<unsigned>(zpatch::GameBuild::v1070)), "OptionA" }};
  const zpatch::PatchManifestView gated{gated_sites, 1};
  CHECK(zpatch::apply_patch_manifest({manifest_module.data(), manifest_module.size()},
      zpatch::GameBuild::v1070, gated, "OptionB", &FakeProtect::protect, manifest_outcomes));
  CHECK(!manifest_outcomes[0].status && manifest_module[1]==std::byte{0x8B});
  CHECK(zpatch::apply_patch_manifest({manifest_module.data(), manifest_module.size()},
      zpatch::GameBuild::v1070, gated, "Other,OptionA", &FakeProtect::protect, manifest_outcomes));
  CHECK(manifest_outcomes[0].status && manifest_module[1]==std::byte{0x90});

  // Recovered option flags: confirmed bindings gate the vehicle-update routine.
  zpatch::ConfigDocument option_config;
  CHECK(zpatch::parse_config("[Options]\nMovingSteeringWheelSupport=0\nShakingEngineBlock=1\nMovingPlaneParts=2\n", option_config));
  zpatch::OptionFlagState option_state;
  CHECK(zpatch::apply_option_flags(option_config, option_state));
  CHECK(!zpatch::option_flag_enabled(option_state, 0x1015ad40u));
  CHECK(zpatch::option_flag_enabled(option_state, 0x1015ad41u));
  CHECK(!zpatch::option_flag_enabled(option_state, 0x10159c17u)); // only exactly 1 enables
  CHECK(option_state.bindings_applied==3);
  // DebugPrintSpawns and DrivebyFOVFix previously hashed to the same slot; with an
  // exact index table they must be independently readable.
  zpatch::ConfigDocument pair_config;
  CHECK(zpatch::parse_config("[Options]\nDebugPrintSpawns=1\nDrivebyFOVFix=0\n", pair_config));
  zpatch::OptionFlagState pair_state;
  CHECK(zpatch::apply_option_flags(pair_config, pair_state));
  CHECK(zpatch::option_flag_enabled(pair_state, 0x10159c15u));
  CHECK(!zpatch::option_flag_enabled(pair_state, 0x1015bdf4u));
  // The build slot is not an option binding.
  CHECK(!zpatch::option_flag_enabled(pair_state, 0x1015be04u));
  // Absent keys use the arm default: DoNotPauseOnMinimize's arm pushes "0"...
  CHECK(!zpatch::option_flag_enabled(option_state, 0x1015ad43u));
  // ...while the moving-parts trio's arms push "1".
  CHECK(zpatch::option_flag_enabled(pair_state, 0x1015ad40u));
  std::size_t binding_count = 0;
  const auto* bindings = zpatch::confirmed_option_flags(binding_count);
  CHECK(binding_count==10);
  for (std::size_t i = 0; i < binding_count; ++i) CHECK(bindings[i].confidence);

  // Loader arm config semantics: an absent key uses the recovered arm default
  // (usually "1"), the parsed value must be exactly 1, and keys whose arm
  // default is "0" stay off when absent.
  {
    zpatch::ConfigDocument absent;
    CHECK(zpatch::parse_config("[Options]\nMouseFix=1\n", absent));
    CHECK(zpatch::option_enabled(absent, "MouseFix"));
    CHECK(zpatch::option_enabled(absent, "SkipIntro"));              // arm default "1"
    CHECK(zpatch::option_enabled(absent, "BuildingDynamicShadows")); // arm default "1"
    CHECK(!zpatch::option_enabled(absent, "CarDynamicShadowFix")); // arm default "0"
    CHECK(!zpatch::option_enabled(absent, "VSyncFix"));              // arm default "0"
    CHECK(!zpatch::option_enabled(absent, "RemoveRGSCLoginRequirement"));
    CHECK(!zpatch::option_enabled(absent, "NoSuchOption"));
    zpatch::ConfigDocument two;
    CHECK(zpatch::parse_config("[Options]\nMouseFix=2\nSkipIntro=2\nVSyncFix=2\n", two));
    CHECK(!zpatch::option_enabled(two, "MouseFix"));
    CHECK(!zpatch::option_enabled(two, "SkipIntro"));
    CHECK(!zpatch::option_enabled(two, "VSyncFix"));
    zpatch::ConfigDocument one;
    CHECK(zpatch::parse_config("[Options]\nVSyncFix=1\n", one));
    CHECK(zpatch::option_enabled(one, "VSyncFix"));
  }

  // Recovered patch sites from option handlers: every entry must be a valid
  // signature the matcher accepts, and jump sites must carry a target.
  std::size_t site_count = 0;
  const auto* sites = zpatch::recovered_patch_sites(site_count);
  CHECK(site_count>=61 && sites);
  const std::array<std::byte, 64> probe{};
  for (std::size_t i = 0; i < site_count; ++i) {
    CHECK(sites[i].option.size() > 0 && sites[i].signature.size() > 0);
    zpatch::Match probe_match;
    const auto status = zpatch::find_pattern({probe.data(), probe.size()}, sites[i].signature, probe_match);
    CHECK(status.code == zpatch::StatusCode::not_found ||
          status.code == zpatch::StatusCode::ok);   // never parse_error
    if (sites[i].targets > 0) CHECK(sites[i].target != 0);
  }

  // Apply a recovered operation over a synthetic module containing its signature.
  {
    std::size_t op_count = 0;
    const auto* ops = zpatch::recovered_patch_ops(op_count);
    CHECK(op_count > 0);
    // Build a module that contains the first jump-site signature at a known offset.
    const auto& first = ops[0];  // RecoveredPatchOp has signature/offset/replacement
    std::vector<std::byte> apply_module(0x200, std::byte{0xcc});
    // decode the signature bytes (wildcards -> 0x00) into the module
    std::size_t decoded = 0, cursor = 0;
    while (cursor < first.signature.size() && decoded < 0x40) {
      const auto space = first.signature.find(' ', cursor);
      const auto token = first.signature.substr(
          cursor, space == std::string_view::npos ? std::string_view::npos : space - cursor);
      const char t0 = token.empty()?'?':token[0];
      const char t1 = token.size()>1?token[1]:token[0];
      const int high = t0=='?'?-1:std::stoi(std::string(1,t0),nullptr,16);
      const int low = t1=='?'?-1:std::stoi(std::string(1,t1),nullptr,16);
      apply_module[0x80 + decoded] = static_cast<std::byte>((std::max(high,0)<<4)|std::max(low,0));
      ++decoded;
      if (space == std::string_view::npos) break;
      cursor = space + 1;
    }
    std::vector<zpatch::RecoveredApplyOutcome> outcomes;
    protect_calls = 0;
    CHECK(zpatch::apply_recovered_operations({apply_module.data(), apply_module.size()},
        first.option, &FakeProtect::protect, outcomes));
    CHECK(outcomes.size()==1 && outcomes[0].status);
    CHECK(outcomes[0].address==apply_module.data()+0x80+first.offset);
    CHECK(protect_calls>=2);
    // A disabled option applies nothing.
    protect_calls = 0;
    CHECK(zpatch::apply_recovered_operations({apply_module.data(), apply_module.size()},
        "SomeUnrelatedOption", &FakeProtect::protect, outcomes));
    CHECK(outcomes.empty() && protect_calls==0);
  }

  // Negative-offset operation (SkipIntro-style): write lands before the match.
  {
    const std::string_view neg_sig = "89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50";
    std::vector<std::byte> neg_module(0x200, std::byte{0xcc});
    // place signature at offset 0x80
    std::size_t decoded = 0, cursor = 0;
    while (cursor < neg_sig.size() && decoded < 0x20) {
      const auto space = neg_sig.find(' ', cursor);
      const auto tok = neg_sig.substr(cursor, space==std::string_view::npos?std::string_view::npos:space-cursor);
      const char c0 = tok.empty()?'?':tok[0];
      const char c1 = tok.size()>1?tok[1]:tok[0];
      const int hi = c0=='?'?-1:std::stoi(std::string(1,c0),nullptr,16);
      const int lo = c1=='?'?-1:std::stoi(std::string(1,c1),nullptr,16);
      neg_module[0x80+decoded]=static_cast<std::byte>((std::max(hi,0)<<4)|std::max(lo,0));
      ++decoded; if(space==std::string_view::npos)break; cursor=space+1;
    }
    std::vector<zpatch::RecoveredApplyOutcome> neg_out;
    protect_calls = 0;
    CHECK(zpatch::apply_recovered_operations({neg_module.data(), neg_module.size()},
        "SkipIntro", &FakeProtect::protect, neg_out));
    // SkipIntro has one -6 signature op and one +? op; the -6 one must write at 0x80-6.
    bool saw_negative = false;
    for (const auto& o : neg_out) if (o.status && o.address==neg_module.data()+0x80-6) saw_negative=true;
    CHECK(saw_negative);
  }
  // Module-relative operation: bounds guard rejects an rva beyond the module.
  {
    std::vector<std::byte> rva_module(0x100, std::byte{0});
    std::vector<zpatch::RecoveredApplyOutcome> rva_out;
    CHECK(zpatch::apply_recovered_operations({rva_module.data(), rva_module.size()},
        "Patch6and7RemoveModChecks", &FakeProtect::protect, rva_out));
    for (const auto& o : rva_out)
      if (!o.status) CHECK(o.status.detail.find("rva outside module")!=std::string_view::npos);
  }

  // Recovered build gates. Head gates (`cmp [0x1015be04],N; jl`) and per-build
  // branch tables are modelled as enable/disable masks; see
  // recovered_build_gates.hpp. The assertions below check the table values.
  {
    bool known = false;
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1070, "LoadDLCs", known));
    CHECK(known);
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1061,
                                     "EpisodicVehicleSupport", known));
    CHECK(known);
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1100,
                                     "EpisodicWeaponSupport", known));
    CHECK(known);
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v12043,
                                     "TBoGTHeliHeightLimit", known));
    CHECK(known);
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1061, "LoadDLCs", known));
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1100, "LoadDLCs", known));
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v12043, "LoadDLCs", known));
    CHECK(known);
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1060, "LoadDLCs", known));
    // v1000 ordinal 1 is not in LoadDLCs' {7,8,9} gate.
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1000, "LoadDLCs", known));
    CHECK(known);
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1080, "EpisodicVehicleSupport", known));
    CHECK(known);
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1000, "EpisodicVehicleSupport", known));
    // The episodic cluster (and TBoGTHeliHeightLimit) gates on the handler's
    // head `cmp [0x1015be04],6; jl` -> every ordinal >= 6.
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1060, "EpisodicVehicleSupport", known));
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1061, "EpisodicVehicleSupport", known));
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1060, "EpisodicWeaponSupport", known));
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1000, "EpisodicWeaponSupport", known));
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1080, "PoliceEpisodicWeaponSupport", known));
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1040, "PipeBombDropIconSupport", known));
    // RemoveRGSCLoginRequirement has no option gate (the build-9 equality is the
    // fallback sub-branch, carried by the rva install's own build field).
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1000, "RemoveRGSCLoginRequirement", known));
    CHECK(!known);
    // No recovered gate -> allowed and reported unknown.
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1000, "SkipIntro", known));
    CHECK(!known);
    // DualVehicleHeadlights: the handler's build check selects the E9
    // destination, so the two install rows carry explicit masks instead.
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1080, "DualVehicleHeadlights@new", known));
    CHECK(known);
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1000, "DualVehicleHeadlights@new", known));
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1000, "DualVehicleHeadlights@old", known));
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1040, "DualVehicleHeadlights@old", known));
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1004, "DualVehicleHeadlights@old", known));
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1070, "DualVehicleHeadlights@old", known));
    // CarDynamicShadowFix@noE: the C+0x3E NOP fill runs where pattern E is
    // missing (E exists only on 1.0.0.4).
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1000, "CarDynamicShadowFix@noE", known));
    CHECK(zpatch::build_allows_option(zpatch::GameBuild::v1040, "CarDynamicShadowFix@noE", known));
    CHECK(!zpatch::build_allows_option(zpatch::GameBuild::v1004, "CarDynamicShadowFix@noE", known));
    std::size_t gate_count = 0;
    CHECK(zpatch::recovered_build_gates(gate_count) && gate_count==16);
  }

  // Build-gated apply: an option whose gate excludes the build performs nothing.
  {
    std::size_t op_count = 0;
    const auto* ops = zpatch::recovered_patch_ops(op_count);
    const auto& first = ops[0];
    std::vector<std::byte> gated_module(0x200, std::byte{0xcc});
    std::size_t decoded = 0, cursor = 0;
    while (cursor < first.signature.size() && decoded < 0x40) {
      const auto sp = first.signature.find(' ', cursor);
      const auto tk = first.signature.substr(cursor, sp==std::string_view::npos?std::string_view::npos:sp-cursor);
      const char c0=tk.empty()?'?':tk[0]; const char c1=tk.size()>1?tk[1]:tk[0];
      const int hi=c0=='?'?-1:std::stoi(std::string(1,c0),nullptr,16);
      const int lo=c1=='?'?-1:std::stoi(std::string(1,c1),nullptr,16);
      gated_module[0x80+decoded]=static_cast<std::byte>((std::max(hi,0)<<4)|std::max(lo,0));
      ++decoded; if(sp==std::string_view::npos)break; cursor=sp+1;
    }
    std::vector<zpatch::RecoveredApplyOutcome> gated_out;
    // EpisodicVehicleSupport is gated to builds >= 6 (ordinal 1 is excluded).
    CHECK(zpatch::apply_recovered_operations_for_build({gated_module.data(), gated_module.size()},
        zpatch::GameBuild::v1000, "EpisodicVehicleSupport", &FakeProtect::protect, gated_out));
    CHECK(gated_out.empty());
    std::vector<zpatch::RecoveredApplyOutcome> allowed_out;
    CHECK(zpatch::apply_recovered_operations_for_build({gated_module.data(), gated_module.size()},
        zpatch::GameBuild::v1070, "EpisodicVehicleSupport", &FakeProtect::protect, allowed_out));
  }

  // Loader-preamble resolutions and fallback salts.
  {
    std::size_t preamble_count = 0;
    const auto* entries = zpatch::recovered_preamble_entries(preamble_count);
    CHECK(preamble_count==17 && entries);
    std::size_t resolves = 0, salts = 0;
    for (std::size_t i = 0; i < preamble_count; ++i) {
      CHECK(!entries[i].signature.empty());
      if (entries[i].kind == 0) { ++resolves; CHECK(entries[i].global >= 0x10100000u); }
      else { ++salts; CHECK(entries[i].value != 0); }
    }
    CHECK(resolves==7 && salts==10);
  }

  // Mapped-image adapter: signature-relative and module-RVA writes.
  {
    std::vector<std::byte> image(0x4000, std::byte{0xcc});
    // place a known signature at 0x1000
    const std::string_view sig = "48 8B 05 90 01";
    std::size_t at = 0x1000;
    for (std::size_t i = 0; i < 5; ++i) image[at + i] = std::byte{0x48 + 0}; // placeholder
    image[at+0]=std::byte{0x48};image[at+1]=std::byte{0x8b};image[at+2]=std::byte{0x05};
    image[at+3]=std::byte{0x90};image[at+4]=std::byte{0x01};
    const zpatch::MappedImage mapped{image.data(), image.size()};
    std::byte* where = nullptr; std::size_t wrote = 0;
    CHECK(zpatch::apply_mapped_operation(mapped, sig, -3, 0, "AA BB", where, wrote));
    CHECK(wrote == 2 && where == image.data() + at - 3);
    CHECK(image[at-3]==std::byte{0xAA} && image[at-2]==std::byte{0xBB});
    // module-RVA write
    CHECK(zpatch::apply_mapped_operation(mapped, "", 0, 0x2000, "E9", where, wrote));
    CHECK(wrote == 1 && where == image.data() + 0x2000 && image[0x2000]==std::byte{0xE9});
    // out-of-range RVA rejected
    CHECK(!zpatch::apply_mapped_operation(mapped, "", 0, 0x4000, "E9", where, wrote));
    CHECK(!zpatch::apply_mapped_operation(mapped, "", 0, 0x3FFF, "AA BB CC", where, wrote));
    // negative offset past the start rejected
    CHECK(!zpatch::apply_mapped_operation(mapped, sig, -0x2000, 0, "AA", where, wrote));
    // missing signature reported as not found
    const auto missing = zpatch::apply_mapped_operation(mapped, "DE AD BE EF", 0, 0, "AA", where, wrote);
    CHECK(!missing && missing.code == zpatch::StatusCode::not_found);
  }

  // Recovered Nth-match literal writes (StartupTimeFix, previously 0 op rows).
  {
    std::size_t count = 0;
    const auto* ops = zpatch::recovered_match_literals(count);
    CHECK(count == 3 && ops != nullptr);
    const zpatch::RecoveredMatchLiteral* startup = nullptr;
    std::size_t noImpostor = 0;
    for (std::size_t i = 0; i < count; ++i) {
      if (ops[i].option == "StartupTimeFix") startup = &ops[i];
      if (ops[i].option == "NoImpostorHeightCutoff") ++noImpostor;
    }
    CHECK(startup != nullptr);
    CHECK(startup->match_index == -1);        // every match
    CHECK(startup->offset == 6 && startup->width == 4 && startup->value == 0x101580d8u);
    CHECK(noImpostor == 2);                    // loader-inline word NOPs at match 0 and 1

    // Place two copies of the signature; every copy gets the dword at +6.
    const std::string sig(startup->signature);
    std::vector<std::byte> image(0x4000, std::byte{0xcc});
    const auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const auto place = [&](std::size_t at) {
      std::size_t cursor = 0;
      while (cursor < sig.size()) {
        const auto sp = sig.find(' ', cursor);
        const auto tk = sig.substr(
            cursor, sp == std::string::npos ? std::string::npos : sp - cursor);
        const int hi = tk[0] == '?' ? 0 : nib(tk[0]);
        const int lo = tk.size() > 1 && tk[1] != '?' ? nib(tk[1]) : hi;
        image[at++] = static_cast<std::byte>((hi << 4) | lo);
        if (sp == std::string::npos) break;
        cursor = sp + 1;
      }
    };
    place(0x1000); place(0x2000);
    zpatch::MappedImage mapped{image.data(), image.size()};
    std::size_t written = 0;
    CHECK(zpatch::apply_recovered_match_literals(mapped, 8, written));
    CHECK(written == 2);                       // both matches patched
    for (std::size_t at : {0x1000u + 6u, 0x2000u + 6u}) {
      std::uint32_t v = 0;
      std::memcpy(&v, image.data() + at, 4);
      CHECK(v == 0x101580d8u);
    }
  }

  // Recovered signature-relative literal stores (5 options the op table missed).
  {
    std::size_t count = 0;
    const auto* stores = zpatch::recovered_literal_stores(count);
    CHECK(count == 9 && stores != nullptr);
    std::size_t ptrNodes = 0, vsync = 0, noLod = 0;
    for (std::size_t i = 0; i < count; ++i) {
      CHECK(stores[i].width >= 1 && stores[i].width <= 4);
      if (stores[i].option == "IncreasePtrNodes") ++ptrNodes;
      if (stores[i].option == "VSyncFix") ++vsync;
      if (stores[i].option == "NoLODLightHeightCutoff") ++noLod;
    }
    CHECK(ptrNodes == 3 && vsync == 2 && noLod == 1);
    // IncreaseVehicleStructLimit changes `push 50` to `push 100` at match+6.
    const zpatch::RecoveredLiteralStore structLimit{"IncreaseVehicleStructLimit",
        "68 ? ? ? ? 6A 32 8B C8", 6, 1, 0x64u};
    std::vector<std::byte> image(0x2000, std::byte{0xcc});
    // Place the signature (with wildcards filled) at 0x1000.
    const std::size_t at = 0x1000;
    image[at+0] = std::byte{0x68}; image[at+1]=std::byte{0}; image[at+2]=std::byte{0};
    image[at+3] = std::byte{0}; image[at+4]=std::byte{0}; image[at+5]=std::byte{0x6a};
    image[at+6] = std::byte{0x32}; image[at+7]=std::byte{0x8b}; image[at+8]=std::byte{0xc8};
    zpatch::MappedImage mapped{image.data(), image.size()};
    std::byte* where = nullptr;
    CHECK(zpatch::apply_literal_store(mapped, structLimit, where));
    CHECK(where == image.data() + at + 6);
    CHECK(image[at+6] == std::byte{0x64});     // 0x32 -> 0x64 (50 -> 100)
    // Offset one past the placed prefix resolves; absent signature reported.
    const zpatch::RecoveredLiteralStore missing{"X", "DE AD BE EF", 0, 1, 0};
    CHECK(zpatch::apply_literal_store(mapped, missing, where).code
          == zpatch::StatusCode::not_found);
    std::size_t applied = 0;
    CHECK(zpatch::apply_recovered_literal_stores(mapped, 8, applied));
    CHECK(applied <= count);
  }

  // Recovered Nth-match jump installs (BenchmarkFix, previously 0 op rows).
  {
    std::size_t count = 0;
    const auto* jumps = zpatch::recovered_match_jumps(count);
    CHECK(count == 4 && jumps != nullptr);
    CHECK(jumps[0].option == "BenchmarkFix" && jumps[1].option == "BenchmarkFix");
    CHECK(jumps[2].option == "BorderlessWindowed" && jumps[2].opcode == 0xE8u);
    CHECK(jumps[3].option == "BorderlessWindowed" && jumps[3].opcode == 0xE8u);
    CHECK(jumps[3].destination == 0x10005900u);
    CHECK(jumps[0].match_index == 0 && jumps[1].match_index == 1);
    CHECK(jumps[0].offset == 8 && jumps[1].offset == 8);
    CHECK(jumps[0].destination == 0x10005650u && jumps[1].destination == 0x10005670u);

    // Place TWO copies of the signature: match[0] at 0x1000, match[1] at 0x2000.
    const std::string sig(jumps[0].signature);
    std::vector<std::byte> image(0x4000, std::byte{0xcc});
    const auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const auto place = [&](std::size_t at) {
      std::size_t cursor = 0;
      while (cursor < sig.size()) {
        const auto sp = sig.find(' ', cursor);
        const auto tk = sig.substr(
            cursor, sp == std::string::npos ? std::string::npos : sp - cursor);
        const int hi = tk[0] == '?' ? 0 : nib(tk[0]);
        const int lo = tk.size() > 1 && tk[1] != '?' ? nib(tk[1]) : hi;
        image[at++] = static_cast<std::byte>((hi << 4) | lo);
        if (sp == std::string::npos) break;
        cursor = sp + 1;
      }
    };
    place(0x1000); place(0x2000);
    std::vector<const std::byte*> hits;
    CHECK(zpatch::find_all_patterns({image.data(), image.size()}, sig, hits) == 2);
    CHECK(hits[0] == image.data() + 0x1000 && hits[1] == image.data() + 0x2000);

    constexpr std::uintptr_t kGameBase = 0x400000u;
    zpatch::MappedImage mapped{image.data(), image.size()};
    std::size_t applied = 0;
    CHECK(zpatch::apply_recovered_match_jumps(mapped, kGameBase, 8, applied));
    CHECK(applied == 2);
    CHECK(image[0x1008] == std::byte{0xE9});     // match[0] + 8
    CHECK(image[0x2008] == std::byte{0xE9});     // match[1] + 8
    std::int32_t d0 = 0, d1 = 0;
    std::memcpy(&d0, image.data() + 0x1008 + 1, 4);
    std::memcpy(&d1, image.data() + 0x2008 + 1, 4);
    CHECK(d0 == static_cast<std::int32_t>(0x10005650u - (kGameBase + 0x1008u + 5u)));
    CHECK(d1 == static_cast<std::int32_t>(0x10005670u - (kGameBase + 0x2008u + 5u)));

    // BorderlessWindowed installs an E8 call (not E9) at match+0.
    const std::string ew(jumps[2].signature);
    std::vector<std::byte> bimage(0x4000, std::byte{0xcc});
    const auto place2 = [&](std::vector<std::byte>& im, const std::string& sg, std::size_t at) {
      std::size_t cursor = 0;
      while (cursor < sg.size()) {
        const auto sp = sg.find(' ', cursor);
        const auto tk = sg.substr(
            cursor, sp == std::string::npos ? std::string::npos : sp - cursor);
        const int hi = tk[0] == '?' ? 0 : nib(tk[0]);
        const int lo = tk.size() > 1 && tk[1] != '?' ? nib(tk[1]) : hi;
        im[at++] = static_cast<std::byte>((hi << 4) | lo);
        if (sp == std::string::npos) break;
        cursor = sp + 1;
      }
    };
    place2(bimage, ew, 0x1000); place2(bimage, ew, 0x2000);
    zpatch::MappedImage bmapped{bimage.data(), bimage.size()};
    std::size_t bapplied = 0;
    CHECK(zpatch::apply_recovered_match_jumps(bmapped, kGameBase, 8, bapplied));
    CHECK(bapplied == 2);                                  // the 2 borderless E8 installs
    CHECK(bimage[0x1000] == std::byte{0xE8});              // E8 at match[0]
    CHECK(bimage[0x2000] == std::byte{0xE8});              // E8 at match[1]
  }

  // Recovered indirect-literal writes (NoPhysicsSleep, IncreaseVehicleModelLimit).
  {
    std::size_t count = 0;
    const auto* ops = zpatch::recovered_indirect_writes(count);
    CHECK(count == 2 && ops != nullptr);
    const zpatch::RecoveredIndirectWrite* noSleep = nullptr;
    const zpatch::RecoveredIndirectWrite* modelLimit = nullptr;
    for (std::size_t i = 0; i < count; ++i) {
      CHECK(ops[i].width >= 1 && ops[i].width <= 4);
      if (ops[i].option == "NoPhysicsSleep") noSleep = &ops[i];
      if (ops[i].option == "IncreaseVehicleModelLimit") modelLimit = &ops[i];
    }
    CHECK(noSleep != nullptr && modelLimit != nullptr);
    CHECK(noSleep->pointer_offset == -38 && noSleep->width == 4 && noSleep->value == 0x0u);
    CHECK(modelLimit->pointer_offset == 77 && modelLimit->value == 0x400u);

    // Build an image: place the signature at 0x1000; the pointer word at
    // match+offset holds a game VA into the image; the write lands there.
    constexpr std::uintptr_t kGameBase = 0x400000u;
    std::vector<std::byte> image(0x8000, std::byte{0xcc});
    const auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    const auto place = [&](const std::string& sig, std::size_t at) {
      std::size_t cursor = 0;
      while (cursor < sig.size()) {
        const auto sp = sig.find(' ', cursor);
        const auto tk = sig.substr(
            cursor, sp == std::string::npos ? std::string::npos : sp - cursor);
        const int hi = tk[0] == '?' ? 0 : nib(tk[0]);
        const int lo = tk.size() > 1 && tk[1] != '?' ? nib(tk[1]) : hi;
        image[at++] = static_cast<std::byte>((hi << 4) | lo);
        if (sp == std::string::npos) break;
        cursor = sp + 1;
      }
    };
    place(std::string(noSleep->signature), 0x1000);
    // pointer word at match + pointer_offset (0x1000 - 38 = 0xFDA)
    const std::uint32_t target_va = static_cast<std::uint32_t>(kGameBase + 0x2000);
    std::memcpy(image.data() + 0x1000 + noSleep->pointer_offset, &target_va, 4);
    zpatch::MappedImage mapped{image.data(), image.size()};
    std::byte* where = nullptr;
    CHECK(zpatch::apply_indirect_write(mapped, kGameBase, *noSleep, where));
    CHECK(where == image.data() + 0x2000);
    CHECK(image[0x2000] == std::byte{0x00} && image[0x2001] == std::byte{0x00});

    // Out-of-image target pointer is rejected, not followed.
    std::uint32_t bad_va = 0x1000;
    std::memcpy(image.data() + 0x1000 + noSleep->pointer_offset, &bad_va, 4);
    const auto bad = zpatch::apply_indirect_write(mapped, kGameBase, *noSleep, where);
    CHECK(!bad && bad.code == zpatch::StatusCode::invalid_input);

    // Gated application counts the candidates whose gate passes.
    std::size_t applied = 0;
    CHECK(zpatch::apply_recovered_indirect_writes(mapped, kGameBase, 9, applied));
    CHECK(applied <= count);
  }

  // Recovered signature-relative jump installs (E9 + rel32 completed).
  {
    std::size_t count = 0;
    const auto* jumps = zpatch::recovered_sig_jumps(count);
    CHECK(count == 31 && jumps != nullptr);
    std::size_t driveby = 0, noLivery = 0, carParts = 0;
    for (std::size_t i = 0; i < count; ++i) {
      CHECK(!jumps[i].option.empty() && !jumps[i].signature.empty());
      CHECK(jumps[i].destination >= 0x10000000u && jumps[i].destination < 0x10200000u);
      if (jumps[i].option == "DrivebyFOVFix") ++driveby;
      if (jumps[i].option == "NoLiveryLimit") ++noLivery;
      if (jumps[i].option == "CarPartsShadowFix") ++carParts;
    }
    CHECK(driveby == 1 && noLivery == 3);   // loader-inline + handler installs
    CHECK(carParts == 1);                   // runtime-built pattern, site match-6
    // AimProjectileCrashFix: signature-relative, offset 0 -> 0x10005590.
    const zpatch::RecoveredSigJump aim{"AimProjectileCrashFix",
        "8B 44 24 08 89 41 20 8B 44 24 0C 89 41 24 F7 D8", 0, 0x10005590u};
    std::vector<std::byte> image(0x3000, std::byte{0xcc});
    // Place the signature verbatim at 0x1000.
    std::size_t at = 0x1000, cursor = 0;
    const auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    while (cursor < aim.signature.size()) {
      const auto sp = aim.signature.find(' ', cursor);
      const auto tk = aim.signature.substr(
          cursor, sp == std::string_view::npos ? std::string_view::npos : sp - cursor);
      image[at++] = static_cast<std::byte>((nib(tk[0]) << 4) | nib(tk[1]));
      if (sp == std::string_view::npos) break;
      cursor = sp + 1;
    }
    constexpr std::uintptr_t kGameBase = 0x400000u;
    zpatch::MappedImage mapped{image.data(), image.size()};
    std::byte* where = nullptr;
    CHECK(zpatch::apply_sig_jump(mapped, kGameBase, aim, where));
    CHECK(where == image.data() + 0x1000);           // offset 0
    CHECK(image[0x1000] == std::byte{0xE9});
    std::int32_t disp = 0;
    std::memcpy(&disp, image.data() + 0x1000 + 1, 4);
    CHECK(disp == static_cast<std::int32_t>(0x10005590u - (kGameBase + 0x1000u + 5u)));
    // Absent signature is reported, not a crash.
    const zpatch::RecoveredSigJump missing{"X", "DE AD BE EF", 0, 0x10000000u};
    const auto notfound = zpatch::apply_sig_jump(mapped, kGameBase, missing, where);
    CHECK(!notfound && notfound.code == zpatch::StatusCode::not_found);
    // Gated application: no signature present -> nothing fails, applied still counts
    // the gated-in candidates.
    std::size_t applied = 0;
    CHECK(zpatch::apply_recovered_sig_jumps(mapped, kGameBase, 8, applied));
    CHECK(applied > 0);
  }

  // Recovered module-RVA relative jumps (SuperLODFix, LoadDLCs, RemoveRGSC).
  {
    std::size_t count = 0;
    const auto* jumps = zpatch::recovered_rva_jumps(count);
    CHECK(count == 9 && jumps != nullptr);
    // SuperLODFix's two installs (previously recovered through 0 op rows).
    std::size_t super = 0, dlc = 0, rgsc = 0;
    for (std::size_t i = 0; i < count; ++i) {
      CHECK(jumps[i].module_rva > 0 && jumps[i].module_rva < 0xee8000u);
      CHECK(jumps[i].destination >= 0x10000000u && jumps[i].destination < 0x12000000u);
      CHECK(jumps[i].opcode == 0xE9u || jumps[i].opcode == 0xE8u);
      if (jumps[i].option == "SuperLODFix") ++super;
      if (jumps[i].option == "LoadDLCs") ++dlc;
      if (jumps[i].option == "RemoveRGSCLoginRequirement") ++rgsc;
    }
    CHECK(super == 2 && dlc == 6 && rgsc == 1);

    // Applying over a caller-owned image writes E9 + rel32 to the plugin VA.
    std::vector<std::byte> image(0x600000, std::byte{0xcc});
    zpatch::MappedImage mapped{image.data(), image.size()};
    zpatch::RecoveredRvaJump sample{"SuperLODFix", 0x4a3ce9u, 0xE9u, 0x100469e0u, 0x10046a10u};
    std::byte* where = nullptr;
    constexpr std::uintptr_t kGameBase = 0x400000u;   // GTAIV.exe image base
    CHECK(zpatch::apply_rva_jump(mapped, kGameBase, sample, where));
    CHECK(where == image.data() + 0x4a3ce9u);
    CHECK(image[0x4a3ce9u] == std::byte{0xE9});
    std::int32_t disp = 0;
    std::memcpy(&disp, image.data() + 0x4a3ce9u + 1, 4);
    // Same arithmetic the handler performs with GetModuleHandleA(0)+rva.
    CHECK(disp == static_cast<std::int32_t>(0x100469e0u - (kGameBase + 0x4a3ce9u + 5u)));

    // Out-of-range RVA is rejected without writing.
    zpatch::RecoveredRvaJump bad{"X", 0x600000u, 0xE9u, 0x100469e0u, 0};
    CHECK(!zpatch::apply_rva_jump(mapped, kGameBase, bad, where));

    // Per-build gating: each row is attributed to the handler branch that writes
    // it (LoadDLCs build 7 -> 0x4144D4 E9 + 0x4137CD E8; build 9 -> 0x4903D4 E9
    // + 0x48F558 E8; SuperLODFix build 9; RemoveRGSC's build-9 fallback E9).
    std::vector<std::byte> image2(image.size(), std::byte{0xcc});
    zpatch::MappedImage mapped2{image2.data(), image2.size()};
    std::size_t applied = 0;
    CHECK(zpatch::apply_recovered_rva_jumps(mapped2, kGameBase, 7, applied));
    CHECK(applied == 2);  // LoadDLCs' two build-7 rows
    CHECK(image2[0x4144d4u] == std::byte{0xE9});
    CHECK(image2[0x4137cdu] == std::byte{0xE8});
    image2.assign(image2.size(), std::byte{0xcc});
    CHECK(zpatch::apply_recovered_rva_jumps(mapped2, kGameBase, 9, applied));
    CHECK(applied == 4);  // LoadDLCs' two build-9 rows + SuperLODFix + RemoveRGSC fallback
    CHECK(image2[0x4903d4u] == std::byte{0xE9});
    CHECK(image2[0x48f558u] == std::byte{0xE8});
    CHECK(image2[0x540e19u] == std::byte{0xE9});
    CHECK(image2[0x498460u] == std::byte{0xE9});
  }

  // Recovered extra signatures for the 15 extractor-gap options.
  {
    std::size_t extra_count = 0;
    const auto* extra = zpatch::recovered_extra_signatures(extra_count);
    CHECK(extra_count == 24 && extra != nullptr);
    std::size_t distinct_options = 0;
    for (std::size_t i = 0; i < extra_count; ++i) {
      CHECK(!extra[i].option.empty() && !extra[i].signature.empty());
      CHECK(extra[i].token_count >= 1);
      if (i == 0 || extra[i].option != extra[i - 1].option) ++distinct_options;
      // token_count matches the shown signature
      std::size_t tokens = 1;
      for (char ch : extra[i].signature) if (ch == ' ') ++tokens;
      CHECK(tokens == extra[i].token_count);
    }
    CHECK(distinct_options == 15);

    // Scanning a caller-owned image finds each signature placed verbatim.
    std::vector<std::byte> image(0x4000, std::byte{0xcc});
    const auto& first = extra[0];             // BenchmarkFix, 16 tokens
    // Build the byte sequence from the signature (no wildcards in this one).
    std::size_t at = 0x1000, cursor = 0;
    while (cursor < first.signature.size()) {
      const auto sp = first.signature.find(' ', cursor);
      const auto tk = first.signature.substr(
          cursor, sp == std::string_view::npos ? std::string_view::npos : sp - cursor);
      const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      int hi = nib(tk[0]), lo = tk.size() > 1 ? nib(tk[1]) : hi;
      if (hi < 0 || lo < 0) { hi = 0; lo = 0; }   // wildcard: any value works here
      image[at++] = static_cast<std::byte>((hi << 4) | lo);
      if (sp == std::string_view::npos) break;
      cursor = sp + 1;
    }
    std::vector<zpatch::ExtraSignatureHit> hits;
    const std::size_t found =
        zpatch::scan_extra_signatures({image.data(), image.size()}, hits);
    CHECK(found >= 1 && hits.size() == extra_count);
    CHECK(hits[0].found && hits[0].address == image.data() + 0x1000);
    // A signature with a wildcard is only matched by its fixed bytes, so the
    // unmatched ones report not-found rather than a false positive.
    std::size_t not_found = 0;
    for (const auto& h : hits) if (!h.found) ++not_found;
    CHECK(not_found == extra_count - found);
  }

  // Build-probe salt semantics: matched -> no substitution, missed -> ten salts.
  {
    const zpatch::SaltSubstitution* subs = nullptr;
    CHECK(zpatch::preamble_salt_substitutions(true, subs) == 0);
    CHECK(subs == nullptr);
    const auto n = zpatch::preamble_salt_substitutions(false, subs);
    CHECK(n == 10 && subs != nullptr);
    std::uint32_t first_value = 0;
    for (std::size_t i = 0; i < n; ++i) {
      CHECK(subs[i].global >= 0x10100000u && subs[i].value != 0u);
      if (subs[i].global == 0x10158054u) first_value = subs[i].value;
    }
    CHECK(first_value == 0x6b86410eu);
  }

  // Recovered IVMenuAPI integration.
  {
    CHECK(!zpatch::zmenu_version_accepted(0x35DD0u));
    CHECK(zpatch::zmenu_version_accepted(0x35DD1u));
    CHECK(zpatch::zmenu_version_accepted(0x35DD2u));
    CHECK(std::strcmp(zpatch::kZMenuModuleName, "ZMenuIV.asi") == 0);
    CHECK(std::strcmp(zpatch::kZMenuProcName, "GetZMenuVersion") == 0);
    CHECK(zpatch::kZMenuRejectFlags == 0x10u);
    CHECK(zpatch::kZMenuTerminateExitCode == 0u);
#if defined(_WIN32)
    // Exercise the real GetModuleHandle/GetProcAddress guard path against a
    // test-only DLL named exactly as the original dependency, without invoking
    // MessageBoxA or TerminateProcess.
    CHECK(GetModuleHandleA(zpatch::kZMenuModuleName) == nullptr);
    std::uint32_t guard_version = 99u;
    CHECK(zpatch::zmenu_version_guard(guard_version) ==
          zpatch::ZMenuGuardResult::module_absent);
    char exe_path[MAX_PATH] = {};
    const DWORD exe_len = GetModuleFileNameA(nullptr, exe_path, MAX_PATH);
    CHECK(exe_len != 0u && exe_len < MAX_PATH);
    if (exe_len != 0u && exe_len < MAX_PATH) {
      char* slash = std::strrchr(exe_path, '\\');
      CHECK(slash != nullptr);
      if (slash != nullptr) {
        const std::size_t dir_bytes =
            static_cast<std::size_t>((slash + 1) - exe_path);
        const char fake_name[] = "ZMenuIV.asi";
        CHECK(dir_bytes + sizeof(fake_name) <= sizeof(exe_path));
        if (dir_bytes + sizeof(fake_name) <= sizeof(exe_path)) {
          std::memcpy(exe_path + dir_bytes, fake_name, sizeof(fake_name));
          HMODULE fake = LoadLibraryA(exe_path);
          CHECK(fake != nullptr);
          if (fake != nullptr) {
            using SetVersionFn = void(__cdecl*)(std::uint32_t);
            const FARPROC set_version_proc =
                GetProcAddress(fake, "ZMenuVersionTestSet");
            SetVersionFn set_version = nullptr;
            static_assert(sizeof(set_version) == sizeof(set_version_proc));
            std::memcpy(&set_version, &set_version_proc, sizeof(set_version));
            CHECK(set_version != nullptr);
            if (set_version != nullptr) {
              set_version(0x35DD0u);
              CHECK(zpatch::zmenu_version_guard(guard_version) ==
                    zpatch::ZMenuGuardResult::version_rejected);
              CHECK(guard_version == 0x35DD0u);
              set_version(zpatch::kZMenuMinVersion);
              CHECK(zpatch::zmenu_version_guard(guard_version) ==
                    zpatch::ZMenuGuardResult::version_ok);
              CHECK(guard_version == zpatch::kZMenuMinVersion);
              set_version(zpatch::kZMenuMinVersion + 1u);
              CHECK(zpatch::zmenu_version_guard(guard_version) ==
                    zpatch::ZMenuGuardResult::version_ok);
            }
            FreeLibrary(fake);
          }
        }
      }
    }
#endif
    using R = zpatch::MenuProbeResult;
    CHECK(zpatch::probe_menu_api(false,false,false,true)==R::disabled);
    CHECK(zpatch::probe_menu_api(true,true,false,false)==R::already_loaded);
    CHECK(zpatch::probe_menu_api(true,false,false,true)==R::loaded);
    CHECK(zpatch::probe_menu_api(true,false,true,true)==R::unavailable);
    zpatch::MenuIntegrationState state;
    CHECK(state.option_enabled);                       // AddOptionsInMenu default 1
    CHECK(zpatch::menu_integration_probe(state,false)==R::unavailable);
    CHECK(state.attempt_made && state.load_attempts==1);
    // A failed attempt is not retried (the recovered one-shot flag).
    CHECK(zpatch::menu_integration_probe(state,true)==R::unavailable);
    CHECK(state.load_attempts==1 && !state.module_present);
    // Disabled option short-circuits.
    zpatch::MenuIntegrationState off; off.option_enabled=false;
    CHECK(zpatch::menu_integration_probe(off,true)==R::disabled);
    CHECK(off.load_attempts==0);
    // Successful load caches the module.
    zpatch::MenuIntegrationState good;
    CHECK(zpatch::menu_integration_probe(good,true)==R::loaded);
    CHECK(good.module_present);
    CHECK(zpatch::menu_integration_probe(good,false)==R::already_loaded);
    CHECK(good.load_attempts==1);
  }

  // Recovered decimal formatting primitive (the composition of FUN_10024600).
  {
    CHECK(zpatch::divide_by_ten_magic(0u)==0u);
    CHECK(zpatch::divide_by_ten_magic(9u)==0u);
    CHECK(zpatch::divide_by_ten_magic(10u)==1u);
    CHECK(zpatch::divide_by_ten_magic(4294967295u)==429496729u);
    for (std::uint32_t v : {0u,1u,9u,10u,99u,100u,12345u,1000000u,4294967295u})
      CHECK(zpatch::divide_by_ten_magic(v)==v/10u);
    CHECK(zpatch::format_unsigned(0u)=="0");
    CHECK(zpatch::format_unsigned(7u)=="7");
    CHECK(zpatch::format_unsigned(1234u)=="1234");
    CHECK(zpatch::format_unsigned(4294967295u)=="4294967295");
    CHECK(zpatch::format_signed(0)=="0");
    CHECK(zpatch::format_signed(-1)=="-1");
    CHECK(zpatch::format_signed(-1234)=="-1234");
    CHECK(zpatch::format_signed(2147483647)=="2147483647");
    CHECK(zpatch::format_signed(-2147483647 - 1)=="-2147483648");   // INT32_MIN
    char digits[8];
    CHECK(zpatch::decimal_digits_lsd_first(123,digits,sizeof(digits))==3);
    CHECK(digits[0]=='3' && digits[1]=='2' && digits[2]=='1');
    CHECK(zpatch::decimal_digits_lsd_first(5,digits,0)==0);        // no capacity
    CHECK(zpatch::decimal_digits_lsd_first(5,nullptr,4)==0);
  }

  // Uninitialised runtime refuses to patch.
  {
    zpatch::shutdown();
    std::vector<std::byte> image(0x100, std::byte{0});
    zpatch::RuntimePatchReport report;
    const auto st = zpatch::apply_recovered_patches(zpatch::GameBuild::v1070,
        {image.data(), image.size()}, &FakeProtect::protect, report);
    CHECK(!st && st.code == zpatch::StatusCode::invalid_input);
    CHECK(report.candidates == 0 && report.applied == 0);
  }

  const auto catalog = zpatch::observed_feature_catalog();
  CHECK(catalog.size == 99);   // 98 INI keys + the undocumented Patch7RemoveModChecks
  CHECK(catalog.data && catalog.data[0].ini_key == "AddOptionsInMenu");

  // Recovered feature catalog: every [Options] key with recovered metadata
  // (plus the undocumented loader key Patch7RemoveModChecks).
  {
    const auto recovered = zpatch::recovered_feature_catalog();
    CHECK(recovered.size == 99);
    CHECK(recovered.data != nullptr);
    std::size_t with_handler = 0, with_signature = 0, gated = 0, resolved = 0;
    std::size_t sig_ops = 0, rva_ops = 0;
    for (std::size_t i = 0; i < recovered.size; ++i) {
      const auto& row = recovered.data[i];
      CHECK(!row.ini_key.empty());
      if (row.handler_va != 0) ++with_handler;
      if (row.has_signature) ++with_signature;
      if (row.has_build_gate) ++gated;
      if (row.resolved_on_1080) ++resolved;
      sig_ops += row.signature_ops;
      rva_ops += row.rva_ops;
    }
    CHECK(with_handler == 80);        // recovered handler count
    CHECK(with_signature == 64);      // options with recovered signatures
    CHECK(gated == 14);               // options with a recovered build gate
    CHECK(sig_ops == 129 && rva_ops == 91);   // unique signature rows + rva rows (shared-handler rows count for both options)
    CHECK(resolved >= 1);

    // Lookup is case-sensitive and exact.
    const auto* skip = zpatch::find_recovered_feature("SkipIntro");
    CHECK(skip != nullptr && skip->observed_default == "1");
    CHECK(zpatch::find_recovered_feature("skipintro") == nullptr);
    CHECK(zpatch::find_recovered_feature("NoSuchOption") == nullptr);

    // Build gate semantics: an ungated option is unconditional...
    CHECK(zpatch::recovered_feature_allows_build("SkipIntro", 9));
    CHECK(zpatch::recovered_feature_allows_build("SkipIntro", 99));
    // ...an unknown key is unrestricted, and a gated one follows its mask.
    CHECK(zpatch::recovered_feature_allows_build("NotAnOption", 9));
    const auto* bench = zpatch::find_recovered_feature("BenchmarkFix");
    CHECK(bench != nullptr && bench->has_build_gate);
    CHECK(!zpatch::recovered_feature_allows_build("BenchmarkFix", 9));   // "!= 9" excludes
  }

  const char* file = "zpatch_test.ini";
  { std::ofstream out(file); out << "[Options]\nSkipIntro=1\n"; }
  CHECK(zpatch::initialize(file));
  const auto state = zpatch::runtime_state();
  CHECK(state.initialized && !state.patches_applied);
  CHECK(state.config.find("Options", "SkipIntro"));
  zpatch::shutdown();
  CHECK(!zpatch::runtime_state().initialized);
  std::remove(file);

  // End-to-end: apply the recovered patch operations to a mapped module image.
  // A synthetic image exercises the real path (option/build gate -> scan -> write)
  // without touching any process memory.
  {
    const char* patch_ini = "zpatch_patch_test.ini";
    {
      // Enable every option the op table references: the runtime now applies an
      // option only when the INI contains the key with a non-zero value.
      std::size_t all_count = 0;
      const auto* all_ops = zpatch::recovered_patch_ops(all_count);
      std::vector<std::string> keys;
      for (std::size_t i = 0; i < all_count; ++i) {
        std::string key(all_ops[i].option);
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
      }
      std::ofstream out(patch_ini);
      out << "[Options]\n";
      for (const auto& key : keys) out << key << "=1\n";
    }
    CHECK(zpatch::initialize(patch_ini));
    constexpr std::size_t kImageSize = 0x3000;
    std::vector<std::byte> image(kImageSize, std::byte{0xcc});
    // Place one recovered signature verbatim at 0x1000 so it can be found.
    std::size_t op_count = 0;
    const auto* ops = zpatch::recovered_patch_ops(op_count);
    CHECK(op_count > 0);
    std::size_t placed = 0;
    for (std::size_t i = 0; i < op_count; ++i) {
      const auto& op = ops[i];
      if (op.module_rva != 0 || op.signature.empty()) continue;
      std::size_t at = 0x1000, cursor = 0;
      bool fits = true;
      while (cursor < op.signature.size()) {
        const auto sp = op.signature.find(' ', cursor);
        const auto tk = op.signature.substr(
            cursor, sp == std::string_view::npos ? std::string_view::npos : sp - cursor);
        if (tk.empty() || tk.size() > 2 || !(at < image.size())) { fits = false; break; }
        const auto nib = [](char c) -> int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          return -1;
        };
        const char c0 = tk[0];
        const char c1 = tk.size() > 1 ? tk[1] : tk[0];
        const int hi = c0 == '?' ? -1 : nib(c0);
        const int lo = c1 == '?' ? -1 : nib(c1);
        if (hi < 0 || lo < 0) { fits = false; break; }
        image[at] = static_cast<std::byte>((hi << 4) | lo);
        ++at;
        if (sp == std::string_view::npos) break;
        cursor = sp + 1;
      }
      if (fits) { placed = 1; break; }
    }
    CHECK(placed == 1);

    const std::vector<std::byte> pristine = image;   // pre-apply image
    zpatch::RuntimePatchReport report;
    protect_calls = 0;
    CHECK(zpatch::apply_recovered_patches(zpatch::GameBuild::v1070,
        {image.data(), image.size()}, &FakeProtect::protect, report));
    CHECK(report.candidates > 0);
    CHECK(report.applied >= 1);                       // the placed signature was patched
    CHECK(report.applied + report.not_found + report.gated_out <= report.candidates);
    CHECK(report.outcomes.size() <= report.candidates);
    CHECK(zpatch::runtime_state().patches_applied);   // the state records the application

    // Rollback mirror: every write captured a preimage, and restoring it puts the
    // image back exactly as it was before the apply (byte-for-byte).
    zpatch::AppliedWriteLog log;
    CHECK(image != pristine);                         // the first apply changed bytes
    // Re-apply with logging over a fresh pre-apply copy to obtain the undo log.
    std::vector<std::byte> image2 = pristine;
    report = {};
    CHECK(zpatch::apply_recovered_patches_logged(zpatch::GameBuild::v1070,
        {image2.data(), image2.size()}, &FakeProtect::protect, report, log));
    // Non-logged applied writes are slot publishes into OUR globals (LoadDLCs'
    // three build-8 rows plus SuperLODFix's two, whose arm default "1" enables it
    // even though this INI only carries the op-table keys). They count as applied
    // but write no image bytes and have no log entry.
    CHECK(report.applied >= 1 && report.applied >= log.size() + 5);
    CHECK(image2 != pristine);                        // bytes actually changed
    for (const auto& entry : log) {
      CHECK(entry.address != nullptr && entry.length > 0 && entry.length <= sizeof(entry.original));
      CHECK(entry.address >= image2.data() && entry.address + entry.length <= image2.data() + image2.size());
    }
    std::size_t restored = 0;
    protect_calls = 0;
    const std::size_t logged_writes = log.size();
    CHECK(zpatch::rollback_recovered_patches(&FakeProtect::protect, log, restored));
    CHECK(restored == logged_writes && log.empty());
    CHECK(image2 == pristine);                        // fully undone
    CHECK(protect_calls == 2 * restored);             // one protect + one restore per write
    CHECK(!zpatch::runtime_state().patches_applied);  // rollback clears the applied flag

    // Rolling back an empty log is a no-op success.
    std::size_t restored_again = 0;
    CHECK(zpatch::rollback_recovered_patches(&FakeProtect::protect, log, restored_again));
    CHECK(restored_again == 0);
    CHECK(!zpatch::rollback_recovered_patches(nullptr, log, restored_again));   // null protect

    // The Nth-match literal op kind is wired into the same apply/rollback path.
    // StartupTimeFix's value points into the ORIGINAL module's data
    // (0x101580d8), so a rebuilt module must skip it (rebasing pending) - the
    // crash dump showed what writing such a dangling pointer does. The
    // NoImpostorHeightCutoff NOP literal (0x9090) still applies.
    {
      const char* ml_ini = "zpatch_match_literal_test.ini";
      { std::ofstream out(ml_ini); out << "[Options]\nStartupTimeFix=1\nNoImpostorHeightCutoff=1\n"; }
      CHECK(zpatch::initialize(ml_ini));
      std::size_t ml_count = 0;
      const auto* ml = zpatch::recovered_match_literals(ml_count);
      CHECK(ml_count == 3);
      const zpatch::RecoveredMatchLiteral* ml_startup = nullptr;
      const zpatch::RecoveredMatchLiteral* ml_cutoff = nullptr;
      for (std::size_t i = 0; i < ml_count; ++i) {
        if (ml[i].option == "StartupTimeFix") ml_startup = &ml[i];
        if (ml[i].option == "NoImpostorHeightCutoff") ml_cutoff = &ml[i];
      }
      CHECK(ml_startup != nullptr && ml_cutoff != nullptr);
      const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      const auto plant = [&](std::vector<std::byte>& img, std::size_t at,
                             std::string_view signature) {
        std::size_t cursor = 0;
        while (cursor < signature.size()) {
          const auto sp = signature.find(' ', cursor);
          const auto tk = signature.substr(
              cursor, sp == std::string_view::npos ? std::string_view::npos : sp - cursor);
          const int hi = tk[0] == '?' ? 0 : nib(tk[0]);
          const int lo = tk.size() > 1 && tk[1] != '?' ? nib(tk[1]) : hi;
          img[at++] = static_cast<std::byte>((hi << 4) | lo);
          if (sp == std::string_view::npos) break;
          cursor = sp + 1;
        }
      };
      std::vector<std::byte> img(0x4000, std::byte{0xcc});
      plant(img, 0x1000, ml_startup->signature);
      plant(img, 0x2000, ml_startup->signature);
      plant(img, 0x3000, ml_cutoff->signature);
      const std::vector<std::byte> ml_pristine = img;
      zpatch::RuntimePatchReport ml_report;
      zpatch::AppliedWriteLog ml_log;
      CHECK(zpatch::apply_recovered_patches_logged(zpatch::GameBuild::v1070,
          {img.data(), img.size()}, &FakeProtect::protect, ml_report, ml_log));
      std::size_t startup_writes = 0;
      for (const auto& e : ml_log) if (e.option == "StartupTimeFix") ++startup_writes;
      // The value is rebased to OUR global (the original's dword_101580d8 does
      // not exist in a rebuilt module).
      CHECK(startup_writes == 2);
      const auto rebased = static_cast<std::uint32_t>(
          reinterpret_cast<std::uintptr_t>(&zpatch::startup_time_global()));
      CHECK(rebased != 0x101580d8u);
      for (std::size_t at : {0x1000u + 6u, 0x2000u + 6u}) {
        std::uint32_t v = 0; std::memcpy(&v, img.data() + at, 4);
        CHECK(v == rebased);
      }
      std::uint16_t cutoff = 0;
      std::memcpy(&cutoff, img.data() + 0x3000, 2);
      CHECK(cutoff == 0x9090u);                              // NOP literal applied
      std::size_t ml_restored = 0;
      const std::size_t ml_logged = ml_log.size();
      CHECK(zpatch::rollback_recovered_patches(&FakeProtect::protect, ml_log, ml_restored));
      CHECK(ml_restored == ml_logged);
      CHECK(img == ml_pristine);                              // fully undone
      zpatch::shutdown();
      std::remove(ml_ini);
    }
    zpatch::shutdown();
    std::remove(patch_ini);
  }

  // Runtime wiring: with a destination resolver the applied jump targets the
  // resolved body instead of the recovered trampoline VA, and rollback still
  // undoes it. The synthetic 32-bit target mirrors the x86 ASI layout.
  {
    const char* hook_ini = "zpatch_hook_resolver_test.ini";
    {
      std::size_t all_count = 0;
      const auto* all_ops = zpatch::recovered_patch_ops(all_count);
      std::vector<std::string> keys;
      for (std::size_t i = 0; i < all_count; ++i) {
        std::string key(all_ops[i].option);
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
      }
      std::ofstream out(hook_ini);
      out << "[Options]\n";
      for (const auto& key : keys) out << key << "=1\n";
    }
    CHECK(zpatch::initialize(hook_ini));
    std::size_t sig_count = 0;
    const auto* sig_jumps = zpatch::recovered_sig_jumps(sig_count);
    const zpatch::RecoveredSigJump* chosen = nullptr;
    for (std::size_t i = 0; i < sig_count; ++i) {
      // Select with the same predicate the runtime apply path uses.
      if (zpatch::recovered_feature_allows_build(
              sig_jumps[i].option, static_cast<std::uint8_t>(zpatch::GameBuild::v1080))) {
        chosen = &sig_jumps[i];
        break;
      }
    }
    CHECK(chosen != nullptr);
    const std::string pattern(chosen->signature);
    std::vector<std::byte> img(0x4000, std::byte{0xcc});
    {
      std::size_t at = 0x1000;
      std::size_t cursor = 0;
      const auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
      };
      while (cursor < pattern.size()) {
        const auto sp = pattern.find(' ', cursor);
        const auto tk = pattern.substr(
            cursor, sp == std::string::npos ? std::string::npos : sp - cursor);
        if (tk.empty() || tk.size() > 2 || at >= img.size()) break;
        const char c0 = tk[0];
        const char c1 = tk.size() > 1 ? tk[1] : tk[0];
        const int hi = c0 == '?' ? -1 : nib(c0);
        const int lo = c1 == '?' ? -1 : nib(c1);
        if (hi >= 0 && lo >= 0) img[at] = static_cast<std::byte>((hi << 4) | lo);
        ++at;
        if (sp == std::string::npos) break;
        cursor = sp + 1;
      }
    }
    static std::uintptr_t resolved_target = 0x00200000;   // synthetic 32-bit body VA
    auto resolver = +[](std::uint32_t, void*) -> std::uintptr_t { return resolved_target; };
    zpatch::RuntimePatchReport report;
    zpatch::AppliedWriteLog log;
    CHECK(zpatch::apply_recovered_patches_logged(zpatch::GameBuild::v1080,
        {img.data(), img.size()}, &FakeProtect::protect, report, log,
        zpatch::kDefaultGameModuleBase, resolver, nullptr));
    auto* site = img.data() + 0x1000 + static_cast<std::ptrdiff_t>(chosen->offset);
    CHECK(std::to_integer<unsigned char>(*site) == 0xE9);
    std::int32_t rel = 0;
    std::memcpy(&rel, site + 1, sizeof(rel));
    const auto site_va = zpatch::kDefaultGameModuleBase + 0x1000u +
                         static_cast<std::uintptr_t>(chosen->offset);
    CHECK(rel == static_cast<std::int32_t>(0x00200000ull - site_va - 5u));
    std::size_t restored = 0;
    CHECK(zpatch::rollback_recovered_patches(&FakeProtect::protect, log, restored));
    CHECK(restored >= 1);
    zpatch::shutdown();
    std::remove(hook_ini);
  }
  // ---- hook_bodies: portable reimplementations of recovered hook bodies ----
  // Every check below mirrors a behaviour recovered from the original body at
  // the documented reference VA (see ZolikaPatch_analysis/checkpoint_0002).
  {
    using namespace zpatch::hook_bodies;

    // 0x10005590 AimProjectileCrashFix.
    {
      std::int32_t fields[16]{};
      CHECK(aim_projectile_crash_fix(nullptr, 1, 2, 3) == 0);
      CHECK(aim_projectile_crash_fix(fields, 11, 22, 33) == 11);
      CHECK(fields[7] == 11 && fields[8] == 22 && fields[9] == 33);
      CHECK(aim_projectile_crash_fix(fields, 11, 22, 0) == 0);
      CHECK(fields[7] == 0 && fields[8] == 22 && fields[9] == 0);
    }

    // Trivial replacements.
    CHECK(better_mp_sync() == 0);
    CHECK(combined_radio_freeze_fix() == 19);
    CHECK(load_dlcs_dummy() == 1);
    CHECK(rgsc_login_removed() == 1);
    CHECK(savegame_fix_null() == 0);

    // 0x10045580 / 0x100455e0 SavegameFix.
    {
      static int close_calls = 0;
      static void* close_seen = nullptr;
      auto close_fn = +[](void* handle) {
        close_calls += 1;
        close_seen = handle;
      };
      int handle = 7;
      CHECK(savegame_fix_close(&handle, close_fn) == 0);
      CHECK(close_calls == 1 && close_seen == &handle);
      CHECK(savegame_fix_mutex(nullptr, nullptr) == 1);
      void* created = nullptr;
      CHECK(savegame_fix_mutex(&created, +[]() -> void* { return reinterpret_cast<void*>(0x1234); }) == 0);
      CHECK(created == reinterpret_cast<void*>(0x1234));
    }

    // 0x10005890 / 0x10005900 BorderlessWindowed style computation.
    {
      const auto styles = borderless_style_fixup(0x10CF0000, 0x00040008);
      CHECK(styles.style == static_cast<std::int32_t>(0x10000000));
      CHECK(styles.ex_style == static_cast<std::int32_t>(0x00040000));
    }

    // 0x10045a30 / 0x10045a80 ImprovedShaderStreaming.
    {
      static char name_h[] = "zzzzh";
      static char name_v[] = "zzzzv";
      static int entry_a = 1;
      static int entry_b = 2;
      void* entries[] = {nullptr, &entry_a, &entry_b};
      auto get_name = +[](void* entry) -> const void* {
        return entry == &entry_a ? static_cast<const void*>(name_h)
                                 : static_cast<const void*>(name_v);
      };
      auto filter_h = +[](int, const void* name) -> int {
        return *reinterpret_cast<const char*>(static_cast<const std::uint8_t*>(name) + 4) == 'h'
                   ? 0
                   : 2;
      };
      auto filter_v = +[](int, const void* name) -> int {
        return *reinterpret_cast<const char*>(static_cast<const std::uint8_t*>(name) + 4) == 'v'
                   ? 0
                   : 2;
      };
      CHECK(improved_shader_streaming_find(entries, 3, 99, get_name, filter_h) == &entry_a);
      CHECK(improved_shader_streaming_find(entries, 3, 99, get_name, filter_v) == &entry_b);
      CHECK(improved_shader_streaming_find(entries, 1, 99, get_name, filter_v) == nullptr);
      CHECK(improved_shader_streaming_find(nullptr, 3, 99, get_name, filter_h) == nullptr);
    }

    // 0x10050540 SpeedupBugFix timing constants.
    {
      const auto c = speedup_timing_constants(1000);
      CHECK(std::fabs(c.seconds_per_tick - 1.0f) < 1e-6f);
      CHECK(std::fabs(c.per_tick_ms - 1000.0f) < 1e-3f);
      CHECK(std::fabs(c.ticks_per_second - 0.001f) < 1e-6f);
      CHECK(std::fabs(c.per_tick_seconds - 0.001f) < 1e-6f);
      const auto q = speedup_timing_constants(3579545);
      CHECK(std::fabs(q.seconds_per_tick - 0.00027936501f) < 1e-9f);
      CHECK(std::fabs(q.per_tick_ms - 0.279365f) < 1e-4f);
      CHECK(std::fabs(q.ticks_per_second - 3.579545f) < 1e-4f);
      const auto zero = speedup_timing_constants(0);
      CHECK(zero.seconds_per_tick == 0.0f && zero.per_tick_ms == 0.0f);
    }

    // 0x10050870 PedModelsInCutscenesFix.
    {
      std::uint8_t object[0x40]{};
      auto hash_fn = +[](const char*) -> unsigned int { return 0x79FBB0C5u; };
      auto lookup_fn = +[](unsigned int, int* out) { *out = 42; };
      *reinterpret_cast<std::int16_t*>(object + 0x2E) = 42;
      CHECK(ped_models_in_cutscenes_check(object, hash_fn, lookup_fn));
      *reinterpret_cast<std::int16_t*>(object + 0x2E) = 43;
      CHECK(!ped_models_in_cutscenes_check(object, hash_fn, lookup_fn));
      CHECK(!ped_models_in_cutscenes_check(nullptr, hash_fn, lookup_fn));
    }

    // 0x10051890 / 0x100518D0 NewFilesCompatibility.
    {
      static int insert_calls = 0;
      static void* insert_target_seen = nullptr;
      static bool contains_result = true;
      GameCallSlots slots;
      slots.hash_name = +[](const char* s) -> unsigned int {
        return static_cast<unsigned int>(s[0]);
      };
      slots.dictionary_container = +[]() -> void* { return reinterpret_cast<void*>(0x77); };
      slots.dictionary_contains = +[](void*, unsigned int) -> bool { return contains_result; };
      slots.dictionary_insert = +[](void* target, const char*) {
        insert_calls += 1;
        insert_target_seen = target;
      };
      int target = 3;
      CHECK(!new_files_compat_update("", &target, slots));
      CHECK(insert_calls == 0);
      CHECK(new_files_compat_update("abc", &target, slots));
      CHECK(insert_calls == 1 && insert_target_seen == &target);
      contains_result = false;
      CHECK(new_files_compat_update("abc", &target, slots));
      CHECK(insert_calls == 1);  // no insert when the dictionary lacks the hash
      contains_result = true;
    }

    // 0x1001b080 EpisodeSwitchFreezeFix sum.
    {
      std::int32_t words[8]{};
      words[3] = 5;
      words[4] = 7;
      CHECK(episode_switch_sum(words) == 12);
      CHECK(episode_switch_sum(nullptr) == 0);
    }

    // 0x10038670 NewMemorySystem.
    {
      void* allocator = nullptr;
      CHECK(new_memory_system_apply(nullptr, nullptr) == nullptr);
      CHECK(new_memory_system_apply(&allocator, reinterpret_cast<void*>(0xABC)) == &allocator);
      CHECK(allocator == reinterpret_cast<void*>(0xABC));
    }

    // 0x10023490 LoadDLCs struct fixup.
    {
      static int callback_calls = 0;
      static int post_calls = 0;
      auto callback = +[](void*) { callback_calls += 1; };
      auto post = +[](void*) { post_calls += 1; };
      std::uint8_t object[512];
      std::memset(object, 0xAA, sizeof(object));
      object[359] = 0;  // take the load path
      load_dlcs_struct_fixup(object, callback, post);
      CHECK(object[0] == 0 && object[64] == 0 && object[128] == 0 && object[192] == 0 &&
            object[256] == 0);
      CHECK(callback_calls == 1 && post_calls == 1);
      CHECK(object[0xA4] == 1);   // verified write at 0x100234d1
      CHECK(object[360] == 0xAA); // untouched on the load path
      callback_calls = post_calls = 0;
      object[359] = 1;
      load_dlcs_struct_fixup(object, callback, post);
      CHECK(object[360] == 1 && callback_calls == 0 && post_calls == 0);
    }

    // 0x10005d80 CollisionDeadlockFix walk.
    {
      std::uint8_t table[4 * 8]{};
      const auto set = [&](int i, std::uint16_t next, std::uint16_t flags) {
        *reinterpret_cast<std::uint16_t*>(table + 8 * i) = next;
        *reinterpret_cast<std::uint16_t*>(table + 8 * i + 4) = flags;
      };
      set(0, 1, 0);
      set(1, 2, 1);
      set(2, 3, 0);
      set(3, 0xFFFF, 0);
      std::uint8_t self[128]{};
      std::uint8_t other[104]{};
      const std::uint8_t* table_ptr = table;
      std::memcpy(self + 112, &table_ptr, sizeof(table_ptr));
      *reinterpret_cast<std::uint16_t*>(other + 102) = 0;
      std::uint32_t counter = 0;
      auto blocked = +[](int mask) -> unsigned char { return mask == 2 ? 1 : 0; };
      CHECK(collision_deadlock_walk(self, other, &counter, blocked) == 1);
      CHECK(counter == 1);
      auto never = +[](int) -> unsigned char { return 0; };
      counter = 0;
      CHECK(collision_deadlock_walk(self, other, &counter, never) == -1);
      CHECK(counter == 4);  // steps: records 0,1,2,3 then the 0xFFFF terminator
      *reinterpret_cast<std::uint16_t*>(other + 102) = 0xFFFF;
      CHECK(collision_deadlock_walk(self, other, &counter, blocked) == -1);
      CHECK(collision_deadlock_walk(nullptr, other, &counter, blocked) == -1);
    }

    // 0x10005650 / 0x10005670 BenchmarkFix.
    {
      static int fn_calls = 0;
      static int fn_arg = 0;
      auto fn = +[](int v) {
        fn_calls += 1;
        fn_arg = v;
      };
      auto resume = +[]() -> int { return 55; };
      CHECK(benchmark_fix_shim(0, 9, fn, resume) == 0);
      CHECK(fn_calls == 0);
      CHECK(benchmark_fix_shim(1, 9, fn, resume) == 55);
      CHECK(fn_calls == 1 && fn_arg == 9);
    }

    // 0x10005af0 BuildingDynamicShadows.
    {
      auto on_default = +[]() -> int { return 1; };
      auto on_special = +[]() -> int { return 2; };
      std::int32_t value = 0x1111;
      CHECK(building_shadow_dispatch(&value, 0x1111, 0x2222, on_default, on_special) == 2);
      value = 0x2222;
      CHECK(building_shadow_dispatch(&value, 0x1111, 0x2222, on_default, on_special) == 2);
      value = 0x3333;
      CHECK(building_shadow_dispatch(&value, 0x1111, 0x2222, on_default, on_special) == 1);
    }

    // 0x10006330 CopSirensFlag (eflags variant).
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      static int resume_first = 0;
      static int resume_second = 0;
      auto resume = +[](int a, int b) -> int {
        resume_first = a;
        resume_second = b;
        return 77;
      };
      std::int32_t stored = 0;
      CHECK(cop_sirens_flag_shim(5, 6, 7, &stored, step, resume) == 77);
      CHECK(stored == 5 && step_calls == 1);
      CHECK(resume_first == 7 && resume_second == 6);  // original: resume(a3, a2)
    }

    // 0x100064b0 DrivebyFOVFix.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      auto resume = +[](int a, int b) -> int { return a + b; };
      CHECK(driveby_fov_shim(3, 4, step, resume) == 7);
      CHECK(step_calls == 1);
    }

    // 0x10006880 CarPartsShadowFix.
    {
      auto on_null = +[]() -> int { return 1; };
      auto on_present = +[]() -> int { return 2; };
      CHECK(car_parts_shadow_dispatch(0, on_null, on_present) == 1);
      CHECK(car_parts_shadow_dispatch(5, on_null, on_present) == 2);
    }

    // 0x1001b040 EpisodeSwitchFreezeFix store-and-resume.
    {
      std::int32_t object[16]{};
      static int resume_calls = 0;
      static int resume_args[4]{};
      auto resume = +[](int a, int b, int c, int d) -> int {
        resume_calls += 1;
        resume_args[0] = a;
        resume_args[1] = b;
        resume_args[2] = c;
        resume_args[3] = d;
        return 1;
      };
      CHECK(episode_switch_store_and_resume(object, 0, 1, 2, 3, 4, resume) == 0);
      CHECK(object[8] == 0 && resume_calls == 0);
      CHECK(episode_switch_store_and_resume(object, 9, 1, 2, 3, 4, resume) == 1);
      CHECK(object[8] == 9 && resume_calls == 1);
      CHECK(resume_args[0] == 1 && resume_args[1] == 2 && resume_args[2] == 3 &&
            resume_args[3] == 4);
    }

    // 0x1001b060 EpisodeSwitchFreezeFix shim b.
    {
      unsigned char edi = 9;
      auto resume = +[]() -> int { return 5; };
      CHECK(episode_switch_shim_b(1, 3, &edi, resume) == 5);
      CHECK(edi == 9);
      CHECK(episode_switch_shim_b(0, 3, &edi, resume) == 3);
      CHECK(edi == 0);
    }

    // 0x1001b0a0 EpisodeSwitchFreezeFix vcall.
    {
      static const void* seen_self = nullptr;
      static int seen_arg = 0;
      auto vcall = +[](const void* self, int arg) -> int {
        seen_self = self;
        seen_arg = arg;
        return 1;
      };
      std::int32_t words[4]{};
      words[0] = 0x55667788;
      words[1] = 99;
      CHECK(episode_switch_vcall(words, vcall) == 1);
      CHECK(seen_self == reinterpret_cast<const void*>(0x55667788));
      CHECK(seen_arg == 99);
      CHECK(episode_switch_vcall(nullptr, vcall) == 0);
    }

    // 0x1001b0c0 EpisodeSwitchFreezeFix shim c.
    {
      auto resume = +[]() -> int { return 4; };
      std::int32_t object = 1;
      CHECK(episode_switch_shim_c(&object, resume) == 4);
      CHECK(episode_switch_shim_c(nullptr, resume) == 0);
    }

    // 0x10036e90 GroupHackFix.
    {
      auto resume = +[]() -> int { return 3; };
      CHECK(group_hack_shim(reinterpret_cast<const void*>(8), resume) == 0);
      CHECK(group_hack_shim(reinterpret_cast<const void*>(9), resume) == 3);
    }

    // 0x10036eb0 FreezeGunFix.
    {
      auto resume = +[](int a, int b) -> int { return a + b; };
      std::uint8_t object[0x48]{};
      auto* field = reinterpret_cast<std::int32_t*>(object + 0x40);
      *field = 0xCDCDCDCD;
      CHECK(freeze_gun_shim(object, 1, 2, resume) == 0);
      *field = -1;
      CHECK(freeze_gun_shim(object, 1, 2, resume) == 0);
      *field = 0;
      CHECK(freeze_gun_shim(object, 1, 2, resume) == 0);
      *field = 123;
      CHECK(freeze_gun_shim(object, 1, 2, resume) == 3);
    }

    // 0x10036ee0 BlackscreenFix.
    {
      auto resume = +[]() -> int { return 1; };
      CHECK(blackscreen_shim(5, resume) == 1);   // <= 7 -> resume
      CHECK(blackscreen_shim(7, resume) == 1);
      CHECK(blackscreen_shim(8, resume) == 8);   // 8..10 pass the value through
      CHECK(blackscreen_shim(10, resume) == 10);
      CHECK(blackscreen_shim(11, resume) == 1);  // >= 11 -> resume
    }

    // 0x10036f00 CrashMsgFix.
    {
      char message[] = "a~b~~c";
      CHECK(crash_msg_sanitize(message) == message);
      CHECK(std::strcmp(message, "a b  c") == 0);
      CHECK(crash_msg_sanitize(nullptr) == nullptr);
    }

    // 0x1003d7b0 PathNodeCrashFix.
    {
      auto on_special = +[]() -> int { return 2; };
      auto resume = +[]() -> int { return 1; };
      CHECK(path_node_shim(0x1FFFFF, on_special, resume) == 2);
      CHECK(path_node_shim(0, on_special, resume) == 1);
    }

    // 0x1003d870 PlaneCrashFix.
    {
      auto resume = +[]() -> int { return 1; };
      auto on_null = +[]() -> int { return 2; };
      std::uint8_t object[0x30]{};
      CHECK(plane_crash_shim(object, 0, 0xAB, resume, on_null) == 2);
      CHECK(plane_crash_shim(object, 1, 0xAB, resume, on_null) == 1);
      CHECK(object[0x26] == 0xAB);
    }

    // 0x1003d9a0 PlayerAsPedComponentsFix.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      auto resume = +[]() -> int { return 9; };
      std::int32_t stored = 0;
      CHECK(player_ped_components_shim(6, &stored, step, resume) == 9);
      CHECK(stored == 6 && step_calls == 1);
    }

    // 0x10044560 PopupHeadlightsFlag.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      auto resume_a = +[](int a, int b) -> int { return a * 100 + b; };
      auto resume_b = +[](int a, int b) -> int { return a * 1000 + b; };
      std::uint8_t object[0x30]{};
      *reinterpret_cast<std::int16_t*>(object + 0x2E) = 7;
      std::int32_t stored = 0;
      bool mode = true;
      CHECK(popup_headlights_shim(object, 3, &stored, &mode, step, resume_a, resume_b) == 307);
      CHECK(stored == 7 && step_calls == 1);
      mode = false;
      CHECK(popup_headlights_shim(object, 3, &stored, &mode, step, resume_a, resume_b) == 3007);
    }

    // 0x10046780 SMPA60Fix tail.
    {
      auto resume = +[]() -> int { return 1; };
      std::int32_t object[8]{};
      std::int32_t linked[8]{};
      // The original stores the linked pointer in a 32-bit field, so the write
      // through it is only representable when pointers are 32 bits (the mod's
      // real ABI). On 64-bit test builds only the guard paths are exercised.
      if (sizeof(void*) == 4) {
        object[5] = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(linked));
        object[4] = 0x1234;
        CHECK(smpa60_tail_shim(object, resume) == 1);
        CHECK(linked[4] == 0x1234);
      }
      object[5] = 0;
      object[4] = 0x5678;
      CHECK(smpa60_tail_shim(object, resume) == 1);
      CHECK(linked[4] == 0);  // untouched
      object[5] = 0xCDCDCDCD;
      CHECK(smpa60_tail_shim(object, resume) == 1);
      CHECK(linked[4] == 0);
    }

    // 0x10046750 SMPA60Fix entry.
    {
      auto on_marker = +[]() -> int { return 2; };
      static int args_seen[5]{};
      auto resume = +[](int a, int b, int c, int d, int e) -> int {
        args_seen[0] = a;
        args_seen[1] = b;
        args_seen[2] = c;
        args_seen[3] = d;
        args_seen[4] = e;
        return 1;
      };
      std::uint8_t object[0x80]{};
      CHECK(smpa60_entry_shim(static_cast<int>(0xDDDDDDDDu), object, on_marker, resume) == 2);
      *reinterpret_cast<std::int32_t*>(object + 0x70) = 0x500;
      CHECK(smpa60_entry_shim(1, object, on_marker, resume) == 1);
      CHECK(args_seen[0] == 0x500 && args_seen[1] == 0x500 && args_seen[2] == 0x510 &&
            args_seen[3] == 0x500);
      CHECK(args_seen[4] == static_cast<int>(reinterpret_cast<std::uintptr_t>(object + 0x40)));
    }

    // 0x100469e0 SuperLODFix.
    {
      auto skip = +[]() -> int { return 1; };
      auto resume = +[]() -> int { return 2; };
      std::uint8_t object[0x80]{};
      CHECK(super_lod_fix_shim(object, true, skip, resume) == 2);
      CHECK(super_lod_fix_shim(object, false, skip, resume) == 1);
      object[0x72] = 0x10;
      CHECK(super_lod_fix_shim(object, false, skip, resume) == 2);
    }

    // 0x1004aab0 VRAMFix.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      std::int32_t stored_a = 0;
      std::int32_t stored_b = 0;
      CHECK(vram_fix_shim(11, 22, &stored_a, &stored_b, step, false) == 11);
      CHECK(stored_a == 22 && stored_b == 11 && step_calls == 1);
      CHECK(vram_fix_shim(11, 22, &stored_a, &stored_b, step, true) == 0x7FFFFFFF);
    }

    // 0x10050490 WantedLevelCrashFix.
    {
      static const void* vcall_seen = nullptr;
      auto vcall = +[](const void* self) { vcall_seen = self; };
      auto resume = +[]() -> int { return 6; };
      std::int32_t object = 1;
      CHECK(wanted_level_shim(&object, vcall, resume) == 6);
      CHECK(vcall_seen == &object);
      CHECK(wanted_level_shim(nullptr, vcall, resume) == 0);
    }

    // 0x100521e0 NewSavesCompatibility.
    {
      static int pre_calls = 0;
      auto pre = +[]() { pre_calls += 1; };
      auto resume = +[](int a, int b) -> int { return a - b; };
      std::int32_t stack_slot = 0;
      CHECK(new_saves_compat_shim(5, 2, 9, &stack_slot, pre, resume) == 7);
      CHECK(stack_slot == 5 && pre_calls == 1);
    }

    // 0x10051600 NewFilesCompatibility tail shim.
    CHECK(new_files_compat_shim(+[]() -> int { return 8; }) == 8);

    // 0x100512f0 / 0x100068a0 detours.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      detour_100512f0_body(step);
      CHECK(step_calls == 1);
      step_calls = 0;
      auto resume = +[]() -> int { return 3; };
      CHECK(detour_100068a0_body(step, resume) == 3);
      CHECK(step_calls == 2);
    }

    // 0x100051b0 vehicle animation detour.
    {
      static int step_calls = 0;
      static int resume_calls = 0;
      auto step = +[]() { step_calls += 1; };
      auto resume = +[]() { resume_calls += 1; };
      std::int32_t stored = 0;
      VehicleAnimContext ctx;
      ctx.stored_esi_1015AD38 = &stored;
      ctx.esi_value = 0x11;
      vehicle_anim_detour(ctx, step, resume);
      CHECK(stored == 0x11 && step_calls == 1 && resume_calls == 1);
    }

    // 0x1001af60 EmissiveLerpFix.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      auto resume = +[](int ecx) -> int { return ecx; };
      EmissiveLerpContext ctx;
      std::int32_t stored_ecx = 0;
      std::int32_t stored_edx = 0;
      ctx.stored_ecx_10159CC0 = &stored_ecx;
      ctx.stored_edx_10159CCC = &stored_edx;
      ctx.ecx_value = 0xAA;
      ctx.edx_value = 0xBB;
      CHECK(emissive_lerp_detour(ctx, step, resume) == 0xAA);
      CHECK(stored_ecx == 0xAA && stored_edx == 0xBB && step_calls == 1);
    }

    // 0x10037ba0 BadPlayerModelCrashFix.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      auto resume = +[](int a, int b) -> int { return a + b; };
      BadPlayerModelContext ctx;
      std::int32_t stored_c = 0;
      std::int32_t stored_10 = 0;
      std::uint8_t object[0x20]{};
      ctx.stored_10159C8C = &stored_c;
      ctx.stored_10159CA4 = &stored_10;
      ctx.value_c = 3;
      ctx.value_10 = 4;
      ctx.object = object;
      CHECK(bad_player_model_detour(ctx, step, 1, 2, resume) == 3);
      CHECK(stored_c == 3 && stored_10 == 4 && step_calls == 1);
      CHECK(*reinterpret_cast<std::int32_t*>(object + 16) == 4);
    }

    // 0x1001ef60 HighFPSBikePhysicsFix.
    {
      static int step_calls = 0;
      auto step = +[]() -> int {
        step_calls += 1;
        return 0x999;
      };
      static int resume_esi = 0;
      static float resume_arg = 0.0f;
      auto resume = +[](int esi, float arg) -> int {
        resume_esi = esi;
        resume_arg = arg;
        return 1;
      };
      BikePhysicsContext ctx;
      float saved = 0.0f;
      std::int32_t result = 0;
      ctx.saved_arg_1015AD7C = &saved;
      ctx.step_result_1015AD80 = &result;
      ctx.arg_value = 2.5f;
      CHECK(bike_physics_detour(ctx, step, 0x77, resume) == 1);
      CHECK(saved == 2.5f && result == 0x999 && step_calls == 1);
      CHECK(resume_esi == 0x77 && resume_arg == 2.5f);
    }

    // 0x1001f030 HighFPSBikePhysicsFix loop body (12 phases then resume).
    {
      static int phases_seen[16]{};
      static int phase_count = 0;
      auto step = +[](int phase, int, int) -> int {
        phases_seen[phase_count++] = phase;
        return 0;
      };
      auto resume = +[]() -> int { return 42; };
      std::int32_t regs[8]{};
      CHECK(bike_physics_loop_body(regs, 8, kBikePhysicsPhaseCount, 1, 2, step, resume) == 42);
      CHECK(phase_count == 12);
      for (int i = 0; i < 12; ++i) CHECK(phases_seen[i] == i);
    }

    // 0x10006310 CopSirensFlag save variant.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      static int resume_eax = 0;
      auto resume = +[](int eax) -> int {
        resume_eax = eax;
        return 1;
      };
      CopSirensContext ctx;
      std::int32_t stored_edx = 0;
      ctx.stored_edx_10159370 = &stored_edx;
      ctx.edx_value = 0x123;
      ctx.loaded_eax_10159B80 = 0x456;
      CHECK(cop_sirens_detour(ctx, step, resume) == 1);
      CHECK(stored_edx == 0x123 && step_calls == 1 && resume_eax == 0x456);
    }

    // 0x1003d310..0x1003d3a0 NoLiveryLimit.
    {
      static int step_calls = 0;
      auto step = +[]() { step_calls += 1; };
      static int resume_args[2]{};
      auto resume = +[](int a, int b) -> int {
        resume_args[0] = a;
        resume_args[1] = b;
        return 1;
      };
      std::uint8_t object[0x140]{};
      *reinterpret_cast<std::int16_t*>(object + 0x48) = -5;  // sign-extended
      NoLiveryContext ctx;
      std::int32_t arglist = 0;
      std::int32_t stored_arg = 0;
      ctx.arglist_10159BA8 = &arglist;
      ctx.stored_arg_10159BB0 = &stored_arg;
      ctx.stored_10159B98 = 0x321;
      ctx.object = object;
      CHECK(no_livery_detour(ctx, 0x55, 0, step, resume) == 1);
      CHECK(stored_arg == 0x55 && step_calls == 1);
      CHECK(arglist == static_cast<std::int32_t>(
                             reinterpret_cast<std::uintptr_t>(object + 0x13C)));
      CHECK(resume_args[0] == -5 && resume_args[1] == 0x321);

      std::int32_t stored_ba0 = 0;
      std::int32_t object_194 = 0;
      CHECK(no_livery_tail(ctx, 0x66, &stored_ba0, 0x77, step, step, &object_194, resume) == 1);
      CHECK(stored_ba0 == 0 && object_194 == 0x77);
      CHECK(stored_arg == 0x66 && step_calls == 3);
      CHECK(resume_args[0] == 0x321 && resume_args[1] == 0x66);
    }

    // Class D reset stubs zero exactly their slot counts.
    {
      std::int32_t a = 1, b = 2, c = 3, d = 4, e = 5;
      std::int32_t* slots5[] = {&a, &b, &c, &d, &e};
      CHECK(reset_stub_10022ed0(slots5, 5) == 5);
      CHECK(a == 0 && b == 0 && c == 0 && d == 0 && e == 0);
      a = 1; b = 2; c = 3;
      std::int32_t* slots3[] = {&a, &b, &c};
      CHECK(reset_stub_1001ec30(slots3, 3) == 3);
      CHECK(a == 0 && b == 0 && c == 0);
      a = 1; b = 2; c = 3;
      CHECK(reset_stub_10023d70(slots3, 3) == 3);
      CHECK(reset_stub_10044130(slots3, 3) == 3);
      a = 1; b = 2;
      std::int32_t* slots2[] = {&a, &b};
      CHECK(reset_stub_1003d760(slots2, 2) == 2);
      CHECK(reset_stub_100453e0(slots2, 2) == 2);
      CHECK(reset_stub_10050820(slots2, 2) == 2);
      CHECK(reset_stub_100509e0(slots2, 2) == 2);
      CHECK(a == 0 && b == 0);
      // Counts are clamped to the documented slot counts.
      a = 1; b = 2; c = 3;
      CHECK(reset_stub_1003d760(slots3, 3) == 2);  // only two slots cleared
      CHECK(a == 0 && b == 0 && c == 3);
      CHECK(reset_stub_1001ec30(nullptr, 3) == 0);
    }
  }

  // ---- hook_install: reimplemented bodies replace the recovered destinations ----
  {
    using namespace zpatch;

    // The generated table covers every mapped destination with a live body.
    // 0x1001F030 (the HQR detour body) is resolved by the live ABI registry
    // (abi_hqr_body), not by this offline table: its old portable entry paired it
    // with the bike-physics loop body, which was wrong.
    std::size_t target_count = 0;
    recovered_hook_targets(target_count);
    CHECK(target_count == 59);  // 67 minus seven withheld shim bodies and the mispaired HQR row
    CHECK(resolve_recovered_hook_target(0x10005590u) ==
          reinterpret_cast<std::uintptr_t>(&hook_bodies::aim_projectile_crash_fix));
    CHECK(resolve_recovered_hook_target(0x10003e50u) ==
          reinterpret_cast<std::uintptr_t>(&game_hash));
    CHECK(resolve_recovered_hook_target(0x100469e0u) ==
          reinterpret_cast<std::uintptr_t>(&hook_bodies::super_lod_fix_shim));
    CHECK(resolve_recovered_hook_target(0x0BADF00Du) == 0);
    CHECK(default_hook_resolver(0x10038550u, nullptr) ==
          reinterpret_cast<std::uintptr_t>(&hook_bodies::rgsc_login_removed));

    // Crash probe (debug): arming serves the probe body for exactly one option's
    // destination and leaves the others alone; disarming restores the normal
    // body. Left disarmed so the live-pipeline tests below see the real bodies.
    {
      const auto plane_normal = hook_bodies_win_resolve(0x1003d870u, nullptr);
      const auto black_normal = hook_bodies_win_resolve(0x10036ee0u, nullptr);
      CHECK(plane_normal != 0 && black_normal != 0);
      CHECK(hook_bodies_win_set_probe("PlaneCrashFix"));
      CHECK(hook_bodies_win_resolve(0x1003d870u, nullptr) != plane_normal);
      CHECK(hook_bodies_win_resolve(0x1003d870u, nullptr) != 0);
      CHECK(hook_bodies_win_resolve(0x10036ee0u, nullptr) == black_normal);
      CHECK(!hook_bodies_win_set_probe("NoSuchFix"));
      CHECK(hook_bodies_win_resolve(0x1003d870u, nullptr) == plane_normal);
      const auto smpa_normal = hook_bodies_win_resolve(0x10046750u, nullptr);
      CHECK(hook_bodies_win_set_probe("SMPA60Fix"));
      CHECK(hook_bodies_win_resolve(0x10046750u, nullptr) != smpa_normal);
      CHECK(hook_bodies_win_resolve(0x10046750u, nullptr) != 0);
      CHECK(!hook_bodies_win_set_probe("0"));
      CHECK(hook_bodies_win_resolve(0x1003d870u, nullptr) == plane_normal);
      CHECK(hook_bodies_win_resolve(0x10046750u, nullptr) == smpa_normal);
    }

    // Plants a signature in a synthetic image: concrete tokens are written,
    // wildcards become zero bytes (the matcher skips them).
    const auto plant = [](std::vector<std::byte>& buffer, std::size_t offset,
                          std::string_view signature) {
      std::size_t index = offset;
      std::istringstream stream{std::string(signature)};
      std::string token;
      while (stream >> token) {
        if (token != "?" && token != "??") {
          unsigned value = 0;
          const std::size_t begin = token.rfind("0x", 0) == 0 ? 2 : 0;
          for (char ch : token.substr(begin)) {
            const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (lower >= '0' && lower <= '9') value = value * 16 + static_cast<unsigned>(lower - '0');
            else if (lower >= 'a' && lower <= 'f') value = value * 16 + static_cast<unsigned>(lower - 'a' + 10);
            else break;
          }
          buffer[index] = static_cast<std::byte>(value & 0xFFu);
        }
        ++index;
      }
    };

    static int protect_calls = 0;
    auto protect = +[](void*, std::size_t, std::uint32_t, std::uint32_t* previous) -> Status {
      protect_calls += 1;
      if (previous) *previous = 0x20;
      return {};
    };
    auto failing_protect = +[](void*, std::size_t, std::uint32_t, std::uint32_t*) -> Status {
      return {StatusCode::invalid_input, "denied"};
    };
    static std::uintptr_t fixed_destination = 0;
    auto resolver = +[](std::uint32_t, void*) -> std::uintptr_t { return fixed_destination; };
    auto empty_resolver = +[](std::uint32_t, void*) -> std::uintptr_t { return 0; };

    // Signature-relative install: gate, resolve, scan, write E9 + rel32.
    {
      std::size_t count = 0;
      const RecoveredSigJump* jumps = recovered_sig_jumps(count);
      const RecoveredSigJump* chosen = nullptr;
      for (std::size_t i = 0; i < count; ++i) {
        bool known = false;
        if (build_allows_option(GameBuild::v1080, jumps[i].option, known)) {
          chosen = &jumps[i];
          break;
        }
      }
      CHECK(chosen != nullptr);

      std::vector<std::byte> image(0x2000, std::byte{0xCC});
      plant(image, 0x100, chosen->signature);
      fixed_destination = 0x20001000;
      protect_calls = 0;
      HookInstallReport report;
      CHECK(install_recovered_sig_jumps(GameBuild::v1080, {image.data(), image.size()}, 0x400000,
                                        protect, resolver, nullptr, report));
      auto* site = image.data() + 0x100 + chosen->offset;
      bool found = false;
      for (const auto& outcome : report.outcomes) {
        if (outcome.reference_va == chosen->destination &&
            outcome.status == HookInstallStatus::installed) {
          found = true;
          CHECK(outcome.site == site);
          CHECK(outcome.resolved_va == 0x20001000);
        }
      }
      CHECK(found);
      CHECK(protect_calls >= 1);
      CHECK(std::to_integer<unsigned char>(*site) == 0xE9);
      std::int32_t displacement = 0;
      std::memcpy(&displacement, site + 1, sizeof(displacement));
      const auto site_va = 0x400000ull + static_cast<std::uintptr_t>(site - image.data());
      CHECK(displacement == static_cast<std::int32_t>(0x20001000ull - site_va - 5));

      // Without a reimplementation the install is skipped, not guessed.
      HookInstallReport no_dest;
      CHECK(install_recovered_sig_jumps(GameBuild::v1080, {image.data(), image.size()}, 0x400000,
                                        protect, empty_resolver, nullptr, no_dest));
      CHECK(no_dest.installed == 0 && no_dest.no_destination >= 1);

      // The runtime must never fall back to the recovered reference VA when a
      // resolver is present: installing the original module's address into a
      // rebuilt module is a dangling jump (the live DEP crash at 0x10003E50).
      {
        const char* skip_ini = "zpatch_skip_destination_test.ini";
        {
          std::size_t all_count = 0;
          const auto* all_ops = zpatch::recovered_patch_ops(all_count);
          std::vector<std::string> keys;
          for (std::size_t i = 0; i < all_count; ++i) {
            std::string key(all_ops[i].option);
            if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
          }
          std::ofstream out(skip_ini);
          out << "[Options]\n";
          for (const auto& key : keys) out << key << "=1\n";
        }
        CHECK(zpatch::initialize(skip_ini));
        std::vector<std::byte> img(0x4000, std::byte{0xcc});
        std::size_t sig_count = 0;
        const auto* sig_jumps = zpatch::recovered_sig_jumps(sig_count);
        const zpatch::RecoveredSigJump* chosen = nullptr;
        for (std::size_t i = 0; i < sig_count; ++i) {
          if (zpatch::recovered_feature_allows_build(
                  sig_jumps[i].option, static_cast<std::uint8_t>(zpatch::GameBuild::v1080))) {
            chosen = &sig_jumps[i];
            break;
          }
        }
        CHECK(chosen != nullptr);
        const std::string pattern(chosen->signature);
        std::size_t at = 0x1000, cursor = 0;
        const auto nib = [](char c) -> int {
          if (c >= '0' && c <= '9') return c - '0';
          if (c >= 'a' && c <= 'f') return c - 'a' + 10;
          if (c >= 'A' && c <= 'F') return c - 'A' + 10;
          return -1;
        };
        while (cursor < pattern.size()) {
          const auto sp = pattern.find(' ', cursor);
          const auto tk = pattern.substr(
              cursor, sp == std::string::npos ? std::string::npos : sp - cursor);
          if (tk.empty() || tk.size() > 2 || at >= img.size()) break;
          const char c0 = tk[0];
          const char c1 = tk.size() > 1 ? tk[1] : tk[0];
          const int hi = c0 == '?' ? -1 : nib(c0);
          const int lo = c1 == '?' ? -1 : nib(c1);
          if (hi >= 0 && lo >= 0) img[at] = static_cast<std::byte>((hi << 4) | lo);
          ++at;
          if (sp == std::string::npos) break;
          cursor = sp + 1;
        }
        const std::vector<std::byte> pristine = img;
        auto empty_resolver2 = +[](std::uint32_t, void*) -> std::uintptr_t { return 0; };
        zpatch::RuntimePatchReport report;
        zpatch::AppliedWriteLog log;
        CHECK(zpatch::apply_recovered_patches_logged(
            zpatch::GameBuild::v1080, {img.data(), img.size()}, &FakeProtect::protect, report,
            log, zpatch::kDefaultGameModuleBase, empty_resolver2, nullptr));
        // The chosen site must be untouched (no dangling jump installed).
        auto* site = img.data() + 0x1000 + static_cast<std::ptrdiff_t>(chosen->offset);
        for (std::size_t b = 0; b < 5; ++b)
          CHECK(site[b] == pristine[0x1000 + static_cast<std::ptrdiff_t>(chosen->offset) + b]);
        CHECK(report.no_destination >= 1);
        zpatch::shutdown();
        std::remove(skip_ini);
      }

      // A denied protection callback is reported as write_failed (re-plant the
      // signature: the successful install above overwrote its first bytes).
      plant(image, 0x100, chosen->signature);
      HookInstallReport denied;
      CHECK(install_recovered_sig_jumps(GameBuild::v1080, {image.data(), image.size()}, 0x400000,
                                        failing_protect, resolver, nullptr, denied));
      CHECK(denied.write_failed >= 1);

      // Gated builds skip before scanning or resolving.
      bool known = false;
      GameBuild other = GameBuild::v1000;
      for (std::uint8_t ordinal = 1; ordinal <= 18; ++ordinal) {
        const auto candidate = static_cast<GameBuild>(ordinal);
        if (!build_allows_option(candidate, chosen->option, known)) {
          other = candidate;
          break;
        }
      }
      if (known && other != GameBuild::v1080) {
        HookInstallReport gated;
        CHECK(install_recovered_sig_jumps(other, {image.data(), image.size()}, 0x400000, protect,
                                          resolver, nullptr, gated));
        CHECK(gated.installed == 0 && gated.gated_out >= 1);
      }
    }

    // Module-RVA install: the site is image base + module_rva, no scan.
    {
      std::size_t count = 0;
      const RecoveredRvaJump* jumps = recovered_rva_jumps(count);
      const RecoveredRvaJump* chosen = nullptr;
      for (std::size_t i = 0; i < count; ++i) {
        bool known = false;
        if (build_allows_option(GameBuild::v1080, jumps[i].option, known)) {
          chosen = &jumps[i];
          break;
        }
      }
      CHECK(chosen != nullptr);
      const std::size_t size = static_cast<std::size_t>(chosen->module_rva) + 0x10;
      std::vector<std::byte> image(size, std::byte{0});
      fixed_destination = 0x30002000;
      HookInstallReport report;
      CHECK(install_recovered_rva_jumps(GameBuild::v1080, {image.data(), image.size()}, 0x400000,
                                        protect, resolver, nullptr, report));
      auto* site = image.data() + chosen->module_rva;
      CHECK(std::to_integer<unsigned char>(*site) == 0xE9);
      std::int32_t displacement = 0;
      std::memcpy(&displacement, site + 1, sizeof(displacement));
      const auto site_va = 0x400000ull + chosen->module_rva;
      CHECK(displacement == static_cast<std::int32_t>(0x30002000ull - site_va - 5));
      CHECK(report.installed >= 1);
    }

    // Nth-match install: match index selection and the recovered opcode.
    {
      std::size_t count = 0;
      const RecoveredMatchJump* jumps = recovered_match_jumps(count);
      const RecoveredMatchJump* chosen = nullptr;
      for (std::size_t i = 0; i < count; ++i) {
        if (jumps[i].opcode == 0xE8) {
          chosen = &jumps[i];
          break;
        }
      }
      CHECK(chosen != nullptr);
      std::vector<std::byte> image(0x2000, std::byte{0xCC});
      plant(image, 0x100, chosen->signature);
      plant(image, 0x800, chosen->signature);
      fixed_destination = 0x20003000;
      HookInstallReport report;
      CHECK(install_recovered_match_jumps(GameBuild::v1080, {image.data(), image.size()}, 0x400000,
                                          protect, resolver, nullptr, report));
      auto* first = image.data() + 0x100 + chosen->offset;
      CHECK(std::to_integer<unsigned char>(*first) == 0xE8);
      // The companion entry (match index 1) writes the second planted match.
      bool saw_index1 = false;
      for (const auto& outcome : report.outcomes) {
        if (outcome.option == chosen->option && outcome.status == HookInstallStatus::installed &&
            outcome.site == image.data() + 0x800 + chosen->offset) {
          saw_index1 = true;
        }
      }
      CHECK(saw_index1);
    }
  }
  // ---- SkipLauncher: the DRM/launcher bypass recovered from 0x10045ce0 ----
  {
    using namespace zpatch;
    std::size_t entry_count = 0;
    const SkipLauncherEntry* entries = skip_launcher_entries(entry_count);
    std::size_t collector_count = 0;
    const SkipLauncherCollector* collectors = skip_launcher_collectors(collector_count);
    CHECK(entry_count == 25);
    CHECK(collector_count == 2);
    CHECK(kSkipLauncherJumpTargetEntry == 1);
    CHECK(entries[kSkipLauncherJumpTargetEntry].write_count == 0);

    auto protect = +[](void*, std::size_t, std::uint32_t, std::uint32_t*) -> Status {
      return {};
    };

    // Absent signatures are reported, never guessed.
    {
      std::vector<std::byte> empty(0x1000, std::byte{0xCC});
      SkipLauncherReport report;
      CHECK(apply_skip_launcher({empty.data(), empty.size()}, protect, nullptr, nullptr, nullptr,
                                report));
      CHECK(report.entries_found == 0 && report.entries_missing == 25);
      CHECK(report.writes == 0 && report.collector_writes == 0 && !report.jump_installed);
    }

    // Against the real GTAIV.exe image when the reference binary is present.
    // Only the entries whose signatures exist in 1.0.8.0 apply (6 of 25: the
    // others target other game builds); the handler patches whatever it finds.
    std::vector<std::byte> game;
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    if (map_pe_image("game_refs/GTAIV.exe", game, base, size) ||
        map_pe_image("../game_refs/GTAIV.exe", game, base, size) ||
        map_pe_image("../../game_refs/GTAIV.exe", game, base, size)) {
      const MappedImage image{game.data(), game.size()};
      // Locate every site first: most patches overwrite the signature bytes,
      // so the addresses must be captured before applying.
      std::vector<const std::byte*> sites(entry_count, nullptr);
      std::size_t found = 0;
      for (std::size_t i = 0; i < entry_count; ++i) {
        Match match;
        if (find_pattern({image.base, image.size}, entries[i].signature, match)) {
          sites[i] = match.address;
          ++found;
        }
      }
      CHECK(found == 6);
      CHECK(sites[1] && sites[6] && sites[9] && sites[10] && sites[21] && sites[22]);
      std::vector<const std::byte*> hits_a;
      std::vector<const std::byte*> hits_b;
      find_all_patterns({image.base, image.size}, collectors[0].pattern, hits_a);
      find_all_patterns({image.base, image.size}, collectors[1].pattern, hits_b);
      CHECK(hits_a.size() == 2);
      CHECK(hits_b.size() == 5);

      std::vector<std::byte> pristine = game;
      AppliedWriteLog log;
      SkipLauncherReport report;
      CHECK(apply_skip_launcher(image, protect, &log, nullptr, nullptr, report));
      CHECK(report.entries_found == 6 && report.entries_missing == 19);
      CHECK(report.jump_installed);
      CHECK(report.writes == 7);
      CHECK(report.collector_writes == 4);
      CHECK(report.create_file_slot != 0);   // captured even without a resolver
      CHECK(report.dfa_installs == 0 && report.rom_installs == 0);  // no resolver supplied
      CHECK(!log.empty());

      const auto at = [](const std::byte* site, std::size_t offset) {
        return std::to_integer<unsigned char>(site[offset]);
      };
      // Entry 6: six NOPs.
      CHECK(at(sites[6], 0) == 0x90 && at(sites[6], 5) == 0x90);
      // Entry 9: xor eax,eax; jmp to the second signature's match.
      CHECK(at(sites[9], 0) == 0x33 && at(sites[9], 1) == 0xC0 && at(sites[9], 2) == 0xE9);
      std::int32_t rel = 0;
      std::memcpy(&rel, sites[9] + 3, sizeof(rel));
      const auto expected = static_cast<std::int32_t>(
          reinterpret_cast<std::uintptr_t>(sites[1]) -
          (reinterpret_cast<std::uintptr_t>(sites[9]) + 3u + 4u));
      CHECK(rel == expected);
      // Entry 10: nop; mov [imm32], eax.
      CHECK(at(sites[10], 0) == 0x90 && at(sites[10], 1) == 0xA3);
      // Entries 21/22: NOP fills.
      CHECK(at(sites[21], 0) == 0x90 && at(sites[21], 15) == 0x90);
      CHECK(at(sites[22], 0) == 0x90 && at(sites[22], 13) == 0x90);
      // Collectors: first two of pattern A, last two of pattern B.
      CHECK(at(hits_a[0], 0) == 0xB0 && at(hits_a[0], 1) == 0x01 &&
            at(hits_a[0], 2) == 0xC3 && at(hits_a[0], 3) == 0x90);
      CHECK(at(hits_a[1], 0) == 0xB0);
      CHECK(at(hits_b[hits_b.size() - 2], 0) == 0xB0);
      CHECK(at(hits_b[hits_b.size() - 1], 0) == 0xB0);

      // Rollback restores every logged byte.
      for (auto it = log.rbegin(); it != log.rend(); ++it)
        std::memcpy(it->address, it->original, it->length);
      CHECK(game == pristine);

      // With a hook resolver the DFA file-API hooks and the ROM window patches
      // install E8/E9 jumps to the reimplemented bodies. Sites are captured
      // before applying because the installs overwrite the signatures.
      auto fake_resolver = +[](SkipHook hook, void*) -> std::uintptr_t {
        return 0x00200000u + static_cast<std::uintptr_t>(hook) * 0x100u;
      };
      std::size_t dfa_count = 0;
      const SkipLauncherInstall* dfa = skip_launcher_dfa_installs(dfa_count);
      std::size_t rom_count = 0;
      const SkipLauncherInstall* rom = skip_launcher_rom_installs(rom_count);
      CHECK(dfa_count == 6 && rom_count == 2);
      std::vector<const std::byte*> dfa_sites(dfa_count, nullptr);
      std::vector<const std::byte*> rom_sites(rom_count, nullptr);
      for (std::size_t i = 0; i < dfa_count; ++i) {
        Match match;
        if (find_pattern({image.base, image.size}, dfa[i].signature, match))
          dfa_sites[i] = match.address + dfa[i].offset;
      }
      for (std::size_t i = 0; i < rom_count; ++i) {
        Match match;
        if (find_pattern({image.base, image.size}, rom[i].signature, match))
          rom_sites[i] = match.address + rom[i].offset;
      }
      std::size_t collected_count = 0;
      const SkipLauncherCollectedInstall* collected = skip_launcher_dfa_collected(collected_count);
      CHECK(collected_count == 1);
      std::vector<const std::byte*> collected_hits;
      std::vector<const std::byte*> collected_targets;
      find_all_patterns({image.base, image.size}, collected[0].collector, collected_hits);
      for (std::size_t t = 0; t < collected[0].target_count; ++t) {
        Match match;
        find_pattern({image.base, image.size}, collected[0].targets[t].target_signature, match);
        collected_targets.push_back(match.address);
      }
      CHECK(collected_hits.size() == 2 && collected_targets.size() == 2);

      SkipLauncherReport hook_report;
      CHECK(apply_skip_launcher(image, protect, nullptr, fake_resolver, nullptr, hook_report));
      CHECK(hook_report.dfa_installs == 6 && hook_report.dfa_missing == 0);
      CHECK(hook_report.rom_installs == 2 && hook_report.rom_missing == 0);
      CHECK(hook_report.collected_installs == 2);
      const auto jump_ok = [&](const std::byte* site, std::uint8_t opcode, SkipHook hook) {
        std::int32_t rel = 0;
        std::memcpy(&rel, site + 1, sizeof(rel));
        const auto expected = static_cast<std::int32_t>(
            fake_resolver(hook, nullptr) -
            (reinterpret_cast<std::uintptr_t>(site) + 5u));
        return std::to_integer<unsigned char>(site[0]) == opcode && rel == expected;
      };
      for (std::size_t i = 0; i < dfa_count; ++i) {
        CHECK(dfa_sites[i] != nullptr);
        CHECK(jump_ok(dfa_sites[i], dfa[i].opcode, dfa[i].hook));
      }
      for (std::size_t i = 0; i < rom_count; ++i) {
        CHECK(rom_sites[i] != nullptr);
        CHECK(jump_ok(rom_sites[i], rom[i].opcode, rom[i].hook));
      }
      // Collected installs jump from the collected sites to the target matches.
      for (std::size_t t = 0; t < collected[0].target_count; ++t) {
        const std::byte* site = collected_hits[collected[0].targets[t].index];
        std::int32_t rel = 0;
        std::memcpy(&rel, site + 1, sizeof(rel));
        const auto expected = static_cast<std::int32_t>(
            reinterpret_cast<std::uintptr_t>(collected_targets[t]) -
            (reinterpret_cast<std::uintptr_t>(site) + 5u));
        CHECK(std::to_integer<unsigned char>(site[0]) == 0xE9 && rel == expected);
      }
    }
  }
  // ---- Live-apply safety: the classes behind both live crashes ----
  // 1. no write may leave the game module;
  // 2. no written 4-byte value may be a dangling address of the ORIGINAL module
  //    (the rebase table must cover it, or the write must be skipped);
  // 3. no jump may target the original module (unknown destinations are
  //    skipped, never installed as dangling rel32s).
#if defined(_WIN32)
  {
    using namespace zpatch;
    std::vector<std::byte> game;
    std::uint32_t base = 0;
    std::uint32_t size = 0;
    if (map_pe_image("game_refs/GTAIV.exe", game, base, size) ||
        map_pe_image("../game_refs/GTAIV.exe", game, base, size)) {
      // Finding N4: the site_deref rows (CrashMsgFix's `*(match-4)`) carry live
      // VAs, so the whole-pipeline applies must run against an image mapped at
      // its live base (0x400000), exactly like the game. A heap mapping only
      // makes those pointers foreign; the product maps at the live base.
      void* live_mapping = VirtualAlloc(reinterpret_cast<void*>(0x400000u), game.size(),
                                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
      std::byte* live_base = live_mapping ? static_cast<std::byte*>(live_mapping)
                                          : game.data();
      std::memcpy(live_base, game.data(), game.size());
      const char* live_ini = "zpatch_live_safety_test.ini";
      {
        std::size_t all_count = 0;
        const auto* all_ops = recovered_patch_ops(all_count);
        std::vector<std::string> keys;
        for (std::size_t i = 0; i < all_count; ++i) {
          std::string key(all_ops[i].option);
          if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
        }
        std::ofstream out(live_ini);
        out << "[Options]\n";
        for (const auto& key : keys) out << key << "=1\n";
        out << "SkipLauncher=1\n";
      }
      CHECK(initialize(live_ini));
      const MappedImage image{live_base, game.size()};
      std::vector<std::byte> pristine = game;
      auto resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
        return hook_bodies_win_resolve(va, nullptr);
      };
      // Diagnostic: dump every applied write for the real game_refs INI so the
      // live patch set can be audited against the user's configuration. Runs
      // before the main apply (below) so the runtime state and slot globals end
      // up bound to `game`, and restores the synthetic all-options config.
      {
        std::ifstream probe("game_refs/ZolikaPatch.ini");
        if (!probe.good()) probe.open("../game_refs/ZolikaPatch.ini");
        if (probe.good()) {
          probe.close();
          zpatch::shutdown();
          CHECK(initialize("game_refs/ZolikaPatch.ini") ||
                initialize("../game_refs/ZolikaPatch.ini"));
          std::memcpy(live_base, pristine.data(), pristine.size());
          RuntimePatchReport r2;
          AppliedWriteLog log2;
          CHECK(apply_recovered_patches_logged(GameBuild::v1080, {live_base, game.size()},
                                               &FakeProtect::protect, r2, log2,
                                               kDefaultGameModuleBase, resolver, nullptr));
          std::printf("DIAG applied=%zu suppressed=%zu\n", r2.applied, r2.suppressed);
          for (const auto& e : log2) {
            const auto off = static_cast<std::size_t>(e.address - live_base);
            std::printf("DIAG %-30s rva=%#08zx len=%zu bytes=",
                        std::string(e.option).c_str(), off, e.length);
            for (std::size_t b = 0; b < e.length && b < 8; ++b)
              std::printf("%02X", std::to_integer<unsigned char>(e.address[b]));
            std::printf("\n");
          }
          zpatch::shutdown();
          CHECK(initialize(live_ini));
          std::memcpy(live_base, pristine.data(), pristine.size());
        }
      }
      // INI gating: a disabled option's installs must never run. SkipMenu=0
      // still skipping the menu was the boot black screen. Runs before the main
      // apply so the slot globals end up bound to `game`.
      {
        std::vector<std::byte> gated_img;
        std::uint32_t gb = 0;
        std::uint32_t gs = 0;
        CHECK(map_pe_image("game_refs/GTAIV.exe", gated_img, gb, gs) ||
              map_pe_image("../game_refs/GTAIV.exe", gated_img, gb, gs));
        // SkipIntro on, every other recovered option explicitly off (absent keys
        // now take their arm default, usually "1").
        ConfigDocument intro_only = single_option_config("SkipIntro");
        auto gated_resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
          return hook_bodies_win_resolve(va, nullptr);
        };
        SigInstallReport gated_report;
        AppliedWriteLog gated_log;
        CHECK(apply_sig_installs(intro_only, {gated_img.data(), gated_img.size()},
                                 zpatch::GameBuild::v1080,
                                 kDefaultGameModuleBase, &FakeProtect::protect, &gated_log,
                                 gated_resolver, nullptr, gated_report));
        CHECK(gated_report.applied == 1);   // SkipIntro's E9 only
        // Every other set (SkipMenu's calls and the ISH/NMS/BetterMPSync/
        // CombinedRadioFreezeFix sets, whose options are absent from this config)
        // is gated, never written.
                CHECK(gated_report.gated_out == 117);
        const std::string menu_sig =
            "83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E";
        Match menu_match;
        CHECK(find_pattern({gated_img.data(), gated_img.size()}, menu_sig, menu_match));
        std::int32_t menu_rel = 0;
        std::memcpy(&menu_rel, menu_match.address + 24, sizeof(menu_rel));
        const auto menu_target = reinterpret_cast<std::uintptr_t>(menu_match.address) + 28u +
                                 static_cast<std::uintptr_t>(
                                     static_cast<std::int32_t>(menu_rel));
        CHECK(menu_target != hook_bodies_win_resolve(0x10046610u, nullptr));
        SlotInstallReport gated_slots;
        CHECK(apply_slot_installs(intro_only, {gated_img.data(), gated_img.size()},
                                  GameBuild::v1080, &FakeProtect::protect, &gated_log,
                                  gated_slots));
                CHECK(gated_slots.applied == 1 && gated_slots.gated_out == 131);  // the shim publishes are gated too
      }
      // MouseFix: the literal_bytes rows (dest_kind 6) write inside their own
      // pattern, so the patch set must scan once and apply all four P3 writes
      // (and P5's two) atomically - a per-row rescan would fail after the first
      // in-pattern write.
      {
        std::vector<std::byte> mf_img;
        std::uint32_t mfb = 0;
        std::uint32_t mfs = 0;
        CHECK(map_pe_image("game_refs/GTAIV.exe", mf_img, mfb, mfs) ||
              map_pe_image("../game_refs/GTAIV.exe", mf_img, mfb, mfs));
        ConfigDocument mf_cfg = single_option_config("MouseFix");
        auto mf_resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
          return hook_bodies_win_resolve(va, nullptr);
        };
        std::vector<std::byte> mf_pristine = mf_img;  // the writes break the patterns
        SigInstallReport mf_report;
        AppliedWriteLog mf_log;
        CHECK(apply_sig_installs(mf_cfg, {mf_img.data(), mf_img.size()},
                                 zpatch::GameBuild::v1080, kDefaultGameModuleBase,
                                 &FakeProtect::protect, &mf_log, mf_resolver, nullptr,
                                 mf_report));
        CHECK(mf_report.applied == 17);
        CHECK(mf_report.missing == 0 && mf_report.skipped_no_destination == 0);
        const std::string p3 =
            "83 C4 08 F7 D8 F3 0F 2A C0 6A 00 8B CE F3 0F 11 07";
        Match mf3;
        CHECK(find_pattern({mf_pristine.data(), mf_pristine.size()}, p3, mf3));
        auto* m3 = mf_img.data() + (mf3.address - mf_pristine.data());
        CHECK(std::to_integer<unsigned char>(m3[3]) == 0x90);
        CHECK(std::to_integer<unsigned char>(m3[4]) == 0x90);
        CHECK(std::to_integer<unsigned char>(m3[5]) == 0x66);
        CHECK(std::to_integer<unsigned char>(m3[8]) == 0xC0);
        CHECK(std::to_integer<unsigned char>(m3[0x1Fu]) == 0x90);
        CHECK(std::to_integer<unsigned char>(m3[0x20u]) == 0x90);
        CHECK(std::to_integer<unsigned char>(m3[0x21u]) == 0x66);
        const std::string p5 = "83 C4 08 F7 D8 F3 0F 2A C8 F3 0F 10 46";
        Match mf5;
        CHECK(find_pattern({mf_pristine.data(), mf_pristine.size()}, p5, mf5));
        auto* m5 = mf_img.data() + (mf5.address - mf_pristine.data());
        CHECK(std::to_integer<unsigned char>(m5[3]) == 0x90);
        CHECK(std::to_integer<unsigned char>(m5[5]) == 0x66);
        CHECK(std::to_integer<unsigned char>(m5[8]) == 0xC8);
      }
      // OutOfCommissionFix: the embedded "finale2.sco" blob must decode to the
      // 179536-byte table whose OOC Adler-32 matches the blob's own trailer, and
      // the built memory-mount archive must match the golden layout/hash
      // (tools/extract_ooc_pak.py regenerates the reference).
      CHECK(zpatch::zpatch_ooc_selftest());
      // HighQualityReflections: the E9 at the pattern's match (the game's
      // `mov eax,[edi+0x938]`) plus the two publishes - resume = match+0x13 and
      // builder = match + *(match+0xF) + 0x13 (the game's call displacement).
      {
        std::vector<std::byte> img;
        std::uint32_t b = 0, s = 0;
        CHECK(map_pe_image("game_refs/GTAIV.exe", img, b, s) ||
              map_pe_image("../game_refs/GTAIV.exe", img, b, s));
        const std::string pat =
            "8B 87 38 09 00 00 6A 08 68 1D 13 00 00 50 E8";
        Match m;
        CHECK(find_pattern({img.data(), img.size()}, pat, m));
        const auto match = reinterpret_cast<std::uintptr_t>(m.address);
        std::uint32_t rel = 0;
        std::memcpy(&rel, m.address + 0x0F, sizeof(rel));
        ConfigDocument hqr_cfg = single_option_config("HighQualityReflections");
        auto hqr_resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
          return hook_bodies_win_resolve(va, nullptr);
        };
        // Slot publishes run before the site installs (they read the original
        // call displacement at match+0xF), exactly like the live pipeline.
        SlotInstallReport hqr_slots;
        CHECK(apply_slot_installs(hqr_cfg, {img.data(), img.size()},
                                  zpatch::GameBuild::v1080, &FakeProtect::protect, nullptr,
                                  hqr_slots));
        CHECK(hqr_slots.applied == 2);
        CHECK(zpatch::zpatch_hqr_resume ==
              static_cast<std::uint32_t>(match + 0x13));
        CHECK(zpatch::zpatch_hqr_builder ==
              static_cast<std::uint32_t>(match + rel + 0x13));
        CHECK(zpatch::zpatch_hqr_flags == 0x11Du);
        SigInstallReport hqr_report;
        AppliedWriteLog hqr_log;
        CHECK(apply_sig_installs(hqr_cfg, {img.data(), img.size()},
                                 zpatch::GameBuild::v1080, kDefaultGameModuleBase,
                                 &FakeProtect::protect, &hqr_log, hqr_resolver, nullptr,
                                 hqr_report));
        CHECK(hqr_report.applied == 1 && hqr_report.missing == 0);
        CHECK(std::to_integer<unsigned char>(m.address[0]) == 0xE9);
      }
      // CutsceneFixes chain B: the worker's two E8 installs land at match-5 of
      // the two recovered patterns (the game camera call sites) and the scan-1
      // publishes carry the site's original call target and target+0x4F. The
      // pattern set has 1 hit on every classic build + EFLC 1110/1120/1130 and
      // is absent on 12059; verified here on 1.0.8.0 and 1.0.4.0.
      {
        const std::string cf_a = "8B 4C 24 2C 5F 5E 5B 33 CC B0 01 E8";
        const std::string cf_b = "8B CF 88 44 24 0F E8 ? ? ? ? A1";
        const auto check_cutscene = [&](const char* exe, zpatch::GameBuild build) {
          std::vector<std::byte> img;
          std::uint32_t b = 0, s = 0;
          const std::string fallback = std::string("../") + exe;
          if (!map_pe_image(exe, img, b, s) && !map_pe_image(fallback.c_str(), img, b, s))
            return false;
          ConfigDocument cfg = single_option_config("CutsceneFixes");
          auto resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
            return hook_bodies_win_resolve(va, nullptr);
          };
          Match ma, mb;
          if (!find_pattern({img.data(), img.size()}, cf_a, ma)) return false;
          if (!find_pattern({img.data(), img.size()}, cf_b, mb)) return false;
          // Capture the original call displacements before the installs
          // overwrite them (the worker publishes them before its E8 write).
          std::int32_t rel_a = 0, rel_b = 0;
          std::memcpy(&rel_a, ma.address - 4, sizeof(rel_a));
          std::memcpy(&rel_b, mb.address - 4, sizeof(rel_b));
          const auto target_a = static_cast<std::uint32_t>(
              reinterpret_cast<std::uintptr_t>(ma.address) + rel_a);
          SlotInstallReport slots;
          if (!apply_slot_installs(cfg, {img.data(), img.size()}, build,
                                   &FakeProtect::protect, nullptr, slots))
            return false;
          if (slots.applied != 2) return false;
          if (zpatch::zpatch_cutscene_target != target_a) return false;
          if (zpatch::zpatch_cutscene_poke != target_a + 0x4Fu) return false;
          SigInstallReport sig;
          AppliedWriteLog log;
          if (!apply_sig_installs(cfg, {img.data(), img.size()}, build,
                                  kDefaultGameModuleBase, &FakeProtect::protect, &log,
                                  resolver, nullptr, sig))
            return false;
          if (sig.applied != 2 || sig.missing != 0 || sig.skipped_no_destination != 0)
            return false;
          const auto body = static_cast<std::uintptr_t>(
              hook_bodies_win_resolve(0x100388C0u, nullptr));
          for (const std::byte* match : {ma.address, mb.address}) {
            const auto site = reinterpret_cast<std::uintptr_t>(match) - 5u;
            const auto want = static_cast<std::int32_t>(body - (site + 5u));
            std::int32_t got = 0;
            if (std::to_integer<unsigned char>(match[-5]) != 0xE8) return false;
            std::memcpy(&got, match - 4, sizeof(got));
            if (got != want) return false;
          }
          return true;
        };
        CHECK(check_cutscene("game_refs/GTAIV.exe", zpatch::GameBuild::v1080));
        CHECK(check_cutscene("game_refs/GTAIV_1040.exe", zpatch::GameBuild::v1040));
      }
      // CutsceneFixes chain A: the hidden worker builds a pattern text
      // char-by-char, scans the main module (first match) and publishes it;
      // the loader callback writes a single EB at that address. The text is
      // build-variant with a fallback scan: primary
      // "74 24 8B 44 24 2C 83 F8 03" (1 hit on 1004/1060/1070/1080 + EFLC
      // 1110/1120/1130; on 1080 the match is the `je` at game 0xC9A840 inside
      // the camera fn F), fallback "74 0B 83 7C 24 2C 03" (1 hit on
      // 1000/1010/1040); neither exists on 12059. The fallback is only scanned
      // when the primary has no match (the original's second scan).
      {
        const std::string ca_primary = "74 24 8B 44 24 2C 83 F8 03";
        const std::string ca_fallback = "74 0B 83 7C 24 2C 03";
        ConfigDocument ca_cfg = single_option_config("CutsceneFixes");
        const auto open_build = [&](const char* exe, std::vector<std::byte>& img) {
          std::uint32_t b = 0, s = 0;
          const std::string f = std::string("../") + exe;
          return map_pe_image(exe, img, b, s) || map_pe_image(f.c_str(), img, b, s);
        };
        // 1080: the primary matches (the je at 0xC9A840); the EB turns it into
        // an unconditional jmp to its own branch target.
        {
          std::vector<std::byte> img;
          if (open_build("game_refs/GTAIV.exe", img)) {
            Match m;
            CHECK(find_pattern({img.data(), img.size()}, ca_primary, m));
            // VA 0xC9A840 with image base 0x400000 -> 0x89A840 into the map.
            CHECK(static_cast<std::uintptr_t>(m.address - img.data()) == 0x89A840u);
            CHECK(std::to_integer<unsigned char>(m.address[0]) == 0x74);
            SigWriteReport rep;
            CHECK(apply_sig_writes(ca_cfg, {img.data(), img.size()},
                                   zpatch::GameBuild::v1080, &FakeProtect::protect, nullptr,
                                   rep));
            CHECK(rep.applied == 1 && rep.missing == 0);
            CHECK(std::to_integer<unsigned char>(m.address[0]) == 0xEB);
          }
        }
        // 1040: the primary is absent, the fallback matches, and the EB lands
        // on the fallback's match.
        {
          std::vector<std::byte> img;
          if (open_build("game_refs/GTAIV_1040.exe", img)) {
            Match primary_m;
            CHECK(!find_pattern({img.data(), img.size()}, ca_primary, primary_m));
            Match m;
            CHECK(find_pattern({img.data(), img.size()}, ca_fallback, m));
            SigWriteReport rep;
            CHECK(apply_sig_writes(ca_cfg, {img.data(), img.size()},
                                   zpatch::GameBuild::v1040, &FakeProtect::protect, nullptr,
                                   rep));
            CHECK(rep.applied == 1 && rep.missing == 0);
            CHECK(std::to_integer<unsigned char>(m.address[0]) == 0xEB);
          }
        }
        // Both texts present (planted fallback copy): the original only scans
        // the fallback when the primary has NO match, so the EB must land on
        // the primary's match and the planted copy must stay untouched.
        {
          std::vector<std::byte> img;
          if (open_build("game_refs/GTAIV.exe", img)) {
            const std::byte planted[] = {std::byte{0x74}, std::byte{0x0B}, std::byte{0x83},
                                         std::byte{0x7C}, std::byte{0x24}, std::byte{0x2C},
                                         std::byte{0x03}};
            const std::size_t plant_off = img.size();
            img.resize(img.size() + sizeof(planted));
            std::memcpy(img.data() + plant_off, planted, sizeof(planted));
            Match m;
            CHECK(find_pattern({img.data(), img.size()}, ca_primary, m));
            SigWriteReport rep;
            CHECK(apply_sig_writes(ca_cfg, {img.data(), img.size()},
                                   zpatch::GameBuild::v1080, &FakeProtect::protect, nullptr,
                                   rep));
            CHECK(rep.applied == 1);
            CHECK(std::to_integer<unsigned char>(m.address[0]) == 0xEB);
            CHECK(std::to_integer<unsigned char>(img[plant_off]) == 0x74);
          }
        }
      }
      // NewMemorySystem: the handler's E8/E9 pair lands on the 1080 exe's first
      // scan (E8 at match-34 -> the vtable-installing body, E9 at match-29 with
      // displacement 8), the second scan publishes the allocator singleton
      // (the immediate of `mov esi, 0x19cf9f0`), and the reimplemented
      // allocator allocates/aligns/frees and answers the registry query.
      {
        std::vector<std::byte> img;
        std::uint32_t b = 0, s = 0;
        CHECK(map_pe_image("game_refs/GTAIV.exe", img, b, s) ||
              map_pe_image("../game_refs/GTAIV.exe", img, b, s));
        ConfigDocument cfg = single_option_config("NewMemorySystem");
        auto resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
          return hook_bodies_win_resolve(va, nullptr);
        };
        zpatch::zpatch_nms_object = 0;
        SlotInstallReport slots;
        CHECK(apply_slot_installs(cfg, {img.data(), img.size()}, zpatch::GameBuild::v1080,
                                  &FakeProtect::protect, nullptr, slots));
        CHECK(slots.applied == 1);
        CHECK(zpatch::zpatch_nms_object == 0x19CF9F0u);
        SigInstallReport sig;
        AppliedWriteLog log;
        CHECK(apply_sig_installs(cfg, {img.data(), img.size()}, zpatch::GameBuild::v1080,
                                 kDefaultGameModuleBase, &FakeProtect::protect, &log,
                                 resolver, nullptr, sig));
        CHECK(sig.applied == 2 && sig.missing == 0 && sig.skipped_no_destination == 0);
        Match nms_match;
        CHECK(find_pattern({img.data(), img.size()}, "3D 80 99 99 59 73 05", nms_match));
        const auto* e8_site = nms_match.address - 34;
        const auto* e9_site = nms_match.address - 29;
        CHECK(std::to_integer<unsigned char>(e8_site[0]) == 0xE8);
        CHECK(std::to_integer<unsigned char>(e9_site[0]) == 0xE9);
        std::int32_t rel = 0;
        std::memcpy(&rel, e8_site + 1, sizeof(rel));
        const auto body = static_cast<std::uintptr_t>(
            hook_bodies_win_resolve(0x10038670u, nullptr));
        CHECK(rel == static_cast<std::int32_t>(body - (reinterpret_cast<std::uintptr_t>(e8_site) + 5u)));
        std::memcpy(&rel, e9_site + 1, sizeof(rel));
        CHECK(rel == 8);
        CHECK(zpatch::zpatch_nms_vtable[2] != 0 && zpatch::zpatch_nms_vtable[18] != 0);
        // The allocator: alignment, 0xCD fill, registry membership, free.
        void* p = zpatch::zpatch_nms_alloc(0x20u, 0x40u, 0u);
        CHECK(p != nullptr);
        CHECK((reinterpret_cast<std::uintptr_t>(p) % 0x40u) == 0);
        for (int i = 0; i < 0x20; ++i)
          CHECK(static_cast<unsigned char*>(p)[i] == 0xCD);
        CHECK(zpatch::zpatch_nms_contains(static_cast<std::uint32_t>(
                  reinterpret_cast<std::uintptr_t>(p))) == 1);
        zpatch::zpatch_nms_free(p);
        CHECK(zpatch::zpatch_nms_contains(static_cast<std::uint32_t>(
                  reinterpret_cast<std::uintptr_t>(p))) == 0);
        // Default alignment unit is 0x10 and the fill covers max(size, unit).
        void* q = zpatch::zpatch_nms_alloc(1u, 1u, 0u);
        CHECK(q != nullptr);
        CHECK((reinterpret_cast<std::uintptr_t>(q) % 0x10u) == 0);
        for (int i = 0; i < 0x10; ++i)
          CHECK(static_cast<unsigned char*>(q)[i] == 0xCD);
        zpatch::zpatch_nms_free(q);
        zpatch::zpatch_nms_free(nullptr);   // null is a no-op
      }
      // Per-build variant rows, verified against the 1.0.4.0 exe (build ordinal 5):
      // CarDynamic B/C/D/E branch, CarParts' 83 FA 04 75 0C variant, the
      // PopupHeadlightsFlag EDX twin and the trio fallback.
      {
        const auto apply_for_1040 = [](const ConfigDocument& cfg,
                                       std::vector<std::byte>& img,
                                       std::vector<std::byte>& pristine,
                                       SigInstallReport& si, SlotInstallReport& sl,
                                       SigWriteReport& sw) {
          std::uint32_t b = 0, s = 0;
          if (!map_pe_image("game_refs/GTAIV_1040.exe", img, b, s) &&
              !map_pe_image("../game_refs/GTAIV_1040.exe", img, b, s))
            return false;
          pristine = img;
          auto resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
            return hook_bodies_win_resolve(va, nullptr);
          };
          if (!apply_slot_installs(cfg, {img.data(), img.size()}, zpatch::GameBuild::v1040,
                                   &FakeProtect::protect, nullptr, sl))
            return false;
          if (!apply_sig_installs(cfg, {img.data(), img.size()}, zpatch::GameBuild::v1040,
                                  kDefaultGameModuleBase, &FakeProtect::protect, nullptr,
                                  resolver, nullptr, si))
            return false;
          return static_cast<bool>(apply_sig_writes(cfg, {img.data(), img.size()},
                                                    zpatch::GameBuild::v1040,
                                                    &FakeProtect::protect, nullptr, sw));
        };
        const auto cfg_of = [](std::initializer_list<const char*> keys) {
          ConfigDocument cfg;
          for (const char* key : keys) {
            ConfigEntry e;
            e.section = "Options";
            e.key = key;
            e.value = "1";
            cfg.entries.push_back(e);
          }
          return cfg;
        };
        const auto site_of = [](const std::vector<std::byte>& pristine,
                                const std::string& pat) -> const std::byte* {
          Match m;
          if (!find_pattern({pristine.data(), pristine.size()}, pat, m)) return nullptr;
          return m.address;
        };

        // CarDynamicShadowFix: pattern B (E9@0 -> 0x100068D0 + 2 publishes),
        // pattern C (E9@C+3 -> C+0x1E), pattern D (dword_1015BDC4 = Dmatch) and
        // the E-missing 6-byte NOP fill at C+0x3E.
        {
          std::vector<std::byte> img, pristine;
          SigInstallReport si;
          SlotInstallReport sl;
          SigWriteReport sw;
          CHECK(apply_for_1040(cfg_of({"CarDynamicShadowFix"}), img, pristine, si, sl, sw));
          const std::string pb =
              "56 6A 02 6A 00 6A 00 57 E8 ? ? ? ? 56 6A 02 6A 04 6A 05 57 E8";
          const std::string pc = "83 F8 14 7D 05 83 F8 06 7D 14";
          const std::string pd = "8B 45 10 8B 16 8B 92 8C 00 00 00";
          const auto* b_match = site_of(pristine, pb);
          const auto* c_match = site_of(pristine, pc);
          const auto* d_match = site_of(pristine, pd);
          CHECK(b_match && c_match && d_match);
          const auto off = [&](const std::byte* q) {
            return static_cast<std::ptrdiff_t>(q - pristine.data());
          };
          CHECK(std::to_integer<unsigned char>(img[off(b_match)]) == 0xE9);
          CHECK(std::to_integer<unsigned char>(img[off(c_match) + 3]) == 0xE9);
          for (int k = 0; k < 6; ++k)
            CHECK(std::to_integer<unsigned char>(img[off(c_match) + 0x3E + k]) == 0x90);
          CHECK(si.applied >= 3);  // B, C and the E-fill
          CHECK(sl.applied >= 3);  // B's two publishes + D
          const auto ia = [&](const std::byte* q) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(img.data()) +
                                              (q - pristine.data()));
          };
          const std::uint32_t rel9 = *reinterpret_cast<const std::uint32_t*>(b_match + 9);
          CHECK(zpatch::zpatch_carparts_resume_2 == ia(b_match) + 0x0D);
          CHECK(zpatch::zpatch_carparts_shadow_fn == ia(b_match) + rel9 + 0x0D);
        }
        // CarPartsShadowFix standalone: the old-build variant writes EB at +3.
        {
          std::vector<std::byte> img, pristine;
          SigInstallReport si;
          SlotInstallReport sl;
          SigWriteReport sw;
          CHECK(apply_for_1040(cfg_of({"CarPartsShadowFix"}), img, pristine, si, sl, sw));
          const std::string pv = "83 FA 04 75 0C";
          const auto* m = site_of(pristine, pv);
          CHECK(m);
          const auto off = static_cast<std::ptrdiff_t>(m - pristine.data());
          CHECK(std::to_integer<unsigned char>(img[off + 3]) == 0xEB);
          CHECK(sw.applied >= 1);
        }
        // PopupHeadlightsFlag EDX twin: E9@0 -> 0x10044560, EB@10, the
        // rebased-global operand at +82 and the two resume publishes.
        {
          std::vector<std::byte> img, pristine;
          SigInstallReport si;
          SlotInstallReport sl;
          SigWriteReport sw;
          CHECK(apply_for_1040(cfg_of({"PopupHeadlightsFlag"}), img, pristine, si, sl, sw));
          const std::string pt = "0F BF 56 2E 3B 15 ? ? ? ? 75 ? A1";
          const auto* m = site_of(pristine, pt);
          CHECK(m);
          const auto off = static_cast<std::ptrdiff_t>(m - pristine.data());
          CHECK(std::to_integer<unsigned char>(img[off]) == 0xE9);
          CHECK(std::to_integer<unsigned char>(img[off + 10]) == 0xEB);
          CHECK(si.applied >= 3 && sl.applied >= 2);
          const auto ia = [&](const std::byte* q) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(img.data()) +
                                              (q - pristine.data()));
          };
          CHECK(zpatch::zpatch_popup_resume_b == ia(m) + 10);
          CHECK(zpatch::zpatch_popup_resume_a == ia(m) + 12);
        }
        // Trio fallback: E9@match+2 -> 0x100051E0, resume match+16 and the
        // part-animation callee = match + *(match+140) + 144.
        {
          std::vector<std::byte> img, pristine;
          SigInstallReport si;
          SlotInstallReport sl;
          SigWriteReport sw;
          CHECK(apply_for_1040(cfg_of({"MovingSteeringWheelSupport"}), img, pristine, si, sl, sw));
          const std::string pf = "7C E0 8B 06 8B 90 28 01 00 00 8B CE FF D2";
          const auto* m = site_of(pristine, pf);
          CHECK(m);
          const auto off = static_cast<std::ptrdiff_t>(m - pristine.data());
          CHECK(std::to_integer<unsigned char>(img[off + 2]) == 0xE9);
          CHECK(si.applied >= 1 && sl.applied >= 2);
          const auto ia = [&](const std::byte* q) {
            return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(img.data()) +
                                              (q - pristine.data()));
          };
          const std::uint32_t rel140 = *reinterpret_cast<const std::uint32_t*>(m + 140);
          CHECK(zpatch::zpatch_steer_resume == ia(m) + 16);
          CHECK(zpatch::zpatch_steer_vtable_fn == ia(m) + rel140 + 144);
        }
      }
      // CarPartsShadowFix's sub_10007E40 EB row (sig_write) must only run on the
      // standalone CarPartsShadowFix==1 arm: the loader arm calls sub_10006910 +
      // sub_10007E40, while the shared CarDynamicShadowFix installer calls only
      // sub_10006910. With CarDynamicShadowFix=1 / CarPartsShadowFix=0 exactly
      // the four CarDynamicShadowFix sig_writes apply.
      {
        const std::string p =
            "83 F8 04 C6 44 24 0E 00 75 0F";
        ConfigDocument shared_cfg = all_options_off();
        set_option(shared_cfg, "CarDynamicShadowFix", "1");
        set_option(shared_cfg, "CarPartsShadowFix", "0");
        {
          std::vector<std::byte> img;
          std::uint32_t b = 0, s = 0;
          CHECK(map_pe_image("game_refs/GTAIV.exe", img, b, s) ||
                map_pe_image("../game_refs/GTAIV.exe", img, b, s));
          Match m;
          CHECK(find_pattern({img.data(), img.size()}, p, m));
          SigWriteReport rep;
          CHECK(apply_sig_writes(shared_cfg, {img.data(), img.size()},
                                 zpatch::GameBuild::v1080, &FakeProtect::protect, nullptr,
                                 rep));
          CHECK(rep.applied == 2);  // pattern F's EB + sub_1000EA00's byte
          auto* site = const_cast<std::byte*>(m.address) + 8;
          CHECK(std::to_integer<unsigned char>(site[0]) == 0x75);  // untouched
        }
        {
          std::vector<std::byte> img;
          std::uint32_t b = 0, s = 0;
          CHECK(map_pe_image("game_refs/GTAIV.exe", img, b, s) ||
                map_pe_image("../game_refs/GTAIV.exe", img, b, s));
          ConfigDocument solo = single_option_config("CarPartsShadowFix");
          Match m;
          CHECK(find_pattern({img.data(), img.size()}, p, m));
          const auto site_off = static_cast<std::ptrdiff_t>(m.address - img.data());
          SigWriteReport rep;
          CHECK(apply_sig_writes(solo, {img.data(), img.size()},
                                 zpatch::GameBuild::v1080, &FakeProtect::protect, nullptr,
                                 rep));
          CHECK(rep.applied == 1);
          auto* site = img.data() + site_off + 8;
          CHECK(std::to_integer<unsigned char>(site[0]) == 0xEB);
        }
      }
      // ForceCarHeadlightShadows=1: the FCHS handler's tail calls sub_1000E870
      // (1001AF0A), which restores the two push immediates and the jz +0x9E at
      // the three pattern sites (the "re-enable headlight shadows" direction).
      // With FCHS=0 (the shipped default) E870 is not called and those sites
      // stay untouched; the a2 path never patches them (the earlier
      // sub_1000B790 attribution was wrong - that function is a path helper
      // with no patch stores).
      {
        ConfigDocument fchs_cfg = all_options_off();
        set_option(fchs_cfg, "CarDynamicShadowFix", "1");
        set_option(fchs_cfg, "ForceCarHeadlightShadows", "1");
        std::vector<std::byte> img;
        std::uint32_t b = 0, s = 0;
        CHECK(map_pe_image("game_refs/GTAIV.exe", img, b, s) ||
              map_pe_image("../game_refs/GTAIV.exe", img, b, s));
        const std::vector<std::byte> pristine = img;
        SigWriteReport rep;
        CHECK(apply_sig_writes(fchs_cfg, {img.data(), img.size()}, zpatch::GameBuild::v1080,
                               &FakeProtect::protect, nullptr, rep));
        // Pattern F's EB + the sub_1000EA00 byte + E870's P1/P3 restores. E870's
        // P2 rows miss because the CarDynamic arm's EA00 write (00 at P2+6) runs
        // first and destroys the P2 pattern - the original's arm order does the
        // same, so its E870 P2 scan fails identically.
        CHECK(rep.applied == 5 && rep.missing == 2);
        for (const std::string& pat : {std::string("8D 4C 24 50 51 68 04 05 00 00"),
                                       std::string("8D 44 24 50 50 68 04 01 00 00"),
                                       std::string("F3 0F 11 44 24 1C 0F 84 9E 00 00 00")}) {
          Match m;
          CHECK(find_pattern({pristine.data(), pristine.size()}, pat, m));
          const auto off = static_cast<std::ptrdiff_t>(m.address - pristine.data());
          CHECK(std::memcmp(img.data() + off, pristine.data() + off, 6) == 0);
        }
        // The E870 restores write the same bytes back (no-ops): P1 and P3 stay
        // byte-identical; P2's immediate is the EA00 write's target (00 at +6).
        for (const std::string& pat : {std::string("8D 4C 24 50 51 68 04 05 00 00"),
                                       std::string("F3 0F 11 44 24 1C 0F 84 9E 00 00 00")}) {
          Match m;
          CHECK(find_pattern({pristine.data(), pristine.size()}, pat, m));
          const auto off = static_cast<std::ptrdiff_t>(m.address - pristine.data());
          CHECK(std::memcmp(img.data() + off, pristine.data() + off, 12) == 0);
        }
        {
          Match m;
          CHECK(find_pattern({pristine.data(), pristine.size()},
                             "8D 44 24 50 50 68 04 01 00 00", m));
          const auto off = static_cast<std::ptrdiff_t>(m.address - pristine.data());
          CHECK(std::to_integer<unsigned char>(img[off + 6]) == 0x00);  // EA00 wrote it
        }
      }
      // DualVehicleHeadlights: the handler E9s the pattern match to the
      // per-build body (sub_100064F0 on 1.0.8.0, sub_10006630 on the old builds)
      // and publishes the five game pointers the body calls through.
      {
        ConfigDocument dual_cfg = single_option_config("DualVehicleHeadlights");
        const std::string pd = "8B 17 8B 42 64 8B CF FF D0 8B 54";
        auto dual_resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
          return hook_bodies_win_resolve(va, nullptr);
        };
        // 1.0.8.0 -> the new-build body plus all five publishes.
        {
          std::vector<std::byte> img;
          std::uint32_t b = 0, s = 0;
          CHECK(map_pe_image("game_refs/GTAIV.exe", img, b, s) ||
                map_pe_image("../game_refs/GTAIV.exe", img, b, s));
          const std::vector<std::byte> pristine = img;
          Match m;
          CHECK(find_pattern({img.data(), img.size()}, pd, m));
          CHECK(static_cast<std::uintptr_t>(m.address - img.data()) == 0x4F7CE5u);
          // Publishes first (the original handler publishes before overwriting
          // the matched bytes with the E9; the live path runs slot installs
          // before sig installs for the same reason).
          SlotInstallReport sl;
          CHECK(apply_slot_installs(dual_cfg, {img.data(), img.size()}, GameBuild::v1080,
                                    &FakeProtect::protect, nullptr, sl));
          CHECK(sl.applied == 5);
          SigInstallReport si;
          AppliedWriteLog log;
          CHECK(apply_sig_installs(dual_cfg, {img.data(), img.size()},
                                   zpatch::GameBuild::v1080, kDefaultGameModuleBase,
                                   &FakeProtect::protect, &log, dual_resolver, nullptr, si));
          CHECK(si.applied == 1 && si.missing == 0 && si.skipped_no_destination == 0);
          const auto off = static_cast<std::ptrdiff_t>(m.address - img.data());
          CHECK(std::to_integer<unsigned char>(img[off]) == 0xE9);
          std::int32_t rel = 0;
          std::memcpy(&rel, img.data() + off + 1, sizeof(rel));
          const auto body = dual_resolver(0x100064F0u, nullptr);
          CHECK(body != 0);
          CHECK(rel == static_cast<std::int32_t>(
                           body - (reinterpret_cast<std::uintptr_t>(img.data()) + off + 5u)));
          const auto ia = static_cast<std::uint32_t>(
              reinterpret_cast<std::uintptr_t>(img.data()) + off);
          std::uint32_t v140 = 0, v151 = 0, v207 = 0, v331 = 0;
          std::memcpy(&v140, pristine.data() + off + 140, sizeof(v140));
          std::memcpy(&v151, pristine.data() + off + 151, sizeof(v151));
          std::memcpy(&v207, pristine.data() + off + 207, sizeof(v207));
          std::memcpy(&v331, pristine.data() + off + 331, sizeof(v331));
          CHECK(zpatch::zpatch_dual_bcc == v207);
          CHECK(zpatch::zpatch_dual_bd0 == ia + v140 + 144);
          CHECK(zpatch::zpatch_dual_bd4 == ia + v331 + 335);
          CHECK(zpatch::zpatch_dual_be0 == ia + 338);
          CHECK(zpatch::zpatch_dual_be4 == ia + v151 + 155);
        }
        // 1.0.4.0 -> the old-build body.
        {
          std::vector<std::byte> img;
          std::uint32_t b = 0, s = 0;
          CHECK(map_pe_image("game_refs/GTAIV_1040.exe", img, b, s) ||
                map_pe_image("../game_refs/GTAIV_1040.exe", img, b, s));
          const std::vector<std::byte> pristine = img;
          Match m;
          CHECK(find_pattern({img.data(), img.size()}, pd, m));
          SigInstallReport si;
          AppliedWriteLog log;
          CHECK(apply_sig_installs(dual_cfg, {img.data(), img.size()},
                                   zpatch::GameBuild::v1040, kDefaultGameModuleBase,
                                   &FakeProtect::protect, &log, dual_resolver, nullptr, si));
          CHECK(si.applied == 1);
          const auto off = static_cast<std::ptrdiff_t>(m.address - img.data());
          CHECK(std::to_integer<unsigned char>(img[off]) == 0xE9);
          std::int32_t rel = 0;
          std::memcpy(&rel, img.data() + off + 1, sizeof(rel));
          const auto body = dual_resolver(0x10006630u, nullptr);
          CHECK(body != 0);
          CHECK(rel == static_cast<std::int32_t>(
                           body - (reinterpret_cast<std::uintptr_t>(img.data()) + off + 5u)));
        }
      }
      // SavegameFix: the handler replaces ten GFWL storage calls (close/copy/
      // mutex/null) across its two signatures. Without the local stubs the
      // game's save-load path calls into the dead GFWL runtime.
      {
        std::vector<std::byte> sg_img;
        std::uint32_t gb = 0;
        std::uint32_t gs = 0;
        CHECK(map_pe_image("game_refs/GTAIV.exe", sg_img, gb, gs) ||
              map_pe_image("../game_refs/GTAIV.exe", sg_img, gb, gs));
        ConfigDocument sg_only = single_option_config("SavegameFix");
        std::vector<std::byte> sg_pristine = sg_img;
        auto sg_resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
          return hook_bodies_win_resolve(va, nullptr);
        };
        SigInstallReport sg_report;
        AppliedWriteLog sg_log;
        CHECK(apply_sig_installs(sg_only, {sg_img.data(), sg_img.size()},
                                 zpatch::GameBuild::v1080,
                                 kDefaultGameModuleBase, &FakeProtect::protect, &sg_log,
                                 sg_resolver, nullptr, sg_report));
        CHECK(sg_report.applied == 10);
        CHECK(sg_log.size() == 10);
        // Every write lands on one of the handler's ten call sites and carries
        // an E8 (the x64 test cannot validate the rel32 itself: the mapped
        // buffer and the stub bodies sit more than 4 GiB apart, which the live
        // 32-bit build never does).
        Match sg_a;
        Match sg_b;
        CHECK(find_pattern({sg_pristine.data(), sg_pristine.size()},
                           "75 ? 83 C1 01 83 C0 01 83 F9 04", sg_a));
        CHECK(find_pattern({sg_pristine.data(), sg_pristine.size()},
                           "8D 4C 24 18 51 6A 00 6A 0C 57", sg_b));
        std::vector<std::ptrdiff_t> expected;
        {
          const auto a_off = static_cast<std::ptrdiff_t>(sg_a.address - sg_pristine.data());
          const auto b_off = static_cast<std::ptrdiff_t>(sg_b.address - sg_pristine.data());
          for (const std::ptrdiff_t off : {-73, -143, -176, -280, -240}) {
            expected.push_back(a_off + off);
          }
          for (const std::ptrdiff_t off : {-86, -47, 10, 90, 128}) {
            expected.push_back(b_off + off);
          }
        }
        std::size_t matched = 0;
        for (const auto& entry : sg_log) {
          CHECK(std::to_integer<unsigned char>(entry.address[0]) == 0xE8);
          const auto off = static_cast<std::ptrdiff_t>(entry.address - sg_img.data());
          for (const std::ptrdiff_t site : expected) {
            if (off == site) ++matched;
          }
        }
        CHECK(matched == 10);
        CHECK(sg_resolver(0x100455A0u, nullptr) != 0);  // the copy stub exists
      }
      // SkipMenu's resume slot must capture the ORIGINAL call target: the slot
      // publish has to run before the site install overwrites the E8
      // displacement, or the body tail-jumps to itself (the live CPU-spinning
      // black screen).
      {
        std::vector<std::byte> menu_img;
        std::uint32_t gb = 0;
        std::uint32_t gs = 0;
        CHECK(map_pe_image("game_refs/GTAIV.exe", menu_img, gb, gs) ||
              map_pe_image("../game_refs/GTAIV.exe", menu_img, gb, gs));
        const std::string menu_sig =
            "83 F8 03 75 ? A1 ? ? ? ? 80 88 ? ? ? ? ? 84 DB 74 0A 6A 00 E8 ? ? ? ? 83 C4 04 5F 5E";
        Match menu_match;
        CHECK(find_pattern({menu_img.data(), menu_img.size()}, menu_sig, menu_match));
        std::int32_t original_rel = 0;
        std::memcpy(&original_rel, menu_match.address + 24, sizeof(original_rel));
        const auto original_target =
            reinterpret_cast<std::uintptr_t>(menu_match.address) + 28u +
            static_cast<std::uintptr_t>(static_cast<std::int32_t>(original_rel));
        ConfigDocument menu_cfg = single_option_config("SkipMenu");
        auto menu_resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
          return hook_bodies_win_resolve(va, nullptr);
        };
        AppliedWriteLog menu_log;
        SlotInstallReport menu_slots;
        CHECK(apply_slot_installs(menu_cfg, {menu_img.data(), menu_img.size()},
                                  GameBuild::v1080, &FakeProtect::protect, &menu_log,
                                  menu_slots));
        CHECK(menu_slots.applied == 1);
        SigInstallReport menu_sigs;
        CHECK(apply_sig_installs(menu_cfg, {menu_img.data(), menu_img.size()},
                                 zpatch::GameBuild::v1080,
                                 kDefaultGameModuleBase, &FakeProtect::protect, &menu_log,
                                 menu_resolver, nullptr, menu_sigs));
        CHECK(menu_sigs.applied == 1);
        CHECK(zpatch_skip_menu_resume ==
              static_cast<std::uint32_t>(original_target));
        CHECK(zpatch_skip_menu_resume !=
              static_cast<std::uint32_t>(menu_resolver(0x10046610u, nullptr)));
      }
      // MiscFixes is a single bundled option in the original: every recovered
      // XLivelessAddon row gates on the MiscFixes key itself (a per-fix split
      // was a reimplementation and is archived in docs/parity_and_improvements.md).
      {
        std::size_t misc_rows = 0;
        std::size_t all_count = 0;
        const auto* all_ops = recovered_patch_ops(all_count);
        for (std::size_t i = 0; i < all_count; ++i) {
          if (all_ops[i].handler == 0x10023dd0u) {
            ++misc_rows;
            CHECK(all_ops[i].option == "MiscFixes");
          }
        }
          CHECK(misc_rows == 12);
      }
      RuntimePatchReport report;
      AppliedWriteLog log;
      CHECK(apply_recovered_patches_logged(GameBuild::v1080, image, &FakeProtect::protect,
                                           report, log, kDefaultGameModuleBase, resolver,
                                           nullptr));
      CHECK(!log.empty());
      {
        // The corrected InfiniteCarBottomDistance write must land in the full
        // pipeline too (the op table rows for this site are suppressed).
        Match icbd_match;
        CHECK(find_pattern({pristine.data(), pristine.size()},
                           "0F 2F D0 76 25 0F 28 C2", icbd_match));
        const auto icbd_off = static_cast<std::size_t>(icbd_match.address - pristine.data());
        CHECK(std::to_integer<unsigned char>(live_base[icbd_off + 3]) == 0xEB);
        CHECK(std::to_integer<unsigned char>(live_base[icbd_off + 4]) == 0x2A);
      }
      const auto module_lo = 0x10000000u;
      const auto module_hi = 0x1016FFFFu;
      std::size_t dangling_values = 0;
      std::size_t outside_text = 0;
      for (const auto& entry : log) {
        const auto offset =
            static_cast<std::size_t>(entry.address - image.base);
        CHECK(offset < image.size);
        // .text on 1.0.8.0 is RVA 0x1000..0x96B000; indirect writes target .data.
        if (!(offset >= 0x1000 && offset < 0x96B000)) ++outside_text;
        if (entry.length == 4) {
          std::uint32_t written = 0;
          std::memcpy(&written, entry.address, 4);
          if (written >= module_lo && written <= module_hi) ++dangling_values;
        }
      }
      // The only writes outside .text are the two indirect .data stores.
      CHECK(outside_text == report.applied - log.size() + 2 ||
            outside_text <= 2);
      CHECK(dangling_values == 0);
      // Every enabled hook destination now resolves to a live body (the last
      // gap, NewMemorySystem's 0x10038670, landed with the CRageAllocator
      // port); an unresolved destination is still reported and skipped, never
      // written (covered by the synthetic missing-destination tests). The one
      // tolerated skip is CrashMsgFix's deref site: its target is a pointer
      // stored in the image (a game VA below 4 GiB), which necessarily lies
      // outside this x64 test's >4 GiB mapping.
      CHECK(report.no_destination <= 1);
      CHECK(report.jump_opcode_pending >= 1); // bare opcode rows never written
      // Rows recovered from another build's handler branch must not be written
      // blindly at their RVA (the site means different code in this build).
      CHECK(report.build_mismatch >= 1);
      CHECK(!rva_build_allows(0x10044710u, 14638u, 9));   // build-7 row
      CHECK(!rva_build_allows(0x100234e0u, 0x4a3ce9u, 9));
      CHECK(rva_build_allows(0x100234e0u, 4785166u, 9));  // build-9 row
      CHECK(!rva_build_allows(0x100234e0u, 4785166u, 8)); // not for build 8

      // The recovered per-build full installs (LoadDLCs, RemoveRGSCLoginRequirement,
      // RemoveUselessChecks, Patch6and7RemoveModChecks, SuperLODFix slots) applied
      // through the same path; each row only ever applies on its own build.
      {
        std::size_t install_count = 0;
        (void)recovered_rva_installs(install_count);
        CHECK(install_count == 51);
        CHECK(zpatch::dlc_register_rva_global() == 4781120u);
        CHECK(zpatch::dlc_apply_rva_global() == 4783616u);
        CHECK(zpatch::zpatch_dlc_load_callback ==
              static_cast<std::uint32_t>(kDefaultGameModuleBase + 4785248u));
        CHECK(rva_install_covers(4785215u, 9));
        CHECK(!rva_install_covers(4785215u, 8));
        CHECK(rva_install_covers(4276959u, 8));   // LoadDLCs build-8 fill site
        CHECK(!rva_install_covers(4276959u, 9));
        CHECK(rva_install_covers(14722u, 7));     // Patch6and7RemoveModChecks build 7
        CHECK(!rva_install_covers(14722u, 8));
        CHECK(rva_install_covers(14492u, 8));     // Patch6and7RemoveModChecks build 8
        CHECK(rva_install_covers(4639975u, 5));   // RemoveUselessChecks build-5 fill
        CHECK(!rva_install_covers(4639975u, 8));
        // LoadDLCs: 4 jumps apply (one module destination has no ABI body yet),
        // 1 fill, 1 slot; SuperLODFix's two slot publishes are not rebased yet.
        auto* site = live_base + 4785215u;
        CHECK(std::to_integer<unsigned char>(site[0]) == 0x90);
        CHECK(std::to_integer<unsigned char>(site[11]) == 0x90);
        auto* jump_site = live_base + 4781400u;
        CHECK(std::to_integer<unsigned char>(jump_site[0]) == 0xE8);
        std::int32_t rel = 0;
        std::memcpy(&rel, jump_site + 1, sizeof(rel));
        const auto expected = static_cast<std::int32_t>(
            hook_bodies_win_resolve(0x10023480u, nullptr) -
            (kDefaultGameModuleBase + 4781400u + 5u));
        CHECK(rel == expected);
        auto* game_jump = live_base + 4785166u;
        CHECK(std::to_integer<unsigned char>(game_jump[0]) == 0xE9);
        std::memcpy(&rel, game_jump + 1, sizeof(rel));
        CHECK(rel == static_cast<std::int32_t>(
                          (kDefaultGameModuleBase + 4785196u) -
                          (kDefaultGameModuleBase + 4785166u + 5u)));
      }

      // Curated corrections: the mis-extracted ImprovedShaderStreaming rows
      // must be suppressed and the handler's real E9 writes installed - the
      // 0x8b22fc operand row was the savefile crash.
      {
        const std::string ish_sig =
            "33 F6 8B 4C 24 08 8B 06 8B 50 04 51 8B CE FF D2 F6 D8 1B C0 23 C6 5E C3 "
            "8B 44 24 0C 8B 74 24";
        CHECK(op_row_suppressed(0x10045ad0u, ish_sig, 267, "8E"));
        CHECK(op_row_suppressed(0x10045ad0u, ish_sig, 267, "77 03"));
        CHECK(op_row_suppressed(0x10045ad0u, ish_sig, 253, "E8"));
        CHECK(op_row_suppressed(0x10022d80u, "0F 2F D0 76 25 0F 28 C2", 4, "EB"));
        CHECK(op_row_suppressed(0x10038680u, "3D 80 99 99 59 73 05", -29, "E8"));
        CHECK(op_row_suppressed(0x100467a0u,
                                "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C", -38, "E9"));
        CHECK(op_row_suppressed(0x10022d80u, "0F 2F D0 76 25 0F 28 C2", 4, "2A"));
        std::size_t suppression_count = 0;
        op_suppressions(suppression_count);
        CHECK(suppression_count == 11);  // + BuildingAlphaFix p2 bare-E9 row
        std::size_t install_count2 = 0;
        sig_installs(install_count2);
        CHECK(install_count2 == 118);  // + MouseFix (17) + OOC (2) + HQR (1) + BuildingAlphaFix p2 (1) + CutsceneFixes chain B (2) + DualVehicleHeadlights old/new (2) + shared bypass RemoveGFWL (1)
        std::size_t slot_count = 0;
        slot_installs(slot_count);
        CHECK(slot_count == 132);      // skip hooks + shim publishes + new features (MouseFix 6, OOC 3, HQR 2, CutsceneFixes 2, NMS 1, DualVehicleHeadlights 5, NoLiveryLimit fallback 2, shared bypass RemoveGFWL 1)
        std::size_t sig_write_count = 0;
        sig_writes(sig_write_count);
        CHECK(sig_write_count == 22);  // ICBD + SavegameFix CRC NOPs + 2 BorderlessWindowed + FastLoading + 2 DoNotPause + CarDynamic/EA00/CarParts + 5 E870 restores + CutsceneFixes chain A
        {
          // Fresh image: the main apply above already patched the shared buffer.
          std::vector<std::byte> fresh;
          std::uint32_t fresh_base = 0;
          std::uint32_t fresh_size = 0;
          CHECK(map_pe_image("game_refs/GTAIV.exe", fresh, fresh_base, fresh_size) ||
                map_pe_image("../game_refs/GTAIV.exe", fresh, fresh_base, fresh_size));
          const std::string icbd_sig = "0F 2F D0 76 25 0F 28 C2";
          Match icbd_match;
          CHECK(find_pattern({fresh.data(), fresh.size()}, icbd_sig, icbd_match));
          SigWriteReport write_report;
          ConfigDocument enabled_config = single_option_config("InfiniteCarBottomDistance");
          CHECK(apply_sig_writes(enabled_config, {fresh.data(), fresh.size()},
                                 zpatch::GameBuild::v1080,
                                 &FakeProtect::protect, nullptr, write_report));
          CHECK(write_report.applied == 1);
          auto* site = const_cast<std::byte*>(icbd_match.address);
          // The handler turns jbe +0x25 (76 25) into jmp +0x2A (EB 2A).
          CHECK(std::to_integer<unsigned char>(site[3]) == 0xEB);
          CHECK(std::to_integer<unsigned char>(site[4]) == 0x2A);
        }

        // Against the real image: ImprovedShaderStreaming's set now installs.
        // The two E8 calls (match+85 / match+253) resolve to the ABI bodies, and
        // the two match-relative E9 jumps land at match+245 / match+1158.
        Match ish_match;
        CHECK(find_pattern({live_base, game.size()}, ish_sig, ish_match));
        auto* crash_site = const_cast<std::byte*>(ish_match.address) + 266;
        CHECK(std::to_integer<unsigned char>(crash_site[0]) == 0xE9);
        {
          std::int32_t rel = 0;
          std::memcpy(&rel, crash_site + 1, sizeof(rel));
          CHECK(rel == 887);
        }
        auto* early_site = const_cast<std::byte*>(ish_match.address) + 98;
        CHECK(std::to_integer<unsigned char>(early_site[0]) == 0xE9);
        {
          std::int32_t rel = 0;
          std::memcpy(&rel, early_site + 1, sizeof(rel));
          CHECK(rel == 142);
        }
        // NewMemorySystem's set installs now that its companion destination
        // (0x10038670, the vtable-installing body) has a live body: E8 at
        // match-34 -> the body, E9 at match-29 -> match-16 (displacement 8).
        const std::string nms_sig = "3D 80 99 99 59 73 05";
        Match nms_match;
        CHECK(find_pattern({live_base, game.size()}, nms_sig, nms_match));
        auto* nms_e8 = const_cast<std::byte*>(nms_match.address) - 34;
        auto* nms_e9 = const_cast<std::byte*>(nms_match.address) - 29;
        CHECK(std::to_integer<unsigned char>(nms_e8[0]) == 0xE8);
        CHECK(std::to_integer<unsigned char>(nms_e9[0]) == 0xE9);
        {
          std::int32_t rel = 0;
          std::memcpy(&rel, nms_e8 + 1, sizeof(rel));
          CHECK(rel == static_cast<std::int32_t>(
                           hook_bodies_win_resolve(0x10038670u, nullptr) -
                           (reinterpret_cast<std::uintptr_t>(nms_e8) + 5u)));
          std::memcpy(&rel, nms_e9 + 1, sizeof(rel));
          CHECK(rel == 8);
        }
        // SkipIntro and SkipMenu sets are complete and applied.
        Match intro_match;
        CHECK(find_pattern({live_base, game.size()},
                           "89 91 ? ? ? ? 8D 44 24 ? 68 ? ? ? ? 50", intro_match));
        auto* intro_site = const_cast<std::byte*>(intro_match.address) - 6;
        CHECK(std::to_integer<unsigned char>(intro_site[0]) == 0xE9);
        std::int32_t intro_rel = 0;
        std::memcpy(&intro_rel, intro_site + 1, sizeof(intro_rel));
        CHECK(intro_rel == static_cast<std::int32_t>(
                               hook_bodies_win_resolve(0x10045C30u, nullptr) -
                               (reinterpret_cast<std::uintptr_t>(intro_site) + 5u)));
        CHECK(zpatch::zpatch_skip_intro_match ==
              static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(intro_match.address)));
      }
      // Finding N1 (phase-3 audit): the anchored handlers gate their entire
      // write set on their first scan. FreezeCarFix / SMPA60Fix /
      // NewSavesCompatibility over-applied their sibling rows on the builds
      // where the guard signature is absent (1000/1004/1040). The anchor table
      // must hold those three options and the rows must gate exactly like the
      // original's `je epilogue`.
      {
        std::size_t anchor_count = 0;
        const auto* anchors = op_anchors(anchor_count);
        CHECK(anchor_count == 3);
        CHECK(std::string(anchors[0].option) == "FreezeCarFix");
        CHECK(std::string(anchors[0].signature) ==
              "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0");
        CHECK(std::string(anchors[1].option) == "SMPA60Fix");
        CHECK(std::string(anchors[1].signature) ==
              "01 96 98 00 00 00 8B 47 04 29 46 54 8B 4F 0C");
        CHECK(std::string(anchors[2].option) == "NewSavesCompatibility");
        CHECK(std::string(anchors[2].signature) ==
              "68 ? ? ? ? E8 ? ? ? ? 8B 2D ? ? ? ? 83 C4 08");

        // Synthetic: the sibling signature without the guard applies nothing.
        const std::string fcf_siblings = "E9 A1 02 00 00 83 FF 09 0F 87 F2 00 00 00";
        const std::string fcf_guard = "32 DB 8B 54 24 24 52 E8 ? ? ? ? 8B F0";
        const auto plant_at = [](std::vector<std::byte>& buffer, std::size_t offset,
                                 std::string_view signature) {
          std::size_t index = offset;
          std::istringstream stream{std::string(signature)};
          std::string token;
          while (stream >> token) {
            if (token != "?" && token != "??") {
              unsigned value = 0;
              for (char ch : token) {
                const auto lower = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lower >= '0' && lower <= '9') value = value * 16 + static_cast<unsigned>(lower - '0');
                else if (lower >= 'a' && lower <= 'f') value = value * 16 + static_cast<unsigned>(lower - 'a' + 10);
                else break;
              }
              buffer[index] = static_cast<std::byte>(value & 0xFFu);
            }
            ++index;
          }
        };
        auto fixed_resolver = +[](std::uint32_t, void*) -> std::uintptr_t {
          return 0x7F000000u;
        };
        {
          std::vector<std::byte> img(0x4000, std::byte{0x90});
          plant_at(img, 0x100, fcf_siblings);
          ConfigDocument fcf = single_option_config("FreezeCarFix");
          SigInstallReport rep;
          AppliedWriteLog wlog;
          CHECK(apply_sig_installs(fcf, {img.data(), img.size()}, GameBuild::v1080,
                                   kDefaultGameModuleBase, &FakeProtect::protect, &wlog,
                                   fixed_resolver, nullptr, rep));
          CHECK(rep.applied == 0);
          CHECK(wlog.empty());
          CHECK(rep.anchor_missing == 17);  // every FreezeCarFix sig row
          CHECK(std::to_integer<unsigned char>(img[0x100]) == 0xE9);
          // With the guard present the same rows apply (the guard row itself, the
          // ten E9-pattern siblings and the prefix row).
          plant_at(img, 0x1000, fcf_guard);
          SigInstallReport rep2;
          AppliedWriteLog wlog2;
          CHECK(apply_sig_installs(fcf, {img.data(), img.size()}, GameBuild::v1080,
                                   kDefaultGameModuleBase, &FakeProtect::protect, &wlog2,
                                   fixed_resolver, nullptr, rep2));
          CHECK(rep2.applied == 12);
          CHECK(rep2.anchor_missing == 0);
          CHECK(rep2.missing == 5);  // the five unplaced sibling signatures
        }
        auto live_resolver = +[](std::uint32_t va, void*) -> std::uintptr_t {
          return hook_bodies_win_resolve(va, nullptr);
        };
        // Real build 1000: FreezeCarFix and SMPA60Fix write nothing (both
        // guards absent), NewSavesCompatibility still applies (guard present).
        {
          std::vector<std::byte> b1000;
          std::uint32_t b = 0, s = 0;
          CHECK(map_pe_image("game_refs/GTAIV_1000.exe", b1000, b, s) ||
                map_pe_image("../game_refs/GTAIV_1000.exe", b1000, b, s));
          ConfigDocument n1cfg = all_options_off();
          set_option(n1cfg, "FreezeCarFix", "1");
          set_option(n1cfg, "SMPA60Fix", "1");
          set_option(n1cfg, "NewSavesCompatibility", "1");
          RuntimePatchReport rep;
          AppliedWriteLog wlog;
          CHECK(apply_recovered_patches_logged(GameBuild::v1000, {b1000.data(), b1000.size()},
                                               &FakeProtect::protect, rep, wlog,
                                               kDefaultGameModuleBase, live_resolver, nullptr));
          bool nsc_written = false;
          for (const auto& w : wlog) {
            CHECK(w.option != "FreezeCarFix");
            CHECK(w.option != "SMPA60Fix");
            if (w.option == "NewSavesCompatibility") nsc_written = true;
          }
          CHECK(nsc_written);
          CHECK(rep.anchor_missing >= 30);
        }
        // Real build 1004: FreezeCarFix and NewSavesCompatibility write nothing,
        // SMPA60Fix still applies (its guard is present there).
        {
          std::vector<std::byte> b1004;
          std::uint32_t b = 0, s = 0;
          CHECK(map_pe_image("game_refs/GTAIV_1004.exe", b1004, b, s) ||
                map_pe_image("../game_refs/GTAIV_1004.exe", b1004, b, s));
          ConfigDocument n1cfg = all_options_off();
          set_option(n1cfg, "FreezeCarFix", "1");
          set_option(n1cfg, "SMPA60Fix", "1");
          set_option(n1cfg, "NewSavesCompatibility", "1");
          RuntimePatchReport rep;
          AppliedWriteLog wlog;
          CHECK(apply_recovered_patches_logged(GameBuild::v1004, {b1004.data(), b1004.size()},
                                               &FakeProtect::protect, rep, wlog,
                                               kDefaultGameModuleBase, live_resolver, nullptr));
          bool smpa_written = false;
          for (const auto& w : wlog) {
            CHECK(w.option != "FreezeCarFix");
            CHECK(w.option != "NewSavesCompatibility");
            if (w.option == "SMPA60Fix") smpa_written = true;
          }
          CHECK(smpa_written);
        }
      }
      // Finding N5: RemoveUselessChecks' table-driven 5-byte NOPs on
      // 1040/1070/1080 (the phase-3 op extractor only recovered the non-loop
      // writes; the fcells sweep flagged ~2,200-2,500 missing sites per build).
      {
        std::size_t site_count = 0;
        const auto* sites = recovered_useless_checks(site_count);
        CHECK(site_count == 7489);
        std::size_t per_build[14] = {};
        for (std::size_t i = 0; i < site_count; ++i)
          if (sites[i].build < 14) ++per_build[sites[i].build];
        CHECK(per_build[5] == 2466);   // 1040
        CHECK(per_build[8] == 2288);   // 1070
        CHECK(per_build[9] == 2735);   // 1080 (2230 five-byte sites + byte patches)
        CHECK(per_build[1] == 0);      // 1000: no table-driven sites
        std::vector<std::byte> b1080;
        std::uint32_t b = 0, s = 0;
        CHECK(map_pe_image("game_refs/GTAIV.exe", b1080, b, s) ||
              map_pe_image("../game_refs/GTAIV.exe", b1080, b, s));
        ConfigDocument n5cfg;
        set_option(n5cfg, "RemoveUselessChecks", "1");
        UselessCheckReport n5report;
        AppliedWriteLog n5log;
        CHECK(apply_recovered_useless_checks(n5cfg, {b1080.data(), b1080.size()},
                                              kDefaultGameModuleBase, &FakeProtect::protect,
                                              &n5log, static_cast<std::uint8_t>(GameBuild::v1080),
                                              n5report));
        std::printf("DIAG n5 enabled=%d applied=%zu gated=%zu nobuild=%zu oob=%zu\n",
                    option_enabled(n5cfg, "RemoveUselessChecks") ? 1 : 0,
                    n5report.applied, n5report.gated_out, n5report.skipped_no_build,
                    n5report.out_of_image);
        CHECK(n5report.applied == 2735);
        CHECK(n5log.size() == 2735);
        // Disabled: the arm's default string is "0", so nothing applies.
        ConfigDocument off;  // key absent -> arm default "0" -> disabled
        UselessCheckReport off_report;
        AppliedWriteLog off_log;
        CHECK(apply_recovered_useless_checks(off, {b1080.data(), b1080.size()},
                                              kDefaultGameModuleBase, &FakeProtect::protect,
                                              &off_log, static_cast<std::uint8_t>(GameBuild::v1080),
                                              off_report));
        CHECK(off_report.applied == 0);
        CHECK(off_report.gated_out == 1);
        CHECK(off_log.empty());
      }
#if defined(_WIN32)
      // Fault-inclusive de-guard proofs (2026-10-16): each forced condition
      // must fault inside the de-guarded body where the original faults. The
      // child process runs the probe; a re-added fault-avoidance guard would
      // make it exit cleanly (status 1) instead of dying with an AV.
      {
        constexpr int kAccessViolation = static_cast<int>(0xC0000005u);
        const char* const fault_probes[] = {
            "fchs-null-lookup", "fchs-null-object", "garage-null-lookup",
            "gfwl-zero-base", "focus-loss-null-site",
            "newfiles-null-register", "ooc-null-logger",
            "vehicle-component-null-registry",
        };
        for (const char* probe : fault_probes) {
          bool launched = false;
          const int status = run_fault_probe_child(probe, launched);
          std::printf("DIAG fault-probe %-34s launched=%d exit=%d\n", probe,
                      launched ? 1 : 0, status);
          CHECK(launched);  // a child blocked by AV is not a passing probe
          CHECK(status == kAccessViolation);
        }
      }
#endif
      // Logging infrastructure (docs/LOGGING.md): the stable line shape, the
      // [Debug] LogLevel filter, rate limiting and the flush. The file is
      // parsed back so the documented key=value shape stays honest.
      {
        const char* path = "zpatch_logging_test.log";
        zpatch::log_set_level(2);
        zpatch::log_open(path);
        zpatch::log_printf("phase=test key=value n=%d", 7);
        zpatch::log_verbose("phase=test verbose=1");  // level 2: suppressed
        zpatch::log_summary("phase=test-summary ok=1");
        std::uint32_t rate_counter = 0;
        for (int i = 0; i < 2500; ++i)
          zpatch::log_rate(&rate_counter, 2, 1000, "phase=test-rate i=%d", i);
        zpatch::log_flush();
        zpatch::log_set_level(3);
        zpatch::log_verbose("phase=test verbose=2");
        zpatch::log_set_level(0);
        zpatch::log_printf("phase=test off=1");  // level 0: suppressed
        zpatch::log_flush();
        zpatch::log_set_level(2);
        zpatch::log_close();
        std::ifstream log_in(path);
        std::size_t total_lines = 0;
        std::size_t rate_lines = 0;
        std::size_t verbose_lines = 0;
        bool saw_off = false;
        bool saw_header = false;
        std::string line;
        while (std::getline(log_in, line)) {
          ++total_lines;
          if (line.find("phase=test-rate") != std::string::npos) ++rate_lines;
          if (line.find("verbose=") != std::string::npos) ++verbose_lines;
          if (line.find("off=1") != std::string::npos) saw_off = true;
          if (line.find("[seq=") != std::string::npos &&
              line.find(" tid=") != std::string::npos &&
              line.find(" I] ") != std::string::npos)
            saw_header = true;
        }
        CHECK(saw_header);
        CHECK(verbose_lines == 1);       // the level-3 line only
        CHECK(rate_lines == 4);          // hits 1, 2, 1002, 2002
        CHECK(!saw_off);                 // level 0 suppresses everything
        CHECK(total_lines == 1 + 1 + 4 + 1);
      }
      shutdown();
      std::remove(live_ini);
    }
  }
#endif
  return 0;
}
