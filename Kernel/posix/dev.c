/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

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
