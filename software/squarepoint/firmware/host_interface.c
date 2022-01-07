#include <stdio.h>
#include <string.h>
#include "atomic.h"
#include "board.h"
#include "calibration.h"
#include "configuration.h"
#include "delay.h"
#include "dw1000.h"
#include "glossy.h"
#include "host_interface.h"
#include "scheduler.h"
#include "SEGGER_RTT.h"
#include "stm32f0xx_i2c_cpal.h"
#include "stm32f0xx_i2c_cpal_hal.h"

// Scratch variables ---------------------------------------------------------------------------------------------------

static GPIO_InitTypeDef GPIO_InitStructure;
static volatile uint32_t tx_needs_ack = 0;
static uint8_t rxBuffer[32] = { 0 };
static uint8_t txBuffer[256] = { 0 };

// Device EUI storage
uint8_t full_eui[EUI_LEN] = { 0 };

// CPAL local transfer structures
static CPAL_TransferTypeDef rxStructure = { 0 };
static CPAL_TransferTypeDef txStructure = { 0 };

// INFO response packets, last byte is the version
static uint8_t INFO_PKT[3] = { 0xb0, 0x1a, 1 };
static uint8_t NULL_PKT[3] = { 0xaa, 0xaa, 0 };


// Public functions ----------------------------------------------------------------------------------------------------

uint32_t host_interface_init(void)
{
   // Reset the acknowledgment flag
   atomic_clear(&tx_needs_ack);

   // Enable the Interrupt pin
   RCC_AHBPeriphClockCmd(EXT_INTERRUPT_CLK, ENABLE);
   GPIO_InitStructure.GPIO_Pin = EXT_INTERRUPT_PIN;
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_DOWN;
   GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
   GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_3;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);

   // Initialize local I2C Reception structures
   rxStructure.wNumData = sizeof(rxBuffer);     /* Maximum Number of data to be received */
   rxStructure.pbBuffer = rxBuffer;             /* Common Rx buffer for all received data */
   rxStructure.wAddr1 = 0;                      /* Not needed */
   rxStructure.wAddr2 = 0;                      /* Not needed */

   // Initialize local I2C Transmission structures
   txStructure.wNumData = sizeof(txBuffer);     /* Maximum Number of data to be sent */
   txStructure.pbBuffer = txBuffer;             /* Common Tx buffer for all received data */
   txStructure.wAddr1 = (I2C_OWN_ADDRESS << 1); /* The own board address */
   txStructure.wAddr2 = 0;                      /* Not needed */

   // Set HSI as I2C clock source
   RCC_I2CCLKConfig(RCC_I2C1CLK_HSI);

   // Configure the device structure
   CPAL_I2C_DeInit(&I2C1_DevStructure);
   CPAL_I2C_StructInit(&I2C1_DevStructure);
   I2C1_DevStructure.CPAL_Mode = CPAL_MODE_SLAVE;
   I2C1_DevStructure.CPAL_ProgModel = CPAL_PROGMODEL_INTERRUPT;
   I2C1_DevStructure.wCPAL_Options = CPAL_OPT_NO_MEM_ADDR | CPAL_OPT_I2C_NACK_ADD | CPAL_OPT_I2C_WAKEUP_STOP;
   I2C1_DevStructure.pCPAL_TransferRx = &rxStructure;
   I2C1_DevStructure.pCPAL_TransferTx = &txStructure;
   I2C1_DevStructure.pCPAL_I2C_Struct->I2C_Timing = I2C_TIMING;
   I2C1_DevStructure.pCPAL_I2C_Struct->I2C_OwnAddress1 = (I2C_OWN_ADDRESS << 1);

   // Initialize CPAL device with the selected parameters
   uint32_t ret = CPAL_I2C_Init(&I2C1_DevStructure);
   I2C_StretchClockCmd(I2C1, ENABLE);
   return ret ? ret : CPAL_I2C_Read(&I2C1_DevStructure);
}

void host_interface_wait(void)
{
   // Re-enter a wait state for incoming data from the host
   if (I2C1_DevStructure.CPAL_State == CPAL_STATE_READY)
   {
      rxStructure.wNumData = sizeof(rxBuffer);
      rxStructure.pbBuffer = rxBuffer;
      CPAL_I2C_Read(&I2C1_DevStructure);
   }
}

// Send the ranges to the host
void host_interface_notify_ranges(uint8_t *ids_ranges, uint8_t len)
{
   // Output the number of ranges being reported
   if (ids_ranges[0])
   {
      debug_msg("INFO: Reporting range to ");
      debug_msg_uint(ids_ranges[0]);
      debug_msg(" device(s):\n");
   }

   // Output the ranging results
   PROTOCOL_EUI_TYPE eui = 0;
   uint32_t timestamp = 0, range = 0;
   memcpy(&timestamp, ids_ranges + len - sizeof(timestamp), sizeof(timestamp));
   for (uint8_t i = 0, offset = 1 + PROTOCOL_EUI_SIZE; i < ids_ranges[0]; ++i, offset += (PROTOCOL_EUI_SIZE + sizeof(range)))
   {
      memcpy(&eui, ids_ranges + offset, PROTOCOL_EUI_SIZE);
      memcpy(&range, ids_ranges + offset + PROTOCOL_EUI_SIZE, sizeof(range));
      debug_msg("INFO:    Centimeters to EUI ");
      debug_msg_eui(eui);
      debug_msg(" @ Timestamp ");
      debug_msg_uint(timestamp);
      debug_msg(": ");
      if (range < MAX_VALID_RANGE_MM)
         debug_msg_uint(range / 10);
      else
         debug_msg("Unable to calculate!");
      debug_msg("\n");
   }

   // Wait for the host to complete any pending reads
   if (atomic_read(&tx_needs_ack))
   {
      host_interface_error_occurred();
      return;
   }
   while (GPIO_ReadInputDataBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN) == Bit_SET);

   // Set up the transfer buffer
   txBuffer[0] = 1 + len;
   txBuffer[1] = HOST_IFACE_INTERRUPT_RANGES;
   memcpy(txBuffer + 2, ids_ranges, len);

   // Inform the host of impending data transfer
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
   GPIO_WriteBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN, Bit_SET);
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
}

void host_interface_notify_calibration(uint8_t *calibration_data, uint8_t len)
{
   // Wait for the host to complete any pending reads
   if (atomic_read(&tx_needs_ack))
   {
      host_interface_error_occurred();
      return;
   }
   while (GPIO_ReadInputDataBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN) == Bit_SET);

   // Set up the transfer buffer
   txBuffer[0] = 1 + len;
   txBuffer[1] = HOST_IFACE_INTERRUPT_CALIBRATION;
   memcpy(txBuffer + 2, calibration_data, len);

   // Inform the host of impending data transfer
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
   GPIO_WriteBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN, Bit_SET);
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
}

void host_interface_schedule_wakeup(uint8_t quarter_wakeup_delay_ms)
{
   // Wait for the host to complete any pending reads
   if (atomic_read(&tx_needs_ack))
   {
      host_interface_error_occurred();
      return;
   }
   while (GPIO_ReadInputDataBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN) == Bit_SET);

   // Set up the transfer buffer
   txBuffer[0] = 2;
   txBuffer[1] = HOST_IFACE_INTERRUPT_WAKEUP;
   txBuffer[2] = quarter_wakeup_delay_ms;

   // Inform the host of impending data transfer
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
   GPIO_WriteBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN, Bit_SET);
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
}

void host_interface_ping_host(void)
{
   // Wait for the host to complete any pending reads
   if (atomic_read(&tx_needs_ack))
   {
      host_interface_error_occurred();
      return;
   }
   while (GPIO_ReadInputDataBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN) == Bit_SET);

   // Set up the transfer buffer
   txBuffer[0] = 1;
   txBuffer[1] = HOST_IFACE_INTERRUPT_PING;

   // Inform the host of impending data transfer
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
   GPIO_WriteBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN, Bit_SET);
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
}

void host_interface_request_time(void)
{
   // Wait for the host to complete any pending reads
   if (atomic_read(&tx_needs_ack))
   {
      host_interface_error_occurred();
      return;
   }
   while (GPIO_ReadInputDataBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN) == Bit_SET);

   // Set up the transfer buffer
   txBuffer[0] = 1;
   txBuffer[1] = HOST_IFACE_INTERRUPT_REQUEST_TIME;

   // Inform the host of impending data transfer
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
   GPIO_WriteBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN, Bit_SET);
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
}

void host_interface_notify_stopped(void)
{
   // Wait for the host to complete any pending reads
   if (atomic_read(&tx_needs_ack))
   {
      host_interface_error_occurred();
      return;
   }
   while (GPIO_ReadInputDataBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN) == Bit_SET);

   // Set up the transfer buffer
   txBuffer[0] = 1;
   txBuffer[1] = HOST_IFACE_INTERRUPT_STOPPED;

   // Inform the host of impending data transfer
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
   GPIO_WriteBit(EXT_INTERRUPT_PORT, EXT_INTERRUPT_PIN, Bit_SET);
   GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
   GPIO_Init(EXT_INTERRUPT_PORT, &GPIO_InitStructure);
}

// Called after a WRITE message from the Host
void host_interface_rx_fired(void)
{
   // Re-enter a wait state for incoming data from the host
   rxStructure.wNumData = sizeof(rxBuffer);
   rxStructure.pbBuffer = rxBuffer;
   CPAL_I2C_Read(&I2C1_DevStructure);
   reset_watchdog();

   // First byte of every correct WRITE packet is the opcode of the packet
   switch (rxBuffer[0])
   {
      /**********************************************************************/
      // Configure the module. This can be called multiple times to change the setup.
      /**********************************************************************/
      case HOST_CMD_START:
      {
         // This packet configures the module and is what kicks off using it
         debug_msg("INFO: Received command from host: HOST_CMD_START\n");
         app_mode_t app_mode = (rxBuffer[1] & HOST_PKT_CONFIG_MAIN_APP_MASK) >> HOST_PKT_CONFIG_MAIN_APP_SHIFT;
         switch (app_mode)
         {
            case APP_STANDARD:
            {
               app_config_t config;
               config.scheduler_role = (rxBuffer[1] & HOST_PKT_CONFIG_MAIN_SCHEDULER_ROLE_MASK) >> HOST_PKT_CONFIG_MAIN_SCHEDULER_ROLE_SHIFT;
               config.device_role = (rxBuffer[1] & HOST_PKT_CONFIG_MAIN_DEVICE_ROLE_MASK) >> HOST_PKT_CONFIG_MAIN_DEVICE_ROLE_SHIFT;
               config.hybrids_perform_all_rangings = HYBRIDS_PERFORM_ALL_RANGINGS;
               config.radio_sleep_while_passive = STM_RADIO_SLEEP_WHILE_PASSIVE;
               config.radio_wakeup_from_host = ALLOW_HOST_TO_WAKEUP_RADIO;
               memcpy(&config.startup_timestamp, rxBuffer + 2, sizeof(config.startup_timestamp));
               if (!module_configure(app_mode, &config) || !module_start())
                  host_interface_notify_stopped();
               break;
            }
            case APP_CALIBRATION:
            {
               calibration_config_t config;
               config.index = rxBuffer[2];
               if (module_configure(app_mode, &config))
                  module_start();
               break;
            }
            default:
            {
               debug_msg("FATAL: Unknown application type code: ");
               debug_msg_uint(app_mode);
               debug_msg("!\n");
               break;
            }
         }
         break;
      }

      /**********************************************************************/
      // Put the module to sleep
      /**********************************************************************/
      case HOST_CMD_STOP:
         debug_msg("INFO: Received command from host: HOST_CMD_STOP\n");
         module_reset();
         break;

      /**********************************************************************/
      // Set epoch timestamp
      /**********************************************************************/
      case HOST_CMD_SET_TIME:
         debug_msg("INFO: Received command from host: HOST_CMD_SET_TIME\n");
         scheduler_set_timestamp((rxBuffer[1] << 3 * 8) + (rxBuffer[2] << 2 * 8) + (rxBuffer[3] << 1 * 8) + rxBuffer[4]);
         module_start();
         break;

      /**********************************************************************/
      // STM and DW1000 have been woken up by nRF
      /**********************************************************************/
      case HOST_CMD_WAKEUP:
         debug_msg("INFO: Received command from host: HOST_CMD_WAKEUP\n");
         if (!dw1000_wakeup() && !dw1000_force_wakeup())
         {
            debug_msg("ERROR: Unable to wake up the DW1000 radio!\n");
            module_reset();
         }
         break;

      /**********************************************************************/
      // nRF responded to our notification message
      /**********************************************************************/
      case HOST_CMD_ACK:
         debug_msg("INFO: Received command from host: HOST_CMD_ACK\n");
         module_set_stopped();
         reset_watchdog();
         break;

      default:
         debug_msg("INFO: Received command from host (0x");
         debug_msg_hex(rxBuffer[0]);
         debug_msg("): UNKNOWN\n");
         break;
   }
}

void host_interface_error_occurred(void)
{
   // Reset the I2C communications subsystem
   debug_msg("ERROR: Problem occurred communicating with the host module\n");
   host_interface_init();
}

// Manages the End of Rx transfer event
void CPAL_I2C_RXTC_UserCallback(CPAL_InitTypeDef *pDevInitStruct)
{
   // Handle the incoming host message
   switch (rxBuffer[0])
   {
      /**********************************************************************/
      // Return the INFO array
      /**********************************************************************/
      case HOST_CMD_INFO:
      {
         // Return whether the module hardware is fully initialized
         atomic_set(&tx_needs_ack);
         txStructure.pbBuffer = module_ready() ? INFO_PKT : NULL_PKT;
         txStructure.wNumData = sizeof(INFO_PKT);
         CPAL_I2C_Write(&I2C1_DevStructure);
         full_eui[7] = rxBuffer[6];
         full_eui[6] = rxBuffer[5];
         full_eui[3] = rxBuffer[4];
         full_eui[2] = rxBuffer[3];
         full_eui[1] = rxBuffer[2];
         full_eui[0] = rxBuffer[1];
         dw1000_update_runtime_eui(full_eui);
         break;
      }

      /**********************************************************************/
      // Transmit the pending packet length to the host
      /**********************************************************************/
      case HOST_CMD_READ_PACKET_LENGTH:
      {
         // Send packet length to the host
         atomic_set(&tx_needs_ack);
         txStructure.pbBuffer = txBuffer;
         txStructure.wNumData = 1;
         CPAL_I2C_Write(&I2C1_DevStructure);
         break;
      }

      /**********************************************************************/
      // Transmit the rest of the packet to the host
      /**********************************************************************/
      case HOST_CMD_READ_PACKET:
      {
         // Send packet to the host and clear interrupt
         atomic_set(&tx_needs_ack);
         txStructure.pbBuffer = txBuffer + 1;
         txStructure.wNumData = txBuffer[0];
         CPAL_I2C_Write(&I2C1_DevStructure);
         break;
      }

      /**********************************************************************/
      // Respond with the stored calibration values
      /**********************************************************************/
      case HOST_CMD_READ_CALIBRATION:
      {
         // Copy the raw values from the stored array
         atomic_set(&tx_needs_ack);
         memcpy(txBuffer, dw1000_get_txrx_delay_raw(), 12);
         txStructure.pbBuffer = txBuffer;
         txStructure.wNumData = 12;
         CPAL_I2C_Write(&I2C1_DevStructure);
         break;
      }

      /**********************************************************************/
      // All of the following do not require a response and can be handled on the main thread
      /**********************************************************************/
      case HOST_CMD_START:
      case HOST_CMD_STOP:
      case HOST_CMD_SET_TIME:
      case HOST_CMD_WAKEUP:
      case HOST_CMD_ACK:
      default:
         mark_interrupt(INTERRUPT_I2C_RX);
         break;
   }
}

// Manages the End of Tx transfer event
void CPAL_I2C_TXTC_UserCallback(CPAL_InitTypeDef *pDevInitStruct)
{
   atomic_clear(&tx_needs_ack);
   rxStructure.wNumData = sizeof(rxBuffer);
   rxStructure.pbBuffer = rxBuffer;
   CPAL_I2C_Read(&I2C1_DevStructure);
}

// User callback that manages the I2C device errors
void CPAL_I2C_ERR_UserCallback(CPAL_DevTypeDef pDevInitStruct, uint32_t DeviceError)
{
   mark_interrupt(INTERRUPT_I2C_ERROR);
}

// User callback that manages the Timeout error
uint32_t CPAL_TIMEOUT_UserCallback(CPAL_InitTypeDef *pDevInitStruct)
{
   mark_interrupt(INTERRUPT_I2C_ERROR);
   return CPAL_PASS;
}
