// Copyright 2026 The SuvOS Authors
// HTTP route handling for suvos-gateway.

#ifndef SUVOS_GATEWAY_ROUTES_H
#define SUVOS_GATEWAY_ROUTES_H

namespace suvos_gateway {

// Read, dispatch and answer a single client HTTP request.
void handleClient(int fd);

}  // namespace suvos_gateway

#endif  // SUVOS_GATEWAY_ROUTES_H
