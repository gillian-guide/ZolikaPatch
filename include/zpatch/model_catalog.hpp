#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
namespace zpatch {
std::size_t model_catalog_size() noexcept;
std::string_view model_catalog_name(std::size_t index) noexcept;
// FUN_10003f00: first match in original table order; "**UNKNOWN**" on miss.
std::string_view model_name_from_hash(std::uint32_t hash) noexcept;
}
