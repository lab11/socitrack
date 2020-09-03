#ifndef __GLOSSY_HEADER_H__
#define __GLOSSY_HEADER_H__

#include "configuration.h"
#include "dw1000.h"

// Defines -------------------------------------------------------------------------------------------------------------

#define CW_CAL_12PF ((3.494350-3.494173)/3.4944*1e6/30)
#define CW_CAL_22PF ((3.494078-3.493998)/3.4944*1e6/30)
#define CW_CAL_33PF ((3.493941-3.493891)/3.4944*1e6/30)

#define GLOSSY_MAX_DEPTH                 2
#define GLOSSY_FLOOD_INTERVAL_US         2e3


// Public functions ----------------------------------------------------------------------------------------------------

void glossy_init(void);
void glossy_reset_clock(void);
void glossy_reset_state(void);
void glossy_fix_clock(double ppm_clock_offset);
bool glossy_transmit_packet(uint32_t transmit_time_dw, uint8_t* packet, uint16_t packet_size, bool flood_packet);
bool glossy_txcallback(void);
void glossy_rxcallback(uint32_t receive_time_dw, uint8_t *packet, uint16_t packet_size);


#endif // __GLOSSY_HEADER_H__
