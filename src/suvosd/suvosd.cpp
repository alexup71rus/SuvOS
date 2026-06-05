#include <algorithm>
#include <csignal>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char *kSystemRoot = "/system/suvos";
constexpr const char *kDataRoot = "/data/suvos";
constexpr const char *kManifestDir = "/system/suvos/apps/manifest.d";
constexpr const char *kLegacyRegistryPath = "/system/suvos/apps/registry.tsv";
constexpr const char *kRolesPath = "/system/suvos/security/roles.conf";
constexpr const char *kDefaultRootHashPath = "/system/suvos/security/root-bootstrap.sha256";
constexpr const char *kRunDir = "/run/suvosd";
constexpr const char *kResponsesDir = "/run/suvosd/responses";
constexpr const char *kRequestFifo = "/run/suvosd/request";
constexpr const char *kControlSocket = "/run/suvosd/control.sock";
constexpr const char *kPidFile = "/run/suvosd/pid";
constexpr const char *kSessionRoleFile = "/run/suvosd/session.role";
constexpr const char *kSessionAuthFile = "/run/suvosd/session.auth";
constexpr const char *kExitPrefix = "__SUVOSD_EXIT__:";
constexpr int kResponseOpenRetries = 200;
constexpr int kResponseOpenSleepMicros = 10000;
constexpr int kAppTimeoutSeconds = 30;
constexpr size_t kMaxAppOutputBytes = 1024 * 1024;
constexpr size_t kMaxRequestLineBytes = 8192;
constexpr size_t kMaxRequestParts = 32;
constexpr size_t kMaxRequestPartBytes = 2048;
constexpr sig_atomic_t kMaxRequestWorkers = 16;

volatile sig_atomic_t gActiveRequestWorkers = 0;
volatile sig_atomic_t gActiveSocketWorkers = 0;

struct App {
  std::string name;
  std::string path;
  std::string permission;
  std::string description;
  std::string runtime;
  std::string uiEntry;
  std::string version;
};

struct CommandResult {
  int code = 0;
  std::string output;
};

struct RoleEntry {
  std::string name;
  std::vector<std::string> permissions;
};

struct RolePolicy {
  std::string defaultRole = "setup";
  std::string rootRole = "root";
  std::string rootHashFile = kDefaultRootHashPath;
  std::string userCreation = "deferred-ui";
  std::vector<RoleEntry> roles;
};

std::vector<std::string> split(const std::string &input, char separator) {
  std::vector<std::string> parts;
  std::string current;
  std::istringstream stream(input);

  while (std::getline(stream, current, separator)) {
    parts.push_back(current);
  }

  if (!input.empty() && input.back() == separator) {
    parts.emplace_back();
  }

  return parts;
}

std::string trim(const std::string &input) {
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

std::vector<std::string> splitList(const std::string &input) {
  std::vector<std::string> result;
  for (auto item : split(input, ',')) {
    item = trim(item);
    if (!item.empty()) {
      result.push_back(item);
    }
  }
  return result;
}

std::string lang() {
  const char *env = getenv("SUVOS_LANG");
  if (env == nullptr || env[0] == '\0') {
    return "ru";
  }

  std::string value(env);
  size_t separator = value.find('_');
  if (separator != std::string::npos) {
    value = value.substr(0, separator);
  }

  return value == "en" ? "en" : "ru";
}

bool isRu() {
  return lang() == "ru";
}

bool systemRootReadOnly() {
  std::ifstream mounts("/proc/mounts");
  std::string line;

  while (std::getline(mounts, line)) {
    auto parts = split(line, ' ');
    if (parts.size() >= 4 && parts[1] == "/system/suvos") {
      auto options = split(parts[3], ',');
      for (const auto &option : options) {
        if (option == "ro") {
          return true;
        }
      }
    }
  }

  return false;
}

std::string tr(const std::string &key) {
  const bool ru = isRu();

  if (key == "status.booted") return ru ? "SuvOS status: загружена" : "SuvOS status: booted";
  if (key == "status.daemon") return ru ? "SuvOS daemon: запущен (c++)" : "SuvOS daemon: running (c++)";
  if (key == "status.system_root") return ru ? "Системная зона" : "System root";
  if (key == "status.system_root_mode") return ru ? "Защита системной зоны" : "System root protection";
  if (key == "status.data_root") return ru ? "Writable-зона" : "Writable data root";
  if (key == "status.api_socket") return ru ? "API socket" : "API socket";
  if (key == "status.manifests") return ru ? "App manifests" : "App manifests";
  if (key == "status.read_only") return ru ? "read-only" : "read-only";
  if (key == "status.writable") return ru ? "writable" : "writable";
  if (key == "status.available") return ru ? "доступен" : "available";
  if (key == "status.missing") return ru ? "отсутствует" : "missing";
  if (key == "status.rootfs") return ru ? "Root filesystem: initramfs" : "Root filesystem: initramfs";
  if (key == "status.kernel") return ru ? "Ядро" : "Kernel";
  if (key == "status.uptime") return ru ? "Uptime" : "Uptime";
  if (key == "roles.current") return ru ? "Текущая роль" : "Current role";
  if (key == "roles.permissions") return ru ? "Права" : "Permissions";
  if (key == "roles.check") return ru ? "Проверка прав" : "Permission check";
  if (key == "roles.auth_state") return ru ? "Auth-состояние" : "Auth state";
  if (key == "roles.root_hash") return ru ? "Root bootstrap hash" : "Root bootstrap hash";
  if (key == "roles.user_creation") return ru ? "Создание пользователя" : "User creation";
  if (key == "roles.configured") return ru ? "настроен" : "configured";
  if (key == "roles.missing") return ru ? "отсутствует" : "missing";
  if (key == "roles.unlocked") return ru ? "root разблокирован" : "root unlocked";
  if (key == "roles.locked") return ru ? "bootstrap root заблокирован" : "bootstrap root locked";
  if (key == "list.header") return ru ? "Разрешенные приложения:" : "Allowed apps:";
  if (key == "auth.usage") return ru ? "Использование: suvos auth status | suvos auth root <bootstrap-secret>\n" : "Usage: suvos auth status | suvos auth root <bootstrap-secret>\n";
  if (key == "auth.secret_missing") return ru ? "suvosd auth: не указан bootstrap-secret\n" : "suvosd auth: missing bootstrap-secret\n";
  if (key == "auth.hash_missing") return ru ? "suvosd auth: root bootstrap hash не найден\n" : "suvosd auth: root bootstrap hash is missing\n";
  if (key == "auth.invalid") return ru ? "suvosd auth: неверный bootstrap-secret\n" : "suvosd auth: invalid bootstrap-secret\n";
  if (key == "auth.unlocked") return ru ? "root-доступ разблокирован для текущей runtime-сессии\n" : "root access unlocked for the current runtime session\n";
  if (key == "auth.unknown") return ru ? "suvosd auth: неизвестная auth-команда: " : "suvosd auth: unknown auth command: ";
  if (key == "run.missing") return ru ? "suvosd run: не указано имя приложения\n" : "suvosd run: missing app name\n";
  if (key == "run.invalid") return ru ? "suvosd run: некорректное имя приложения: " : "suvosd run: invalid app name: ";
  if (key == "run.not_found") return ru ? "suvosd run: приложение не найдено: " : "suvosd run: app not found: ";
  if (key == "run.exec_missing") return ru ? "suvosd run: исполняемый файл не найден: " : "suvosd run: executable not found: ";
  if (key == "run.pipe_failed") return ru ? "suvosd run: ошибка pipe\n" : "suvosd run: pipe failed\n";
  if (key == "run.fork_failed") return ru ? "suvosd run: ошибка fork\n" : "suvosd run: fork failed\n";
  if (key == "run.timeout") return ru ? "suvosd run: приложение превысило timeout\n" : "suvosd run: app timed out\n";
  if (key == "run.truncated") return ru ? "suvosd run: output обрезан\n" : "suvosd run: output truncated\n";
  if (key == "permission.denied") return ru ? "suvosd run: недостаточно прав: " : "suvosd run: permission denied: ";
  if (key == "unknown") return ru ? "suvosd: неизвестная команда: " : "suvosd: unknown command: ";
  if (key == "missing_command") return ru ? "suvosd: команда не указана\n" : "suvosd: missing command\n";
  if (key == "request.too_large") return ru ? "suvosd: request слишком большой\n" : "suvosd: request is too large\n";
  if (key == "request.too_many_parts") return ru ? "suvosd: слишком много аргументов request\n" : "suvosd: too many request arguments\n";
  if (key == "request.bad_chars") return ru ? "suvosd: request содержит управляющие символы\n" : "suvosd: request contains control characters\n";
  if (key == "too_many_workers") return ru ? "suvosd: слишком много активных request workers\n" : "suvosd: too many active request workers\n";
  if (key == "worker_fork_failed") return ru ? "suvosd: не удалось создать request worker\n" : "suvosd: failed to fork request worker\n";

  return key;
}

bool validName(const std::string &value) {
  if (value.empty()) {
    return false;
  }

  if (value.find('/') != std::string::npos || value.find("..") != std::string::npos) {
    return false;
  }

  for (char ch : value) {
    const bool allowed = (ch >= 'A' && ch <= 'Z') ||
                         (ch >= 'a' && ch <= 'z') ||
                         (ch >= '0' && ch <= '9') ||
                         ch == '_' || ch == '-' || ch == '.';
    if (!allowed) {
      return false;
    }
  }

  return true;
}

bool hasControlChar(const std::string &value) {
  for (char ch : value) {
    if (std::iscntrl(static_cast<unsigned char>(ch))) {
      return true;
    }
  }
  return false;
}

bool validCapability(const std::string &value) {
  if (value.empty() || value.size() > 128 || hasControlChar(value)) {
    return false;
  }

  for (char ch : value) {
    const bool allowed = (ch >= 'A' && ch <= 'Z') ||
                         (ch >= 'a' && ch <= 'z') ||
                         (ch >= '0' && ch <= '9') ||
                         ch == '_' || ch == '-' || ch == '.' || ch == ':' || ch == '*';
    if (!allowed) {
      return false;
    }
  }

  return true;
}

CommandResult validateRequestParts(const std::vector<std::string> &parts) {
  if (parts.empty()) {
    return {2, tr("missing_command")};
  }

  if (parts.size() > kMaxRequestParts) {
    return {2, tr("request.too_many_parts")};
  }

  for (const auto &part : parts) {
    if (part.size() > kMaxRequestPartBytes || hasControlChar(part)) {
      return {2, tr("request.bad_chars")};
    }
  }

  return {0, ""};
}

bool pathIsSocket(const std::string &path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISSOCK(st.st_mode);
}

bool dirExists(const std::string &path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool fileExistsExecutable(const std::string &path) {
  return access(path.c_str(), X_OK) == 0;
}

bool startsWithDir(const std::string &path, const std::string &root) {
  return path == root || path.rfind(root + "/", 0) == 0;
}

bool normalizeAllowedAppPath(const std::string &path, std::string *normalized) {
  if (path.empty() || path[0] != '/' || path.find("..") != std::string::npos) {
    return false;
  }

  char resolved[PATH_MAX];
  if (realpath(path.c_str(), resolved) == nullptr) {
    return false;
  }

  std::string resolvedPath(resolved);
  if (!startsWithDir(resolvedPath, "/system/suvos/apps") &&
      !startsWithDir(resolvedPath, "/system/suvos/bin")) {
    return false;
  }

  struct stat st {};
  if (stat(resolvedPath.c_str(), &st) != 0) {
    return false;
  }

  if (!S_ISREG(st.st_mode) || (st.st_mode & S_IXUSR) == 0) {
    return false;
  }

  if ((st.st_mode & S_IWGRP) != 0 || (st.st_mode & S_IWOTH) != 0) {
    return false;
  }

  *normalized = resolvedPath;
  return true;
}

void killProcessGroup(pid_t pid, int signal) {
  if (pid > 0) {
    kill(-pid, signal);
  }
}

void logConsole(const std::string &message) {
  int fd = open("/dev/console", O_WRONLY | O_APPEND);
  if (fd < 0) {
    return;
  }

  std::string line = "[suvosd] " + message + "\n";
  (void)write(fd, line.data(), line.size());
  close(fd);
}

bool ensureDir(const char *path, mode_t mode) {
  if (mkdir(path, mode) == 0) {
    chmod(path, mode);
    return true;
  }

  if (errno == EEXIST) {
    chmod(path, mode);
    return true;
  }

  return false;
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

bool writeFile(const std::string &path, const std::string &data, mode_t mode) {
  std::ofstream file(path);
  if (!file) {
    return false;
  }

  file << data;
  file.close();
  chmod(path.c_str(), mode);
  return true;
}

uint32_t rotateRight(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}

std::string sha256Hex(const std::string &input) {
  static constexpr uint32_t k[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
  };

  std::vector<uint8_t> data(input.begin(), input.end());
  const uint64_t bitLength = static_cast<uint64_t>(data.size()) * 8U;
  data.push_back(0x80U);
  while ((data.size() % 64U) != 56U) {
    data.push_back(0U);
  }
  for (int i = 7; i >= 0; --i) {
    data.push_back(static_cast<uint8_t>((bitLength >> (i * 8)) & 0xffU));
  }

  uint32_t h[8] = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
  };

  for (size_t offset = 0; offset < data.size(); offset += 64U) {
    uint32_t w[64] = {};
    for (size_t i = 0; i < 16; ++i) {
      const size_t j = offset + i * 4U;
      w[i] = (static_cast<uint32_t>(data[j]) << 24U) |
             (static_cast<uint32_t>(data[j + 1]) << 16U) |
             (static_cast<uint32_t>(data[j + 2]) << 8U) |
             static_cast<uint32_t>(data[j + 3]);
    }

    for (size_t i = 16; i < 64; ++i) {
      const uint32_t s0 = rotateRight(w[i - 15], 7U) ^ rotateRight(w[i - 15], 18U) ^ (w[i - 15] >> 3U);
      const uint32_t s1 = rotateRight(w[i - 2], 17U) ^ rotateRight(w[i - 2], 19U) ^ (w[i - 2] >> 10U);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];
    uint32_t e = h[4];
    uint32_t f = h[5];
    uint32_t g = h[6];
    uint32_t hh = h[7];

    for (size_t i = 0; i < 64; ++i) {
      const uint32_t s1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^ rotateRight(e, 25U);
      const uint32_t ch = (e & f) ^ ((~e) & g);
      const uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
      const uint32_t s0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^ rotateRight(a, 22U);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + maj;

      hh = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
  }

  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (uint32_t value : h) {
    out << std::setw(8) << value;
  }
  return out.str();
}

bool constantTimeEquals(const std::string &left, const std::string &right) {
  unsigned char diff = static_cast<unsigned char>(left.size() ^ right.size());
  const size_t maxSize = std::max(left.size(), right.size());

  for (size_t i = 0; i < maxSize; ++i) {
    const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0U;
    const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0U;
    diff |= static_cast<unsigned char>(a ^ b);
  }

  return diff == 0U;
}

RolePolicy loadRolePolicy() {
  RolePolicy policy;
  policy.roles.push_back({"setup", {"status.read", "role.read", "apps.list", "auth.status", "auth.root", "app.hello", "app.cpp-hello", "app.py-hello", "app.node-hello"}});
  policy.roles.push_back({"root", {"*"}});

  std::ifstream file(kRolesPath);
  if (!file) {
    return policy;
  }

  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const size_t separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }

    const std::string key = trim(line.substr(0, separator));
    const std::string value = trim(line.substr(separator + 1));

    if (key == "default_role" && validName(value)) {
      policy.defaultRole = value;
      continue;
    }
    if (key == "root_role" && validName(value)) {
      policy.rootRole = value;
      continue;
    }
    if (key == "root_hash_file" && !value.empty() && value[0] == '/') {
      policy.rootHashFile = value;
      continue;
    }
    if (key == "user_creation" && !value.empty()) {
      policy.userCreation = value;
      continue;
    }

    const std::string prefix = "role.";
    const std::string suffix = ".permissions";
    if (key.rfind(prefix, 0) == 0 && key.size() > prefix.size() + suffix.size() &&
        key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0) {
      const std::string roleName = key.substr(prefix.size(), key.size() - prefix.size() - suffix.size());
      if (!validName(roleName)) {
        continue;
      }

      auto permissions = splitList(value);
      if (permissions.empty()) {
        continue;
      }

      bool replaced = false;
      for (auto &role : policy.roles) {
        if (role.name == roleName) {
          role.permissions = permissions;
          replaced = true;
          break;
        }
      }

      if (!replaced) {
        policy.roles.push_back({roleName, permissions});
      }
    }
  }

  return policy;
}

std::vector<std::string> permissionsForRole(const RolePolicy &policy, const std::string &roleName) {
  for (const auto &role : policy.roles) {
    if (role.name == roleName) {
      return role.permissions;
    }
  }
  return {};
}

std::string joinPermissions(const std::vector<std::string> &permissions) {
  std::ostringstream out;
  for (size_t i = 0; i < permissions.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << permissions[i];
  }
  return out.str();
}

bool hasSuffix(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() &&
         value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string currentRole(const RolePolicy &policy) {
  std::string role = trim(readFile(kSessionRoleFile));
  if (validName(role)) {
    return role;
  }
  return policy.defaultRole;
}

std::string currentPermissions(const RolePolicy &policy) {
  auto permissions = permissionsForRole(policy, currentRole(policy));
  if (permissions.empty()) {
    return "";
  }
  return joinPermissions(permissions);
}

bool hasPermission(const RolePolicy &policy, const std::string &permission) {
  const auto permissions = permissionsForRole(policy, currentRole(policy));
  for (const auto &item : permissions) {
    if (item == "*" || item == permission) {
      return true;
    }
    if (hasSuffix(item, ".*")) {
      const std::string prefix = item.substr(0, item.size() - 1);
      if (permission.rfind(prefix, 0) == 0) {
        return true;
      }
    }
  }
  return false;
}

bool rootHashConfigured(const RolePolicy &policy) {
  return !trim(readFile(policy.rootHashFile)).empty();
}

bool rootSessionUnlocked(const RolePolicy &policy) {
  return currentRole(policy) == policy.rootRole && trim(readFile(kSessionAuthFile)) == policy.rootRole;
}

bool unlockRootSession(const RolePolicy &policy) {
  return writeFile(kSessionRoleFile, policy.rootRole + "\n", 0600) &&
         writeFile(kSessionAuthFile, policy.rootRole + "\n", 0600);
}

std::vector<std::string> listManifestFiles() {
  std::vector<std::string> files;
  DIR *dir = opendir(kManifestDir);
  if (dir == nullptr) {
    return files;
  }

  while (dirent *entry = readdir(dir)) {
    std::string name(entry->d_name);
    if (name.empty() || name[0] == '.') {
      continue;
    }
    if (!hasSuffix(name, ".app")) {
      continue;
    }
    files.push_back(std::string(kManifestDir) + "/" + name);
  }

  closedir(dir);
  std::sort(files.begin(), files.end());
  return files;
}

bool loadAppManifest(const std::string &path, App *app) {
  std::ifstream file(path);
  if (!file) {
    return false;
  }

  std::string descriptionEn;
  std::string descriptionRu;
  App parsed;
  parsed.runtime = "native";

  std::string line;
  while (std::getline(file, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }

    const size_t separator = line.find('=');
    if (separator == std::string::npos) {
      continue;
    }

    const std::string key = trim(line.substr(0, separator));
    const std::string value = trim(line.substr(separator + 1));

    if (key == "name") {
      parsed.name = value;
    } else if (key == "path") {
      parsed.path = value;
    } else if (key == "capability" || key == "permission") {
      parsed.permission = value;
    } else if (key == "runtime") {
      parsed.runtime = value.empty() ? "native" : value;
    } else if (key == "version") {
      parsed.version = value;
    } else if (key == "ui_entry") {
      parsed.uiEntry = value;
    } else if (key == "description.en") {
      descriptionEn = value;
    } else if (key == "description.ru") {
      descriptionRu = value;
    } else if (key == "description") {
      descriptionEn = value;
      descriptionRu = value;
    }
  }

  parsed.description = isRu() && !descriptionRu.empty() ? descriptionRu : descriptionEn;
  if (parsed.description.empty()) {
    parsed.description = descriptionRu;
  }

  if (!validName(parsed.name) || !validCapability(parsed.permission)) {
    return false;
  }

  if (!parsed.runtime.empty() && !validName(parsed.runtime)) {
    return false;
  }

  if (!parsed.uiEntry.empty() &&
      (parsed.uiEntry[0] == '/' || parsed.uiEntry.find("..") != std::string::npos ||
       hasControlChar(parsed.uiEntry))) {
    return false;
  }

  if (!normalizeAllowedAppPath(parsed.path, &parsed.path)) {
    logConsole("skipping invalid app manifest path for " + parsed.name);
    return false;
  }

  *app = parsed;
  return true;
}

std::vector<App> loadManifestRegistry() {
  std::vector<App> apps;
  for (const auto &manifestPath : listManifestFiles()) {
    App app;
    if (loadAppManifest(manifestPath, &app)) {
      apps.push_back(app);
    } else {
      logConsole("skipping invalid app manifest: " + manifestPath);
    }
  }
  return apps;
}

std::vector<App> loadLegacyRegistry() {
  std::vector<App> apps;
  std::ifstream file(kLegacyRegistryPath);

  if (!file) {
    return apps;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }

    auto fields = split(line, '\t');
    if (fields.size() < 3) {
      continue;
    }

    App app;
    app.name = fields[0];
    app.path = fields[1];
    app.permission = fields[2];
    if (fields.size() >= 5) {
      app.description = isRu() ? fields[4] : fields[3];
    } else {
      app.description = fields.size() >= 4 ? fields[3] : "";
    }

    if (!validName(app.name)) {
      continue;
    }

    if (!normalizeAllowedAppPath(app.path, &app.path)) {
      logConsole("skipping invalid app registry path for " + app.name);
      continue;
    }

    apps.push_back(app);
  }

  return apps;
}

std::vector<App> loadRegistry() {
  std::vector<App> apps = loadManifestRegistry();
  if (!apps.empty()) {
    return apps;
  }

  return loadLegacyRegistry();
}

const App *findApp(const std::vector<App> &apps, const std::string &name) {
  for (const auto &app : apps) {
    if (app.name == name) {
      return &app;
    }
  }

  return nullptr;
}

CommandResult runProgram(const std::string &path, const std::vector<std::string> &args) {
  if (!fileExistsExecutable(path)) {
    return {127, tr("run.exec_missing") + path + "\n"};
  }

  int pipefd[2];
  if (pipe(pipefd) != 0) {
    return {1, tr("run.pipe_failed")};
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return {1, tr("run.fork_failed")};
  }

  if (pid == 0) {
    setpgid(0, 0);
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    std::vector<std::string> storage;
    storage.push_back(path);
    for (const auto &arg : args) {
      storage.push_back(arg);
    }

    std::vector<char *> argv;
    for (auto &item : storage) {
      argv.push_back(const_cast<char *>(item.c_str()));
    }
    argv.push_back(nullptr);

    execv(path.c_str(), argv.data());
    std::string error = "suvosd run: exec failed: " + std::string(strerror(errno)) + "\n";
    (void)write(STDERR_FILENO, error.data(), error.size());
    _exit(127);
  }

  close(pipefd[1]);
  setpgid(pid, pid);
  fcntl(pipefd[0], F_SETFL, fcntl(pipefd[0], F_GETFL, 0) | O_NONBLOCK);

  std::string output;
  bool outputTruncated = false;
  char buffer[4096];
  int status = 0;
  bool exited = false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kAppTimeoutSeconds);

  while (true) {
    ssize_t n = 0;
    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
      if (output.size() < kMaxAppOutputBytes) {
        size_t remaining = kMaxAppOutputBytes - output.size();
        output.append(buffer, std::min(static_cast<size_t>(n), remaining));
        if (static_cast<size_t>(n) > remaining) {
          outputTruncated = true;
        }
      } else {
        outputTruncated = true;
      }
    }

    pid_t waitResult = waitpid(pid, &status, WNOHANG);
    if (waitResult == pid) {
      exited = true;
      killProcessGroup(pid, SIGKILL);
      while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        if (output.size() < kMaxAppOutputBytes) {
          size_t remaining = kMaxAppOutputBytes - output.size();
          output.append(buffer, std::min(static_cast<size_t>(n), remaining));
          if (static_cast<size_t>(n) > remaining) {
            outputTruncated = true;
          }
        } else {
          outputTruncated = true;
        }
      }
      break;
    }

    if (waitResult < 0 && errno == ECHILD) {
      status = 1;
      break;
    }

    if (std::chrono::steady_clock::now() >= deadline) {
      killProcessGroup(pid, SIGKILL);
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      output += tr("run.timeout");
      close(pipefd[0]);
      return {124, output};
    }

    usleep(10000);
  }

  close(pipefd[0]);

  if (outputTruncated) {
    output += tr("run.truncated");
  }

  if (exited && WIFEXITED(status)) {
    return {WEXITSTATUS(status), output};
  }

  if (exited && WIFSIGNALED(status)) {
    return {128 + WTERMSIG(status), output};
  }

  return {1, output};
}

CommandResult handleCommand(const std::vector<std::string> &parts) {
  if (parts.empty()) {
    return {2, tr("missing_command")};
  }

  RolePolicy policy = loadRolePolicy();
  const std::string &command = parts[0];

  if (command == "ping") {
    return {0, "pong\n"};
  }

  if (command == "status") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    std::ostringstream out;
    out << tr("status.booted") << "\n";
    out << tr("status.daemon") << "\n";
    out << tr("status.system_root") << ": " << kSystemRoot << "\n";
    out << tr("status.system_root_mode") << ": " << (systemRootReadOnly() ? tr("status.read_only") : tr("status.writable")) << "\n";
    out << tr("status.data_root") << ": " << kDataRoot << " (" << (dirExists(kDataRoot) ? tr("status.writable") : tr("status.missing")) << ")\n";
    out << tr("status.manifests") << ": " << kManifestDir << " (" << (dirExists(kManifestDir) ? tr("status.available") : tr("status.missing")) << ")\n";
    out << tr("status.api_socket") << ": " << kControlSocket << " (" << (pathIsSocket(kControlSocket) ? tr("status.available") : tr("status.missing")) << ")\n";
    out << tr("status.rootfs") << "\n";
    out << tr("status.kernel") << ": " << readFile("/proc/version");
    std::string uptime = readFile("/proc/uptime");
    if (!uptime.empty()) {
      auto values = split(uptime, ' ');
      out << tr("status.uptime") << ": " << values[0] << " seconds\n";
    }
    return {0, out.str()};
  }

  if (command == "roles" || command == "role") {
    if (!hasPermission(policy, "role.read")) {
      return {1, tr("permission.denied") + "role.read\n"};
    }

    std::ostringstream out;
    out << tr("roles.current") << ": " << currentRole(policy) << "\n";
    out << tr("roles.permissions") << ": " << currentPermissions(policy) << "\n";
    out << tr("roles.check") << ": " << (hasPermission(policy, "*") ? "allow" : "deny") << "\n";
    out << tr("roles.root_hash") << ": " << (rootHashConfigured(policy) ? tr("roles.configured") : tr("roles.missing")) << "\n";
    out << tr("roles.auth_state") << ": " << (rootSessionUnlocked(policy) ? tr("roles.unlocked") : tr("roles.locked")) << "\n";
    out << tr("roles.user_creation") << ": " << policy.userCreation << "\n";
    return {0, out.str()};
  }

  if (command == "whoami") {
    if (!hasPermission(policy, "role.read")) {
      return {1, tr("permission.denied") + "role.read\n"};
    }
    return {0, currentRole(policy) + "\n"};
  }

  if (command == "auth") {
    if (parts.size() < 2) {
      return {2, tr("auth.usage")};
    }

    const std::string &authCommand = parts[1];
    if (authCommand == "status") {
      if (!hasPermission(policy, "auth.status")) {
        return {1, tr("permission.denied") + "auth.status\n"};
      }

      std::ostringstream out;
      out << tr("roles.root_hash") << ": " << (rootHashConfigured(policy) ? tr("roles.configured") : tr("roles.missing")) << "\n";
      out << tr("roles.current") << ": " << currentRole(policy) << "\n";
      out << tr("roles.auth_state") << ": " << (rootSessionUnlocked(policy) ? tr("roles.unlocked") : tr("roles.locked")) << "\n";
      out << tr("roles.user_creation") << ": " << policy.userCreation << "\n";
      return {0, out.str()};
    }

    if (authCommand == "root") {
      if (!hasPermission(policy, "auth.root")) {
        return {1, tr("permission.denied") + "auth.root\n"};
      }
      if (parts.size() < 3) {
        return {2, tr("auth.secret_missing")};
      }

      const std::string expectedHash = trim(readFile(policy.rootHashFile));
      if (expectedHash.empty()) {
        return {1, tr("auth.hash_missing")};
      }

      const std::string actualHash = sha256Hex(parts[2]);
      if (!constantTimeEquals(actualHash, expectedHash)) {
        return {1, tr("auth.invalid")};
      }

      if (!unlockRootSession(policy)) {
        return {1, tr("permission.denied") + "session.write\n"};
      }

      return {0, tr("auth.unlocked")};
    }

    return {2, tr("auth.unknown") + authCommand + "\n"};
  }

  std::vector<App> apps = loadRegistry();

  if (command == "list") {
    if (!hasPermission(policy, "apps.list")) {
      return {1, tr("permission.denied") + "apps.list\n"};
    }

    std::ostringstream out;
    out << tr("list.header") << "\n";
    for (const auto &app : apps) {
      out << "  " << app.name;
      if (!app.description.empty()) {
        out << " - " << app.description;
      }
      out << "\n";
    }
    return {0, out.str()};
  }

  if (command == "run") {
    if (parts.size() < 2) {
      return {2, tr("run.missing")};
    }

    const std::string &name = parts[1];
    if (!validName(name)) {
      return {2, tr("run.invalid") + name + "\n"};
    }

    const App *app = findApp(apps, name);
    if (app == nullptr) {
      return {127, tr("run.not_found") + name + "\n"};
    }

    if (!hasPermission(policy, app->permission)) {
      return {1, tr("permission.denied") + app->permission + "\n"};
    }

    std::vector<std::string> args;
    for (size_t i = 2; i < parts.size(); ++i) {
      args.push_back(parts[i]);
    }

    return runProgram(app->path, args);
  }

  return {2, tr("unknown") + command + "\n"};
}

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

int openResponseFifo(const std::string &responsePath) {
  for (int attempt = 0; attempt < kResponseOpenRetries; ++attempt) {
    int fd = open(responsePath.c_str(), O_WRONLY | O_NONBLOCK);
    if (fd >= 0) {
      int flags = fcntl(fd, F_GETFL, 0);
      if (flags >= 0) {
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
      }
      return fd;
    }

    if (errno != ENXIO && errno != EINTR) {
      return -1;
    }

    usleep(kResponseOpenSleepMicros);
  }

  return -1;
}

void writeResponse(const std::string &responsePath, const CommandResult &result) {
  std::string output = result.output;
  if (!output.empty() && output.back() != '\n') {
    output += "\n";
  }
  output += std::string(kExitPrefix) + std::to_string(result.code) + "\n";

  int fd = openResponseFifo(responsePath);
  if (fd < 0) {
    logConsole("failed to open response fifo: " + std::string(strerror(errno)));
    return;
  }

  if (!writeAll(fd, output)) {
    logConsole("failed to write response fifo: " + std::string(strerror(errno)));
  }

  close(fd);
}

std::string encodeResponse(const CommandResult &result) {
  std::string output = result.output;
  if (!output.empty() && output.back() != '\n') {
    output += "\n";
  }
  output += std::string(kExitPrefix) + std::to_string(result.code) + "\n";
  return output;
}

CommandResult handleValidatedCommand(const std::vector<std::string> &parts) {
  CommandResult validation = validateRequestParts(parts);
  if (validation.code != 0) {
    return validation;
  }
  return handleCommand(parts);
}

bool readSocketLine(int fd, std::string *line) {
  line->clear();
  char ch = '\0';

  while (true) {
    ssize_t n = read(fd, &ch, 1);
    if (n == 0) {
      return !line->empty();
    }
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }

    if (ch == '\n') {
      return true;
    }

    if (line->size() >= kMaxRequestLineBytes) {
      return false;
    }

    line->push_back(ch);
  }
}

void handleSocketClient(int fd) {
  std::string line;
  CommandResult result;

  if (!readSocketLine(fd, &line)) {
    result = {2, tr("request.too_large")};
  } else {
    auto parts = split(line, '\t');
    result = handleValidatedCommand(parts);
  }

  const std::string response = encodeResponse(result);
  (void)writeAll(fd, response);
}

void reapSocketClients() {
  while (waitpid(-1, nullptr, WNOHANG) > 0) {
    if (gActiveSocketWorkers > 0) {
      --gActiveSocketWorkers;
    }
  }
}

void runSocketServer() {
  signal(SIGCHLD, SIG_DFL);

  int server = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server < 0) {
    logConsole("failed to create control socket: " + std::string(strerror(errno)));
    _exit(1);
  }

  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  if (std::strlen(kControlSocket) >= sizeof(addr.sun_path)) {
    logConsole("control socket path is too long");
    close(server);
    _exit(1);
  }
  std::strncpy(addr.sun_path, kControlSocket, sizeof(addr.sun_path) - 1);

  unlink(kControlSocket);
  if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    logConsole("failed to bind control socket: " + std::string(strerror(errno)));
    close(server);
    _exit(1);
  }

  chmod(kControlSocket, 0600);

  if (listen(server, 16) != 0) {
    logConsole("failed to listen on control socket: " + std::string(strerror(errno)));
    close(server);
    _exit(1);
  }

  while (true) {
    reapSocketClients();
    int client = accept(server, nullptr, nullptr);
    if (client < 0) {
      if (errno == EINTR) {
        continue;
      }
      logConsole("failed to accept control socket client: " + std::string(strerror(errno)));
      sleep(1);
      continue;
    }

    reapSocketClients();
    if (gActiveSocketWorkers >= kMaxRequestWorkers) {
      const std::string response = encodeResponse({1, tr("too_many_workers")});
      (void)writeAll(client, response);
      close(client);
      continue;
    }

    pid_t worker = fork();
    if (worker < 0) {
      logConsole("failed to fork control socket worker: " + std::string(strerror(errno)));
      close(client);
      continue;
    }

    if (worker == 0) {
      close(server);
      signal(SIGCHLD, SIG_DFL);
      handleSocketClient(client);
      close(client);
      _exit(0);
    }

    ++gActiveSocketWorkers;
    close(client);
  }
}

void startSocketServer() {
  pid_t pid = fork();
  if (pid < 0) {
    logConsole("failed to fork control socket server: " + std::string(strerror(errno)));
    return;
  }

  if (pid == 0) {
    runSocketServer();
    _exit(0);
  }
}

void handleRequestLine(const std::string &line) {
  auto fields = split(line, '\t');
  if (fields.size() < 2) {
    logConsole("invalid request line");
    return;
  }

  const std::string requestId = fields[0];
  if (!validName(requestId)) {
    logConsole("invalid request id: " + requestId);
    return;
  }

  std::string responsePath = std::string(kResponsesDir) + "/" + requestId;
  struct stat responseStat {};
  if (stat(responsePath.c_str(), &responseStat) != 0 || !S_ISFIFO(responseStat.st_mode)) {
    logConsole("response fifo is missing: " + responsePath);
    return;
  }

  if (line.size() > kMaxRequestLineBytes) {
    writeResponse(responsePath, {2, tr("request.too_large")});
    return;
  }

  sigset_t blockSet;
  sigset_t oldSet;
  sigemptyset(&blockSet);
  sigaddset(&blockSet, SIGCHLD);
  sigprocmask(SIG_BLOCK, &blockSet, &oldSet);

  if (gActiveRequestWorkers >= kMaxRequestWorkers) {
    sigprocmask(SIG_SETMASK, &oldSet, nullptr);
    writeResponse(responsePath, {1, tr("too_many_workers")});
    return;
  }

  pid_t worker = fork();
  if (worker < 0) {
    sigprocmask(SIG_SETMASK, &oldSet, nullptr);
    writeResponse(responsePath, {1, tr("worker_fork_failed")});
    return;
  }

  if (worker > 0) {
    ++gActiveRequestWorkers;
    sigprocmask(SIG_SETMASK, &oldSet, nullptr);
    return;
  }

  sigprocmask(SIG_SETMASK, &oldSet, nullptr);
  signal(SIGCHLD, SIG_DFL);
  std::vector<std::string> commandParts(fields.begin() + 1, fields.end());
  CommandResult result = handleValidatedCommand(commandParts);
  writeResponse(responsePath, result);
  _exit(0);
}

void writePidFile() {
  std::ofstream file(kPidFile);
  file << getpid() << "\n";
}

void prepareRuntimeDir() {
  unlink(kRequestFifo);
  unlink(kControlSocket);
  unlink(kPidFile);
  unlink(kSessionRoleFile);
  unlink(kSessionAuthFile);
  ensureDir("/run", 0755);
  ensureDir(kRunDir, 0700);
  ensureDir(kResponsesDir, 0700);

  if (mkfifo(kRequestFifo, 0600) != 0 && errno != EEXIST) {
    std::cerr << "suvosd: mkfifo failed: " << strerror(errno) << "\n";
    _exit(1);
  }

  chmod(kRequestFifo, 0600);
  RolePolicy policy = loadRolePolicy();
  writeFile(kSessionRoleFile, policy.defaultRole + "\n", 0600);
  startSocketServer();
  writePidFile();
}

void runDaemon() {
  prepareRuntimeDir();
  logConsole("started pid=" + std::to_string(getpid()) + " mode=c++");

  while (true) {
    std::ifstream request(kRequestFifo);
    if (!request) {
      logConsole("failed to open request fifo");
      sleep(1);
      continue;
    }

    std::string line;
    while (std::getline(request, line)) {
      handleRequestLine(line);
    }
  }
}

void reapWorkers(int) {
  while (waitpid(-1, nullptr, WNOHANG) > 0) {
    if (gActiveRequestWorkers > 0) {
      --gActiveRequestWorkers;
    }
  }
}

} // namespace

int main() {
  signal(SIGPIPE, SIG_IGN);
  struct sigaction sa {};
  sa.sa_handler = reapWorkers;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
  sigaction(SIGCHLD, &sa, nullptr);
  runDaemon();
  return 0;
}
