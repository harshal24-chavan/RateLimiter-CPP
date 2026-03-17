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

  HandleRpcs(threadCount);
}

void RateLimitAsyncServer::HandleRpcs(int threadCount) {
  std::vector<std::thread> workers;
  for (size_t i = 0; i < (size_t)threadCount; ++i) {
    workers.emplace_back([this, i]() {
      new CallData(&service_, cq_.get(), strategies_, syncManager_, i);
      void *tag;
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

// CallData Implementation
RateLimitAsyncServer::CallData::CallData(
    RateLimitService::AsyncService *service, grpc::ServerCompletionQueue *cq,
    std::shared_ptr<std::unordered_map<std::string, EndpointContext>>
        strategies,
    std::shared_ptr<SyncManager> syncManager, size_t threadLane)
    : service_(service), cq_(cq), strategies_(strategies),
      syncManager_(syncManager), responder_(&ctx_), status_(CREATE),
      lane_(threadLane) {
  Proceed();
}

void RateLimitAsyncServer::CallData::Proceed() {
  if (status_ == CREATE) {
    status_ = PROCESS;
    service_->RequestCheckBatch(&ctx_, &request_, &responder_, cq_, cq_, this);
  } else if (status_ == PROCESS) {

    new CallData(service_, cq_, strategies_, syncManager_, lane_);

    const std::string &endpoint = request_.endpoint();
    auto it = strategies_->find(endpoint);

    // std::cout << endpoint << std::endl;

    if (it == strategies_->end()) {
      status_ = FINISH;
      responder_.Finish(
          response_,
          grpc::Status(grpc::StatusCode::NOT_FOUND, "Endpoint not found"),
          this);
      return;
    }

    auto &requests = request_.checks();
    response_.mutable_results()->Reserve(requests.size());

    for (const auto &check : requests) {
      uint64_t hashedId = HashUtils::hashID(check.user_id());
      auto result = it->second.strategy->isAllowed(hashedId);

      auto *res = response_.add_results();
      res->set_allowed(result.allowed);
      res->set_remaining_tokens(result.remaining);

      if (result.allowed) {
        SyncTask st = {hashedId, it->second.id, 1};
        syncManager_->pushTask(lane_, st);
      }
    }

    status_ = FINISH;
    responder_.Finish(response_, grpc::Status::OK, this);
  } else {
    delete this;
  }
}
