/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
/*
 * Copyright 2020-2022 NetaScale Systems Ltd.
 * All rights reserved.
 */

#ifndef NVMEDISK_H_
#define NVMEDISK_H_

#include "devicekit/DKDisk.h"

@class NVMEController;

struct nvme_disk_probe {
	NVMEController *controller;
};

@interface NVMEDisk : DKDisk {
}

+ probe:(NVMEController *)controller;

@end

#endif /* NVMEDISK_H_ */
