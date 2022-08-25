#include <userver/components/loggable_component_base.hpp>
#include <userver/components/manager_controller_component.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/components/tracer.hpp>
#include <userver/dynamic_config/fallbacks/component.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/logging/component.hpp>
#include <userver/os_signals/component.hpp>
#include <userver/utils/daemon_run.hpp>

#include <unetwork/http_server.h>

using namespace userver;
using namespace unetwork;
using namespace std::chrono_literals;

class HelloServiceComponent final : public unetwork::http::SimpleHttpServer {
 public:
  static constexpr std::string_view kName = "http-hello-server";

  HelloServiceComponent(const userver::components::ComponentConfig& component_config,
                        const userver::components::ComponentContext& component_context)
      : unetwork::http::SimpleHttpServer(component_config, component_context) {
  }

  std::string OnRequest(const unetwork::http::Request& request) override {
    LOG_INFO() << fmt::format("{} request for {}", ToString(request.method), request.url);
    return "hello world!";
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
                            .Append<HelloServiceComponent>();

  return userver::utils::DaemonMain(argc, argv, component_list);
}
