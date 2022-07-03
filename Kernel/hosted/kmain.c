#include "../kern/vm.h"

int
main(int argc, char *argv[])
{
	TAILQ_INIT(&kmap.entries);
}
