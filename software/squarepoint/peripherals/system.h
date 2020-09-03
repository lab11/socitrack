#ifndef __SYSTEM_HEADER_H__
#define __SYSTEM_HEADER_H__

// Includes ------------------------------------------------------------------------------------------------------------

#include <stdint.h>

// Definitions and typedefs --------------------------------------------------------------------------------------------

#define TRUE  1
#define FALSE 0
typedef uint8_t bool;

#define MIN(_a, _b) (((_a) < (_b)) ? (_a) : (_b))
#define MAX(_a, _b) (((_a) > (_b)) ? (_a) : (_b))

// Public functions ----------------------------------------------------------------------------------------------------

bool hw_init(void);
void hw_stop_chip(void);
bool hw_restart_chip(void);

#endif // __SYSTEM_HEADER_H__
