#include "GRPC-RateLimiter.h"
#include "HashUtils.h"

using namespace ratelimiter;

RateLimitAsyncServer::RateLimitAsyncServer(
    std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
        strategies,
    std::shared_ptr<SyncManager> syncManager)
    : strategies_(std::move(strategies)), syncManager_(std::move(syncManager)) {
}

RateLimitAsyncServer::~RateLimitAsyncServer() {
  server_->Shutdown();
  cq_->Shutdown();
}

void RateLimitAsyncServer::Run(int port, int threadCount) {
  std::string server_address = "0.0.0.0:" + std::to_string(port);
  grpc::ServerBuilder builder;
  builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
  builder.RegisterService(&service_);
  cq_ = builder.AddCompletionQueue();
  server_ = builder.BuildAndStart();

  std::cout << "GRPC Server listening on " << server_address << std::endl;

  HandleRpcs(threadCount);
}

void RateLimitAsyncServer::HandleRpcs(int threadCount) {
  std::vector<std::thread> workers;

  for (int i = 0; i < threadCount; ++i) {
    workers.emplace_back([this, i]() {
      // 1. SEED: Create the first CallData for this thread.
      auto *first_call =
          new CallData(&service_, cq_.get(), strategies_, syncManager_);
      first_call->setLane(i);
      first_call->Proceed(true);

      void *tag;
      bool ok;

      // 2. POLL: The shared queue
      while (cq_->Next(&tag, &ok)) {
        if (tag)
          static_cast<CallData *>(tag)->Proceed(ok);
      }
    });
  }

  for (auto &t : workers) {
    t.join();
  }
}

// CallData Implementation
RateLimitAsyncServer::CallData::CallData(
    RateLimitService::AsyncService *service, grpc::ServerCompletionQueue *cq,
    std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
        strategies,
    std::shared_ptr<SyncManager> syncManager)
    : service_(service), cq_(cq), strategies_(strategies),
      syncManager_(syncManager), responder_(&ctx_), status_(CREATE) {}

void RateLimitAsyncServer::CallData::Proceed(bool ok) {
  // 1. If the event failed (client disconnect, shutdown, etc.)
  if (!ok) {
    // If we were waiting for a request, we MUST spawn a replacement
    // before dying, or the "listening pool" shrinks by one.
    if (status_ == PROCESS) {
      auto *next_call = new CallData(service_, cq_, strategies_, syncManager_);
      next_call->setLane(lane_);
      next_call->Proceed(true);
    }
    delete this;
    return;
  }

  if (status_ == CREATE) {
    status_ = PROCESS;
    service_->RequestCheckBatch(&ctx_, &request_, &responder_, cq_, cq_, this);
  } else if (status_ == PROCESS) {
    // 2. CHAIN REACTION: Spawn the NEXT listener immediately
    auto *next_call = new CallData(service_, cq_, strategies_, syncManager_);
    next_call->setLane(lane_); // CRITICAL: Keep the thread in its lane
    next_call->Proceed(true);  // Kickstart the next one

    // 3. LOGIC: Handle the current request
    const std::string &endpoint = request_.endpoint();
    auto it = strategies_->find(endpoint);

    if (it == strategies_->end()) {
      status_ = FINISH;
      responder_.Finish(response_,
                        grpc::Status(grpc::StatusCode::NOT_FOUND, "Not Found"),
                        this);
      return;
    }

    // Process batch...
    for (const auto &check : request_.checks()) {
      uint64_t hashedId = HashUtils::hashID(check.user_id());
      auto result = it->second.strategy->isAllowed(hashedId);
      auto *res = response_.add_results();
      res->set_allowed(result.allowed);

      if (result.allowed) {
        SyncTask st = {hashedId, it->second.id, 1};
        syncManager_->pushTask(lane_, st);
      }
    }

    status_ = FINISH;
    responder_.Finish(response_, grpc::Status::OK, this);
  } else {
    // status_ is FINISH. gRPC is done with us.
    delete this;
  }
}
