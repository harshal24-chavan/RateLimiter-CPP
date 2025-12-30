#include "tomlParser.h"
#include <iostream>
#include <stdexcept>
#include <string>
#include <toml++/toml.hpp>
#include <vector>

RateLimiterConfig parseTomlFile(const std::string &filename) {
  RateLimiterConfig config; // Create a config object with default values

  toml::table tbl;
  try {
    tbl = toml::parse_file(filename);

    // --- Safely get the settings ---
    // Use .value_or() for safety. It returns the default if the key isn't
    // found.
    config.host = tbl["server"]["host"].value_or("0.0.0.0");
    config.port = tbl["server"]["port"].value_or(18000);
    config.redis_uri = tbl["redis"]["uri"].value_or("tcp://127.0.0.1:6379");

    if (auto rules_array = tbl["rules"].as_array()) {
      for (auto&& item : *rules_array) {
        // 'item' is a table representing one rule
        auto& table = *item.as_table();

        RuleConfig rule;
        rule.endpoint = table["endpoint"].value_or("default");
        rule.strategy_type = table["strategy"].value_or("fixed_window");
        rule.limit = table["limit"].value_or(10);
        rule.window_seconds = table["window_seconds"].value_or(60);

        // Add to your list of rules
        config.rules.push_back(rule);
      }
    }
  } catch (const toml::parse_error &err) {
    std::cerr << "TOML parsing failed:\n" << err << "\n";
    std::cerr << "Using default configuration.\n";
  } catch (const std::exception &e) {
    std::cerr << "An error occurred during config parsing: " << e.what()
      << std::endl;
    std::cerr << "Using default configuration.\n";
  }

  return config;
}
