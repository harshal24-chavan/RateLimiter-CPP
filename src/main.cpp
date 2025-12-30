#include<iostream>
#include <iterator>
#include <memory>
#include <sw/redis++/redis.h>
#include<unordered_map>
#include<string>
#include "IRateLimitStrategy.h"
#include "tomlParser.h"
#include "RateLimitFactory.h"

int main(){

  std::string configPath = "config.toml";
  auto config = parseTomlFile(configPath);

  std::cout<<config.redis_uri<<std::endl;

  std::shared_ptr<sw::redis::Redis> redis = std::make_shared<sw::redis::Redis>(config.redis_uri);

  std::unordered_map<std::string, std::unique_ptr<IRateLimitStrategy>> strategy_map;
  for(const auto& rule: config.rules){
    std::cout<<rule.endpoint;
    strategy_map[rule.endpoint] = RateLimitFactory::createStrategy(rule, redis);
  }

  return 0;
}
