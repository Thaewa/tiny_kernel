/* osGpio.c */

#include <osGpio.h>


#define GPIOAEN			(1U<<0)//  0b 0000 0000 0000 0000 0000 0000 0000 0001
#define LED_PIN			(1U<<5)

void gpio_init(void)
{
	/*Enable clock access to led port (Port A)*/

	//Example of 'friendly' programming
	// Initial state =    0b 0000 0000 0000 0000 1100 0000 0000 0000
	//Set bit0 = (1u<<0)= 0b 0000 0000 0000 0000 0000 0000 0000 0001
	//final state =  Initial state OR Set bit0  =  0b 0000 0000 0000 0000 1100 0000 0000 0001

	RCC->AHB1ENR  |= GPIOAEN;

	/* Set PA4 as output */
	GPIOA->MODER |=  (1U << (4*2));   // set bit 8
	GPIOA->MODER &= ~(1U << (4*2+1)); // clear bit 9

	/* Set PA5 as output */
	GPIOA->MODER |=  (1U << (5*2));   // set bit 10
	GPIOA->MODER &= ~(1U << (5*2+1)); // clear bit 11

	/* Set PA6 as output */
	GPIOA->MODER |=  (1U << (6*2));   // set bit 12
	GPIOA->MODER &= ~(1U << (6*2+1)); // clear bit 13

	/* Set PA7 as output */
	GPIOA->MODER |=  (1U << (7*2));   // set bit 14
	GPIOA->MODER &= ~(1U << (7*2+1)); // clear bit 15

}


