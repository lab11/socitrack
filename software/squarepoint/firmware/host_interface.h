#ifndef __HOST_INTERFACE_H
#define __HOST_INTERFACE_H

// Includes ------------------------------------------------------------------------------------------------------------

#include <stdint.h>
#include <system.h>

// Defines -------------------------------------------------------------------------------------------------------------

// List of command byte opcodes for messages from the I2C master to us
#define HOST_CMD_INFO                               0x01
#define HOST_CMD_READ_CALIBRATION                   0x02
#define HOST_CMD_READ_PACKET_LENGTH                 0x03
#define HOST_CMD_READ_PACKET                        0x04
#define HOST_CMD_START                              0x05
#define HOST_CMD_STOP                               0x06
#define HOST_CMD_SET_TIME                           0x07
#define HOST_CMD_WAKEUP                             0x08
#define HOST_CMD_ACK                                0x09

// Structs for parsing the messages for each command
#define HOST_PKT_CONFIG_MAIN_DEVICE_ROLE_MASK		0x07
#define HOST_PKT_CONFIG_MAIN_DEVICE_ROLE_SHIFT		0
#define HOST_PKT_CONFIG_MAIN_SCHEDULER_ROLE_MASK    0x38
#define HOST_PKT_CONFIG_MAIN_SCHEDULER_ROLE_SHIFT   3
#define HOST_PKT_CONFIG_MAIN_APP_MASK               0xC0
#define HOST_PKT_CONFIG_MAIN_APP_SHIFT              6


// Data structures -----------------------------------------------------------------------------------------------------

typedef enum
{
   HOST_IFACE_INTERRUPT_RANGES = 0x01,
   HOST_IFACE_INTERRUPT_CALIBRATION = 0x02,
   HOST_IFACE_INTERRUPT_WAKEUP = 0x03,
   HOST_IFACE_INTERRUPT_STOPPED = 0x04,
   HOST_IFACE_INTERRUPT_REQUEST_TIME = 0x05,
   HOST_IFACE_INTERRUPT_PING = 0x06
} interrupt_reason_t;

// Public functions ----------------------------------------------------------------------------------------------------

uint32_t host_interface_init(void);
void host_interface_wait(void);
void host_interface_notify_ranges(uint8_t *ids_ranges, uint8_t len);
void host_interface_notify_calibration(uint8_t *calibration_data, uint8_t len);
void host_interface_schedule_wakeup(uint8_t quarter_wakeup_delay_ms);
void host_interface_ping_host(void);
void host_interface_request_time(void);
void host_interface_notify_stopped(void);

// Interrupt callbacks
void host_interface_rx_fired(void);
void host_interface_error_occurred(void);

#endif // __HOST_INTERFACE_H
