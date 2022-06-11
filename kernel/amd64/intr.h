#ifndef INTR_H_
#define INTR_H_

#include <stdint.h>

void idt_init();
void idt_load();

void lapic_enable(uint8_t spurvec);
void lapic_eoi();
uint32_t lapic_timer_calibrate();

#endif /* INTR_H_ */
