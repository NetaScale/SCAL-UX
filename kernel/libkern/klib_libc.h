#ifndef KLIB_LIBC_H_
#define KLIB_LIBC_H_

#include <kern/liballoc.h>

#include "klib.h"

#define fflush(...)
#define printf(...) kprintf(__VA_ARGS__)
#define fprintf(file, ...) kprintf(__VA_ARGS__)
#define vfprintf(file, ...) kvpprintf(__VA_ARGS__)

#define abort() fatal("abort!")

#define malloc kmalloc
#define calloc kcalloc
#define free kfree
#define realloc krealloc

#endif /* KLIB_LIBC_H_ */
