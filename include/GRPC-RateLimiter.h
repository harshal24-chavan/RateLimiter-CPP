#pragma once

#include "ratelimit.grpc.pb.h"
#include <grpcpp/grpcpp.h>
#include <memory>
#include <string>
#include <sw/redis++/queued_redis.h>
#include <sw/redis++/redis.h>
#include <unordered_map>
#include <vector>

#include "IRateLimitStrategy.h"
#include "SyncManager.h"

// Forward declarations to keep headers light
namespace ratelimiter {
class BatchCheckRequest;
class BatchCheckResponse;
} // namespace ratelimiter

struct EndpointContext {
  uint32_t id;
  std::unique_ptr<IRateLimitStrategy> strategy;
};

class RateLimitAsyncServer {
public:
  RateLimitAsyncServer(
      std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
          strategies,
      std::shared_ptr<SyncManager> syncManager);

  ~RateLimitAsyncServer();

  void Run(int port, int threadCount);

private:
  class CallData {
  public:
    CallData(ratelimiter::RateLimitService::AsyncService *service,
             grpc::ServerCompletionQueue *cq,
             std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
                 strategies,
             std::shared_ptr<SyncManager> syncManager, size_t threadLane);

    void Proceed();

  private:
    ratelimiter::RateLimitService::AsyncService *service_;
    grpc::ServerCompletionQueue *cq_;
    std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
        strategies_;
    std::shared_ptr<SyncManager> syncManager_;

    grpc::ServerContext ctx_;
    ratelimiter::BatchCheckRequest request_;
    ratelimiter::BatchCheckResponse response_;
    grpc::ServerAsyncResponseWriter<ratelimiter::BatchCheckResponse> responder_;

    size_t lane_;
    enum CallStatus { CREATE, PROCESS, FINISH };
    CallStatus status_;
  };

  void HandleRpcs(int threadCount);

  std::unique_ptr<grpc::ServerCompletionQueue> cq_;
  ratelimiter::RateLimitService::AsyncService service_;
  std::unique_ptr<grpc::Server> server_;
  std::shared_ptr<std::unordered_map<std::string, EndpointContext>> strategies_;
  std::shared_ptr<SyncManager> syncManager_;
};
