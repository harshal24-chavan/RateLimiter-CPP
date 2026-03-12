#include "ratelimit.grpc.pb.h"
#include <atomic>
#include <chrono>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using grpc::Channel;
using grpc::ClientAsyncResponseReader;
using grpc::ClientContext;
using grpc::CompletionQueue;
using grpc::Status;
using ratelimiter::CheckRequest;
using ratelimiter::CheckResponse;
using ratelimiter::RateLimitService;

struct AsyncClientCall {
  ratelimiter::CheckResponse response;
  grpc::ClientContext context;
  grpc::Status status;
  std::unique_ptr<grpc::ClientAsyncResponseReader<ratelimiter::CheckResponse>>
      response_reader;
};

class RateLimitClient {
public:
  RateLimitClient(std::shared_ptr<Channel> channel)
      : stub_(RateLimitService::NewStub(channel)) {}

  // Minimal overhead call for benchmarking
  bool CheckRateLimit(const std::string &user, const std::string &endpoint) {
    CheckRequest request;
    request.set_user_id(user);
    request.set_endpoint(endpoint);

    CheckResponse response;
    ClientContext context;

    Status status = stub_->Check(&context, request, &response);
    return status.ok() && response.allowed();
  }

private:
  std::unique_ptr<RateLimitService::Stub> stub_;
};

// Use an atomic to track how many requests are currently in the air
std::atomic<int> in_flight{0};
const int MAX_IN_FLIGHT = 100; // Adjust this: too high = freeze, too low = slow

// Global counters for all threads
std::atomic<uint64_t> total_requests(0);
std::atomic<uint64_t> allowed_requests(0);

class RateLimitAsyncClient {
public:
  explicit RateLimitAsyncClient(std::shared_ptr<Channel> channel)
      : stub_(RateLimitService::NewStub(channel)) {}

  void Check(const std::string &user, const std::string &endpoint) {
    ratelimiter::CheckRequest request;
    request.set_user_id(user);
    request.set_endpoint(endpoint);

    AsyncClientCall *call = new AsyncClientCall;

    // 2. Now 'request' exists in this scope
    call->response_reader =
        stub_->PrepareAsyncCheck(&call->context, request, &cq_);

    call->response_reader->StartCall();
    call->response_reader->Finish(&call->response, &call->status, (void *)call);
  }

  void AsyncCompleteRpc(int total_expected) {
    void *got_tag;
    bool ok = false;
    int processed = 0;

    while (processed < total_expected && cq_.Next(&got_tag, &ok)) {
      AsyncClientCall *call = static_cast<AsyncClientCall *>(got_tag);
      if (ok && call->status.ok()) {
        if (call->response.allowed())
          allowed_requests++;
      }
      delete call;
      in_flight--; // Request finished, open a slot
      processed++;
    }
  }

private:
  std::unique_ptr<RateLimitService::Stub> stub_;
  CompletionQueue cq_; // The "Mailbox"
};

void run_async_benchmark(int thread_id, int total_reqs, std::string target) {
  auto channel =
      grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  RateLimitAsyncClient client(channel);

  // Start reader thread - it now knows how many to expect
  std::thread reader(&RateLimitAsyncClient::AsyncCompleteRpc, &client,
                     total_reqs);

  std::string user = "user_" + std::to_string(thread_id);
  std::string endpoint = "/api/v1/login";

  for (int i = 0; i < total_reqs; ++i) {
    // BACKPRESSURE: If we have too many requests in-flight, wait.
    // This stops your PC from hanging.
    while (in_flight.load() >= MAX_IN_FLIGHT) {
      std::this_thread::yield();
    }

    in_flight++;
    client.Check(user, endpoint);
    total_requests++;
  }

  // Wait for the reader to finish processing all responses before destroying
  // 'client'
  if (reader.joinable())
    reader.join();
}

int main(int argc, char **argv) {
  int num_threads = 2;
  int req_per_thread = 25000; // Adjust this to test longer durations
  std::string target = "unix:///tmp/rl.sock";
  ;

  std::cout << "Starting benchmark with " << num_threads << " threads..."
            << std::endl;
  std::cout << "Total targeted requests: " << (num_threads * req_per_thread)
            << std::endl;

  std::vector<std::thread> threads;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back(run_async_benchmark, i, req_per_thread, target);
  }

  for (auto &t : threads) {
    t.join();
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;

  double rps = total_requests / elapsed.count();

  std::cout << "-------------------------------------------" << std::endl;
  std::cout << "Benchmark Results:" << std::endl;
  std::cout << "Time elapsed:    " << elapsed.count() << "s" << std::endl;
  std::cout << "Total Requests:  " << total_requests.load() << std::endl;
  std::cout << "Total Allowed:   " << allowed_requests.load() << std::endl;
  std::cout << "Throughput:      " << rps << " RPS" << std::endl;
  std::cout << "-------------------------------------------" << std::endl;

  return 0;
}
