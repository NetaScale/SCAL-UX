#include "FBTerm.h"
#include "amd64.h"
#include "amd64/limine.h"
#include "dev/fbterm/term.h"
#include "kern/vm.h"
#include "posix/dev.h"
#include "posix/tty.h"
#include "posix/vfs.h"

extern char sun12x22[], nbsdbold[];

FBTerm *syscon = nil;
static int termnum = 0;

static int fbtopen(dev_t dev, int mode, struct posix_proc *proc);
static int fbtputch(void *data, int ch);

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

	tty.termios.c_cc[VINTR] = 0x03;
	tty.termios.c_cc[VEOL] = '\n';
	tty.termios.c_cc[VEOF] = '\0';
	tty.termios.c_cflag = TTYDEF_CFLAG;
	tty.termios.c_iflag = TTYDEF_IFLAG;
	tty.termios.c_lflag = TTYDEF_LFLAG;
	tty.termios.c_oflag = TTYDEF_OFLAG;
	tty.termios.ibaud = tty.termios.obaud = TTYDEF_SPEED;
	tty.putch = fbtputch;
	tty.data = self;

	ksnprintf(name, sizeof name, "FBTerm%d", termnum++);

	term_init(&term, NULL, false);
	term_vbe(&term, frm, font, style, back);

	if (syscon == nil) {
		int maj;
		vnode_t *node;
		syscon = self;
		cdev.is_tty = true;
		cdev.private = self;
		cdev.open = fbtopen;
		cdev.write = tty_write;
		maj = cdevsw_attach(&cdev);
		assert(root_dev->ops->mknod(root_dev, &node, "console",
			   makedev(maj, 0)) == 0);
	}

	[self registerDevice];

	return self;
}

- (tty_t *)tty
{
	return &tty;
}

- (void)input:(int)c
{
	tty_input(&tty, c);
}

- (void)write:(void *)buf len:(size_t)len
{
	term_write(&term, (uint64_t)buf, len);
}

- (void)putc:(int)c
{
	term_putchar(&term, c);
	if (c == '\n')
		term_double_buffer_flush(&term);
}

- (void)flush
{
	term_double_buffer_flush(&term);
}

@end

static int
fbtopen(dev_t dev, int mode, struct posix_proc *proc)
{
	return 0;
}

static int
fbtputch(void *data, int c)
{
	[(FBTerm *)data putc:c];
	[(FBTerm *)data flush];
	return 0;
}

void
sysconputc(int c)
{
	if (syscon) {
		[syscon putc:c];
	}
}

void
sysconflush()
{
	[syscon flush];
}
