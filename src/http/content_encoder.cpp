#include "content_encoder.h"
#include <array>
#include <string>
#include "coders/deflate.h"

namespace unetwork::http {

using EncodeFunc =
    std::optional<std::vector<std::byte>> (*)(const std::span<const std::byte>&) noexcept;

static std::pair<const char*, EncodeFunc> supported_coders[] = {
    {"deflate", unetwork::http::deflate}};

EncodeResult encode(const std::span<const std::byte>& in,
                    const std::string& accept_encoding) noexcept {
  for (auto [coderName, coderFunc] : supported_coders) {
    if (accept_encoding.find(coderName) != std::string::npos)
      if (auto coderResult = coderFunc(in); coderResult.has_value())
        return {coderName, coderResult.value()};
  }
  return {};
}

}  // namespace unetwork::http
