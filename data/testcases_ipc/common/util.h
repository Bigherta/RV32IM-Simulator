#ifndef RISCV_BENCHMARKS_UTIL_H
#define RISCV_BENCHMARKS_UTIL_H

#include <stdint.h>
#include <stdio.h>
#include "amf.h"

#ifndef HOST_DEBUG
#define HOST_DEBUG 0
#endif

#ifndef PREALLOCATE
#define PREALLOCATE 0
#endif

#ifndef static_assert
#define static_assert(cond) _Static_assert((cond), "benchmark assertion")
#endif

static inline void setStats(int enable) { (void)enable; }

static inline void printArray(const char *name, int n, const int *arr)
{
  (void)name;
  (void)n;
  (void)arr;
}

static inline int verify(int n, const volatile int *test, const int *expected)
{
  for (int i = 0; i < n; ++i)
    if (test[i] != expected[i])
      return i + 1;
  return 0;
}

#endif
