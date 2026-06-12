// Copyright 2026 The SuvOS Authors
// HTTP route handling for suvos-gateway.

#include "gateway_routes.h"

#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <vector>

#include "gateway_aec.h"
#include "gateway_config.h"
#include "gateway_http.h"
#include "gateway_suvosd.h"

namespace suvos_gateway {

namespace {

std::string healthJson(bool *ok) {
  const bool uiBundleReady =
      pathReadable(std::string(kUiRoot) + "/index.html") &&
      pathReadable(std::string(kUiRoot) + "/styles.css") &&
      pathReadable(std::string(kUiRoot) + "/app.js");

  const SuvosdResult status = callSuvosd({"status-json"});
  const bool suvosdOk = status.transportOk && status.code == 0;
  const bool apiSocketAvailable = suvosdOk && jsonFlagTrue(status.output, "apiSocketAvailable");
  const bool systemRootReadOnly = suvosdOk && jsonFlagTrue(status.output, "systemRootReadOnly");

  *ok = suvosdOk && uiBundleReady && apiSocketAvailable && systemRootReadOnly;

  std::ostringstream body;
  body << "{";
  body << "\"ok\":" << (*ok ? "true" : "false") << ",";
  body << "\"service\":\"suvos-gateway\",";
  body << "\"status\":\"" << (*ok ? "ok" : "degraded") << "\",";
  body << "\"suvosdOk\":" << (suvosdOk ? "true" : "false") << ",";
  body << "\"suvosdExitCode\":" << status.code << ",";
  body << "\"uiBundleReady\":" << (uiBundleReady ? "true" : "false") << ",";
  body << "\"apiSocketAvailable\":" << (apiSocketAvailable ? "true" : "false") << ",";
  body << "\"systemRootReadOnly\":" << (systemRootReadOnly ? "true" : "false");
  body << "}\n";
  return body.str();
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

// Map a `/api/system/power/<action>` request to a suvosd power command.
// Chromium never invokes shutdown directly: it POSTs here and suvosd performs
// the capability check and the privileged action (or returns mock JSON).
bool handlePowerRoute(int fd, const std::string &method, const std::string &path) {
  constexpr const char *kPrefix = "/api/system/power/";
  if (path.rfind(kPrefix, 0) != 0) {
    return false;
  }

  if (method != "POST") {
    sendJson(fd, 405, "Method Not Allowed",
             "{\"ok\":false,\"error\":\"method_not_allowed\"}\n");
    return true;
  }

  const std::string action = path.substr(std::strlen(kPrefix));
  if (action != "shutdown" && action != "reboot" && action != "logout") {
    sendJson(fd, 404, "Not Found",
             "{\"ok\":false,\"error\":\"unknown_power_action\"}\n");
    return true;
  }

  proxyJsonCommand(fd, {"power", action});
  return true;
}

std::string queryValue(const std::map<std::string, std::string> &queryValues,
                       const std::string &key) {
  auto item = queryValues.find(key);
  return item == queryValues.end() ? "" : item->second;
}

std::vector<std::string> commandWithOptionalValue(const std::vector<std::string> &base,
                                                  const std::string &value) {
  std::vector<std::string> parts = base;
  if (!value.empty()) {
    parts.push_back(value);
  }
  return parts;
}

void appendQueryOption(std::vector<std::string> *parts,
                       const std::map<std::string, std::string> &queryValues,
                       const std::string &key) {
  const std::string value = queryValue(queryValues, key);
  if (!value.empty()) {
    parts->push_back(key + "=" + value);
  }
}

bool requireMethod(int fd, const std::string &method, const std::string &expected) {
  if (method == expected) {
    return true;
  }

  sendJson(fd, 405, "Method Not Allowed",
           "{\"ok\":false,\"error\":\"method_not_allowed\"}\n");
  return false;
}

bool handleSystemRoute(int fd,
                       const std::string &method,
                       const std::string &path,
                       const std::map<std::string, std::string> &queryValues) {
  if (path == "/api/system/network") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"network-json"});
    return true;
  }

  if (path == "/api/system/network/config") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"network-config-json"});
    return true;
  }

  if (path == "/api/system/network/configure") {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    std::vector<std::string> parts = {"network", "configure"};
    appendQueryOption(&parts, queryValues, "mode");
    appendQueryOption(&parts, queryValues, "interface");
    appendQueryOption(&parts, queryValues, "address");
    appendQueryOption(&parts, queryValues, "gateway");
    appendQueryOption(&parts, queryValues, "dns");
    proxyJsonCommand(fd, parts);
    return true;
  }

  constexpr const char *kNetworkPrefix = "/api/system/network/";
  if (path.rfind(kNetworkPrefix, 0) == 0) {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    const std::string action = path.substr(std::strlen(kNetworkPrefix));
    proxyJsonCommand(
        fd, commandWithOptionalValue({"network", action}, queryValue(queryValues, "interface")));
    return true;
  }

  if (path == "/api/system/wifi") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"wifi-json"});
    return true;
  }

  if (path == "/api/system/wifi/config") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"wifi-config-json"});
    return true;
  }

  if (path == "/api/system/wifi/scan") {
    if (method != "GET" && method != "POST") {
      sendJson(fd, 405, "Method Not Allowed",
               "{\"ok\":false,\"error\":\"method_not_allowed\"}\n");
      return true;
    }
    proxyJsonCommand(fd, {"wifi-scan-json"});
    return true;
  }

  if (path == "/api/system/wifi/connect") {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    const std::string ssid = queryValue(queryValues, "ssid");
    if (ssid.empty()) {
      sendJson(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing_ssid\"}\n");
      return true;
    }
    std::vector<std::string> parts = {"wifi", "connect", "ssid=" + ssid};
    appendQueryOption(&parts, queryValues, "psk");
    proxyJsonCommand(fd, parts);
    return true;
  }

  if (path == "/api/system/wifi/forget") {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    proxyJsonCommand(fd, {"wifi", "forget"});
    return true;
  }

  constexpr const char *kWifiPrefix = "/api/system/wifi/";
  if (path.rfind(kWifiPrefix, 0) == 0) {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    const std::string action = path.substr(std::strlen(kWifiPrefix));
    proxyJsonCommand(
        fd, commandWithOptionalValue({"wifi", action}, queryValue(queryValues, "interface")));
    return true;
  }

  if (path == "/api/system/battery") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"battery-json"});
    return true;
  }

  if (path == "/api/system/bluetooth") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"bluetooth-json"});
    return true;
  }

  constexpr const char *kBluetoothPrefix = "/api/system/bluetooth/";
  if (path.rfind(kBluetoothPrefix, 0) == 0) {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    const std::string action = path.substr(std::strlen(kBluetoothPrefix));
    proxyJsonCommand(fd, {"bluetooth", action});
    return true;
  }

  if (path == "/api/system/brightness") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"brightness-json"});
    return true;
  }

  if (path == "/api/system/brightness/set") {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    const std::string percent = queryValue(queryValues, "percent");
    if (percent.empty()) {
      sendJson(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing_percent\"}\n");
      return true;
    }
    proxyJsonCommand(fd, {"brightness", "set", percent});
    return true;
  }

  if (path == "/api/system/audio") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"audio-json"});
    return true;
  }

  constexpr const char *kAudioPrefix = "/api/system/audio/";
  if (path.rfind(kAudioPrefix, 0) == 0) {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    const std::string action = path.substr(std::strlen(kAudioPrefix));
    if (action == "set") {
      const std::string percent = queryValue(queryValues, "percent");
      if (percent.empty()) {
        sendJson(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing_percent\"}\n");
        return true;
      }
      proxyJsonCommand(fd, {"audio", action, percent});
      return true;
    }
    proxyJsonCommand(fd, {"audio", action});
    return true;
  }

  if (path == "/api/system/datetime") {
    if (!requireMethod(fd, method, "GET")) {
      return true;
    }
    proxyJsonCommand(fd, {"datetime-json"});
    return true;
  }

  if (path == "/api/system/datetime/set") {
    if (!requireMethod(fd, method, "POST")) {
      return true;
    }
    const std::string epoch = queryValue(queryValues, "epoch");
    if (epoch.empty()) {
      sendJson(fd, 400, "Bad Request", "{\"ok\":false,\"error\":\"missing_epoch\"}\n");
      return true;
    }
    proxyJsonCommand(fd, {"datetime", "set", epoch});
    return true;
  }

  return false;
}

}  // namespace

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

  // Power routes accept POST and must run before the GET-only guard below.
  if (handlePowerRoute(fd, method, path)) {
    return;
  }
  if (handleSystemRoute(fd, method, path, queryValues)) {
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
    bool ok = false;
    const std::string body = healthJson(&ok);
    sendJson(fd, ok ? 200 : 503, ok ? "OK" : "Service Unavailable", body);
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
  if (path == "/api/time") {
    proxyJsonCommand(fd, {"time-json"});
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

}  // namespace suvos_gateway
