#pragma once
#include <optional>
#include <span>
#include <vector>

namespace unetwork::http {

std::optional<std::vector<std::byte>> deflate(const std::span<const std::byte>& in) noexcept;
}
