// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "battery.h"
#include "logging.h"
#include "rtc.h"
#include "storage.h"
#include "system.h"
#include "tusb.h"
#include "usb.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

#define CONFIG_TOTAL_LEN        (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

#define EPNUM_CDC_NOTIF         0x81
#define EPNUM_CDC_OUT           0x02
#define EPNUM_CDC_IN            0x82

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

static const tusb_desc_device_t usb_descriptor =
{
   .bLength            = sizeof(tusb_desc_device_t),
   .bDescriptorType    = TUSB_DESC_DEVICE,
   .bcdUSB             = 0x0200,
   .bDeviceClass       = TUSB_CLASS_MISC,
   .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
   .bDeviceProtocol    = MISC_PROTOCOL_IAD,
   .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
   .idVendor           = USB_VID,
   .idProduct          = USB_PID,
   .bcdDevice          = 0x0100,
   .iManufacturer      = 0x01,
   .iProduct           = 0x02,
   .iSerialNumber      = 0x03,
   .bNumConfigurations = 0x01
};

static char const* string_descriptions[] =
{
   (const char[]){ 0x09, 0x04 },    // Supported Language: English (0x0409)
   MANUFACTURER,                    // Manufacturer
   HW_MODEL,                        // Product
   SERIAL_NUMBER,                   // Serial Number
   HW_MODEL" CDC",                  // CDC Interface
};

static const uint8_t full_speed_configuration[] =
{
   TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
   TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

static const uint8_t high_speed_configuration[] =
{
   TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
   TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 512),
};


// TinyUSB Callback Functions ------------------------------------------------------------------------------------------

const uint8_t* tud_descriptor_device_cb(void)
{
   return (const uint8_t*)&usb_descriptor;
}

const uint8_t* tud_descriptor_configuration_cb(uint8_t index)
{
   return (tud_speed_get() == TUSB_SPEED_HIGH) ? high_speed_configuration : full_speed_configuration;
}

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
   static uint16_t string_description[32];
   uint8_t chr_count;
   if (index == 0)
   {
      memcpy(&string_description[1], string_descriptions[0], 2);
      chr_count = 1;
   }
   else
   {
      if (!(index < (sizeof(string_descriptions) / sizeof(string_descriptions[0]))))
         return NULL;

      // Convert ASCII string into UTF-16
      const char* str = string_descriptions[index];
      chr_count = strlen(str);
      for (uint8_t i = 0; i < chr_count; ++i)
         string_description[i+1] = str[i];
   }
   string_description[0] = (TUSB_DESC_STRING << 8) | ((2 * chr_count) + 2);
   return string_description;
}

AM_USED void tud_suspend_cb(bool remote_wakeup_en)
{
   // USB cable disconnected, reboot
   print("INFO: USB cable disconnected...rebooting!\n");
   system_reset(true);
}

AM_USED void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
   // Log that a USB connection change has occurred
   print("INFO: USB connection %s\n", dtr ? "opened" : "closed");
}


// Public API Functions ------------------------------------------------------------------------------------------------

extern void app_maintenance_activate_find_my_tottag(uint32_t seconds_to_activate);
extern void app_maintenance_download_log_file(uint32_t start_time, uint32_t end_time);

uint32_t usb_write(const void* data, uint32_t num_bytes)
{
   return tud_cdc_write(data, num_bytes);
}

void UsbTask(void *params)
{
   // Initialize the TinyUSB library
   tusb_init();

   // Loop forever handling USB device events
   while (true)
      tud_task();
}

void UsbCdcTask(void *params)
{
   // Set up task variables
   usb_command_t command;
   uint8_t uid[EUI_LEN + 1];
   uint32_t download_start_timestamp = 0, download_end_timestamp = 0;
   system_read_UID(uid, sizeof(uid));
   uid[EUI_LEN] = '\n';

   // Put the storage peripheral into maintenance mode
   storage_enter_maintenance_mode();

   // Loop forever listening for incoming data
   while (true)
   {
      // Read from USB
      if (tud_cdc_available() && tud_cdc_read(&command, 1) == 1)
         switch (command)
         {
            case USB_VERSION_COMMAND:
            {
               static const uint8_t firmware_ver[] = FW_REVISION"\n";
               static const uint8_t hardware_ver[] = HW_REVISION"\n";
               tud_cdc_write(firmware_ver, sizeof(firmware_ver) - 1);
               tud_cdc_write(hardware_ver, sizeof(hardware_ver) - 1);
               tud_cdc_write_flush();
               break;
            }
            case USB_VOLTAGE_COMMAND:
            {
               uint16_t voltage = (uint16_t)battery_monitor_get_level_mV();
               tud_cdc_write(&voltage, sizeof(voltage));
               tud_cdc_write_flush();
               break;
            }
            case USB_GET_TIMESTAMP_COMMAND:
            {
               uint32_t timestamp = rtc_get_timestamp();
               tud_cdc_write(&timestamp, sizeof(timestamp));
               tud_cdc_write_flush();
               break;
            }
            case USB_SET_TIMESTAMP_COMMAND:
            {
               uint32_t timestamp = 0;
               if (tud_cdc_read(&timestamp, sizeof(timestamp)) == sizeof(timestamp))
                  rtc_set_time_from_timestamp(timestamp);
               break;
            }
            case USB_FIND_MY_TOTTAG_COMMAND:
               app_maintenance_activate_find_my_tottag(10);
               break;
            case USB_GET_EXPERIMENT_COMMAND:
            {
               experiment_details_t details = { 0 };
               uint16_t details_len = (uint16_t)sizeof(details);
               storage_retrieve_experiment_details(&details);
               tud_cdc_write(&details_len, sizeof(details_len));
               tud_cdc_write(&details, sizeof(details));
               tud_cdc_write_flush();
               break;
            }
            case USB_DELETE_EXPERIMENT_COMMAND:
            {
               const experiment_details_t empty_details = { 0 };
               storage_store_experiment_details(&empty_details);
               break;
            }
            case USB_NEW_EXPERIMENT_COMMAND:
            {
               experiment_details_t new_details = { 0 };
               if (tud_cdc_read(&new_details, sizeof(new_details)) == sizeof(new_details))
                  storage_store_experiment_details(&new_details);
               break;
            }
            case USB_SET_LOG_DL_DATES_COMMAND:
            {
               tud_cdc_read(&download_start_timestamp, sizeof(download_start_timestamp));
               tud_cdc_read(&download_end_timestamp, sizeof(download_end_timestamp));
               break;
            }
            case USB_DOWNLOAD_LOG_COMMAND:
            {
               print("INFO: Transferring log over USB...\n");
               uint16_t details_len = (uint16_t)sizeof(experiment_details_t);
               tud_cdc_write(uid, sizeof(uid));
               tud_cdc_write(&details_len, sizeof(details_len));
               app_maintenance_download_log_file(download_start_timestamp, download_end_timestamp);
               tud_cdc_write_flush();
               print("INFO: USB log transfer complete!\n");
               break;
            }
            default:
               break;
         }

      // Allow lower priority tasks to run
      taskYIELD();
   }
}
