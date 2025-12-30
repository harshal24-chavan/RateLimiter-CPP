// tomlParser.h

#pragma once // Prevents this file from being included multiple times

#include <string>
#include <vector>

/**
 * @brief Holds the application's configuration, loaded from the TOML file.
 */
struct RuleConfig {
    std::string endpoint;      // e.g., "/api/v1/login"
    std::string strategy;      // e.g., "fixed_window" or "token_bucket"
    int limit;                 // Max requests or Bucket capacity
    
    // Time normalization
    int time_limit;            // The value (e.g., 5)
    std::string time_unit;     // The unit (e.g., "minutes")
    
    // Token Bucket specific 
    int refill_rate;           // How many tokens added per unit
};

// top-level Config
struct RateLimiterConfig {
    std::string host;
    int port;
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
RateLimiterConfig parseTomlFile(const std::string& filename);
