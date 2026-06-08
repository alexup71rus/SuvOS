// Copyright 2026 The SuvOS Authors
// HTTP transport, string, URL and file helpers for suvos-gateway.

#ifndef SUVOS_GATEWAY_HTTP_H
#define SUVOS_GATEWAY_HTTP_H

#include <cstddef>
#include <map>
#include <string>

namespace suvos_gateway {

// Low-level socket write that retries on EINTR and short writes.
bool writeAll(int fd, const std::string &data);

// JSON string-value escaping.
std::string jsonEscape(const std::string &value);

// Percent/`+` decoding for query components.
std::string urlDecode(const std::string &value);

// Parse a URL query string into key/value pairs.
std::map<std::string, std::string> parseQuery(const std::string &query);

// Read an entire file into a string (empty on failure).
std::string readFile(const std::string &path);
bool pathReadable(const std::string &path);

// True when a request token is safe to forward to suvosd.
bool validPart(const std::string &value);

std::string lowerAscii(const std::string &value);
std::string trimAscii(const std::string &input);

// Locate the end of the HTTP header block; `separatorSize` receives 2 or 4.
std::size_t httpHeaderEnd(const std::string &request, std::size_t *separatorSize);
std::size_t contentLengthFromHeaders(const std::string &headers);

// Read a full HTTP request (headers + declared body) from a socket.
std::string readHttpRequest(int fd);

// Normalize an HTTP target, collapsing `.`/`..`/duplicate slashes.
std::string normalizePathSegments(const std::string &path);
std::string normalizeHttpTarget(const std::string &target);

// Response helpers.
void sendJson(int fd, int status, const std::string &reason, const std::string &body);
void sendBody(int fd, int status, const std::string &reason,
              const std::string &contentType, const std::string &body);

}  // namespace suvos_gateway

#endif  // SUVOS_GATEWAY_HTTP_H
