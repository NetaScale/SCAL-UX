#include "FBTerm.h"
#include "amd64/limine.h"
#include "dev/fbterm/term.h"
#include "amd64.h"
#include "kern/vm.h"

extern char sun12x22[], nbsdbold[];

FBTerm *syscon = nil;
static int termnum = 0;

@implementation FBTerm

+ (BOOL)probeWithFB:(LimineFB *)fb
{
	if (syscon == nil) {
		syscon = [[self alloc] initWithFB:fb];
		return YES;
	}
	return NO;
}

- (id)initWithFB:(LimineFB *)fb
{
	self = [super init];

	parent = nil;
	_fb = fb;
	style = (struct style_t) { DEFAULT_ANSI_COLOURS,
		DEFAULT_ANSI_BRIGHT_COLOURS, 0xFFFFFFFF, 0x00000000, 40, 0 };
	back = (struct background_t) { NULL, 0, 0x00000000 };
	frm = (struct framebuffer_t) { .address = (uintptr_t)fb.base,
		.width = fb.width,
		.height = fb.height,
		.pitch = fb.pitch };
	font = (struct font_t) {
		.address = (uintptr_t)nbsdbold,
		.spacing = 1,
		.scale_x = 1,
		.scale_y = 1,
	};

	ksnprintf(name, sizeof name, "FBTerm%d", termnum++);

	term_init(&term, NULL, false);
	term_vbe(&term, frm, font, style, back);
	term_print(&term, "hello from FBTerm\n");

	if (syscon == nil)
		syscon = self;

	[self registerDevice];

	return self;
}

- (void)putc:(int)c
{
	term_putchar(&term, c);
	if (c == '\n')
		term_double_buffer_flush(&term);
}

@end

void
sysconputc(int c)
{
	if (syscon) {
		[syscon putc: c];
	}
}
