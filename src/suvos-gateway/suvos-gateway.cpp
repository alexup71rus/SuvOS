#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
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
#include <sys/select.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char *kSuvosdSocket = "/run/suvosd/control.sock";
constexpr const char *kUiRoot = "/system/suvos/ui";
constexpr const char *kBindAddress = "127.0.0.1";
constexpr const char *kAecHost = "127.0.0.1";
constexpr int kPort = 80;
constexpr int kAecPort = 3030;
constexpr const char *kExitPrefix = "__SUVOSD_EXIT__:";
constexpr size_t kMaxHttpRequestBytes = 4 * 1024 * 1024;
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

bool isAecPath(const std::string &path) {
  return path == "/aec" || path.rfind("/aec/", 0) == 0;
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

  size_t queryStart = target.find('?');
  std::string path = target.substr(0, queryStart);
  std::string query = queryStart == std::string::npos ? "" : target.substr(queryStart + 1);
  auto queryValues = parseQuery(query);

  if (path == "/api/aec/status") {
    sendJson(fd, 200, "OK", aecStatusJson());
    return;
  }

  if (isAecPath(path)) {
    proxyAec(fd, request);
    return;
  }

  if (method != "GET") {
    sendJson(fd, 405, "Method Not Allowed", "{\"ok\":false,\"error\":\"method_not_allowed\"}\n");
    return;
  }

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

} // namespace

int main() {
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);

  int server = createServer(kPort);
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
      handleClient(client);
      close(client);
      continue;
    }

    if (child == 0) {
      close(server);
      handleClient(client);
      close(client);
      _exit(0);
    }

    close(client);
  }
}
