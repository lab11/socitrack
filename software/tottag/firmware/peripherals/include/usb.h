#ifndef __USB_HEADER_H
#define __USB_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include <stdint.h>


// Typedefs and definitions --------------------------------------------------------------------------------------------

#define CDC_ACM_COMM_INTERFACE      0
#define CDC_ACM_COMM_EPIN           NRFX_USBD_EPIN2
#define CDC_ACM_DATA_INTERFACE      1
#define CDC_ACM_DATA_EPIN           NRFX_USBD_EPIN1
#define CDC_ACM_DATA_EPOUT          NRFX_USBD_EPOUT1
#define USB_REQUEST_MAX_SIZE        32

#define USB_LOG_LISTING_REQUEST     1
#define USB_LOG_DOWNLOAD_REQUEST    2
#define USB_LOG_ERASE_REQUEST       3


// Public USB functionality --------------------------------------------------------------------------------------------

void usb_init(void);
void usb_change_power_status(bool plugged_in);
void usb_handle_comms(void);
uint32_t usb_send_data(const void *data, uint32_t length);

#endif // #ifndef __USB_HEADER_H
