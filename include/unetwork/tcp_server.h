#pragma once

#include <server/net/listener_config.hpp>
#include <userver/components/component_fwd.hpp>
#include <userver/engine/async.hpp>
#include <userver/engine/io/socket.hpp>
#include <userver/engine/task/task.hpp>
#include <userver/engine/task/task_processor_fwd.hpp>
#include <userver/formats/parse/common.hpp>
#include <userver/yaml_config/yaml_config.hpp>

#include <vector>

namespace unetwork {

using userver::components::ComponentConfig;
using userver::components::ComponentContext;

class TCPConnection {
 public:
  TCPConnection(userver::engine::io::Socket&& conn_sock)
      : socket(std::move(conn_sock)), fd(socket.Fd()), peername(socket.Getpeername()) {}
  virtual ~TCPConnection();

  virtual void Start(userver::engine::TaskProcessor& tp,
                     std::shared_ptr<TCPConnection> self) = 0;
  virtual void Stop() = 0;

  int Fd() const { return fd; }
  const userver::engine::io::Sockaddr& RemoteAddr() const {
    return peername;
  }

 protected:
  userver::engine::io::Socket socket;
  int fd;
  userver::engine::io::Sockaddr peername;
};

class DenyTCPConnection : public userver::engine::io::IoException {
 public:
  using userver::engine::io::IoException::IoException;
};

class TCPServer {
 public:
  struct Config {
    userver::server::net::ListenerConfig listener;
    std::string clients_task_processor;
  };

  TCPServer(const ComponentConfig& component_config, const ComponentContext& component_context);
  void Stop();

 private:
  Config config;
  userver::engine::Task listenerTask;
  std::vector<std::weak_ptr<TCPConnection>> connections;

  void AcceptConnection(userver::engine::io::Socket& listen_sock,
                        userver::engine::TaskProcessor& cli_tp);

  void ServerRun(userver::engine::io::Socket& listen_sock, userver::engine::TaskProcessor& cli_tp);

  virtual std::shared_ptr<TCPConnection> makeConnection(userver::engine::io::Socket&&) = 0;

 protected:
  virtual void onNewConnection(std::shared_ptr<TCPConnection>& connection);
};

}  // namespace unetwork
