#include "sys/klibc.h"

int
memcmp(const void *str1, const void *str2, size_t count)
{
	register const unsigned char *c1, *c2;

	c1 = (const unsigned char *)str1;
	c2 = (const unsigned char *)str2;

	while (count-- > 0) {
		if (*c1++ != *c2++)
			return c1[-1] < c2[-1] ? -1 : 1;
	}
	return 0;
}

void *
memcpy(void *restrict dstv, const void *restrict srcv, size_t len)
{
	unsigned char *dst = (unsigned char *)dstv;
	const unsigned char *src = (const unsigned char *)srcv;
	for (size_t i = 0; i < len; i++)
		dst[i] = src[i];
	return dst;
}

void *
memset(void *b, int c, size_t len)
{
	char *ss = b;
	while (len-- > 0)
		*ss++ = c;
	return b;
}

int
strcmp(const char *s1, const char *s2)
{

	while (*s1 == *s2++)
		if (*s1++ == 0)
			return (0);
	return (*(const unsigned char *)s1 - *(const unsigned char *)--s2);
}

size_t
strlen(const char *str)
{
	const char *s;

	for (s = str; *s; ++s)
		continue;
	return (s - str);
}

#undef malloc
void *
malloc(size_t size)
{
	return kmalloc(size);
}