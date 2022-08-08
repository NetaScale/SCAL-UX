#include <x86_64/limine.h>

#include <libkern/klib.h>
#include <stddef.h>
#include <stdint.h>

void idt_init();
void idt_load();

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent.

static volatile struct limine_terminal_request terminal_request = {
	.id = LIMINE_TERMINAL_REQUEST,
	.revision = 0
};

void
md_kputc(int ch, void *ctx)
{
	struct limine_terminal *terminal =
	    terminal_request.response->terminals[0];
	terminal_request.response->write(terminal, (const char *)&ch, 1);
}

static void
done(void)
{
	for (;;) {
		__asm__("hlt");
	}
}

// The following will be our kernel's entry point.
void
_start(void)
{
	// Ensure we got a terminal
	if (terminal_request.response == NULL ||
	    terminal_request.response->terminal_count < 1) {
		done();
	}

	kprintf("The SCAL/UX Operating System\n");

	idt_init();
	idt_load();

	*(double *)11 = 48000000.12f;

	// We're done, just hang...
	done();
}
