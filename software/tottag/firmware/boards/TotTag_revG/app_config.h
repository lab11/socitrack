// application-specific configuration
// augments the base configuration in sdk/<SDK>/config/<IC>/config/sdk_config.h

#pragma once

// No shared resources
#define NRFX_PRS_ENABLED 0

#define NRF_QUEUE_ENABLED 1
#define NRF_BLE_CONN_PARAMS_ENABLED 1
#define NRF_BLE_GATT_ENABLED 1
#define NRF_BLE_QWR_ENABLED 1
#define BLE_ADVERTISING_ENABLED 1
#define BLE_DB_DISCOVERY_ENABLED 1
#define BLE_ECS_ENABLED 1
#define BLE_LBS_ENABLED 1
#define BLE_NUS_ENABLED 1

#define NRFX_GPIOTE_ENABLED 1
#define NRFX_GPIOTE_CONFIG_NUM_OF_LOW_POWER_EVENTS 4

#define NRFX_SPI_ENABLED 1
#define NRFX_SPI0_ENABLED 1

#define NRF_CLOCK_ENABLED 1
#define NRFX_CLOCK_ENABLED 1
#define NRFX_SYSTICK_ENABLED 0

#define NRFX_WDT_ENABLED 1

#define NRFX_RTC_ENABLED 1
#define NRFX_RTC2_ENABLED 1

#define APP_TIMER_ENABLED 1
#define APP_TIMER_KEEPS_RTC_ACTIVE 1

#define NRFX_TWI_ENABLED 1
#define NRFX_TWI1_ENABLED 1

#define NRF_FSTORAGE_ENABLED 1
#define FDS_ENABLED 1
#define FDS_VIRTUAL_PAGES 10
#define FDS_OP_QUEUE_SIZE 10

#define NRFX_SAADC_ENABLED 1
#define NRFX_SAADC_CONFIG_LP_MODE 1
#define NRFX_SAADC_API_V2 1

// Disable retargeting of printf to UART, as we retarget it to RTT
#define RETARGET_ENABLED 0

#define NRF_LOG_ENABLED 1
#define NRF_LOG_BACKEND_RTT_ENABLED 1
#define NRF_LOG_USES_RTT 1
#define NRF_LOG_DEFERRED 0

#define NRF_SDH_ENABLED 1
#define NRF_SDH_BLE_ENABLED 1
#define NRF_SDH_BLE_GAP_DATA_LENGTH 251
#define NRF_SDH_BLE_PERIPHERAL_LINK_COUNT   2
#define NRF_SDH_BLE_CENTRAL_LINK_COUNT      2
#define NRF_SDH_BLE_TOTAL_LINK_COUNT        4
#define NRF_SDH_BLE_GATT_MAX_MTU_SIZE 230
#define NRF_SDH_BLE_VS_UUID_COUNT 10
#define NRF_SDH_SOC_ENABLED 1

#define NRFX_PWM_ENABLED 1
#define NRFX_PWM0_ENABLED 1

#define NRFX_POWER_ENABLED 1
#define POWER_ENABLED 1
#define POWER_CONFIG_DEFAULT_DCDCEN 1
#define NRF_PWR_MGMT_ENABLED 1
#define NRF_PWR_MGMT_CONFIG_FPU_SUPPORT_ENABLED 1

#define APP_SDCARD_ENABLED 1
#define APP_SDCARD_SPI_INSTANCE 0

#define SEGGER_RTT_CONFIG_BUFFER_SIZE_UP 2048
#define SEGGER_RTT_CONFIG_DEFAULT_MODE 0  // Change to 2 to ensure all printf contents are output, will crash if no debugger attached
