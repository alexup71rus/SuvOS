#include <unistd.h>
#include <cstdlib>
#include <cstring>

class SuvOSMessage {
public:
  const char *text() const {
    const char *lang = getenv("SUVOS_LANG");
    if (lang != nullptr && strncmp(lang, "en", 2) == 0) {
      return "Hello from a statically linked x86_64 C++ program inside SuvOS.\n";
    }
    return "Привет из статически собранной x86_64 C++ программы внутри SuvOS.\n";
  }
};

int main() {
  SuvOSMessage message;
  const char *text = message.text();
  unsigned long length = 0;

  while (text[length] != '\0') {
    ++length;
  }

  write(1, text, length);
  return 0;
}
