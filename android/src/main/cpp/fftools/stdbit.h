#ifndef COMPAT_STDBIT_H
#define COMPAT_STDBIT_H

#include <stdint.h>

// Compatibility layer for C23 stdbit.h functions

static inline unsigned int stdc_count_ones(unsigned int x) {
  unsigned int count = 0;
  while (x) {
    count += x & 1;
    x >>= 1;
  }
  return count;
}

static inline unsigned int stdc_trailing_zeros(unsigned int x) {
  if (x == 0)
    return 32;
  unsigned int count = 0;
  while ((x & 1) == 0) {
    x >>= 1;
    count++;
  }
  return count;
}

#endif /* COMPAT_STDBIT_H */
