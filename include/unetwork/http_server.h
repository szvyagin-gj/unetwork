#pragma once

#include <unetwork/tcp_server.h>
#include <userver/server/http/http_method.hpp>
#include <userver/server/http/http_status.hpp>

#include <atomic>
#include <exception>
#include <span>
#include <unordered_map>

namespace unetwork::http {

using userver::server::http::HttpMethod;
using userver::server::http::HttpStatus;

struct HttpServerConfig : TCPServerConfig {
  bool allow_encoding = true;
};

struct SimpleHttpServerConfig : HttpServerConfig {
  std::string content_type = "text/plain";
};

HttpServerConfig Parse(const userver::yaml_config::YamlConfig& value,
                       userver::formats::parse::To<HttpServerConfig>);

SimpleHttpServerConfig Parse(const userver::yaml_config::YamlConfig& value,
                             userver::formats::parse::To<SimpleHttpServerConfig>);

using Headers = std::unordered_map<std::string, std::string>;

struct Request {
  std::string url;
  HttpMethod method;
  Headers headers;
  std::vector<std::byte> content;

  bool keepalive = false;
};

struct Response {
  HttpStatus status = HttpStatus::kOk;
  Headers headers;
  std::vector<std::byte> content;
  std::string_view content_type;

  bool keepalive = false;
};

struct HttpStatusException : public std::exception {
  HttpStatus status;
  HttpStatusException(HttpStatus s) : status(s) {}
};

class HttpServer;

class HttpConnection final : public TCPConnection {
 public:
  HttpConnection(userver::engine::io::Socket&& conn_sock, HttpServer* owner);

  void Start(userver::engine::TaskProcessor& tp, std::shared_ptr<TCPConnection> self) override;
  void Stop() override;

  // stop handling HTTP requests and return connection socket without closing
  userver::engine::io::Socket Detach();

  class HttpConnectionImpl;

 private:
  std::unique_ptr<HttpConnectionImpl> impl;
};

class HttpServer : public TCPServer {

 public:
  HttpServer(const HttpServerConfig& config,
             const userver::components::ComponentContext& component_context)
      : TCPServer(config, component_context), allow_encoding(config.allow_encoding) {}

  ~HttpServer() {
    // Connections can't operate without http sever and must be stopped.
    // It is not required for generic TCP server where connections do not bound
    // to their server i.e. server can be destroied while connections are live
    Stop();
  }

  virtual Response HandleRequest(const Request& request, HttpConnection* connection) = 0;

  enum class OperationMode
  {
    Normal,
    Throttled // Server will response with status kTooManyRequests on all requests and close connection
  };

  void SetOperationMode(OperationMode opmode);

 private:
  std::shared_ptr<TCPConnection> makeConnection(userver::engine::io::Socket&&) override;

  bool allow_encoding;
  std::atomic<OperationMode> operation_mode = OperationMode::Normal;
  friend class HttpConnection::HttpConnectionImpl;
};

class SimpleHttpServer : public HttpServer {
 public:
  SimpleHttpServer(const SimpleHttpServerConfig& config,
                   const userver::components::ComponentContext& component_context)
      : HttpServer(config, component_context), content_type(config.content_type) {}

 protected:
  virtual std::string OnRequest(const Request& request) = 0;

 private:
  Response HandleRequest(const Request& request, HttpConnection* /*connection*/) override final {
    std::string respBody = OnRequest(request);
    Response resp;
    resp.keepalive = request.keepalive;
    resp.content.insert(resp.content.end(), (const std::byte*)respBody.data(),
                        (const std::byte*)(respBody.data() + respBody.size()));
    resp.content_type = content_type;
    return resp;
  }

  std::string content_type;
};

}  // namespace unetwork::http
