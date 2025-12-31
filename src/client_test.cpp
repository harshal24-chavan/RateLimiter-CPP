#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

// Include the generated headers
#include "ratelimit.grpc.pb.h"

using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;
using ratelimiter::RateLimitService;
using ratelimiter::CheckRequest;
using ratelimiter::CheckResponse;

class RateLimitClient {
public:
    RateLimitClient(std::shared_ptr<Channel> channel)
        : stub_(RateLimitService::NewStub(channel)) {}

    void CheckRateLimit(const std::string& user, const std::string& endpoint) {
        CheckRequest request;
        request.set_user_id(user);
        request.set_endpoint(endpoint);

        CheckResponse response;
        ClientContext context;

        // The actual RPC call
        Status status = stub_->Check(&context, request, &response);

        if (status.ok()) {
            std::cout << "Response -> Allowed: " << (response.allowed() ? "YES" : "NO ") 
                      << " | Remaining: " << response.remaining_tokens() << std::endl;
        } else {
            std::cerr << "RPC failed: " << status.error_message() << std::endl;
        }
    }

private:
    std::unique_ptr<RateLimitService::Stub> stub_;
};

int main() {
    // 1. Connect to the server running on localhost:50051
    RateLimitClient client(grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials()));

    std::string user = "test_user_1";
    std::string endpoint = "/api/v1/login";

    std::cout << "Testing Rate Limiter for " << endpoint << " (Limit is usually 5)..." << std::endl;

    // 2. Send 10 requests in a row
    for (int i = 1; i <= 100; ++i) {
        std::cout << "[" << i << "] ";
        client.CheckRateLimit(user, endpoint);
    }

    return 0;
}
