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
    config.host = tbl["ratelimiter"]["host"].value_or("0.0.0.0");
    config.port = tbl["ratelimiter"]["port"].value_or(18000);
    config.redis_host = tbl["ratelimiter"]["redis_host"].value_or("0.0.0.0");
    config.redis_port = tbl["ratelimiter"]["redis_port"].value_or(1000);

    // --- Safely parse the 'array of tables' for Servers ---
    // Note the capital 'S' in "Servers"
    if (toml::array *rules_array = tbl["Rules"].as_array()) {

      rules_array->for_each([&](auto &&el) {
        if (toml::table *rule_table = el.as_table()) {

          // Safely get the "endpoint" string from within the table
          if (auto endpoint_node = rule_table->get("endpoint")) {
            if (auto endpoint_str = endpoint_node->value<std::string>()) {
              std::cout<<*endpoint_str;
              // config.serverList.push_back(*endpoint_str);
            }
          }
        }
      });
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
