#ifndef KLIBC_H_
#define KLIBC_H_

#include "liballoc.h"
#include "vxkern.h"

#define fflush(...)
#define printf(...) kprintf(__VA_ARGS__)
#define fprintf(file, ...) kprintf(__VA_ARGS__)
#define vfprintf(file, ...) kvpprintf(__VA_ARGS__)

#define abort() fatal("abort!")

#define malloc kmalloc
#define calloc kcalloc
#define free kfree
#define realloc krealloc

int memcmp(const void *str1, const void *str2, size_t count);
void *memcpy(void *restrict dstv, const void *restrict srcv, size_t len);
void *memset(void *b, int c, size_t len);

int strcmp(const char *s1, const char *s2);
size_t strlen(const char *str);

#endif /* KLIBC_H_ */
