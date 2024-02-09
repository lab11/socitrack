#ifndef __RANGING_HEADER_H__
#define __RANGING_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "deca_device_api.h"


// Peripheral Type Definitions -----------------------------------------------------------------------------------------

#define DO_NOT_CHANGE_FLAG                                  UINT8_MAX
#define SPEED_OF_LIGHT                                      299711693.79        // In air @ 22C, 101.325kPa, 50% RH
#define MODULE_PANID                                        0x6611

#define US_TO_DWT(_microsec)                                ((uint64_t)((_microsec) * 499.2 * 128.0))
#define DWT_TO_US(_dw_units)                                ((uint32_t)((_dw_units) / (499.2 * 128.0)))
#define DW_DELAY_FROM_US(_us)                               ((uint32_t)(US_TO_DWT((_us)) >> 8))
#define DW_TIMEOUT_FROM_US(_us)                             ((uint32_t)((_us) * 499.2 / 512.0))


// Data structures for 802.15.4 packets --------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   uint8_t frameCtrl[2];
   uint8_t msgType;
   uint8_t panID[2];
   uint8_t destAddr[2];
   uint8_t sourceAddr[2];
} ieee154_header_t;

typedef struct  __attribute__ ((__packed__))
{
   uint8_t fcs[2];
} ieee154_footer_t;


// Public API Functions ------------------------------------------------------------------------------------------------

void ranging_radio_init(uint8_t *uid);
void ranging_radio_deinit(void);
void ranging_radio_reset(void);
void ranging_radio_enable_rx_diagnostics(void);
void ranging_radio_register_callbacks(dwt_cb_t tx_done, dwt_cb_t rx_done, dwt_cb_t rx_timeout, dwt_cb_t rx_err);
void ranging_radio_choose_channel(uint8_t channel);
void ranging_radio_choose_antenna(uint8_t antenna_number);
void ranging_radio_disable(void);
void ranging_radio_sleep(bool deep_sleep);
void ranging_radio_wakeup(void);
bool ranging_radio_rxenable(int mode);
uint64_t ranging_radio_readrxtimestamp(void);
uint64_t ranging_radio_readtxtimestamp(void);
float ranging_radio_received_signal_level(bool first_signal_level);
int ranging_radio_time_to_millimeters(double dwtime);

#endif  // #ifndef __RANGING_HEADER_H__
