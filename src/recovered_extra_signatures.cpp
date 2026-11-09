#include "zpatch/recovered_extra_signatures.hpp"
#include "zpatch/pattern.hpp"
namespace zpatch {
namespace {
constexpr ExtraSignature kExtra[] = {
#include "recovered_extra_signatures.inc"
};
}
const ExtraSignature* recovered_extra_signatures(std::size_t& count) noexcept {
  count = sizeof(kExtra) / sizeof(kExtra[0]);
  return kExtra;
}

std::size_t scan_extra_signatures(MappedImage image,
                                 std::vector<ExtraSignatureHit>& output) noexcept {
  output.clear();
  std::size_t found = 0;
  if (!image.base || image.size == 0) return found;
  for (const auto& entry : kExtra) {
    ExtraSignatureHit hit{entry.option, entry.signature, false, nullptr};
    Match match;
    if (find_pattern({image.base, image.size}, entry.signature, match)) {
      hit.found = true;
      hit.address = const_cast<std::byte*>(match.address);
      ++found;
    }
    output.push_back(hit);
  }
  return found;
}
}  // namespace zpatch
