#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <linux/fb.h>
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

int fillFramebuffer(const char *path, const Color &color, bool checkOnly) {
  int fd = open(path, O_RDWR);
  if (fd < 0) {
    std::fprintf(stderr, "suvos-splash: framebuffer unavailable: %s: %s\n", path, std::strerror(errno));
    return 0;
  }

  fb_fix_screeninfo fixed {};
  fb_var_screeninfo var {};
  if (ioctl(fd, FBIOGET_FSCREENINFO, &fixed) != 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0) {
    std::fprintf(stderr, "suvos-splash: framebuffer info unavailable: %s\n", std::strerror(errno));
    close(fd);
    return 0;
  }

  std::printf(
    "suvos-splash: %ux%u %ubpp line=%u memory=%u\n",
    var.xres,
    var.yres,
    var.bits_per_pixel,
    fixed.line_length,
    fixed.smem_len
  );

  if (checkOnly) {
    close(fd);
    return 0;
  }

  if (var.bits_per_pixel != 8 && var.bits_per_pixel != 16 && var.bits_per_pixel != 24 && var.bits_per_pixel != 32) {
    std::fprintf(stderr, "suvos-splash: unsupported framebuffer depth: %u\n", var.bits_per_pixel);
    close(fd);
    return 0;
  }

  void *memory = mmap(nullptr, fixed.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (memory == MAP_FAILED) {
    std::fprintf(stderr, "suvos-splash: mmap failed: %s\n", std::strerror(errno));
    close(fd);
    return 0;
  }

  const uint32_t packed = packColor(var, color);
  const uint32_t bytesPerPixel = var.bits_per_pixel / 8U;
  auto *base = static_cast<uint8_t *>(memory);

  for (uint32_t y = 0; y < var.yres; ++y) {
    uint8_t *row = base + y * fixed.line_length;
    for (uint32_t x = 0; x < var.xres; ++x) {
      writePixel(row + x * bytesPerPixel, var.bits_per_pixel, packed);
    }
  }

  msync(memory, fixed.smem_len, MS_SYNC);
  munmap(memory, fixed.smem_len);
  close(fd);
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  const char *framebuffer = "/dev/fb0";
  Color color;
  bool checkOnly = false;

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

    std::fprintf(stderr, "Usage: suvos-splash [--check] [--fb /dev/fb0] [--color #rrggbb]\n");
    return 2;
  }

  return fillFramebuffer(framebuffer, color, checkOnly);
}
