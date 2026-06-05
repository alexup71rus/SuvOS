#include <algorithm>
#include <csignal>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char *kSystemRoot = "/system/suvos";
constexpr const char *kRegistryPath = "/system/suvos/apps/registry.tsv";
constexpr const char *kRunDir = "/run/suvosd";
constexpr const char *kResponsesDir = "/run/suvosd/responses";
constexpr const char *kRequestFifo = "/run/suvosd/request";
constexpr const char *kPidFile = "/run/suvosd/pid";
constexpr const char *kExitPrefix = "__SUVOSD_EXIT__:";
constexpr int kResponseOpenRetries = 200;
constexpr int kResponseOpenSleepMicros = 10000;
constexpr int kAppTimeoutSeconds = 30;
constexpr size_t kMaxAppOutputBytes = 1024 * 1024;
constexpr sig_atomic_t kMaxRequestWorkers = 16;

volatile sig_atomic_t gActiveRequestWorkers = 0;

struct App {
  std::string name;
  std::string path;
  std::string permission;
  std::string description;
};

struct CommandResult {
  int code = 0;
  std::string output;
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
  if (key == "status.read_only") return ru ? "read-only" : "read-only";
  if (key == "status.writable") return ru ? "writable" : "writable";
  if (key == "status.rootfs") return ru ? "Root filesystem: initramfs" : "Root filesystem: initramfs";
  if (key == "status.kernel") return ru ? "Ядро" : "Kernel";
  if (key == "status.uptime") return ru ? "Uptime" : "Uptime";
  if (key == "roles.current") return ru ? "Текущая роль" : "Current role";
  if (key == "roles.permissions") return ru ? "Права" : "Permissions";
  if (key == "roles.check") return ru ? "Проверка прав" : "Permission check";
  if (key == "list.header") return ru ? "Разрешенные приложения:" : "Allowed apps:";
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

std::vector<App> loadRegistry() {
  std::vector<App> apps;
  std::ifstream file(kRegistryPath);

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

const App *findApp(const std::vector<App> &apps, const std::string &name) {
  for (const auto &app : apps) {
    if (app.name == name) {
      return &app;
    }
  }

  return nullptr;
}

std::string currentRole() {
  return "root";
}

std::string currentPermissions() {
  return "*";
}

bool hasPermission(const std::string &) {
  return true;
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

  const std::string &command = parts[0];

  if (command == "ping") {
    return {0, "pong\n"};
  }

  if (command == "status") {
    std::ostringstream out;
    out << tr("status.booted") << "\n";
    out << tr("status.daemon") << "\n";
    out << tr("status.system_root") << ": " << kSystemRoot << "\n";
    out << tr("status.system_root_mode") << ": " << (systemRootReadOnly() ? tr("status.read_only") : tr("status.writable")) << "\n";
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
    std::ostringstream out;
    out << tr("roles.current") << ": " << currentRole() << "\n";
    out << tr("roles.permissions") << ": " << currentPermissions() << "\n";
    out << tr("roles.check") << ": " << (hasPermission("root") ? "allow" : "deny") << "\n";
    return {0, out.str()};
  }

  std::vector<App> apps = loadRegistry();

  if (command == "list") {
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

    if (!hasPermission(app->permission)) {
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
  CommandResult result = handleCommand(commandParts);
  writeResponse(responsePath, result);
  _exit(0);
}

void writePidFile() {
  std::ofstream file(kPidFile);
  file << getpid() << "\n";
}

void prepareRuntimeDir() {
  unlink(kRequestFifo);
  unlink(kPidFile);
  ensureDir("/run", 0755);
  ensureDir(kRunDir, 0700);
  ensureDir(kResponsesDir, 0700);

  if (mkfifo(kRequestFifo, 0600) != 0 && errno != EEXIST) {
    std::cerr << "suvosd: mkfifo failed: " << strerror(errno) << "\n";
    _exit(1);
  }

  chmod(kRequestFifo, 0600);
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
