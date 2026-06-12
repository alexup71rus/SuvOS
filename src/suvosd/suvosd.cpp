#include <algorithm>
#include <csignal>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <map>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/reboot.h>
#include <sys/time.h>
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
constexpr const char *kNetworkStateDir = "/data/suvos/network";
constexpr const char *kNetworkConfigPath = "/data/suvos/network/network.conf";
constexpr const char *kWifiConfigPath = "/data/suvos/network/wifi.conf";
constexpr const char *kWifiSupplicantConfigPath = "/data/suvos/network/wpa_supplicant.conf";
constexpr const char *kWifiSupplicantPidPath = "/run/suvosd/wpa_supplicant.pid";
constexpr const char *kTimeStateDir = "/data/suvos/time";
constexpr const char *kTimeConfigPath = "/data/suvos/time/time.conf";
constexpr const char *kNotificationsDir = "/data/suvos/notifications";
constexpr const char *kNotificationsPath = "/data/suvos/notifications/notifications.tsv";
constexpr const char *kCalendarDir = "/data/suvos/calendar";
constexpr const char *kCalendarEventsPath = "/data/suvos/calendar/events.tsv";
constexpr const char *kStateLockPath = "/data/suvos/state/suvosd-state.lock";
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
          const char *hex = "0123456789abcdef";
          out << "\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
        } else {
          out << static_cast<char>(ch);
        }
        break;
    }
  }
  return out.str();
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
  ssize_t ignored = write(fd, line.data(), line.size());
  (void)ignored;
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

class ScopedFileLock {
 public:
  explicit ScopedFileLock(const std::string &path) {
    ensureDir(kDataRoot, 0755);
    const std::string stateDir = std::string(kDataRoot) + "/state";
    ensureDir(stateDir.c_str(), 0700);
    fd_ = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd_ >= 0 && flock(fd_, LOCK_EX) == 0) {
      locked_ = true;
    }
  }

  ~ScopedFileLock() {
    if (fd_ >= 0) {
      if (locked_) {
        flock(fd_, LOCK_UN);
      }
      close(fd_);
    }
  }

  bool locked() const {
    return locked_;
  }

 private:
  int fd_ = -1;
  bool locked_ = false;
};

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
  policy.roles.push_back({"setup", {"status.read", "role.read", "apps.list", "auth.status", "auth.root"}});
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

std::string sessionRole(const RolePolicy &policy) {
  std::string role = trim(readFile(kSessionRoleFile));
  if (validName(role)) {
    return role;
  }
  return policy.defaultRole;
}

bool rootSessionMarkerValid(const RolePolicy &policy) {
  return trim(readFile(kSessionAuthFile)) == policy.rootRole;
}

std::string currentRole(const RolePolicy &policy) {
  const std::string role = sessionRole(policy);
  if (role == policy.rootRole && !rootSessionMarkerValid(policy)) {
    return policy.defaultRole;
  }
  return role;
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
  return currentRole(policy) == policy.rootRole;
}

bool unlockRootSession(const RolePolicy &policy) {
  if (!writeFile(kSessionAuthFile, policy.rootRole + "\n", 0600)) {
    return false;
  }

  if (writeFile(kSessionRoleFile, policy.rootRole + "\n", 0600)) {
    return true;
  }

  unlink(kSessionAuthFile);
  (void)writeFile(kSessionRoleFile, policy.defaultRole + "\n", 0600);
  return false;
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

std::string statusJson() {
  std::string uptime = readFile("/proc/uptime");
  std::string uptimeSeconds = "0";
  if (!uptime.empty()) {
    auto values = split(uptime, ' ');
    if (!values.empty() && !values[0].empty()) {
      uptimeSeconds = values[0];
    }
  }

  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"status\":\"booted\",";
  out << "\"daemon\":\"running\",";
  out << "\"language\":\"" << jsonEscape(lang()) << "\",";
  out << "\"systemRoot\":\"" << jsonEscape(kSystemRoot) << "\",";
  out << "\"systemRootReadOnly\":" << (systemRootReadOnly() ? "true" : "false") << ",";
  out << "\"dataRoot\":\"" << jsonEscape(kDataRoot) << "\",";
  out << "\"dataRootAvailable\":" << (dirExists(kDataRoot) ? "true" : "false") << ",";
  out << "\"manifestDir\":\"" << jsonEscape(kManifestDir) << "\",";
  out << "\"manifestDirAvailable\":" << (dirExists(kManifestDir) ? "true" : "false") << ",";
  out << "\"apiSocket\":\"" << jsonEscape(kControlSocket) << "\",";
  out << "\"apiSocketAvailable\":" << (pathIsSocket(kControlSocket) ? "true" : "false") << ",";
  out << "\"rootfs\":\"initramfs\",";
  out << "\"kernel\":\"" << jsonEscape(trim(readFile("/proc/version"))) << "\",";
  out << "\"uptimeSeconds\":" << uptimeSeconds;
  out << "}\n";
  return out.str();
}

bool ensureTimeStateDir() {
  ensureDir(kDataRoot, 0755);
  return ensureDir(kTimeStateDir, 0700);
}

bool validTimezoneName(const std::string &value) {
  if (value.empty() || value.size() > 96 || value.find("..") != std::string::npos ||
      hasControlChar(value)) {
    return false;
  }
  for (char ch : value) {
    const bool allowed = (ch >= 'A' && ch <= 'Z') ||
                         (ch >= 'a' && ch <= 'z') ||
                         (ch >= '0' && ch <= '9') ||
                         ch == '_' || ch == '-' || ch == '+' || ch == '/' || ch == ':';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

std::string configuredTimezone() {
  std::istringstream lines(readFile(kTimeConfigPath));
  std::string line;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.rfind("timezone=", 0) != 0) {
      continue;
    }
    const std::string value = line.substr(std::strlen("timezone="));
    return validTimezoneName(value) ? value : "";
  }
  return "";
}

bool applyTimezone(const std::string &timezone) {
  if (!validTimezoneName(timezone)) {
    return false;
  }
  setenv("TZ", timezone.c_str(), 1);
  tzset();
  return writeFile("/etc/TZ", timezone + "\n", 0644);
}

bool applySavedTimezone() {
  const std::string timezone = configuredTimezone();
  if (timezone.empty()) {
    return true;
  }
  return applyTimezone(timezone);
}

std::string timeJson() {
  applySavedTimezone();
  std::time_t now = std::time(nullptr);
  std::tm localTm {};
  localtime_r(&now, &localTm);

  char isoBuf[32] = {0};
  std::strftime(isoBuf, sizeof(isoBuf), "%Y-%m-%dT%H:%M:%S", &localTm);
  char hhmmBuf[8] = {0};
  std::strftime(hhmmBuf, sizeof(hhmmBuf), "%H:%M", &localTm);
  char tzBuf[8] = {0};
  std::strftime(tzBuf, sizeof(tzBuf), "%z", &localTm);

  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"epoch\":" << static_cast<long long>(now) << ",";
  out << "\"iso\":\"" << isoBuf << tzBuf << "\",";
  out << "\"time\":\"" << hhmmBuf << "\",";
  out << "\"timezone\":\"" << tzBuf << "\",";
  out << "\"configuredTimezone\":\"" << jsonEscape(configuredTimezone()) << "\"";
  out << "}\n";
  return out.str();
}

std::string rolesJson(const RolePolicy &policy) {
  auto permissions = permissionsForRole(policy, currentRole(policy));
  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"currentRole\":\"" << jsonEscape(currentRole(policy)) << "\",";
  out << "\"defaultRole\":\"" << jsonEscape(policy.defaultRole) << "\",";
  out << "\"rootRole\":\"" << jsonEscape(policy.rootRole) << "\",";
  out << "\"permissions\":[";
  for (size_t i = 0; i < permissions.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << "\"" << jsonEscape(permissions[i]) << "\"";
  }
  out << "],";
  out << "\"hasRootAccess\":" << (hasPermission(policy, "*") ? "true" : "false") << ",";
  out << "\"rootHashConfigured\":" << (rootHashConfigured(policy) ? "true" : "false") << ",";
  out << "\"rootSessionUnlocked\":" << (rootSessionUnlocked(policy) ? "true" : "false") << ",";
  out << "\"userCreation\":\"" << jsonEscape(policy.userCreation) << "\"";
  out << "}\n";
  return out.str();
}

std::string appsJson(const std::vector<App> &apps) {
  std::ostringstream out;
  out << "{\"ok\":true,\"apps\":[";
  for (size_t i = 0; i < apps.size(); ++i) {
    const auto &app = apps[i];
    if (i > 0) {
      out << ",";
    }
    out << "{";
    out << "\"name\":\"" << jsonEscape(app.name) << "\",";
    out << "\"description\":\"" << jsonEscape(app.description) << "\",";
    out << "\"capability\":\"" << jsonEscape(app.permission) << "\",";
    out << "\"runtime\":\"" << jsonEscape(app.runtime.empty() ? "native" : app.runtime) << "\",";
    out << "\"version\":\"" << jsonEscape(app.version) << "\",";
    out << "\"uiEntry\":\"" << jsonEscape(app.uiEntry) << "\"";
    out << "}";
  }
  out << "]}\n";
  return out.str();
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
    ssize_t ignored = write(STDERR_FILENO, error.data(), error.size());
    (void)ignored;
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

std::vector<std::string> listDirNames(const std::string &path) {
  std::vector<std::string> names;
  DIR *dir = opendir(path.c_str());
  if (dir == nullptr) {
    return names;
  }

  while (dirent *entry = readdir(dir)) {
    std::string name(entry->d_name);
    if (name.empty() || name == "." || name == "..") {
      continue;
    }
    names.push_back(name);
  }

  closedir(dir);
  std::sort(names.begin(), names.end());
  return names;
}

std::string readTrimmed(const std::string &path) {
  return trim(readFile(path));
}

bool writePlainFile(const std::string &path, const std::string &value) {
  std::ofstream file(path);
  if (!file) {
    return false;
  }
  file << value;
  return static_cast<bool>(file);
}

bool parseLongLong(const std::string &value, long long *out) {
  if (value.empty()) {
    return false;
  }

  char *end = nullptr;
  errno = 0;
  long long parsed = std::strtoll(value.c_str(), &end, 10);
  if (errno != 0 || end == value.c_str() || *end != '\0') {
    return false;
  }

  *out = parsed;
  return true;
}

bool parsePercent(const std::string &value, int *out) {
  long long parsed = 0;
  if (!parseLongLong(value, &parsed) || parsed < 0 || parsed > 100) {
    return false;
  }
  *out = static_cast<int>(parsed);
  return true;
}

std::string jsonBool(bool value) {
  return value ? "true" : "false";
}

std::string findExecutable(const std::vector<std::string> &candidates) {
  for (const auto &candidate : candidates) {
    if (fileExistsExecutable(candidate)) {
      return candidate;
    }
  }
  return "";
}

bool validDeviceName(const std::string &value) {
  if (value.empty() || value.size() > 64 || value.find("..") != std::string::npos ||
      value.find('/') != std::string::npos || hasControlChar(value)) {
    return false;
  }

  for (char ch : value) {
    const bool allowed = (ch >= 'A' && ch <= 'Z') ||
                         (ch >= 'a' && ch <= 'z') ||
                         (ch >= '0' && ch <= '9') ||
                         ch == '_' || ch == '-' || ch == '.' || ch == ':';
    if (!allowed) {
      return false;
    }
  }

  return true;
}

bool validConfigValue(const std::string &value, size_t maxSize) {
  if (value.size() > maxSize || hasControlChar(value)) {
    return false;
  }
  return value.find('\t') == std::string::npos;
}

std::map<std::string, std::string> parseKeyValueFile(const std::string &path) {
  std::map<std::string, std::string> values;
  std::ifstream file(path);
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
    values[trim(line.substr(0, separator))] = trim(line.substr(separator + 1));
  }
  return values;
}

std::map<std::string, std::string> parseOptions(const std::vector<std::string> &parts,
                                                size_t begin) {
  std::map<std::string, std::string> values;
  for (size_t i = begin; i < parts.size(); ++i) {
    const size_t separator = parts[i].find('=');
    if (separator == std::string::npos || separator == 0) {
      values[""] = parts[i];
      continue;
    }
    values[parts[i].substr(0, separator)] = parts[i].substr(separator + 1);
  }
  return values;
}

std::string optionValue(const std::map<std::string, std::string> &options,
                        const std::string &key) {
  auto item = options.find(key);
  return item == options.end() ? "" : item->second;
}

bool optionPresent(const std::map<std::string, std::string> &options,
                   const std::string &key) {
  return options.find(key) != options.end();
}

bool ensureNetworkStateDir() {
  ensureDir(kDataRoot, 0755);
  return ensureDir(kNetworkStateDir, 0700);
}

std::string networkConfigJson() {
  auto config = parseKeyValueFile(kNetworkConfigPath);
  const bool configured = !config.empty();
  const std::string mode = optionValue(config, "mode").empty() ? "dhcp" : optionValue(config, "mode");

  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"configured\":" << jsonBool(configured) << ",";
  out << "\"mode\":\"" << jsonEscape(mode) << "\",";
  out << "\"interface\":\"" << jsonEscape(optionValue(config, "interface")) << "\",";
  out << "\"address\":\"" << jsonEscape(optionValue(config, "address")) << "\",";
  out << "\"netmask\":\"" << jsonEscape(optionValue(config, "netmask")) << "\",";
  out << "\"gateway\":\"" << jsonEscape(optionValue(config, "gateway")) << "\",";
  out << "\"dns\":\"" << jsonEscape(optionValue(config, "dns")) << "\"";
  out << "}\n";
  return out.str();
}

std::vector<std::string> networkInterfaces(bool includeLoopback) {
  std::vector<std::string> names;
  for (const auto &name : listDirNames("/sys/class/net")) {
    if (!includeLoopback && name == "lo") {
      continue;
    }
    if (validDeviceName(name)) {
      names.push_back(name);
    }
  }
  return names;
}

std::string firstNetworkInterface() {
  const auto names = networkInterfaces(false);
  return names.empty() ? "" : names[0];
}

bool writeResolvConf(const std::string &dns) {
  if (dns.empty()) {
    return true;
  }

  std::ostringstream out;
  for (const auto &server : splitList(dns)) {
    if (!validConfigValue(server, 128)) {
      return false;
    }
    out << "nameserver " << server << "\n";
  }
  return writeFile("/etc/resolv.conf", out.str(), 0644);
}

CommandResult applyDhcpNetwork(const std::string &iface) {
  const std::string ifconfig = findExecutable({
      "/bin/ifconfig", "/sbin/ifconfig", "/usr/bin/ifconfig", "/usr/sbin/ifconfig"});
  if (ifconfig.empty()) {
    return {1, "{\"ok\":false,\"available\":true,\"applied\":false,\"error\":\"ifconfig_missing\"}\n"};
  }

  CommandResult up = runProgram(ifconfig, {iface, "up"});
  if (up.code != 0) {
    std::ostringstream out;
    out << "{\"ok\":false,\"available\":true,\"applied\":false,\"interface\":\""
        << jsonEscape(iface) << "\",\"error\":\"interface_up_failed\"}\n";
    return {1, out.str()};
  }

  const std::string udhcpc = findExecutable({
      "/bin/udhcpc", "/sbin/udhcpc", "/usr/bin/udhcpc", "/usr/sbin/udhcpc"});
  if (udhcpc.empty()) {
    std::ostringstream out;
    out << "{\"ok\":false,\"available\":true,\"applied\":false,\"interface\":\""
        << jsonEscape(iface) << "\",\"error\":\"dhcp_client_missing\"}\n";
    return {1, out.str()};
  }

  CommandResult dhcp = runProgram(
      udhcpc, {"-i", iface, "-q", "-n", "-t", "3", "-T", "3", "-s",
               std::string(kSystemRoot) + "/bin/suvos-udhcpc-script"});
  const bool success = dhcp.code == 0;
  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"available\":true,\"configured\":true,";
  out << "\"mode\":\"dhcp\",";
  out << "\"interface\":\"" << jsonEscape(iface) << "\",";
  out << "\"applied\":" << jsonBool(success);
  if (!success) {
    out << ",\"error\":\"dhcp_failed\",";
    out << "\"details\":\"" << jsonEscape(trim(dhcp.output)) << "\"";
  }
  out << "}\n";
  return {success ? 0 : 1, out.str()};
}

CommandResult applyStaticNetwork(const std::string &iface,
                                 const std::string &address,
                                 const std::string &netmask,
                                 const std::string &gateway,
                                 const std::string &dns) {
  const std::string ifconfig = findExecutable({
      "/bin/ifconfig", "/sbin/ifconfig", "/usr/bin/ifconfig", "/usr/sbin/ifconfig"});
  if (ifconfig.empty()) {
    return {1, "{\"ok\":false,\"available\":true,\"applied\":false,\"error\":\"ifconfig_missing\"}\n"};
  }

  std::vector<std::string> args = {iface, address};
  if (!netmask.empty()) {
    args.push_back("netmask");
    args.push_back(netmask);
  }
  args.push_back("up");
  CommandResult setAddress = runProgram(ifconfig, args);
  if (setAddress.code != 0) {
    std::ostringstream out;
    out << "{\"ok\":false,\"available\":true,\"applied\":false,\"interface\":\""
        << jsonEscape(iface) << "\",\"mode\":\"static\",\"error\":\"static_address_failed\",";
    out << "\"details\":\"" << jsonEscape(trim(setAddress.output)) << "\"}\n";
    return {1, out.str()};
  }

  if (!gateway.empty()) {
    const std::string route = findExecutable({
        "/bin/route", "/sbin/route", "/usr/bin/route", "/usr/sbin/route"});
    if (route.empty()) {
      return {1, "{\"ok\":false,\"available\":true,\"applied\":false,\"mode\":\"static\",\"error\":\"route_missing\"}\n"};
    }
    (void)runProgram(route, {"del", "default", "dev", iface});
    CommandResult routeAdd = runProgram(route, {"add", "default", "gw", gateway, "dev", iface});
    if (routeAdd.code != 0) {
      std::ostringstream out;
      out << "{\"ok\":false,\"available\":true,\"applied\":false,\"interface\":\""
          << jsonEscape(iface) << "\",\"mode\":\"static\",\"error\":\"default_route_failed\",";
      out << "\"details\":\"" << jsonEscape(trim(routeAdd.output)) << "\"}\n";
      return {1, out.str()};
    }
  }

  if (!writeResolvConf(dns)) {
    return {1, "{\"ok\":false,\"available\":true,\"applied\":false,\"mode\":\"static\",\"error\":\"dns_write_failed\"}\n"};
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"available\":true,\"configured\":true,\"mode\":\"static\",";
  out << "\"interface\":\"" << jsonEscape(iface) << "\",";
  out << "\"address\":\"" << jsonEscape(address) << "\",";
  out << "\"netmask\":\"" << jsonEscape(netmask) << "\",";
  out << "\"gateway\":\"" << jsonEscape(gateway) << "\",";
  out << "\"applied\":true}\n";
  return {0, out.str()};
}

CommandResult applyNetworkConfig(const std::map<std::string, std::string> &config) {
  const std::string mode = optionValue(config, "mode").empty() ? "dhcp" : optionValue(config, "mode");
  std::string iface = optionValue(config, "interface");
  const std::string address = optionValue(config, "address");
  const std::string netmask = optionValue(config, "netmask");
  const std::string gateway = optionValue(config, "gateway");
  const std::string dns = optionValue(config, "dns");

  if (iface.empty()) {
    iface = firstNetworkInterface();
  }
  if (iface.empty()) {
    std::ostringstream out;
    out << "{\"ok\":true,\"available\":false,\"configured\":true,\"mode\":\""
        << jsonEscape(mode) << "\",\"applied\":false,\"reason\":\"no_network_interface\"}\n";
    return {0, out.str()};
  }
  if (!validDeviceName(iface) || !dirExists("/sys/class/net/" + iface) || iface == "lo") {
    return {2, "{\"ok\":false,\"error\":\"invalid_interface\"}\n"};
  }

  if (mode == "dhcp") {
    return applyDhcpNetwork(iface);
  }
  return applyStaticNetwork(iface, address, netmask, gateway, dns);
}

CommandResult networkConfigure(const std::map<std::string, std::string> &options) {
  if (options.count("") > 0) {
    return {2, "{\"ok\":false,\"error\":\"invalid_option\"}\n"};
  }

  const std::string mode = optionValue(options, "mode").empty() ? "dhcp" : optionValue(options, "mode");
  const std::string iface = optionValue(options, "interface");
  const std::string address = optionValue(options, "address");
  const std::string netmask = optionValue(options, "netmask");
  const std::string gateway = optionValue(options, "gateway");
  const std::string dns = optionValue(options, "dns");

  if (mode != "dhcp" && mode != "static") {
    return {2, "{\"ok\":false,\"error\":\"invalid_network_mode\"}\n"};
  }
  if (!iface.empty() && (!validDeviceName(iface) || !dirExists("/sys/class/net/" + iface))) {
    return {2, "{\"ok\":false,\"error\":\"invalid_interface\"}\n"};
  }
  if (!validConfigValue(address, 128) ||
      !validConfigValue(netmask, 128) ||
      !validConfigValue(gateway, 128) ||
      !validConfigValue(dns, 256)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_network_config\"}\n"};
  }
  if (mode == "static" && address.empty()) {
    return {2, "{\"ok\":false,\"error\":\"missing_static_address\"}\n"};
  }

  if (!ensureNetworkStateDir()) {
    return {1, "{\"ok\":false,\"error\":\"network_state_unavailable\"}\n"};
  }

  std::ostringstream data;
  data << "mode=" << mode << "\n";
  data << "interface=" << iface << "\n";
  data << "address=" << address << "\n";
  data << "netmask=" << netmask << "\n";
  data << "gateway=" << gateway << "\n";
  data << "dns=" << dns << "\n";
  if (!writeFile(kNetworkConfigPath, data.str(), 0600)) {
    return {1, "{\"ok\":false,\"error\":\"network_config_write_failed\"}\n"};
  }

  return applyNetworkConfig(parseKeyValueFile(kNetworkConfigPath));
}

bool hasDefaultRoute() {
  std::ifstream routes("/proc/net/route");
  std::string line;
  std::getline(routes, line);
  while (std::getline(routes, line)) {
    auto fields = split(line, '\t');
    if (fields.size() >= 4 && fields[1] == "00000000") {
      return true;
    }
  }
  return false;
}

std::string networkJson() {
  const bool available = dirExists("/sys/class/net");
  const auto names = networkInterfaces(true);
  bool anyNonLoopback = false;
  bool anyEnabled = false;

  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"available\":" << jsonBool(available) << ",";
  out << "\"online\":" << jsonBool(hasDefaultRoute()) << ",";
  out << "\"interfaces\":[";
  for (size_t i = 0; i < names.size(); ++i) {
    const std::string base = "/sys/class/net/" + names[i];
    const bool loopback = names[i] == "lo";
    const bool wireless = dirExists(base + "/wireless");
    const std::string operstate = readTrimmed(base + "/operstate");
    const std::string carrier = readTrimmed(base + "/carrier");
    if (!loopback) {
      anyNonLoopback = true;
      if (operstate != "down") {
        anyEnabled = true;
      }
    }

    if (i > 0) {
      out << ",";
    }
    out << "{";
    out << "\"name\":\"" << jsonEscape(names[i]) << "\",";
    out << "\"loopback\":" << jsonBool(loopback) << ",";
    out << "\"wireless\":" << jsonBool(wireless) << ",";
    out << "\"enabled\":" << jsonBool(operstate != "down") << ",";
    out << "\"operstate\":\"" << jsonEscape(operstate.empty() ? "unknown" : operstate) << "\",";
    if (carrier.empty()) {
      out << "\"carrier\":null";
    } else {
      out << "\"carrier\":" << (carrier == "1" ? "true" : "false");
    }
    out << "}";
  }
  out << "],";
  out << "\"enabled\":" << jsonBool(anyEnabled) << ",";
  out << "\"manageable\":" << jsonBool(anyNonLoopback);
  out << "}\n";
  return out.str();
}

CommandResult networkAction(const std::string &action, const std::string &requestedInterface) {
  if (action != "enable" && action != "disable") {
    return {2, "{\"ok\":false,\"error\":\"unknown_network_action\"}\n"};
  }

  const std::string ifconfig = findExecutable({
      "/bin/ifconfig", "/sbin/ifconfig", "/usr/bin/ifconfig", "/usr/sbin/ifconfig"});
  if (ifconfig.empty()) {
    return {0, "{\"ok\":true,\"available\":false,\"reason\":\"ifconfig_missing\"}\n"};
  }

  std::vector<std::string> targets;
  if (!requestedInterface.empty()) {
    if (!validDeviceName(requestedInterface) ||
        !dirExists("/sys/class/net/" + requestedInterface) ||
        requestedInterface == "lo") {
      return {2, "{\"ok\":false,\"error\":\"invalid_interface\"}\n"};
    }
    targets.push_back(requestedInterface);
  } else {
    targets = networkInterfaces(false);
  }

  if (targets.empty()) {
    return {0, "{\"ok\":true,\"available\":false,\"reason\":\"no_network_interface\"}\n"};
  }

  const std::string state = action == "enable" ? "up" : "down";
  bool success = true;
  std::ostringstream details;
  for (size_t i = 0; i < targets.size(); ++i) {
    CommandResult result = runProgram(ifconfig, {targets[i], state});
    if (result.code != 0) {
      success = false;
    }
    if (i > 0) {
      details << "; ";
    }
    details << targets[i] << "=" << result.code;
  }

  std::ostringstream out;
  out << "{";
  out << "\"ok\":" << jsonBool(success) << ",";
  out << "\"available\":true,";
  out << "\"action\":\"" << action << "\",";
  out << "\"details\":\"" << jsonEscape(details.str()) << "\"";
  out << "}\n";
  return {success ? 0 : 1, out.str()};
}

std::vector<std::string> wirelessInterfaces() {
  std::vector<std::string> names;
  for (const auto &name : networkInterfaces(false)) {
    if (dirExists("/sys/class/net/" + name + "/wireless")) {
      names.push_back(name);
    }
  }
  return names;
}

std::string wifiJson() {
  const auto names = wirelessInterfaces();
  bool anyEnabled = false;
  auto config = parseKeyValueFile(kWifiConfigPath);

  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"available\":" << jsonBool(!names.empty()) << ",";
  out << "\"configured\":" << jsonBool(!config.empty()) << ",";
  out << "\"configuredSsid\":\"" << jsonEscape(optionValue(config, "ssid")) << "\",";
  out << "\"interfaces\":[";
  for (size_t i = 0; i < names.size(); ++i) {
    const std::string operstate = readTrimmed("/sys/class/net/" + names[i] + "/operstate");
    if (operstate != "down") {
      anyEnabled = true;
    }
    if (i > 0) {
      out << ",";
    }
    out << "{";
    out << "\"name\":\"" << jsonEscape(names[i]) << "\",";
    out << "\"enabled\":" << jsonBool(operstate != "down") << ",";
    out << "\"operstate\":\"" << jsonEscape(operstate.empty() ? "unknown" : operstate) << "\"";
    out << "}";
  }
  out << "],";
  out << "\"enabled\":" << jsonBool(anyEnabled);
  if (names.empty()) {
    out << ",\"reason\":\"no_wireless_interface\"";
  }
  out << "}\n";
  return out.str();
}

std::string wifiConfigJson() {
  auto config = parseKeyValueFile(kWifiConfigPath);
  const std::string ssid = optionValue(config, "ssid");
  const std::string psk = optionValue(config, "psk");
  const std::string iface = optionValue(config, "interface");

  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"configured\":" << jsonBool(!ssid.empty()) << ",";
  out << "\"interface\":\"" << jsonEscape(iface) << "\",";
  out << "\"ssid\":\"" << jsonEscape(ssid) << "\",";
  out << "\"hasPsk\":" << jsonBool(!psk.empty()) << ",";
  out << "\"path\":\"" << jsonEscape(kWifiConfigPath) << "\"";
  out << "}\n";
  return out.str();
}

std::string wpaQuoted(const std::string &value) {
  std::ostringstream out;
  out << "\"";
  for (char ch : value) {
    if (ch == '\\' || ch == '"') {
      out << "\\";
    }
    out << ch;
  }
  out << "\"";
  return out.str();
}

void stopWifiSupplicant(const std::string &iface) {
  const std::string wpaCli = findExecutable({
      "/sbin/wpa_cli", "/usr/sbin/wpa_cli", "/bin/wpa_cli", "/usr/bin/wpa_cli"});
  if (!wpaCli.empty() && !iface.empty()) {
    (void)runProgram(wpaCli, {"-i", iface, "terminate"});
  }

  long long pid = 0;
  if (parseLongLong(readTrimmed(kWifiSupplicantPidPath), &pid) && pid > 1) {
    kill(static_cast<pid_t>(pid), SIGTERM);
    usleep(200000);
    kill(static_cast<pid_t>(pid), SIGKILL);
  }
  unlink(kWifiSupplicantPidPath);
}

bool writeWifiSupplicantConfig(const std::string &ssid, const std::string &psk) {
  std::ostringstream conf;
  conf << "ctrl_interface=/run/wpa_supplicant\n";
  conf << "update_config=0\n";
  conf << "network={\n";
  conf << "\tssid=" << wpaQuoted(ssid) << "\n";
  if (psk.empty()) {
    conf << "\tkey_mgmt=NONE\n";
  } else {
    conf << "\tpsk=" << wpaQuoted(psk) << "\n";
  }
  conf << "}\n";
  return writeFile(kWifiSupplicantConfigPath, conf.str(), 0600);
}

CommandResult applyWifiConfig(const std::map<std::string, std::string> &config) {
  const std::string ssid = optionValue(config, "ssid");
  const std::string psk = optionValue(config, "psk");
  std::string iface = optionValue(config, "interface");
  if (iface.empty()) {
    const auto names = wirelessInterfaces();
    iface = names.empty() ? "" : names[0];
  }

  if (ssid.empty()) {
    return {0, "{\"ok\":true,\"configured\":false,\"applied\":false,\"reason\":\"wifi_not_configured\"}\n"};
  }
  if (iface.empty()) {
    std::ostringstream out;
    out << "{\"ok\":true,\"available\":false,\"configured\":true,\"ssid\":\""
        << jsonEscape(ssid) << "\",\"hasPsk\":" << jsonBool(!psk.empty())
        << ",\"applied\":false,\"reason\":\"no_wireless_interface\"}\n";
    return {0, out.str()};
  }
  if (!validDeviceName(iface) || !dirExists("/sys/class/net/" + iface + "/wireless")) {
    return {2, "{\"ok\":false,\"error\":\"invalid_wireless_interface\"}\n"};
  }

  const std::string wpaSupplicant = findExecutable({
      "/sbin/wpa_supplicant", "/usr/sbin/wpa_supplicant",
      "/bin/wpa_supplicant", "/usr/bin/wpa_supplicant"});
  const std::string wpaCli = findExecutable({
      "/sbin/wpa_cli", "/usr/sbin/wpa_cli", "/bin/wpa_cli", "/usr/bin/wpa_cli"});
  if (wpaSupplicant.empty()) {
    return {1, "{\"ok\":false,\"available\":true,\"configured\":true,\"applied\":false,\"error\":\"wpa_supplicant_missing\"}\n"};
  }
  if (wpaCli.empty()) {
    return {1, "{\"ok\":false,\"available\":true,\"configured\":true,\"applied\":false,\"error\":\"wpa_cli_missing\"}\n"};
  }
  if (!ensureNetworkStateDir() ||
      !ensureDir("/run/wpa_supplicant", 0755) ||
      !writeWifiSupplicantConfig(ssid, psk)) {
    return {1, "{\"ok\":false,\"available\":true,\"configured\":true,\"applied\":false,\"error\":\"wifi_config_write_failed\"}\n"};
  }

  (void)networkAction("enable", iface);
  stopWifiSupplicant(iface);
  CommandResult start = runProgram(
      wpaSupplicant, {"-B", "-i", iface, "-c", kWifiSupplicantConfigPath,
                      "-P", kWifiSupplicantPidPath});
  if (start.code != 0) {
    std::ostringstream out;
    out << "{\"ok\":false,\"available\":true,\"configured\":true,\"applied\":false,";
    out << "\"interface\":\"" << jsonEscape(iface) << "\",";
    out << "\"error\":\"wpa_supplicant_start_failed\",";
    out << "\"details\":\"" << jsonEscape(trim(start.output)) << "\"}\n";
    return {1, out.str()};
  }

  bool associated = false;
  std::string lastStatus;
  for (int attempt = 0; attempt < 12; ++attempt) {
    CommandResult status = runProgram(wpaCli, {"-i", iface, "status"});
    lastStatus = status.output;
    if (status.code == 0 && status.output.find("wpa_state=COMPLETED") != std::string::npos) {
      associated = true;
      break;
    }
    sleep(1);
  }

  if (!associated) {
    std::ostringstream out;
    out << "{\"ok\":false,\"available\":true,\"configured\":true,\"applied\":false,";
    out << "\"interface\":\"" << jsonEscape(iface) << "\",";
    out << "\"ssid\":\"" << jsonEscape(ssid) << "\",";
    out << "\"hasPsk\":" << jsonBool(!psk.empty()) << ",";
    out << "\"error\":\"wifi_association_failed\",";
    out << "\"details\":\"" << jsonEscape(trim(lastStatus)) << "\"}\n";
    return {1, out.str()};
  }

  CommandResult dhcp = applyDhcpNetwork(iface);
  const bool success = dhcp.code == 0;
  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"available\":true,\"configured\":true,";
  out << "\"interface\":\"" << jsonEscape(iface) << "\",";
  out << "\"ssid\":\"" << jsonEscape(ssid) << "\",";
  out << "\"hasPsk\":" << jsonBool(!psk.empty()) << ",";
  out << "\"associated\":true,";
  out << "\"applied\":" << jsonBool(success);
  if (!success) {
    out << ",\"error\":\"wifi_dhcp_failed\",";
    out << "\"details\":\"" << jsonEscape(trim(dhcp.output)) << "\"";
  }
  out << "}\n";
  return {success ? 0 : 1, out.str()};
}

CommandResult wifiConnect(const std::map<std::string, std::string> &options) {
  if (options.count("") > 0) {
    return {2, "{\"ok\":false,\"error\":\"invalid_option\"}\n"};
  }

  const std::string ssid = optionValue(options, "ssid");
  const std::string psk = optionValue(options, "psk");
  const std::string iface = optionValue(options, "interface");
  if (ssid.empty() || !validConfigValue(ssid, 96)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_ssid\"}\n"};
  }
  if (!validConfigValue(psk, 128)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_psk\"}\n"};
  }
  if (!iface.empty() && !validDeviceName(iface)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_wireless_interface\"}\n"};
  }

  if (!ensureNetworkStateDir()) {
    return {1, "{\"ok\":false,\"error\":\"network_state_unavailable\"}\n"};
  }

  std::ostringstream data;
  data << "ssid=" << ssid << "\n";
  data << "psk=" << psk << "\n";
  data << "interface=" << iface << "\n";
  if (!writeFile(kWifiConfigPath, data.str(), 0600)) {
    return {1, "{\"ok\":false,\"error\":\"wifi_config_write_failed\"}\n"};
  }

  return applyWifiConfig(parseKeyValueFile(kWifiConfigPath));
}

CommandResult wifiForget() {
  auto config = parseKeyValueFile(kWifiConfigPath);
  stopWifiSupplicant(optionValue(config, "interface"));
  unlink(kWifiConfigPath);
  unlink(kWifiSupplicantConfigPath);
  return {0, "{\"ok\":true,\"configured\":false,\"applied\":true}\n"};
}

std::string wifiScanJson() {
  const auto names = wirelessInterfaces();
  if (names.empty()) {
    return "{\"ok\":true,\"available\":false,\"reason\":\"no_wireless_interface\",\"networks\":[]}\n";
  }

  const std::string iw = findExecutable({"/sbin/iw", "/usr/sbin/iw", "/bin/iw", "/usr/bin/iw"});
  if (iw.empty()) {
    return "{\"ok\":true,\"available\":false,\"reason\":\"iw_missing\",\"networks\":[]}\n";
  }

  std::vector<std::map<std::string, std::string>> networks;
  for (const auto &iface : names) {
    (void)networkAction("enable", iface);
    CommandResult scan = runProgram(iw, {"dev", iface, "scan"});
    if (scan.code != 0) {
      continue;
    }

    std::map<std::string, std::string> current;
    std::istringstream lines(scan.output);
    std::string line;
    while (std::getline(lines, line)) {
      line = trim(line);
      if (line.rfind("BSS ", 0) == 0) {
        if (!current.empty()) {
          networks.push_back(current);
        }
        current.clear();
        auto fields = split(line, ' ');
        if (fields.size() >= 2) {
          current["bssid"] = fields[1];
        }
        current["interface"] = iface;
      } else if (line.rfind("SSID:", 0) == 0) {
        current["ssid"] = trim(line.substr(5));
      } else if (line.rfind("signal:", 0) == 0) {
        current["signal"] = trim(line.substr(7));
      } else if (line.rfind("freq:", 0) == 0) {
        current["frequency"] = trim(line.substr(5));
      }
    }
    if (!current.empty()) {
      networks.push_back(current);
    }
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"available\":true,\"networks\":[";
  for (size_t i = 0; i < networks.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << "{";
    out << "\"ssid\":\"" << jsonEscape(networks[i]["ssid"]) << "\",";
    out << "\"bssid\":\"" << jsonEscape(networks[i]["bssid"]) << "\",";
    out << "\"signal\":\"" << jsonEscape(networks[i]["signal"]) << "\",";
    out << "\"frequency\":\"" << jsonEscape(networks[i]["frequency"]) << "\",";
    out << "\"interface\":\"" << jsonEscape(networks[i]["interface"]) << "\"";
    out << "}";
  }
  out << "]}\n";
  return out.str();
}

CommandResult wifiAction(const std::string &action, const std::string &requestedInterface) {
  if (action != "enable" && action != "disable") {
    return {2, "{\"ok\":false,\"error\":\"unknown_wifi_action\"}\n"};
  }

  if (!requestedInterface.empty()) {
    if (!validDeviceName(requestedInterface) ||
        !dirExists("/sys/class/net/" + requestedInterface + "/wireless")) {
      return {2, "{\"ok\":false,\"error\":\"invalid_wireless_interface\"}\n"};
    }
    return networkAction(action, requestedInterface);
  }

  const auto names = wirelessInterfaces();
  if (names.empty()) {
    return {0, "{\"ok\":true,\"available\":false,\"reason\":\"no_wireless_interface\"}\n"};
  }

  bool success = true;
  for (const auto &name : names) {
    CommandResult result = networkAction(action, name);
    success = success && result.code == 0;
  }

  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"available\":true,\"action\":\""
      << action << "\"}\n";
  return {success ? 0 : 1, out.str()};
}

std::string batteryJson() {
  struct Supply {
    std::string name;
    std::string type;
    std::string status;
    std::string capacity;
    std::string online;
  };

  std::vector<Supply> supplies;
  bool hasBattery = false;
  bool onExternalPower = false;
  for (const auto &name : listDirNames("/sys/class/power_supply")) {
    const std::string base = "/sys/class/power_supply/" + name;
    Supply supply {
      name,
      readTrimmed(base + "/type"),
      readTrimmed(base + "/status"),
      readTrimmed(base + "/capacity"),
      readTrimmed(base + "/online")
    };
    if (supply.type == "Battery") {
      hasBattery = true;
    }
    if (supply.type != "Battery" && supply.online == "1") {
      onExternalPower = true;
    }
    supplies.push_back(supply);
  }

  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"available\":" << jsonBool(hasBattery) << ",";
  out << "\"onExternalPower\":" << jsonBool(onExternalPower) << ",";
  out << "\"supplies\":[";
  for (size_t i = 0; i < supplies.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << "{";
    out << "\"name\":\"" << jsonEscape(supplies[i].name) << "\",";
    out << "\"type\":\"" << jsonEscape(supplies[i].type) << "\",";
    out << "\"status\":\"" << jsonEscape(supplies[i].status) << "\",";
    long long capacity = 0;
    if (!parseLongLong(supplies[i].capacity, &capacity)) {
      out << "\"capacityPercent\":null,";
    } else {
      out << "\"capacityPercent\":" << capacity << ",";
    }
    if (supplies[i].online.empty()) {
      out << "\"online\":null";
    } else {
      out << "\"online\":" << (supplies[i].online == "1" ? "true" : "false");
    }
    out << "}";
  }
  out << "]";
  if (!hasBattery) {
    out << ",\"reason\":\"no_battery\"";
  }
  out << "}\n";
  return out.str();
}

std::vector<std::string> bluetoothRfkillEntries() {
  std::vector<std::string> entries;
  for (const auto &name : listDirNames("/sys/class/rfkill")) {
    const std::string base = "/sys/class/rfkill/" + name;
    if (readTrimmed(base + "/type") == "bluetooth") {
      entries.push_back(name);
    }
  }
  return entries;
}

std::string bluetoothJson() {
  const auto rfkills = bluetoothRfkillEntries();
  const auto controllers = listDirNames("/sys/class/bluetooth");
  bool blocked = false;
  bool anyUnblocked = false;

  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"available\":" << jsonBool(!rfkills.empty() || !controllers.empty()) << ",";
  out << "\"controllers\":[";
  for (size_t i = 0; i < controllers.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << "\"" << jsonEscape(controllers[i]) << "\"";
  }
  out << "],";
  out << "\"rfkill\":[";
  for (size_t i = 0; i < rfkills.size(); ++i) {
    const std::string soft = readTrimmed("/sys/class/rfkill/" + rfkills[i] + "/soft");
    if (soft == "1") {
      blocked = true;
    } else if (soft == "0") {
      anyUnblocked = true;
    }
    if (i > 0) {
      out << ",";
    }
    out << "{";
    out << "\"name\":\"" << jsonEscape(rfkills[i]) << "\",";
    out << "\"blocked\":" << jsonBool(soft == "1");
    out << "}";
  }
  out << "],";
  out << "\"enabled\":" << jsonBool(anyUnblocked || (!controllers.empty() && !blocked)) << ",";
  out << "\"blocked\":" << jsonBool(blocked);
  if (rfkills.empty() && controllers.empty()) {
    out << ",\"reason\":\"no_bluetooth_device\"";
  }
  out << "}\n";
  return out.str();
}

bool validBluetoothAddress(const std::string &value) {
  if (value.size() != 17) {
    return false;
  }
  for (size_t i = 0; i < value.size(); ++i) {
    if (i % 3 == 2) {
      if (value[i] != ':') {
        return false;
      }
      continue;
    }
    if (!std::isxdigit(static_cast<unsigned char>(value[i]))) {
      return false;
    }
  }
  return true;
}

std::string bluetoothCtlPath() {
  return findExecutable({"/usr/bin/bluetoothctl", "/bin/bluetoothctl",
                         "/usr/sbin/bluetoothctl", "/sbin/bluetoothctl"});
}

bool bluetoothAvailableForCtl(std::string *reason) {
  if (bluetoothCtlPath().empty()) {
    *reason = "bluetoothctl_missing";
    return false;
  }
  if (bluetoothRfkillEntries().empty() && listDirNames("/sys/class/bluetooth").empty()) {
    *reason = "no_bluetooth_device";
    return false;
  }
  return true;
}

std::string bluetoothDevicesJson() {
  std::string reason;
  if (!bluetoothAvailableForCtl(&reason)) {
    return "{\"ok\":true,\"available\":false,\"reason\":\"" + reason + "\",\"devices\":[]}\n";
  }

  CommandResult result = runProgram(bluetoothCtlPath(), {"devices"});
  std::vector<std::pair<std::string, std::string>> devices;
  std::istringstream lines(result.output);
  std::string line;
  while (std::getline(lines, line)) {
    line = trim(line);
    if (line.rfind("Device ", 0) != 0) {
      continue;
    }
    auto parts = split(line, ' ');
    if (parts.size() < 2 || !validBluetoothAddress(parts[1])) {
      continue;
    }
    const std::string name = line.size() > 25 ? line.substr(25) : "";
    devices.push_back({parts[1], name});
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"available\":true,\"devices\":[";
  for (size_t i = 0; i < devices.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << "{\"address\":\"" << jsonEscape(devices[i].first) << "\",";
    out << "\"name\":\"" << jsonEscape(devices[i].second) << "\"}";
  }
  out << "]}\n";
  return out.str();
}

CommandResult bluetoothDeviceAction(const std::string &action, const std::string &address) {
  std::string reason;
  if (!bluetoothAvailableForCtl(&reason)) {
    return {0, "{\"ok\":true,\"available\":false,\"reason\":\"" + reason + "\"}\n"};
  }

  std::vector<std::string> args;
  if (action == "scan") {
    args = {"--timeout", "8", "scan", "on"};
  } else {
    if (!validBluetoothAddress(address)) {
      return {2, "{\"ok\":false,\"error\":\"invalid_bluetooth_address\"}\n"};
    }
    if (action == "pair" || action == "connect" || action == "disconnect" ||
        action == "trust" || action == "remove") {
      args = {action, address};
    } else {
      return {2, "{\"ok\":false,\"error\":\"unknown_bluetooth_action\"}\n"};
    }
  }

  CommandResult result = runProgram(bluetoothCtlPath(), args);
  const bool success = result.code == 0 &&
                       result.output.find("Failed to") == std::string::npos &&
                       result.output.find("not available") == std::string::npos;
  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"available\":true,";
  out << "\"action\":\"" << jsonEscape(action) << "\",";
  out << "\"applied\":" << jsonBool(success);
  if (!address.empty()) {
    out << ",\"address\":\"" << jsonEscape(address) << "\"";
  }
  if (!success) {
    out << ",\"error\":\"bluetoothctl_failed\",";
    out << "\"details\":\"" << jsonEscape(trim(result.output)) << "\"";
  }
  out << "}\n";
  return {success ? 0 : 1, out.str()};
}

CommandResult bluetoothAction(const std::string &action) {
  if (action != "enable" && action != "disable") {
    return {2, "{\"ok\":false,\"error\":\"unknown_bluetooth_action\"}\n"};
  }

  const auto rfkills = bluetoothRfkillEntries();
  if (rfkills.empty()) {
    return {0, "{\"ok\":true,\"available\":false,\"reason\":\"no_bluetooth_rfkill\"}\n"};
  }

  bool success = true;
  const std::string soft = action == "enable" ? "0\n" : "1\n";
  for (const auto &entry : rfkills) {
    success = writePlainFile("/sys/class/rfkill/" + entry + "/soft", soft) && success;
  }

  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"available\":true,\"action\":\""
      << action << "\"}\n";
  return {success ? 0 : 1, out.str()};
}

std::string brightnessJson() {
  const auto devices = listDirNames("/sys/class/backlight");
  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"available\":" << jsonBool(!devices.empty()) << ",";
  out << "\"devices\":[";
  for (size_t i = 0; i < devices.size(); ++i) {
    const std::string base = "/sys/class/backlight/" + devices[i];
    long long brightness = 0;
    long long maxBrightness = 0;
    parseLongLong(readTrimmed(base + "/brightness"), &brightness);
    parseLongLong(readTrimmed(base + "/max_brightness"), &maxBrightness);
    const long long percent = maxBrightness > 0 ? (brightness * 100LL) / maxBrightness : 0;

    if (i > 0) {
      out << ",";
    }
    out << "{";
    out << "\"name\":\"" << jsonEscape(devices[i]) << "\",";
    out << "\"brightness\":" << brightness << ",";
    out << "\"maxBrightness\":" << maxBrightness << ",";
    out << "\"percent\":" << percent;
    out << "}";
  }
  out << "]";
  if (!devices.empty()) {
    const std::string base = "/sys/class/backlight/" + devices[0];
    long long brightness = 0;
    long long maxBrightness = 0;
    parseLongLong(readTrimmed(base + "/brightness"), &brightness);
    parseLongLong(readTrimmed(base + "/max_brightness"), &maxBrightness);
    out << ",\"activeDevice\":\"" << jsonEscape(devices[0]) << "\"";
    out << ",\"percent\":" << (maxBrightness > 0 ? (brightness * 100LL) / maxBrightness : 0);
  } else {
    out << ",\"reason\":\"no_backlight\"";
  }
  out << "}\n";
  return out.str();
}

CommandResult brightnessSet(const std::string &percentValue, const std::string &requestedDevice) {
  int percent = 0;
  if (!parsePercent(percentValue, &percent)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_percent\"}\n"};
  }

  const auto devices = listDirNames("/sys/class/backlight");
  if (devices.empty()) {
    return {0, "{\"ok\":true,\"available\":false,\"reason\":\"no_backlight\"}\n"};
  }

  std::string device = requestedDevice.empty() ? devices[0] : requestedDevice;
  if (!validDeviceName(device) ||
      std::find(devices.begin(), devices.end(), device) == devices.end()) {
    return {2, "{\"ok\":false,\"error\":\"invalid_backlight_device\"}\n"};
  }

  const std::string base = "/sys/class/backlight/" + device;
  long long maxBrightness = 0;
  if (!parseLongLong(readTrimmed(base + "/max_brightness"), &maxBrightness) || maxBrightness <= 0) {
    return {1, "{\"ok\":false,\"available\":true,\"error\":\"invalid_max_brightness\"}\n"};
  }

  long long value = (maxBrightness * percent) / 100;
  if (percent > 0 && value == 0) {
    value = 1;
  }

  const bool success = writePlainFile(base + "/brightness", std::to_string(value) + "\n");
  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"available\":true,\"percent\":"
      << percent << ",\"device\":\"" << jsonEscape(device) << "\"}\n";
  return {success ? 0 : 1, out.str()};
}

int parseFirstAudioPercent(const std::string &output) {
  const size_t percentPos = output.find('%');
  if (percentPos == std::string::npos) {
    return -1;
  }

  size_t begin = percentPos;
  while (begin > 0 && std::isdigit(static_cast<unsigned char>(output[begin - 1]))) {
    --begin;
  }

  if (begin == percentPos) {
    return -1;
  }

  try {
    return std::stoi(output.substr(begin, percentPos - begin));
  } catch (...) {
    return -1;
  }
}

std::string audioJson() {
  const std::string amixer = findExecutable({"/usr/bin/amixer", "/bin/amixer"});
  const std::string cards = trim(readFile("/proc/asound/cards"));
  if (amixer.empty()) {
    std::ostringstream out;
    out << "{\"ok\":true,\"available\":false,\"reason\":\"amixer_missing\",";
    out << "\"cardsText\":\"" << jsonEscape(cards) << "\"}\n";
    return out.str();
  }

  CommandResult result = runProgram(amixer, {"get", "Master"});
  if (result.code != 0) {
    std::ostringstream out;
    out << "{\"ok\":true,\"available\":false,\"reason\":\"master_control_missing\",";
    out << "\"control\":\"Master\",";
    out << "\"cardsText\":\"" << jsonEscape(cards) << "\"}\n";
    return out.str();
  }

  const int percent = parseFirstAudioPercent(result.output);
  const bool muted = result.output.find("[off]") != std::string::npos;
  std::ostringstream out;
  out << "{";
  out << "\"ok\":true,";
  out << "\"available\":true,";
  out << "\"control\":\"Master\",";
  if (percent < 0) {
    out << "\"volumePercent\":null,";
  } else {
    out << "\"volumePercent\":" << percent << ",";
  }
  out << "\"muted\":" << jsonBool(muted) << ",";
  out << "\"cardsText\":\"" << jsonEscape(cards) << "\"";
  out << "}\n";
  return out.str();
}

CommandResult audioAction(const std::string &action, const std::string &value) {
  const std::string amixer = findExecutable({"/usr/bin/amixer", "/bin/amixer"});
  if (amixer.empty()) {
    return {0, "{\"ok\":true,\"available\":false,\"reason\":\"amixer_missing\"}\n"};
  }

  std::vector<std::string> args = {"set", "Master"};
  if (action == "set") {
    int percent = 0;
    if (!parsePercent(value, &percent)) {
      return {2, "{\"ok\":false,\"error\":\"invalid_percent\"}\n"};
    }
    args.push_back(std::to_string(percent) + "%");
  } else if (action == "mute") {
    args.push_back("mute");
  } else if (action == "unmute") {
    args.push_back("unmute");
  } else if (action == "toggle") {
    args.push_back("toggle");
  } else {
    return {2, "{\"ok\":false,\"error\":\"unknown_audio_action\"}\n"};
  }

  CommandResult result = runProgram(amixer, args);
  const bool success = result.code == 0;
  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"available\":true,\"action\":\""
      << jsonEscape(action) << "\"}\n";
  return {success ? 0 : 1, out.str()};
}

std::string datetimeJson() {
  std::string base = timeJson();
  if (!base.empty() && base.back() == '\n') {
    base.pop_back();
  }
  if (!base.empty() && base.back() == '}') {
    base.pop_back();
  }
  base += ",\"canSet\":true,\"timezoneManaged\":true}\n";
  return base;
}

CommandResult datetimeSet(const std::string &epochValue) {
  long long epoch = 0;
  if (!parseLongLong(epochValue, &epoch) || epoch <= 0) {
    return {2, "{\"ok\":false,\"error\":\"invalid_epoch\"}\n"};
  }

  timeval tv {};
  tv.tv_sec = static_cast<time_t>(epoch);
  tv.tv_usec = 0;
  const bool success = settimeofday(&tv, nullptr) == 0;

  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"epoch\":" << epoch;
  if (!success) {
    out << ",\"error\":\"settimeofday_failed\"";
  }
  out << "}\n";
  return {success ? 0 : 1, out.str()};
}

CommandResult datetimeSetTimezone(const std::string &timezone) {
  if (!validTimezoneName(timezone)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_timezone\"}\n"};
  }
  if (!ensureTimeStateDir()) {
    return {1, "{\"ok\":false,\"error\":\"time_state_unavailable\"}\n"};
  }
  if (!writeFile(kTimeConfigPath, "timezone=" + timezone + "\n", 0600)) {
    return {1, "{\"ok\":false,\"error\":\"timezone_write_failed\"}\n"};
  }
  const bool success = applyTimezone(timezone);
  std::ostringstream out;
  out << "{\"ok\":" << jsonBool(success) << ",\"timezone\":\""
      << jsonEscape(timezone) << "\",\"applied\":" << jsonBool(success);
  if (!success) {
    out << ",\"error\":\"timezone_apply_failed\"";
  }
  out << "}\n";
  return {success ? 0 : 1, out.str()};
}

long long nowEpoch() {
  return static_cast<long long>(std::time(nullptr));
}

std::string generatedId(const std::string &prefix) {
  timeval tv {};
  gettimeofday(&tv, nullptr);
  std::ostringstream out;
  out << prefix << "-" << tv.tv_sec << "-" << tv.tv_usec << "-" << getpid();
  return out.str();
}

bool validRecordId(const std::string &value) {
  return validName(value) && value.size() <= 96;
}

std::string normalizedValue(const std::map<std::string, std::string> &options,
                            const std::string &key,
                            const std::string &fallback,
                            size_t maxSize) {
  const std::string value = optionValue(options, key);
  if (value.empty()) {
    return fallback;
  }
  if (!validConfigValue(value, maxSize)) {
    return "";
  }
  return value;
}

bool optionBool(const std::string &value, bool fallback) {
  if (value == "true" || value == "1" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "false" || value == "0" || value == "no" || value == "off") {
    return false;
  }
  return fallback;
}

bool parseOptionalEpoch(const std::map<std::string, std::string> &options,
                        const std::string &key,
                        long long fallback,
                        long long *out) {
  const std::string value = optionValue(options, key);
  if (value.empty()) {
    *out = fallback;
    return true;
  }
  return parseLongLong(value, out);
}

bool parseOptionalLimit(const std::map<std::string, std::string> &options,
                        size_t fallback,
                        size_t *out) {
  long long parsed = 0;
  const std::string value = optionValue(options, "limit");
  if (value.empty()) {
    *out = fallback;
    return true;
  }
  if (!parseLongLong(value, &parsed) || parsed < 0 || parsed > 1000) {
    return false;
  }
  *out = static_cast<size_t>(parsed);
  return true;
}

struct NotificationRecord {
  std::string id;
  long long createdEpoch = 0;
  long long updatedEpoch = 0;
  long long timeEpoch = 0;
  std::string source;
  std::string origin;
  std::string appId;
  std::string siteUrl;
  std::string type;
  std::string status;
  std::string title;
  std::string body;
  std::string eventId;
  std::string meta;
};

struct CalendarEventRecord {
  std::string id;
  long long createdEpoch = 0;
  long long updatedEpoch = 0;
  long long startEpoch = 0;
  long long endEpoch = 0;
  std::string source;
  std::string origin;
  std::string type;
  std::string status;
  std::string title;
  std::string description;
  std::string meta;
  std::string notificationId;
};

bool validNotificationStatus(const std::string &status) {
  return status == "unread" || status == "read" || status == "dismissed";
}

bool validEventStatus(const std::string &status) {
  return status == "active" || status == "completed" || status == "cancelled";
}

bool validShortToken(const std::string &value, bool allowEmpty) {
  if (value.empty()) {
    return allowEmpty;
  }
  if (value.size() > 128 || hasControlChar(value)) {
    return false;
  }
  for (char ch : value) {
    const bool allowed = (ch >= 'A' && ch <= 'Z') ||
                         (ch >= 'a' && ch <= 'z') ||
                         (ch >= '0' && ch <= '9') ||
                         ch == '_' || ch == '-' || ch == '.' || ch == ':' || ch == '/';
    if (!allowed) {
      return false;
    }
  }
  return true;
}

bool validTextField(const std::string &value, size_t maxSize, bool allowEmpty) {
  if (value.empty()) {
    return allowEmpty;
  }
  return validConfigValue(value, maxSize);
}

bool ensureNotificationsDir() {
  ensureDir(kDataRoot, 0755);
  return ensureDir(kNotificationsDir, 0700);
}

bool ensureCalendarDir() {
  ensureDir(kDataRoot, 0755);
  return ensureDir(kCalendarDir, 0700);
}

std::vector<NotificationRecord> readNotifications() {
  std::vector<NotificationRecord> records;
  std::ifstream file(kNotificationsPath);
  std::string line;
  while (std::getline(file, line)) {
    auto fields = split(line, '\t');
    if (fields.size() < 14) {
      continue;
    }

    NotificationRecord record;
    record.id = fields[0];
    parseLongLong(fields[1], &record.createdEpoch);
    parseLongLong(fields[2], &record.updatedEpoch);
    parseLongLong(fields[3], &record.timeEpoch);
    record.source = fields[4];
    record.origin = fields[5];
    record.appId = fields[6];
    record.siteUrl = fields[7];
    record.type = fields[8];
    record.status = fields[9];
    record.title = fields[10];
    record.body = fields[11];
    record.eventId = fields[12];
    record.meta = fields[13];

    if (validRecordId(record.id) && validNotificationStatus(record.status)) {
      records.push_back(record);
    }
  }
  return records;
}

bool writeNotifications(const std::vector<NotificationRecord> &records) {
  if (!ensureNotificationsDir()) {
    return false;
  }

  std::ostringstream out;
  for (const auto &record : records) {
    out << record.id << '\t'
        << record.createdEpoch << '\t'
        << record.updatedEpoch << '\t'
        << record.timeEpoch << '\t'
        << record.source << '\t'
        << record.origin << '\t'
        << record.appId << '\t'
        << record.siteUrl << '\t'
        << record.type << '\t'
        << record.status << '\t'
        << record.title << '\t'
        << record.body << '\t'
        << record.eventId << '\t'
        << record.meta << '\n';
  }
  return writeFile(kNotificationsPath, out.str(), 0600);
}

std::vector<CalendarEventRecord> readCalendarEvents() {
  std::vector<CalendarEventRecord> records;
  std::ifstream file(kCalendarEventsPath);
  std::string line;
  while (std::getline(file, line)) {
    auto fields = split(line, '\t');
    if (fields.size() < 13) {
      continue;
    }

    CalendarEventRecord record;
    record.id = fields[0];
    parseLongLong(fields[1], &record.createdEpoch);
    parseLongLong(fields[2], &record.updatedEpoch);
    parseLongLong(fields[3], &record.startEpoch);
    parseLongLong(fields[4], &record.endEpoch);
    record.source = fields[5];
    record.origin = fields[6];
    record.type = fields[7];
    record.status = fields[8];
    record.title = fields[9];
    record.description = fields[10];
    record.meta = fields[11];
    record.notificationId = fields[12];

    if (validRecordId(record.id) && validEventStatus(record.status)) {
      records.push_back(record);
    }
  }
  return records;
}

bool writeCalendarEvents(const std::vector<CalendarEventRecord> &records) {
  if (!ensureCalendarDir()) {
    return false;
  }

  std::ostringstream out;
  for (const auto &record : records) {
    out << record.id << '\t'
        << record.createdEpoch << '\t'
        << record.updatedEpoch << '\t'
        << record.startEpoch << '\t'
        << record.endEpoch << '\t'
        << record.source << '\t'
        << record.origin << '\t'
        << record.type << '\t'
        << record.status << '\t'
        << record.title << '\t'
        << record.description << '\t'
        << record.meta << '\t'
        << record.notificationId << '\n';
  }
  return writeFile(kCalendarEventsPath, out.str(), 0600);
}

bool notificationMatches(const NotificationRecord &record,
                         const std::map<std::string, std::string> &filters) {
  const std::string id = optionValue(filters, "id");
  const std::string source = optionValue(filters, "source");
  const std::string origin = optionValue(filters, "origin");
  const std::string appId = optionValue(filters, "appId");
  const std::string siteUrl = optionValue(filters, "siteUrl");
  const std::string type = optionValue(filters, "type");
  const std::string status = optionValue(filters, "status");
  const std::string eventId = optionValue(filters, "eventId");
  long long from = 0;
  long long to = 0;
  parseLongLong(optionValue(filters, "from"), &from);
  parseLongLong(optionValue(filters, "to"), &to);
  const long long time = record.timeEpoch > 0 ? record.timeEpoch : record.createdEpoch;

  return (id.empty() || record.id == id) &&
         (source.empty() || record.source == source) &&
         (origin.empty() || record.origin == origin) &&
         (appId.empty() || record.appId == appId) &&
         (siteUrl.empty() || record.siteUrl == siteUrl) &&
         (type.empty() || record.type == type) &&
         (status.empty() || record.status == status) &&
         (eventId.empty() || record.eventId == eventId) &&
         (from <= 0 || time >= from) &&
         (to <= 0 || time <= to);
}

bool calendarEventMatches(const CalendarEventRecord &record,
                          const std::map<std::string, std::string> &filters,
                          long long now) {
  const std::string id = optionValue(filters, "id");
  const std::string source = optionValue(filters, "source");
  const std::string origin = optionValue(filters, "origin");
  const std::string type = optionValue(filters, "type");
  const std::string status = optionValue(filters, "status");
  const std::string passed = optionValue(filters, "passed");
  long long from = 0;
  long long to = 0;
  parseLongLong(optionValue(filters, "from"), &from);
  parseLongLong(optionValue(filters, "to"), &to);
  const bool isPassed = record.startEpoch <= now;

  return (id.empty() || record.id == id) &&
         (source.empty() || record.source == source) &&
         (origin.empty() || record.origin == origin) &&
         (type.empty() || record.type == type) &&
         (status.empty() || record.status == status) &&
         (passed.empty() || optionBool(passed, false) == isPassed) &&
         (from <= 0 || record.startEpoch >= from) &&
         (to <= 0 || record.startEpoch <= to);
}

long long notificationSortEpoch(const NotificationRecord &record,
                                const std::string &sortKey) {
  if (sortKey == "updated") {
    return record.updatedEpoch;
  }
  if (sortKey == "time" || sortKey == "timeEpoch") {
    return record.timeEpoch > 0 ? record.timeEpoch : record.createdEpoch;
  }
  return record.createdEpoch;
}

long long eventSortEpoch(const CalendarEventRecord &record,
                         const std::string &sortKey) {
  if (sortKey == "created") {
    return record.createdEpoch;
  }
  if (sortKey == "updated") {
    return record.updatedEpoch;
  }
  if (sortKey == "end" || sortKey == "endEpoch") {
    return record.endEpoch;
  }
  return record.startEpoch;
}

void sortNotifications(std::vector<NotificationRecord> *records,
                       const std::map<std::string, std::string> &filters) {
  const std::string sortKey = optionValue(filters, "sort").empty()
      ? "created"
      : optionValue(filters, "sort");
  const bool desc = optionValue(filters, "order") == "desc";
  std::sort(records->begin(), records->end(), [&](const auto &left, const auto &right) {
    int cmp = 0;
    if (sortKey == "source") {
      cmp = left.source.compare(right.source);
    } else if (sortKey == "origin") {
      cmp = left.origin.compare(right.origin);
    } else if (sortKey == "type") {
      cmp = left.type.compare(right.type);
    } else if (sortKey == "status") {
      cmp = left.status.compare(right.status);
    } else {
      const long long a = notificationSortEpoch(left, sortKey);
      const long long b = notificationSortEpoch(right, sortKey);
      cmp = a < b ? -1 : (a > b ? 1 : 0);
    }
    if (cmp == 0) {
      cmp = left.id.compare(right.id);
    }
    return desc ? cmp > 0 : cmp < 0;
  });
}

void sortCalendarEvents(std::vector<CalendarEventRecord> *records,
                        const std::map<std::string, std::string> &filters) {
  const std::string sortKey = optionValue(filters, "sort").empty()
      ? "start"
      : optionValue(filters, "sort");
  const bool desc = optionValue(filters, "order") == "desc";
  std::sort(records->begin(), records->end(), [&](const auto &left, const auto &right) {
    int cmp = 0;
    if (sortKey == "title") {
      cmp = left.title.compare(right.title);
    } else if (sortKey == "source") {
      cmp = left.source.compare(right.source);
    } else if (sortKey == "status") {
      cmp = left.status.compare(right.status);
    } else {
      const long long a = eventSortEpoch(left, sortKey);
      const long long b = eventSortEpoch(right, sortKey);
      cmp = a < b ? -1 : (a > b ? 1 : 0);
    }
    if (cmp == 0) {
      cmp = left.id.compare(right.id);
    }
    return desc ? cmp > 0 : cmp < 0;
  });
}

std::string notificationToJson(const NotificationRecord &record) {
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << jsonEscape(record.id) << "\",";
  out << "\"createdEpoch\":" << record.createdEpoch << ",";
  out << "\"updatedEpoch\":" << record.updatedEpoch << ",";
  out << "\"timeEpoch\":" << record.timeEpoch << ",";
  out << "\"source\":\"" << jsonEscape(record.source) << "\",";
  out << "\"origin\":\"" << jsonEscape(record.origin) << "\",";
  out << "\"appId\":\"" << jsonEscape(record.appId) << "\",";
  out << "\"siteUrl\":\"" << jsonEscape(record.siteUrl) << "\",";
  out << "\"type\":\"" << jsonEscape(record.type) << "\",";
  out << "\"status\":\"" << jsonEscape(record.status) << "\",";
  out << "\"title\":\"" << jsonEscape(record.title) << "\",";
  out << "\"body\":\"" << jsonEscape(record.body) << "\",";
  out << "\"eventId\":\"" << jsonEscape(record.eventId) << "\",";
  out << "\"meta\":\"" << jsonEscape(record.meta) << "\"";
  out << "}";
  return out.str();
}

std::string calendarEventToJson(const CalendarEventRecord &record, long long now) {
  const bool passed = record.startEpoch <= now;
  const bool editable = record.status == "active" && !passed;
  std::ostringstream out;
  out << "{";
  out << "\"id\":\"" << jsonEscape(record.id) << "\",";
  out << "\"createdEpoch\":" << record.createdEpoch << ",";
  out << "\"updatedEpoch\":" << record.updatedEpoch << ",";
  out << "\"startEpoch\":" << record.startEpoch << ",";
  out << "\"endEpoch\":" << record.endEpoch << ",";
  out << "\"source\":\"" << jsonEscape(record.source) << "\",";
  out << "\"origin\":\"" << jsonEscape(record.origin) << "\",";
  out << "\"type\":\"" << jsonEscape(record.type) << "\",";
  out << "\"status\":\"" << jsonEscape(record.status) << "\",";
  out << "\"title\":\"" << jsonEscape(record.title) << "\",";
  out << "\"description\":\"" << jsonEscape(record.description) << "\",";
  out << "\"meta\":\"" << jsonEscape(record.meta) << "\",";
  out << "\"notificationId\":\"" << jsonEscape(record.notificationId) << "\",";
  out << "\"notificationDelivered\":" << jsonBool(!record.notificationId.empty()) << ",";
  out << "\"passed\":" << jsonBool(passed) << ",";
  out << "\"editable\":" << jsonBool(editable);
  out << "}";
  return out.str();
}

bool validateNotificationRecord(const NotificationRecord &record) {
  return validRecordId(record.id) &&
         validShortToken(record.source, false) &&
         validShortToken(record.origin, true) &&
         validShortToken(record.appId, true) &&
         validTextField(record.siteUrl, 256, true) &&
         validShortToken(record.type, false) &&
         validNotificationStatus(record.status) &&
         validTextField(record.title, 160, false) &&
         validTextField(record.body, 1024, true) &&
         validShortToken(record.eventId, true) &&
         validTextField(record.meta, 1024, true);
}

bool validateCalendarEventRecord(const CalendarEventRecord &record) {
  return validRecordId(record.id) &&
         record.startEpoch > 0 &&
         record.endEpoch >= record.startEpoch &&
         validShortToken(record.source, false) &&
         validShortToken(record.origin, true) &&
         validShortToken(record.type, false) &&
         validEventStatus(record.status) &&
         validTextField(record.title, 160, false) &&
         validTextField(record.description, 2048, true) &&
         validTextField(record.meta, 2048, true) &&
         validShortToken(record.notificationId, true);
}

bool generateDueCalendarNotifications() {
  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return false;
  }

  auto events = readCalendarEvents();
  auto notifications = readNotifications();
  const long long now = nowEpoch();
  bool changed = false;

  for (auto &event : events) {
    if (event.status != "active" || event.notificationId.empty() == false ||
        event.startEpoch > now) {
      continue;
    }

    NotificationRecord notification;
    notification.id = generatedId("ntf");
    notification.createdEpoch = now;
    notification.updatedEpoch = now;
    notification.timeEpoch = event.startEpoch;
    notification.source = "calendar";
    notification.origin = event.origin.empty() ? "suvos.calendar" : event.origin;
    notification.appId = "";
    notification.siteUrl = "";
    notification.type = event.type.empty() ? "calendar" : event.type;
    notification.status = "unread";
    notification.title = event.title;
    notification.body = event.description;
    notification.eventId = event.id;
    notification.meta = event.meta;

    if (!validateNotificationRecord(notification)) {
      continue;
    }

    notifications.push_back(notification);
    event.notificationId = notification.id;
    event.updatedEpoch = now;
    changed = true;
  }

  if (!changed) {
    return true;
  }

  return writeNotifications(notifications) && writeCalendarEvents(events);
}

std::string notificationsJson(const std::map<std::string, std::string> &filters) {
  generateDueCalendarNotifications();

  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return "{\"ok\":false,\"error\":\"state_lock_failed\"}\n";
  }

  auto records = readNotifications();
  std::vector<NotificationRecord> filtered;
  for (const auto &record : records) {
    if (notificationMatches(record, filters)) {
      filtered.push_back(record);
    }
  }

  sortNotifications(&filtered, filters);
  size_t limit = filtered.size();
  parseOptionalLimit(filters, filtered.size(), &limit);
  if (limit < filtered.size()) {
    filtered.resize(limit);
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"count\":" << filtered.size() << ",\"notifications\":[";
  for (size_t i = 0; i < filtered.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << notificationToJson(filtered[i]);
  }
  out << "]}\n";
  return out.str();
}

CommandResult notificationCreate(const std::map<std::string, std::string> &options) {
  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return {1, "{\"ok\":false,\"error\":\"state_lock_failed\"}\n"};
  }

  NotificationRecord record;
  const long long now = nowEpoch();
  record.id = generatedId("ntf");
  record.createdEpoch = now;
  record.updatedEpoch = now;
  if (!parseOptionalEpoch(options, "timeEpoch", 0, &record.timeEpoch)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_time_epoch\"}\n"};
  }
  if (record.timeEpoch == 0) {
    if (!parseOptionalEpoch(options, "time", 0, &record.timeEpoch)) {
      return {2, "{\"ok\":false,\"error\":\"invalid_time_epoch\"}\n"};
    }
  }
  if (record.timeEpoch < 0) {
    return {2, "{\"ok\":false,\"error\":\"invalid_time_epoch\"}\n"};
  }
  record.source = normalizedValue(options, "source", "system", 64);
  record.origin = normalizedValue(options, "origin", "", 128);
  record.appId = normalizedValue(options, "appId", "", 128);
  record.siteUrl = normalizedValue(options, "siteUrl", "", 256);
  record.type = normalizedValue(options, "type", "info", 64);
  record.status = normalizedValue(options, "status", "unread", 32);
  record.title = normalizedValue(options, "title", "", 160);
  record.body = normalizedValue(options, "body", "", 1024);
  record.eventId = normalizedValue(options, "eventId", "", 96);
  record.meta = normalizedValue(options, "meta", "", 1024);

  if (!validateNotificationRecord(record)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_notification\"}\n"};
  }

  auto records = readNotifications();
  records.push_back(record);
  if (!writeNotifications(records)) {
    return {1, "{\"ok\":false,\"error\":\"notification_write_failed\"}\n"};
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"notification\":" << notificationToJson(record) << "}\n";
  return {0, out.str()};
}

bool updateNotificationFromOptions(NotificationRecord *record,
                                   const std::map<std::string, std::string> &options) {
  const std::vector<std::string> textKeys = {
      "source", "origin", "appId", "siteUrl", "type", "status", "title", "body", "eventId", "meta"};
  for (const auto &key : textKeys) {
    if (!optionPresent(options, key)) {
      continue;
    }
    const std::string value = optionValue(options, key);
    const bool allowEmpty = key == "origin" || key == "appId" || key == "siteUrl" ||
                            key == "body" || key == "eventId" || key == "meta";
    const size_t maxSize = key == "body" || key == "meta" ? 1024 :
                           (key == "title" ? 160 : 256);
    if (!validTextField(value, maxSize, allowEmpty)) {
      return false;
    }
    if (key == "source") record->source = value;
    if (key == "origin") record->origin = value;
    if (key == "appId") record->appId = value;
    if (key == "siteUrl") record->siteUrl = value;
    if (key == "type") record->type = value;
    if (key == "status") record->status = value;
    if (key == "title") record->title = value;
    if (key == "body") record->body = value;
    if (key == "eventId") record->eventId = value;
    if (key == "meta") record->meta = value;
  }

  long long timeEpoch = 0;
  const std::string timeValue = optionValue(options, "timeEpoch").empty()
      ? optionValue(options, "time")
      : optionValue(options, "timeEpoch");
  if (!timeValue.empty()) {
    if (!parseLongLong(timeValue, &timeEpoch) || timeEpoch < 0) {
      return false;
    }
    record->timeEpoch = timeEpoch;
  }

  record->updatedEpoch = nowEpoch();
  return validateNotificationRecord(*record);
}

CommandResult notificationUpdate(const std::string &id,
                                 const std::map<std::string, std::string> &options) {
  if (!validRecordId(id)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_notification_id\"}\n"};
  }

  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return {1, "{\"ok\":false,\"error\":\"state_lock_failed\"}\n"};
  }

  auto records = readNotifications();
  for (auto &record : records) {
    if (record.id != id) {
      continue;
    }
    if (!updateNotificationFromOptions(&record, options)) {
      return {2, "{\"ok\":false,\"error\":\"invalid_notification_update\"}\n"};
    }
    if (!writeNotifications(records)) {
      return {1, "{\"ok\":false,\"error\":\"notification_write_failed\"}\n"};
    }
    std::ostringstream out;
    out << "{\"ok\":true,\"notification\":" << notificationToJson(record) << "}\n";
    return {0, out.str()};
  }

  return {127, "{\"ok\":false,\"error\":\"notification_not_found\"}\n"};
}

CommandResult notificationSetStatus(const std::string &id, const std::string &status) {
  return notificationUpdate(id, {{"status", status}});
}

CommandResult notificationDelete(const std::string &id) {
  if (!validRecordId(id)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_notification_id\"}\n"};
  }

  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return {1, "{\"ok\":false,\"error\":\"state_lock_failed\"}\n"};
  }

  auto records = readNotifications();
  const size_t before = records.size();
  records.erase(std::remove_if(records.begin(), records.end(),
                               [&](const auto &record) { return record.id == id; }),
                records.end());
  if (records.size() == before) {
    return {127, "{\"ok\":false,\"error\":\"notification_not_found\"}\n"};
  }
  if (!writeNotifications(records)) {
    return {1, "{\"ok\":false,\"error\":\"notification_write_failed\"}\n"};
  }

  return {0, "{\"ok\":true,\"deleted\":1}\n"};
}

CommandResult notificationClear(const std::map<std::string, std::string> &filters) {
  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return {1, "{\"ok\":false,\"error\":\"state_lock_failed\"}\n"};
  }

  auto records = readNotifications();
  std::vector<NotificationRecord> kept;
  size_t removed = 0;
  for (const auto &record : records) {
    if (notificationMatches(record, filters)) {
      ++removed;
    } else {
      kept.push_back(record);
    }
  }
  if (!writeNotifications(kept)) {
    return {1, "{\"ok\":false,\"error\":\"notification_write_failed\"}\n"};
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"removed\":" << removed << "}\n";
  return {0, out.str()};
}

std::string calendarEventsJson(const std::map<std::string, std::string> &filters) {
  generateDueCalendarNotifications();

  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return "{\"ok\":false,\"error\":\"state_lock_failed\"}\n";
  }

  const long long now = nowEpoch();
  auto records = readCalendarEvents();
  std::vector<CalendarEventRecord> filtered;
  for (const auto &record : records) {
    if (calendarEventMatches(record, filters, now)) {
      filtered.push_back(record);
    }
  }

  sortCalendarEvents(&filtered, filters);
  size_t limit = filtered.size();
  parseOptionalLimit(filters, filtered.size(), &limit);
  if (limit < filtered.size()) {
    filtered.resize(limit);
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"count\":" << filtered.size() << ",\"events\":[";
  for (size_t i = 0; i < filtered.size(); ++i) {
    if (i > 0) {
      out << ",";
    }
    out << calendarEventToJson(filtered[i], now);
  }
  out << "]}\n";
  return out.str();
}

CommandResult calendarEventCreate(const std::map<std::string, std::string> &options) {
  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return {1, "{\"ok\":false,\"error\":\"state_lock_failed\"}\n"};
  }

  CalendarEventRecord record;
  const long long now = nowEpoch();
  record.id = generatedId("evt");
  record.createdEpoch = now;
  record.updatedEpoch = now;
  if (!parseOptionalEpoch(options, "startEpoch", 0, &record.startEpoch) ||
      record.startEpoch <= 0) {
    return {2, "{\"ok\":false,\"error\":\"invalid_start_epoch\"}\n"};
  }
  if (!parseOptionalEpoch(options, "endEpoch", record.startEpoch, &record.endEpoch) ||
      record.endEpoch < record.startEpoch) {
    return {2, "{\"ok\":false,\"error\":\"invalid_end_epoch\"}\n"};
  }

  record.source = normalizedValue(options, "source", "calendar", 64);
  record.origin = normalizedValue(options, "origin", "suvos.calendar", 128);
  record.type = normalizedValue(options, "type", "calendar", 64);
  record.status = normalizedValue(options, "status", "active", 32);
  record.title = normalizedValue(options, "title", "", 160);
  record.description = normalizedValue(options, "description", "", 2048);
  record.meta = normalizedValue(options, "meta", "", 2048);
  record.notificationId = "";

  if (!validateCalendarEventRecord(record)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_calendar_event\"}\n"};
  }

  auto records = readCalendarEvents();
  records.push_back(record);
  if (!writeCalendarEvents(records)) {
    return {1, "{\"ok\":false,\"error\":\"calendar_write_failed\"}\n"};
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"event\":" << calendarEventToJson(record, now) << "}\n";
  return {0, out.str()};
}

bool updateCalendarEventFromOptions(CalendarEventRecord *record,
                                    const std::map<std::string, std::string> &options) {
  const long long now = nowEpoch();
  bool scheduleChanged = false;

  if (!optionValue(options, "startEpoch").empty()) {
    long long value = 0;
    if (!parseLongLong(optionValue(options, "startEpoch"), &value) || value <= 0) {
      return false;
    }
    record->startEpoch = value;
    scheduleChanged = true;
  }
  if (!optionValue(options, "endEpoch").empty()) {
    long long value = 0;
    if (!parseLongLong(optionValue(options, "endEpoch"), &value) || value < record->startEpoch) {
      return false;
    }
    record->endEpoch = value;
    scheduleChanged = true;
  }

  const std::vector<std::string> textKeys = {
      "source", "origin", "type", "status", "title", "description", "meta"};
  for (const auto &key : textKeys) {
    if (!optionPresent(options, key)) {
      continue;
    }
    const std::string value = optionValue(options, key);
    const bool allowEmpty = key == "origin" || key == "description" || key == "meta";
    const size_t maxSize = key == "description" || key == "meta" ? 2048 :
                           (key == "title" ? 160 : 256);
    if (!validTextField(value, maxSize, allowEmpty)) {
      return false;
    }
    if (key == "source") record->source = value;
    if (key == "origin") record->origin = value;
    if (key == "type") record->type = value;
    if (key == "status") record->status = value;
    if (key == "title") record->title = value;
    if (key == "description") record->description = value;
    if (key == "meta") record->meta = value;
  }

  if (scheduleChanged && record->startEpoch > now) {
    record->notificationId.clear();
  }
  record->updatedEpoch = now;
  return validateCalendarEventRecord(*record);
}

CommandResult calendarEventUpdate(const std::string &id,
                                  const std::map<std::string, std::string> &options) {
  if (!validRecordId(id)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_event_id\"}\n"};
  }

  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return {1, "{\"ok\":false,\"error\":\"state_lock_failed\"}\n"};
  }

  auto records = readCalendarEvents();
  const long long now = nowEpoch();
  for (auto &record : records) {
    if (record.id != id) {
      continue;
    }
    if (!updateCalendarEventFromOptions(&record, options)) {
      return {2, "{\"ok\":false,\"error\":\"invalid_event_update\"}\n"};
    }
    if (!writeCalendarEvents(records)) {
      return {1, "{\"ok\":false,\"error\":\"calendar_write_failed\"}\n"};
    }
    std::ostringstream out;
    out << "{\"ok\":true,\"event\":" << calendarEventToJson(record, now) << "}\n";
    return {0, out.str()};
  }

  return {127, "{\"ok\":false,\"error\":\"event_not_found\"}\n"};
}

CommandResult calendarEventSetStatus(const std::string &id, const std::string &status) {
  return calendarEventUpdate(id, {{"status", status}});
}

CommandResult calendarEventDelete(const std::string &id) {
  if (!validRecordId(id)) {
    return {2, "{\"ok\":false,\"error\":\"invalid_event_id\"}\n"};
  }

  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return {1, "{\"ok\":false,\"error\":\"state_lock_failed\"}\n"};
  }

  auto records = readCalendarEvents();
  const size_t before = records.size();
  records.erase(std::remove_if(records.begin(), records.end(),
                               [&](const auto &record) { return record.id == id; }),
                records.end());
  if (records.size() == before) {
    return {127, "{\"ok\":false,\"error\":\"event_not_found\"}\n"};
  }
  if (!writeCalendarEvents(records)) {
    return {1, "{\"ok\":false,\"error\":\"calendar_write_failed\"}\n"};
  }

  return {0, "{\"ok\":true,\"deleted\":1}\n"};
}

CommandResult calendarEventClear(const std::map<std::string, std::string> &filters) {
  ScopedFileLock lock(kStateLockPath);
  if (!lock.locked()) {
    return {1, "{\"ok\":false,\"error\":\"state_lock_failed\"}\n"};
  }

  const long long now = nowEpoch();
  auto records = readCalendarEvents();
  std::vector<CalendarEventRecord> kept;
  size_t removed = 0;
  for (const auto &record : records) {
    if (calendarEventMatches(record, filters, now)) {
      ++removed;
    } else {
      kept.push_back(record);
    }
  }
  if (!writeCalendarEvents(kept)) {
    return {1, "{\"ok\":false,\"error\":\"calendar_write_failed\"}\n"};
  }

  std::ostringstream out;
  out << "{\"ok\":true,\"removed\":" << removed << "}\n";
  return {0, out.str()};
}

// Power control plane. Chromium never calls shutdown directly: the browser
// POSTs to suvos-gateway, which forwards `power <action>` here. suvosd owns the
// capability check and the privileged action.
//
// Real SuvOS sets SUVOS_POWER_MODE=real (done by /init). Any other value -
// including unset, e.g. the Docker dev gateway - is treated as mock and only
// reports what would happen, never touching the host/container.
bool powerModeReal() {
  const char *mode = std::getenv("SUVOS_POWER_MODE");
  return mode != nullptr && std::string(mode) == "real";
}

CommandResult powerAction(const std::string &action) {
  if (!powerModeReal()) {
    std::ostringstream out;
    out << "{\"ok\":true,\"mode\":\"mock\",\"action\":\"" << action << "\"}\n";
    return {0, out.str()};
  }

  // Real mode: flush state before handing off. Full teardown (close services,
  // unmount writable layers, poweroff) belongs to a system power helper that
  // /init provides; suvosd delegates to it and falls back to a direct reboot
  // syscall for shutdown/reboot when the helper is absent.
  logConsole("power: " + action + " requested (real mode)");
  sync();

  const std::string helper = std::string(kSystemRoot) + "/bin/suvos-power";
  if (fileExistsExecutable(helper)) {
    CommandResult helperResult = runProgram(helper, {action});
    if (helperResult.code == 0) {
      std::ostringstream out;
      out << "{\"ok\":true,\"mode\":\"real\",\"action\":\"" << action
          << "\",\"via\":\"helper\"}\n";
      return {0, out.str()};
    }
    return helperResult;
  }

  if (action == "logout") {
    // Session teardown without poweroff is the helper's job; report ok so the
    // browser shell can return to a login surface.
    return {0, "{\"ok\":true,\"mode\":\"real\",\"action\":\"logout\"}\n"};
  }

  sync();
  if (action == "reboot") {
    reboot(RB_AUTOBOOT);
  } else {  // shutdown
    reboot(RB_POWER_OFF);
  }
  // reboot() does not return on success.
  return {1, "{\"ok\":false,\"mode\":\"real\",\"error\":\"power_failed\"}\n"};
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

  if (command == "status-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, statusJson()};
  }

  if (command == "time-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, timeJson()};
  }

  if (command == "network-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, networkJson()};
  }

  if (command == "network-config-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, networkConfigJson()};
  }

  if (command == "network") {
    if (parts.size() < 2) {
      return {2, "{\"ok\":false,\"error\":\"missing_network_action\"}\n"};
    }
    if (!hasPermission(policy, "system.network")) {
      return {1, tr("permission.denied") + "system.network\n"};
    }
    if (parts[1] == "configure") {
      return networkConfigure(parseOptions(parts, 2));
    }
    const std::string iface = parts.size() >= 3 ? parts[2] : "";
    return networkAction(parts[1], iface);
  }

  if (command == "wifi-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, wifiJson()};
  }

  if (command == "wifi-config-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, wifiConfigJson()};
  }

  if (command == "wifi-scan-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, wifiScanJson()};
  }

  if (command == "wifi") {
    if (parts.size() < 2) {
      return {2, "{\"ok\":false,\"error\":\"missing_wifi_action\"}\n"};
    }
    if (!hasPermission(policy, "system.wifi")) {
      return {1, tr("permission.denied") + "system.wifi\n"};
    }
    if (parts[1] == "connect") {
      return wifiConnect(parseOptions(parts, 2));
    }
    if (parts[1] == "forget") {
      return wifiForget();
    }
    const std::string iface = parts.size() >= 3 ? parts[2] : "";
    return wifiAction(parts[1], iface);
  }

  if (command == "battery-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, batteryJson()};
  }

  if (command == "bluetooth-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, bluetoothJson()};
  }

  if (command == "bluetooth-devices-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, bluetoothDevicesJson()};
  }

  if (command == "bluetooth") {
    if (parts.size() < 2) {
      return {2, "{\"ok\":false,\"error\":\"missing_bluetooth_action\"}\n"};
    }
    if (!hasPermission(policy, "system.bluetooth")) {
      return {1, tr("permission.denied") + "system.bluetooth\n"};
    }
    if (parts[1] == "enable" || parts[1] == "disable") {
      return bluetoothAction(parts[1]);
    }
    auto options = parseOptions(parts, 2);
    return bluetoothDeviceAction(parts[1], optionValue(options, "address"));
  }

  if (command == "brightness-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, brightnessJson()};
  }

  if (command == "brightness") {
    if (parts.size() < 3 || parts[1] != "set") {
      return {2, "{\"ok\":false,\"error\":\"invalid_brightness_command\"}\n"};
    }
    if (!hasPermission(policy, "system.brightness")) {
      return {1, tr("permission.denied") + "system.brightness\n"};
    }
    auto options = parseOptions(parts, 3);
    return brightnessSet(parts[2], optionValue(options, "device"));
  }

  if (command == "audio-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, audioJson()};
  }

  if (command == "audio") {
    if (parts.size() < 2) {
      return {2, "{\"ok\":false,\"error\":\"missing_audio_action\"}\n"};
    }
    if (!hasPermission(policy, "system.audio")) {
      return {1, tr("permission.denied") + "system.audio\n"};
    }
    const std::string value = parts.size() >= 3 ? parts[2] : "";
    return audioAction(parts[1], value);
  }

  if (command == "datetime-json") {
    if (!hasPermission(policy, "status.read")) {
      return {1, tr("permission.denied") + "status.read\n"};
    }

    return {0, datetimeJson()};
  }

  if (command == "datetime") {
    if (parts.size() < 2) {
      return {2, "{\"ok\":false,\"error\":\"invalid_datetime_command\"}\n"};
    }
    if (!hasPermission(policy, "system.datetime")) {
      return {1, tr("permission.denied") + "system.datetime\n"};
    }
    if (parts[1] == "set" && parts.size() >= 3) {
      return datetimeSet(parts[2]);
    }
    if (parts[1] == "timezone") {
      auto options = parseOptions(parts, 2);
      return datetimeSetTimezone(optionValue(options, "timezone"));
    }
    return {2, "{\"ok\":false,\"error\":\"invalid_datetime_command\"}\n"};
  }

  if (command == "notifications-json") {
    if (!hasPermission(policy, "notifications.read")) {
      return {1, tr("permission.denied") + "notifications.read\n"};
    }

    return {0, notificationsJson(parseOptions(parts, 1))};
  }

  if (command == "notification") {
    if (parts.size() < 2) {
      return {2, "{\"ok\":false,\"error\":\"missing_notification_action\"}\n"};
    }
    if (!hasPermission(policy, "notifications.write")) {
      return {1, tr("permission.denied") + "notifications.write\n"};
    }

    const std::string &action = parts[1];
    auto options = parseOptions(parts, 2);
    const std::string id = optionValue(options, "id");
    if (action == "create") {
      return notificationCreate(options);
    }
    if (action == "update") {
      return notificationUpdate(id, options);
    }
    if (action == "delete") {
      return notificationDelete(id);
    }
    if (action == "clear") {
      return notificationClear(options);
    }
    if (action == "read") {
      return notificationSetStatus(id, "read");
    }
    if (action == "dismiss") {
      return notificationSetStatus(id, "dismissed");
    }
    return {2, "{\"ok\":false,\"error\":\"unknown_notification_action\"}\n"};
  }

  if (command == "calendar-events-json") {
    if (!hasPermission(policy, "calendar.read")) {
      return {1, tr("permission.denied") + "calendar.read\n"};
    }

    return {0, calendarEventsJson(parseOptions(parts, 1))};
  }

  if (command == "calendar") {
    if (parts.size() < 2) {
      return {2, "{\"ok\":false,\"error\":\"missing_calendar_action\"}\n"};
    }
    if (!hasPermission(policy, "calendar.write")) {
      return {1, tr("permission.denied") + "calendar.write\n"};
    }

    const std::string &action = parts[1];
    auto options = parseOptions(parts, 2);
    const std::string id = optionValue(options, "id");
    if (action == "create") {
      return calendarEventCreate(options);
    }
    if (action == "update") {
      return calendarEventUpdate(id, options);
    }
    if (action == "delete") {
      return calendarEventDelete(id);
    }
    if (action == "clear") {
      return calendarEventClear(options);
    }
    if (action == "complete") {
      return calendarEventSetStatus(id, "completed");
    }
    if (action == "cancel") {
      return calendarEventSetStatus(id, "cancelled");
    }
    if (action == "reopen") {
      return calendarEventSetStatus(id, "active");
    }
    return {2, "{\"ok\":false,\"error\":\"unknown_calendar_action\"}\n"};
  }

  if (command == "power") {
    if (parts.size() < 2) {
      return {2, "missing power action\n"};
    }
    const std::string &action = parts[1];
    if (action != "shutdown" && action != "reboot" && action != "logout") {
      return {2, "unknown power action: " + action + "\n"};
    }
    if (!hasPermission(policy, "system.power")) {
      return {1, tr("permission.denied") + "system.power\n"};
    }
    return powerAction(action);
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

  if (command == "roles-json" || command == "role-json") {
    if (!hasPermission(policy, "role.read")) {
      return {1, tr("permission.denied") + "role.read\n"};
    }

    return {0, rolesJson(policy)};
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

  if (command == "apps-json") {
    if (!hasPermission(policy, "apps.list")) {
      return {1, tr("permission.denied") + "apps.list\n"};
    }

    return {0, appsJson(apps)};
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

void applySavedRuntimeState() {
  (void)applySavedTimezone();

  auto networkConfig = parseKeyValueFile(kNetworkConfigPath);
  if (!networkConfig.empty()) {
    CommandResult result = applyNetworkConfig(networkConfig);
    logConsole("network reapply exit=" + std::to_string(result.code));
  }

  auto wifiConfig = parseKeyValueFile(kWifiConfigPath);
  if (!wifiConfig.empty()) {
    CommandResult result = applyWifiConfig(wifiConfig);
    logConsole("wifi reapply exit=" + std::to_string(result.code));
  }
}

void startCalendarNotificationScheduler() {
  pid_t pid = fork();
  if (pid < 0) {
    logConsole("failed to fork notification scheduler: " + std::string(strerror(errno)));
    return;
  }

  if (pid == 0) {
    signal(SIGCHLD, SIG_DFL);
    while (true) {
      (void)generateDueCalendarNotifications();
      sleep(1);
    }
  }
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
  applySavedRuntimeState();
  startCalendarNotificationScheduler();
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
