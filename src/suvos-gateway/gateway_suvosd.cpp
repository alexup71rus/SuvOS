// Copyright 2026 The SuvOS Authors
// suvosd control-socket client for suvos-gateway.

#include "gateway_suvosd.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "gateway_config.h"
#include "gateway_http.h"

namespace suvos_gateway {

namespace {

int connectSuvosd() {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, kSuvosdSocket, sizeof(addr.sun_path) - 1);

  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return -1;
  }

  return fd;
}

std::string readAll(int fd, size_t maxBytes) {
  std::string response;
  char buffer[4096];
  while (true) {
    ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n == 0) {
      break;
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      break;
    }
    if (response.size() < maxBytes) {
      size_t remaining = maxBytes - response.size();
      response.append(buffer, std::min(static_cast<size_t>(n), remaining));
    }
  }
  return response;
}

}  // namespace

bool jsonFlagTrue(const std::string &json, const std::string &key) {
  return json.find("\"" + key + "\":true") != std::string::npos;
}

SuvosdResult callSuvosd(const std::vector<std::string> &parts) {
  SuvosdResult result;
  int fd = connectSuvosd();
  if (fd < 0) {
    result.output = "suvos-gateway: suvosd socket unavailable\n";
    return result;
  }

  std::ostringstream request;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (!validPart(parts[i])) {
      result.output = "suvos-gateway: invalid request part\n";
      close(fd);
      return result;
    }
    if (i > 0) {
      request << '\t';
    }
    request << parts[i];
  }
  request << '\n';

  if (!writeAll(fd, request.str())) {
    result.output = "suvos-gateway: suvosd write failed\n";
    close(fd);
    return result;
  }

  shutdown(fd, SHUT_WR);
  std::string response = readAll(fd, kMaxSuvosdResponseBytes);
  close(fd);

  const size_t marker = response.rfind(kExitPrefix);
  if (marker == std::string::npos) {
    result.output = response;
    return result;
  }

  result.output = response.substr(0, marker);
  std::string status = response.substr(marker + std::strlen(kExitPrefix));
  while (!status.empty() && (status.back() == '\n' || status.back() == '\r')) {
    status.pop_back();
  }

  try {
    result.code = std::stoi(status);
    result.transportOk = true;
  } catch (...) {
    result.output = "suvos-gateway: malformed suvosd status\n";
  }

  return result;
}

}  // namespace suvos_gateway
