#include "zpatch/model_catalog.hpp"
#include "zpatch/game.hpp"
namespace zpatch {
namespace {
constexpr const char* names[] = {
#include "model_catalog.inc"
};
static_assert(sizeof(names)/sizeof(names[0]) == 0x9637);
}
std::size_t model_catalog_size() noexcept { return sizeof(names)/sizeof(names[0]); }
std::string_view model_catalog_name(std::size_t index) noexcept {
  return index<model_catalog_size() ? names[index] : "**UNKNOWN**";
}
std::string_view model_name_from_hash(std::uint32_t hash) noexcept {
  for (const auto* name : names) if (game_hash(name)==hash) return name;
  return "**UNKNOWN**";
}
}
