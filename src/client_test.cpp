#include "ratelimit.grpc.pb.h" // Ensure this matches your filename
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
// Note the updated types from your .proto
using ratelimiter::BatchCheckRequest;
using ratelimiter::BatchCheckResponse;
using ratelimiter::RateLimitService;

// Adjust these for the "sweet spot" on your i3-540
const int BATCH_SIZE = 100;
const int MAX_IN_FLIGHT_BATCHES = 50;

struct AsyncClientCall {
  BatchCheckResponse response;
  ClientContext context;
  Status status;
  std::unique_ptr<ClientAsyncResponseReader<BatchCheckResponse>>
      response_reader;
};

// Global counters
std::atomic<uint64_t> total_user_checks(0);
std::atomic<uint64_t> allowed_user_checks(0);
std::atomic<int> batches_in_flight{0};

class RateLimitAsyncClient {
public:
  explicit RateLimitAsyncClient(std::shared_ptr<Channel> channel)
      : stub_(RateLimitService::NewStub(channel)) {}

  void CheckBatch(const std::vector<std::string> &users,
                  const std::string &endpoint) {
    BatchCheckRequest request;
    request.set_endpoint(endpoint);

    // Fill the batch
    for (const auto &user_id : users) {
      auto *check = request.add_checks();
      check->set_user_id(user_id);
    }

    AsyncClientCall *call = new AsyncClientCall;

    // Use the new CheckBatch RPC
    call->response_reader =
        stub_->PrepareAsyncCheckBatch(&call->context, request, &cq_);

    call->response_reader->StartCall();
    call->response_reader->Finish(&call->response, &call->status, (void *)call);
  }

  void AsyncCompleteRpc(int total_expected_batches) {
    void *got_tag;
    bool ok = false;
    int processed_batches = 0;

    while (processed_batches < total_expected_batches &&
           cq_.Next(&got_tag, &ok)) {
      AsyncClientCall *call = static_cast<AsyncClientCall *>(got_tag);

      if (ok && call->status.ok()) {
        // Process each result in the batch response
        for (const auto &res : call->response.results()) {
          if (res.allowed()) {
            allowed_user_checks++;
          }
        }
      } else if (!call->status.ok()) {
        std::cerr << "RPC Failed: " << call->status.error_message()
                  << std::endl;
      }

      delete call;
      batches_in_flight--;
      processed_batches++;
    }
  }

private:
  std::unique_ptr<RateLimitService::Stub> stub_;
  CompletionQueue cq_;
};

void run_batch_benchmark(int thread_id, int total_users_to_check,
                         std::string target) {
  auto channel =
      grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
  RateLimitAsyncClient client(channel);

  int total_batches = total_users_to_check / BATCH_SIZE;

  // Reader thread expects total_batches
  std::thread reader(&RateLimitAsyncClient::AsyncCompleteRpc, &client,
                     total_batches);

  std::string endpoint = "/api/v1/login";

  for (int b = 0; b < total_batches; ++b) {
    // Backpressure based on batches
    while (batches_in_flight.load() >= MAX_IN_FLIGHT_BATCHES) {
      std::this_thread::yield();
    }

    std::vector<std::string> user_batch;
    user_batch.reserve(BATCH_SIZE);

    for (int i = 0; i < BATCH_SIZE; ++i) {
      // Unique user IDs to avoid perfect cache hits if desired
      user_batch.push_back("user_" + std::to_string(thread_id) + "_" +
                           std::to_string(i * BATCH_SIZE));
    }

    batches_in_flight++;
    client.CheckBatch(user_batch, endpoint);
    total_user_checks += BATCH_SIZE;
  }

  if (reader.joinable())
    reader.join();
}

int main(int argc, char **argv) {
  int num_threads =
      2; // Try 2 threads first for the i3-540 (1 per physical core)
  int users_per_thread = 500000;

  std::string target = "0.0.0.0:50051";
  // std::string target = "unix:///tmp/rl.sock";

  std::cout << "Starting Batch Benchmark..." << std::endl;
  std::cout << "Batch Size: " << BATCH_SIZE << " | Threads: " << num_threads
            << std::endl;
  std::cout << "Total targeted user checks: "
            << (num_threads * users_per_thread) << std::endl;

  std::vector<std::thread> threads;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < num_threads; ++i) {
    threads.emplace_back(run_batch_benchmark, i, users_per_thread, target);
  }

  for (auto &t : threads)
    t.join();

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;

  double user_rps = total_user_checks / elapsed.count();

  std::cout << "-------------------------------------------" << std::endl;
  std::cout << "Benchmark Results:" << std::endl;
  std::cout << "Time elapsed:    " << elapsed.count() << "s" << std::endl;
  std::cout << "Total User Checks: " << total_user_checks.load() << std::endl;
  std::cout << "Total Allowed:     " << allowed_user_checks.load() << std::endl;
  std::cout << "Throughput:        " << user_rps << " Users/sec" << std::endl;
  std::cout << "-------------------------------------------" << std::endl;

  return 0;
}
