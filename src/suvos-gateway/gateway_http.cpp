// Copyright 2026 The SuvOS Authors
// HTTP transport, string, URL and file helpers for suvos-gateway.

#include "gateway_http.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "gateway_config.h"

namespace suvos_gateway {

bool writeAll(int fd, const std::string &data) {
  const char *cursor = data.data();
  size_t remaining = data.size();

  while (remaining > 0) {
    ssize_t written = write(fd, cursor, remaining);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    cursor += written;
    remaining -= static_cast<size_t>(written);
  }

  return true;
}

std::string jsonEscape(const std::string &value) {
  std::ostringstream out;
  for (unsigned char ch : value) {
    switch (ch) {
      case '\\': out << "\\\\"; break;
      case '"': out << "\\\""; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) {
          out << "\\u";
          const char *hex = "0123456789abcdef";
          out << "00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  return out.str();
}

std::string urlDecode(const std::string &value) {
  std::string out;
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') {
      out.push_back(' ');
      continue;
    }
    if (value[i] == '%' && i + 2 < value.size()) {
      const std::string hex = value.substr(i + 1, 2);
      char *end = nullptr;
      long decoded = std::strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        out.push_back(static_cast<char>(decoded));
        i += 2;
        continue;
      }
    }
    out.push_back(value[i]);
  }
  return out;
}

std::map<std::string, std::string> parseQuery(const std::string &query) {
  std::map<std::string, std::string> values;
  size_t cursor = 0;
  while (cursor <= query.size()) {
    size_t amp = query.find('&', cursor);
    std::string pair = query.substr(cursor, amp == std::string::npos ? std::string::npos : amp - cursor);
    if (!pair.empty()) {
      size_t eq = pair.find('=');
      std::string key = urlDecode(pair.substr(0, eq));
      std::string value = eq == std::string::npos ? "" : urlDecode(pair.substr(eq + 1));
      values[key] = value;
    }
    if (amp == std::string::npos) {
      break;
    }
    cursor = amp + 1;
  }
  return values;
}

std::string readFile(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    return {};
  }

  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

bool pathReadable(const std::string &path) {
  return access(path.c_str(), R_OK) == 0;
}

bool validPart(const std::string &value) {
  if (value.empty() || value.size() > 128) {
    return false;
  }
  for (unsigned char ch : value) {
    if (ch < 0x20 || ch == 0x7f || ch == '\t') {
      return false;
    }
  }
  return true;
}

std::string lowerAscii(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (unsigned char ch : value) {
    out.push_back(static_cast<char>(std::tolower(ch)));
  }
  return out;
}

std::string trimAscii(const std::string &input) {
  size_t begin = 0;
  while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) {
    ++begin;
  }

  size_t end = input.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) {
    --end;
  }

  return input.substr(begin, end - begin);
}

size_t httpHeaderEnd(const std::string &request, size_t *separatorSize) {
  size_t headerEnd = request.find("\r\n\r\n");
  if (headerEnd != std::string::npos) {
    *separatorSize = 4;
    return headerEnd;
  }

  headerEnd = request.find("\n\n");
  if (headerEnd != std::string::npos) {
    *separatorSize = 2;
    return headerEnd;
  }

  *separatorSize = 0;
  return std::string::npos;
}

size_t contentLengthFromHeaders(const std::string &headers) {
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
    if (name != "content-length") {
      continue;
    }

    const std::string value = trimAscii(line.substr(colon + 1));
    try {
      return static_cast<size_t>(std::stoul(value));
    } catch (...) {
      return 0;
    }
  }

  return 0;
}

std::string readHttpRequest(int fd) {
  std::string request;
  char buffer[512];

  while (request.size() < kMaxHttpRequestBytes) {
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

    size_t remaining = kMaxHttpRequestBytes - request.size();
    request.append(buffer, std::min(static_cast<size_t>(n), remaining));
    if (request.size() >= kMaxHttpRequestBytes) {
      break;
    }

    size_t separatorSize = 0;
    const size_t headerEnd = httpHeaderEnd(request, &separatorSize);
    if (headerEnd != std::string::npos) {
      const std::string headers = request.substr(0, headerEnd);
      const size_t contentLength = contentLengthFromHeaders(headers);
      const size_t targetSize = headerEnd + separatorSize + contentLength;
      if (request.size() >= targetSize || targetSize > kMaxHttpRequestBytes) {
        break;
      }
    }
  }

  return request;
}

std::string normalizePathSegments(const std::string &path) {
  std::vector<std::string> parts;
  size_t cursor = 0;

  while (cursor <= path.size()) {
    const size_t slash = path.find('/', cursor);
    const std::string part = path.substr(
        cursor, slash == std::string::npos ? std::string::npos : slash - cursor);

    if (part.empty() || part == ".") {
      // Skip duplicate slashes and current-directory segments.
    } else if (part == "..") {
      if (!parts.empty()) {
        parts.pop_back();
      }
    } else {
      parts.push_back(part);
    }

    if (slash == std::string::npos) {
      break;
    }
    cursor = slash + 1;
  }

  std::ostringstream out;
  out << "/";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      out << "/";
    }
    out << parts[i];
  }

  const bool trailingSlash = path.size() > 1 && path.back() == '/';
  if (trailingSlash && !parts.empty()) {
    out << "/";
  }

  return out.str();
}

std::string normalizeHttpTarget(const std::string &target) {
  const size_t queryStart = target.find('?');
  const std::string path =
      target.substr(0, queryStart == std::string::npos ? std::string::npos : queryStart);
  const std::string query = queryStart == std::string::npos ? "" : target.substr(queryStart);
  return normalizePathSegments(path) + query;
}

void sendJson(int fd, int status, const std::string &reason, const std::string &body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << " " << reason << "\r\n";
  response << "Content-Type: application/json; charset=utf-8\r\n";
  response << "Cache-Control: no-store\r\n";
  response << "Connection: close\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "\r\n";
  response << body;
  (void)writeAll(fd, response.str());
}

void sendBody(int fd, int status, const std::string &reason, const std::string &contentType, const std::string &body) {
  std::ostringstream response;
  response << "HTTP/1.1 " << status << " " << reason << "\r\n";
  response << "Content-Type: " << contentType << "\r\n";
  response << "Cache-Control: no-store\r\n";
  response << "Connection: close\r\n";
  response << "Content-Length: " << body.size() << "\r\n";
  response << "\r\n";
  response << body;
  (void)writeAll(fd, response.str());
}

}  // namespace suvos_gateway
