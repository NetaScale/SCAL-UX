#ifndef INTR_H_
#define INTR_H_

#include <stdint.h>

void idt_init();
void idt_load();

void lapic_enable(uint8_t spurvec);

#endif /* INTR_H_ */
