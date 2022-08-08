// Header inclusions ---------------------------------------------------------------------------------------------------

#include "ble_advertising.h"
#include "ble_conn_params.h"
#include "ble_db_discovery.h"
#include "bluetooth.h"
#include "buzzer.h"
#include "nrf_ble_es.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"
#include "nrf_sdh.h"
#include "rtc.h"
#include "sd_card.h"


// Bluetooth state definitions -----------------------------------------------------------------------------------------

NRF_BLE_GATT_DEF(_gatt);
BLE_DB_DISCOVERY_DEF(_db_disc);
NRF_BLE_QWR_DEF(_qwr);
BLE_ADVERTISING_DEF(_advertising);
NRF_BLE_GQ_DEF(_ble_gatt_queue, NRF_SDH_BLE_CENTRAL_LINK_COUNT, NRF_BLE_GQ_QUEUE_SIZE);


// Bluetooth configuration variables -----------------------------------------------------------------------------------

static const ble_uuid128_t BLE_SERV_LONG_UUID = { .uuid128 = { 0x2e, 0x5d, 0x5e, 0x39, 0x31, 0x52, 0x45, 0x0c, 0x90, 0xee, 0x3f, 0xa2, 0x52, 0x31, 0x8c, 0xd6 } };  // Service UUID
static ble_uuid_t _adv_uuids[] = { { PHYSWEB_SERVICE_ID, BLE_UUID_TYPE_BLE } };  // Advertisement UUIDs: Eddystone
static ble_uuid_t _sr_uuids[] = { { BLE_UUID_DEVICE_INFORMATION_SERVICE, BLE_UUID_TYPE_BLE }, { BLE_SERV_SHORT_UUID, BLE_UUID_TYPE_VENDOR_BEGIN } };  // Scan Response UUIDs
static ble_gatts_char_handles_t _ble_char_location_handle = { .value_handle = BLE_CHAR_LOCATION };
static ble_gatts_char_handles_t _ble_char_find_my_tottag_handle = { .value_handle = BLE_CHAR_FIND_MY_TOTTAG };
static ble_gatts_char_handles_t _ble_char_sd_management_command_handle = { .value_handle = BLE_CHAR_SD_MANAGEMENT_COMMAND };
static ble_gatts_char_handles_t _ble_char_sd_management_data_handle = { .value_handle = BLE_CHAR_SD_MANAGEMENT_DATA };
static ble_gatts_char_handles_t _ble_char_timestamp_handle = { .value_handle = BLE_CHAR_TIMESTAMP };
static uint16_t _ble_service_handle = 0, _ble_conn_handle = BLE_CONN_HANDLE_INVALID;
static uint8_t _ble_address[BLE_GAP_ADDR_LEN] = { 0 }, _scan_buffer_data[BLE_GAP_SCAN_BUFFER_MIN] = { 0 };
static ble_gap_scan_params_t _scan_params = { .active = 0, .channel_mask = { 0, 0, 0, 0, 0 }, .extended = 0, .interval = APP_SCAN_INTERVAL, .window = APP_SCAN_WINDOW, .timeout = BLE_GAP_SCAN_TIMEOUT_UNLIMITED, .scan_phys = BLE_GAP_PHY_1MBPS, .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL };
static ble_gap_scan_params_t _scan_connect_params = { .active = 0, .channel_mask = { 0, 0, 0, 0, 0 }, .extended = 0, .interval = APP_SCAN_INTERVAL, .window = APP_SCAN_WINDOW, .timeout = APP_SCAN_CONNECT_TIMEOUT, .scan_phys = BLE_GAP_PHY_1MBPS, .filter_policy = BLE_GAP_SCAN_FP_ACCEPT_ALL };
static ble_gap_conn_params_t const _connection_params = { MIN_CONN_INTERVAL, MAX_CONN_INTERVAL, SLAVE_LATENCY, CONN_SUP_TIMEOUT };
static ble_data_t _scan_buffer = { _scan_buffer_data, BLE_GAP_SCAN_BUFFER_MIN };
static ble_gap_addr_t _wl_addr_base = { 0 }, _networked_device_addr = { 0 };
static ble_advertising_init_t _adv_init = { 0 };
static ble_advdata_manuf_data_t _manuf_data_adv = { 0 };
static ble_advdata_service_data_t _service_data = { 0 };
static uint8_t _app_ble_advdata[APP_BLE_ADVDATA_LENGTH] = { 0 }, _scratch_eui[BLE_GAP_ADDR_LEN] = { 0 };
static uint8_t _scheduler_eui[BLE_GAP_ADDR_LEN] = { 0 }, _highest_discovered_eui[BLE_GAP_ADDR_LEN] = { 0 };
static volatile uint8_t _outgoing_ble_connection_active = 0;
static volatile uint32_t _retrieved_timestamp = 0, _find_my_tottag_counter = 0;
static const uint8_t _empty_eui[BLE_GAP_ADDR_LEN] = { 0 };
static nrfx_atomic_flag_t *_ble_advertising_flag = NULL, *_ble_scanning_flag = NULL, *_sd_card_maintenance_mode_flag = NULL;
static nrfx_atomic_u32_t _network_discovered_counter = 0, _time_since_last_network_discovery = 0;
static ble_gatts_rw_authorize_reply_params_t _ble_timestamp_reply = { BLE_GATTS_AUTHORIZE_TYPE_READ, {} };
static uint8_t _sd_command_data[APP_BLE_BUFFER_LENGTH] = { 0 };
static volatile uint8_t _sd_management_command = 0;
static device_role_t _device_role = HYBRID;


// Helper functions and prototypes -------------------------------------------------------------------------------------

static void nrf_qwr_error_handler(uint32_t nrf_error) { APP_ERROR_HANDLER(nrf_error); }
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context);
static void on_adv_evt(ble_adv_evt_t ble_adv_evt);
static void on_ble_service_discovered(ble_db_discovery_evt_t * p_evt);
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
   // Initialize the SoftDevice and the BLE stack
   uint32_t ram_start = 0;
   APP_ERROR_CHECK(nrf_sdh_enable_request());
   APP_ERROR_CHECK(nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start));
   APP_ERROR_CHECK(nrf_sdh_ble_enable(&ram_start));
   NRF_SDH_BLE_OBSERVER(_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

static void gap_params_init(void)
{
   // Set up all necessary GAP (Generic Access Profile) parameters of the device, permissions, and appearance
   ble_gap_conn_sec_mode_t sec_mode;
   BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);
   APP_ERROR_CHECK(sd_ble_gap_device_name_set(&sec_mode, (uint8_t const * const)APP_DEVICE_NAME, strlen(APP_DEVICE_NAME)));

   // Set BLE address
   ble_gap_addr_t gap_addr = { .addr_type = BLE_GAP_ADDR_TYPE_PUBLIC };
   memcpy(_ble_address, (uint8_t*)DEVICE_ID_MEMORY, BLE_GAP_ADDR_LEN);
   APP_ERROR_CHECK((_ble_address[5] != 0xc0) || (_ble_address[4] != 0x98));
   printf("INFO: Bluetooth address: %02x:%02x:%02x:%02x:%02x:%02x\n", _ble_address[5], _ble_address[4], _ble_address[3], _ble_address[2], _ble_address[1], _ble_address[0]);
   memcpy(gap_addr.addr, _ble_address, BLE_GAP_ADDR_LEN);
   memcpy(_scratch_eui, _ble_address, sizeof(_scratch_eui));
   APP_ERROR_CHECK(sd_ble_gap_addr_set(&gap_addr));

   // Setup connection parameters
   APP_ERROR_CHECK(sd_ble_gap_ppcp_set(&_connection_params));
}

static void gatt_init(void)
{
   // Initialize GATT structures
   APP_ERROR_CHECK(nrf_ble_gatt_init(&_gatt, NULL));
}

static void conn_params_init(void)
{
   // Initializing the Connection Parameters module
   ble_conn_params_init_t cp_init;
   memset(&cp_init, 0, sizeof(cp_init));
   cp_init.p_conn_params = NULL;
   cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
   cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
   cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
   cp_init.start_on_notify_cccd_handle = BLE_GATT_HANDLE_INVALID;
   cp_init.disconnect_on_fail = true;
   APP_ERROR_CHECK(ble_conn_params_init(&cp_init));
}

static void db_discovery_init(void)
{
   _ble_timestamp_reply.params.read.offset = 0;
   _ble_timestamp_reply.params.read.update = 1;
   _ble_timestamp_reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
   ble_db_discovery_init_t db_init = { .evt_handler = on_ble_service_discovered, .p_gatt_queue = &_ble_gatt_queue };
   APP_ERROR_CHECK(ble_db_discovery_init(&db_init));
}

static void ble_characteristic_add(uint8_t read, uint8_t write, uint8_t notify, uint8_t read_event, uint8_t vlen, uint8_t data_len, volatile uint8_t *data, uint16_t uuid, ble_gatts_char_handles_t *char_handle)
{
   // Add characteristics
   ble_uuid128_t base_uuid = BLE_SERV_LONG_UUID;
   ble_uuid_t char_uuid = { .uuid = uuid, .type = BLE_UUID_TYPE_VENDOR_BEGIN };
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
   attr_md.rd_auth = read_event;
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
   APP_ERROR_CHECK(sd_ble_gatts_characteristic_add(_ble_service_handle, &char_md, &attr_char_value, char_handle));
}

static void services_init(void)
{
   // Initialize the Queued Write Module
   nrf_ble_qwr_init_t qwr_init = { 0 };
   qwr_init.error_handler = nrf_qwr_error_handler;
   APP_ERROR_CHECK(nrf_ble_qwr_init(&_qwr, &qwr_init));

   // Initialize our own BLE service
   ble_uuid128_t base_uuid = BLE_SERV_LONG_UUID;
   ble_uuid_t service_uuid = { .uuid = BLE_SERV_SHORT_UUID, .type = BLE_UUID_TYPE_VENDOR_BEGIN };
   APP_ERROR_CHECK(sd_ble_uuid_vs_add(&base_uuid, &service_uuid.type));
   APP_ERROR_CHECK(sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &service_uuid, &_ble_service_handle));

   // Add characteristics
   ble_characteristic_add(0, 0, 1, 0, 0, 0, NULL, BLE_CHAR_LOCATION, &_ble_char_location_handle);
   ble_characteristic_add(0, 1, 0, 0, 0, 4, (volatile uint8_t*)&_find_my_tottag_counter, BLE_CHAR_FIND_MY_TOTTAG, &_ble_char_find_my_tottag_handle);
   ble_characteristic_add(0, 1, 0, 0, 0, 1, &_sd_management_command, BLE_CHAR_SD_MANAGEMENT_COMMAND, &_ble_char_sd_management_command_handle);
   ble_characteristic_add(1, 0, 0, 0, 0, 0, NULL, BLE_CHAR_SD_MANAGEMENT_DATA, &_ble_char_sd_management_data_handle);
   ble_characteristic_add(1, 0, 0, 1, 0, 4, NULL, BLE_CHAR_TIMESTAMP, &_ble_char_timestamp_handle);

   // Register service discovery events
   APP_ERROR_CHECK(ble_db_discovery_evt_register(&service_uuid));
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
   _adv_init.config.ble_adv_fast_interval = (uint32_t)APP_ADV_INTERVAL;

   // Define Event handler
   _adv_init.evt_handler = on_adv_evt;
   APP_ERROR_CHECK(ble_advertising_init(&_advertising, &_adv_init));
   ble_advertising_conn_cfg_tag_set(&_advertising, APP_BLE_CONN_CFG_TAG);

   // Create whitelist for all our devices
   ble_gap_addr_t whitelisted_ble_address = { .addr_type = BLE_GAP_ADDR_TYPE_PUBLIC, .addr = { APP_BLE_ADDR_MIN, APP_BLE_ADDR_MIN, 0x42, 0xe5, 0x98, 0xc0 } };
   memcpy(&_wl_addr_base, &whitelisted_ble_address, sizeof(ble_gap_addr_t));
}


// Bluetooth callback and event functionality --------------------------------------------------------------------------

void assert_nrf_callback(uint16_t line_num, const uint8_t *p_file_name) { app_error_handler(DEAD_BEEF, line_num, p_file_name); }

static uint8_t on_scheduler_eui(const uint8_t* scheduler_eui, bool force_update)
{
   // Only reconfigure if scheduler is non-zero and has changed
   if ((memcmp(scheduler_eui, _scheduler_eui, sizeof(_scheduler_eui)) == 0) || (!force_update && (memcmp(scheduler_eui, _empty_eui, sizeof(_empty_eui)) == 0)))
      return 0;
   uint8_t switched_euis = memcmp(_scheduler_eui, _empty_eui, sizeof(_empty_eui));
   log_printf("INFO: Switched Scheduler EUI from %02x:%02x:%02x:%02x:%02x:%02x to %02x:%02x:%02x:%02x:%02x:%02x\n",
         _scheduler_eui[5], _scheduler_eui[4], _scheduler_eui[3], _scheduler_eui[2], _scheduler_eui[1], _scheduler_eui[0],
         scheduler_eui[5], scheduler_eui[4], scheduler_eui[3], scheduler_eui[2], scheduler_eui[1], scheduler_eui[0]);
   memcpy(_scheduler_eui, scheduler_eui, sizeof(_scheduler_eui));

   // Stop BLE advertisements, update data, and restart
   ble_stop_advertising();
   memcpy(_app_ble_advdata, _scheduler_eui, APP_BLE_ADVDATA_LENGTH);
   ble_advertising_init(&_advertising, &_adv_init);
   ble_advertising_conn_cfg_tag_set(&_advertising, APP_BLE_CONN_CFG_TAG);
   ble_start_advertising();
   return switched_euis;
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
         memcpy(&_networked_device_addr, &p_adv_report->peer_addr, sizeof(_networked_device_addr));
         memcpy(_highest_discovered_eui, p_adv_report->peer_addr.addr, sizeof(_highest_discovered_eui));
         log_printf("INFO: Discovered new device: %02x:%02x:%02x:%02x:%02x:%02x\n", p_adv_report->peer_addr.addr[5], p_adv_report->peer_addr.addr[4], p_adv_report->peer_addr.addr[3], p_adv_report->peer_addr.addr[2], p_adv_report->peer_addr.addr[1], p_adv_report->peer_addr.addr[0]);
      }

      // Update the scheduler EUI
      if ((err_code == NRFX_SUCCESS) && (advdata.len == (1 + 2 + sizeof(_app_ble_advdata))))
         ble_set_scheduler_eui(advdata.p_data + 2, APP_BLE_ADVDATA_LENGTH);

      // Reset the network discovery timeout counters
      nrfx_atomic_u32_store(&_network_discovered_counter, BLE_NETWORK_DISCOVERY_COUNTDOWN_VALUE);
      if (nrfx_atomic_u32_fetch_store(&_time_since_last_network_discovery, 0) >= BLE_MISSING_NETWORK_TRANSITION1_TIMEOUT)
      {
         // Reset the BLE scanning interval and window to their active default values
         log_printf("INFO: BLE scanning window and interval have been reset to their default values\n");
         _scan_params.interval = APP_SCAN_INTERVAL;
         _scan_params.window = APP_SCAN_WINDOW;
         sd_ble_gap_scan_stop();
      }
   }
}

static void on_ble_service_discovered(ble_db_discovery_evt_t * p_evt)
{
   // Handle a service discovery event
   if (p_evt->evt_type == BLE_DB_DISCOVERY_COMPLETE)
   {
      // Search for and read from the requested characteristic
      for (uint8_t i = 0; i < p_evt->params.discovered_db.char_count; ++i)
         if (p_evt->params.discovered_db.charateristics[i].characteristic.uuid.uuid == BLE_CHAR_TIMESTAMP)
            sd_ble_gattc_read(p_evt->conn_handle, p_evt->params.discovered_db.charateristics[i].characteristic.handle_value, 0);
   }
}

static void on_ble_write(const ble_evt_t *p_ble_evt)
{
   // Handle a BLE write event
   const ble_gatts_evt_write_t *p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;
   if (p_evt_write->handle == _ble_char_find_my_tottag_handle.value_handle)
   {
      log_printf("INFO: Received BLE event: FIND_MY_TOTTAG\n");
      log_printf("      Value: %hu, Length: %hu\n", (uint16_t)p_evt_write->data[0], p_evt_write->len);
      // TODO: DO WE NEED TO STORE THE VALUE, OR IS IT ALREADY IN _find_my_tottag_counter
   }
   else if (p_evt_write->handle == _ble_char_sd_management_command_handle.value_handle)
   {
      log_printf("INFO: Received BLE event: SD_CARD_MANAGEMENT, Command = %hu, Length = %hu\n", (uint16_t)p_evt_write->data[0], p_evt_write->len);
      // TODO: DO WE NEED TO STORE THE VALUE, OR IS IT ALREADY IN _sd_management_command
      // TODO: May need to set _sd_card_maintenance_mode_flag here if command is != 0, or clear if == 0
      // TODO: memcpy(_sd_command_data, p_evt_write->data + 1, min(p_evt_write->len - 1, APP_BLE_CHAR_MAX_LEN))
  }
   else
      log_printf("ERROR: Received unknown BLE event handle: %hu\n", p_evt_write->handle);
}

static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
   // Set or clear the advertising active flag
   if (ble_adv_evt == BLE_ADV_EVT_IDLE)
      nrfx_atomic_flag_clear(_ble_advertising_flag);
   else
      nrfx_atomic_flag_set(_ble_advertising_flag);
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
         sd_ble_gatts_sys_attr_set(p_ble_evt->evt.common_evt.conn_handle, NULL, 0, 0);
         break;
      }
      case BLE_GAP_EVT_CONNECTED:
      {
         // Assign BLE connection handle
         _ble_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
         APP_ERROR_CHECK(nrf_ble_qwr_conn_handle_assign(&_qwr, _ble_conn_handle));

         // Discover services if this was an outgoing connection
         if (_outgoing_ble_connection_active)
         {
            memset(&_db_disc, 0, sizeof(_db_disc));
            APP_ERROR_CHECK(ble_db_discovery_start(&_db_disc, p_ble_evt->evt.gap_evt.conn_handle));
         }
         else
         {
            // Adjust the PHY to 2MBPS for increased data throughput on incoming connections
            ble_gap_phys_t const phys = { .rx_phys = BLE_GAP_PHY_2MBPS, .tx_phys = BLE_GAP_PHY_2MBPS, };
            APP_ERROR_CHECK(sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys));
         }
         break;
      }
      case BLE_GAP_EVT_DISCONNECTED:
      {
         _outgoing_ble_connection_active = 0;
         _ble_conn_handle = BLE_CONN_HANDLE_INVALID;
         nrfx_atomic_flag_clear(_sd_card_maintenance_mode_flag);
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
      case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:
      {
         uint32_t timestamp = rtc_get_current_time();
         _ble_timestamp_reply.params.read.len = sizeof(timestamp);
         _ble_timestamp_reply.params.read.p_data = (uint8_t*)&timestamp;
         APP_ERROR_CHECK(sd_ble_gatts_rw_authorize_reply(p_ble_evt->evt.gap_evt.conn_handle, &_ble_timestamp_reply));
         break;
      }
      case BLE_GATTC_EVT_READ_RSP:
      {
         if (p_ble_evt->evt.gattc_evt.params.read_rsp.len == sizeof(_retrieved_timestamp))
            _retrieved_timestamp = *(const uint32_t*)p_ble_evt->evt.gattc_evt.params.read_rsp.data;
         break;
      }
      case BLE_GAP_EVT_TIMEOUT:
      {
         sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
         _outgoing_ble_connection_active = 0;
         _ble_conn_handle = BLE_CONN_HANDLE_INVALID;
         nrfx_atomic_flag_clear(_sd_card_maintenance_mode_flag);
         break;
      }
      case BLE_GATTC_EVT_TIMEOUT:
      {
         APP_ERROR_CHECK(sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
         _outgoing_ble_connection_active = 0;
         _ble_conn_handle = BLE_CONN_HANDLE_INVALID;
         nrfx_atomic_flag_clear(_sd_card_maintenance_mode_flag);
         break;
      }
      case BLE_GATTS_EVT_TIMEOUT:
      {
         APP_ERROR_CHECK(sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION));
         _outgoing_ble_connection_active = 0;
         _ble_conn_handle = BLE_CONN_HANDLE_INVALID;
         nrfx_atomic_flag_clear(_sd_card_maintenance_mode_flag);
         break;
      }
      default:
         break;
   }

   // Scanning stops upon reception of a BLE response packet
   nrfx_atomic_flag_clear(_ble_scanning_flag);
}


// BLE data transfer functionality -------------------------------------------------------------------------------------

void ble_transfer_data(const uint8_t* data, uint32_t data_length)
{
   // TODO: Implement this
}

void ble_send_transfer_complete(bool command_successful)
{
   // TODO: Implement this
}


// Public Bluetooth API ------------------------------------------------------------------------------------------------

void ble_init(nrfx_atomic_flag_t* ble_is_advertising_flag, nrfx_atomic_flag_t* ble_is_scanning_flag, nrfx_atomic_flag_t* sd_card_maintenance_mode_flag)
{
   // Initialize the entire BLE stack
   _sd_card_maintenance_mode_flag = sd_card_maintenance_mode_flag;
   _ble_advertising_flag = ble_is_advertising_flag;
   _ble_scanning_flag = ble_is_scanning_flag;
   ble_stack_init();
   gap_params_init();
   gatt_init();
   conn_params_init();
   db_discovery_init();
   services_init();
   advertising_init();

   // Tell the SoftDevice to use the DC/DC regulator and low-power mode
   APP_ERROR_CHECK(sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE));
   APP_ERROR_CHECK(sd_power_mode_set(NRF_POWER_MODE_LOWPWR));
}

uint8_t ble_get_device_role(void) { return _device_role; }
const uint8_t* ble_get_eui(void) { return _ble_address; }
const uint8_t* ble_get_empty_eui(void) { return _empty_eui; }
const uint8_t* ble_get_scheduler_eui(void) { return _scheduler_eui; }
const uint8_t* ble_get_highest_network_eui(void) { return _highest_discovered_eui; }

void ble_start_advertising(void) { ble_advertising_start(&_advertising, BLE_ADV_MODE_FAST); }

void ble_stop_advertising(void)
{
   ble_advertising_start(&_advertising, BLE_ADV_MODE_IDLE);
   sd_ble_gap_adv_stop(_advertising.adv_handle);
}

void ble_start_scanning(void)
{
   // Start scanning for devices if not currently connected to a device
   if (!_outgoing_ble_connection_active)
   {
      nrfx_err_t err_code = sd_ble_gap_scan_start(NULL, &_scan_buffer);
      if (err_code == NRF_ERROR_INVALID_STATE)
         err_code = sd_ble_gap_scan_start(&_scan_params, &_scan_buffer);
      if (err_code == NRF_SUCCESS)
         nrfx_atomic_flag_set(_ble_scanning_flag);
      else
      {
         log_printf("ERROR: Unable to start scanning for BLE advertisements\n");
         nrfx_atomic_flag_clear(_ble_scanning_flag);
      }
   }
}

void ble_stop_scanning(void)
{
   sd_ble_gap_scan_stop();
   nrfx_atomic_flag_clear(_ble_scanning_flag);
   nrfx_atomic_u32_store(&_network_discovered_counter, 0);
   memset(_highest_discovered_eui, 0, sizeof(_highest_discovered_eui));
}

void ble_clear_scheduler_eui(void) { on_scheduler_eui(_empty_eui, true); }
uint8_t ble_set_scheduler_eui(const uint8_t* eui, uint8_t num_eui_bytes)
{
   if (memcmp(eui, _empty_eui, num_eui_bytes) != 0)
   {
      memcpy(_scratch_eui, eui, num_eui_bytes);
      return on_scheduler_eui(_scratch_eui, false);
   }
   return 0;
}

void ble_update_ranging_data(const uint8_t *data, uint16_t length)
{
   // Only update the Bluetooth characteristic if there is a valid connection
   if ((_ble_conn_handle != BLE_CONN_HANDLE_INVALID) && (length <= APP_BLE_BUFFER_LENGTH))
   {
      ble_gatts_hvx_params_t notify_params = {
         .handle = _ble_char_location_handle.value_handle,
         .type   = BLE_GATT_HVX_NOTIFICATION,
         .offset = 0,
         .p_len  = &length,
         .p_data = data
      };
      sd_ble_gatts_hvx(_ble_conn_handle, &notify_params);
   }
}

void ble_second_has_elapsed(void)
{
   // Reset highest discovered network after discovery counter has reached 0
   if (!nrfx_atomic_u32_sub_hs(&_network_discovered_counter, 1))
      memset(_highest_discovered_eui, 0, sizeof(_highest_discovered_eui));

   // Increase the number of seconds since a network was last discovered
   switch (nrfx_atomic_u32_add(&_time_since_last_network_discovery, 1))
   {
      // Update the scanning interval and window if a transition point has been reached
      case BLE_MISSING_NETWORK_TRANSITION1_TIMEOUT:
         log_printf("INFO: BLE scanning window and interval have been set to their %u-second disconnected values\n", BLE_MISSING_NETWORK_TRANSITION1_TIMEOUT);
         _scan_params.interval = BLE_NETWORK_TRANSITION1_SCAN_INTERVAL;
         ble_stop_scanning();
         break;
      case BLE_MISSING_NETWORK_TRANSITION2_TIMEOUT:
         log_printf("INFO: BLE scanning window and interval have been set to their %u-second disconnected values\n", BLE_MISSING_NETWORK_TRANSITION2_TIMEOUT);
         _scan_params.interval = BLE_NETWORK_TRANSITION2_SCAN_INTERVAL;
         ble_stop_scanning();
         break;
      default:
         break;
   }

   // Cause TotTag to beep if FindMyTottag has been activated
   if (nrfx_atomic_u32_sub_hs(&_find_my_tottag_counter, 1))
      buzzer_indicate_location((nrfx_atomic_u32_fetch(&_find_my_tottag_counter) % 2) == 0);
}

uint32_t ble_request_timestamp(void)
{
   // Connect to a discovered device and request the current timestamp
   if (!_outgoing_ble_connection_active)
   {
      log_printf("INFO: Requesting current timestamp from network...\n");
      _retrieved_timestamp = 0;
      _outgoing_ble_connection_active = 1;
      if (sd_ble_gap_connect(&_networked_device_addr, &_scan_connect_params, &_connection_params, APP_BLE_CONN_CFG_TAG) != NRF_SUCCESS)
         _outgoing_ble_connection_active = 0;
      nrfx_atomic_flag_clear(_ble_scanning_flag);
   }
   else if (_retrieved_timestamp)
      sd_ble_gap_disconnect(_ble_conn_handle, BLE_HCI_LOCAL_HOST_TERMINATED_CONNECTION);
   return _retrieved_timestamp;
}

uint32_t ble_is_network_available(void) { return nrfx_atomic_u32_fetch(&_network_discovered_counter); }

void ble_sd_card_maintenance(void)
{
   // Determine which SD card maintenance task to carry out
   switch (_sd_management_command)
   {
      case BLE_SD_CARD_LIST_FILES:
      {
         uint32_t file_size = 0;
         uint8_t continuation = 0;
         char file_name[1024] = { 0 };
         while (sd_card_list_files(file_name, &file_size, continuation++))
         {
            ble_transfer_data((const uint8_t*)"NEW", 3);
            ble_transfer_data((uint8_t*)&file_size, sizeof(file_size));
            ble_transfer_data((uint8_t*)file_name, strlen(file_name));
         }
         ble_send_transfer_complete(true);
         break;
      }
      case BLE_SD_CARD_DOWNLOAD_FILE:
      {
         uint32_t bytes_read;
         bool success = sd_card_open_file_for_reading((const char*)_sd_command_data);
         if (success)
            while ((bytes_read = sd_card_read_reading_file(_sd_command_data, sizeof(_sd_command_data))) != 0)
               ble_transfer_data(_sd_command_data, bytes_read);
         sd_card_close_reading_file();
         ble_send_transfer_complete(success);
         break;
      }
      case BLE_SD_CARD_DELETE_FILE:
         ble_send_transfer_complete(sd_card_erase_file((const char*)_sd_command_data));
         break;
      case BLE_SD_CARD_DELETE_ALL:
         ble_send_transfer_complete(sd_card_erase_all_files());
         break;
      default:
         ble_send_transfer_complete(false);
         break;
   }

   // Clean up command space to get ready for the next command
   memset(_sd_command_data, 0, sizeof(_sd_command_data));
   _sd_management_command = 0;
}
