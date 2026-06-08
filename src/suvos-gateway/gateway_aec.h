// Copyright 2026 The SuvOS Authors
// Admin Explorer Code (AEC) reverse proxy for suvos-gateway.

#ifndef SUVOS_GATEWAY_AEC_H
#define SUVOS_GATEWAY_AEC_H

#include <string>

namespace suvos_gateway {

// Open a TCP connection to host:port (used for AEC upstream).
int connectTcp(const char *host, int port);

bool isAecPath(const std::string &path);
bool aecEnabled();
bool aecRunning();
std::string aecStatusJson();

// Reverse-proxy an HTTP/WebSocket request to the AEC backend.
void proxyAec(int client, const std::string &request);

}  // namespace suvos_gateway

#endif  // SUVOS_GATEWAY_AEC_H
