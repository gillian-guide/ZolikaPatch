#pragma once

#include <string_view>

namespace zpatch {

enum class StatusCode {
  ok,
  not_reconstructed,
  invalid_input,
  io_error,
  parse_error,
  not_found,
  unsupported_platform,
  evidence_mismatch,
};

struct Status final {
  StatusCode code{StatusCode::ok};
  std::string_view detail{};
  constexpr explicit operator bool() const noexcept { return code == StatusCode::ok; }
};

}  // namespace zpatch
