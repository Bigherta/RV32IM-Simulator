#pragma once
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Host-only end-of-run metrics consumed by the active benchmark tables.
namespace debug {
enum Topic : unsigned {
  TOPIC_NONE = 0,
  TOPIC_CLOCK = 1u << 0,
  TOPIC_BRANCH = 1u << 1,
  TOPIC_ICACHE = 1u << 2,
  TOPIC_ALL = TOPIC_CLOCK | TOPIC_BRANCH | TOPIC_ICACHE,
};

inline unsigned parseVerbose(const char *env) {
  if (env == nullptr || env[0] == '\0' || env[0] == '0')
    return TOPIC_NONE;
  if (strcmp(env, "1") == 0 || strcmp(env, "all") == 0)
    return TOPIC_ALL;
  unsigned mask = TOPIC_NONE;
  const char *p = env;
  while (true) {
    const char *end = strchr(p, ',');
    const auto len = end ? end - p : static_cast<std::ptrdiff_t>(strlen(p));
    if (len == 5 && strncmp(p, "clock", 5) == 0)
      mask |= TOPIC_CLOCK;
    else if (len == 6 && strncmp(p, "branch", 6) == 0)
      mask |= TOPIC_BRANCH;
    else if (len == 6 && strncmp(p, "icache", 6) == 0)
      mask |= TOPIC_ICACHE;
    else if (len == 6 && strncmp(p, "dcache", 6) == 0)
      mask |= TOPIC_ICACHE;
    if (end == nullptr)
      break;
    p = end + 1;
  }
  return mask;
}

inline bool enabled(Topic topic) {
  static const unsigned mask = parseVerbose(std::getenv("VERBOSE"));
  return (mask & topic) != 0;
}

inline void print(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
}
} // namespace debug
