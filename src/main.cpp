#include <grpcpp/grpcpp.h>
// #include <iostream>
#include <memory>
#include <string>
#include <unordered_map>

// Your project headers
#include "IRateLimitStrategy.h"
#include "RateLimitFactory.h"
#include "ratelimit.grpc.pb.h"
#include "tomlParser.h"

class RateLimitServiceImpl final
    : public ratelimiter::RateLimitService::Service {
private:
  std::unordered_map<std::string, std::unique_ptr<IRateLimitStrategy>>
      &_strategies;

public:
  explicit RateLimitServiceImpl(
      std::unordered_map<std::string, std::unique_ptr<IRateLimitStrategy>>
          &strategies)
      : _strategies(strategies) {}

  // FIXED: Using CheckRequest and CheckResponse to match your .proto file
  grpc::Status Check(grpc::ServerContext *context,
                     const ratelimiter::CheckRequest *request,
                     ratelimiter::CheckResponse *response) override {

    const std::string &endpoint = request->endpoint();
    const std::string &user_id = request->user_id();

    // DIAGNOSTIC LOG 1
    // std::cout << "[Request] User: " << user_id << " | Endpoint: " << endpoint
    // << std::endl;

    auto it = _strategies.find(endpoint);

    // Fallback logic
    if (it == _strategies.end()) {
      // std::cout << "[Warning] No specific strategy for " << endpoint
      // << ". Checking default..." << std::endl;
      it = _strategies.find("default");
    }

    if (it != _strategies.end()) {
      RateLimitResult result = it->second->isAllowed(user_id);

      // DIAGNOSTIC LOG 2
      // std::cout << "[Result] Allowed: " << (result.allowed ? "YES" : "NO")
      // << " | Remaining: " << result.remaining << std::endl;

      response->set_allowed(result.allowed);
      response->set_remaining_tokens(result.remaining);
    } else {
      // DIAGNOSTIC LOG 3
      // std::cout << "[Critical] No strategy found at all! Falling open."
      // << std::endl;
      response->set_allowed(true);
      response->set_remaining_tokens(-1);
    }

    return grpc::Status::OK;
  }
};

int main(int argc, char **argv) {
  std::string configPath = "config.toml";

  // Allow passing config path as an argument: ./RateLimiter my_config.toml
  if (argc > 1) {
    configPath = argv[1];
  }

  // std::cout << "--- Initializing Rate Limiter Server ---" << std::endl;

  // 1. Load Configuration
  std::vector<RuleConfig> rules;
  std::string redis_uri;
  int port;
  std::string redis_host;
  int redis_port;

  try {

    RateLimiterConfig config = parseTomlFile(configPath);
    rules = config.rules;
    port = config.port;

    redis_uri = config.redis_uri;
    redis_host = config.redis_host;
    redis_port = config.redis_port;

  } catch (const std::exception &e) {
    std::cerr << "[Critical] Failed to load config: " << e.what() << std::endl;
    return 1;
  }

  // 2. Initialize Redis
  sw::redis::ConnectionPoolOptions pool_opts;
  pool_opts.size = 20; // Allow 20 concurrent connections to Redis

  sw::redis::ConnectionOptions connection_opts;
  connection_opts.host = redis_host;
  connection_opts.port = redis_port;

  std::shared_ptr<sw::redis::Redis> redis;
  try {
    redis = std::make_shared<sw::redis::Redis>(connection_opts, pool_opts);
    redis->ping(); // Test connection

  } catch (const sw::redis::Error &e) {
    std::cerr << "[Critical] Redis connection failed: " << e.what()
              << std::endl;
    return 1;
  }

  // 3. Build Strategy Map using the Factory
  std::unordered_map<std::string, std::unique_ptr<IRateLimitStrategy>>
      strategyMap;
  for (const auto &rule : rules) {
    auto strategy = RateLimitFactory::createStrategy(rule, redis);
    if (strategy) {
      // std::cout << "[Factory] Created " << rule.strategy_type << " for "
      // << rule.endpoint << std::endl;
      strategyMap[rule.endpoint] = std::move(strategy);
    }
  }

  // 4. Start gRPC Server
  std::string server_address = "0.0.0.0:" + std::to_string(port);
  RateLimitServiceImpl service(strategyMap);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  // std::cout << "[Server] gRPC Server listening on " << server_address
  // << std::endl;

  // Wait for the server to shut down
  server->Wait();

  return 0;
}
