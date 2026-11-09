#include "zpatch/recovered_preamble.hpp"
namespace zpatch {
namespace {
constexpr RecoveredPreambleEntry kEntries[] = {
#include "recovered_preamble.inc"
};
}
const RecoveredPreambleEntry* recovered_preamble_entries(std::size_t& count) noexcept {
  count = sizeof(kEntries) / sizeof(kEntries[0]);
  return kEntries;
}
}  // namespace zpatch
