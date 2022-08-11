#pragma once
#include <userver/concurrent/queue.hpp>
#include "http_server.h"

namespace unetwork::websocket {

using CloseStatusInt = int16_t;

enum class CloseStatus : CloseStatusInt {
  kNormal = 1000,
  kGoingAway = 1001,
  kProtocolError = 1002,
  kUnsupportedData = 1003,
  kFrameTooLarge = 1004,
  kNoStatusRcvd = 1005,
  kAbnormalClosure = 1006,
  kBadMessageData = 1007,
  kPolicyViolation = 1008,
  kTooBigData = 1009,
  kExtensionMismatch = 1010,
  kServerError = 1011
};

struct Message {
  std::vector<std::byte> data;
  std::optional<CloseStatusInt> remoteCloseStatus;
  bool isText = false;
  bool closed = false;
};

using InboxMessageQueue = userver::concurrent::SpscQueue<Message>;

class WebSocketConnection {
 public:
  virtual InboxMessageQueue::Consumer GetMessagesConsumer() = 0;
  virtual void Send(Message&& message) = 0;
  virtual void Close(CloseStatusInt status_code) = 0;

  virtual const userver::engine::io::Sockaddr& RemoteAddr() const = 0;
  virtual const http::Headers& HandshakeHTTPHeaders() const = 0;
};

class WebSocketServer : private http::HttpServer {
 public:
  struct Config {
    bool debug_logging = false;
    unsigned max_remote_payload = 65536;
    unsigned fragment_size = 65536; // 0 - do not fragment
  };

  WebSocketServer(const ComponentConfig& component_config,
                  const ComponentContext& component_context);

 private:
  virtual void onNewWSConnection(std::shared_ptr<WebSocketConnection> connection) = 0;

  http::Response HandleRequest(const http::Request& request,
                               http::HttpConnection* connection) override final;
  void UpgradeConnection(http::HttpConnection* connection, http::Headers&& headers);

  Config config;
};

}  // namespace unetwork::websocket
