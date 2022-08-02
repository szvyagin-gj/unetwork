#pragma once
#include <userver/yaml_config/yaml_config.hpp>

namespace unetwork {

template <class Config>
void ParseAs(Config& config, const userver::yaml_config::YamlConfig& value) {
  config = Parse(value, userver::formats::parse::To<Config>{});
}

};  // namespace unetwork
