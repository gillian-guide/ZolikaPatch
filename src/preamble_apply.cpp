#include "zpatch/preamble_apply.hpp"

namespace zpatch {
namespace {
// Fallback hash salts written by the loader when the build probe misses.
constexpr SaltSubstitution kFallbackSalts[] = {
  {0x10158054u, 0x6b86410eu}, {0x101580c8u, 0x406b4b20u},
  {0x10158050u, 0x6dcd115au}, {0x101580b8u, 0x62bfdb37u},
  {0x10158028u, 0x931f73a2u}, {0x101580a8u, 0x7f6a60d3u},
  {0x10158040u, 0xc9dbda90u}, {0x1015803cu, 0xa4ea0691u},
  {0x101580d4u, 0x09399528u}, {0x1015805cu, 0x1cc648eau},
};
}
std::size_t preamble_salt_substitutions(bool probe_matched,
                                        const SaltSubstitution*& substitutions) noexcept {
  if (probe_matched) { substitutions = nullptr; return 0; }
  substitutions = kFallbackSalts;
  return sizeof(kFallbackSalts) / sizeof(kFallbackSalts[0]);
}
}  // namespace zpatch
