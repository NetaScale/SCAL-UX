#include "dev.h"

cdevsw_t cdevsw[64];

int
cdevsw_attach(cdevsw_t *bindings)
{
	for (int i = 0; i < 64; i++)
		if (!cdevsw[i].valid) {
			cdevsw[i] = *bindings;
			cdevsw[i].valid = true;
			return i;
		}

	/* out of majors */
	return -1;
}
