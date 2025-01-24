// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "logging.h"
#include "tusb.h"


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

const uint8_t* tud_descriptor_configuration_cb(uint8_t)
{
   return (tud_speed_get() == TUSB_SPEED_HIGH) ? high_speed_configuration : full_speed_configuration;
}

const uint16_t* tud_descriptor_string_cb(uint8_t index, uint16_t)
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

AM_USED void tud_mount_cb(void)
{
   print("tud_mount_cb\n");
}

AM_USED void tud_umount_cb(void)
{
   print("tud_umount_cb\n");
}

AM_USED void tud_suspend_cb(bool)
{
   print("tud_suspend_cb\n");
}

AM_USED void tud_resume_cb(void)
{
   print("tud_resume_cb\n");
}

AM_USED void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
   (void) itf;
   (void) rts;

   // connected
   if (dtr)
   {
      // print initial message when connected
      am_util_stdio_printf("dtr up\r\n");
      tud_cdc_write_str("\r\nTinyUSB CDC device example\r\n");
      tud_cdc_write_flush();
   }
   else
      am_util_stdio_printf("dtr down\r\n");
}


// Private Helper Functions --------------------------------------------------------------------------------------------

//*****************************************************************************
//
//! @brief callback called from tinyusb tud_task code on powerup (reconnect)
//!
//! @param x unused
//
//*****************************************************************************
/*static void usb_disconn_pwrUpCB(void*)
{
   print("usb_disconn_pwrUpCB\n");
   dcd_powerup(0, false);
   tud_connect();
}*/

//*****************************************************************************
//
//! @brief callback called from tinyusb code on powerloss (shutdown)
//!
//! @details this is called from the USB task in response to a powerDown event
//! the powerDown event was issued above in the usb_disconnect_callPowerDown call
//! this will perform a graceful usb shutdown
//!
//! @note This function should only be called from the tusb_task. It is called in the
//! background or in the tud_task(if rtos).
//!
//! @param x unused
//
//*****************************************************************************
/*static void usb_disconn_pwrDownCB(void*)
{
   print("usb_disconn_pwrDownCB\n");
   dcd_powerdown(0, false);
}*/


// Public API Functions ------------------------------------------------------------------------------------------------

void UsbTask(void*)
{
   // Initialize the TinyUSB library
   tusb_init();
   NVIC_SetPriority(USB0_IRQn, NVIC_configMAX_SYSCALL_INTERRUPT_PRIORITY);

   // Loop forever handling USB device events
   while (true)
      tud_task();
}

void UsbCdcTask(void*)
{
   // Loop forever listening for incoming data
   while (true)
   {
      // Check for rx and tx buffer watermark
      uint32_t rx_count = tud_cdc_available();
      uint32_t tx_avail = tud_cdc_write_available();

      // If data received and there is enough tx buffer to echo back. Else
      // keep rx FIFO and wait for next service.
      if ((rx_count != 0) && (tx_avail >= rx_count))
      {
         uint8_t buf_rx[CFG_TUD_CDC_RX_BUFSIZE];

         // read and echo back
         uint32_t count = tud_cdc_read(buf_rx, sizeof(buf_rx));

         tud_cdc_write(buf_rx, count);
         tud_cdc_write_flush();
      }

      // Allow lower priority tasks to run
      taskYIELD();
   }
}
