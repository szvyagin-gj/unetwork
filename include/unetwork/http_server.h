#pragma once

#include <userver/server/http/http_method.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/crypto/certificate.hpp>
#include <userver/crypto/private_key.hpp>
#include <userver/components/tcp_acceptor_base.hpp>

#include <atomic>
#include <exception>
#include <span>
#include <unordered_map>
#include <functional>

namespace unetwork::util {
struct string_hash {
  using hash_type = std::hash<std::string_view>;
  using is_transparent = void;
  size_t operator()(const char* str) const { return hash_type{}(str); }
  size_t operator()(std::string_view str) const { return hash_type{}(str); }
  size_t operator()(std::string const& str) const { return hash_type{}(str); }
};

template <typename T>
using string_map = std::unordered_map<std::string, T, string_hash, std::equal_to<>>;
}  // namespace unetwork::util

namespace unetwork::http {

using userver::server::http::HttpMethod;
using userver::server::http::HttpStatus;

using Headers = util::string_map<std::string>;

struct Request {
  std::string url;
  HttpMethod method;
  Headers headers;
  std::vector<std::byte> content;

  const userver::engine::io::Sockaddr* client_address;
  bool keepalive = false;
};

struct Response {
  HttpStatus status = HttpStatus::kOk;
  Headers headers;
  std::vector<std::byte> content;
  std::string_view content_type;
  bool keepalive = false;
  std::function<void()> post_send_cb;
  std::function<void(std::unique_ptr<userver::engine::io::RwBase>)> upgrade_connection;
};

struct HttpStatusException : public std::exception {
  HttpStatus status;
  HttpStatusException(HttpStatus s) : status(s) {}
};

class HttpServer : public userver::components::TcpAcceptorBase {
 public:
  struct Config {
    bool allow_encoding = true;
    struct TlsConfig
    {
      userver::crypto::Certificate cert;
      userver::crypto::PrivateKey  key;
    };

    std::optional<TlsConfig> tls;
  };

  HttpServer(const userver::components::ComponentConfig& component_config,
             const userver::components::ComponentContext& component_context);

  enum class OperationMode
  {
    Normal,
    Throttled // Server will response with status kTooManyRequests on all requests and close connection
  };

  void SetOperationMode(OperationMode opmode);

 private:
  virtual Response HandleRequest(const Request& request) = 0;

  void ProcessSocket(userver::engine::io::Socket&& sock) override final;

  Config config;
  std::atomic<OperationMode> operation_mode = OperationMode::Normal;
};

class SimpleHttpServer : public HttpServer {
 public:
  struct Config {
    std::string content_type = "text/plain";
  };

  SimpleHttpServer(const userver::components::ComponentConfig& component_config,
                   const userver::components::ComponentContext& component_context);

 protected:
  virtual std::string OnRequest(const Request& request) = 0;

 private:
  Response HandleRequest(const Request& request) override final {
    std::string respBody = OnRequest(request);
    Response resp;
    resp.keepalive = request.keepalive;
    resp.content.insert(resp.content.end(), (const std::byte*)respBody.data(),
                        (const std::byte*)(respBody.data() + respBody.size()));
    resp.content_type = config.content_type;
    return resp;
  }

  Config config;
};

}  // namespace unetwork::http
