#ifndef __MODULE_INTERFACE_H
#define __MODULE_INTERFACE_H

#include "stdbool.h"
#include "sdk_errors.h"

#define MODULE_ID 0xB01A

#define MODULE_ADDRESS 0x65

#define MODULE_CMD_INFO             0x01
#define MODULE_CMD_CONFIG           0x02
#define MODULE_CMD_READ_INTERRUPT   0x03
#define MODULE_CMD_DO_RANGE         0x04
#define MODULE_CMD_SLEEP            0x05
#define MODULE_CMD_RESUME           0x06
#define MODULE_CMD_SET_LOCATION     0x07
#define MODULE_CMD_READ_CALIBRATION 0x08
#define MODULE_CMD_SET_TIME         0x09

// App configuration
#define APP_STANDARD    0x00
#define APP_CALIBRATION 0x01

// Defines for identifying data sent to host
#define HOST_IFACE_INTERRUPT_RANGES         0x01
#define HOST_IFACE_INTERRUPT_CALIBRATION    0x02
#define HOST_IFACE_INTERRUPT_MASTER_EUI     0x03
#define HOST_IFACE_INTERRUPT_RANGES_RAW     0x04

// Ranging errors
#define ONEWAY_TAG_RANGE_MIN (-1000)
// The ANCHOR did not receive matching packets from the first three cycle.
// This prevents us from calculating clock skew, and we have to skip this anchor range.
#define ONEWAY_TAG_RANGE_ERROR_NO_OFFSET      0x80000001
// The anchor did not receive enough packets from the tag, so we don't have
// enough observations (ranges) to actually calculate a range to this anchor.
#define ONEWAY_TAG_RANGE_ERROR_TOO_FEW_RANGES 0x80000002
// Something else went wrong that we don't have pinned down.
#define ONEWAY_TAG_RANGE_ERROR_MISC           0x8000000F

typedef void (*module_interface_data_cb_f)(uint8_t* data, uint32_t len);


ret_code_t module_init (bool* module_interrupt_thrown, module_interface_data_cb_f cb);
ret_code_t module_hw_init ();
ret_code_t module_get_info (uint16_t* id, uint8_t* version);
ret_code_t module_start_role(uint8_t role, bool is_glossy_master, uint8_t master_eui);
ret_code_t module_start_ranging (uint8_t master_eui);
ret_code_t module_start_anchor (bool is_glossy_master, uint8_t master_eui);
ret_code_t module_start_calibration (uint8_t index);
ret_code_t module_get_calibration (uint8_t* calib_buf);
ret_code_t module_interrupt_dispatch ();
ret_code_t module_sleep ();
ret_code_t module_resume ();
ret_code_t module_set_time(uint32_t epoch);

#endif
