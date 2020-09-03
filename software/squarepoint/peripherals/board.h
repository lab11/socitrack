#ifndef __BOARD_H
#define __BOARD_H

// Board versions
#define TRIPOINT    1
#define SQUAREPOINT 2

#ifdef BOARD_V
   #if (BOARD_V == TRIPOINT)
   #include "tripoint.h"
   #elif (BOARD_V == SQUAREPOINT)
   #include "squarepoint.h"
   #else
   #error "UNKNOWN BOARD VERSION"
   #endif
#else
#error "MUST #define BOARD_V"
#endif

#endif // __BOARD_H
