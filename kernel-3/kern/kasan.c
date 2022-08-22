#include <vm/vm.h>
#include <libkern/klib.h>

static void
kasan_check(vaddr_t addr, size_t size, bool isStore, void *ip)
{
#if 0
	static bool in_check = false;

	if (in_check || addr < (vaddr_t)KHEAP_BASE ||
	    addr > (vaddr_t)KHEAP_BASE + KHEAP_SIZE)
		return;

	in_check = true;

	if (addr == 0xffff8001005d4f00) {
		kprintf("%p %s(%d) by %p. Old value: %p\n", addr,
		    isStore ? "STORED" : "loaded", size,
		    __builtin_return_address(1), *(void **)addr);

		kprintf(" --> %p\n", __builtin_return_address(2));
		kprintf(" --> %p\n", __builtin_return_address(3));
		kprintf(" --> %p\n", __builtin_return_address(4));
		kprintf(" --> %p\n", __builtin_return_address(5));
	}

	in_check = false;
#endif
}

void
__asan_alloca_poison(uintptr_t addr, size_t size)
{
	(void)addr;
	(void)size;
}

void
__asan_allocas_unpoison(void *stackTop, void *stackBase)
{
	(void)stackTop;
	(void)stackBase;
}

#define _KASAN_HANDLE_FUN(NAME, SIZE, IS_STORE)            \
	void __asan_##NAME##_noabort(uintptr_t addr)       \
	{                                                  \
		kasan_check((vaddr_t)addr, SIZE, IS_STORE, \
		    __builtin_return_address(0));          \
	}

#define KASAN_HANDLE_FUN(SIZE)                            \
	_KASAN_HANDLE_FUN(load##SIZE, SIZE, false)        \
	_KASAN_HANDLE_FUN(report_load##SIZE, SIZE, false) \
	_KASAN_HANDLE_FUN(store##SIZE, SIZE, true)        \
	_KASAN_HANDLE_FUN(report_store##SIZE, SIZE, true)

KASAN_HANDLE_FUN(1);
KASAN_HANDLE_FUN(2);
KASAN_HANDLE_FUN(4);
KASAN_HANDLE_FUN(8);
KASAN_HANDLE_FUN(16);

void
__asan_loadN_noabort(uintptr_t addr, size_t size)
{
	kasan_check((vaddr_t)addr, size, false, __builtin_return_address(0));
}

void
__asan_storeN_noabort(uintptr_t addr, size_t size)
{
	kasan_check((vaddr_t)addr, size, true, __builtin_return_address(0));
}

void
__asan_report_load_n_noabort(uintptr_t addr, size_t size)
{
	kasan_check((vaddr_t)addr, size, true, __builtin_return_address(0));
}

void
__asan_report_store_n_noabort(uintptr_t addr, size_t size)
{
	kasan_check((vaddr_t)addr, size, true, __builtin_return_address(0));
}

void
__asan_handle_no_return()
{
	/* epsilon */
}
