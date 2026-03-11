#pragma once

#include "tomlParser.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class EndPointRegistry {
private:
  // endpoints for faster access to get the string
  // could have done better than saying 1024 here itself
  // but said it because we most probably won't have this many paths to limit
  std::array<std::string, 1024> endpoints;
  // map to intern the endpoint for efficient struct in SPSC queue
  std::unordered_map<std::string, uint32_t> stringInternMap;

  uint16_t _defaultID;

  std::vector<RuleConfig> _rules;

  std::vector<size_t> endPointLimits;

public:
  EndPointRegistry(std::vector<RuleConfig> &rules) : _rules(rules) {
    endPointLimits.resize(_rules.size());

    // moving all the defined rules into the array and map
    for (uint32_t index = 0; index < _rules.size(); index++) {
      std::string ep = _rules[index].endpoint;

      if (ep == "default")
        _defaultID = index;

      endpoints[index] = ep;
      stringInternMap[ep] = index;
      endPointLimits[index] = _rules[index].limit;
    }
  }

  // std::string_view just points to the existing buffer
  // and doesn't make a temporary copy of the string
  uint32_t getId(const std::string &endpoint) const {
    // pointer to endpoint's value
    auto it = stringInternMap.find(endpoint);

    if (it == stringInternMap.end()) {
      // we do not have this endpoint so let's just give default value;
      return _defaultID;
    }
    return it->second;
  }

  std::string getEndPointString(uint32_t id) const { return endpoints[id]; }

  int getEndPointLimit(uint32_t endPointId) const {
    return endPointLimits[endPointId];
  }
};
