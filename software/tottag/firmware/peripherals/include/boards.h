#ifndef __BOARDS_H
#define __BOARDS_H

#ifndef BOARD_TOTTAG
#define BOARD_TOTTAG
#endif

// Include board file
#if defined(BOARD_TRITAG)
	#include "tritag.h"
#elif defined(BOARD_TOTTAG)
    #include "tottag.h"
#else
	#error "Board is not defined in boards.h"
#endif

#endif // #ifndef __BOARDS_H
