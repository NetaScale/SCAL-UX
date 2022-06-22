#include <string.h>

#include "vm.h"

int
test_vm()
{
	vaddr_t addr1 = VADDR_MAX, addr2 = VADDR_MAX;
	vm_object_t *obj;

	kprintf("-------\ntesting anonymous COW\n");

	vm_allocate(kmap, &obj, &addr1, 0x1000, false);

	kprintf("initial write to first:\n");
	strcpy(addr1, "Hello, world\n");

	kprintf("mapping a second one\n");
	vm_map_object(kmap, obj, &addr2, 0x1000, true);

	kprintf("reading from second\n");
	int x = *(char *)addr2;
	(void)x;
	kprintf(" =%s", (char *)addr2);

	kprintf("now altering second\n");
	*(char *)addr2 = 'J';

	kprintf("reading first, should start 'H'\n");
	kprintf("  =%s\n", (char *)addr1);

	kprintf("changing first\n");
	*(char *)addr1 = 'M';

	kprintf("reading second, should start 'J'\n");
	kprintf(" =%s\n", (char *)addr2);

	kprintf("done\n------\n");
}
