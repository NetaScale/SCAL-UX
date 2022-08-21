#ifndef BOOT_H_
#define BOOT_H_

#include "limine.h"

extern volatile struct limine_framebuffer_request framebuffer_request;
extern volatile struct limine_rsdp_request	  rsdp_request;
extern volatile struct limine_terminal_request	  terminal_request;

#endif /* BOOT_H_ */
