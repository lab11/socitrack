#include "delay.h"
#include "board.h"
#include "stm32f0xx.h"

#define USECOND 1e9
#define MSECOND 1e6

void uDelay(uint32_t u) {
	volatile uint32_t i = 0;
#if BOARD_V == TRIPOINT
	for (i = 0; i < ((float)SystemCoreClock/(float)USECOND * 12)*u; i++) { }
#elif BOARD_V == SQUAREPOINT
	for (i = 0; i < ((float)SystemCoreClock/(float)USECOND * 6)*u; i++) { }
#endif
}

void mDelay(uint32_t m) {
	volatile uint32_t i = 0;
#if BOARD_V == TRIPOINT
	for (i = 0; i < ((float)SystemCoreClock/(float)MSECOND * 12)*m; i++) { }
#elif BOARD_V == SQUAREPOINT
	for (i = 0; i < ((float)SystemCoreClock/(float)MSECOND * 6)*m; i++) { }
#endif
}
