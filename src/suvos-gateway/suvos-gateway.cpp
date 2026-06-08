// Copyright 2026 The SuvOS Authors
// suvos-gateway: localhost-only HTTP front for suvosd and the SuvOS UI.
//
// Source layout:
//   gateway_config.h   - shared compile-time constants
//   gateway_http.*     - HTTP/string/url/file helpers + responses
//   gateway_suvosd.*   - suvosd control-socket client
//   gateway_aec.*      - Admin Explorer Code reverse proxy
//   gateway_routes.*   - request dispatch and route handlers
//   suvos-gateway.cpp  - server socket + accept loop (this file)

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "gateway_config.h"
#include "gateway_routes.h"

namespace suvos_gateway {
namespace {

int createServer(int port) {
  int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) {
    std::cerr << "suvos-gateway: socket failed: " << strerror(errno) << "\n";
    return -1;
  }

  int enabled = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, kBindAddress, &addr.sin_addr) != 1) {
    close(server);
    return -1;
  }

  if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    std::cerr << "suvos-gateway: bind failed on " << kBindAddress << ":" << port << ": " << strerror(errno) << "\n";
    close(server);
    return -1;
  }

  if (listen(server, 16) != 0) {
    std::cerr << "suvos-gateway: listen failed on " << kBindAddress << ":" << port << ": " << strerror(errno) << "\n";
    close(server);
    return -1;
  }

  std::cerr << "suvos-gateway: listening on " << kBindAddress << ":" << port << "\n";
  return server;
}

}  // namespace
}  // namespace suvos_gateway

int main() {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  int server = suvos_gateway::createServer(suvos_gateway::kPort);
  if (server < 0) {
    return 1;
  }

  while (true) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(server, &readSet);

    int ready = select(server + 1, &readSet, nullptr, nullptr, nullptr);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      sleep(1);
      continue;
    }

    if (!FD_ISSET(server, &readSet)) {
      continue;
    }

    int client = accept(server, nullptr, nullptr);
    if (client < 0) {
      continue;
    }

    pid_t child = fork();
    if (child < 0) {
      suvos_gateway::handleClient(client);
      close(client);
      continue;
    }

    if (child == 0) {
      close(server);
      suvos_gateway::handleClient(client);
      close(client);
      _exit(0);
    }

    close(client);
  }

  return 0;
}
