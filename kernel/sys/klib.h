#ifndef KLIB_H_
#define KLIB_H_

#include <sys/types.h>

int memcmp(const void *str1, const void *str2, size_t count);
void *memcpy(void *restrict dstv, const void *restrict srcv, size_t len);
void *memset(void *b, int c, size_t len);

int strcmp(const char *s1, const char *s2);
char *strcpy(char *restrict dst, const char *restrict src);
char *strdup(const char *src);
size_t strlen(const char *str);

#endif /* KLIB_H_ */
