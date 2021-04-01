// Header inclusions ---------------------------------------------------------------------------------------------------

#include <stdlib.h>
#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_config.h"
#include "bluetooth.h"
#include "nrf_ble_es.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "rtc.h"


// Bluetooth state definitions -----------------------------------------------------------------------------------------

NRF_BLE_GATT_DEF(_gatt);
NRF_BLE_QWR_DEF(_qwr);
BLE_ADVERTISING_DEF(_advertising);


// Bluetooth configuration variables -----------------------------------------------------------------------------------

static const ble_uuid128_t CARRIER_BLE_SERV_LONG_UUID = { .uuid128 = { 0x2e, 0x5d, 0x5e, 0x39, 0x31, 0x52, 0x45, 0x0c, 0x90, 0xee, 0x3f, 0xa2, 0x52, 0x31, 0x8c, 0xd6 } };  // Service UUID
static ble_uuid_t _adv_uuids[] = { { PHYSWEB_SERVICE_ID, BLE_UUID_TYPE_BLE } };  // Advertisement UUIDs: Eddystone
static ble_uuid_t _sr_uuids[] = { { BLE_UUID_DEVICE_INFORMATION_SERVICE, BLE_UUID_TYPE_BLE }, { CARRIER_BLE_SERV_SHORT_UUID, BLE_UUID_TYPE_VENDOR_BEGIN } };  // Scan Response UUIDs
static ble_gatts_char_handles_t carrier_ble_char_location_handle = { .value_handle = CARRIER_BLE_CHAR_LOCATION };
static ble_gatts_char_handles_t carrier_ble_char_config_handle = { .value_handle = CARRIER_BLE_CHAR_CONFIG };
static ble_gatts_char_handles_t _carrier_ble_char_enable_handle = { .value_handle = CARRIER_BLE_CHAR_ENABLE };
static ble_gatts_char_handles_t carrier_ble_char_calibration_handle = { .value_handle = CARRIER_BLE_CHAR_CALIBRATION };
static uint16_t _carrier_ble_service_handle = 0, _carrier_ble_conn_handle = BLE_CONN_HANDLE_INVALID;
static uint8_t _carrier_ble_address[BLE_GAP_ADDR_LEN] = { 0 }, _scan_buffer_data[BLE_GAP_SCAN_BUFFER_MIN] = { 0 };
static ble_gap_scan_params_t const _scan_params = { .active = 0, .channel_mask = { 0, 0, 0, 0, 0 }, .extended = 0, .interval = APP_SCAN_INTERVAL, .window = APP_SCAN_WINDOW, .timeout = BLE_GAP_SCAN_TIMEOUT_UNLIMITED, .scan_phys = BLE_GAP_PHY_1MBPS, .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL };
static ble_gap_conn_params_t const _connection_params = { MIN_CONN_INTERVAL, MAX_CONN_INTERVAL, SLAVE_LATENCY, CONN_SUP_TIMEOUT };
static ble_data_t _scan_buffer = { _scan_buffer_data, BLE_GAP_SCAN_BUFFER_MIN };
static ble_gap_addr_t _wl_addr_base = { 0 };
static ble_advertising_init_t _adv_init = { 0 };
static ble_advdata_manuf_data_t _manuf_data_adv = { 0 };
static ble_advdata_service_data_t _service_data = { 0 };
static uint8_t _app_ble_advdata[APP_BLE_ADVDATA_LENGTH] = { 0 }, _scratch_eui[BLE_GAP_ADDR_LEN] = { 0 };
static uint8_t _scheduler_eui[BLE_GAP_ADDR_LEN] = { 0 }, _highest_discovered_eui[BLE_GAP_ADDR_LEN] = { 0 };
static const uint8_t _empty_eui[BLE_GAP_ADDR_LEN] = { 0 };
static nrfx_atomic_flag_t *_squarepoint_enabled_flag = NULL, *_squarepoint_running_flag = NULL, *_ble_scanning_flag = NULL;
static nrfx_atomic_u32_t _network_discovered_counter = 0, *_calibration_index = NULL;
static device_role_t _device_role = HYBRID;


// Helper functions and prototypes -------------------------------------------------------------------------------------

static void nrf_qwr_error_handler(uint32_t nrf_error) { APP_ERROR_HANDLER(nrf_error); }
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context);
static void on_adv_evt(ble_adv_evt_t ble_adv_evt);
static bool addr_in_whitelist(ble_gap_addr_t const *ble_addr) { return (memcmp(ble_addr->addr + APP_BLE_ADV_SCHED_EUI_LENGTH, _wl_addr_base.addr + APP_BLE_ADV_SCHED_EUI_LENGTH, sizeof(_wl_addr_base.addr) - APP_BLE_ADV_SCHED_EUI_LENGTH) == 0); }
static uint8_t ascii_to_i(uint8_t number)
{
   // Convert single digit of ASCII in hex to a number
   if ((number >= '0') && (number <= '9'))
      return (number - (uint8_t)'0');
   else if ((number >= 'A') && (number <= 'F'))
      return (number - (uint8_t)'A' + (uint8_t)10);
   else if ((number >= 'a') && (number <= 'f'))
      return (number - (uint8_t)'a' + (uint8_t)10);
   else
      log_printf("ERROR: Tried converting non-hex ASCII: %i\n", number);
   return 0;
}


// Bluetooth initialization --------------------------------------------------------------------------------------------

static void ble_stack_init(void)
{
   // Initialize the BLE stack, including the SoftDevice and the BLE event interrupt
   uint32_t ram_start = 0;
   APP_ERROR_CHECK(nrf_sdh_enable_request());
   APP_ERROR_CHECK(nrf_sdh_ble_app_ram_start_get(&ram_start));

   // Configure the maximum number of BLE connections
   ble_cfg_t ble_cfg;
   memset(&ble_cfg, 0, sizeof(ble_cfg));
   ble_cfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;
   ble_cfg.conn_cfg.params.gap_conn_cfg.conn_count = NRF_SDH_BLE_TOTAL_LINK_COUNT;
   ble_cfg.conn_cfg.params.gap_conn_cfg.event_length = NRF_SDH_BLE_GAP_EVENT_LENGTH;
   APP_ERROR_CHECK(sd_ble_cfg_set(BLE_CONN_CFG_GAP, &ble_cfg, ram_start));

   // Configure the connection roles
   memset(&ble_cfg, 0, sizeof(ble_cfg));
   ble_cfg.gap_cfg.role_count_cfg.periph_role_count = NRF_SDH_BLE_PERIPHERAL_LINK_COUNT;
   ble_cfg.gap_cfg.role_count_cfg.central_role_count = NRF_SDH_BLE_CENTRAL_LINK_COUNT;
   ble_cfg.gap_cfg.role_count_cfg.central_sec_count = BLE_GAP_ROLE_COUNT_CENTRAL_SEC_DEFAULT;
   APP_ERROR_CHECK(sd_ble_cfg_set(BLE_GAP_CFG_ROLE_COUNT, &ble_cfg, ram_start));

   // Configure the max ATT MTU size
   memset(&ble_cfg, 0x00, sizeof(ble_cfg));
   ble_cfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG;
   ble_cfg.conn_cfg.params.gatt_conn_cfg.att_mtu = NRF_SDH_BLE_GATT_MAX_MTU_SIZE;
   APP_ERROR_CHECK(sd_ble_cfg_set(BLE_CONN_CFG_GATT, &ble_cfg, ram_start));

   // Configure the number of custom UUIDS
   memset(&ble_cfg, 0, sizeof(ble_cfg));
   ble_cfg.common_cfg.vs_uuid_cfg.vs_uuid_count = NRF_SDH_BLE_VS_UUID_COUNT;
   APP_ERROR_CHECK(sd_ble_cfg_set(BLE_COMMON_CFG_VS_UUID, &ble_cfg, ram_start));

   // Configure the GATTS attribute table
   memset(&ble_cfg, 0x00, sizeof(ble_cfg));
   ble_cfg.gatts_cfg.attr_tab_size.attr_tab_size = NRF_SDH_BLE_GATTS_ATTR_TAB_SIZE;
   APP_ERROR_CHECK(sd_ble_cfg_set(BLE_GATTS_CFG_ATTR_TAB_SIZE, &ble_cfg, ram_start));

   // Configure the Service Changed characteristic
   memset(&ble_cfg, 0x00, sizeof(ble_cfg));
   ble_cfg.gatts_cfg.service_changed.service_changed = NRF_SDH_BLE_SERVICE_CHANGED;
   APP_ERROR_CHECK(sd_ble_cfg_set(BLE_GATTS_CFG_SERVICE_CHANGED, &ble_cfg, ram_start));

   // Enable the BLE stack and register a BLE event handler
   APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));
   NRF_SDH_BLE_OBSERVER(_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

static uint8_t gap_params_init(void)
{
   // Set up all necessary GAP (Generic Access Profile) parameters of the device, permissions, and appearance
   ble_gap_conn_sec_mode_t sec_mode;
   uint8_t device_name[] = APP_DEVICE_NAME;
   BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
   APP_ERROR_CHECK(sd_ble_gap_device_name_set(&sec_mode, device_name, (uint16_t)strlen((const char*)device_name)));

   // Set BLE address
   ble_gap_addr_t gap_addr = { .addr_type = BLE_GAP_ADDR_TYPE_PUBLIC };
   memcpy(_carrier_ble_address, (uint8_t*)DEVICE_ID_MEMORY, BLE_GAP_ADDR_LEN);
   if ((_carrier_ble_address[5] != 0xc0) || (_carrier_ble_address[4] != 0x98))
      return 0;
   log_printf("INFO: Bluetooth address: %02x:%02x:%02x:%02x:%02x:%02x\n", _carrier_ble_address[5], _carrier_ble_address[4], _carrier_ble_address[3], _carrier_ble_address[2], _carrier_ble_address[1], _carrier_ble_address[0]);
   memcpy(gap_addr.addr, _carrier_ble_address, BLE_GAP_ADDR_LEN);
   memcpy(_scratch_eui, _carrier_ble_address, sizeof(_scratch_eui));
   APP_ERROR_CHECK(sd_ble_gap_addr_set(&gap_addr));

   // Setup connection parameters
   APP_ERROR_CHECK(sd_ble_gap_ppcp_set(&_connection_params));
   return 1;
}

static void gatt_init(void)
{
   APP_ERROR_CHECK(nrf_ble_gatt_init(&_gatt, NULL));
}

static void advertising_init(void)
{
   // Custom advertisement data
   memset(&_adv_init, 0, sizeof(_adv_init));
   memset(&_app_ble_advdata, 0, sizeof(_app_ble_advdata));
   _manuf_data_adv.company_identifier = APP_COMPANY_IDENTIFIER;  // UMich's Company ID
   _manuf_data_adv.data.p_data = _app_ble_advdata;
   _manuf_data_adv.data.size = sizeof(_app_ble_advdata);
   _adv_init.advdata.p_manuf_specific_data = &_manuf_data_adv;
   _adv_init.advdata.name_type = BLE_ADVDATA_NO_NAME;

   // Physical Web data
   const char *url_str = PHYSWEB_URL;
   const uint8_t header_len = 3;
   uint8_t url_frame_length = header_len + strlen(url_str);  // Change to 4 if URLEND is applied
   uint8_t m_url_frame[url_frame_length];
   m_url_frame[0] = PHYSWEB_URL_TYPE;
   m_url_frame[1] = PHYSWEB_TX_POWER;
   m_url_frame[2] = PHYSWEB_URLSCHEME_HTTPS;
   for (uint8_t i = 0; i < strlen(url_str); ++i)
      m_url_frame[i + 3] = url_str[i];

   // Advertise Physical Web service
   _service_data.service_uuid = PHYSWEB_SERVICE_ID;
   _service_data.data.p_data = m_url_frame;
   _service_data.data.size = url_frame_length;
   _adv_init.advdata.p_service_data_array = &_service_data;
   _adv_init.advdata.service_data_count = 1;
   _adv_init.advdata.include_appearance = false;
   _adv_init.advdata.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
   _adv_init.advdata.uuids_complete.uuid_cnt = sizeof(_adv_uuids) / sizeof(_adv_uuids[0]);
   _adv_init.advdata.uuids_complete.p_uuids = _adv_uuids;

   // Scan response
   _adv_init.srdata.name_type = BLE_ADVDATA_FULL_NAME;
   _adv_init.srdata.uuids_complete.uuid_cnt = sizeof(_sr_uuids) / sizeof(_sr_uuids[0]);
   _adv_init.srdata.uuids_complete.p_uuids = _sr_uuids;
   _adv_init.config.ble_adv_fast_enabled = true;
#ifndef BLE_CALIBRATION
   _adv_init.config.ble_adv_fast_interval = (uint32_t)APP_ADV_INTERVAL;
#else
    adv_init.config.ble_adv_fast_interval = (uint32_t)APP_ADV_INTERVAL_CALIBRATION;
#endif

   // Define Event handler
   _adv_init.evt_handler = on_adv_evt;
   APP_ERROR_CHECK(ble_advertising_init(&_advertising, &_adv_init));
   ble_advertising_conn_cfg_tag_set(&_advertising, APP_BLE_CONN_CFG_TAG);
}

static void ble_characteristic_add(uint8_t read, uint8_t write, uint8_t notify, uint8_t vlen, uint8_t data_len, volatile uint8_t *data, uint16_t uuid, ble_gatts_char_handles_t *char_handle)
{
   // Add characteristics
   ble_uuid128_t base_uuid = CARRIER_BLE_SERV_LONG_UUID;
   ble_uuid_t char_uuid = { .uuid = uuid };
   APP_ERROR_CHECK(sd_ble_uuid_vs_add(&base_uuid, &char_uuid.type));

   // Add read/write properties to our characteristic
   ble_gatts_char_md_t char_md;
   memset(&char_md, 0, sizeof(char_md));
   char_md.char_props.read = read;
   char_md.char_props.write = write;
   char_md.char_props.notify = notify;

   // Configure Client Characteristic Configuration Descriptor (CCCD) metadata and add to char_md structure
   ble_gatts_attr_md_t cccd_md;
   memset(&cccd_md, 0, sizeof(cccd_md));
   BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
   BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
   cccd_md.vloc = BLE_GATTS_VLOC_STACK;
   char_md.p_cccd_md = &cccd_md;

   // Configure the attribute metadata
   ble_gatts_attr_md_t attr_md;
   memset(&attr_md, 0, sizeof(attr_md));
   BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
   BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
   attr_md.vloc = BLE_GATTS_VLOC_STACK;
   attr_md.vlen = vlen;

   // Configure the characteristic value attribute
   ble_gatts_attr_t attr_char_value;
   memset(&attr_char_value, 0, sizeof(attr_char_value));
   attr_char_value.p_uuid = &char_uuid;
   attr_char_value.p_attr_md = &attr_md;

   // Set characteristic length in number of bytes
   attr_char_value.max_len = data_len;
   attr_char_value.init_len = 0;
   attr_char_value.p_value = data;

   // Add our new characteristics to the service
   APP_ERROR_CHECK(sd_ble_gatts_characteristic_add(_carrier_ble_service_handle, &char_md, &attr_char_value, char_handle));
}

static void services_init(void)
{
   // Initialize the Queued Write Module
   nrf_ble_qwr_init_t qwr_init = { 0 };
   qwr_init.error_handler = nrf_qwr_error_handler;
   APP_ERROR_CHECK(nrf_ble_qwr_init(&_qwr, &qwr_init));

   // Initialize our own BLE service
   ble_uuid128_t base_uuid = CARRIER_BLE_SERV_LONG_UUID;
   ble_uuid_t service_uuid = { .uuid = CARRIER_BLE_SERV_SHORT_UUID };
   APP_ERROR_CHECK(sd_ble_uuid_vs_add(&base_uuid, &service_uuid.type));
   APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &service_uuid, &_carrier_ble_service_handle));

   // Add characteristics
   ble_characteristic_add(1, 0, 1, 0, 128, NULL, CARRIER_BLE_CHAR_LOCATION, &carrier_ble_char_location_handle);
   ble_characteristic_add(1, 1, 0, 0, 28, (volatile uint8_t*)&_device_role, CARRIER_BLE_CHAR_CONFIG, &carrier_ble_char_config_handle);
   ble_characteristic_add(1, 1, 0, 0, 10, (volatile uint8_t*)_squarepoint_enabled_flag, CARRIER_BLE_CHAR_ENABLE, &_carrier_ble_char_enable_handle);
   ble_characteristic_add(1, 1, 0, 0, 14, (volatile uint8_t*)_calibration_index, CARRIER_BLE_CHAR_CALIBRATION, &carrier_ble_char_calibration_handle);
}

static void central_init(void)
{
   // Create whitelist for all our devices
   ble_gap_addr_t whitelisted_ble_address = { .addr_type = BLE_GAP_ADDR_TYPE_PUBLIC, .addr = { APP_BLE_ADDR_MIN, APP_BLE_ADDR_MIN, 0x42, 0xe5, 0x98, 0xc0 } };
   memcpy(&_wl_addr_base, &whitelisted_ble_address, sizeof(ble_gap_addr_t));
}

static void conn_params_init(void)
{
   // Initializing the Connection Parameters module
   ble_conn_params_init_t cp_init;
   memset(&cp_init, 0, sizeof(cp_init));
   cp_init.p_conn_params = NULL;//&_connection_params;
   cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
   cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
   cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
   cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
   cp_init.disconnect_on_fail = true;  // Can also add a on_conn_params_evt as a handler
   ret_code_t err_code = ble_conn_params_init(&cp_init);
   APP_ERROR_CHECK(err_code);
}


// Bluetooth callback and event functionality --------------------------------------------------------------------------

void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) { app_error_handler(DEAD_BEEF, line_num, p_file_name); }

static void on_scheduler_eui(const uint8_t* scheduler_eui, bool force_update)
{
   // Only reconfigure if scheduler is non-zero and has changed
   if ((memcmp(scheduler_eui, _scheduler_eui, sizeof(_scheduler_eui)) == 0) || (!force_update && (memcmp(scheduler_eui, _empty_eui, sizeof(_empty_eui)) == 0)))
      return;
   log_printf("INFO: Switched Scheduler EUI from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x\n",
         _scheduler_eui[5], _scheduler_eui[4], _scheduler_eui[3], _scheduler_eui[2], _scheduler_eui[1], _scheduler_eui[0],
         scheduler_eui[5], scheduler_eui[4], scheduler_eui[3], scheduler_eui[2], scheduler_eui[1], scheduler_eui[0]);
   memcpy(_scheduler_eui, scheduler_eui, sizeof(_scheduler_eui));

   // Stop BLE advertisements, update data, and restart
   ret_code_t err_code = sd_ble_gap_adv_stop(_advertising.adv_handle);
   bool should_reenable = (err_code == NRF_SUCCESS);
   memcpy(_app_ble_advdata, _scheduler_eui, APP_BLE_ADVDATA_LENGTH);
   APP_ERROR_CHECK(ble_advertising_init(&_advertising, &_adv_init));
   ble_advertising_conn_cfg_tag_set(&_advertising, APP_BLE_CONN_CFG_TAG);
   if (should_reenable)
      ble_start_advertising();
}

static uint32_t adv_report_parse(uint8_t type, const ble_data_t* p_advdata, ble_data_t* p_typedata)
{
   uint8_t *p_data = p_advdata->p_data;
   for (uint32_t index = 0; index < p_advdata->len; )
   {
      uint8_t field_length = p_data[index];
      uint8_t field_type = p_data[index + 1];
      if (field_type == type)
      {
         p_typedata->p_data = &p_data[index + 2];
         p_typedata->len = field_length;
         return NRFX_SUCCESS;
      }
      else
         index += field_length + 1;
   }
   return NRF_ERROR_NOT_FOUND;
}

static void on_adv_report(ble_gap_evt_adv_report_t const *p_adv_report)
{
   // Only handle non-response BLE packets from devices we know
   if (!p_adv_report->type.scan_response && addr_in_whitelist(&p_adv_report->peer_addr) && (memcmp(p_adv_report->peer_addr.addr, _empty_eui, sizeof(_empty_eui)) != 0))
   {
      // Update the highest discovered EUI in the network
      ble_data_t advdata = { 0 };
      nrfx_err_t err_code = adv_report_parse(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, &p_adv_report->data, &advdata);
      if (memcmp(p_adv_report->peer_addr.addr, _highest_discovered_eui, sizeof(_highest_discovered_eui)) > 0)
      {
         memcpy(_highest_discovered_eui, p_adv_report->peer_addr.addr, sizeof(_highest_discovered_eui));
         log_printf("INFO: Discovered new device: %02x:%02x:%02x:%02x:%02x:%02x\n", p_adv_report->peer_addr.addr[5], p_adv_report->peer_addr.addr[4], p_adv_report->peer_addr.addr[3], p_adv_report->peer_addr.addr[2], p_adv_report->peer_addr.addr[1], p_adv_report->peer_addr.addr[0]);
      }

      // Update the scheduler EUI
      if ((err_code == NRFX_SUCCESS) && (advdata.len == (1 + 2 + sizeof(_app_ble_advdata))) && (memcmp(advdata.p_data + 2, _empty_eui, APP_BLE_ADVDATA_LENGTH) != 0))
         ble_set_scheduler_eui(advdata.p_data + 2, APP_BLE_ADVDATA_LENGTH);

      // Reset the network discovery timeout counter
      nrfx_atomic_u32_store(&_network_discovered_counter, BLE_NETWORK_DISCOVERY_COUNTDOWN_VALUE);
   }
}

static void on_ble_write(const ble_evt_t *p_ble_evt)
{
   // Handle a BLE write event
   const ble_gatts_evt_write_t *p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;
   if (p_evt_write->handle == carrier_ble_char_config_handle.value_handle)
   {
      // Configure the device by role and epoch time
      log_printf("INFO: Received CONFIG evt: %s, length %hu\n", (const char*)p_evt_write->data, p_evt_write->len);
      const uint8_t expected_response_role_offset = 6, role_length = 1, time_length = 10;
      const uint8_t expected_response_time_offset = expected_response_role_offset + role_length + 8;
      uint8_t response_role = ascii_to_i(p_evt_write->data[expected_response_role_offset]);
      uint32_t response_time = 0;
      for (uint8_t i = expected_response_time_offset; i < (expected_response_time_offset + time_length); ++i)
         response_time = (10 * response_time) + ascii_to_i(p_evt_write->data[i]);
      rtc_set_current_time(response_time);
      if ((response_role > UNASSIGNED) && (response_role <= SUPPORTER))
         _device_role = response_role;
   }
   else if (p_evt_write->handle == _carrier_ble_char_enable_handle.value_handle)
   {
      // Enable or disable ranging
      log_printf("INFO: Received ENABLE evt: %s, length %hu\n", (const char*)p_evt_write->data, p_evt_write->len);
      const uint8_t expected_response_ranging_offset = 9;
      uint8_t response_ranging = ascii_to_i(p_evt_write->data[expected_response_ranging_offset]);
      if (response_ranging)
         nrfx_atomic_flag_set(_squarepoint_enabled_flag);
      else
         nrfx_atomic_flag_clear(_squarepoint_enabled_flag);
   }
   else if (p_evt_write->handle == carrier_ble_char_calibration_handle.value_handle)
   {
      // Start device calibration
      log_printf("INFO: Received CALIBRATION evt: %s, length %hu\n", (const char*)p_evt_write->data, p_evt_write->len);
      const uint8_t expected_response_calib_offset = 13;
      uint8_t response = ascii_to_i(p_evt_write->data[expected_response_calib_offset]);
      nrfx_atomic_u32_store(_calibration_index, response);
  }
   else
      log_printf("ERROR: Received unknown BLE event handle: %hu\n", p_evt_write->handle);
}

static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
   // Start advertisements if idle
   if (ble_adv_evt == BLE_ADV_EVT_IDLE)
   {
      ret_code_t err_code = sd_ble_gap_adv_set_configure(&_advertising.adv_handle, _advertising.p_adv_data, &_advertising.adv_params);
      if (err_code != NRF_ERROR_INVALID_STATE)
         APP_ERROR_CHECK(err_code);
      err_code = sd_ble_gap_adv_start(_advertising.adv_handle, _advertising.conn_cfg_tag);
      if (err_code != NRF_ERROR_INVALID_STATE)
         APP_ERROR_CHECK(err_code);
   }
}

void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context)
{
   switch (p_ble_evt->header.evt_id)
   {
      case BLE_GATTS_EVT_WRITE:
      {
         on_ble_write(p_ble_evt);
         break;
      }
      case BLE_GATTS_EVT_SYS_ATTR_MISSING:
      {
         ret_code_t err_code = sd_ble_gatts_sys_attr_set(p_ble_evt->evt.common_evt.conn_handle, NULL, 0, 0);
         APP_ERROR_CHECK(err_code);
         break;
      }
      case BLE_GAP_EVT_CONNECTED:
      {
         // Assign BLE connection handle
         _carrier_ble_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
         ret_code_t err_code = nrf_ble_qwr_conn_handle_assign(&_qwr, _carrier_ble_conn_handle);
         APP_ERROR_CHECK(err_code);

         // Continue advertising, but non-connectably
         _advertising.adv_params.properties.type = BLE_GAP_ADV_TYPE_NONCONNECTABLE_SCANNABLE_UNDIRECTED;

         // Note that ble_advertising_start() IGNORES some input parameters and sets them to defaults
         err_code = sd_ble_gap_adv_set_configure(&_advertising.adv_handle, _advertising.p_adv_data, &_advertising.adv_params);
         if (err_code != NRF_ERROR_INVALID_STATE)
            APP_ERROR_CHECK(err_code);
         err_code = sd_ble_gap_adv_start(_advertising.adv_handle, _advertising.conn_cfg_tag);
         if (err_code != NRF_ERROR_INVALID_STATE)
            APP_ERROR_CHECK(err_code);

         // Set initial CCCD attributes to NULL
         err_code = sd_ble_gatts_sys_attr_set(_carrier_ble_conn_handle, NULL, 0, 0);
         APP_ERROR_CHECK(err_code);
         break;
      }
      case BLE_GAP_EVT_DISCONNECTED:
      {
         // Go back to advertising connectably
         _carrier_ble_conn_handle = BLE_CONN_HANDLE_INVALID;
         _advertising.adv_params.properties.type = BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED;
         ret_code_t err_code = sd_ble_gap_adv_set_configure(&_advertising.adv_handle, _advertising.p_adv_data, &_advertising.adv_params);
         if (err_code != NRF_ERROR_INVALID_STATE)
            APP_ERROR_CHECK(err_code);
         err_code = sd_ble_gap_adv_start(_advertising.adv_handle, _advertising.conn_cfg_tag);
         if (err_code != NRF_ERROR_INVALID_STATE)
            APP_ERROR_CHECK(err_code);
         break;
      }
      case BLE_GAP_EVT_ADV_REPORT:
      {
         on_adv_report(&p_ble_evt->evt.gap_evt.params.adv_report);
         break;
      }
      case BLE_GAP_EVT_PHY_UPDATE_REQUEST:
      {
         ble_gap_phys_t const phys = { .rx_phys = BLE_GAP_PHY_AUTO, .tx_phys = BLE_GAP_PHY_AUTO, };
         APP_ERROR_CHECK(sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys));
         break;
      }
      case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST:
      {
         APP_ERROR_CHECK(sd_ble_gap_conn_param_update(p_ble_evt->evt.gap_evt.conn_handle, &p_ble_evt->evt.gap_evt.params.conn_param_update_request.conn_params));
         break;
      }
      case BLE_GAP_EVT_TIMEOUT:
      {
         // Only connection attempts can timeout
         if (p_ble_evt->evt.gap_evt.params.timeout.src == BLE_GAP_TIMEOUT_SRC_CONN)
            log_printf("WARNING: BLE connection attempts timed out\n");
         break;
      }
      case BLE_GATTC_EVT_TIMEOUT:
      {
         APP_ERROR_CHECK(sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
         break;
      }
      case BLE_GATTS_EVT_TIMEOUT:
      {
         APP_ERROR_CHECK(sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
         break;
      }
      default:
         break;
   }

   // Scanning stops upon reception of a BLE response packet
   nrfx_atomic_flag_clear(_ble_scanning_flag);
}


// Public Bluetooth API ------------------------------------------------------------------------------------------------

nrfx_err_t ble_init(nrfx_atomic_flag_t* squarepoint_enabled_flag, nrfx_atomic_flag_t* squarepoint_running_flag, nrfx_atomic_flag_t* ble_is_scanning_flag, nrfx_atomic_u32_t* calibration_index)
{
   _squarepoint_enabled_flag = squarepoint_enabled_flag;
   _squarepoint_running_flag = squarepoint_running_flag;
   _ble_scanning_flag = ble_is_scanning_flag;
   _calibration_index = calibration_index;
   ble_stack_init();
   if (!gap_params_init())
      return NRF_ERROR_INVALID_STATE;
   gatt_init();
   services_init();
   advertising_init();
   central_init();
   conn_params_init();
   return NRF_SUCCESS;
}
uint8_t ble_get_device_role(void) { return _device_role; }
const uint8_t* ble_get_eui(void) { return _carrier_ble_address; }
const uint8_t* ble_get_empty_eui(void) { return _empty_eui; }
const uint8_t* ble_get_scheduler_eui(void) { return _scheduler_eui; }
const uint8_t* ble_get_highest_network_eui(void) { return _highest_discovered_eui; }
nrfx_err_t ble_start_advertising(void) { return ble_advertising_start(&_advertising, BLE_ADV_MODE_FAST); }
nrfx_err_t ble_start_scanning(void)
{
   nrfx_err_t err_code = sd_ble_gap_scan_start(NULL, &_scan_buffer);
   if (err_code == NRF_ERROR_INVALID_STATE)
      err_code = sd_ble_gap_scan_start(&_scan_params, &_scan_buffer);
   return err_code;
}
void ble_stop_advertising(void)
{
   ble_advertising_start(&_advertising, BLE_ADV_MODE_IDLE);
   sd_ble_gap_adv_stop(_advertising.adv_handle);
}
void ble_stop_scanning(void) { sd_ble_gap_scan_stop(); }
void ble_clear_scheduler_eui(void) { on_scheduler_eui(_empty_eui, true); }
void ble_set_scheduler_eui(const uint8_t* eui, uint8_t num_eui_bytes)
{
   if (memcmp(eui, _empty_eui, num_eui_bytes) != 0)
   {
      memcpy(_scratch_eui, eui, num_eui_bytes);
      on_scheduler_eui(_scratch_eui, false);
   }
}
void ble_update_ranging_data(const uint8_t *data, uint16_t *length)
{
   // Only update the Bluetooth characteristic if there is a valid connection
   if ((_carrier_ble_conn_handle != BLE_CONN_HANDLE_INVALID) && (*length <= APP_BLE_MAX_CHAR_LEN))
   {
      ble_gatts_hvx_params_t notify_params = { 0 };
      notify_params.handle = carrier_ble_char_location_handle.value_handle;
      notify_params.type   = BLE_GATT_HVX_NOTIFICATION;
      notify_params.offset = 0;
      notify_params.p_len  = length;
      notify_params.p_data = data;
      sd_ble_gatts_hvx(_carrier_ble_conn_handle, &notify_params);
   }
}
void ble_reset_discovered_devices(void)
{
   nrfx_atomic_u32_store(&_network_discovered_counter, 0);
   memset(_highest_discovered_eui, 0, sizeof(_highest_discovered_eui));
}
void ble_second_has_elapsed(void) { nrfx_atomic_u32_sub_hs(&_network_discovered_counter, 1); }
uint32_t ble_is_network_available(void) { return nrfx_atomic_u32_fetch(&_network_discovered_counter); }
