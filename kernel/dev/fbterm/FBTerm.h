#ifndef FBTERM_H_
#define FBTERM_H_

#ifdef __OBJC__
#include "../LimineFB.h"
#include "devicekit/DKDevice.h"
#include "term.h"
#include "posix/tty.h"

@interface FBTerm : DKDevice {
	LimineFB *_fb;
	tty_t tty;
	struct framebuffer_t frm;
	struct term_t term;
	struct style_t style;
	struct image_t img;
	struct background_t back;
	struct font_t font;
}

+ (BOOL)probeWithFB:(LimineFB *)fb;

- initWithFB:(LimineFB *)fb;

- (void)putc:(int)c;

@end
#endif

void sysconputc(int c);

#endif /* FBTERM_H_ */
