#ifndef DKLIMINEFB_H_
#define DKLIMINEFB_H_

#include "DKDevice.h"
#include "amd64/limine.h"
#include "kern/vm.h"

@interface DKLimineFB : DKDevice
{
    uint64_t width, height, pitch;
    uint16_t bpp;
    vaddr_t base;
}

@property (nonatomic) uint64_t width, height, pitch;
@property (nonatomic) uint16_t bpp;
@property (nonatomic) vaddr_t base;

+ (BOOL)probeWithLimineFBResponse:(struct limine_framebuffer_response *)resp;

- initWithLimineFB:(struct limine_framebuffer *)fb;
@end

#endif /* DKLIMINEFB_H_ */
