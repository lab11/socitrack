// Header inclusions ---------------------------------------------------------------------------------------------------

#include "boards.h"
#include "sd_card.h"
#include "usb.h"

#if (BOARD_V >= 0x10)

#include "app_usbd.h"
#include "app_usbd_cdc_acm.h"
#include "app_usbd_serial_num.h"
#include "nrfx_atomic.h"
#include "nrf_drv_clock.h"


// USB state variables -------------------------------------------------------------------------------------------------

static void usb_cdc_acm_event_handler(app_usbd_class_inst_t const *p_inst, app_usbd_cdc_acm_user_event_t event);
APP_USBD_CDC_ACM_GLOBAL_DEF(_usb_cdc_acm_instance, usb_cdc_acm_event_handler, CDC_ACM_COMM_INTERFACE, CDC_ACM_DATA_INTERFACE,
      CDC_ACM_COMM_EPIN, CDC_ACM_DATA_EPIN, CDC_ACM_DATA_EPOUT, APP_USBD_CDC_COMM_PROTOCOL_AT_V250);

static nrfx_atomic_flag_t _usb_request_flag = false;
static nrfx_atomic_flag_t _usb_ongoing_transmission = false, _usb_currently_writing = false;
static uint8_t _usb_request_buffer[USB_REQUEST_MAX_SIZE] = { 0 };
static uint8_t _usb_transfer_buffer[1024] = { 0 };
static uint32_t _usb_transfer_buffer_index = 0, _usb_transfer_buffer_fill_size = 0;


// Private helper functions --------------------------------------------------------------------------------------------

static void usb_cdc_acm_event_handler(app_usbd_class_inst_t const *p_inst, app_usbd_cdc_acm_user_event_t event)
{
   switch (event)
   {
      case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN:
      {
         printf("INFO: USB comm port opened!\n");
         memset(&_usb_request_buffer, 0, sizeof(_usb_request_buffer));
         if (app_usbd_cdc_acm_read_any(&_usb_cdc_acm_instance, _usb_request_buffer, sizeof(_usb_request_buffer)) == NRFX_SUCCESS)
            nrfx_atomic_flag_set(&_usb_request_flag);
         break;
      }
      case APP_USBD_CDC_ACM_USER_EVT_RX_DONE:
      {
         ret_code_t ret;
         do
         {
            nrfx_atomic_flag_set(&_usb_request_flag);
            ret = app_usbd_cdc_acm_read_any(&_usb_cdc_acm_instance, _usb_request_buffer, sizeof(_usb_request_buffer));
         } while (ret == NRFX_SUCCESS);
         break;
      }
      case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
         printf("INFO: USB comm port closed!\n");
         break;
      case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
         nrfx_atomic_flag_clear(&_usb_currently_writing);
         break;
      default:
         break;
   }
}

static void usb_event_handler(app_usbd_event_type_t event)
{
   // Disable the USB driver when not in use
   if (event == APP_USBD_EVT_STOPPED)
      app_usbd_disable();
}

static void transfer_sd_file_over_usb(void)
{
   _usb_transfer_buffer_fill_size = sd_card_read_reading_file(_usb_transfer_buffer, sizeof(_usb_transfer_buffer));
   _usb_transfer_buffer_index = usb_send_data(_usb_transfer_buffer, _usb_transfer_buffer_fill_size);
   if (_usb_transfer_buffer_fill_size)
      nrfx_atomic_flag_set(&_usb_ongoing_transmission);
   else
   {
      nrfx_atomic_flag_clear(&_usb_ongoing_transmission);
      sd_card_close_reading_file();
   }
}

static void handle_usb_request(void)
{
   // Handle an incoming USB data request
   switch (_usb_request_buffer[0])
   {
      case USB_LOG_LISTING_REQUEST:
      {
         uint32_t file_size = 0;
         uint8_t continuation = 0;
         char file_name[16] = { 0 };
         printf("INFO: Incoming USB request to list SD card files:\n");
         _usb_transfer_buffer_index = _usb_transfer_buffer_fill_size = 0;
         while (sd_card_list_files(file_name, &file_size, continuation++))
         {
            printf("      %s (%lu bytes)\n", file_name, file_size);
            memcpy(_usb_transfer_buffer + _usb_transfer_buffer_fill_size, file_name, 1 + strlen(file_name));
            _usb_transfer_buffer_fill_size += (1 + strlen(file_name));
         }
         _usb_transfer_buffer_index = usb_send_data(_usb_transfer_buffer, _usb_transfer_buffer_fill_size);
         break;
      }
      case USB_LOG_DOWNLOAD_REQUEST:
      {
         printf("INFO: Incoming USB request to download SD card file: %s\n", (char*)(_usb_request_buffer + 1));
         if (sd_card_open_file_for_reading((char*)(_usb_request_buffer + 1)))
            transfer_sd_file_over_usb();
         break;
      }
      case USB_LOG_ERASE_REQUEST:
      {
         printf("INFO: Incoming USB request to delete SD card file: %s\n", (char*)(_usb_request_buffer + 1));
         sd_card_erase_file((char*)(_usb_request_buffer + 1));
         break;
      }
      default:
         printf("WARNING: Ignoring unknown or unexpected incoming USB request\n");
         break;
   }
}

#endif // #if (BOARD_V >= 0x10)


// Public USB functionality --------------------------------------------------------------------------------------------

void usb_init(void)
{
#if (BOARD_V >= 0x10)

   // Initialize the USB interface
   const app_usbd_config_t usbd_config = { .ev_state_proc = usb_event_handler };
   app_usbd_serial_num_generate();
   APP_ERROR_CHECK(nrf_drv_clock_init());
   APP_ERROR_CHECK(app_usbd_init(&usbd_config));

   // Setup the USB interface to implement the CDC ACM class
   app_usbd_class_inst_t const *class_cdc_acm = app_usbd_cdc_acm_class_inst_get(&_usb_cdc_acm_instance);
   APP_ERROR_CHECK(app_usbd_class_append(class_cdc_acm));

#endif
}

void usb_change_power_status(bool plugged_in)
{
#if (BOARD_V >= 0x10)

   // Enable/disable USB stack here because APP_USBD power events do not work when SoftDevice is running
   if (plugged_in)
   {
      if (!nrfx_usbd_is_enabled())
         app_usbd_enable();
      while (!nrfx_usbd_is_enabled());
      if (!nrfx_usbd_is_started())
         app_usbd_start();
   }
   else
   {
      if (nrfx_usbd_is_started())
         app_usbd_stop();
   }

#endif
}

void usb_handle_comms(void)
{
#if (BOARD_V >= 0x10)

   // Process all enqueued USB events
   while (app_usbd_event_queue_process());
   if (!nrfx_atomic_flag_fetch(&_usb_currently_writing))
   {
      // Determine if there is any outstanding data waiting to be transferred
      if (_usb_transfer_buffer_index < _usb_transfer_buffer_fill_size)
         _usb_transfer_buffer_index += usb_send_data(_usb_transfer_buffer + _usb_transfer_buffer_index, _usb_transfer_buffer_fill_size - _usb_transfer_buffer_index);
      else if (nrfx_atomic_flag_fetch(&_usb_ongoing_transmission))
         transfer_sd_file_over_usb();
      else if (nrfx_atomic_flag_clear_fetch(&_usb_request_flag))
         handle_usb_request();
   }

#endif
}

uint32_t usb_send_data(const void *data, uint32_t length)
{
#if (BOARD_V >= 0x10)

   // Send new data
   if (length)
   {
      nrfx_atomic_flag_set(&_usb_currently_writing);
      size_t bytes_to_send = MIN(length, NRFX_USBD_EPSIZE);
      app_usbd_cdc_acm_write(&_usb_cdc_acm_instance, data, bytes_to_send);
      return (uint32_t)bytes_to_send;
   }

#endif
   return 0;
}
