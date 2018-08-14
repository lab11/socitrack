#ifndef __BOARD_H
#define __BOARD_H

// FIXME: Uncomment when done with debugging
#define BOARD_V 2

// Board versions
#define TRIPOINT    1
#define SQUAREPOINT 2


#ifdef BOARD_V

#if (BOARD_V == TRIPOINT)
// TriPoint
#include "tripoint.h"
#elif (BOARD_V == SQUAREPOINT)
// SquarePoint
#include "squarepoint.h"
#else
#error "UNKNOWN BOARD VERSION"
#endif

#else
#error "MUST #define BOARD_V"
#endif

#endif /* __BOARD_H */
