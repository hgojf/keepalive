#include <stdlib.h>
#include <string.h>

void
freezero(void *p, size_t n)
{
	if (p != NULL)
		explicit_bzero(p, n);
}
