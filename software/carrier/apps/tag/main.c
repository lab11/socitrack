/*

  UWB Localization Tag

*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <ble.h>
#include <ble_gatts.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_sdh.h"
#include "nrf_sdh_ble.h"
#include "nrf_sdh_soc.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_pwr_mgmt.h"
#include "app_error.h"
#include "app_util.h"
#include "app_timer.h"
#include "app_scheduler.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "ble_hci.h"
#include "ble_srv_common.h"
#include "ble_advdata.h"
#include "ble_conn_params.h"
#include "ble_conn_state.h"
#include "ble_advertising.h"
#include "nrf_ble_es.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_qwr.h"

#include "led.h"
#include "boards.h"

#include "ble_config.h"
#include "module_interface.h"

/*******************************************************************************
 *   Configuration and settings
 ******************************************************************************/

// BLE characteristics
#define CARRIER_SHORT_UUID           0x3152
#define CARRIER_BLE_CHAR_LOCATION    0x3153
#define CARRIER_BLE_CHAR_RANGING     0x3154
#define CARRIER_BLE_CHAR_STATUS      0x3155
#define CARRIER_BLE_CHAR_CALIBRATION 0x3156

// Service UUID
const ble_uuid128_t APP_SERVICE_UUID = {
        .uuid128 = {0x2e, 0x5d, 0x31, 0x52, 0x3e, 0xfa, 0x45, 0x0c, 0x90, 0xee, 0x3f, 0xa2, 0x31, 0x52, 0x8c, 0xd6}
};

ble_gatts_char_handles_t carrier_ble_char_location_handle    = {.value_handle = CARRIER_BLE_CHAR_LOCATION};
ble_gatts_char_handles_t carrier_ble_char_ranging_handle     = {.value_handle = CARRIER_BLE_CHAR_RANGING};
ble_gatts_char_handles_t carrier_ble_char_status_handle      = {.value_handle = CARRIER_BLE_CHAR_STATUS};
ble_gatts_char_handles_t carrier_ble_char_calibration_handle = {.value_handle = CARRIER_BLE_CHAR_CALIBRATION};
uint16_t carrier_ble_service_handle          = 0;

#define DEAD_BEEF                       0xDEADBEEF            //!< Value used as error code on stack dump, can be used to identify stack location on stack unwind.
#define NON_CONNECTABLE_ADV_LED_PIN     CARRIER_LED_RED       //!< Toggles when non-connectable advertisement is sent.
#define CONNECTED_LED_PIN               CARRIER_LED_GREEN     //!< Is on when device has connected.
#define CONNECTABLE_ADV_LED_PIN         CARRIER_LED_BLUE      //!< Is on when device is advertising connectable advertisements.

/*******************************************************************************
 *   State for this application
 ******************************************************************************/

// Main application state
static ble_app_t app;

// GP Timer. Used to retry initializing the module.
APP_TIMER_DEF(watchdog_timer);

// Whether or not we successfully got through to the module and got it configured properly.
bool module_inited = false;

// Handle of the current connection
static uint16_t m_conn_handle = BLE_CONN_HANDLE_INVALID;

// Universally unique service identifiers (UUID)
static ble_uuid_t m_adv_uuids[] = {
        {PHYSWEB_SERVICE_ID,                  BLE_UUID_TYPE_BLE}
};
// Advertise Eddystone beacon; other services are included in the Scan Response
static ble_uuid_t m_sr_uuids[] = {
        {BLE_UUID_DEVICE_INFORMATION_SERVICE, BLE_UUID_TYPE_BLE},
        {APP_SERVICE_ID,                      BLE_UUID_TYPE_VENDOR_BEGIN}
};

// Copy address from flash
uint8_t _ble_address[6];
uint16_t ble_device_id;

NRF_BLE_GATT_DEF(m_gatt);                                                       /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);                                                         /**< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising);                                             /**< Advertising module instance. */


/*******************************************************************************
 *   nRF CALLBACKS - In response to various BLE/hardware events.
 ******************************************************************************/

//Callback function for asserts in the SoftDevice.
void assert_nrf_callback(uint16_t line_num, const uint8_t * p_file_name)
{
    // Use better_error_handling.c
    /*printf("ERROR: assert in SoftDevice failed!\n");
    
    while(1) { led_toggle(CARRIER_LED_RED); }*/
    
    app_error_handler(DEAD_BEEF, line_num, p_file_name);
}

// Function for handling the WRITE CHARACTERISTIC BLE event.
void on_ble_write(const ble_evt_t* p_ble_evt)
{
    const ble_gatts_evt_write_t *p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

    if (p_evt_write->handle == carrier_ble_char_ranging_handle.value_handle) {

        // Handle a write to the characteristic that starts and stops ranging.
        app.app_ranging_enabled = p_evt_write->data[0];

        // Stop or start the module based on the value we just got
        if (app.app_ranging_enabled) {
            module_resume();
        } else {
            module_sleep();
        }

    } else if (p_evt_write->handle == carrier_ble_char_calibration_handle.value_handle) {

        // Handle a write to the characteristic that starts calibration
        app.calibration_index = p_evt_write->data[0];

        // Configure this node for calibration and set the calibration node
        // index. If 0, this node will immediately start calibration.
        module_start_calibration(app.calibration_index);
    }
}


// Function for handling BLE events
static void ble_evt_handler(ble_evt_t const * p_ble_evt, void * p_context)
{
    ret_code_t err_code;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GATTS_EVT_WRITE: {
            // Decide which characteristics is used
            on_ble_write(p_ble_evt);
        }
        case BLE_GAP_EVT_SEC_PARAMS_REQUEST: {
            // Pairing not supported
            err_code = sd_ble_gap_sec_params_reply(p_ble_evt->evt.common_evt.conn_handle,
                                                   BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP,
                                                   NULL,
                                                   NULL);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GATTS_EVT_SYS_ATTR_MISSING: {
            // No system attributes have been stored.
            err_code = sd_ble_gatts_sys_attr_set(p_ble_evt->evt.common_evt.conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GAP_EVT_CONNECTED: {
            led_on(CONNECTED_LED_PIN);
            led_off(CONNECTABLE_ADV_LED_PIN);
            NRF_LOG_INFO("Connected.");

            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GAP_EVT_DISCONNECTED: {
            // LED indication will be changed when advertising starts.
            NRF_LOG_INFO("Disconnected.");
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            break;
        }
        case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
            ble_gap_phys_t const phys =
            {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GATTC_EVT_TIMEOUT: {
            // Disconnect on GATT Client timeout event.
            NRF_LOG_DEBUG("GATT Client Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;
        }
        case BLE_GATTS_EVT_TIMEOUT: {
            // Disconnect on GATT Server timeout event.
            NRF_LOG_DEBUG("GATT Server Timeout.");
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;
        }
        default:
            // No implementation needed.
            break;
    }
}

// Handle Eddystone event
static void on_es_evt(nrf_ble_es_evt_t evt)
{
    switch (evt)
    {
        case NRF_BLE_ES_EVT_ADVERTISEMENT_SENT:
            led_toggle(NON_CONNECTABLE_ADV_LED_PIN);
            break;

        case NRF_BLE_ES_EVT_CONNECTABLE_ADV_STARTED:
            led_on(CONNECTABLE_ADV_LED_PIN);
            break;

        case NRF_BLE_ES_EVT_CONNECTABLE_ADV_STOPPED:
            led_off(CONNECTABLE_ADV_LED_PIN);
            break;

        default:
            break;
    }
}

// Handles advertising events
static void on_adv_evt(ble_adv_evt_t ble_adv_evt)
{
    switch (ble_adv_evt)
    {
        case BLE_ADV_EVT_FAST:
            NRF_LOG_INFO("Fast advertising.");
            break;

        case BLE_ADV_EVT_IDLE:
            NRF_LOG_INFO("Application is idle.");
            break;

        default:
            break;
    }
}

// Function for handling Queued Write Module errors
static void nrf_qwr_error_handler(uint32_t nrf_error)
{
    APP_ERROR_HANDLER(nrf_error);
}


/*******************************************************************************
 *   Module Callbacks
 ******************************************************************************/

uint8_t buffer_updated = 0;

void updateData (uint8_t * data, uint32_t len)
{
    uint16_t copy_len = (uint16_t) MIN(len, 256);
	memcpy(app.app_raw_response_buffer, data, copy_len);
	app.app_raw_response_length = copy_len;

    /* Print ranging packet
       0:   Length of the included payload (not including this byte)
       1:   Interrupt_reason

       Packet:
       0:   Number of ranges
       1-9: Ranging response Nr. 1

       Per ranging:
       8:   EUI
       4:   Range in mm
    */
	printf("Interrupt with reason %i", data[0]);

	if (data[0] == HOST_IFACE_INTERRUPT_RANGES) {
        printf(", included number of anchors: %i\r\n", data[1]);

        const uint8_t packet_overhead = 2;
        const uint8_t ranging_length = 12;
        uint8_t nr_ranges = ((uint8_t) len - packet_overhead) / ranging_length;

        for (uint8_t i = 0; i < nr_ranges; i++) {
            uint8_t offset = packet_overhead + i * ranging_length;
            printf(" Nr %i", i + 1);
            printf(": Anchor %#04X with range ", data[offset + 0]);

            // Little-endian notation
            int32_t range = data[offset + 8] + (data[offset + 9] << 1*8) + (data[offset + 10] << 2*8) + (data[offset + 11] << 3*8);
            
            if (range > ONEWAY_TAG_RANGE_MIN) {
                printf("%li", range);
            } else if (range == (int32_t)ONEWAY_TAG_RANGE_ERROR_NO_OFFSET) {
                printf("ERROR_NO_OFFSET");
            } else if (range == (int32_t)ONEWAY_TAG_RANGE_ERROR_TOO_FEW_RANGES) {
                printf("ERROR_TOO_FEW_RANGES");
            } else if (range == (int32_t)ONEWAY_TAG_RANGE_ERROR_MISC) {
                printf("ERROR_MISC");
            } else {
                printf("INVALID");
            }

            printf("\r\n");
        }
    }

	// Trigger moduleDataUpdate from main loop
	buffer_updated = 1;
}


void moduleDataUpdate ()
{
    // Update the data value and notify on the data
	if (app.app_raw_response_length >= 5) {
        printf("Received data from module\n");
	}

	if(m_conn_handle != BLE_CONN_HANDLE_INVALID) {

	    ble_gatts_hvx_params_t notify_params;
		memset(&notify_params, 0, sizeof(notify_params));

        notify_params.handle = carrier_ble_char_location_handle.value_handle;
		notify_params.type   = BLE_GATT_HVX_NOTIFICATION;
		notify_params.offset = 0;
		notify_params.p_len  = &app.app_raw_response_length;
		notify_params.p_data = app.app_raw_response_buffer;

		ret_code_t err_code = sd_ble_gatts_hvx(m_conn_handle, &notify_params);
        APP_ERROR_CHECK(err_code);

        printf("Sent BLE packet of length %i \r\n", app.app_raw_response_length);
	}

	buffer_updated = 0;
}

static void watchdog_handler (void* p_context)
{
    uint32_t err_code;

    if (!module_inited) {
        err_code = module_init(updateData);
        if (err_code == NRF_SUCCESS) {
            module_inited = true;
            module_start_ranging(true, 10);
        }
    }
}

// If no pending operation, sleep until the next event occurs
static void power_manage(void)
{
    nrf_pwr_mgmt_run();

    // After wake-up, check whether things occured
    app_sched_execute();
}


/*******************************************************************************
 *   INIT FUNCTIONS
 ******************************************************************************/

// Initializing the BLE stack, including the SoftDevice and the BLE event interrupt
static void ble_stack_init(void)
{
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_app_ram_start_get(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Overwrite some of the default configurations for the BLE stack.
    ble_cfg_t ble_cfg;

    // Configure the maximum number of connections.
    memset(&ble_cfg, 0, sizeof(ble_cfg));
    ble_cfg.gap_cfg.role_count_cfg.periph_role_count  = 1;
    ble_cfg.gap_cfg.role_count_cfg.central_role_count = 0;
    ble_cfg.gap_cfg.role_count_cfg.central_sec_count  = 0;
    err_code = sd_ble_cfg_set(BLE_GAP_CFG_ROLE_COUNT, &ble_cfg, ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}

// GAP initialization: set up all the necessary GAP (Generic Access Profile) parameters of the device. It also sets the permissions and appearance
static void gap_params_init(void)
{
    ret_code_t              err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;
    uint8_t                 device_name[] = APP_DEVICE_NAME;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode, device_name, strlen((const char *)device_name));
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);

    APP_ERROR_CHECK(err_code);
}


// Initializing the GATT module.
static void gatt_init(void)
{
    ret_code_t err_code = nrf_ble_gatt_init(&m_gatt, NULL);
    APP_ERROR_CHECK(err_code);
}

static void advertising_init(void)
{
    ret_code_t             err_code;
    ble_advertising_init_t init;

    memset(&init, 0, sizeof(init));

    // Custom advertisement data
    /*ble_advdata_manuf_data_t             manuf_data_adv;
    #define MANUF_ADV_LENGTH             8
    uint8_t data_adv[MANUF_ADV_LENGTH]   = {0};
    data_adv[0]                          = APP_SERVICE_ID; // TotTag's service identifier, expected to be first byte of advertisement
    manuf_data_adv.company_identifier    = APP_COMPANY_IDENTIFIER; // UMich's Company ID
    manuf_data_adv.data.p_data           = data_adv;
    manuf_data_adv.data.size             = sizeof(data_adv);
    init.advdata.p_manuf_specific_data   = &manuf_data_adv;*/

    // General Advertisement settings

    // Add device name in advertisements
    // init.advdata.name_type               = BLE_ADVDATA_FULL_NAME;
    // For shorter names, adjust as follows. The full name will still be displayed in connections
    init.advdata.name_type               = BLE_ADVDATA_SHORT_NAME;
    init.advdata.short_name_len          = 4; // Advertise the first X letters of the name

    // Physical Web data
    const char* url_str = PHYSWEB_URL;
    const uint8_t header_len = 3;
    uint8_t url_frame_length = header_len + strlen((char*)url_str); // Change to 4 if URLEND is applied
    uint8_t m_url_frame[url_frame_length];
    m_url_frame[0] = PHYSWEB_URL_TYPE;
    m_url_frame[1] = PHYSWEB_TX_POWER;
    m_url_frame[2] = PHYSWEB_URLSCHEME_HTTPS;
    for (uint8_t i=0; i<strlen((char*)url_str); i++) {
        m_url_frame[i+3] = url_str[i];
    }

    // Advertise Physical Web service
    ble_advdata_service_data_t service_data;
    service_data.service_uuid            = PHYSWEB_SERVICE_ID;
    service_data.data.p_data             = m_url_frame;
    service_data.data.size               = url_frame_length;
    init.advdata.p_service_data_array    = &service_data;
    init.advdata.service_data_count      = 1;

    //printf("Advertising Eddystone address %s with total length %i\n", url_str, url_frame_length);

    init.advdata.include_appearance      = false;
    init.advdata.flags                   = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
    init.advdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.advdata.uuids_complete.p_uuids  = m_adv_uuids;

    // Scan response
    /*ble_advdata_manuf_data_t             manuf_data_resp;
    #define MANUF_RESP_LENGTH            20
    uint8_t data_resp[MANUF_RESP_LENGTH] = {0};
    manuf_data_resp.company_identifier   = APP_COMPANY_IDENTIFIER;
    manuf_data_resp.data.p_data          = data_resp;
    manuf_data_resp.data.size            = sizeof(data_resp);
    init.srdata.p_manuf_specific_data    = &manuf_data_resp;*/
    init.srdata.name_type                = BLE_ADVDATA_FULL_NAME;
    init.srdata.uuids_complete.uuid_cnt  = sizeof(m_sr_uuids) / sizeof(m_sr_uuids[0]);
    init.srdata.uuids_complete.p_uuids   = m_sr_uuids;

    init.config.ble_adv_fast_enabled  = true;
    init.config.ble_adv_fast_interval = (uint32_t)APP_ADV_INTERVAL;
    //init.config.ble_adv_fast_timeout  = APP_ADV_DURATION;

    // Define Event handler
    init.evt_handler = on_adv_evt;

    err_code = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

// Used for the initialization of the service; initializes a specific characteristic
void ble_characteristic_add(uint8_t read, uint8_t write, uint8_t notify, uint8_t vlen,
                               uint8_t data_len, uint8_t* data,
                               uint16_t uuid,
                               ble_gatts_char_handles_t* char_handle)
{
    ret_code_t err_code;

    // Add characteristics
    ble_uuid128_t base_uuid = APP_SERVICE_UUID;
    ble_uuid_t    char_uuid = {.uuid = uuid};

    err_code = sd_ble_uuid_vs_add(&base_uuid, &char_uuid.type);
    APP_ERROR_CHECK(err_code);

    // Add read/write properties to our characteristic
    ble_gatts_char_md_t char_md;
    memset(&char_md, 0, sizeof(char_md));
    char_md.char_props.read   = read;
    char_md.char_props.write  = write;
    char_md.char_props.notify = notify;

    // Configuring Client Characteristic Configuration Descriptor (CCCD) metadata and add to char_md structure
    ble_gatts_attr_md_t cccd_md;
    memset(&cccd_md, 0, sizeof(cccd_md));
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&cccd_md.write_perm);
    cccd_md.vloc                = BLE_GATTS_VLOC_STACK;
    char_md.p_cccd_md           = &cccd_md;

    // Configure the attribute metadata
    ble_gatts_attr_md_t attr_md;
    memset(&attr_md, 0, sizeof(attr_md));
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.read_perm);
    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&attr_md.write_perm);
    attr_md.vloc = BLE_GATTS_VLOC_STACK;
    attr_md.vlen = vlen;

    // Configure the characteristic value attribute
    ble_gatts_attr_t    attr_char_value;
    memset(&attr_char_value, 0, sizeof(attr_char_value));
    attr_char_value.p_uuid      = &char_uuid;
    attr_char_value.p_attr_md   = &attr_md;

    // Set characteristic length in number of bytes
    attr_char_value.max_len     = data_len;
    attr_char_value.init_len    = 0;
    attr_char_value.p_value     = data;

    // Add our new characteristics to the service
    err_code = sd_ble_gatts_characteristic_add(carrier_ble_service_handle, &char_md, &attr_char_value, char_handle);
    APP_ERROR_CHECK(err_code);
}

// Initializing services that will be used by the application.
static void services_init(void)
{
    ret_code_t         err_code;
    nrf_ble_qwr_init_t qwr_init = {0};

    // Initialize Queued Write Module.
    qwr_init.error_handler = nrf_qwr_error_handler;

    err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
    APP_ERROR_CHECK(err_code);

    // Initialize our own BLE service
    ble_uuid_t        service_uuid;
    ble_uuid128_t     base_uuid = APP_SERVICE_UUID;
    service_uuid.uuid = APP_SERVICE_ID;

    err_code = sd_ble_uuid_vs_add(&base_uuid, &service_uuid.type);
    APP_ERROR_CHECK(err_code);

    err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &service_uuid, &carrier_ble_service_handle);
    APP_ERROR_CHECK(err_code);

    // Add characteristics
    ble_characteristic_add(1, 0, 1, 0,
                           128, app.app_raw_response_buffer,
                           CARRIER_BLE_CHAR_LOCATION,
                           &carrier_ble_char_location_handle);

    ble_characteristic_add(1, 1, 0, 0,
                           1, (uint8_t*) &app.app_ranging_enabled,
                           CARRIER_BLE_CHAR_RANGING,
                           &carrier_ble_char_ranging_handle);

    ble_characteristic_add(1, 0, 0, 0,
                           1, (uint8_t*) &module_inited,
                           CARRIER_BLE_CHAR_STATUS,
                           &carrier_ble_char_status_handle);

    ble_characteristic_add(1, 1, 0, 0,
                           1, &app.calibration_index,
                           CARRIER_BLE_CHAR_CALIBRATION,
                           &carrier_ble_char_calibration_handle);
}

// Initializing the Connection Parameters module.
static void conn_params_init(void)
{
    ret_code_t             err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = true; // Can also add a on_conn_params_evt as a handler

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);

}


void ble_init(void)
{
    //printf("Started initializing BLE...\n");

    ble_stack_init();
    //printf("Stack initialized\n");

    gap_params_init();
    //printf("GAP parameters initialized\n");

    gatt_init();
    //printf("GATT module initialized\n");

    services_init();
    //printf("BLE services initialized\n");

    advertising_init();
    //printf("Advertising initialized\n");

    conn_params_init();
    //printf("Connection parameters initialized\n");

    // Instead of advertising directly, use Eddystone library
    //nrf_ble_es_init(on_es_evt);
    //printf("Eddystone beaconing initialized\n");
}

// Non-BLE inits -------------------------------------------------------------------------------------------------------

static void timers_init (void)
{
    uint32_t err_code = app_timer_init();
    APP_ERROR_CHECK(err_code);

    // Create and start watchdog
    err_code = app_timer_create(&watchdog_timer, APP_TIMER_MODE_REPEATED, watchdog_handler);
    APP_ERROR_CHECK(err_code);

    err_code = app_timer_start(watchdog_timer, UPDATE_RATE, NULL);
    APP_ERROR_CHECK(err_code);
}

static void scheduler_init(void)
{
    APP_SCHED_INIT(SCHED_MAX_EVENT_DATA_SIZE, SCHED_QUEUE_SIZE);
}

static void power_management_init(void)
{
    ret_code_t err_code = nrf_pwr_mgmt_init();
    APP_ERROR_CHECK(err_code);
}


/*******************************************************************************
 *   MAIN FUNCTION
 ******************************************************************************/

int main (void)
{
    ret_code_t err_code;

    // Initialization
    led_init(CARRIER_LED_RED);
    led_init(CARRIER_LED_BLUE);
    led_init(CARRIER_LED_GREEN);

    // Initialize RTT library
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();
    printf("\r\n----------------------------------------------\r\n");
    printf("Initializing nRF...\r\n");

    // Signal initialization
    led_on(CARRIER_LED_RED);

    // -----------------------------------------------------------------------------------------------------------------

    // Configuration

    // We default to doing ranging at the start
    app.app_ranging_enabled = 1;

    // Set to effective -1
    app.calibration_index = 255;

    // Get stored address
    memcpy(_ble_address, (uint8_t*) BLE_FLASH_ADDRESS, 6);
    ble_device_id = (uint16_t)( (uint16_t)_ble_address[1] << (uint8_t)8) | _ble_address[0];

    // Initialize
    timers_init();
    scheduler_init();
    power_management_init();
    ble_init();

    printf("Initialized all software modules\n");

    // Start advertisements
    err_code = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
    APP_ERROR_CHECK(err_code);

    printf("Started broadcasting BLE advertisements...\n");

    // -----------------------------------------------------------------------------------------------------------------

    // Init the nRF hardware to work with the module module.
    err_code = module_hw_init();
    APP_ERROR_CHECK(err_code);

    // Init the state machine on the module
    err_code = module_init(updateData);
    if (err_code == NRF_SUCCESS) {
        module_inited = true;
        printf("Finished initialization\r\n");
    } else {
        printf("ERROR: Failed initialization!\r\n");
    }

    // Start the ranging
    if (module_inited) {
        err_code = module_start_ranging(true, 10);
        if (err_code != NRF_SUCCESS) {
            printf("ERROR: Failed to start ranging!\r\n");
        } else {
            printf("Started ranging...\r\n");
        }
    }

    // Signal end of initialization
    led_off(CARRIER_LED_RED);
    led_on(CARRIER_LED_BLUE);

    // -----------------------------------------------------------------------------------------------------------------

    // Loop: update location and advertise
    while (1) {

        // For power measurements: Disable timers (timers_init()) and comment the lines below power_manage(); if bluetooth should be disabled, comment eddystone_adv()
        //printf("Going back go sleep...\r\n");
        power_manage();

		if (buffer_updated) {
		    //printf("Updating location...\r\n");
			moduleDataUpdate();
		}
    }
}
