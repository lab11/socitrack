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

typedef struct
{
   uint32_t rx_ok;          // ranging slots that produced a decoded packet
   uint32_t rx_failed;      // ranging slots that timed out or errored -- the receive-sensitivity metric
   uint32_t wake_max_us;    // worst wake-up seen, which is what a safety margin actually has to cover
   uint32_t wake_last_us;
   uint32_t tx_failed;      // delayed transmissions programmed after their slot had already passed
   uint32_t rx_arm_failed;  // delayed receives rejected for the same reason -- these abort the whole round
   uint32_t isr_max_us;     // longest single radio interrupt since boot
   uint32_t isr_max_events; // radio events serviced by that longest interrupt, since one entry may drain several
   uint32_t isr_over_count; // radio interrupts that exceeded RADIO_ISR_BUDGET_US
   uint32_t isr_count;      // radio interrupts serviced, so the overrun count has a denominator
   uint32_t isr_us_total;   // summed duration of every measured radio interrupt, to recover a mean
   uint32_t isr_warm_max_us;// longest radio interrupt after the instruction cache has warmed
   uint32_t isr_warm_count; // radio interrupts behind isr_warm_max_us
   uint8_t network_size;    // devices in the schedule, without which the receive ratio cannot be read
   uint32_t wake_skipped;   // wake-ups that found the radio already awake and did nothing
   bool cycle_counter_ok;   // false means isr_max_us is not measurable on this build
} ranging_radio_stats_t;

typedef struct
{
   uint8_t eui;             // device this row accumulates, or 0 for an unused row
   bool offset_set;         // deviations accumulate against the first sample seen, because a variance
   int32_t offset_mm;       // taken about zero cancels catastrophically at metre-scale ranges
   uint32_t n;              // pre-filter range samples accumulated for this peer
   int64_t sum_mm;          // summed deviations from offset_mm, from which a mean and a standard
   uint64_t sumsq_mm;       // deviation are recovered without ever squaring a metre-scale range
} ranging_range_stats_t;


// Data structures for 802.15.4 packets --------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__))
{
   uint8_t msgType;
} ieee154_header_t;

typedef struct  __attribute__ ((__packed__))
{
   uint8_t fcs[2];
} ieee154_footer_t;


// Public API Functions ------------------------------------------------------------------------------------------------

bool ranging_radio_init(uint8_t *uid);
void ranging_radio_deinit(void);
bool ranging_radio_reset(void);
void ranging_radio_enable_rx_diagnostics(void);
void ranging_radio_register_callbacks(dwt_cb_t tx_done, dwt_cb_t rx_done, dwt_cb_t rx_timeout, dwt_cb_t rx_err);
void ranging_radio_choose_channel(uint8_t channel);
void ranging_radio_choose_antenna(uint8_t antenna_number);
void ranging_radio_disable(void);
void ranging_radio_sleep(bool deep_sleep);
void ranging_radio_wakeup(void);
uint32_t ranging_radio_get_isr_overrun_count(void);
void ranging_radio_get_wake_timing(uint32_t *pin_us, uint32_t *ready_us, uint32_t *restore_us);
void ranging_radio_note_tx_failure(void);
void ranging_radio_note_rx_arm_failure(void);
void ranging_radio_note_rx_result(bool decoded);
void ranging_radio_note_network_size(uint8_t devices);
void ranging_radio_get_stats(ranging_radio_stats_t *stats);
void ranging_radio_note_phase(uint8_t phase);
void ranging_radio_get_isr_phase_max(uint32_t *max_us, uint32_t *counts);
void ranging_radio_note_range_sample(uint8_t eui, int32_t range_mm);
const ranging_range_stats_t* ranging_radio_get_range_stats(void);
bool ranging_radio_rxenable(int mode);
uint64_t ranging_radio_readrxtimestamp(void);
uint32_t ranging_radio_readrxtimestamp_lo(void);
uint32_t ranging_radio_readrxtimestamp_hi(void);
uint64_t ranging_radio_readtxtimestamp(void);
float ranging_radio_received_signal_level(bool first_signal_level);
int ranging_radio_time_to_millimeters(double dwtime);

#endif  // #ifndef __RANGING_HEADER_H__
