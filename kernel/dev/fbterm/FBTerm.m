#include "FBTerm.h"
#include "amd64.h"
#include "amd64/limine.h"
#include "dev/fbterm/term.h"
#include "kern/vm.h"
#include "posix/dev.h"

extern char sun12x22[], nbsdbold[];

FBTerm *syscon = nil;
static int termnum = 0;

static int fbtwrite(dev_t dev, void *buf, size_t nbyte, off_t off);

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
	cdevsw_t cdev;
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

	if (syscon == nil) {
		syscon = self;
		cdev.is_tty = true;
		cdev.private = self;
		cdev.open = NULL;
		cdev.write = fbtwrite;
		cdevsw_attach(&cdev);
	}

	[self registerDevice];

	return self;
}

- (void)write:(void *)buf len:(size_t)len
{
	term_write(&term, buf, len);
}

- (void)putc:(int)c
{
	term_putchar(&term, c);
	if (c == '\n')
		term_double_buffer_flush(&term);
}

@end

static int
fbtwrite(dev_t dev, void *buf, size_t nbyte, off_t off)
{
	[syscon write:buf len:nbyte];
	return 0;
}

void
sysconputc(int c)
{
	if (syscon) {
		[syscon putc:c];
	}
}
