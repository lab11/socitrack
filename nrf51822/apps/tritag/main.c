/*

  UWB Localization Tag

*/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "nordic_common.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include "nrf_delay.h"
#include "ble.h"
#include "ble_db_discovery.h"
#include "softdevice_handler.h"
#include "app_util.h"
#include "app_error.h"
#include "ble_advdata_parser.h"
#include "ble_conn_params.h"
#include "ble_hci.h"
#include "boards.h"
#include "nrf_gpio.h"
#include "pstorage.h"
#include "app_trace.h"
#include "ble_hrs_c.h"
#include "ble_bas_c.h"
#include "app_util.h"
#include "app_timer.h"

#include "simple_ble.h"
#include "eddystone.h"

#include "led.h"
#include "boards.h"
#include "tritag.h"

#include "ble_config.h"
#include "tripoint_interface.h"
#include "SEGGER_RTT.h"

/*******************************************************************************
 *   Configuration and settings
 ******************************************************************************/

#define TRITAG_SHORT_UUID                     0x3152
#define TRITAG_CHAR_LOCATION_SHORT_UUID       0x3153
#define TRITAG_CHAR_RANGING_ENABLE_SHORT_UUID 0x3154
#define TRITAG_CHAR_CALIBRATION_SHORT_UUID    0x3159
#define TRITAG_CHAR_STATUS_SHORT_UUID         0x3155

// Randomly generated UUID
simple_ble_service_t service_handle = {
    .uuid128 = {{0x2e, 0x5d, 0x5e, 0x39, 0x31, 0x52, 0x45, 0x0c,
                 0x90, 0xee, 0x3f, 0xa2, 0x31, 0x52, 0x8c, 0xd6}}
};

simple_ble_char_t char_range_handle             = {.uuid16 = TRITAG_CHAR_LOCATION_SHORT_UUID};
simple_ble_char_t char_calibration_index_handle = {.uuid16 = TRITAG_CHAR_CALIBRATION_SHORT_UUID};
simple_ble_char_t char_ranging_enable_handle    = {.uuid16 = TRITAG_CHAR_RANGING_ENABLE_SHORT_UUID};
simple_ble_char_t char_status_handle            = {.uuid16 = TRITAG_CHAR_STATUS_SHORT_UUID};


// Intervals for advertising and connections
static simple_ble_config_t ble_config = {
    // c0:98:e5:45:xx:xx
    .platform_id       = 0x45,              // used as 4th octect in device BLE address
    .device_id         = DEVICE_ID_DEFAULT,
    .adv_name          = DEVICE_NAME,       // used in advertisements if there is room
    .adv_interval      = MSEC_TO_UNITS(1000, UNIT_0_625_MS),
    // .min_conn_interval = MSEC_TO_UNITS(500, UNIT_1_25_MS),
    // .max_conn_interval = MSEC_TO_UNITS(1000, UNIT_1_25_MS),
    .min_conn_interval = MSEC_TO_UNITS(8, UNIT_1_25_MS),
    .max_conn_interval = MSEC_TO_UNITS(10, UNIT_1_25_MS),
};

// Copy address from flash
uint8_t _ble_address[6];


/*******************************************************************************
 *   State for this application
 ******************************************************************************/

// Main application state
simple_ble_app_t* simple_ble_app;
static ble_app_t app;

// GP Timer. Used to retry initializing the TriPoint.
static app_timer_id_t  app_timer;

// Whether or not we successfully got through to the TriPoint module
// and got it configured properly.
bool tripoint_inited = false;


/*******************************************************************************
 *   nRF CALLBACKS - In response to various BLE/hardware events.
 ******************************************************************************/

// Function for handling the WRITE CHARACTERISTIC BLE event.
void ble_evt_write (ble_evt_t* p_ble_evt)
{
    ble_gatts_evt_write_t* p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;

    if (simple_ble_is_char_event(p_ble_evt, &char_ranging_enable_handle)) {
        // Handle a write to the characteristic that starts and stops
        // TriPoint ranging.

        app.app_ranging = p_evt_write->data[0];

        // Stop or start the tripoint based on the value we just got
        if (app.app_ranging == 1) {
            tripoint_resume();
        } else {
            tripoint_sleep();
        }

    } else if (simple_ble_is_char_event(p_ble_evt, &char_calibration_index_handle)) {
        // Handle a write to the characteristic that starts calibration
        app.calibration_index = p_evt_write->data[0];

        // Configure this node for calibration and set the calibration node
        // index. If 0, this node will immediately start calibration.
        tripoint_start_calibration(app.calibration_index);
    }
}


/*******************************************************************************
 *   PolyPoint Callbacks
 ******************************************************************************/

uint8_t updated = 0;
uint16_t blobLen;
uint8_t dataBlob[256];

void updateData (uint8_t * data, uint32_t len)
{
    uint16_t copy_len = (uint16_t)MIN(len, 256);
	memcpy(app.app_raw_response_buffer, data, copy_len);
	blobLen = copy_len;

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
	debug_msg("Interrupt with reason ");
	debug_msg_int(data[0]);

	if (data[0] == HOST_IFACE_INTERRUPT_RANGES) {
        debug_msg(", included number of anchors: ");
        debug_msg_int(data[1]);
        debug_msg("\r\n");

        const uint8_t packet_overhead = 2;
        const uint8_t ranging_length = 12;
        uint8_t nr_ranges = ((uint8_t) len - packet_overhead) / ranging_length;

        for (uint8_t i = 0; i < nr_ranges; i++) {
            uint8_t offset = packet_overhead + i * ranging_length;
            debug_msg(" Nr ");
            debug_msg_int(i + 1);
            debug_msg(": Anchor ");
            debug_msg_hex(data[offset + 0] >> 4);
            debug_msg_hex(data[offset + 0] & 0x0F);
            debug_msg(" with range ");

            // Little-endian notation
            int32_t range = data[offset + 8] + (data[offset + 9] << 1*8) + (data[offset + 10] << 2*8) + (data[offset + 11] << 3*8);
            
            if (range > ONEWAY_TAG_RANGE_MIN) {
                debug_msg_int(range);
            } else if (range == ONEWAY_TAG_RANGE_ERROR_NO_OFFSET) {
                debug_msg("ERROR_NO_OFFSET");
            } else if (range == ONEWAY_TAG_RANGE_ERROR_TOO_FEW_RANGES) {
                debug_msg("ERROR_TOO_FEW_RANGES");
            } else if (range == ONEWAY_TAG_RANGE_ERROR_MISC) {
                debug_msg("ERROR_MISC");
            } else {
                debug_msg("INVALID");
            }

            debug_msg("\r\n");
        }
    }

	// Trigger tripointDataUpdate from main loop
	updated = 1;
}


void tripointDataUpdate ()
{
    // Update the data value and notify on the data
	if (blobLen >= 5) {
        led_on(LED_0);
        nrf_delay_us(1000);
		led_off(LED_0);
	}

	if(simple_ble_app->conn_handle != BLE_CONN_HANDLE_INVALID) {

		ble_gatts_hvx_params_t notify_params;
		uint16_t len = blobLen;
        notify_params.handle = char_range_handle.char_handle.value_handle;
		notify_params.type   = BLE_GATT_HVX_NOTIFICATION;
		notify_params.offset = 0;
		notify_params.p_len  = &len;
		notify_params.p_data = app.app_raw_response_buffer;

		// volatile uint32_t err_code = 0;
        // err_code = sd_ble_gatts_hvx(simple_ble_app->conn_handle, &notify_params);
		sd_ble_gatts_hvx(simple_ble_app->conn_handle, &notify_params);
        // APP_ERROR_CHECK(err_code);

        debug_msg("Sent BLE packet of length ");
        debug_msg_int(len / 10);
        debug_msg_int(len % 10);
        debug_msg("\r\n");
	}

	updated = 0;
}

static void timer_handler (void* p_context)
{
    uint32_t err_code;

    if (!tripoint_inited) {
        err_code = tripoint_init(updateData);
        if (err_code == NRF_SUCCESS) {
            tripoint_inited = true;
            tripoint_start_ranging(true, 10);
        }
    }
}

// Handle errors thrown by APP_ERROR_CHECK; overwrites default implementation in app_error.c
// Defined in ble_simple.c
/*void app_error_handler (uint32_t error_code,
                       uint32_t line_num,
                       const uint8_t * p_file_name)
{
    // Reset system
    NVIC_SystemReset();
}*/


/*******************************************************************************
 *   INIT FUNCTIONS
 ******************************************************************************/

void initialize_app_timer (void)
{
    APP_TIMER_INIT(TRITAG_TIMER_PRESCALER,
                   TRITAG_MAX_TIMERS,
                   TRITAG_OP_QUEUE_SIZE,
                   false);
}


static void timers_init (void)
{
    uint32_t err_code;

    err_code = app_timer_create(&app_timer,
                                APP_TIMER_MODE_REPEATED,
                                timer_handler);
    APP_ERROR_CHECK(err_code);

    err_code = app_timer_start(app_timer, UPDATE_RATE, NULL);

    APP_ERROR_CHECK(err_code);
}


// Init services, called by simple_ble_init
void services_init (void)
{
    // Add main TriTag service
    simple_ble_add_service(&service_handle);

    //add the characteristic that exposes a blob of interrupt response
    simple_ble_add_characteristic(1, 0, 1, 0, // read, write, notify, vlen
                                  128, app.app_raw_response_buffer,
                                  &service_handle,
                                  &char_range_handle);

    // Add the characteristic that enables/disables ranging
    simple_ble_add_characteristic(1, 1, 0, 0, // read, write, notify, vlen
                                  1, &app.calibration_index,
                                  &service_handle,
                                  &char_calibration_index_handle);

    // Add the characteristic that sets the index of a node during calibration. Writing a 0 to this characteristic will start the calibration.
    simple_ble_add_characteristic(1, 1, 0, 0, // read, write, notify, vlen
                                  1, &app.app_ranging,
                                  &service_handle,
                                  &char_ranging_enable_handle);

    // Status
    simple_ble_add_characteristic(1, 0, 0, 0, // read, write, notify, vlen
                                  1,(uint8_t*) &tripoint_inited,
                                  &service_handle,
                                  &char_status_handle);
}


int main (void)
{
    uint32_t err_code;

    // Initialization
    led_init(LED_0);
    debug_msg("\r\n----------------------------------------------\r\n");
    debug_msg("Initializing nRF...\r\n");

    // We default to doing ranging at the start
    app.app_ranging = 1;

    // Set to effective -1
    app.calibration_index = 255;

    // Get stored address
    memcpy(_ble_address, (uint8_t*) ADDRESS_FLASH_LOCATION, 6);
    // And use it to setup the BLE
    ble_config.device_id = (uint16_t)( (uint16_t)_ble_address[1] << (uint8_t)8) | _ble_address[0];

    // Setup BLE
    simple_ble_app = simple_ble_init(&ble_config);

    // Setup the advertisement to use the Eddystone format.
    // We include the device name in the scan response
    debug_msg("Starting advertisement...\r\n");
    ble_advdata_t srdata;
    memset(&srdata, 0, sizeof(srdata));
    srdata.name_type = BLE_ADVDATA_FULL_NAME;
    eddystone_adv(PHYSWEB_URL, &srdata);

    // Need a timer to make sure we have inited the tripoint
    timers_init();

    // Init the nRF hardware to work with the tripoint module.
    err_code = tripoint_hw_init();
    APP_ERROR_CHECK(err_code);

    // Init the state machine on the tripoint
    err_code = tripoint_init(updateData);
    if (err_code == NRF_SUCCESS) {
        tripoint_inited = true;
        debug_msg("Finished initialization\r\n");
    } else {
        debug_msg("ERROR: Failed initialization!\r\n");
    }

    // Start the ranging
    if (tripoint_inited) {
        err_code = tripoint_start_ranging(true, 10);
        if (err_code != NRF_SUCCESS) {
            debug_msg("ERROR: Failed to start ranging!\r\n");
        } else {
            debug_msg("Started ranging...\r\n");
        }
    }

    // Signal end of initialization
    led_on(LED_0);

    // Loop: update location and advertise
    while (1) {

        power_manage();

		if (updated) {
		    //debug_msg("Updating location...\r\n");
			tripointDataUpdate();
		}
    }
}
