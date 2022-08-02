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

struct nvme_disk_attach {
	NVMEController		       *controller;
	uint16_t		       nsid;
	struct nvm_identify_namespace *nsident;
};

@interface NVMEDisk : DKDrive <DKDriveMethods> {
	uint16_t		       nsid;
}

- initWithAttachmentInfo:(struct nvme_disk_attach *)info;

@end

#endif /* NVMEDISK_H_ */
