#pragma once
#include <span>
#include <userver/yaml_config/yaml_config.hpp>
#include <unetwork/http_server.h>
#include <unetwork/io_wrapper.h>

namespace unetwork {

inline void SendExactly(IoBase* io, std::span<const std::byte> data,
                        userver::engine::Deadline deadline) {
  if (io->SendAll(data.data(), data.size(), deadline) != data.size())
    throw(userver::engine::io::IoException() << "Socket closed during transfer ");
}

inline void RecvExactly(IoBase* io, std::span<std::byte> buffer,
                        userver::engine::Deadline deadline) {
  if (io->ReadAll(buffer.data(), buffer.size(), deadline) != buffer.size())
    throw(userver::engine::io::IoException() << "Socket closed during transfer ");
}

inline bool TestHeaderVal(const http::Headers& headers, std::string_view key,
                          std::string_view val) {
  auto it = headers.find(key);
  if (it == headers.end()) return false;
  return it->second == val;
}

inline const std::string& GetOptKey(const http::Headers& headers, std::string_view key,
                                    const std::string& def) {
  auto it = headers.find(key);
  if (it != headers.end()) return it->second;
  return def;
}

};  // namespace unetwork
