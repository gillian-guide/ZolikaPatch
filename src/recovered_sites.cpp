#include "zpatch/recovered_sites.hpp"
namespace zpatch {
namespace {
constexpr RecoveredPatchSite kSites[] = {
#include "recovered_patch_sites.inc"
};
}
const RecoveredPatchSite* recovered_patch_sites(std::size_t& count) noexcept {
  count = sizeof(kSites) / sizeof(kSites[0]);
  return kSites;
}
}  // namespace zpatch
