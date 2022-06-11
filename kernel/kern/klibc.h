#ifndef KLIBC_H_
#define KLIBC_H_

#include <string.h>

#include "kern/kern.h"
#include "kern/liballoc.h"

#define fflush(...)
#define printf(...) kprintf(__VA_ARGS__)
#define fprintf(file, ...) kprintf(__VA_ARGS__)
#define vfprintf(file, ...) kvpprintf(__VA_ARGS__)

#define abort() fatal("abort!")

#define malloc kmalloc
#define calloc kcalloc
#define free kfree
#define realloc krealloc

#endif /* KLIBC_H_ */
