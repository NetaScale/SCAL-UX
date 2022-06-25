#include "amd64/limine.h"
#include "kern/vm.h"
#include "term.h"

extern char sun12x22[], nbsdbold[];

struct framebuffer_t frm;
struct background_t back;
struct term_t term;
struct style_t style = { DEFAULT_ANSI_COLOURS, DEFAULT_ANSI_BRIGHT_COLOURS,
	0xA0000000, 0xFFFFFF, 64, 0 };
struct background_t back = { NULL, STRETCHED, 0x00000000 };
struct font_t font;

void
fbterm_init(struct limine_framebuffer *limfb)
{
	frm = (struct framebuffer_t) { .address = (uintptr_t)limfb->address,
		.width = limfb->width,
		.height = limfb->height,
		.pitch = limfb->pitch };
	font = (struct font_t) {
		.address = (uintptr_t)nbsdbold,
		.spacing = 1,
		.scale_x = 1,
		.scale_y = 1,
	};
	term_init(&term, NULL, false);
	term_vbe(&term, frm, font, style, back);
	term_print(&term, "hello from FBTerm\n");
}
