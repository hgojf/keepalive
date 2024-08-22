#ifndef KEEPALIVE_COMPAT_STRING_H
#define KEEPALIVE_COMPAT_STRING_H
#include_next <string.h>

void explicit_bzero(void *, size_t);
#endif /* KEEPALIVE_COMPAT_STRING_H */
