// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "bluetooth.h"
#include "app_main.h"
#include "device_info_service.h"
#include "gatt_api.h"
#include "gap_gatt_service.h"
#include "hci_drv_apollo.h"
#include "hci_drv_cooper.h"
#include "live_stats_functionality.h"
#include "live_stats_service.h"
#include "logging.h"
#include "maintenance_functionality.h"
#include "maintenance_service.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

static volatile uint16_t connection_mtu;
static volatile bool is_scanning, is_advertising, is_connected, ranges_requested, data_requested;
static volatile bool expected_scanning, expected_advertising, is_initialized, first_initialization;
static volatile uint8_t adv_data_conn[HCI_ADV_DATA_LEN], scan_data_conn[HCI_ADV_DATA_LEN], current_ranging_role[3];
static const char adv_local_name[] = { 'T', 'o', 't', 'T', 'a', 'g' };
static const uint8_t adv_data_flags[] = { DM_FLAG_LE_GENERAL_DISC | DM_FLAG_LE_BREDR_NOT_SUP };
static ble_discovery_callback_t discovery_callback;


// Bluetooth LE Advertising and Connection Parameters ------------------------------------------------------------------

static const appAdvCfg_t ble_adv_cfg = {
   {       BLE_ADVERTISING_DURATION_MS,       BLE_ADVERTISING_DURATION_MS,       BLE_ADVERTISING_DURATION_MS },
   { BLE_ADVERTISING_INTERVAL_0_625_MS, BLE_ADVERTISING_INTERVAL_0_625_MS, BLE_ADVERTISING_INTERVAL_0_625_MS }
};
static const appSlaveCfg_t ble_slave_cfg = { MAX_NUM_CONNECTIONS };
static const appSecCfg_t ble_sec_cfg = { 0, 0, 0, FALSE, FALSE };
static const appUpdateCfg_t ble_update_cfg = {
   0,
   BLE_MIN_CONNECTION_INTERVAL_1_25_MS,
   BLE_MAX_CONNECTION_INTERVAL_1_25_MS,
   BLE_CONNECTION_SLAVE_LATENCY,
   BLE_SUPERVISION_TIMEOUT_10_MS,
   BLE_MAX_CONNECTION_UPDATE_ATTEMPTS
};
static const attCfg_t ble_att_cfg = { 1, BLE_DESIRED_MTU, BLE_TRANSACTION_TIMEOUT_S, 4 };
static const appMasterCfg_t ble_master_cfg = {
   BLE_SCANNING_INTERVAL_0_625_MS,
   BLE_SCANNING_WINDOW_0_625_MS,
   BLE_SCANNING_DURATION_MS,
   DM_DISC_MODE_NONE,
   DM_SCAN_TYPE_PASSIVE
};


// Client Characteristic Configuration Descriptors (CCCDs) -------------------------------------------------------------

enum
{
   TOTTAG_GATT_SERVICE_CHANGED_CCC_IDX,
   TOTTAG_RANGING_CCC_IDX,
   TOTTAG_MAINTENANCE_RESULT_CCC_IDX,
   TOTTAG_NUM_CCC_CHARACTERISTICS
};

static const attsCccSet_t characteristicSet[TOTTAG_NUM_CCC_CHARACTERISTICS] =
{
   { GATT_SERVICE_CHANGED_CCC_HANDLE,  ATT_CLIENT_CFG_INDICATE,  DM_SEC_LEVEL_NONE },
   { RANGES_CCC_HANDLE,                  ATT_CLIENT_CFG_NOTIFY,  DM_SEC_LEVEL_NONE },
   { MAINTENANCE_RESULT_CCC_HANDLE,    ATT_CLIENT_CFG_INDICATE,  DM_SEC_LEVEL_NONE }
};


// Bluetooth LE Advertising Setup Functions ----------------------------------------------------------------------------

static void advertising_setup(void)
{
   // Set the BLE address as the System ID, formatted for GATT 0x2A23
   uint8_t *bdaddr = HciGetBdAddr(), sys_id[8];
   sys_id[0] = bdaddr[0]; sys_id[1] = bdaddr[1]; sys_id[2] = bdaddr[2];
   sys_id[3] = 0xFE; sys_id[4] = 0xFF;
   sys_id[5] = bdaddr[3]; sys_id[6] = bdaddr[4]; sys_id[7] = bdaddr[5];
   AttsSetAttr(DEVICE_INFO_SYSID_HANDLE, sizeof(sys_id), sys_id);

   // Set the advertising data
   memset((uint8_t*)adv_data_conn, 0, sizeof(adv_data_conn));
   AppAdvSetData(APP_ADV_DATA_CONNECTABLE, 0, (uint8_t*)adv_data_conn);
   AppAdvSetAdValue(APP_ADV_DATA_CONNECTABLE, DM_ADV_TYPE_FLAGS, sizeof(adv_data_flags), (uint8_t*)adv_data_flags);
   AppAdvSetAdValue(APP_ADV_DATA_CONNECTABLE, DM_ADV_TYPE_LOCAL_NAME, sizeof(adv_local_name), (uint8_t*)adv_local_name);
   AppAdvSetAdValue(APP_ADV_DATA_CONNECTABLE, DM_ADV_TYPE_MANUFACTURER, sizeof(current_ranging_role), (uint8_t*)current_ranging_role);

   // Set the scan response data
   memset((uint8_t*)scan_data_conn, 0, sizeof(scan_data_conn));
   AppAdvSetData(APP_SCAN_DATA_CONNECTABLE, 0, (uint8_t*)scan_data_conn);

   // Setup the advertising mode and power level
   AppSetBondable(FALSE);
   AppSetAdvType(DM_ADV_CONN_UNDIRECT);
   HciVscSetRfPowerLevelEx(TX_POWER_LEVEL_0P0_dBm);
   DmScanSetInterval(HCI_SCAN_PHY_LE_1M_BIT, (uint16_t*)&ble_master_cfg.scanInterval, (uint16_t*)&ble_master_cfg.scanWindow);
}


// TotTag BLE Event Callbacks ------------------------------------------------------------------------------------------

#ifndef AM_DEBUG_PRINTF
void hci_process_trace_data(uint8_t *dbg_data, uint32_t len) {}
#endif
void AppUiBtnPressed(void) {}
void appUiTimerExpired(wsfMsgHdr_t *pMsg) {}
void appUiBtnPoll(void) {}

void am_timer03_isr(void)
{
   // Force stop the BLE scanning cycle
   am_hal_timer_interrupt_clear(AM_HAL_TIMER_MASK(BLE_SCAN_PROBLEM_TIMER_NUMBER, AM_HAL_TIMER_COMPARE_BOTH));
   am_hal_timer_clear(BLE_SCAN_PROBLEM_TIMER_NUMBER);
   DmScanStop();
}

static void deviceManagerCallback(dmEvt_t *pDmEvt)
{
   // Handle the Device Manager message based on its type
   AppSlaveProcDmMsg(pDmEvt);
   switch (pDmEvt->hdr.event)
   {
      case DM_RESET_CMPL_IND:
         print("TotTag BLE: deviceManagerCallback: Received DM_RESET_CMPL_IND\n");
         if (first_initialization)
            AttsCalculateDbHash();
         is_advertising = is_scanning = first_initialization = false;
         is_initialized = true;
         if (expected_advertising)
            bluetooth_start_advertising();
         if (expected_scanning)
            bluetooth_start_scanning();
         break;
      case DM_CONN_OPEN_IND:
         print("TotTag BLE: deviceManagerCallback: Received DM_CONN_OPEN_IND\n");
         is_connected = true;
         connection_mtu = AttGetMtu(pDmEvt->hdr.param);
         AttsCccInitTable(pDmEvt->hdr.param, NULL);
         break;
      case DM_CONN_CLOSE_IND:
         print("TotTag BLE: deviceManagerCallback: Received DM_CONN_CLOSE_IND\n");
         is_connected = ranges_requested = data_requested = false;
         AttsCccClearTable(pDmEvt->hdr.param);
         break;
      case DM_ADV_START_IND:
         print("TotTag BLE: deviceManagerCallback: Received DM_ADV_START_IND\n");
         is_advertising = (pDmEvt->hdr.status == HCI_SUCCESS);
         if (!is_advertising)
            bluetooth_start_advertising();
         break;
      case DM_ADV_STOP_IND:
         print("TotTag BLE: deviceManagerCallback: Received DM_ADV_STOP_IND\n");
         is_advertising = false;
         if (is_initialized && expected_advertising)
            bluetooth_start_advertising();
         break;
      case DM_SCAN_START_IND:
         print("TotTag BLE: deviceManagerCallback: Received DM_SCAN_START_IND\n");
         is_scanning = (pDmEvt->hdr.status == HCI_SUCCESS);
         if (!is_scanning)
            bluetooth_start_scanning();
         break;
      case DM_SCAN_STOP_IND:
         print("TotTag BLE: deviceManagerCallback: Received DM_SCAN_STOP_IND\n");
         is_scanning = false;
         if (is_initialized && expected_scanning)
            bluetooth_start_scanning();
         break;
      case DM_SCAN_REPORT_IND:
      {
         uint8_t *nameLengthData = DmFindAdType(DM_ADV_TYPE_LOCAL_NAME, pDmEvt->scanReport.len, pDmEvt->scanReport.pData);
         uint8_t *rangingRoleData = DmFindAdType(DM_ADV_TYPE_MANUFACTURER, pDmEvt->scanReport.len, pDmEvt->scanReport.pData);
         if (nameLengthData && rangingRoleData && (*nameLengthData == (1 + sizeof(adv_local_name))) && (*rangingRoleData == (1 + sizeof(current_ranging_role))) &&
               (memcmp(adv_local_name, nameLengthData + 2, sizeof(adv_local_name)) == 0) && (current_ranging_role[0] == rangingRoleData[2]) && (current_ranging_role[1] == rangingRoleData[3]))
         {
            print("TotTag BLE: Found TotTag: %02x:%02x:%02x:%02x:%02x:%02x rssi: %d\n",
                  pDmEvt->scanReport.addr[5], pDmEvt->scanReport.addr[4], pDmEvt->scanReport.addr[3],
                  pDmEvt->scanReport.addr[2], pDmEvt->scanReport.addr[1], pDmEvt->scanReport.addr[0], pDmEvt->scanReport.rssi);
            if (discovery_callback)
               discovery_callback(pDmEvt->scanReport.addr, rangingRoleData[4]);
         }
         break;
      }
      case DM_PHY_UPDATE_IND:
         print("TotTag BLE: deviceManagerCallback: Negotiated PHY: RX = %d, TX = %d\n", pDmEvt->phyUpdate.rxPhy, pDmEvt->phyUpdate.txPhy);
         break;
      case DM_HW_ERROR_IND:
         print("TotTag BLE: deviceManagerCallback: Received DM_HW_ERROR_IND...Rebooting BLE\n");
         bluetooth_set_uninitialized();
         HciDrvRadioShutdown();
         HciDrvRadioBoot(false);
         DmDevReset();
         break;
      default:
         print("TotTag BLE: deviceManagerCallback: Received Event ID %d\n", pDmEvt->hdr.event);
         break;
   }
}

static void attProtocolCallback(attEvt_t *pEvt)
{
   // Handle the ATT Protocol message based on its type
   switch (pEvt->hdr.event)
   {
      case ATT_MTU_UPDATE_IND:
         print("TotTag BLE: attProtocolCallback: Negotiated MTU = %u\n", (uint32_t)pEvt->mtu);
         connection_mtu = pEvt->mtu;
         break;
      case ATTS_HANDLE_VALUE_CNF:
         print("TotTag BLE: attProtocolCallback: Data Notify Completed = %u\n", (uint32_t)pEvt->hdr.status);
         if ((pEvt->hdr.status == ATT_SUCCESS) && (pEvt->handle == MAINTENANCE_RESULT_HANDLE) && data_requested)
            continueSendingLogData((dmConnId_t)pEvt->hdr.param, connection_mtu - 3);
         break;
      default:
         print("TotTag BLE: attProtocolCallback: Received Event ID %d\n", pEvt->hdr.event);
         break;
   }
}

static void cccCallback(attsCccEvt_t *pEvt)
{
   // Handle various BLE notification requests
   print("TotTag BLE: cccCallback: index = %d, handle = %d, value = %d\n", pEvt->idx, pEvt->handle, pEvt->value);
   if (pEvt->idx == TOTTAG_RANGING_CCC_IDX)
      ranges_requested = (pEvt->value == ATT_CLIENT_CFG_NOTIFY);
   else if (pEvt->idx == TOTTAG_MAINTENANCE_RESULT_CCC_IDX)
      data_requested = (pEvt->value == ATT_CLIENT_CFG_INDICATE);
}


// Public API Functions ------------------------------------------------------------------------------------------------

void bluetooth_init(uint8_t* uid)
{
   // Initialize static variables
   const uint8_t ranging_role[] = { BLUETOOTH_COMPANY_ID, 0x00 };
   memcpy((uint8_t*)current_ranging_role, ranging_role, sizeof(ranging_role));
   data_requested = expected_scanning = expected_advertising = is_initialized = false;
   is_scanning = is_advertising = is_connected = ranges_requested = false;
   first_initialization = true;
   discovery_callback = NULL;

   // Store all BLE configuration pointers
   pAppAdvCfg = (appAdvCfg_t*)&ble_adv_cfg;
   pAppMasterCfg = (appMasterCfg_t*)&ble_master_cfg;
   pAppSlaveCfg = (appSlaveCfg_t*)&ble_slave_cfg;
   pAppSecCfg = (appSecCfg_t*)&ble_sec_cfg;
   pAppUpdateCfg = (appUpdateCfg_t*)&ble_update_cfg;
   pAttCfg = (attCfg_t*)&ble_att_cfg;

   // Initialize the BLE scanning error detection timer
   am_hal_timer_config_t scan_error_timer_config;
   am_hal_timer_default_config_set(&scan_error_timer_config);
   scan_error_timer_config.ui32Compare0 = (uint32_t)((BLE_SCANNING_DURATION_MS / 700) * BLE_SCAN_PROBLEM_TIMER_TICK_RATE_HZ);
   am_hal_timer_config(BLE_SCAN_PROBLEM_TIMER_NUMBER, &scan_error_timer_config);
   am_hal_timer_interrupt_enable(AM_HAL_TIMER_MASK(BLE_SCAN_PROBLEM_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));
   NVIC_SetPriority(TIMER0_IRQn + BLE_SCAN_PROBLEM_TIMER_NUMBER, NVIC_configKERNEL_INTERRUPT_PRIORITY);
   NVIC_SetPriority(COOPER_IOM_IRQn, NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY);
   NVIC_SetPriority(AM_COOPER_IRQn, NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY);
   NVIC_EnableIRQ(TIMER0_IRQn + BLE_SCAN_PROBLEM_TIMER_NUMBER);

   // Set the Bluetooth address and boot the BLE radio
   HciVscSetCustom_BDAddr(uid);
   configASSERT0(HciDrvRadioBoot(false));
   print("TotTag BLE: Initialized with address %02X:%02X:%02X:%02X:%02X:%02X\n", uid[5], uid[4], uid[3], uid[2], uid[1], uid[0]);
}

void bluetooth_deinit(void)
{
   // Stop all running timers
   am_hal_timer_disable(BLE_SCAN_PROBLEM_TIMER_NUMBER);
   NVIC_DisableIRQ(TIMER0_IRQn + BLE_SCAN_PROBLEM_TIMER_NUMBER);
   am_hal_timer_interrupt_disable(AM_HAL_TIMER_MASK(BLE_SCAN_PROBLEM_TIMER_NUMBER, AM_HAL_TIMER_COMPARE0));

   // Shut down the BLE controller
   HciDrvRadioShutdown();
   NVIC_DisableIRQ(AM_COOPER_IRQn);
   NVIC_DisableIRQ(COOPER_IOM_IRQn);
   is_initialized = is_advertising = is_scanning = false;

   // Put the BLE controller into reset
   am_hal_gpio_state_write(AM_DEVICES_BLECTRLR_RESET_PIN, AM_HAL_GPIO_OUTPUT_CLEAR);
   am_hal_gpio_pinconfig(AM_DEVICES_BLECTRLR_RESET_PIN, am_hal_gpio_pincfg_output);
   am_hal_gpio_state_write(AM_DEVICES_BLECTRLR_RESET_PIN, AM_HAL_GPIO_OUTPUT_SET);
   am_hal_gpio_state_write(AM_DEVICES_BLECTRLR_RESET_PIN, AM_HAL_GPIO_OUTPUT_CLEAR);
}

void bluetooth_reset(void)
{
   // Shutdown and reboot the BLE controller
   bluetooth_set_uninitialized();
   HciDrvRadioShutdown();
   HciDrvRadioBoot(false);
   DmDevReset();
}

void bluetooth_start(void)
{
   // Register all BLE protocol stack callback functions
   AppSlaveInit();
   DmRegister(deviceManagerCallback);
   DmConnRegister(DM_CLIENT_ID_APP, deviceManagerCallback);
   DmSetDefaultPhy(HCI_ALL_PHY_ALL_PREFERENCES, HCI_PHY_LE_2M_BIT, HCI_PHY_LE_2M_BIT);
   AttRegister(attProtocolCallback);
   AttsCccRegister(TOTTAG_NUM_CCC_CHARACTERISTICS, (attsCccSet_t*)characteristicSet, cccCallback);

   // Initialize all TotTag BLE services
   gapGattRegisterCallbacks(GattReadCback, GattWriteCback);
   gapGattAddGroup();
   deviceInfoAddGroup();
   liveStatsRegisterCallbacks(handleLiveStatsRead, handleLiveStatsWrite);
   liveStatsAddGroup();
   deviceMaintenanceRegisterCallbacks(handleDeviceMaintenanceRead, handleDeviceMaintenanceWrite);
   deviceMaintenanceAddGroup();

   // Set the GATT Service Changed CCCD index
   GattSetSvcChangedIdx(TOTTAG_GATT_SERVICE_CHANGED_CCC_IDX);

   // Reset the BLE device
   DmDevReset();
}

bool bluetooth_is_initialized(void)
{
   // Return whether the BLE stack has been fully initialized
   return is_initialized;
}

void bluetooth_set_uninitialized(void)
{
   // Force all initialization and activity flags to false
   is_initialized = is_advertising = is_scanning = false;
   am_hal_timer_disable(BLE_SCAN_PROBLEM_TIMER_NUMBER);
}

void bluetooth_register_discovery_callback(ble_discovery_callback_t callback)
{
   // Store the device discovery callback
   discovery_callback = callback;
}

uint8_t bluetooth_get_current_ranging_role(void)
{
   return current_ranging_role[2];
}

void bluetooth_set_current_ranging_role(uint8_t ranging_role)
{
   // Update the current device ranging role in the BLE advertisements
   is_advertising = false;
   current_ranging_role[2] = ranging_role;
#ifndef _TEST_RANGING_TASK
   AppAdvSetAdValue(APP_ADV_DATA_CONNECTABLE, DM_ADV_TYPE_MANUFACTURER, sizeof(current_ranging_role), (uint8_t*)current_ranging_role);
   AppAdvStop();
#endif
}

void bluetooth_write_range_results(const uint8_t *results, uint16_t results_length)
{
   // Update the current set of ranging data
   if (ranges_requested)
      updateRangeResults(AppConnIsOpen(), results, results_length);
}

void bluetooth_start_advertising(void)
{
   // Attempt to begin advertising
   expected_advertising = true;
   if (is_initialized && !is_advertising)
   {
      advertising_setup();
      print("TotTag BLE: Starting advertising...\n");
      AppAdvStart(APP_MODE_CONNECTABLE);
   }
}

void bluetooth_stop_advertising(void)
{
   // Attempt to stop advertising
   expected_advertising = false;
   if (is_initialized && is_advertising)
   {
      print("TotTag BLE: Stopping advertising...\n");
      AppAdvStop();
   }
}

bool bluetooth_is_advertising(void)
{
   // Return whether advertising is currently enabled
   return is_advertising;
}

void bluetooth_start_scanning(void)
{
   // Attempt to start scanning
   expected_scanning = true;
   if (is_initialized && !is_scanning)
   {
      print("TotTag BLE: Starting scanning...\n");
      am_hal_timer_clear(BLE_SCAN_PROBLEM_TIMER_NUMBER);
      DmScanStart(HCI_SCAN_PHY_LE_1M_BIT, ble_master_cfg.discMode, &ble_master_cfg.scanType, TRUE, ble_master_cfg.scanDuration, 0);
   }
}

void bluetooth_stop_scanning(void)
{
   // Attempt to stop scanning
   expected_scanning = false;
   if (is_initialized && is_scanning)
   {
      print("TotTag BLE: Stopping scanning...\n");
      am_hal_timer_disable(BLE_SCAN_PROBLEM_TIMER_NUMBER);
      DmScanStop();
   }
}

void bluetooth_reset_scanning(void)
{
   // Attempt to stop scanning without changing the scanning expectation
   if (is_initialized && is_scanning)
   {
      am_hal_timer_clear(BLE_SCAN_PROBLEM_TIMER_NUMBER);
      DmScanStop();
   }
}

bool bluetooth_is_scanning(void)
{
   // Return whether scanning is currently enabled
   return is_scanning;
}

bool bluetooth_is_connected(void)
{
   // Return whether we are actively connected to another device
   return is_connected;
}

void bluetooth_clear_whitelist(void)
{
   // Clear and disable the whitelist
   DmDevWhiteListClear();
   DmDevSetFilterPolicy(DM_FILT_POLICY_MODE_SCAN, HCI_FILT_NONE);
   DmDevSetFilterPolicy(DM_FILT_POLICY_MODE_ADV, HCI_ADV_FILT_NONE);
}

void bluetooth_add_device_to_whitelist(uint8_t* uid)
{
#ifndef _TEST_BLE_RANGING_TASK
   // Add the specified device to the whitelist
   DmDevWhiteListAdd(DM_ADDR_PUBLIC, uid);
   //DmDevSetFilterPolicy(DM_FILT_POLICY_MODE_ADV, HCI_ADV_FILT_CONN);
   DmDevSetFilterPolicy(DM_FILT_POLICY_MODE_SCAN, HCI_FILT_WHITE_LIST);
#endif
}
