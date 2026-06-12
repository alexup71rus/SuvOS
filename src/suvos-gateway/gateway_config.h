// Copyright 2026 The SuvOS Authors
// Shared compile-time configuration for suvos-gateway.

#ifndef SUVOS_GATEWAY_CONFIG_H
#define SUVOS_GATEWAY_CONFIG_H

#include <cstddef>

namespace suvos_gateway {

constexpr const char *kSuvosdSocket = "/run/suvosd/control.sock";
constexpr const char *kUiRoot = "/system/suvos/ui";
constexpr const char *kBindAddress = "127.0.0.1";
constexpr const char *kAecHost = "127.0.0.1";
constexpr int kPort = 80;
constexpr int kAecPort = 3030;
constexpr const char *kExitPrefix = "__SUVOSD_EXIT__:";
constexpr std::size_t kMaxHttpRequestBytes = 4 * 1024 * 1024;
constexpr std::size_t kMaxSuvosdResponseBytes = 1024 * 1024;
constexpr std::size_t kMaxSuvosdRequestPartBytes = 2048;

}  // namespace suvos_gateway

#endif  // SUVOS_GATEWAY_CONFIG_H
