#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace debug {

enum Topic : unsigned {
  TOPIC_NONE = 0,
  TOPIC_ISSUE = 1u << 0,
  TOPIC_EXEC = 1u << 1,
  TOPIC_WB = 1u << 2,
  TOPIC_COMMIT = 1u << 3,
  TOPIC_LSQ = 1u << 4,
  TOPIC_MEM = 1u << 5,
  TOPIC_CLOCK = 1u << 6,
  TOPIC_BRANCH = 1u << 7,
  TOPIC_PRF = 1u << 8,
  TOPIC_MDP = 1u << 9,
  TOPIC_BPMISS = 1u << 10,
  // Cache-profile dump. Named TOPIC_ICACHE to match the main tree's debug
  // facade: the block reports BOTH the ICache and the DCache, so the two trees
  // must answer to the same VERBOSE token. Both "icache" and the historical
  // "dcache" spelling are accepted below (see parseVerbose).
  TOPIC_ICACHE = 1u << 11,
  TOPIC_ALL = 0xFFFFFFFFu,
};

static unsigned parseVerbose(const char *env) {
  if (env == nullptr || env[0] == '\0' || env[0] == '0')
    return TOPIC_NONE;
  if (strcmp(env, "1") == 0 || strcmp(env, "all") == 0)
    return TOPIC_ALL;
  unsigned mask = TOPIC_NONE;
  const char *p = env;
  while (true) {
    const char *end = strchr(p, ',');
    int len = end ? (int)(end - p) : (int)strlen(p);
    if (len == 5 && strncmp(p, "issue", 5) == 0)
      mask |= TOPIC_ISSUE;
    else if (len == 4 && strncmp(p, "exec", 4) == 0)
      mask |= TOPIC_EXEC;
    else if (len == 2 && strncmp(p, "wb", 2) == 0)
      mask |= TOPIC_WB;
    else if (len == 6 && strncmp(p, "commit", 6) == 0)
      mask |= TOPIC_COMMIT;
    else if (len == 3 && strncmp(p, "lsq", 3) == 0)
      mask |= TOPIC_LSQ;
    else if (len == 3 && strncmp(p, "mem", 3) == 0)
      mask |= TOPIC_MEM;
    else if (len == 5 && strncmp(p, "clock", 5) == 0)
      mask |= TOPIC_CLOCK;
    else if (len == 6 && strncmp(p, "branch", 6) == 0)
      mask |= TOPIC_BRANCH;
    else if (len == 3 && strncmp(p, "prf", 3) == 0)
      mask |= TOPIC_PRF;
    else if (len == 3 && strncmp(p, "mdp", 3) == 0)
      mask |= TOPIC_MDP;
    else if (len == 6 && strncmp(p, "bpmiss", 6) == 0)
      mask |= TOPIC_BPMISS;
    else if (len == 6 && strncmp(p, "icache", 6) == 0)
      mask |= TOPIC_ICACHE;
    else if (len == 6 && strncmp(p, "dcache", 6) == 0)
      mask |= TOPIC_ICACHE; // legacy spelling, same cache-profile dump
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

