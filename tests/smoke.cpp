#include "zpatch/config.hpp"
#include "zpatch/patch_catalog.hpp"
#include "zpatch/pattern.hpp"
#include "zpatch/runtime.hpp"
#include <array>
#include <cassert>
int main() {
  const auto catalog = zpatch::observed_feature_catalog();
  assert(catalog.data && catalog.size == 98);
  zpatch::ConfigDocument document;
  assert(zpatch::parse_config("[Options]\nSkipIntro=1\n", document));
  assert(document.entries.size() == 1 && document.entries[0].key == "SkipIntro");
  std::array<std::byte, 3> bytes{std::byte{0x10}, std::byte{0x20}, std::byte{0x30}};
  zpatch::Match match{};
  assert(zpatch::find_pattern({bytes.data(), bytes.size()}, "10 ? 30", match));
  assert(match.address == bytes.data());
  assert(zpatch::initialize("/a/path/that/does/not/exist").code == zpatch::StatusCode::io_error);
  zpatch::shutdown();
  assert(!zpatch::runtime_state().initialized);
}
