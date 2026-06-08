// Copyright 2026 The SuvOS Authors
// suvosd control-socket client for suvos-gateway.

#ifndef SUVOS_GATEWAY_SUVOSD_H
#define SUVOS_GATEWAY_SUVOSD_H

#include <string>
#include <vector>

namespace suvos_gateway {

struct SuvosdResult {
  int code = 1;
  std::string output;
  bool transportOk = false;
};

// Send a tab-separated command to suvosd and collect its response.
SuvosdResult callSuvosd(const std::vector<std::string> &parts);

// True when a `"key":true` flag is present in a suvosd JSON payload.
bool jsonFlagTrue(const std::string &json, const std::string &key);

}  // namespace suvos_gateway

#endif  // SUVOS_GATEWAY_SUVOSD_H
