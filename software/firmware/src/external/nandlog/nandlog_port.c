// Ambiq Apollo4 implementation of the nandlog porting interface

#include <stdio.h>
#include "logging.h"
#include "nandlog_port.h"
#include "system.h"

static void *spi_handle;
static SemaphoreHandle_t log_mutex;
static StaticSemaphore_t log_mutex_buffer;

static inline bool locking_applies(void)
{
   // Whether locking applies at all
   // Before the scheduler runs there is exactly one context, so there is nothing to serialize against
   return (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) && !xPortIsInsideInterrupt();
}

bool nandlog_port_init(void)
{
   const am_hal_iom_config_t spi_config =
   {
      .eInterfaceMode = AM_HAL_IOM_SPI_MODE,
      .ui32ClockFreq = AM_HAL_IOM_48MHZ,
      .eSpiMode = AM_HAL_IOM_SPI_MODE_0,
      .pNBTxnBuf = NULL,
      .ui32NBTxnBufLength = 0
   };

   // Configure and assert the Write-Protect and Hold pins to disable them
   configASSERT0(am_hal_gpio_pinconfig(PIN_STORAGE_WRITE_PROTECT, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
   configASSERT0(am_hal_gpio_pinconfig(PIN_STORAGE_HOLD, am_hal_gpio_pincfg_output));
   am_hal_gpio_output_set(PIN_STORAGE_HOLD);

   // Initialize the SPI module and enable all relevant SPI pins
   am_hal_gpio_pincfg_t sck_config = g_AM_BSP_GPIO_IOM0_SCK;
   am_hal_gpio_pincfg_t miso_config = g_AM_BSP_GPIO_IOM0_MISO;
   am_hal_gpio_pincfg_t mosi_config = g_AM_BSP_GPIO_IOM0_MOSI;
   am_hal_gpio_pincfg_t cs_config = g_AM_BSP_GPIO_IOM0_CS;
   sck_config.GP.cfg_b.uFuncSel = PIN_STORAGE_SPI_SCK_FUNCTION;
   miso_config.GP.cfg_b.uFuncSel = PIN_STORAGE_SPI_MISO_FUNCTION;
   mosi_config.GP.cfg_b.uFuncSel = PIN_STORAGE_SPI_MOSI_FUNCTION;
   cs_config.GP.cfg_b.uFuncSel = PIN_STORAGE_SPI_CS_FUNCTION;
   cs_config.GP.cfg_b.uNCE = 4 * STORAGE_SPI_NUMBER;
   configASSERT0(am_hal_iom_initialize(STORAGE_SPI_NUMBER, &spi_handle));
   configASSERT0(am_hal_gpio_pinconfig(PIN_STORAGE_SPI_SCK, sck_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_STORAGE_SPI_MISO, miso_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_STORAGE_SPI_MOSI, mosi_config));
   configASSERT0(am_hal_gpio_pinconfig(PIN_STORAGE_SPI_CS, cs_config));
   configASSERT0(am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, false));
   configASSERT0(am_hal_iom_configure(spi_handle, &spi_config));
   configASSERT0(am_hal_iom_enable(spi_handle));
   return true;
}

void nandlog_port_deinit(void)
{
   am_hal_iom_disable(spi_handle);
   am_hal_iom_uninitialize(spi_handle);
   spi_handle = NULL;
}

void nandlog_port_spi_read(uint8_t command, const void *address, uint32_t address_length, void *read_buffer, uint32_t read_length)
{
   // Create the SPI transaction structure
   uint32_t instruction = command, retries_remaining = 4;
   uint32_t num_reads = 1 + (read_length / (1 + AM_HAL_IOM_MAX_TXNSIZE_SPI));
   memcpy(((uint8_t*)&instruction) + 1, address, address_length);
   am_hal_iom_transfer_t spi_transaction = {
      .uPeerInfo.ui32SpiChipSelect  = 0,
      .ui32InstrLen                 = 0,
      .ui64Instr                    = 0,
      .eDirection                   = AM_HAL_IOM_TX,
      .ui32NumBytes                 = 1 + address_length,
      .pui32TxBuffer                = &instruction,
      .pui32RxBuffer                = NULL,
      .bContinue                    = true,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };

   // Repeat the transfer until it succeeds or requires a device reset
   while (--retries_remaining && (am_hal_iom_blocking_transfer(spi_handle, &spi_transaction) != AM_HAL_STATUS_SUCCESS))
      am_hal_delay_us(10);
   if (!retries_remaining)
      system_reset(true);

   // Split the read across as many transactions as the peripheral's maximum requires. The count is fixed
   // before the loop rather than tested against the remaining length, so a zero-length read still issues
   // the single empty transaction that the smaller-page parts have always relied on
   uint32_t read_offset = 0;
   while (num_reads--)
   {
      // Determine the actual read size for this transaction
      uint32_t read_bytes = (read_length > AM_HAL_IOM_MAX_TXNSIZE_SPI) ? AM_HAL_IOM_MAX_TXNSIZE_SPI : read_length;
      read_length -= read_bytes;

      // Update the SPI transaction structure
      retries_remaining = 4;
      spi_transaction.eDirection = AM_HAL_IOM_RX;
      spi_transaction.ui32NumBytes = read_bytes;
      spi_transaction.pui32TxBuffer = NULL,
      spi_transaction.pui32RxBuffer = (uint32_t*)((uint8_t*)read_buffer + read_offset);
      spi_transaction.bContinue = read_length > 0;
      read_offset += read_bytes;

      // Repeat the transfer until it succeeds or requires a device reset
      while (--retries_remaining && (am_hal_iom_blocking_transfer(spi_handle, &spi_transaction) != AM_HAL_STATUS_SUCCESS))
         am_hal_delay_us(10);
      if (!retries_remaining)
         system_reset(true);
   }
}

void nandlog_port_lock(void)
{
   // Only continue if locking applies here
   if (locking_applies())
   {
      // Created on first use, which is guaranteed to be after the scheduler has started. The double check
      // under a critical section makes the creation itself safe against two tasks arriving together
      if (!log_mutex)
      {
         taskENTER_CRITICAL();
         if (!log_mutex)
            log_mutex = xSemaphoreCreateMutexStatic(&log_mutex_buffer);
         taskEXIT_CRITICAL();
      }
      if (log_mutex)
         xSemaphoreTake(log_mutex, portMAX_DELAY);
   }
}

void nandlog_port_unlock(void)
{
   if (log_mutex && locking_applies())
      xSemaphoreGive(log_mutex);
}

void nandlog_port_spi_write(uint8_t command, const void *address, uint32_t address_length, const void *write_buffer, uint32_t write_length)
{
   // Create the SPI transaction structure
   uint32_t instruction = command, retries_remaining = 4;
   uint32_t num_writes = 1 + (write_length / (1 + AM_HAL_IOM_MAX_TXNSIZE_SPI));
   memcpy(((uint8_t*)&instruction) + 1, address, address_length);
   am_hal_iom_transfer_t spi_transaction = {
      .uPeerInfo.ui32SpiChipSelect  = 0,
      .ui32InstrLen                 = 0,
      .ui64Instr                    = 0,
      .eDirection                   = AM_HAL_IOM_TX,
      .ui32NumBytes                 = 1 + address_length,
      .pui32TxBuffer                = &instruction,
      .pui32RxBuffer                = NULL,
      .bContinue                    = true,
      .ui8RepeatCount               = 0,
      .ui8Priority                  = 1,
      .ui32PauseCondition           = 0,
      .ui32StatusSetClr             = 0
   };

   // Repeat the transfer until it succeeds or requires a device reset
   while (--retries_remaining && (am_hal_iom_blocking_transfer(spi_handle, &spi_transaction) != AM_HAL_STATUS_SUCCESS))
      am_hal_delay_us(10);
   if (!retries_remaining)
      system_reset(true);

   // Split the write the same way, for the same reason
   uint32_t write_offset = 0;
   while (num_writes--)
   {
      // Determine the actual write size for this transaction
      uint32_t write_bytes = (write_length > AM_HAL_IOM_MAX_TXNSIZE_SPI) ? AM_HAL_IOM_MAX_TXNSIZE_SPI : write_length;
      write_length -= write_bytes;

      // Update the SPI transaction structure
      retries_remaining = 4;
      spi_transaction.ui32NumBytes = write_bytes;
      spi_transaction.pui32TxBuffer = (uint32_t*)((uint8_t*)write_buffer + write_offset);
      spi_transaction.bContinue = write_length > 0;
      write_offset += write_bytes;

      // Repeat the transfer until it succeeds or requires a device reset
      while (--retries_remaining && (am_hal_iom_blocking_transfer(spi_handle, &spi_transaction) != AM_HAL_STATUS_SUCCESS))
         am_hal_delay_us(10);
      if (!retries_remaining)
         system_reset(true);
   }
}

void nandlog_port_write_enable(bool enable)
{
   // The write-protect pin is asserted high to PERMIT programming, matching the original storage.c usage
   if (enable)
      am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
   else
      am_hal_gpio_output_clear(PIN_STORAGE_WRITE_PROTECT);
}

void nandlog_port_power(bool awake)
{
   am_hal_iom_power_ctrl(spi_handle, awake ? AM_HAL_SYSCTRL_WAKE : AM_HAL_SYSCTRL_DEEPSLEEP, true);
}

void nandlog_port_log(const char *format, ...)
{
   char message[160];
   va_list args;
   va_start(args, format);
   vsnprintf(message, sizeof(message), format, args);
   va_end(args);
   print("%s", message);
}

void nandlog_port_delay_us(uint32_t microseconds) { am_hal_delay_us(microseconds); }
void nandlog_port_delay_ms(uint32_t milliseconds) { am_util_delay_ms(milliseconds); }

void nandlog_port_fatal(const char *reason)
{
   print("ERROR: Storage hardware fault: %s; resetting\n", reason);
   system_record_diagnostic(RESET_DIAGNOSTIC_STORAGE_FATAL);
   system_reset(true);
}

void nandlog_port_unwritable(void)
{
   // Not a reason to reset: the device can still range and still serve BLE.
   // Leave a breadcrumb so that the next boot's log says the flash has already given up
   system_record_diagnostic(RESET_DIAGNOSTIC_STORAGE_UNWRITABLE);
}
