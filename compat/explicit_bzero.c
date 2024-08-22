#include <strings.h>

void
explicit_bzero(void *b, size_t len)
{
	/* sorry */
	bzero(b, len);
}
