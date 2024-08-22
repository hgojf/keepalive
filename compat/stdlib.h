#ifndef KEEPALIVE_COMPAT_STDLIB_H
#define KEEPALIVE_COMPAT_STDLIB_H
#include_next <stdlib.h>

void freezero(void *, size_t);
void *reallocarray(void *, size_t, size_t);
void *recallocarray(void *, size_t, size_t, size_t);
long long strtonum(const char *, long long, long long, const char **);
#endif /* KEEPALIVE_COMPAT_STDLIB_H */
