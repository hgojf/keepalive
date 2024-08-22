#ifndef KEEPALIVE_COMPAT_UNISTD_H
#define KEEPALIVE_COMPAT_UNISTD_H
#include_next <unistd.h>

int getdtablecount(void);
#endif /* KEEPALIVE_COMPAT_UNISTD_H */
