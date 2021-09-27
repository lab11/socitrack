#ifndef __CONFIGURATION_HEADER_H__
#define __CONFIGURATION_HEADER_H__

// Includes ------------------------------------------------------------------------------------------------------------

#include "system.h"

// Ranging configuration parameters ------------------------------------------------------------------------------------

// FAST_RANGING_CONFIG: 6.8 Mbps
// LONG_RANGING_CONFIG: 110 Kbps
#define FAST_RANGING_CONFIG

#ifdef LONG_RANGING_CONFIG
#define DW1000_PREAMBLE_LENGTH        DWT_PLEN_4096
#define DW1000_PAC_SIZE               DWT_PAC64
#define DW1000_DATA_RATE              DWT_BR_110K
#define DW1000_SMART_PWR_EN           0
#define DW1000_SFD_TO                 (4096 + 64 + 1)
#define RANGING_BROADCASTS_PERIOD_US  10000
#define RANGING_CONTENTION_PADDING_US 10000
#endif
#ifdef FAST_RANGING_CONFIG
#define DW1000_PREAMBLE_LENGTH        DWT_PLEN_128
#define DW1000_PAC_SIZE               DWT_PAC8
#define DW1000_DATA_RATE              DWT_BR_6M8
#define DW1000_SMART_PWR_EN           1
#define DW1000_SFD_TO                 (128 + 1 + 8 - 8) // (Preamble length + 1 + SFD length - PAC size)
#define RANGING_BROADCASTS_PERIOD_US  2000
#define RANGING_CONTENTION_PADDING_US 2000
#endif

#define MSG_MAX_PACKET_LENGTH            1023
#define MSG_TYPE_CONTROL                 0
#define MSG_TYPE_RANGING                 1

#define NUM_RANGING_CHANNELS 3
#define NUM_ANTENNAS 3
#define MIN_VALID_RANGE_MM -1000      // -1 meter
#define MAX_VALID_RANGE_MM (50*1000)  // 50 meters
#define MIN_VALID_RANGES_PER_DEVICE 8

// Use 30 broadcasts per device for ranging: (3 channels * 3 antennas on requester * 3 antennas on responder) + 3
#define NUM_RANGING_BROADCASTS ((NUM_RANGING_CHANNELS*NUM_ANTENNAS*NUM_ANTENNAS) + NUM_RANGING_CHANNELS)
#define RANGING_RESPONSE_CHANNEL_INDEX  0
#define RANGING_RESPONSE_ANTENNA_INDEX  0

// Protocol adaptation -------------------------------------------------------------------------------------------------

// Disable LEDs to save power (and reduce distraction)
#define STM_ENABLE_LEDS FALSE

// Do not attempt to wait for the host to send instructions
//#define BYPASS_HOST_INTERFACE TRUE

// Adjust Tx parameters
#define DW1000_MAXIMIZE_TX_POWER TRUE

// Re-transmit received Glossy flood packets
#define ENABLE_GLOSSY_FLOOD_ON_RECEPTION FALSE

// Put STM and DW1000 into low-power mode between rangings
#define STM_RADIO_SLEEP_WHILE_PASSIVE TRUE

// Change to 1 to allow the nRF to wake up the DW1000 before ranging
#define ALLOW_HOST_TO_WAKEUP_RADIO FALSE

// Force hybrids to perform their own rangings to ALL nodes
#define HYBRIDS_PERFORM_ALL_RANGINGS TRUE

// Protocol parameters
#define PROTOCOL_MAX_NUM_DEVICES_PER_TYPE  10
#define PROTOCOL_MAX_NUM_REQUESTERS        PROTOCOL_MAX_NUM_DEVICES_PER_TYPE
#define PROTOCOL_MAX_NUM_RESPONDERS        PROTOCOL_MAX_NUM_DEVICES_PER_TYPE
#define PROTOCOL_MAX_NUM_HYBRIDS           PROTOCOL_MAX_NUM_DEVICES_PER_TYPE
#define PROTOCOL_EUI_TYPE                  uint8_t
#define PROTOCOL_EUI_SIZE                  sizeof(PROTOCOL_EUI_TYPE)
#define PROTOCOL_DEVICE_TIMEOUT_SECONDS    3
#define PROTOCOL_SCHEDULING_INTERVAL_US    1e6
#define PROTOCOL_SCHEDULING_INTERVAL_DW    DW_DELAY_FROM_US(PROTOCOL_SCHEDULING_INTERVAL_US)
#define PROTOCOL_SLOT_PERIOD_US            10000
#define PROTOCOL_NUM_SLOTS_PER_REQUEST     7
#define PROTOCOL_NUM_SLOTS_PER_RESPONSE    1
#define PROTOCOL_MAX_MISSING_SCHEDULE_ROUNDS_BEFORE_STATE_CHANGE        3
#define PROTOCOL_MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE                   5
#define PROTOCOL_MAX_NUM_NETWORK_SEARCH_ATTEMPTS                        1

// Application states and interrupts -----------------------------------------------------------------------------------

typedef enum { APP_UNSPECIFIED = 0, APP_STANDARD, APP_CALIBRATION } app_mode_t;
typedef enum { APP_NOT_INITIALIZED, APP_INITIALIZING, APP_STOPPED, APP_RUNNING } app_status_t;
typedef enum
{
   INTERRUPT_RANGING,
   INTERRUPT_DW1000,
   INTERRUPT_I2C_RX,
   INTERRUPT_I2C_ERROR,
   NUM_INTERRUPT_SOURCES
} interrupt_source_t;

// Application-specific defines ----------------------------------------------------------------------------------------

#define MAX_SPI_TRANSACTION_BYTES 250
#define MODULE_PANID 0x6611
#define I2C_OWN_ADDRESS 0x65
#define INFO_BYTE_0 0xB0
#define INFO_BYTE_1 0x1A

// Application and OS functions ----------------------------------------------------------------------------------------

bool module_configure(app_mode_t app_mode, void *config);
bool module_start(void);
void module_stop(void);
void module_reset(void);
bool module_ready(void);
void module_set_stopped(void);
void reset_watchdog(void);
void mark_interrupt(interrupt_source_t src);

#endif // __CONFIGURATION_HEADER_H__
