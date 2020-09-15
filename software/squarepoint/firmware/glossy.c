#include <math.h>
#include <stddef.h>
#include "configuration.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "dw1000.h"
#include "glossy.h"
#include "scheduler.h"

// Application state ---------------------------------------------------------------------------------------------------

static uint8_t _xtal_trim;
static uint32_t _transmit_time;
static uint8_t _current_glossy_depth;
static bool _currently_transmitting;


// Public functions ----------------------------------------------------------------------------------------------------

void glossy_init(void)
{
   // Initialize Glossy application state
   _transmit_time = 0;
   _current_glossy_depth = 0;
   _currently_transmitting = FALSE;

   // Set Glossy clock
   _xtal_trim = DW1000_DEFAULT_XTALTRIM;
   dwt_setxtaltrim(DW1000_DEFAULT_XTALTRIM);
}

void glossy_reset_clock(void)
{
   // Slowly crystal trim to mid-range
   if (_xtal_trim != DW1000_DEFAULT_XTALTRIM)
   {
      if (_xtal_trim > DW1000_DEFAULT_XTALTRIM)
         --_xtal_trim;
      else
         ++_xtal_trim;
      dwt_setxtaltrim(_xtal_trim);
   }
}

void glossy_reset_state(void)
{
   // Re-initialize Glossy state
   _current_glossy_depth = 0;
   _currently_transmitting = FALSE;
}

void glossy_fix_clock(double ppm_clock_offset)
{
   // Update the DW1000's crystal trim to account for the observed PPM offset
   if ((ppm_clock_offset > CW_CAL_12PF) && (_xtal_trim > 1))
   {
      --_xtal_trim;
      dwt_setxtaltrim(_xtal_trim);
   }
   else if ((ppm_clock_offset < -CW_CAL_12PF) && (_xtal_trim < 31))
   {
      ++_xtal_trim;
      dwt_setxtaltrim(_xtal_trim);
   }
}

bool glossy_transmit_packet(uint32_t transmit_time_dw, uint8_t* packet, uint16_t packet_size, bool flood_packet)
{
   // Set Glossy flooding state active
   _currently_transmitting = flood_packet;
   _current_glossy_depth = 0;
   _transmit_time = transmit_time_dw;

   // Turn off the DW1000 radio and setup the correct antenna and channel for transmission
   if (dw1000_radio_disable())
   {
      dw1000_update_channel(SCHEDULE_XMIT_CHANNEL);
      dw1000_choose_antenna(SCHEDULE_XMIT_ANTENNA);
      dwt_write8bitoffsetreg(PMSC_ID, PMSC_CTRL0_OFFSET, PMSC_CTRL0_TXCLKS_125M);

      // Transmit the packet
      dwt_writetxfctrl(packet_size, 0, MSG_TYPE_CONTROL);
      dw1000_setdelayedtrxtime(_transmit_time);
      return ((dwt_starttx_no_auto_fcs(DWT_START_TX_DELAYED) == DWT_SUCCESS) && (dwt_writetxdata_enable_fcs(packet_size, packet, 0) == DWT_SUCCESS));
   }
   return FALSE;
}

bool glossy_txcallback(void)
{
   // Flood at every timeslot until the maximum depth is reached
   _transmit_time += DW_DELAY_FROM_US(GLOSSY_FLOOD_INTERVAL_US);
   if (_currently_transmitting && (_current_glossy_depth++ < GLOSSY_MAX_DEPTH) && dw1000_radio_disable())
   {
      dwt_write8bitoffsetreg(PMSC_ID, PMSC_CTRL0_OFFSET, PMSC_CTRL0_TXCLKS_125M);
      dwt_writetodevice(TX_BUFFER_ID, offsetof(struct ieee154_header_broadcast, seqNum), 1, &_current_glossy_depth);
      dwt_setdelayedtrxtime(_transmit_time);
      _currently_transmitting = (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS);
   }
   else
      _currently_transmitting = FALSE;
   return _currently_transmitting;
}

void glossy_rxcallback(uint32_t receive_time_dw, uint8_t *packet, uint16_t packet_size)
{
   // Do not handle packet receptions during transmission
   if (_currently_transmitting)
      return;

   // If not at the maximum depth, perpetuate the Glossy flood
   _current_glossy_depth = ((struct ieee154_header_broadcast*)packet)->seqNum;
   _transmit_time = receive_time_dw + DW_DELAY_FROM_US(GLOSSY_FLOOD_INTERVAL_US);
   if ((_current_glossy_depth++ < GLOSSY_MAX_DEPTH) && dw1000_radio_disable() && ENABLE_GLOSSY_FLOOD_ON_RECEPTION)
   {
      dwt_write8bitoffsetreg(PMSC_ID, PMSC_CTRL0_OFFSET, PMSC_CTRL0_TXCLKS_125M);
      dwt_writetxfctrl(packet_size, 0, MSG_TYPE_CONTROL);
      dwt_setdelayedtrxtime(_transmit_time);
      _currently_transmitting = (dwt_starttx_no_auto_fcs(DWT_START_TX_DELAYED) == DWT_SUCCESS) && (dwt_writetxdata_enable_fcs(packet_size, packet, 0) == DWT_SUCCESS);
   }
   else
      _currently_transmitting = FALSE;
}
