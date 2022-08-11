#pragma once

#include <userver/engine/io/tls_wrapper.hpp>

namespace unetwork {

class IoBase : public userver::engine::io::ReadableBase {
 public:
  [[nodiscard]] virtual size_t SendAll(const void* buf, size_t len,
                                       userver::engine::Deadline deadline) = 0;
  [[nodiscard]] virtual bool WaitWriteable(userver::engine::Deadline deadline) = 0;

  virtual void Close() = 0;
};

class SocketWrapper final : public IoBase {
 public:
  SocketWrapper(userver::engine::io::Socket&& socket_) : socket(std::move(socket_)) {}
  [[nodiscard]] size_t SendAll(const void* buf, size_t len,
                               userver::engine::Deadline deadline) override {
    return socket.SendAll(buf, len, deadline);
  }

  bool IsValid() const override { return socket.IsValid(); }

  [[nodiscard]] bool WaitReadable(userver::engine::Deadline deadline) override {
    return socket.WaitReadable(deadline);
  }

  [[nodiscard]] bool WaitWriteable(userver::engine::Deadline deadline) override {
    return socket.WaitWriteable(deadline);
  }

  [[nodiscard]] size_t ReadSome(void* buf, size_t len,
                                userver::engine::Deadline deadline) override {
    return socket.ReadSome(buf, len, deadline);
  }

  [[nodiscard]] size_t ReadAll(void* buf, size_t len, userver::engine::Deadline deadline) override {
    return socket.ReadAll(buf, len, deadline);
  }

  void Close() override {
    socket.Close();
  }

 private:
  userver::engine::io::Socket socket;
};

class TlsWrapper : public IoBase {
 public:
  TlsWrapper(userver::engine::io::Socket&& socket, const std::string& server_name,
             userver::engine::Deadline deadline)
      : tlsWrapper(userver::engine::io::TlsWrapper::StartTlsClient(std::move(socket), server_name,
                                                                   deadline)) {}

  TlsWrapper(userver::engine::io::Socket&& socket, const userver::crypto::Certificate& cert,
             const userver::crypto::PrivateKey& key, userver::engine::Deadline deadline,
             const std::vector<userver::crypto::Certificate>& cert_authorities = {})
      : tlsWrapper(userver::engine::io::TlsWrapper::StartTlsServer(std::move(socket), cert, key,
                                                                   deadline, cert_authorities)) {}

  [[nodiscard]] size_t SendAll(const void* buf, size_t len,
                               userver::engine::Deadline deadline) override {
    return tlsWrapper.SendAll(buf, len, deadline);
  }

  bool IsValid() const override { return tlsWrapper.IsValid(); }

  [[nodiscard]] bool WaitReadable(userver::engine::Deadline deadline) override {
    return tlsWrapper.WaitReadable(deadline);
  }

  [[nodiscard]] bool WaitWriteable(userver::engine::Deadline deadline) override {
    return tlsWrapper.WaitWriteable(deadline);
  }

  [[nodiscard]] size_t ReadSome(void* buf, size_t len,
                                userver::engine::Deadline deadline) override {
    return tlsWrapper.ReadSome(buf, len, deadline);
  }

  [[nodiscard]] size_t ReadAll(void* buf, size_t len, userver::engine::Deadline deadline) override {
    return tlsWrapper.ReadAll(buf, len, deadline);
  }

  void Close() override {
    tlsWrapper.StopTls({}).Close();
  }


 private:
  userver::engine::io::TlsWrapper tlsWrapper;
};

}  // namespace unetwork
