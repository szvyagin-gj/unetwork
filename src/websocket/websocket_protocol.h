#pragma once
#include <unetwork/websocket_server.h>
#include <unetwork/io_wrapper.h>
#include <userver/engine/io/socket.hpp>

#include <span>
#include <string>
#include <vector>

namespace unetwork::websocket {

namespace frames {
std::vector<std::byte> DataFrame(std::span<const std::byte> data, bool is_text,
                                 bool is_continuation, bool is_final);
std::vector<std::byte> CloseFrame(CloseStatusInt status_code);

const std::vector<std::byte>& PingFrame();
const std::vector<std::byte>& PongFrame();
const std::vector<std::byte>& CloseFrame();
}  // namespace frames

std::string WebsocketSecAnswer(std::string_view sec_key);

struct FrameParserState {
  bool closed = false;
  bool pingReceived = false;
  bool pongReceived = false;
  bool waitingContinuation = false;
  bool isText = false;
  CloseStatusInt remoteCloseStatus = 0;

  std::vector<std::byte> payload;
};

CloseStatusInt ReadWSFrame(FrameParserState& frame, IoBase* io, unsigned max_payload_size);

}  // namespace unetwork::websocket
