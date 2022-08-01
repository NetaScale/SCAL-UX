#ifndef GPTVOLUMEMANAGER_H_
#define GPTVOLUMEMANAGER_H_

#include "devicekit/DKDevice.h"
#include "devicekit/DKDisk.h"

@interface GPTVolumeManager : DKDevice

+ (BOOL)probe:(DKDisk *)disk;

@end

#endif /* GPTVOLUMEMANAGER_H_ */
