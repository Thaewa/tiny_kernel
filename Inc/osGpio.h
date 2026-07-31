#ifndef __LED_H__
#define __LED_H__

#include "stm32f4xx.h"

#if 0
void led_init(void);
void led_on(void);
void led_off(void);
#endif

void gpio_init (void);

#define PIN_SET(pin)	(GPIOA->BSRR = (1U<<(pin)))
#define PIN_RESET(pin)	(GPIOA->BSRR = (1U<<((pin) + 16)))


#endif
