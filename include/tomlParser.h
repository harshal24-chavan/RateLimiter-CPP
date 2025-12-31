// tomlParser.h

#pragma once // Prevents this file from being included multiple times

#include <string>
#include <vector>

/**
 * @brief Holds the application's configuration, loaded from the TOML file.
 */
struct RuleConfig {
  std::string endpoint;
  std::string strategy_type;
  int limit;
  int window_seconds;
};

// top-level Config
struct RateLimiterConfig {
  std::string host;
  int port;
  std::string redis_uri;
  std::string redis_host;
  int redis_port;

  std::vector<RuleConfig> rules; // Allow for multiple endpoints!
};
/**
 * @brief Parses a TOML configuration file.
 *
 * @param filename The path to the .toml file.
 * @return An AppConfig struct populated with values from the file.
 * If parsing fails, it returns an Config with default values.
 */
RateLimiterConfig parseTomlFile(const std::string &filename);
