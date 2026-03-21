#include <grpcpp/grpcpp.h>
#include <grpcpp/resource_quota.h>
// #include <iostream>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>

#include "EndPointRegistry.h"
#include "GRPC-RateLimiter.h"
#include "HashUtils.h"
#include "IRateLimitStrategy.h"
#include "RateLimitFactory.h"
#include "SyncManager.h"
#include "tomlParser.h"

int main(int argc, char **argv) {
  std::string configPath = "config.toml";

  // Allow passing config path as an argument: ./RateLimiter my_config.toml
  if (argc > 1) {
    configPath = argv[1];
  }

  // 1. Load Configuration
  std::vector<RuleConfig> rules;
  std::string redis_uri;
  int port;
  std::string redis_host;
  int redis_port(6379);

  try {

    RateLimiterConfig config = parseTomlFile(configPath);
    rules = config.rules;
    port = config.port;

    redis_uri = config.redis_uri;
    redis_host = config.redis_host;
    redis_port = config.redis_port;

    std::cout << "tomlParsed" << std::endl;

  } catch (const std::exception &e) {
    std::cerr << "[Critical] Failed to load config: " << e.what() << std::endl;
    return 1;
  }

  // 2. Initialize Redis
  sw::redis::ConnectionPoolOptions pool_opts;
  pool_opts.size = 1; // Allow 1 concurrent connections to Redis

  sw::redis::ConnectionOptions connection_opts;
  // keep alive should be true in order to use that same connection and no
  // handshakes again
  connection_opts.keep_alive = true;
  // low socket_timeout because we do not want the spsc queue to fill and wait
  // for long time if connection fails
  using namespace std::chrono_literals;
  connection_opts.socket_timeout = 500ms;

  connection_opts.host = redis_host;
  connection_opts.port = redis_port;

  std::shared_ptr<sw::redis::Redis> redis;
  try {
    redis = std::make_shared<sw::redis::Redis>(connection_opts, pool_opts);
    std::cout << "redis->ping()" << std::endl;
    redis->ping(); // Test connection
    std::cout << "redis responded" << std::endl;

  } catch (const sw::redis::Error &e) {
    std::cerr << "[Critical] Redis connection failed: " << e.what()
              << std::endl;
    return 1;
  }

  // 3. Build Strategy Map using the Factory
  auto epRegistry = std::make_shared<EndPointRegistry>(rules);

  auto strategyMap =
      std::make_shared<std::unordered_map<std::string, EndpointContext>>();

  for (const auto &rule : rules) {
    auto strategy = RateLimitFactory::createStrategy(rule, redis);
    if (strategy) {
      std::cout << rule.endpoint << std::endl;

      (*strategyMap)[rule.endpoint] = {epRegistry->getId(rule.endpoint),
                                       std::move(strategy)};
    }
  }

  // Start Sync Manager background Thread for flush and pull of global states
  size_t threadCount = 2;
  std::shared_ptr<SyncManager> syncManager = std::make_shared<SyncManager>(
      threadCount, rules.size(), redis, epRegistry, strategyMap);

  std::thread syncWorkerThread([&]() { syncManager->run(); });

  // 4. Start gRPC Server

  std::string server_address = "0.0.0.0:" + std::to_string(port);
  // 3 threads for GRPC server and 1 thread for SyncManager to sync with redis
  int grpcport = 50051;
  std::cout << grpcport << std::endl;

  RateLimitAsyncServer server(strategyMap, syncManager);

  std::cout << "SERVER VERSION 2.0 - BATCHING" << std::endl;

  // This call is BLOCKING. It will:
  // - Build the server
  // - Create a CompletionQueue
  // - Start 3 worker threads (grpcThreads)
  // - Handle the event loop
  server.Run(grpcport, threadCount);

  // 5. CLEANUP
  // The server has stopped, so we should stop the sync thread
  if (syncWorkerThread.joinable()) {
    syncWorkerThread.join();
  }
  return 0;
}
