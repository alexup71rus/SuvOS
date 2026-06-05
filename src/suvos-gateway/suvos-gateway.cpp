#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char *kSuvosdSocket = "/run/suvosd/control.sock";
constexpr const char *kUiRoot = "/system/suvos/ui";
constexpr const char *kBindAddress = "127.0.0.1";
constexpr int kPort = 8080;
constexpr const char *kExitPrefix = "__SUVOSD_EXIT__:";
constexpr size_t kMaxHttpRequestBytes = 8192;
constexpr size_t kMaxSuvosdResponseBytes = 1024 * 1024;

struct SuvosdResult {
  int code = 1;
  std::string output;
  bool transportOk = false;
};

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
    if (value[i] == '+' ) {
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
    if (request.find("\r\n\r\n") != std::string::npos || request.find("\n\n") != std::string::npos) {
      break;
    }
  }

  return request;
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

bool sendUiAsset(int fd, const std::string &path) {
  std::string filePath;
  std::string contentType;

  if (path == "/" || path == "/ui" || path == "/ui/" || path == "/ui/index.html") {
    filePath = std::string(kUiRoot) + "/index.html";
    contentType = "text/html; charset=utf-8";
  } else if (path == "/ui/styles.css") {
    filePath = std::string(kUiRoot) + "/styles.css";
    contentType = "text/css; charset=utf-8";
  } else if (path == "/ui/app.js") {
    filePath = std::string(kUiRoot) + "/app.js";
    contentType = "application/javascript; charset=utf-8";
  } else {
    return false;
  }

  std::string body = readFile(filePath);
  if (body.empty()) {
    sendJson(fd, 404, "Not Found", "{\"ok\":false,\"error\":\"asset_not_found\"}\n");
    return true;
  }

  sendBody(fd, 200, "OK", contentType, body);
  return true;
}

std::string resultJson(const SuvosdResult &result) {
  std::ostringstream body;
  body << "{";
  body << "\"ok\":" << (result.transportOk && result.code == 0 ? "true" : "false") << ",";
  body << "\"exitCode\":" << result.code << ",";
  body << "\"output\":\"" << jsonEscape(result.output) << "\"";
  body << "}\n";
  return body.str();
}

void proxyCommand(int fd, const std::vector<std::string> &parts) {
  SuvosdResult result = callSuvosd(parts);
  int httpStatus = result.transportOk ? (result.code == 0 ? 200 : 400) : 502;
  sendJson(fd, httpStatus, httpStatus == 200 ? "OK" : "Error", resultJson(result));
}

void proxyJsonCommand(int fd, const std::vector<std::string> &parts) {
  SuvosdResult result = callSuvosd(parts);
  int httpStatus = result.transportOk ? (result.code == 0 ? 200 : 400) : 502;
  if (httpStatus == 200) {
    sendJson(fd, 200, "OK", result.output);
    return;
  }

  sendJson(fd, httpStatus, "Error", resultJson(result));
}

void handleClient(int fd) {
  timeval timeout {};
  timeout.tv_sec = 5;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

  std::string request = readHttpRequest(fd);
  std::istringstream lines(request);
  std::string requestLine;
  std::getline(lines, requestLine);
  if (!requestLine.empty() && requestLine.back() == '\r') {
    requestLine.pop_back();
  }

  std::istringstream first(requestLine);
  std::string method;
  std::string target;
  std::string version;
  first >> method >> target >> version;

  if (method != "GET") {
    sendJson(fd, 405, "Method Not Allowed", "{\"ok\":false,\"error\":\"method_not_allowed\"}\n");
    return;
  }

  size_t queryStart = target.find('?');
  std::string path = target.substr(0, queryStart);
  std::string query = queryStart == std::string::npos ? "" : target.substr(queryStart + 1);
  auto queryValues = parseQuery(query);

  if (sendUiAsset(fd, path)) {
    return;
  }

  if (path == "/health") {
    sendJson(fd, 200, "OK", "{\"ok\":true,\"service\":\"suvos-gateway\"}\n");
    return;
  }
  if (path == "/api/ping") {
    proxyCommand(fd, {"ping"});
    return;
  }
  if (path == "/api/status") {
    proxyJsonCommand(fd, {"status-json"});
    return;
  }
  if (path == "/api/roles") {
    proxyJsonCommand(fd, {"roles-json"});
    return;
  }
  if (path == "/api/apps") {
    proxyJsonCommand(fd, {"apps-json"});
    return;
  }
  if (path == "/api/run") {
    auto name = queryValues.find("name");
    if (name == queryValues.end() || name->second.empty()) {
      sendJson(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing_name\"}\n");
      return;
    }
    proxyCommand(fd, {"run", name->second});
    return;
  }

  sendJson(fd, 404, "Not Found", "{\"ok\":false,\"error\":\"not_found\"}\n");
}

int createServer() {
  int server = socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) {
    std::cerr << "suvos-gateway: socket failed: " << strerror(errno) << "\n";
    return -1;
  }

  int enabled = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));

  sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(kPort);
  if (inet_pton(AF_INET, kBindAddress, &addr.sin_addr) != 1) {
    close(server);
    return -1;
  }

  if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    std::cerr << "suvos-gateway: bind failed: " << strerror(errno) << "\n";
    close(server);
    return -1;
  }

  if (listen(server, 16) != 0) {
    std::cerr << "suvos-gateway: listen failed: " << strerror(errno) << "\n";
    close(server);
    return -1;
  }

  return server;
}

} // namespace

int main() {
  signal(SIGPIPE, SIG_IGN);

  int server = createServer();
  if (server < 0) {
    return 1;
  }

  while (true) {
    int client = accept(server, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) {
        continue;
      }
      sleep(1);
      continue;
    }

    handleClient(client);
    close(client);
  }
}
