#ifndef __APP_CONFIG_HEADER_H__
#define __APP_CONFIG_HEADER_H__

// Application Configuration -------------------------------------------------------------------------------------------

#ifdef AM_DEBUG_PRINTF

#define ENABLE_LEDS
#define ENABLE_LOGGING

#endif


// Common Header Inclusions --------------------------------------------------------------------------------------------

#include <stdlib.h>
#include <string.h>
#include "am_bsp.h"
#include "am_util.h"
#include "pinout.h"
#include "FreeRTOS.h"
#include "event_groups.h"
#include "portable.h"
#include "portmacro.h"
#include "semphr.h"
#include "task.h"
#include "wsf_types.h"


// Common Application Definitions --------------------------------------------------------------------------------------

#define EUI_LEN                                     6
#define EUI_NAME_MAX_LEN                            16

#define MAX_NUM_RANGING_DEVICES                     10
#define COMPRESSED_RANGE_DATUM_LENGTH               (1 + sizeof(int16_t))       // EUI + Range
#define MAX_COMPRESSED_RANGE_DATA_LENGTH            (1 + (COMPRESSED_RANGE_DATUM_LENGTH * MAX_NUM_RANGING_DEVICES))

#define STORAGE_QUEUE_MAX_NUM_ITEMS                 24

#define BATTERY_CHECK_INTERVAL_S                    300


// Battery Configuration -----------------------------------------------------------------------------------------------

typedef enum { BATTERY_EMPTY = 3200, BATTERY_CRITICAL = 3500, BATTERY_NOMINAL = 3700, BATTERY_FULL = 4200 } battery_status_t;


// DW3000 Ranging Radio Configuration ----------------------------------------------------------------------------------

#define DW_PREAMBLE_LENGTH                          DWT_PLEN_128
#define DW_PAC_SIZE                                 DWT_PAC8
#define DW_DATA_RATE                                DWT_BR_6M8
#define DW_SFD_TYPE                                 DWT_SFD_DW_16
#define DW_SFD_TO                                   (128 + 1 + 16 - 8)   // (Preamble length + 1 + SFD length - PAC size)
#define DW_PREAMBLE_TIMEOUT                         (128 / 8)   // (Preamble length / PAC size)
#define DW_PREAMBLE_LENGTH_US                       ((1 + 128 + 16) * 64 / 62.89133858)   // (1 + Preamble length + SFD length) * 64 / 62.89133858


// Bluetooth LE Configuration ------------------------------------------------------------------------------------------

#define MANUFACTURER                                "Lab11"
#define HW_MODEL                                    "TotTag"
#define HW_REVISION                                 "Rev. "STRINGIZE_VAL(_HW_REVISION)

#define MAX_NUM_CONNECTIONS                         1

#define BLE_ADVERTISING_DURATION_MS                 0
#define BLE_ADVERTISING_INTERVAL_0_625_MS           120         // 75 ms
#define BLE_SCANNING_WINDOW_0_625_MS                160         // 100 ms
#define BLE_SCANNING_INTERVAL_0_625_MS              1600        // 1000 ms
#define BLE_SCANNING_DURATION_MS                    10000

#define BLE_DESIRED_MTU                             247
#define BLE_TRANSACTION_TIMEOUT_S                   1
#define BLE_MIN_CONNECTION_INTERVAL_1_25_MS         12          // 15 ms
#define BLE_MAX_CONNECTION_INTERVAL_1_25_MS         24          // 30 ms
#define BLE_CONNECTION_SLAVE_LATENCY                9
#define BLE_SUPERVISION_TIMEOUT_10_MS               100         // 1000 ms
#define BLE_MAX_CONNECTION_UPDATE_ATTEMPTS          5

#define BLUETOOTH_COMPANY_ID                        0xe0,0x02
#define BLE_LIVE_STATS_SERVICE_ID                   0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x52,0x31,0x8c,0xd6
#define BLE_LIVE_STATS_BATTERY_CHAR                 0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x53,0x31,0x8c,0xd6
#define BLE_LIVE_STATS_TIMESTAMP_CHAR               0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x54,0x31,0x8c,0xd6
#define BLE_LIVE_STATS_FINDMYTOTTAG_CHAR            0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x55,0x31,0x8c,0xd6
#define BLE_LIVE_STATS_RANGING_CHAR                 0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x56,0x31,0x8c,0xd6
#define BLE_LIVE_STATS_ADDRESS_CHAR                 0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x57,0x31,0x8c,0xd6
#define BLE_SCHEDULING_SERVICE_ID                   0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x5A,0x31,0x8c,0xd6
#define BLE_SCHEDULING_REQUEST_CHAR                 0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x5B,0x31,0x8c,0xd6
#define BLE_MAINTENANCE_SERVICE_ID                  0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x60,0x31,0x8c,0xd6
#define BLE_MAINTENANCE_EXPERIMENT_CHAR             0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x61,0x31,0x8c,0xd6
#define BLE_MAINTENANCE_COMMAND_CHAR                0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x62,0x31,0x8c,0xd6
#define BLE_MAINTENANCE_DATA_CHAR                   0x2e,0x5d,0x5e,0x39,0x31,0x52,0x45,0x0c,0x90,0xee,0x3f,0xa2,0x63,0x31,0x8c,0xd6


// Ranging Protocol Configuration --------------------------------------------------------------------------------------

#define RADIO_XMIT_CHANNEL                          5
#define NUM_XMIT_ANTENNAS                           2
#define NUM_RCV_ANTENNAS                            2
#define TX_ANTENNA_DELAY                            16385
#define RX_ANTENNA_DELAY                            16385
#define MIN_VALID_RANGE_MM                          (-1000)
#define MAX_VALID_RANGE_MM                          (32*1000)
#define UNSCHEDULED_SLOT                            0xFF

#define SCHEDULING_INTERVAL_US                      1000000
#define RADIO_WAKEUP_SAFETY_DELAY_US                5000
#define RECEIVE_EARLY_START_US                      ((uint32_t)DW_PREAMBLE_LENGTH_US)

#define DEVICE_TIMEOUT_SECONDS                      60
#define NETWORK_SEARCH_TIME_SECONDS                 3
#define MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE        3

#define SCHEDULE_NUM_TOTAL_BROADCASTS               5
#define SCHEDULE_NUM_MASTER_BROADCASTS              2
#define SCHEDULE_RESEND_INTERVAL_US                 1000
#define SCHEDULE_BROADCAST_PERIOD_US                (SCHEDULE_NUM_TOTAL_BROADCASTS * SCHEDULE_RESEND_INTERVAL_US)

#define RANGING_BROADCAST_INTERVAL_US               600
#define RANGING_TIMEOUT_US                          ((uint32_t)DW_PREAMBLE_LENGTH_US + 56)
#define RANGING_NUM_SEQUENCES_PER_RANGE             (NUM_XMIT_ANTENNAS * NUM_RCV_ANTENNAS)
#define RANGING_NUM_PACKETS_PER_RANGE               (4 * RANGING_NUM_SEQUENCES_PER_RANGE)
#define RANGING_US_PER_RANGE                        (RANGING_BROADCAST_INTERVAL_US * RANGING_NUM_PACKETS_PER_RANGE)

#define RANGE_STATUS_NUM_TOTAL_BROADCASTS           4
#define RANGE_STATUS_RESEND_INTERVAL_US             1000
#define RANGE_STATUS_BROADCAST_PERIOD_US            (RANGE_STATUS_NUM_TOTAL_BROADCASTS * RANGE_STATUS_RESEND_INTERVAL_US)
#define RANGE_STATUS_TIMEOUT_US                     (RANGE_STATUS_BROADCAST_PERIOD_US - 900 + RECEIVE_EARLY_START_US)

#define SUBSCRIPTION_BROADCAST_PERIOD_US            2000
#define SUBSCRIPTION_TIMEOUT_US                     1400

#endif  // #ifndef __APP_CONFIG_HEADER_H__
