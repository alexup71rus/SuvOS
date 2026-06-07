#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr const char *kDefaultSocket = "/run/suvosd/control.sock";
constexpr const char *kExitPrefix = "__SUVOSD_EXIT__:";
constexpr size_t kMaxResponseBytes = 1024 * 1024;

bool hasProtocolChar(const std::string &value) {
  for (char ch : value) {
    if (ch == '\t' || ch == '\n' || ch == '\r') {
      return true;
    }
  }
  return false;
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

int connectSocket(const std::string &socketPath) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    std::cerr << "suvosctl: socket failed: " << strerror(errno) << "\n";
    return -1;
  }

  sockaddr_un addr {};
  addr.sun_family = AF_UNIX;
  if (socketPath.size() >= sizeof(addr.sun_path)) {
    std::cerr << "suvosctl: socket path is too long\n";
    close(fd);
    return -1;
  }
  std::strncpy(addr.sun_path, socketPath.c_str(), sizeof(addr.sun_path) - 1);

  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    std::cerr << "suvosctl: connect failed: " << strerror(errno) << "\n";
    close(fd);
    return -1;
  }

  return fd;
}

std::string readResponse(int fd) {
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

    if (response.size() < kMaxResponseBytes) {
      const size_t remaining = kMaxResponseBytes - response.size();
      response.append(buffer, std::min(static_cast<size_t>(n), remaining));
    }
  }

  return response;
}

int parseAndPrintResponse(const std::string &response) {
  const size_t marker = response.rfind(kExitPrefix);
  if (marker == std::string::npos) {
    std::cout << response;
    std::cerr << "suvosctl: malformed response from suvosd\n";
    return 1;
  }

  std::cout << response.substr(0, marker);

  std::string status = response.substr(marker + std::strlen(kExitPrefix));
  while (!status.empty() && (status.back() == '\n' || status.back() == '\r')) {
    status.pop_back();
  }

  try {
    return std::stoi(status);
  } catch (...) {
    std::cerr << "suvosctl: malformed status from suvosd\n";
    return 1;
  }
}

void usage() {
  std::cerr << "Usage: suvosctl [--socket <path>] <command> [args...]\n";
  std::cerr << "Examples:\n";
  std::cerr << "  suvosctl ping\n";
  std::cerr << "  suvosctl status\n";
  std::cerr << "  suvosctl list\n";
}

} // namespace

int main(int argc, char **argv) {
  std::string socketPath = kDefaultSocket;
  std::vector<std::string> parts;

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "--socket") {
      if (i + 1 >= argc) {
        usage();
        return 2;
      }
      socketPath = argv[++i];
      continue;
    }
    if (arg == "-h" || arg == "--help") {
      usage();
      return 0;
    }
    parts.push_back(arg);
  }

  if (parts.empty()) {
    usage();
    return 2;
  }

  std::ostringstream request;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (hasProtocolChar(parts[i])) {
      std::cerr << "suvosctl: arguments cannot contain tabs or newlines\n";
      return 2;
    }
    if (i > 0) {
      request << '\t';
    }
    request << parts[i];
  }
  request << '\n';

  int fd = connectSocket(socketPath);
  if (fd < 0) {
    return 1;
  }

  if (!writeAll(fd, request.str())) {
    std::cerr << "suvosctl: write failed: " << strerror(errno) << "\n";
    close(fd);
    return 1;
  }

  shutdown(fd, SHUT_WR);
  std::string response = readResponse(fd);
  close(fd);

  return parseAndPrintResponse(response);
}
