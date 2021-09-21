#ifndef __BLE_CONFIG_H
#define __BLE_CONFIG_H

// Includes ------------------------------------------------------------------------------------------------------------

#include "app_timer.h"
#include "boards.h"
#include "sd_card.h"


// User configurable debug definitions ---------------------------------------------------------------------------------

#define DEVICE_FORCE_RESET_INTERVAL_SEC 3600
#define STOP_BLE_AND_SQUAREPOINT_WHEN_CHARGING
//#define ENABLE_LEDS
//#define PRINTF_TO_SD_CARD


// Forced definitions for "Deployment Mode" ----------------------------------------------------------------------------

#ifndef DEBUG_MODE
#undef ENABLE_LEDS
#ifndef STOP_BLE_AND_SQUAREPOINT_WHEN_CHARGING
#define STOP_BLE_AND_SQUAREPOINT_WHEN_CHARGING
#endif
#undef PRINTF_TO_SD_CARD
#endif

#ifndef DEBUG_MODE
#define log_printf(...) (void)0
#else
#ifdef PRINTF_TO_SD_CARD
#define log_printf sd_card_printf
#else
#define log_printf printf
#endif
#endif

// Constants -----------------------------------------------------------------------------------------------------------

// SUMMON URL (must be <= 17 characters to not throw NRF_ERROR_INVALID_LENGTH, as header overhead is 14 byte and packet has size <= 31)

#define PHYSWEB_URL "bit.ly/p1R8"               // Points to https://n.ethz.ch/~abiri/d/

// BLE characteristics
#define CARRIER_BLE_SERV_SHORT_UUID             0x3152
#define CARRIER_BLE_CHAR_LOCATION               0x3153
#define CARRIER_BLE_CHAR_CALIBRATION            0x3157
#define CARRIER_BLE_CHAR_TIMESTAMP              0x3158

// Information
#define APP_COMPANY_IDENTIFIER                  0x02E0
#define MANUFACTURER_NAME                       "Lab11"
#define MODEL_NUMBER                            TOTTAG
#define HARDWARE_REVISION                       REVISION
#define FIRMWARE_REVISION                       "2.0"
#define FIRMWARE_VERSION                        FW_VERSION

// Calibration
#define BLE_CALIBRATION_INDEX_INVALID           255

// Behavior
#define WATCHDOG_CHECK_RATE_MS                  1000
#define WATCHDOG_HARD_RESET_TIMEOUT_MS          2000
#define APP_BATTERY_CHECK_TIMEOUT_SEC           300
#define APP_LACK_OF_MOTION_TIMEOUT_SEC          2
#define APP_RUNNING_RESPONSE_TIMEOUT_SEC        2
#define SQUAREPOINT_ERROR_NOTIFY_COUNT          5
#define BLE_NETWORK_DISCOVERY_COUNTDOWN_VALUE   5
#define BLE_SINGLE_SCAN_DURATION_SEC            5
#define BLE_SINGLE_SCAN_INTERVAL_SEC            180
#define MINIMUM_VALID_TIMESTAMP                 1612810414
#define MAXIMUM_VALID_TIMESTAMP                 2000000000

// Storage / Buffers
#define APP_BLE_BUFFER_LENGTH                   256
#define APP_BLE_MAX_CHAR_LEN                    128
#define APP_SDCARD_BUFFER_LENGTH                10240

#define APP_LOG_BUFFER_LINE                     (10 + 1 + 3*6 + 6 + 1)
#define APP_LOG_BUFFER_LENGTH                   (10 * APP_LOG_BUFFER_LINE)
#define APP_LOG_RANGE_LENGTH                    (SQUAREPOINT_EUI_LEN + 4)
#define APP_LOG_OUT_OF_RANGE_VALUE              999999

#define APP_BLE_ADV_SCHED_EUI_LENGTH            2
#define APP_BLE_ADVDATA_LENGTH                  APP_BLE_ADV_SCHED_EUI_LENGTH

// nRF hardware definitions
#define DEVICE_ID_MEMORY                        0x10001080
#define DEAD_BEEF                               0xDEADBEEF


// Structs -------------------------------------------------------------------------------------------------------------

typedef enum { UNKNOWN = 0, SCHEDULER, BACKUP_SCHEDULER, PARTICIPANT } schedule_role_t;
typedef enum { UNASSIGNED = 0, HYBRID, REQUESTER, RESPONDER, SUPPORTER } device_role_t;

typedef struct app_flags_t
{
   nrfx_atomic_flag_t squarepoint_enabled;
   nrfx_atomic_flag_t squarepoint_running;
   nrfx_atomic_flag_t squarepoint_data_received;
   nrfx_atomic_flag_t squarepoint_wakeup_triggered;
   nrfx_atomic_flag_t squarepoint_time_epoch_requested;
   nrfx_atomic_flag_t squarepoint_needs_init;
   nrfx_atomic_flag_t elapsed_second;
   nrfx_atomic_flag_t sd_card_inserted;
   nrfx_atomic_flag_t rtc_time_valid;
   nrfx_atomic_flag_t imu_data_ready;
   nrfx_atomic_flag_t imu_motion_changed;
   nrfx_atomic_flag_t battery_status_changed;
   nrfx_atomic_flag_t range_buffer_updated;
   nrfx_atomic_flag_t device_in_motion;
   nrfx_atomic_flag_t bluetooth_is_advertising;
   nrfx_atomic_flag_t bluetooth_is_scanning;
   nrfx_atomic_flag_t bluetooth_single_scanning;
   nrfx_atomic_u32_t bluetooth_single_scan_timer;
   nrfx_atomic_u32_t squarepoint_comms_error_count;
   nrfx_atomic_u32_t squarepoint_timeout_counter;
   nrfx_atomic_u32_t battery_check_counter;
   nrfx_atomic_u32_t device_reset_counter;
   nrfx_atomic_u32_t calibration_index;
} app_flags_t;


// BLE -----------------------------------------------------------------------------------------------------------------

// Structure: c0:98:e5:42:XX:XX
#define APP_BLE_ADDR_MIN        0x00
#define APP_BLE_ADDR_MAX        0xFF

// Peripheral
#define APP_ADV_INTERVAL_MS     50
#define APP_ADV_INTERVAL_CAL_MS 50

// Central
#define APP_SCAN_INTERVAL_MS    1085
#define APP_SCAN_WINDOW_MS      (APP_ADV_INTERVAL_MS + 15)

// No-network transitions
#define BLE_MISSING_NETWORK_TRANSITION1_TIMEOUT  300
#define BLE_NETWORK_TRANSITION1_SCAN_INTERVAL       MSEC_TO_UNITS(4085, UNIT_0_625_MS)
#define BLE_MISSING_NETWORK_TRANSITION2_TIMEOUT  600
#define BLE_NETWORK_TRANSITION2_SCAN_INTERVAL       MSEC_TO_UNITS(6085, UNIT_0_625_MS)

// Physical Web
#define PHYSWEB_SERVICE_ID  0xFEAA
#define PHYSWEB_URL_TYPE    0x10    // Denotes URLs (vs URIs or TLM data)
#define PHYSWEB_TX_POWER    0xBA    // Tx Power. Measured at 1 m plus 41 dBm. (who cares)

#define PHYSWEB_URLSCHEME_HTTPWWW   0x00    // http://www.
#define PHYSWEB_URLSCHEME_HTTPSWWW  0x01    // https://www.
#define PHYSWEB_URLSCHEME_HTTP      0x02    // http://
#define PHYSWEB_URLSCHEME_HTTPS     0x03    // https://

#define PHYSWEB_URLEND_COMSLASH 0x00    // .com/
#define PHYSWEB_URLEND_ORGSLASH 0x01    // .org/
#define PHYSWEB_URLEND_EDUSLASH 0x02    // .edu/
#define PHYSWEB_URLEND_COM      0x07    // .com
#define PHYSWEB_URLEND_ORG      0x08    // .org
#define PHYSWEB_URLEND_EDU      0x09    // .edu

// Security parameters
#define SEC_PARAMS_BOND                 1                                               /**< Perform bonding. */
#if LESC_MITM_NC
#define SEC_PARAMS_MITM                 1                                               /**< Man In The Middle protection required. */
#define SEC_PARAMS_IO_CAPABILITIES      BLE_GAP_IO_CAPS_DISPLAY_YESNO                   /**< Display Yes/No to force Numeric Comparison. */
#else
#define SEC_PARAMS_MITM                 0                                               /**< Man In The Middle protection required. */
#define SEC_PARAMS_IO_CAPABILITIES      BLE_GAP_IO_CAPS_NONE                            /**< No I/O caps. */
#endif
#define SEC_PARAMS_LESC                 1                                               /**< LE Secure Connections pairing required. */
#define SEC_PARAMS_KEYPRESS             0                                               /**< Keypress notifications not required. */
#define SEC_PARAMS_OOB                  0                                               /**< Out Of Band data not available. */
#define SEC_PARAMS_MIN_KEY_SIZE         7                                               /**< Minimum encryption key size in octets. */
#define SEC_PARAMS_MAX_KEY_SIZE         16                                              /**< Maximum encryption key size in octets. */

#define BLE_GAP_LESC_P256_SK_LEN        32
#define NRF_BLE_GQ_QUEUE_SIZE           4


// Eddystone App configurations, adapted from nRF example "examples\ble_peripheral\ble_app_eddystone\es_app_config.h"
#define APP_CONFIG_LOCK_CODE {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define APP_CONFIG_CALIBRATED_RANGING_DATA {-49, -39, -29, -24, -19, -14, -9, -7, -5}   //!< Calibrated TX power at 0 m. See the nRF52 Product Specification for corresponding TX values.
#define APP_CONFIG_TLM_TEMP_VBATT_UPDATE_INTERVAL_SECONDS   10                          //!< How often should the data in the TLM frame be updated.
#define APP_CONFIG_TLM_ADV_INTERLEAVE_RATIO                 5                           //!< How often should the TLM frame be advertised.

#define APP_CONFIG_ADV_INTERVAL_MS_MAX                      20000                       //!< Maximum allowed advertisement interval. Can be undefined without int
#define APP_CONFIG_ADV_FRAME_SPACING_MS_MIN                 500                         //!< Minimum time between advertisement frames. Imposes limit on minumum accepted advertisement interval
#define APP_CONFIG_ADV_FRAME_ETLM_SPACING_MS                300                         //!< The time that is required for preparing an eTLM slot. Imposes limit on minimum accepted advertisement interval.

#define ES_STOPWATCH_MAX_USERS                              4                           //!< Maximum amount of users that can be registered with the es_stopwatch module.

#define DEBUG_TIMING_INIT_VALUE                             65280                       //!< Initial time (as recommended by Google) to test the TK rollover behavior.
#define APP_CONFIG_TIMING_INIT_VALUE                        DEBUG_TIMING_INIT_VALUE     //!< Initializing value for the timing value of security slots.

#define APP_CONFIG_TLM_TEMP_INTERVAL_SECONDS                (30)                        //!< How often should the temperature of the beacon be updated when TLM slot is configured.
#define APP_CONFIG_TLM_VBATT_INTERVAL_SECONDS               (30)                        //!< How often should the battery voltage of the beacon be updated when TLM slot is configured.

// BLE CONFIGS
#define APP_DEVICE_NAME                                     DEVICE_NAME                 //!< Advertised device name in the scan response when in connectable mode.
#define IS_SRVC_CHANGED_CHARACT_PRESENT                     0                           //!< Information whether the service changed characteristic is available. If it is not enabled, the server's database cannot be changed for the lifetime of the device.
#define MAX_ADV_INTERVAL                                    (1000)                      //!< Maximum connection interval (in ms).
#define MIN_CONN_ADV_INTERVAL                               (20)                        //!< Minimum connection interval (in ms).
#define MIN_NON_CONN_ADV_INTERVAL                           (100)                       //!< Minimum advertisement interval for non-connectable advertisements (in ms).

#define APP_CFG_NON_CONN_ADV_TIMEOUT                        0                           //!< Time for which the device must be advertising in non-connectable mode (in seconds). 0 disables the time-out.
#define APP_CFG_NON_CONN_ADV_INTERVAL_MS                    APP_ADV_INTERVAL_MS         //!< The advertising interval for non-connectable advertisement (in milliseconds). This value can vary between 100 ms and 10.24 s.
#define APP_CFG_CONNECTABLE_ADV_TIMEOUT                     0                           //!< Time for which the device must be advertising in connectable mode (in milliseconds). 0 disables the time-out.
#define APP_CFG_CONNECTABLE_ADV_INTERVAL_MS                 APP_ADV_INTERVAL_MS         //!< The advertising interval for connectable advertisement (in milliseconds). This value can vary between 20 ms and 10.24 s.

#define APP_CFG_DEFAULT_RADIO_TX_POWER                      0x00                        //!< Default TX power of the radio.

#define APP_ADV_INTERVAL                    MSEC_TO_UNITS(APP_ADV_INTERVAL_MS, UNIT_0_625_MS)
#define APP_ADV_INTERVAL_CALIBRATION        MSEC_TO_UNITS(APP_ADV_INTERVAL_CAL_MS, UNIT_0_625_MS)

#define APP_SCAN_INTERVAL                   MSEC_TO_UNITS(APP_SCAN_INTERVAL_MS, UNIT_0_625_MS)
#define APP_SCAN_WINDOW                     MSEC_TO_UNITS(APP_SCAN_WINDOW_MS, UNIT_0_625_MS)
#define APP_SCAN_CONNECT_TIMEOUT            MSEC_TO_UNITS(2500, UNIT_10_MS)

#define MIN_CONN_INTERVAL                   MSEC_TO_UNITS(50, UNIT_1_25_MS)             //!< Minimum acceptable connection interval (50 ms). The connection interval uses 1.25 ms units.
#define MAX_CONN_INTERVAL                   MSEC_TO_UNITS(100, UNIT_1_25_MS)            //!< Maximum acceptable connection interval (100 ms). The connection interval uses 1.25 ms units.
#define SLAVE_LATENCY                       9                                           //!< Slave latency.
#define CONN_SUP_TIMEOUT                    MSEC_TO_UNITS(4000, UNIT_10_MS)             //!< Connection supervision time-out (4 seconds). The supervision time-out uses 10 ms units.
#define FIRST_CONN_PARAMS_UPDATE_DELAY      APP_TIMER_TICKS(5000)                       //!< Time from initiating an event (connection or start of notification) to the first time @ref sd_ble_gap_conn_param_update is called (5 seconds).
#define NEXT_CONN_PARAMS_UPDATE_DELAY       APP_TIMER_TICKS(30000)                      //!< Time between each call to @ref sd_ble_gap_conn_param_update after the first call (30 seconds).
#define MAX_CONN_PARAMS_UPDATE_COUNT        3                                           //!< Number of attempts before giving up the connection parameter negotiation.

// Priority of the application BLE event handler.
#define APP_BLE_OBSERVER_PRIO               3
#define APP_BLE_CONN_CFG_TAG                1                                           /**< A tag identifying the SoftDevice BLE configuration. */

// ES CONFIGS
#define APP_MAX_ADV_SLOTS                   5                                           //!< Maximum number of advertisement slots.
#define APP_MAX_EID_SLOTS                   APP_MAX_ADV_SLOTS                           /**< @brief Maximum number of EID slots.
                                                                                         * @note The maximum number of EID slots must be equal to the maximum number of advertisement slots */
// Broadcast Capabilities
#define APP_IS_VARIABLE_ADV_SUPPORTED       ESCS_BROADCAST_VAR_ADV_SUPPORTED_No         //!< Information whether variable advertisement intervals are supported.
#define APP_IS_VARIABLE_TX_POWER_SUPPORTED  ESCS_BROADCAST_VAR_TX_POWER_SUPPORTED_Yes   //!< Information whether variable advertisement TX power is supported.

#define APP_IS_UID_SUPPORTED                ESCS_FRAME_TYPE_UID_SUPPORTED_Yes           //!< Information whether the UID frame is supported.
#define APP_IS_URL_SUPPORTED                ESCS_FRAME_TYPE_URL_SUPPORTED_Yes           //!< Information whether the URL frame is supported.
#define APP_IS_TLM_SUPPORTED                ESCS_FRAME_TYPE_TLM_SUPPORTED_Yes           //!< Information whether the TLM frame is supported.
#define APP_IS_EID_SUPPORTED                ESCS_FRAME_TYPE_EID_SUPPORTED_No            //!< Information whether the EID frame is supported.

// Remain connectable
#define APP_IS_REMAIN_CONNECTABLE_SUPPORTED ESCS_FUNCT_REMAIN_CONNECTABLE_SUPPORTED_Yes //!< Information whether the 'remain connectable' option is supported.

// Eddystone common data
#define APP_ES_UUID                         0xFEAA                                      //!< UUID for Eddystone beacons according to specification.

// Eddystone UID data
#define APP_ES_UID_FRAME_TYPE               ES_FRAME_TYPE_UID                                          //!< UID frame type (fixed at 0x00).
#define APP_ES_UID_NAMESPACE                0x2e, 0x5d, 0x5e, 0x39, 0x31, 0x52, 0x45, 0x0c, 0x90, 0xee //!< 10-byte Eddystone UID ID namespace.
#define APP_ES_UID_ID                       0x3f, 0xa2, 0x31, 0x52, 0x8c, 0xd6                         //!< 6-byte Eddystone UID ID instance.

// Eddystone URL data
#define APP_ES_URL_FRAME_TYPE               ES_FRAME_TYPE_URL                           //!< URL Frame type (fixed at 0x10).
#define APP_ES_URL_SCHEME                   PHYSWEB_URLSCHEME_HTTPS                     //!< URL prefix scheme according to specification (0x01 = "https://www", 0x03 = "https://).
#define APP_ES_URL_URL                      'b', 'i', 't', '.', 'l', 'y', '/', \
                                            'p', '1', 'R', '8'                          //!< PHYSWEB_URL. Can use last byte suffix 0x00 = ".com" according to specification.

#define DEFAULT_FRAME_TYPE                  APP_ES_URL_FRAME_TYPE                       //!< Frame type of default frame.
#define DEFAULT_FRAME_TX_POWER              0x00                                        //!< Default frame TX power.

/** @brief This value should mimic the data that would be written to the RW ADV Slot characteristic (for example, no RSSI for UID). */
#define DEFAULT_FRAME_DATA                  {DEFAULT_FRAME_TYPE, DEFAULT_FRAME_TX_POWER, APP_ES_URL_SCHEME, APP_ES_URL_URL}
#define DEFAULT_FRAME_LENGTH                14                                          //!< 1 - Frame Type, 1 - TX - power 1 - URL Scheme, URL - 11 = 14

// SCHEDULER CONFIGS
#define SCHED_MAX_EVENT_DATA_SIZE           APP_TIMER_SCHED_EVENT_DATA_SIZE             //!< Maximum size of the scheduler event data.
#define SCHED_QUEUE_SIZE                    10                                          //!< Size of the scheduler queue.

#endif
