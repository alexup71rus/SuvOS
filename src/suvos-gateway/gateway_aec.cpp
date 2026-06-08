// Copyright 2026 The SuvOS Authors
// Admin Explorer Code (AEC) reverse proxy for suvos-gateway.

#include "gateway_aec.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sstream>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "gateway_config.h"
#include "gateway_http.h"

namespace suvos_gateway {

namespace {

bool streamFromTo(int from, int to) {
  char buffer[8192];
  while (true) {
    ssize_t n = read(from, buffer, sizeof(buffer));
    if (n == 0) {
      return true;
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (!writeAll(to, std::string(buffer, static_cast<size_t>(n)))) {
      return false;
    }
  }
}

void relayBidirectional(int client, int upstream) {
  while (true) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(client, &readSet);
    FD_SET(upstream, &readSet);

    const int maxFd = std::max(client, upstream);
    int ready = select(maxFd + 1, &readSet, nullptr, nullptr, nullptr);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }

    char buffer[8192];
    if (FD_ISSET(client, &readSet)) {
      ssize_t n = read(client, buffer, sizeof(buffer));
      if (n <= 0) {
        return;
      }
      if (!writeAll(upstream, std::string(buffer, static_cast<size_t>(n)))) {
        return;
      }
    }

    if (FD_ISSET(upstream, &readSet)) {
      ssize_t n = read(upstream, buffer, sizeof(buffer));
      if (n <= 0) {
        return;
      }
      if (!writeAll(client, std::string(buffer, static_cast<size_t>(n)))) {
        return;
      }
    }
  }
}

void clearReceiveTimeout(int fd) {
  timeval timeout {};
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

bool requestWantsWebSocket(const std::string &headers) {
  std::istringstream lines(headers);
  std::string line;
  std::getline(lines, line);

  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    const std::string name = lowerAscii(trimAscii(line.substr(0, colon)));
    const std::string value = lowerAscii(trimAscii(line.substr(colon + 1)));
    if (name == "upgrade" && value.find("websocket") != std::string::npos) {
      return true;
    }
  }

  return false;
}

std::string proxyRequestForAec(const std::string &request, bool *isWebSocket) {
  size_t separatorSize = 0;
  const size_t headerEnd = httpHeaderEnd(request, &separatorSize);
  if (headerEnd == std::string::npos) {
    return {};
  }

  const std::string headers = request.substr(0, headerEnd);
  const std::string body = request.substr(headerEnd + separatorSize);
  *isWebSocket = requestWantsWebSocket(headers);

  std::istringstream lines(headers);
  std::string requestLine;
  std::getline(lines, requestLine);
  if (!requestLine.empty() && requestLine.back() == '\r') {
    requestLine.pop_back();
  }

  std::ostringstream out;
  std::istringstream first(requestLine);
  std::string method;
  std::string target;
  std::string version;
  first >> method >> target >> version;
  if (method.empty() || target.empty() || version.empty()) {
    return {};
  }

  const std::string normalizedTarget = normalizeHttpTarget(target);
  const size_t queryStart = normalizedTarget.find('?');
  const std::string normalizedPath = normalizedTarget.substr(0, queryStart);
  if (!isAecPath(normalizedPath)) {
    return {};
  }

  out << method << " " << normalizedTarget << " " << version << "\r\n";
  out << "Host: " << kAecHost << ":" << kAecPort << "\r\n";
  out << "X-Forwarded-Host: suv.os\r\n";
  out << "X-Forwarded-Proto: http\r\n";

  std::string line;
  while (std::getline(lines, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      continue;
    }

    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    const std::string name = lowerAscii(trimAscii(line.substr(0, colon)));
    if (name == "connection" || name == "host" || name == "keep-alive" ||
        name == "proxy-connection") {
      continue;
    }

    out << line << "\r\n";
  }

  out << "Connection: " << (*isWebSocket ? "Upgrade" : "close") << "\r\n";
  out << "\r\n";
  out << body;
  return out.str();
}

}  // namespace

int connectTcp(const char *host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    close(fd);
    return -1;
  }

  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return -1;
  }

  return fd;
}

bool isAecPath(const std::string &path) {
  return path == "/aec" || path.rfind("/aec/", 0) == 0;
}

bool aecEnabled() {
  return access("/system/suvos/aec/bin/admin-explorer-code-server", X_OK) == 0;
}

bool aecRunning() {
  int fd = connectTcp(kAecHost, kAecPort);
  if (fd < 0) {
    return false;
  }
  close(fd);
  return true;
}

std::string aecStatusJson() {
  const bool enabled = aecEnabled();
  const bool running = enabled && aecRunning();
  std::ostringstream body;
  body << "{";
  body << "\"ok\":" << (enabled ? "true" : "false") << ",";
  body << "\"enabled\":" << (enabled ? "true" : "false") << ",";
  body << "\"running\":" << (running ? "true" : "false") << ",";
  body << "\"host\":\"" << kAecHost << "\",";
  body << "\"port\":" << kAecPort << ",";
  body << "\"url\":\"/aec/\"";
  body << "}\n";
  return body.str();
}

void proxyAec(int client, const std::string &request) {
  if (!aecEnabled()) {
    sendJson(client, 404, "Not Found", "{\"ok\":false,\"error\":\"aec_not_enabled\"}\n");
    return;
  }

  bool isWebSocket = false;
  std::string upstreamRequest = proxyRequestForAec(request, &isWebSocket);
  if (upstreamRequest.empty()) {
    sendJson(client, 400, "Bad Request", "{\"ok\":false,\"error\":\"bad_aec_request\"}\n");
    return;
  }

  int upstream = connectTcp(kAecHost, kAecPort);
  if (upstream < 0) {
    sendJson(client, 502, "Bad Gateway", "{\"ok\":false,\"error\":\"aec_unavailable\"}\n");
    return;
  }

  if (!writeAll(upstream, upstreamRequest)) {
    close(upstream);
    sendJson(client, 502, "Bad Gateway", "{\"ok\":false,\"error\":\"aec_write_failed\"}\n");
    return;
  }

  if (isWebSocket) {
    clearReceiveTimeout(client);
    clearReceiveTimeout(upstream);
    relayBidirectional(client, upstream);
  } else {
    (void)streamFromTo(upstream, client);
  }

  close(upstream);
}

}  // namespace suvos_gateway
