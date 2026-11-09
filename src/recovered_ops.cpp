#include "zpatch/recovered_ops.hpp"
namespace zpatch {
namespace {
constexpr RecoveredPatchOp kOps[] = {
#include "recovered_patch_ops.inc"
};

struct RvaBuildGate final {
  std::uint32_t handler;
  std::uint32_t rva;
  std::uint8_t kind;   // 0=eq 1=ge 2=gt 3=le 4=lt 5=any
  std::uint8_t value;
};

#include "recovered_rva_builds.inc"
}  // namespace
const RecoveredPatchOp* recovered_patch_ops(std::size_t& count) noexcept {
  count = sizeof(kOps) / sizeof(kOps[0]);
  return kOps;
}

bool rva_build_allows(std::uint32_t handler, std::uint32_t rva,
                      std::uint8_t build) noexcept {
  for (const RvaBuildGate& gate : kRvaBuildGates) {
    if (gate.handler != handler || gate.rva != rva) continue;
    switch (gate.kind) {
      case 0: return build == gate.value;
      case 1: return build >= gate.value;
      case 2: return build > gate.value;
      case 3: return build <= gate.value;
      case 4: return build < gate.value;
      default: return true;
    }
  }
  return false;  // no attribution: never blind-write
}
}  // namespace zpatch
