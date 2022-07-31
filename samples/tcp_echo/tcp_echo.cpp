#include <userver/components/loggable_component_base.hpp>
#include <userver/components/manager_controller_component.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/components/tracer.hpp>
#include <userver/concurrent/queue.hpp>
#include <userver/dynamic_config/fallbacks/component.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/engine/io.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/logging/component.hpp>
#include <userver/os_signals/component.hpp>
#include <userver/server/request/request_base.hpp>
#include <userver/utils/async.hpp>
#include <userver/utils/daemon_run.hpp>
#include <userver/utils/fast_scope_guard.hpp>
#include <userver/utils/scope_guard.hpp>

#include <unetwork/tcp_server.h>

using namespace userver;
using namespace std::chrono_literals;

class TCPEchoConnection final : public unetwork::TCPConnection {
 public:
  using TCPConnection::TCPConnection;

 private:
  engine::Task readTask;
  engine::Task writeTask;

  using Packet = std::array<std::byte, 32>;

  struct EchoData {
    Packet storage;
    size_t size;
  };

  using Queue = concurrent::NonFifoSpscQueue<EchoData>;
  std::shared_ptr<Queue> queue;

  void ReadTaskCoro(Queue::Producer& producer) {
    utils::FastScopeGuard closeQueue([&]() noexcept {
      LOG_INFO() << "Connection closed. Exit read coro";
      producer.Push({{}, 0});
    });

    EchoData readData;
    while (!engine::current_task::ShouldCancel()) {
      try {
        size_t nread = socket.RecvSome(readData.storage.data(),
                                       readData.storage.size(), {});
        if (nread > 0) {
          LOG_INFO() << fmt::format("{} bytes recieved", nread);
          readData.size = nread;
          producer.Push(std::move(readData));
        } else if (nread == 0) {
          return;
        }
      } catch (engine::io::IoException& e) {
        return;
      }
    }
  }

  void WriteTaskCoro(Queue::Consumer& consumer) {
    utils::FastScopeGuard onExit(
        [&]() noexcept { LOG_INFO() << "Connection closed. Exit write coro"; });
    EchoData writeData;
    while (!engine::current_task::ShouldCancel()) {
      try {
        if (!consumer.Pop(writeData, engine::Deadline::FromDuration(42ms)))
          continue;
        if (writeData.size == 0) {
          break;
        } else {
          LOG_INFO() << fmt::format("sending {} bytes", writeData.size);
          [[maybe_unused]] auto sent =
              socket.SendAll(writeData.storage.data(), writeData.size, {});
        }
      } catch (engine::io::IoException& e) {
        break;
      }
    }
  }

  void Start(engine::TaskProcessor& tp,
             std::shared_ptr<TCPConnection> self) override {
    queue = Queue::Create(1);
    readTask = engine::CriticalAsyncNoSpan(
        tp,
        [self, this](Queue::Producer&& producer) {
          utils::ScopeGuard cleanup(
              [this] { std::move(this->readTask).Detach(); });
          this->ReadTaskCoro(producer);
        },
        queue->GetProducer());

    writeTask = engine::CriticalAsyncNoSpan(
        tp,
        [self, this](Queue::Consumer&& consumer) {
          utils::ScopeGuard cleanup(
              [this] { std::move(this->writeTask).Detach(); });
          this->WriteTaskCoro(consumer);
        },
        queue->GetConsumer());
  }

  void Stop() override {
    readTask.RequestCancel();
    writeTask.RequestCancel();
  }
};

class TCPEchoServer final : public unetwork::TCPServer {
 public:
  using unetwork::TCPServer::TCPServer;

 private:
  std::shared_ptr<unetwork::TCPConnection> makeConnection(
      userver::engine::io::Socket&& s) override {
    return std::make_shared<TCPEchoConnection>(std::move(s));
  }
};

class TCPServerComponent final
    : public userver::components::LoggableComponentBase {
 public:
  TCPEchoServer server;
  static constexpr std::string_view kName = "tcp-echo-server";

  TCPServerComponent(
      const userver::components::ComponentConfig& component_config,
      const userver::components::ComponentContext& component_context)
      : userver::components::LoggableComponentBase(component_config,
                                                   component_context),
        server(component_config.As<unetwork::TCPServerConfig>(),
               component_context) {}

  void OnAllComponentsAreStopping() override {
    // close active connections
    server.Stop();
  }
};

int main(int argc, char* argv[]) {
  auto component_list = components::ComponentList()
                            .Append<os_signals::ProcessorComponent>()
                            .Append<components::Logging>()
                            .Append<components::Tracer>()
                            .Append<components::ManagerControllerComponent>()
                            .Append<components::StatisticsStorage>()
                            .Append<components::DynamicConfig>()
                            .Append<components::DynamicConfigFallbacks>()
                            .Append<TCPServerComponent>();

  return userver::utils::DaemonMain(argc, argv, component_list);
}
