#pragma once
// Process-wide trace log. The loader opens it next to the ASI; every stage
// (config, installs, hook bodies) appends timestamped lines so a live run can
// be audited against the build without re-deriving the patch set.
//
// Line shape (stable, key=value):
//   2026-10-16 04:43:23.123 [seq=000123 tid=1a2b I] phase=config key=SkipIntro raw="1" enabled=1 source=ini
// The bracket prefix carries the wall clock, the sequence number, the thread
// id and the level letter (S=summary, I=decisions, V=verbose). Everything the
// templates in tools/phase3 + tools/asi_trace consume is `key=value` tokens.
//
// Levels: 0 off, 1 summary, 2 decisions (default), 3 verbose. The level comes
// from the non-original [Debug] LogLevel INI key (documented in
// docs/LOGGING.md); the original has no such key, so it never affects parity.
//
// Thread-safe: the hook bodies run on the game's threads, the loader on its
// own, and the log file stays open for the whole process lifetime. Output is
// buffered; log_flush() forces the tail out (called at end of load, before
// faults and on close), and every summary line flushes.

#include <cstddef>
#include <cstdint>

namespace zpatch {

// Opens (truncating) the log file. Called once from DllMain; later calls are
// ignored. Without a path every log call is a no-op.
void log_open(const char* path) noexcept;
void log_close() noexcept;

// 0 off, 1 summary, 2 decisions (default), 3 verbose. Values are clamped.
void log_set_level(int level) noexcept;
int log_level() noexcept;

// Appends one timestamped line at the decisions level.
void log_printf(const char* format, ...) noexcept;

// Summary lines (level >= 1). Flushed immediately.
void log_summary(const char* format, ...) noexcept;

// Verbose lines (level >= 3): hot-path detail that would flood the default log.
void log_verbose(const char* format, ...) noexcept;

// Rate-limited decisions line from a hot path: the first `first` calls and
// then every `every`th log; the counter lives at `counter` so each call site
// has its own.
void log_rate(std::uint32_t* counter, std::uint32_t first, std::uint32_t every,
              const char* format, ...) noexcept;

// Appends `label option=... va=XXXXXXXX len=N bytes=AA BB ...` with the same
// timestamped prefix.
void log_write(const char* label, const char* option, std::uintptr_t va, const void* bytes,
               std::size_t length) noexcept;

// Forces the buffered tail to disk.
void log_flush() noexcept;

}  // namespace zpatch
