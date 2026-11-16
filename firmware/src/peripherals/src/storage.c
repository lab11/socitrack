// TotTag implementation of the nandlog porting interface

#include <stdio.h>
#include "logging.h"
#include "nandlog_port.h"
#include "system.h"

static void *spi_handle;
static SemaphoreHandle_t log_mutex;
static StaticSemaphore_t log_mutex_buffer;


// Hardware CRC-32 -----------------------------------------------------------------------------------------------------
//
// With NANDLOG_PORT_CRC32 (the Makefiles set it) every CRC nandlog writes or checks comes through nandlog_port_crc32().
// The Apollo4's security block computes CRC-32 in hardware (the HAL's am_hal_crc32()): it reads LEN bytes, a multiple
// of four, from SRCADDR, and goes on from the seed in RESULT. Measured on the Apollo4 Plus (tests/test_nandlog_crcdiag.c)
// and documented nowhere: RESULT reads as the finished CRC, the zlib value, but a seed written to it is taken
// bit-reversed. So a run that continues the zlib value z is seeded with __RBIT(~z); the HAL's 0xFFFFFFFF, which bit
// reversal leaves alone, is z = 0. A wrong CRC goes to the flash as it is and makes its page read as damaged from then
// on, so the engine does nothing for the log until nandlog_port_init() has checked it against the library's software
// on known vectors, and a disagreement -- then or later -- leaves it to the software for good. The engine is the BLE controller's too (am_devices_cooper.c checks its
// boot loader's messages with it), so a run holds the scheduler, leaves alone an engine another task has running, and
// puts back the registers it borrowed

#if NANDLOG_PORT_CRC32

#define HW_CRC_MIN_BYTES                            64          // shorter runs cost the software less than the engine's setup
#define HW_CRC_POLL_LIMIT                           1000000     // ENABLE polls before a run is given up as hung

typedef enum { HW_CRC_UNTESTED, HW_CRC_IN_USE, HW_CRC_REFUSED } hw_crc_state_t;
typedef enum { ENGINE_DONE, ENGINE_BUSY, ENGINE_FAILED } engine_run_t;

extern void (* const g_am_pfnVectors[])(void);

static hw_crc_state_t hw_crc_state = HW_CRC_UNTESTED;
static uint32_t engine_misses;              // runs the engine was asked for and did not do

static void refuse_engine(const char *why)
{
   hw_crc_state = HW_CRC_REFUSED;
   nandlog_port_log("WARNING: The CRC-32 engine %s; nandlog's CRCs are computed in software from now on\n", why);
}

static engine_run_t engine_run(uint32_t *crc, const uint8_t *words, uint32_t length)
{
   // One run of the engine over 'length' bytes at 'words', both multiples of four, going on from the zlib-style value
   // '*crc', which it replaces only if the run completed
   const bool scheduling = (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING);
   if (scheduling)
      vTaskSuspendAll();
   engine_run_t run = ENGINE_BUSY;
   if (!SECURITY->CTRL_b.ENABLE)
   {
      const uint32_t result_was = SECURITY->RESULT, address_was = SECURITY->SRCADDR, length_was = SECURITY->LEN;
      const uint32_t function_was = SECURITY->CTRL_b.FUNCTION;
      SECURITY->RESULT = __RBIT(~*crc);        // the seed, which the engine takes bit-reversed
      SECURITY->SRCADDR = (uint32_t)words;
      SECURITY->LEN = length;
      SECURITY->CTRL_b.FUNCTION = SECURITY_CTRL_FUNCTION_CRC32;
      if (((uint32_t)words + length) >= (SRAM_BASEADDR + TCM_MAX_SIZE))
         am_hal_sysctrl_bus_write_flush();      // as the HAL does: writes outside the TCM must land before the engine reads
      SECURITY->CTRL_b.ENABLE = 1;
      uint32_t polls = 0;
      while (SECURITY->CTRL_b.ENABLE && (++polls < HW_CRC_POLL_LIMIT));
      if (SECURITY->CTRL_b.ENABLE)
         run = ENGINE_FAILED;                   // still running: its registers stay as they are
      else
      {
         run = SECURITY->CTRL_b.CRCERROR ? ENGINE_FAILED : ENGINE_DONE;
         if (run == ENGINE_DONE)
            *crc = SECURITY->RESULT;            // the finished CRC
         SECURITY->RESULT = __RBIT(~result_was); // RESULT reads as it did before
         SECURITY->SRCADDR = address_was;
         SECURITY->LEN = length_was;
         SECURITY->CTRL_b.FUNCTION = function_was;
      }
   }
   if (scheduling)
      xTaskResumeAll();
   return run;
}

static uint32_t crc_by_engine(uint32_t crc, const uint8_t *bytes, uint32_t length)
{
   // The software to the first word boundary, the engine for the whole words after it, and the software for the rest.
   // A run the engine does not do goes to the software too; one it starts and does not finish refuses it for good
   uint32_t head = (uint32_t)(-(uintptr_t)bytes & 3);
   if (head > length)
      head = length;
   crc = nandlog_crc32_software(crc, bytes, head);
   bytes += head;
   length -= head;
   const uint32_t words = length & ~3u;
   if (words)
   {
      const engine_run_t run = engine_run(&crc, bytes, words);
      if (run != ENGINE_DONE)
      {
         ++engine_misses;
         if ((run == ENGINE_FAILED) && (hw_crc_state == HW_CRC_IN_USE))
            refuse_engine("did not complete a run");
         crc = nandlog_crc32_software(crc, bytes, words);
      }
   }
   return nandlog_crc32_software(crc, bytes + words, length - words);
}

static const char *engine_disagreement(void)
{
   // The engine's CRC of "12345678" from the HAL's seed; then the engine's whole path, the software's head and tail
   // around it, against the software alone: "123456789" at every alignment, 4,064 bytes of the program image in
   // MRAM whole and continued from a split at an odd offset, and runs in RAM at every alignment, whole and split.
   // Every one of them must have been the engine's. NULL if all agree, else what disagreed
   static const uint8_t check[] = "123456789";
   const uint8_t *image = (const uint8_t*)g_am_pfnVectors;
   uint32_t ram[68], value = 0;
   memcpy(ram, check, 8);
   const engine_run_t first = engine_run(&value, (const uint8_t*)ram, 8);
   if (first == ENGINE_BUSY)
      return "was running for another driver when it was to be checked";
   if (first != ENGINE_DONE)
      return "did not complete its first run";
   if (value != 0x9AE0DAAFu)
      return "gives the wrong CRC of \"12345678\"";
   const uint32_t misses = engine_misses;
   for (uint32_t align = 0; align < 4; ++align)
   {
      memcpy((uint8_t*)ram + align, check, 9);
      if (crc_by_engine(0, (const uint8_t*)ram + align, 9) != 0xCBF43926u)
         return "disagrees on \"123456789\"";
   }
   const uint32_t whole = nandlog_crc32_software(0, image, 4064);
   if ((crc_by_engine(0, image, 4064) != whole) || (crc_by_engine(crc_by_engine(0, image, 1001), image + 1001, 4064 - 1001) != whole))
      return "disagrees on 4,064 bytes of MRAM";
   memcpy(ram, image + 512, sizeof(ram));
   for (uint32_t align = 0; align < 4; ++align)
   {
      const uint8_t *bytes = (const uint8_t*)ram + align;
      const uint32_t length = sizeof(ram) - 4, ram_whole = nandlog_crc32_software(0, bytes, length);
      if ((crc_by_engine(0, bytes, length) != ram_whole) || (crc_by_engine(crc_by_engine(0, bytes, 101), bytes + 101, length - 101) != ram_whole))
         return "disagrees on a run in RAM";
   }
   return (engine_misses == misses) ? NULL : "left runs to the software during its check";
}

uint32_t nandlog_port_crc32(uint32_t crc, const void *data, uint32_t length)
{
   if ((hw_crc_state != HW_CRC_IN_USE) || (length < HW_CRC_MIN_BYTES) || xPortIsInsideInterrupt())
      return nandlog_crc32_software(crc, data, length);
   return crc_by_engine(crc, (const uint8_t*)data, length);
}

bool nandlog_port_crc32_hardware(void)
{
   return hw_crc_state == HW_CRC_IN_USE;
}

#endif  // #if NANDLOG_PORT_CRC32

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

#if NANDLOG_PORT_CRC32
   // Once a boot, before the log computes its first CRC: the engine is used only if it agrees on every vector
   if (hw_crc_state == HW_CRC_UNTESTED)
   {
      const char *disagreement = engine_disagreement();
      if (disagreement)
         refuse_engine(disagreement);
      else
      {
         hw_crc_state = HW_CRC_IN_USE;
         nandlog_port_log("INFO: The CRC-32 engine agrees with the software; nandlog's CRCs of %u bytes or more use it\n",
                          (uint32_t)HW_CRC_MIN_BYTES);
      }
   }
#endif
   return true;
}

void nandlog_port_deinit(void)
{
   am_hal_iom_disable(spi_handle);
   am_hal_iom_uninitialize(spi_handle);
   spi_handle = NULL;
}

void nandlog_port_transfer_read(uint8_t command, const void *address, uint32_t address_length, void *read_buffer, uint32_t read_length)
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

void nandlog_port_transfer_write(uint8_t command, const void *address, uint32_t address_length, const void *write_buffer, uint32_t write_length)
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
   // The write-protect pin is asserted high to PERMIT programming, matching the original storage driver's usage
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
   while (true);   // nandlog requires this never to return
}

void nandlog_port_unwritable(void)
{
   // Not a reason to reset: the device can still range and still serve BLE.
   // Leave a breadcrumb so that the next boot's log says the flash has already given up
   system_record_diagnostic(RESET_DIAGNOSTIC_STORAGE_UNWRITABLE);
}
