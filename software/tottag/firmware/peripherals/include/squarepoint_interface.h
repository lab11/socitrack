#ifndef __SQUAREPOINT_INTERFACE_H
#define __SQUAREPOINT_INTERFACE_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include "nrfx_atomic.h"


// SquarePoint access details ------------------------------------------------------------------------------------------

#define SQUAREPOINT_ID                                  0xB01A
#define SQUAREPOINT_ADDRESS                             0x65
#define SQUAREPOINT_EUI_LEN                             1


// SquarePoint outgoing command types ----------------------------------------------------------------------------------

#define SQUAREPOINT_CMD_INFO                            0x01
#define SQUAREPOINT_CMD_READ_CALIBRATION                0x02
#define SQUAREPOINT_CMD_READ_PACKET_LENGTH              0x03
#define SQUAREPOINT_CMD_READ_PACKET                     0x04
#define SQUAREPOINT_CMD_START                           0x05
#define SQUAREPOINT_CMD_STOP                            0x06
#define SQUAREPOINT_CMD_SET_TIME                        0x07
#define SQUAREPOINT_CMD_WAKEUP                          0x08
#define SQUAREPOINT_CMD_ACK                             0x09


// SquarePoint incoming message types ----------------------------------------------------------------------------------

#define SQUAREPOINT_INCOMING_RANGES                     0x01
#define SQUAREPOINT_INCOMING_CALIBRATION                0x02
#define SQUAREPOINT_INCOMING_WAKEUP                     0x03
#define SQUAREPOINT_INCOMING_STOPPED                    0x04
#define SQUAREPOINT_INCOMING_REQUEST_TIME               0x05
#define SQUAREPOINT_INCOMING_PING                       0x06


// SquarePoint runtime modes -------------------------------------------------------------------------------------------

#define SQUAREPOINT_RUNTIME_MODE_STANDARD               0x01
#define SQUAREPOINT_RUNTIME_MODE_CALIBRATION            0x02


// Internal typedefs ---------------------------------------------------------------------------------------------------

typedef void (*squarepoint_interface_data_callback)(uint8_t* data, uint32_t len);


// Public SquarePoint Interface API ------------------------------------------------------------------------------------

nrfx_err_t squarepoint_init(nrfx_atomic_flag_t* incoming_data_flag, squarepoint_interface_data_callback callback, const uint8_t* eui);
nrfx_err_t squarepoint_start_application(uint32_t current_time, uint8_t device_role, uint8_t scheduler_role);
nrfx_err_t squarepoint_start_calibration(uint8_t index);
nrfx_err_t squarepoint_get_calibration(uint8_t* calib_buf);
nrfx_err_t squarepoint_stop(void);
nrfx_err_t squarepoint_set_time(uint32_t epoch);
nrfx_err_t squarepoint_wakeup_radio(void);
nrfx_err_t squarepoint_ack(void);
void squarepoint_handle_incoming_data(void);

#endif // #ifndef __SQUAREPOINT_INTERFACE_H
