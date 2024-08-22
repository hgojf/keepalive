#ifndef KEEPALIVE_COMPAT_LIMITS_H
#define KEEPALIVE_COMPAT_LIMITS_H
#include_next <limits.h>

#ifndef IOV_MAX
#define IOV_MAX 16
#endif /* IOV_MAX */
#endif /* KEEPALIVE_COMPAT_LIMITS_H */
