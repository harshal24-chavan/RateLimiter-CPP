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

using grpc::Server;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerBuilder;
using grpc::ServerCompletionQueue;
using grpc::ServerContext;
using ratelimiter::CheckRequest;
using ratelimiter::CheckResponse;
using ratelimiter::RateLimitService;

class RateLimitAsyncServer {
public:
  RateLimitAsyncServer(
      std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
          strategies,
      std::shared_ptr<SyncManager> syncManager)
      : strategies_(std::move(strategies)),
        syncManager_(std::move(syncManager)) {}

  ~RateLimitAsyncServer() {
    server_->Shutdown();
    cq_->Shutdown();
  }

  void Run(int port, int threadCount) {
    std::string server_address = "0.0.0.0:" + std::to_string(port);
    ServerBuilder builder;
    builder.AddListeningPort("unix:///tmp/rl.sock",
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&service_);
    cq_ = builder.AddCompletionQueue();
    server_ = builder.BuildAndStart();

    std::cout << "Async Server listening on " << server_address << std::endl;

    // Start the event loop (usually run in multiple threads for multi-core
    // CPUs)
    HandleRpcs(threadCount);
  }

private:
  // This inner class handles the lifecycle of one single RPC
  class CallData {
  public:
    CallData(RateLimitService::AsyncService *service, ServerCompletionQueue *cq,
             std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
                 strategies,
             std::shared_ptr<SyncManager> syncManager, size_t threadLane)
        : service_(service), cq_(cq), strategies_(strategies),
          syncManager_(syncManager), responder_(&ctx_), status_(CREATE),
          lane_(threadLane) {
      Proceed();
    }

    void Proceed() {
      if (status_ == CREATE) {
        status_ = PROCESS;
        // Tell gRPC to fill this object when a 'Check' call arrives
        service_->RequestCheck(&ctx_, &request_, &responder_, cq_, cq_, this);
      } else if (status_ == PROCESS) {
        // IMPORTANT: Spawn a new CallData instance to handle the next request
        // This ensures the server is always 'listening'
        new CallData(service_, cq_, strategies_, syncManager_, lane_);

        // --- CORE LOGIC START ---
        const std::string &endpoint = request_.endpoint();
        const std::string &user_id = request_.user_id();

        auto it = strategies_->find(endpoint);
        if (it == strategies_->end()) {
          it = strategies_->find("default");
        }

        if (it != strategies_->end()) {
          uint64_t hashedUserId = HashUtils::hashID(user_id);
          RateLimitResult result = it->second.strategy->isAllowed(hashedUserId);

          response_.set_allowed(result.allowed);
          response_.set_remaining_tokens(result.remaining);

          if (result.allowed) {
            SyncTask st = {hashedUserId, it->second.id, 1};
            syncManager_->pushTask(lane_, st);
          }
        } else {
          response_.set_allowed(true); // Fallback
        }
        // --- CORE LOGIC END ---

        status_ = FINISH;
        responder_.Finish(response_, grpc::Status::OK, this);
      } else {
        // Request is finished, memory cleanup
        delete this;
      }
    }

  private:
    RateLimitService::AsyncService *service_;
    ServerCompletionQueue *cq_;
    std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
        strategies_;
    std::shared_ptr<SyncManager> syncManager_;
    ServerContext ctx_;
    CheckRequest request_;
    CheckResponse response_;
    ServerAsyncResponseWriter<CheckResponse> responder_;
    size_t lane_;

    enum CallStatus { CREATE, PROCESS, FINISH };
    CallStatus status_;
  };

  void HandleRpcs(int threadCount) {
    // Since you have a dual-core i3-540, let's create 2 workers
    std::vector<std::thread> workers;
    for (size_t i = 0; i < threadCount; ++i) {
      workers.emplace_back([this, i]() {
        // Each thread gets a unique lane for the SyncManager
        new CallData(&service_, cq_.get(), strategies_, syncManager_, i);

        void *tag; // uniquely identifies a request (pointer to CallData)
        bool ok;
        while (cq_->Next(&tag, &ok)) {
          if (!ok)
            break;
          static_cast<CallData *>(tag)->Proceed();
        }
      });
    }

    for (auto &t : workers)
      t.join();
  }

  std::unique_ptr<ServerCompletionQueue> cq_;
  RateLimitService::AsyncService service_;
  std::unique_ptr<Server> server_;
  std::shared_ptr<std::unordered_map<std::string, EndpointContext>> strategies_;
  std::shared_ptr<SyncManager> syncManager_;
};

// class RateLimitServiceImpl final
//     : public ratelimiter::RateLimitService::Service {
// private:
//   std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
//   _strategies;
//
//   std::shared_ptr<SyncManager> _syncManager;
//
// public:
//   explicit RateLimitServiceImpl(
//       std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
//           strategies,
//       std::shared_ptr<SyncManager> syncManager)
//       : _strategies(std::move(strategies)),
//         _syncManager(std::move(syncManager)) {}
//
//   // FIXED: Using CheckRequest and CheckResponse to match your .proto file
//   grpc::Status Check(grpc::ServerContext *context,
//                      const ratelimiter::CheckRequest *request,
//                      ratelimiter::CheckResponse *response) override {
//
//     static thread_local size_t threadLane =
//     std::numeric_limits<size_t>::max(); if (threadLane ==
//     std::numeric_limits<size_t>::max()) {
//       threadLane = _syncManager->getLane();
//     }
//
//     const std::string &endpoint = request->endpoint();
//     const std::string &user_id = request->user_id();
//
//     // Perform a single lookup
//     auto it = _strategies->find(endpoint);
//     if (it == _strategies->end()) {
//       it = _strategies->find("default");
//     }
//
//     uint32_t epID = 0; // Default or fallback ID
//
//     if (it != _strategies->end()) {
//       uint64_t hashedUserId =
//           HashUtils::hashID(user_id); // get the hashed user_id
//
//       // Access the strategy inside the context
//       RateLimitResult result = it->second.strategy->isAllowed(hashedUserId);
//       epID = it->second.id; // Use the ID we already stored in the context!
//
//       response->set_allowed(result.allowed);
//       response->set_remaining_tokens(result.remaining);
//
//       // Only sync to Redis if the request was actually processed/allowed
//       if (result.allowed) {
//         uint64_t hashedUser = HashUtils::hashID(user_id);
//         SyncTask st = {hashedUser, epID, 1};
//
//         // Push to SPSC Queue (The Producer side of the pipe)
//         if (!_syncManager->pushTask(threadLane, st)) {
//           // Optional: handle full queue (e.g., log dropped sync)
//         }
//       }
//     } else {
//       // Fallback if not even "default" exists
//       response->set_allowed(true);
//       response->set_remaining_tokens(-1);
//     }
//
//     return grpc::Status::OK;
//   }
// };

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
  size_t threadCount = 2;
  std::shared_ptr<SyncManager> syncManager = std::make_shared<SyncManager>(
      threadCount, rules.size(), redis, epRegistry);

  std::string server_address = "0.0.0.0:" + std::to_string(port);
  // 3 threads for GRPC server and 1 thread for SyncManager to sync with redis
  int grpcport = 50051;
  RateLimitAsyncServer server(strategyMap, syncManager);

  // This call is BLOCKING. It will:
  // - Build the server
  // - Create a CompletionQueue
  // - Start 3 worker threads (grpcThreads)
  // - Handle the event loop
  server.Run(grpcport, threadCount);
  return 0;
}
