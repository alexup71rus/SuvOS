#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
#include <algorithm>
#include <array>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

struct Color {
  uint8_t red = 0x11;
  uint8_t green = 0x61;
  uint8_t blue = 0x49;
};

enum class Mode {
  Fill,
  Loader,
  Crash,
};

struct Canvas {
  uint8_t *base = nullptr;
  fb_fix_screeninfo fixed {};
  fb_var_screeninfo var {};
};

bool parseHexColor(const std::string &value, Color *color) {
  if (value.size() != 7 || value[0] != '#') {
    return false;
  }

  char *end = nullptr;
  const long parsed = std::strtol(value.c_str() + 1, &end, 16);
  if (end == nullptr || *end != '\0' || parsed < 0 || parsed > 0xffffff) {
    return false;
  }

  color->red = static_cast<uint8_t>((parsed >> 16) & 0xff);
  color->green = static_cast<uint8_t>((parsed >> 8) & 0xff);
  color->blue = static_cast<uint8_t>(parsed & 0xff);
  return true;
}

uint32_t scaleChannel(uint8_t value, uint32_t length) {
  if (length == 0) {
    return 0;
  }
  const uint32_t maxValue = (1U << length) - 1U;
  return (static_cast<uint32_t>(value) * maxValue + 127U) / 255U;
}

uint32_t packColor(const fb_var_screeninfo &var, const Color &color) {
  return (scaleChannel(color.red, var.red.length) << var.red.offset) |
         (scaleChannel(color.green, var.green.length) << var.green.offset) |
         (scaleChannel(color.blue, var.blue.length) << var.blue.offset);
}

void writePixel(uint8_t *pixel, uint32_t bitsPerPixel, uint32_t packed) {
  switch (bitsPerPixel) {
    case 8:
      pixel[0] = static_cast<uint8_t>(packed & 0xff);
      break;
    case 16:
      pixel[0] = static_cast<uint8_t>(packed & 0xff);
      pixel[1] = static_cast<uint8_t>((packed >> 8) & 0xff);
      break;
    case 24:
      pixel[0] = static_cast<uint8_t>(packed & 0xff);
      pixel[1] = static_cast<uint8_t>((packed >> 8) & 0xff);
      pixel[2] = static_cast<uint8_t>((packed >> 16) & 0xff);
      break;
    case 32:
      pixel[0] = static_cast<uint8_t>(packed & 0xff);
      pixel[1] = static_cast<uint8_t>((packed >> 8) & 0xff);
      pixel[2] = static_cast<uint8_t>((packed >> 16) & 0xff);
      pixel[3] = static_cast<uint8_t>((packed >> 24) & 0xff);
      break;
  }
}

std::array<uint8_t, 7> glyph(char input) {
  const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(input)));
  switch (c) {
    case 'A': return {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    case 'B': return {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e};
    case 'C': return {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e};
    case 'D': return {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
    case 'E': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f};
    case 'F': return {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10};
    case 'G': return {0x0e, 0x11, 0x10, 0x13, 0x11, 0x11, 0x0f};
    case 'H': return {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
    case 'I': return {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e};
    case 'J': return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0c};
    case 'K': return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L': return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
    case 'M': return {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N': return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O': return {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    case 'P': return {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10};
    case 'Q': return {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d};
    case 'R': return {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11};
    case 'S': return {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e};
    case 'T': return {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
    case 'V': return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04};
    case 'W': return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a};
    case 'X': return {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11};
    case 'Y': return {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04};
    case 'Z': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f};
    case '0': return {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e};
    case '1': return {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e};
    case '2': return {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f};
    case '3': return {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e};
    case '4': return {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02};
    case '5': return {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e};
    case '6': return {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e};
    case '7': return {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8': return {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e};
    case '9': return {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e};
    case '.': return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c};
    case ':': return {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00};
    case '-': return {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00};
    case '/': return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
    default: return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  }
}

uint32_t textWidth(const std::string &text, uint32_t scale) {
  if (text.empty()) {
    return 0;
  }
  return static_cast<uint32_t>((text.size() * 6U - 1U) * scale);
}

void fillRect(const Canvas &canvas, uint32_t x, uint32_t y, uint32_t width, uint32_t height, const Color &color) {
  if (canvas.var.bits_per_pixel != 8 && canvas.var.bits_per_pixel != 16 &&
      canvas.var.bits_per_pixel != 24 && canvas.var.bits_per_pixel != 32) {
    return;
  }

  const uint32_t packed = packColor(canvas.var, color);
  const uint32_t bytesPerPixel = canvas.var.bits_per_pixel / 8U;
  const uint32_t xEnd = std::min<uint32_t>(canvas.var.xres, x + width);
  const uint32_t yEnd = std::min<uint32_t>(canvas.var.yres, y + height);

  for (uint32_t rowIndex = y; rowIndex < yEnd; ++rowIndex) {
    uint8_t *row = canvas.base + rowIndex * canvas.fixed.line_length;
    for (uint32_t col = x; col < xEnd; ++col) {
      writePixel(row + col * bytesPerPixel, canvas.var.bits_per_pixel, packed);
    }
  }
}

void drawGlyph(const Canvas &canvas, char c, uint32_t x, uint32_t y, uint32_t scale, const Color &color) {
  const auto rows = glyph(c);
  for (uint32_t row = 0; row < rows.size(); ++row) {
    for (uint32_t col = 0; col < 5; ++col) {
      if ((rows[row] & (1U << (4U - col))) == 0) {
        continue;
      }
      fillRect(canvas, x + col * scale, y + row * scale, scale, scale, color);
    }
  }
}

void drawText(const Canvas &canvas, const std::string &text, uint32_t x, uint32_t y, uint32_t scale, const Color &color) {
  uint32_t cursor = x;
  for (const char c : text) {
    drawGlyph(canvas, c, cursor, y, scale, color);
    cursor += 6U * scale;
  }
}

void drawCenteredText(const Canvas &canvas, const std::string &text, uint32_t y, uint32_t scale, const Color &color) {
  const uint32_t width = textWidth(text, scale);
  const uint32_t x = width < canvas.var.xres ? (canvas.var.xres - width) / 2U : 0U;
  drawText(canvas, text, x, y, scale, color);
}

uint32_t clampScale(uint32_t value, uint32_t minimum, uint32_t maximum) {
  return std::max<uint32_t>(minimum, std::min<uint32_t>(maximum, value));
}

void drawLoader(const Canvas &canvas, const std::string &message) {
  const Color background {0x0b, 0x11, 0x10};
  const Color panel {0x12, 0x1d, 0x1a};
  const Color accent {0x43, 0xd3, 0x9e};
  const Color text {0xec, 0xf5, 0xf1};
  const Color muted {0x86, 0xa4, 0x98};

  fillRect(canvas, 0, 0, canvas.var.xres, canvas.var.yres, background);

  const uint32_t logoScale = clampScale(canvas.var.xres / 160U, 3U, 7U);
  const uint32_t smallScale = clampScale(canvas.var.xres / 360U, 2U, 3U);
  const uint32_t logoY = canvas.var.yres / 3U - 4U * logoScale;
  drawCenteredText(canvas, "SUVOS", logoY, logoScale, text);

  const std::string status = message.empty() ? "STARTING SYSTEM" : message;
  drawCenteredText(canvas, status, logoY + 11U * logoScale, smallScale, muted);

  const uint32_t barWidth = std::max<uint32_t>(180U, std::min<uint32_t>(canvas.var.xres * 45U / 100U, 520U));
  const uint32_t barHeight = std::max<uint32_t>(8U, smallScale * 4U);
  const uint32_t barX = (canvas.var.xres - barWidth) / 2U;
  const uint32_t barY = logoY + 17U * logoScale;
  fillRect(canvas, barX, barY, barWidth, barHeight, panel);
  fillRect(canvas, barX, barY, barWidth / 2U, barHeight, accent);
}

void drawCrash(const Canvas &canvas, const std::string &message, const Color &background) {
  const Color text {0xf4, 0xff, 0xfb};
  const Color muted {0xbe, 0xdb, 0xd1};

  fillRect(canvas, 0, 0, canvas.var.xres, canvas.var.yres, background);

  const uint32_t logoScale = clampScale(canvas.var.xres / 210U, 3U, 5U);
  const uint32_t smallScale = clampScale(canvas.var.xres / 420U, 2U, 3U);
  const uint32_t y = canvas.var.yres / 3U;

  drawCenteredText(canvas, "SUVOS STOPPED", y, logoScale, text);
  drawCenteredText(canvas, message.empty() ? "SYSTEM FALLBACK" : message, y + 10U * logoScale, smallScale, muted);
  drawCenteredText(canvas, "SERIAL CONSOLE ACTIVE", y + 16U * logoScale, smallScale, muted);
}

int renderFramebuffer(const char *path, const Color &color, bool checkOnly, Mode mode, const std::string &message) {
  int fd = open(path, O_RDWR);
  if (fd < 0) {
    std::fprintf(stderr, "suvos-splash: framebuffer unavailable: %s: %s\n", path, std::strerror(errno));
    return 0;
  }

  Canvas canvas {};
  if (ioctl(fd, FBIOGET_FSCREENINFO, &canvas.fixed) != 0 || ioctl(fd, FBIOGET_VSCREENINFO, &canvas.var) != 0) {
    std::fprintf(stderr, "suvos-splash: framebuffer info unavailable: %s\n", std::strerror(errno));
    close(fd);
    return 0;
  }

  std::printf(
    "suvos-splash: %ux%u %ubpp line=%u memory=%u\n",
    canvas.var.xres,
    canvas.var.yres,
    canvas.var.bits_per_pixel,
    canvas.fixed.line_length,
    canvas.fixed.smem_len
  );

  if (checkOnly) {
    close(fd);
    return 0;
  }

  if (canvas.var.bits_per_pixel != 8 && canvas.var.bits_per_pixel != 16 &&
      canvas.var.bits_per_pixel != 24 && canvas.var.bits_per_pixel != 32) {
    std::fprintf(stderr, "suvos-splash: unsupported framebuffer depth: %u\n", canvas.var.bits_per_pixel);
    close(fd);
    return 0;
  }

  void *memory = mmap(nullptr, canvas.fixed.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (memory == MAP_FAILED) {
    std::fprintf(stderr, "suvos-splash: mmap failed: %s\n", std::strerror(errno));
    close(fd);
    return 0;
  }

  canvas.base = static_cast<uint8_t *>(memory);
  switch (mode) {
    case Mode::Fill:
      fillRect(canvas, 0, 0, canvas.var.xres, canvas.var.yres, color);
      break;
    case Mode::Loader:
      drawLoader(canvas, message);
      break;
    case Mode::Crash:
      drawCrash(canvas, message, color);
      break;
  }

  msync(memory, canvas.fixed.smem_len, MS_SYNC);
  munmap(memory, canvas.fixed.smem_len);
  close(fd);
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  const char *framebuffer = "/dev/fb0";
  Color color;
  bool checkOnly = false;
  Mode mode = Mode::Fill;
  std::string message;

  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    if (arg == "--check") {
      checkOnly = true;
      continue;
    }
    if (arg == "--fb" && i + 1 < argc) {
      framebuffer = argv[++i];
      continue;
    }
    if (arg == "--color" && i + 1 < argc) {
      if (!parseHexColor(argv[++i], &color)) {
        std::fprintf(stderr, "suvos-splash: invalid color, expected #rrggbb\n");
        return 2;
      }
      continue;
    }
    if (arg == "--mode" && i + 1 < argc) {
      const std::string value(argv[++i]);
      if (value == "fill") {
        mode = Mode::Fill;
      } else if (value == "loader") {
        mode = Mode::Loader;
      } else if (value == "crash") {
        mode = Mode::Crash;
      } else {
        std::fprintf(stderr, "suvos-splash: invalid mode, expected fill, loader or crash\n");
        return 2;
      }
      continue;
    }
    if (arg == "--message" && i + 1 < argc) {
      message = argv[++i];
      continue;
    }

    std::fprintf(stderr, "Usage: suvos-splash [--check] [--fb /dev/fb0] [--color #rrggbb] [--mode fill|loader|crash] [--message text]\n");
    return 2;
  }

  return renderFramebuffer(framebuffer, color, checkOnly, mode, message);
}
