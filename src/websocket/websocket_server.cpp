#include <unetwork/websocket_server.h>
#include <userver/components/component.hpp>
#include <userver/utils/async.hpp>
#include <userver/utils/fast_scope_guard.hpp>
#include <userver/logging/log.hpp>
#include <userver/concurrent/mpsc_queue.hpp>

#include "utils.h"
#include "websocket_protocol.h"

using namespace userver;

namespace unetwork::websocket {

static WebSocketServer::Config Parse(const userver::yaml_config::YamlConfig& value,
                                     userver::formats::parse::To<WebSocketServer::Config>) {
  WebSocketServer::Config config;
  config.max_remote_payload = value["max_remote_payload"].As<unsigned>(config.max_remote_payload);
  config.fragment_size = value["fragment_size"].As<unsigned>(config.fragment_size);
  config.debug_logging = value["debug_logging"].As<bool>(config.debug_logging);
  return config;
}

static Message CloseMessage(CloseStatusInt status) { return {{}, status, false, true}; }

static Message DataMessage(std::vector<std::byte>&& payload, bool is_text) {
  return {std::move(payload), {}, is_text, false};
}

class WebSocketConnectionImpl : public WebSocketConnection {
 private:
  std::unique_ptr<IoBase> io;

  struct MessageExtended : Message {
    bool ping = false;
    bool pong = false;
  };

  using OutboxMessageQueue = userver::concurrent::MpscQueue<std::unique_ptr<MessageExtended>>;
  std::shared_ptr<InboxMessageQueue> inbox;
  std::shared_ptr<OutboxMessageQueue> outbox;
  OutboxMessageQueue::Producer  outboxProducer;
  userver::engine::Task readTask;
  http::Headers headers;
  const engine::io::Sockaddr remoteAddr;

  WebSocketServer::Config config;

 public:
  WebSocketConnectionImpl(std::unique_ptr<IoBase> io_, http::Headers&& h,
                          engine::io::Sockaddr&& remote_addr,
                          const WebSocketServer::Config& server_config)
      : io(std::move(io_)),
        inbox(InboxMessageQueue::Create(3)),
        outbox(OutboxMessageQueue::Create(3)),
        outboxProducer(outbox->GetMultiProducer()),
        headers(std::move(h)),
        remoteAddr(std::move(remote_addr)),
        config(server_config) {}

  ~WebSocketConnectionImpl()
  {
    if (config.debug_logging) LOG_DEBUG() << "Websocket connection closed";
  }

  void ReadTaskCoro() {
    auto writeTask = userver::utils::Async("ws-write", &WebSocketConnectionImpl::WriteTaskCoro, this);

    FrameParserState frame;
    InboxMessageQueue::Producer producer = inbox->GetProducer();
    try {
      while (!userver::engine::current_task::ShouldCancel()) {
        CloseStatusInt status = ReadWSFrame(frame, io.get(), config.max_remote_payload);
        if (config.debug_logging)
          LOG_DEBUG() << fmt::format(
              "Read frame isText {}, closed {}, data size {} status {} waitCont {}", frame.isText,
              frame.closed, frame.payload.size(), status, frame.waitingContinuation);
        if (status != 0) {
          MessageExtended closeMsg;
          closeMsg.remoteCloseStatus = status;
          SendExtended(std::move(closeMsg));
          producer.Push(CloseMessage(status));
          return;
        }
        if (frame.closed) {
          producer.Push(CloseMessage(frame.remoteCloseStatus));
          return;
        }
        if (frame.pingReceived) {
          MessageExtended pongMsg;          pongMsg.pong = true;
          SendExtended(std::move(pongMsg));
          frame.pingReceived = false;
          continue;
        }
        if (frame.pongReceived) {
          frame.pongReceived = false;
          continue;
        }
        if (frame.waitingContinuation) continue;
        producer.Push(DataMessage(std::move(frame.payload), frame.isText));
      }
    } catch (std::exception const& e) {
      if (config.debug_logging) LOG_DEBUG() << "Exception during frame parsing " << e;
    }
    producer.Push(CloseMessage((CloseStatusInt)CloseStatus::kAbnormalClosure));
  }

  void WriteTaskCoro() {

    OutboxMessageQueue::Consumer consumer = outbox->GetConsumer();
    while (!userver::engine::current_task::ShouldCancel()) {
      std::unique_ptr<MessageExtended> messagePtr;
      if (consumer.Pop(messagePtr)) {
        MessageExtended& message = *messagePtr;
        if (config.debug_logging) LOG_DEBUG() << "Write message " << message.data.size() << " bytes";
        if (message.ping) {
          SendExactly(io.get(), frames::PingFrame(), {});
        } else if (message.pong) {
          SendExactly(io.get(), frames::PongFrame(), {});
        } else if (message.remoteCloseStatus.has_value()) {
          SendExactly(io.get(), frames::CloseFrame(message.remoteCloseStatus.value()), {});
        } else if (!message.data.empty()) {
          std::span<const std::byte> dataToSend = message.data;
          bool firstFrame = true;
          while (dataToSend.size() > config.fragment_size && config.fragment_size > 0) {
            SendExactly(io.get(),
                        frames::DataFrame(dataToSend.first(config.fragment_size), message.isText,
                                   !firstFrame, false),
                        {});
            firstFrame = false;
            dataToSend = dataToSend.last(dataToSend.size() - config.fragment_size);
          }
          SendExactly(io.get(), frames::DataFrame(dataToSend, message.isText, !firstFrame, true), {});
        }
      }
    }
  }

  void Start(std::shared_ptr<WebSocketConnection> self) {
    readTask = userver::utils::Async("ws-read", [self, this] {
      userver::utils::FastScopeGuard cleanup(
          [this]() noexcept { std::move(this->readTask).Detach(); });
      this->ReadTaskCoro();
    });
  }

  void SendExtended(MessageExtended&& message)
  {
    outboxProducer.Push(std::make_unique<MessageExtended>(std::move(message)), {});
  }

  void Stop() {
    readTask.RequestCancel();
  }

  InboxMessageQueue::Consumer GetMessagesConsumer() override { return inbox->GetConsumer(); }

  void Send(Message&& message) override {
    MessageExtended mext;
    mext.isText = message.isText;
    mext.remoteCloseStatus = message.remoteCloseStatus;
    mext.data = std::move(message.data);
    SendExtended(std::move(mext));
  }

  void Close(CloseStatusInt status_code) override { Send(CloseMessage(status_code)); }

  const userver::engine::io::Sockaddr& RemoteAddr() const override
  {
    return remoteAddr;
  }

  const http::Headers& HandshakeHTTPHeaders() const override { return headers; }
};

WebSocketServer::WebSocketServer(const ComponentConfig& component_config,
                                 const ComponentContext& component_context)
    : http::HttpServer(component_config, component_context),
      config(component_config.As<Config>()) {}

void WebSocketServer::UpgradeConnection(http::HttpConnection* connection, http::Headers&& headers) {
  engine::io::Sockaddr remoteAddr = connection->RemoteAddr();
  std::shared_ptr<WebSocketConnectionImpl> wsConnection = std::make_shared<WebSocketConnectionImpl>(
      connection->Release(), std::move(headers), std::move(remoteAddr), config);
  wsConnection->Start(wsConnection);
  onNewWSConnection(wsConnection);
}

http::Response WebSocketServer::HandleRequest(const http::Request& request,
                                              http::HttpConnection* connection) {
  if (!TestHeaderVal(request.headers, "Upgrade", "websocket") ||
      !TestHeaderVal(request.headers, "Connection", "Upgrade"))
    throw http::HttpStatusException(http::HttpStatus::kBadRequest);

  const std::string& secWebsocketKey = GetOptKey(request.headers, "Sec-WebSocket-Key", "");
  if (secWebsocketKey.empty()) throw http::HttpStatusException(http::HttpStatus::kBadRequest);

  http::Response resp;
  resp.status = http::HttpStatus::kSwitchingProtocols;
  resp.headers["Connection"] = "Upgrade";
  resp.headers["Upgrade"] = "websocket";
  resp.headers["Sec-WebSocket-Accept"] = WebsocketSecAnswer(secWebsocketKey);
  resp.keepalive = true;

  resp.post_send_cb = [this, connection, headers = std::move(request.headers)]() mutable {
    this->UpgradeConnection(connection, std::move(headers));
  };
  return resp;
}

}  // namespace unetwork::websocket
