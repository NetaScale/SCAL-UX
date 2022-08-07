#ifndef LIMINEFB_H_
#define LIMINEFB_H_

#include "devicekit/DKDevice.h"
#include "kern/vm.h"

struct limine_framebuffer_response;
struct limine_framebuffer;

@interface LimineFB : DKDevice {
	uint64_t width, height, pitch;
	uint16_t bpp;
	vaddr_t	 base;
}

@property (nonatomic) uint64_t width, height, pitch;
@property (nonatomic) uint16_t bpp;
@property (nonatomic) vaddr_t  base;

+ (BOOL)probeWithLimineFBResponse:(struct limine_framebuffer_response *)resp;

- initWithLimineFB:(struct limine_framebuffer *)fb;
@end

extern LimineFB *sysfb;

#endif /* LIMINEFB_H_ */
