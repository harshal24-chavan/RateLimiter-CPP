#include <grpcpp/grpcpp.h>
#include <grpcpp/resource_quota.h>
// #include <iostream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

// Your project headers
#include "EndPointRegistry.h"
#include "HashUtils.h"
#include "IRateLimitStrategy.h"
#include "RateLimitFactory.h"
#include "SyncManager.h"
#include "ratelimit.grpc.pb.h"
#include "tomlParser.h"

struct EndpointContext {
  uint32_t id;
  std::unique_ptr<IRateLimitStrategy> strategy;
};

class RateLimitServiceImpl final
    : public ratelimiter::RateLimitService::Service {
private:
  std::shared_ptr<std::unordered_map<std::string, EndpointContext>> _strategies;

  std::shared_ptr<SyncManager> _syncManager;

public:
  explicit RateLimitServiceImpl(
      std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
          strategies,
      std::shared_ptr<SyncManager> syncManager)
      : _strategies(std::move(strategies)),
        _syncManager(std::move(syncManager)) {}

  // FIXED: Using CheckRequest and CheckResponse to match your .proto file
  grpc::Status Check(grpc::ServerContext *context,
                     const ratelimiter::CheckRequest *request,
                     ratelimiter::CheckResponse *response) override {

    static thread_local size_t threadLane = std::numeric_limits<size_t>::max();
    if (threadLane == std::numeric_limits<size_t>::max()) {
      threadLane = _syncManager->getLane();
    }

    const std::string &endpoint = request->endpoint();
    const std::string &user_id = request->user_id();

    // Perform a single lookup
    auto it = _strategies->find(endpoint);
    if (it == _strategies->end()) {
      it = _strategies->find("default");
    }

    uint32_t epID = 0; // Default or fallback ID

    if (it != _strategies->end()) {
      uint64_t hashedUserId =
          HashUtils::hashID(user_id); // get the hashed user_id

      // Access the strategy inside the context
      RateLimitResult result = it->second.strategy->isAllowed(hashedUserId);
      epID = it->second.id; // Use the ID we already stored in the context!

      response->set_allowed(result.allowed);
      response->set_remaining_tokens(result.remaining);

      // Only sync to Redis if the request was actually processed/allowed
      if (result.allowed) {
        uint64_t hashedUser = HashUtils::hashID(user_id);
        SyncTask st = {hashedUser, epID, 1};

        // Push to SPSC Queue (The Producer side of the pipe)
        if (!_syncManager->pushTask(threadLane, st)) {
          // Optional: handle full queue (e.g., log dropped sync)
        }
      }
    } else {
      // Fallback if not even "default" exists
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
  connection_opts.socket_timeout = 50ms;

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

  // 4. Start gRPC Server
  size_t threadCount = 3;
  std::shared_ptr<SyncManager> syncManager =
      std::make_shared<SyncManager>(3, rules.size(), redis, epRegistry);
  std::string server_address = "0.0.0.0:" + std::to_string(port);
  RateLimitServiceImpl service(strategyMap, syncManager);

  // 3 threads for GRPC server and 1 thread for SyncManager to sync with redis
  grpc::ResourceQuota rq;
  rq.SetMaxThreads(3);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.SetResourceQuota(rq);
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());

  // Wait for the server to shut down
  server->Wait();

  return 0;
}
