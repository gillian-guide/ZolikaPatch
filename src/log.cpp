#include "zpatch/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <ctime>
#endif

namespace {

std::mutex& log_mutex() noexcept {
  static std::mutex mutex;
  return mutex;
}

std::FILE*& log_file() noexcept {
  static std::FILE* file = nullptr;
  return file;
}

std::uint32_t& log_sequence() noexcept {
  static std::uint32_t sequence = 0;
  return sequence;
}

int& log_level_state() noexcept {
  static int level = 2;  // decisions
  return level;
}

std::uint32_t& log_unflushed() noexcept {
  static std::uint32_t lines = 0;
  return lines;
}

// Local time + milliseconds for the line prefix: `2026-10-16 04:43:23.123`.
void format_timestamp(char* out, std::size_t size) noexcept {
#if defined(_WIN32)
  SYSTEMTIME st{};
  GetLocalTime(&st);
  std::snprintf(out, size, "%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear, st.wMonth,
                st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
#else
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  localtime_s(&tm, &now);
  std::snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d.000", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
#endif
}

std::uint32_t current_thread_id() noexcept {
#if defined(_WIN32)
  return static_cast<std::uint32_t>(GetCurrentThreadId());
#else
  return 0;
#endif
}

char level_letter(int level) noexcept {
  switch (level) {
    case 1: return 'S';
    case 3: return 'V';
    default: return 'I';
  }
}

void log_vprintf(int level, const char* format, va_list args) noexcept {
  if (!format) return;
  std::lock_guard<std::mutex> lock(log_mutex());
  std::FILE* file = log_file();
  if (!file) return;
  if (level > log_level_state()) return;
  char buffer[2048];
  char stamp[32];
  format_timestamp(stamp, sizeof(stamp));
  const int prefix = std::snprintf(
      buffer, sizeof(buffer), "%s [seq=%06u tid=%04x %c] ", stamp, ++log_sequence(),
      current_thread_id(), level_letter(level));
  if (prefix < 0 || static_cast<std::size_t>(prefix) >= sizeof(buffer)) return;
  std::vsnprintf(buffer + prefix, sizeof(buffer) - static_cast<std::size_t>(prefix), format,
                 args);
  std::fputs(buffer, file);
  std::fputc('\n', file);
  // Buffered output: summary lines and every 64th line flush, so a crash keeps
  // the recent tail while hot paths do not pay an fflush per line.
  if (level <= 1 || ++log_unflushed() >= 64) {
    std::fflush(file);
    log_unflushed() = 0;
  }
}

}  // namespace

namespace zpatch {

void log_open(const char* path) noexcept {
  if (!path || !*path) return;
  std::lock_guard<std::mutex> lock(log_mutex());
  if (log_file()) return;
  log_file() = std::fopen(path, "w");
  if (log_file()) {
    // 256 KiB buffer: the load writes hundreds of decision lines; batching keeps
    // the load fast while log_flush()/summary lines bound what a crash can lose.
    std::setvbuf(log_file(), nullptr, _IOFBF, 1u << 18);
  }
}

void log_close() noexcept {
  std::lock_guard<std::mutex> lock(log_mutex());
  if (log_file()) {
    std::fflush(log_file());
    std::fclose(log_file());
    log_file() = nullptr;
  }
}

void log_set_level(int level) noexcept {
  if (level < 0) level = 0;
  if (level > 3) level = 3;
  log_level_state() = level;
}

int log_level() noexcept { return log_level_state(); }

void log_printf(const char* format, ...) noexcept {
  va_list args;
  va_start(args, format);
  log_vprintf(2, format, args);
  va_end(args);
}

void log_summary(const char* format, ...) noexcept {
  va_list args;
  va_start(args, format);
  log_vprintf(1, format, args);
  va_end(args);
}

void log_verbose(const char* format, ...) noexcept {
  va_list args;
  va_start(args, format);
  log_vprintf(3, format, args);
  va_end(args);
}

void log_rate(std::uint32_t* counter, std::uint32_t first, std::uint32_t every,
              const char* format, ...) noexcept {
  if (!counter || every == 0u) return;
  const std::uint32_t hits = ++*counter;
  if (hits > first && (hits - first) % every != 0u) return;
  va_list args;
  va_start(args, format);
  log_vprintf(2, format, args);
  va_end(args);
}

void log_write(const char* label, const char* option, std::uintptr_t va, const void* bytes,
               std::size_t length) noexcept {
  char hex[3 * 64 + 1];
  hex[0] = '\0';
  const auto* data = static_cast<const unsigned char*>(bytes);
  const std::size_t shown = length < 64 ? length : 64;
  std::size_t cursor = 0;
  for (std::size_t i = 0; i < shown && cursor + 4 <= sizeof(hex); ++i) {
    const int written = std::snprintf(hex + cursor, sizeof(hex) - cursor, "%s%02X",
                                      i ? " " : "", data[i]);
    if (written <= 0) break;
    cursor += static_cast<std::size_t>(written);
  }
  log_printf("%s option=%s va=%08zX len=%zu bytes=%s", label, option ? option : "?",
             static_cast<std::size_t>(va), length, hex);
}

void log_flush() noexcept {
  std::lock_guard<std::mutex> lock(log_mutex());
  if (log_file()) {
    std::fflush(log_file());
    log_unflushed() = 0;
  }
}

}  // namespace zpatch
