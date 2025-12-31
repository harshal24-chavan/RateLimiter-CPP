#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <grpcpp/grpcpp.h>

// Your project headers
#include "ratelimit.grpc.pb.h"
#include "tomlParser.h"
#include "RateLimitFactory.h"
#include "IRateLimitStrategy.h"

// The implementation of the gRPC service
class RateLimitServiceImpl final : public ratelimiter::RateLimitService::Service {
private:
    // Reference to the map created in main()
    std::unordered_map<std::string, std::unique_ptr<IRateLimitStrategy>>& _strategies;

public:
    explicit RateLimitServiceImpl(std::unordered_map<std::string, std::unique_ptr<IRateLimitStrategy>>& strategies)
        : _strategies(strategies) {}

    grpc::Status Check(grpc::ServerContext* context,
                       const ratelimiter::RateLimitRequest* request,
                       ratelimiter::RateLimitResponse* response) override {
        
        const std::string& endpoint = request->endpoint();
        const std::string& user_id = request->user_id();

        // 1. Look for the specific endpoint strategy
        auto it = _strategies.find(endpoint);
        
        // 2. Fallback to "default" if endpoint isn't specifically mapped
        if (it == _strategies.end()) {
            it = _strategies.find("default");
        }

        if (it != _strategies.end()) {
            RateLimitResult result = it->second->isAllowed(user_id);
            response->set_allowed(result.allowed);
            response->set_remaining_tokens(result.remaining);
            response->set_reset_time_unix(result.reset_at);
        } else {
            // Fail-open: if no rule exists at all, allow the request
            response->set_allowed(true);
        }

        return grpc::Status::OK;
    }
};

int main(int argc, char** argv) {
    std::string configPath = "config.toml";
    
    // Allow passing config path as an argument: ./RateLimiter my_config.toml
    if (argc > 1) {
        configPath = argv[1];
    }

    std::cout << "--- Initializing Rate Limiter Server ---" << std::endl;

    // 1. Load Configuration
    std::vector<RuleConfig> rules;
    std::string redis_uri;
    int port;

    try {
        rules = ConfigLoader::loadConfig(configPath);
        redis_uri = ConfigLoader::getRedisUri(configPath);
        port = ConfigLoader::getServerPort(configPath);
        std::cout << "[Config] Loaded " << rules.size() << " rules from " << configPath << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Critical] Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    // 2. Initialize Redis
    std::shared_ptr<sw::redis::Redis> redis;
    try {
        redis = std::make_shared<sw::redis::Redis>(redis_uri);
        redis->ping(); // Test connection
        std::cout << "[Redis] Connected to " << redis_uri << std::endl;
    } catch (const sw::redis::Error& e) {
        std::cerr << "[Critical] Redis connection failed: " << e.what() << std::endl;
        return 1;
    }

    // 3. Build Strategy Map using the Factory
    std::unordered_map<std::string, std::unique_ptr<IRateLimitStrategy>> strategyMap;
    for (const auto& rule : rules) {
        auto strategy = RateLimitFactory::createStrategy(rule, redis);
        if (strategy) {
            std::cout << "[Factory] Created " << rule.strategy_type << " for " << rule.endpoint << std::endl;
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
    std::cout << "[Server] gRPC Server listening on " << server_address << std::endl;

    // Wait for the server to shut down
    server->Wait();

    return 0;
}
