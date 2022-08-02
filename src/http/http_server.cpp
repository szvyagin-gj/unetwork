#include <unetwork/http_server.h>
#include <userver/concurrent/queue.hpp>
#include <userver/logging/log.hpp>
#include <userver/utils/async.hpp>
#include <userver/utils/fast_scope_guard.hpp>

#include <http_parser.h>

#include <http/content_encoder.h>
#include <utils.h>

namespace unetwork::http {

using namespace userver;

HttpServerConfig Parse(const userver::yaml_config::YamlConfig& value,
                       userver::formats::parse::To<HttpServerConfig>) {
  HttpServerConfig config;
  ParseAs<TCPServerConfig>(config, value);
  config.allow_encoding = value["allow_encoding"].As<bool>();
  return config;
}

SimpleHttpServerConfig Parse(const userver::yaml_config::YamlConfig& value,
                             userver::formats::parse::To<SimpleHttpServerConfig>) {
  SimpleHttpServerConfig config;
  ParseAs<HttpServerConfig>(config, value);
  config.content_type = value["content_type"].As<std::string>();
  return config;
}

namespace {

HttpMethod ConvertHttpMethod(http_method method) {
  switch (method) {
    case HTTP_DELETE:
      return HttpMethod::kDelete;
    case HTTP_GET:
      return HttpMethod::kGet;
    case HTTP_HEAD:
      return HttpMethod::kHead;
    case HTTP_POST:
      return HttpMethod::kPost;
    case HTTP_PUT:
      return HttpMethod::kPut;
    case HTTP_CONNECT:
      return HttpMethod::kConnect;
    case HTTP_PATCH:
      return HttpMethod::kPatch;
    case HTTP_OPTIONS:
      return HttpMethod::kOptions;
    default:
      return HttpMethod::kUnknown;
  }
}

std::vector<char> status_response(HttpStatus status) {
  std::vector<char> result;
  fmt::format_to(std::back_inserter(result), "HTTP/1.1 {} {}\r\nConnection: close\r\n\r\n",
                 (int)status, status);
  return result;
}

std::vector<std::byte> serialize_response(const Response& response, const Request& request,
                                          bool allow_encoding) {
  std::vector<std::byte> content;
  std::string additionalHeaders;
  if (!response.content_type.empty()) {
    if (allow_encoding) {
      if (auto acceptEncodingIt = request.headers.find("Accept-Encoding");
          acceptEncodingIt != request.headers.end()) {
        if (EncodeResult er = encode({response.content.data(), response.content.size()},
                                     acceptEncodingIt->second);
            !er.encoded_data.empty()) {
          content = std::move(er.encoded_data);
          additionalHeaders += "Content-Encoding: ";
          additionalHeaders += er.encoding;
          additionalHeaders += "\r\n";
        }
      }
    }

    if (content.empty()) content = std::move(response.content);

    additionalHeaders += "Content-Type: ";
    additionalHeaders += response.content_type;
    additionalHeaders += "\r\n";
    additionalHeaders += "Content-Length: ";
    additionalHeaders += std::to_string(content.size());
    additionalHeaders += "\r\n";
  }
  if (!response.keepalive) additionalHeaders += "Connection: close\r\n";

  std::string head = "HTTP/1.1 ";
  head += std::to_string((int)response.status) + " " + ToString(response.status) + "\r\n";
  for (auto attrib : response.headers) head += attrib.first + ": " + attrib.second + "\r\n";
  head += additionalHeaders;
  head += "\r\n";

  std::vector<std::byte> result;
  result.reserve(head.size() + content.size());
  result.insert(result.end(), (const std::byte*)head.data(),
                (const std::byte*)(head.data() + head.size()));
  ;
  if (!content.empty()) result.insert(result.end(), content.begin(), content.end());
  return result;
}

static const std::vector<char> throttled_answer = status_response(HttpStatus::kTooManyRequests);

struct ParserState {
  Request curRequest;

  std::string headerField;
  bool complete_header = false;
  bool complete = false;

  http_parser parser;
  http_parser_settings settings;

  ParserState() {
    http_parser_init(&parser, HTTP_REQUEST);
    parser.data = this;

    http_parser_settings_init(&settings);
    settings.on_message_begin = http_on_message_begin;
    settings.on_url = http_on_url;
    settings.on_header_field = http_on_header_field;
    settings.on_header_value = http_on_header_value;
    settings.on_headers_complete = http_on_header_complete;
    settings.on_body = http_on_body;
    settings.on_message_complete = http_on_message_complete;
  }

  static int http_on_url(http_parser* parser, const char* at, size_t length) {
    auto self = static_cast<ParserState*>(parser->data);
    self->curRequest.url.append(at, length);
    return 0;
  }

  static int http_on_header_field(http_parser* parser, const char* at, size_t length) {
    auto self = static_cast<ParserState*>(parser->data);
    if (!self->complete_header)
      self->headerField.append(at, length);
    else {
      self->headerField.assign(at, length);
      self->complete_header = false;
    }
    return 0;
  }

  static int http_on_header_value(http_parser* parser, const char* at, size_t length) {
    auto self = static_cast<ParserState*>(parser->data);
    self->curRequest.headers[self->headerField].append(at, length);
    self->complete_header = true;
    return 0;
  }

  static int http_on_body(http_parser* parser, const char* at, size_t length) {
    auto self = static_cast<ParserState*>(parser->data);
    self->curRequest.content.insert(self->curRequest.content.end(), (const std::byte*)at,
                                    (const std::byte*)at + length);
    return 0;
  }

  static int http_on_message_begin(http_parser* parser) {
    auto self = static_cast<ParserState*>(parser->data);
    self->curRequest.method = ConvertHttpMethod((http_method)parser->method);
    self->complete = false;
    return 0;
  }

  static int http_on_header_complete(http_parser* parser) {
    auto self = static_cast<ParserState*>(parser->data);
    self->curRequest.keepalive = http_should_keep_alive(parser);
    return 0;
  }

  static int http_on_message_complete(http_parser* parser) {
    auto self = static_cast<ParserState*>(parser->data);
    self->complete = true;
    return 0;
  }
};

}  // namespace

class HttpConnection::HttpConnectionImpl {
 private:
  HttpServer* handler;
  HttpConnection* connection;
  engine::Task readTask;
  ParserState parserState;

  void OnRequest(engine::io::Socket& socket) {
    Response response;
    std::optional<HttpStatus> errorStatus;

    Request curRequest = std::move(parserState.curRequest);
    try {
      response = handler->HandleRequest(curRequest, connection);
    } catch (HttpStatusException const& e) {
      errorStatus = e.status;
      LOG_INFO() << "Status exception " << ToString(e.status);
    } catch (std::exception const& e) {
      LOG_WARNING() << "Exception in http handler " << e;
      errorStatus = HttpStatus::kInternalServerError;
    }

    if (errorStatus.has_value()) {
      std::vector<char> respData = status_response(errorStatus.value());
      [[maybe_unused]] auto s = socket.SendAll(respData.data(), respData.size(), {});
      socket.Close();
    } else {
      std::vector<std::byte> respData =
          serialize_response(response, curRequest, handler->allow_encoding);
      [[maybe_unused]] auto s = socket.SendAll(respData.data(), respData.size(), {});
      if (!response.keepalive) socket.Close();
    }
  }

  void ReadTaskCoro(engine::io::Socket& socket) {
    std::array<std::byte, 512> buf;

    try {
      while (!engine::current_task::ShouldCancel()) {
        size_t nread = socket.RecvSome(buf.data(), buf.size(), {});
        if (nread > 0) {
          if (handler->operation_mode == HttpServer::OperationMode::Throttled) {
            [[maybe_unused]] auto s =
                socket.SendAll(throttled_answer.data(), throttled_answer.size(), {});
            return;
          }

          http_parser_execute(&parserState.parser, &parserState.settings, (const char*)buf.data(),
                              nread);
          if (parserState.parser.http_errno != 0) {
            // TODO: dump broken request
            LOG_WARNING() << "bad data in http request ";
            return;
          }

          if (parserState.complete) {
            OnRequest(socket);
            parserState.complete = false;
          }

        } else if (nread == 0)  // connection closed
          return;
      }
    } catch (engine::io::IoException& e) {
    }
  }

 public:
  HttpConnectionImpl(HttpServer* server) : handler(server) {}

  void Start(engine::TaskProcessor& tp, std::shared_ptr<TCPConnection> self,
             engine::io::Socket& socket) {
    readTask = utils::Async(tp, "http-connection", [self, this, &socket] {
      utils::FastScopeGuard cleanup([this]() noexcept { std::move(this->readTask).Detach(); });
      this->ReadTaskCoro(socket);
    });
    selfWeak = self;
  }

  void Stop() { readTask.RequestCancel(); }

  void SyncStop() {
    Stop();
    readTask.BlockingWait();
  }

  std::weak_ptr<TCPConnection> selfWeak;
};

HttpConnection::HttpConnection(engine::io::Socket&& conn_sock, HttpServer* owner)
    : TCPConnection(std::move(conn_sock)), impl(new HttpConnectionImpl(owner)) {}

void HttpConnection::Start(engine::TaskProcessor& tp, std::shared_ptr<TCPConnection> self) {
  assert(socket.IsValid());
  impl->Start(tp, self, socket);
}

void HttpConnection::Stop() { impl->Stop(); }

userver::engine::io::Socket HttpConnection::Detach() {
  // connection lifetime controlled by shared_ptr owned by coroutine.
  // when coroutine stops connection is destroyed
  if (auto lock = impl->selfWeak.lock()) {
    impl->SyncStop();
    return std::move(socket);
  }
  return {};
}

std::shared_ptr<TCPConnection> HttpServer::makeConnection(engine::io::Socket&& socket) {
  return std::make_shared<HttpConnection>(std::move(socket), this);
}

void HttpServer::SetOperationMode(HttpServer::OperationMode opmode) {
  if (opmode == operation_mode) return;
  operation_mode = opmode;

  LOG_INFO() << "Http server operation mode changed to "
             << (opmode == OperationMode::Throttled ? "throttled" : "normal");
}

}  // namespace unetwork::http
