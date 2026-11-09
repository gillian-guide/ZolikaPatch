#pragma once
#include <cstddef>
#include <cstdint>
#include "zpatch/recovered_preamble.hpp"

namespace zpatch {

struct SaltSubstitution final {
  std::uint32_t global;
  std::uint32_t value;
};

// Models the recovered preamble behaviour for the build-probe entry:
// if the probe matched, the game's own salts stand and NO substitution is
// applied. If the probe missed, the recovered fallback salts are substituted.
// `probe_matched` must be supplied by the caller from an actual scan of the
// loaded game image (the reconstruction does not read game memory itself).
std::size_t preamble_salt_substitutions(bool probe_matched,
                                        const SaltSubstitution*& substitutions) noexcept;

}  // namespace zpatch
