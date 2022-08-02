#pragma once
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace unetwork::http {

struct EncodeResult {
  std::string_view encoding;
  std::vector<std::byte> encoded_data;
};

EncodeResult encode(const std::span<const std::byte>& in,
                    const std::string& accept_encoding) noexcept;
}  // namespace unetwork::http
