// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "buzzer.h"
#include "logging.h"
#include "rtc.h"
#include "storage.h"
#include "system.h"


// Chip-Specific Definitions -------------------------------------------------------------------------------------------

#if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)

#define COMMAND_READ_DEVICE_ID                      0x9F
#define COMMAND_DEVICE_RESET                        0xFF
#define COMMAND_READ_STATUS_REGISTER                0x0F
#define COMMAND_WRITE_STATUS_REGISTER               0x1F
#define COMMAND_WRITE_ENABLE                        0x06
#define COMMAND_WRITE_DISABLE                       0x04
#define COMMAND_BLOCK_ERASE                         0xD8
#define COMMAND_PROGRAM_DATA_LOAD                   0x02
#define COMMAND_PROGRAM_EXECUTE                     0x10
#define COMMAND_PAGE_DATA_READ                      0x13
#define COMMAND_READ                                0x03

#if REVISION_ID < REVISION_N
#define COMMAND_WRITE_BBM_LUT                       0xA1
#define COMMAND_READ_BBM_LUT                        0xA5
#endif

#define STATUS_REGISTER_1                           0xA0
#define STATUS_REGISTER_2                           0xB0
#define STATUS_REGISTER_3                           0xC0
#if REVISION_ID < REVISION_N
#define OTP_BASE_ADDRESS                            0x02
#else
#define OTP_BASE_ADDRESS                            0x03
#endif
#define FIRST_BOOT_ADDRESS                          OTP_BASE_ADDRESS

#define STATUS_LUT_FULL                             0b01000000
#define STATUS_PAGE_FATAL_ERROR                     0b00100000
#define STATUS_PAGE_NONFATAL_ERROR                  0b00010000
#define STATUS_WRITE_FAILURE                        0b00001000
#define STATUS_ERASE_FAILURE                        0b00000100
#define STATUS_BUSY                                 0b00000001

#if REVISION_ID < REVISION_N
#define BBM_LUT_NUM_ENTRIES                         20
#define BBM_NUM_RESERVED_BLOCKS                     40
#define BBM_TABLE_SIZE                              BBM_LUT_NUM_ENTRIES
#else
#define BBM_NUM_RESERVED_BLOCKS                     80
#define BBM_TABLE_SIZE                              256
#endif
#define BBM_LUT_BASE_ADDRESS                        ((MEMORY_BLOCK_COUNT - BBM_NUM_RESERVED_BLOCKS) * MEMORY_PAGES_PER_BLOCK)
#define MEMORY_MAX_PAGE_ADDRESS                     (MEMORY_BLOCK_COUNT * MEMORY_PAGES_PER_BLOCK)


// Helper Structures ---------------------------------------------------------------------------------------------------

#if REVISION_ID < REVISION_N
typedef struct __attribute__ ((__packed__)) { uint16_t lba, pba; } bbm_lut_t;
#else
typedef uint32_t bbm_lut_t;
static volatile uint32_t bbm_index, bbm_storage_page;
#endif


// Static Global Variables ---------------------------------------------------------------------------------------------

static void *spi_handle;
static bbm_lut_t bad_block_lookup_table[BBM_TABLE_SIZE];
static uint8_t cache[2 * MEMORY_PAGE_SIZE_BYTES], transfer_buffer[MEMORY_PAGE_SIZE_BYTES + MEMORY_ECC_BYTES_PER_PAGE];
static volatile uint32_t starting_page, current_page, reading_page, last_reading_page, cache_index, log_data_size;
static volatile bool is_reading, in_maintenance_mode, disabled;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void spi_read(uint8_t command, const void *address, uint32_t address_length, void *read_buffer, uint32_t read_length)
{
   // Create the SPI transaction structure
   uint32_t instruction = command, retries_remaining = 4;
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

   // Split SPI reads if necessary
   uint32_t read_offset = 0;
   while (read_length)
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

static void spi_write(uint8_t command, const void *address, uint32_t address_length, const void *write_buffer, uint32_t write_length)
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

   // Split SPI writes if necessary
   uint32_t write_offset = 0;
   while (num_writes--)
   {
      // Determine the actual read size for this transaction
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

static uint8_t read_register(uint8_t register_number)
{
   static uint8_t register_value;
   spi_read(COMMAND_READ_STATUS_REGISTER, &register_number, 1, &register_value, 1);
   return register_value;
}

static void write_register(uint8_t register_number, uint8_t value)
{
   spi_write(COMMAND_WRITE_STATUS_REGISTER, &register_number, 1, &value, 1);
}

static bool verify_device_id(void)
{
#if REVISION_ID < REVISION_N
   uint8_t device_id_read[4], device_id_known[3] = STORAGE_DEVICE_ID;
   spi_read(COMMAND_READ_DEVICE_ID, NULL, 0, device_id_read, sizeof(device_id_read));
   return (memcmp(device_id_read + 1, device_id_known, sizeof(device_id_known)) == 0);
#else
   static const uint8_t address = 0x01;
   uint8_t device_id_read[1], device_id_known[1] = STORAGE_DEVICE_ID;
   spi_read(COMMAND_READ_DEVICE_ID, &address, sizeof(address), device_id_read, sizeof(device_id_read));
   return (memcmp(device_id_read, device_id_known, sizeof(device_id_known)) == 0);
#endif
}

static void wait_until_not_busy(void)
{
   while ((read_register(STATUS_REGISTER_3) & STATUS_BUSY) == STATUS_BUSY)
      am_hal_delay_us(10);
}

static void enter_low_power_mode(void)
{
   // Placeholder in case a future version has a low-power mode
}

static void exit_low_power_mode(void)
{
   // Placeholder in case a future version has a low-power mode
}

static bool write_page_raw(const uint8_t *data, uint32_t page_number)
{
   const uint16_t byte_offset = 0;
   const uint8_t page_number_reordered[] = { (uint8_t)((page_number & 0x00FF0000) >> 16), (uint8_t)((page_number & 0x0000FF00) >> 8), (uint8_t)(page_number & 0x000000FF) };
   for (uint8_t retry_index = 0; retry_index < MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL; ++retry_index)
   {
      wait_until_not_busy();
      spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      spi_write(COMMAND_PROGRAM_DATA_LOAD, &byte_offset, 2, data, MEMORY_PAGE_SIZE_BYTES);
      wait_until_not_busy();
      spi_write(COMMAND_PROGRAM_EXECUTE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
      wait_until_not_busy();
      if ((read_register(STATUS_REGISTER_3) & STATUS_WRITE_FAILURE) != STATUS_WRITE_FAILURE)
         return true;
   }
   return false;
}

static bool read_page(uint8_t *buffer, uint32_t page_number)
{
   const uint32_t byte_offset = 0;
   const uint8_t page_number_reordered[] = { (uint8_t)((page_number & 0x00FF0000) >> 16), (uint8_t)((page_number & 0x0000FF00) >> 8), (uint8_t)(page_number & 0x000000FF) };
   wait_until_not_busy();
   spi_write(COMMAND_PAGE_DATA_READ, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
   wait_until_not_busy();
   spi_read(COMMAND_READ, &byte_offset, 3, buffer, MEMORY_PAGE_SIZE_BYTES);
   wait_until_not_busy();
   return (read_register(STATUS_REGISTER_3) & STATUS_PAGE_FATAL_ERROR) != STATUS_PAGE_FATAL_ERROR;
}

#if REVISION_ID > REVISION_M
static bool read_page_with_spare_data(uint8_t *buffer, uint32_t page_number)
{
   const uint32_t byte_offset = 0;
   const uint8_t page_number_reordered[] = { (uint8_t)((page_number & 0x00FF0000) >> 16), (uint8_t)((page_number & 0x0000FF00) >> 8), (uint8_t)(page_number & 0x000000FF) };
   wait_until_not_busy();
   spi_write(COMMAND_PAGE_DATA_READ, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
   wait_until_not_busy();
   spi_read(COMMAND_READ, &byte_offset, 3, buffer, MEMORY_PAGE_SIZE_BYTES + MEMORY_ECC_BYTES_PER_PAGE);
   wait_until_not_busy();
   return (read_register(STATUS_REGISTER_3) & STATUS_PAGE_FATAL_ERROR) != STATUS_PAGE_FATAL_ERROR;
}
#endif

static bool transfer_block(uint32_t source, uint32_t destination, uint32_t num_pages)
{
   for (uint32_t i = 0, page = source; i < num_pages; ++i, ++page, ++destination)
   {
      if (!read_page(transfer_buffer, page))
         memset(transfer_buffer, 0xFF, MEMORY_PAGE_SIZE_BYTES);
      if (!write_page_raw(transfer_buffer, destination))
         return false;
   }
   return true;
}

#if REVISION_ID < REVISION_N

static bool is_bad_block(uint32_t block_address)
{
   // Search for the block address in the bad block lookup table
   block_address = (block_address & 0x0000FFC0) >> 6;
   for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
      if (bad_block_lookup_table[i].lba == (uint16_t)block_address)
         return true;
   return false;
}

static void add_bad_block(uint32_t block_address)
{
   // Find first available workaround block
   uint16_t workaround_block = 0;
   for (uint32_t page = BBM_LUT_BASE_ADDRESS; !workaround_block && (page < MEMORY_PAGE_COUNT); page += MEMORY_PAGES_PER_BLOCK)
      if (read_page(transfer_buffer, page) && (transfer_buffer[0] == 0xFF))
      {
         // Ensure that the candidate block is not already in use
         workaround_block = (uint16_t)((page & 0x0000FFC0) >> 6);
         for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
            if (bad_block_lookup_table[i].pba == workaround_block)
            {
               workaround_block = 0;
               break;
            }
      }

   // Update LUT with the workaround block
   if (workaround_block)
   {
      block_address = (block_address & 0x0000FFC0) >> 6;
      bbm_lut_t destination_address = {
         .lba = (uint16_t)(((block_address << 8) & 0xFF00) | ((block_address >> 8) & 0x00FF)),
         .pba = ((workaround_block << 8) & 0xFF00) | ((workaround_block >> 8) & 0x00FF)
      };
      spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      spi_write(COMMAND_WRITE_BBM_LUT, NULL, 0, &destination_address, sizeof(destination_address));
      wait_until_not_busy();

      // Update the bad block lookup table
      for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
         if ((bad_block_lookup_table[i].pba == 0) && (bad_block_lookup_table[i].lba == 0))
         {
            bad_block_lookup_table[i].lba = (uint16_t)block_address;
            bad_block_lookup_table[i].pba = workaround_block;
            break;
         }
   }
}

#else

static bool is_bad_block(uint32_t block_address)
{
   // Search for the block address in the bad block lookup table
   block_address &= 0xFFFFFFC0;
   for (uint32_t i = 0; i < bbm_index; ++i)
      if (bad_block_lookup_table[i] == block_address)
         return true;
   return false;
}

static void add_bad_block(uint32_t block_address)
{
   // Disable memory page write protection
   am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
   write_register(STATUS_REGISTER_1, 0b00000010);

   // Erase the BBM LUT page and ensure that the command was successful
   bool success = false;
   while (!success)
   {
      const uint8_t page_number_reordered[] = {
         (uint8_t)((bbm_storage_page & 0x00FF0000) >> 16),
         (uint8_t)((bbm_storage_page & 0x0000FF00) >> 8),
         (uint8_t)(bbm_storage_page & 0x000000FF)
      };
      wait_until_not_busy();
      spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      spi_write(COMMAND_BLOCK_ERASE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
      wait_until_not_busy();
      if ((read_register(STATUS_REGISTER_3) & STATUS_ERASE_FAILURE) == STATUS_ERASE_FAILURE)
      {
         uint32_t next_bbm_storage_page = (bbm_storage_page + MEMORY_PAGES_PER_BLOCK) & 0xFFFFFFC0;
         while (is_bad_block(next_bbm_storage_page))
            next_bbm_storage_page = (next_bbm_storage_page + MEMORY_PAGES_PER_BLOCK) & 0xFFFFFFC0;
         if (next_bbm_storage_page < MEMORY_MAX_PAGE_ADDRESS)
            bbm_storage_page = next_bbm_storage_page;
         else
            success = true;
      }
      else
         success = true;
   }

   // Update the LUT with the bad block
   bad_block_lookup_table[bbm_index++] = block_address & 0xFFFFFFC0;
   memset(transfer_buffer, 0, MEMORY_PAGE_SIZE_BYTES);
   memcpy(transfer_buffer, "BBM_", 4);
   memcpy(transfer_buffer + 4, (uint32_t*)&bbm_index, sizeof(bbm_index));
   memcpy(transfer_buffer + 4 + sizeof(bbm_index), bad_block_lookup_table, sizeof(bad_block_lookup_table));
   write_page_raw(transfer_buffer, bbm_storage_page);

   // Re-enable memory page write protection
   write_register(STATUS_REGISTER_1, 0b01111110);
   am_hal_gpio_output_clear(PIN_STORAGE_WRITE_PROTECT);
}

#endif

static void write_page(uint16_t data_length)
{
   // Disable memory page write protection
   if (!in_maintenance_mode)
   {
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, true);
      exit_low_power_mode();
   }
   am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
   write_register(STATUS_REGISTER_1, 0b00000010);
   const uint32_t original_page = current_page;

   // Continue trying to write the current page to memory until successful
   bool success = false;
   while (!success)
   {
      // Fill up transfer buffer with current page data
      memset(transfer_buffer, 0xFF, MEMORY_PAGE_SIZE_BYTES);
      transfer_buffer[0] = 'D';
      transfer_buffer[1] = 'A';
      *(uint16_t*)(transfer_buffer+2) = data_length;
      memcpy(transfer_buffer+4, cache, data_length);

      // Add the current block to the list of bad blocks if unable to write or if read-back contains errors
      if (write_page_raw(transfer_buffer, current_page) && read_page(transfer_buffer, current_page))
         success = true;
      else
      {
         // Transfer any already-written pages in the current block to the next block
         uint32_t next_block = ((current_page + MEMORY_PAGES_PER_BLOCK) & 0xFFFFFFC0) % BBM_LUT_BASE_ADDRESS;
         while (is_bad_block(next_block))
            next_block = ((next_block + MEMORY_PAGES_PER_BLOCK) & 0xFFFFFFC0) % BBM_LUT_BASE_ADDRESS;
         transfer_block(original_page & 0xFFFFFFC0, next_block, current_page & 0x003F);
         add_bad_block(current_page);
         current_page = next_block | (current_page & 0x003F);
      }
   }

   // Re-enable memory page write protection
   write_register(STATUS_REGISTER_1, 0b01111110);
   am_hal_gpio_output_clear(PIN_STORAGE_WRITE_PROTECT);
   if (!in_maintenance_mode)
   {
      enter_low_power_mode();
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
   }
}

static void erase_block(uint32_t starting_page, uint32_t ending_page)
{
   // Disable memory page write protection
   am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
   write_register(STATUS_REGISTER_1, 0b00000010);

   // Iterate through all blocks to be erased
   ending_page &= 0xFFFFFFC0;
   starting_page &= 0xFFFFFFC0;
   const uint8_t num_iterations = (starting_page <= ending_page) ? 1 : 2;
   uint32_t end = (starting_page <= ending_page) ? ending_page : (BBM_LUT_BASE_ADDRESS - 1);
   for (uint8_t i = 0; i < num_iterations; ++i)
   {
      for (uint32_t page = starting_page; page <= end; page += MEMORY_PAGES_PER_BLOCK)
      {
         // Erase the current page and ensure that the command was successful
         const uint8_t page_number_reordered[] = { (uint8_t)((page & 0x00FF0000) >> 16), (uint8_t)((page & 0x0000FF00) >> 8), (uint8_t)(page & 0x000000FF) };
         wait_until_not_busy();
         spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
         spi_write(COMMAND_BLOCK_ERASE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
         wait_until_not_busy();
         if ((read_register(STATUS_REGISTER_3) & STATUS_ERASE_FAILURE) == STATUS_ERASE_FAILURE)
            add_bad_block(page);
      }
      starting_page = 0;
      end = ending_page;
   }

   // Re-enable memory page write protection
   write_register(STATUS_REGISTER_1, 0b01111110);
   am_hal_gpio_output_clear(PIN_STORAGE_WRITE_PROTECT);
}

static bool is_first_boot(void)
{
   bool first_boot = false;
   uint8_t device_id[3] = STORAGE_DEVICE_ID;
#if REVISION_ID < REVISION_N
   write_register(STATUS_REGISTER_2, 0b01011001);
#else
   write_register(STATUS_REGISTER_2, 0b01010000);
#endif
   read_page(transfer_buffer, FIRST_BOOT_ADDRESS);
   if (memcmp(transfer_buffer, device_id, sizeof(device_id)))
   {
      memset(transfer_buffer, 0, MEMORY_PAGE_SIZE_BYTES);
      memcpy(transfer_buffer, device_id, sizeof(device_id));
      write_page_raw(transfer_buffer, FIRST_BOOT_ADDRESS);
      spi_write(COMMAND_WRITE_DISABLE, NULL, 0, NULL, 0);
      first_boot = true;
   }
#if REVISION_ID < REVISION_N
   write_register(STATUS_REGISTER_2, 0b00011001);
#else
   write_register(STATUS_REGISTER_2, 0b00010000);
#endif
   return first_boot;
}


// Public API Functions ------------------------------------------------------------------------------------------------

void storage_init(void)
{
   // Create an SPI configuration structure
   is_reading = in_maintenance_mode = disabled = false;
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

   // Wait until the chip becomes accessible
   while (!verify_device_id())
      am_util_delay_ms(1);
   am_util_delay_ms(3);
   wait_until_not_busy();

   // Configure the memory chip
   const uint8_t status_register_1_bits = 0b01111110;
#if REVISION_ID < REVISION_N
   const uint8_t status_register_2_bits = 0b00011001;
#else
   const uint8_t status_register_2_bits = 0b00010000;
#endif
   write_register(STATUS_REGISTER_1, status_register_1_bits);
   write_register(STATUS_REGISTER_2, status_register_2_bits);

   // Retrieve the list of existing bad storage blocks
#if REVISION_ID < REVISION_N
   uint8_t dummy_value = 0;
   spi_read(COMMAND_READ_BBM_LUT, &dummy_value, 1, &bad_block_lookup_table, sizeof(bad_block_lookup_table));
   for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
   {
      bad_block_lookup_table[i].lba = (((bad_block_lookup_table[i].lba << 8) & 0xFF00) | ((bad_block_lookup_table[i].lba >> 8) & 0x00FF)) & 0x3FF;
      bad_block_lookup_table[i].pba = (((bad_block_lookup_table[i].pba << 8) & 0xFF00) | ((bad_block_lookup_table[i].pba >> 8) & 0x00FF)) & 0x3FF;
   }
#else
   bbm_index = 0;
   memset(bad_block_lookup_table, 0, sizeof(bad_block_lookup_table));
   for (bbm_storage_page = MEMORY_MAX_PAGE_ADDRESS - MEMORY_PAGES_PER_BLOCK; bbm_storage_page >= BBM_LUT_BASE_ADDRESS; bbm_storage_page -= MEMORY_PAGES_PER_BLOCK)
      if (read_page(transfer_buffer, bbm_storage_page) && (memcmp(transfer_buffer, "BBM_", 4) == 0))
      {
         memcpy((uint32_t*)&bbm_index, transfer_buffer + 4, sizeof(bbm_index));
         memcpy(bad_block_lookup_table, transfer_buffer + 4 + sizeof(bbm_index), sizeof(bad_block_lookup_table));
         break;
      }
#endif

   // Check for bad storage blocks if this is the first boot
   if (is_first_boot())
   {
      write_register(STATUS_REGISTER_1, 0b00000010);
#if REVISION_ID < REVISION_N
      for (uint32_t page = 0; page < BBM_LUT_BASE_ADDRESS; page += MEMORY_PAGES_PER_BLOCK)
         if (!read_page(transfer_buffer, page) || (transfer_buffer[0] != 0xFF))
            add_bad_block(page);
#else
      for (uint32_t page = 0; page < MEMORY_MAX_PAGE_ADDRESS; page += MEMORY_PAGES_PER_BLOCK)
         if ((!read_page_with_spare_data(transfer_buffer, page) || (transfer_buffer[MEMORY_PAGE_SIZE_BYTES] != 0xFF)) && !is_bad_block(page))
            bad_block_lookup_table[bbm_index++] = page;
      bbm_storage_page = BBM_LUT_BASE_ADDRESS;
      while (is_bad_block(bbm_storage_page))
         bbm_storage_page = (bbm_storage_page + MEMORY_PAGES_PER_BLOCK) & 0xFFFFFFC0;
      memset(transfer_buffer, 0, MEMORY_PAGE_SIZE_BYTES);
      memcpy(transfer_buffer, "BBM_", 4);
      memcpy(transfer_buffer + 4, (uint32_t*)&bbm_index, sizeof(bbm_index));
      memcpy(transfer_buffer + 4 + sizeof(bbm_index), bad_block_lookup_table, sizeof(bad_block_lookup_table));
      write_page_raw(transfer_buffer, bbm_storage_page);
#endif
      write_register(STATUS_REGISTER_1, 0b01111110);
   }

   // Search for the starting page
   int32_t start_page = -1;
   cache_index = last_reading_page = log_data_size = 0;
   memset(cache, 0, sizeof(cache));
   for (uint32_t page = 0; page < BBM_LUT_BASE_ADDRESS; page += MEMORY_PAGES_PER_BLOCK)
      if (read_page(transfer_buffer, page) && (memcmp(transfer_buffer, "META", 4) == 0))
      {
         start_page = (int32_t)page;
         current_page = (page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS;
         break;
      }

   // Search for the current page if a starting page was found
   if (start_page >= 0)
   {
      // Check if the data wraps around memory
      starting_page = (uint32_t)start_page;
      if (read_page(transfer_buffer, 0) && (memcmp(transfer_buffer, "DA", 2) == 0))
         current_page = 0;

      // Search for the last page containing valid data
      for (uint32_t curr_page_found_count = 0; (current_page != starting_page) && (curr_page_found_count < 2); current_page = (current_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS)
         if (read_page(transfer_buffer, current_page) && (memcmp(transfer_buffer, "DA", 2) == 0))
            curr_page_found_count = 0;
         else if (++curr_page_found_count == 2)
         {
            current_page = (current_page ? current_page : BBM_LUT_BASE_ADDRESS) - MEMORY_PAGES_PER_BLOCK;
            current_page = (current_page ? current_page : BBM_LUT_BASE_ADDRESS) - MEMORY_PAGES_PER_BLOCK;
            for (uint32_t i = 0; i < MEMORY_PAGES_PER_BLOCK; ++i)
               if (read_page(transfer_buffer, current_page + i) && ((memcmp(transfer_buffer, "META", 4) == 0) || (memcmp(transfer_buffer, "DA", 2) == 0)))
                  continue;
               else
               {
                  current_page = (current_page - MEMORY_PAGES_PER_BLOCK) + i;
                  break;
               }
         }
   }
   else
   {
      current_page = 1;
      starting_page = 0;
      memset(transfer_buffer, 0, MEMORY_PAGE_SIZE_BYTES);
      memcpy(transfer_buffer, "META", 4);
      write_register(STATUS_REGISTER_1, 0b00000010);
      write_page_raw(transfer_buffer, starting_page);
      write_register(STATUS_REGISTER_1, 0b01111110);
   }

   // Put the storage SPI peripheral into Deep Sleep mode and disable writes
   enter_low_power_mode();
   configASSERT0(am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true));
   am_hal_gpio_output_clear(PIN_STORAGE_WRITE_PROTECT);
}

void storage_deinit(void)
{
   // Disable all SPI communications
   if (!in_maintenance_mode)
   {
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, true);
      exit_low_power_mode();
   }
   am_hal_iom_uninitialize(spi_handle);
   is_reading = in_maintenance_mode = false;
}

void storage_disable(bool disable)
{
   // Set the storage disabled flag
   disabled = disable;
}

void storage_store_experiment_details(const experiment_details_t *details)
{
   // Only store new details in maintenance mode
   if (in_maintenance_mode)
   {
      // Erase all existing used pages and update storage metadata
      erase_block(0, BBM_LUT_BASE_ADDRESS - MEMORY_PAGES_PER_BLOCK);
      starting_page = ((current_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS) & 0xFFFFFFC0;
      while (is_bad_block(starting_page))
         starting_page = ((starting_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS) & 0xFFFFFFC0;
      current_page = (starting_page + 1) % BBM_LUT_BASE_ADDRESS;
      cache_index = 0;

      // Write experiment details to storage
      bool success = false;
      while (!success)
      {
         // Disable memory page write protection
         am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
         write_register(STATUS_REGISTER_1, 0b00000010);

         // Perform the write
         memset(transfer_buffer, 0, MEMORY_PAGE_SIZE_BYTES);
         memcpy(transfer_buffer, "META", 4);
         memcpy(transfer_buffer + 4, details, sizeof(*details));
         success = write_page_raw(transfer_buffer, starting_page) && read_page(transfer_buffer, starting_page);

         // Re-enable memory page write protection
         write_register(STATUS_REGISTER_1, 0b01111110);
         am_hal_gpio_output_clear(PIN_STORAGE_WRITE_PROTECT);

         // Update bad block metadata if unable to write
         if (!success)
         {
            erase_block(starting_page, starting_page);
            add_bad_block(starting_page);
            starting_page = (starting_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS;
            while (is_bad_block(starting_page))
               starting_page = ((starting_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS) & 0xFFFFFFC0;
            current_page = (starting_page + 1) % BBM_LUT_BASE_ADDRESS;
         }
      }

      // Determine whether there is an active experiment taking place
      uint32_t timestamp = rtc_get_timestamp(), time_of_day = rtc_get_time_of_day();
      bool valid_experiment = rtc_is_valid() && details->num_devices;
      bool active_experiment = valid_experiment &&
            (timestamp >= details->experiment_start_time) && (timestamp < details->experiment_end_time) &&
            (!details->use_daily_times ||
               ((details->daily_start_time < details->daily_end_time) &&
                  (time_of_day >= details->daily_start_time) && (time_of_day < details->daily_end_time)) ||
               ((details->daily_start_time > details->daily_end_time) &&
                  ((time_of_day >= details->daily_start_time) || (time_of_day < details->daily_end_time))));
      storage_disable(!active_experiment);
   }
}

void storage_store(const void *data, uint32_t data_length)
{
   // Add new data to in-memory cache if not disabled
   if (!disabled)
   {
      memcpy(cache + cache_index, data, data_length);
      cache_index += data_length;
   }
}

void storage_flush(bool write_partial_pages)
{
   // Do not flush if currently reading or if memory is full
   if (disabled || is_reading || (starting_page == current_page))
      return;

   // Flush a full page of data to memory and update the storage metadata
   if (cache_index >= MEMORY_NUM_DATA_BYTES_PER_PAGE)
   {
      write_page(MEMORY_NUM_DATA_BYTES_PER_PAGE);
      cache_index -= MEMORY_NUM_DATA_BYTES_PER_PAGE;
      current_page = (current_page + 1) % BBM_LUT_BASE_ADDRESS;
      while (is_bad_block(current_page))
         current_page = ((current_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS) & 0xFFFFFFC0;
      memmove(cache, cache + MEMORY_NUM_DATA_BYTES_PER_PAGE, cache_index);
   }

   // Write a partial page of data if requested
   if (write_partial_pages && cache_index)
      write_page((uint16_t)cache_index);
}

void storage_retrieve_experiment_details(experiment_details_t *details)
{
   // Retrieve experiment details
   if (!in_maintenance_mode)
   {
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, true);
      exit_low_power_mode();
   }
   if (read_page(transfer_buffer, starting_page))
      memcpy(details, transfer_buffer + 4, sizeof(*details));
   else
      memset(details, 0, sizeof(*details));
   if (!in_maintenance_mode)
   {
      enter_low_power_mode();
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
   }
}

void storage_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp)
{
   // Update the data reading details
   reading_page = (starting_page + 1) % BBM_LUT_BASE_ADDRESS;
   is_reading = in_maintenance_mode;
#ifndef _TEST_IMU_DATA
   experiment_details_t details;
   storage_retrieve_experiment_details(&details);
   starting_timestamp = (starting_timestamp >= details.experiment_start_time) ? (1000 * (starting_timestamp - details.experiment_start_time)) : 0;
   ending_timestamp = (ending_timestamp >= details.experiment_start_time) ? (1000 * (ending_timestamp - details.experiment_start_time)) : (1000 * (details.experiment_end_time - details.experiment_start_time));
   last_reading_page = reading_page;

   // Search for the page that contains the starting timestamp
   bool timestamp_found = !starting_timestamp;
   while (!timestamp_found && (reading_page != current_page))
   {
      if (is_bad_block(reading_page))
         reading_page = ((reading_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS) & 0xFFFFFFC0;
      else if (read_page(transfer_buffer, reading_page))
      {
         bool found_valid_timestamp = false;
         uint32_t num_bytes_retrieved = *(uint16_t*)(transfer_buffer+2);
         for (uint32_t i = 0; !found_valid_timestamp && ((i + 14) < num_bytes_retrieved); ++i)
         {
            const uint32_t potential_timestamp1 = *(uint32_t*)(transfer_buffer + 5 + i);
            const uint32_t potential_timestamp2 = *(uint32_t*)(transfer_buffer + 14 + i);
            if ((transfer_buffer[4 + i] == STORAGE_TYPE_VOLTAGE) && ((potential_timestamp1 % 500) == 0) && transfer_buffer[13 + i] && (transfer_buffer[13 + i] < STORAGE_NUM_TYPES) && ((potential_timestamp2 % 500) == 0) && (potential_timestamp1 < ending_timestamp) && ((potential_timestamp2 - potential_timestamp1) <= (BATTERY_CHECK_INTERVAL_S * 1000)))
            {
               found_valid_timestamp = true;
               if (potential_timestamp1 >= starting_timestamp)
               {
                  reading_page = last_reading_page;
                  timestamp_found = true;
               }
               else
               {
                  last_reading_page = reading_page;
                  reading_page = (reading_page + 1) % BBM_LUT_BASE_ADDRESS;
               }
            }
         }
         if (!found_valid_timestamp)
            reading_page = (reading_page + 1) % BBM_LUT_BASE_ADDRESS;
      }
      else
         reading_page = (reading_page + 1) % BBM_LUT_BASE_ADDRESS;
   }
#endif
}

void storage_end_reading(void)
{
   last_reading_page = 0;
   is_reading = false;
}

void storage_enter_maintenance_mode(void)
{
   if (!in_maintenance_mode)
   {
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, true);
      exit_low_power_mode();
   }
   in_maintenance_mode = true;
}

void storage_exit_maintenance_mode(void)
{
   storage_end_reading();
   if (in_maintenance_mode)
   {
      enter_low_power_mode();
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
   }
   in_maintenance_mode = false;
}

uint32_t storage_retrieve_num_data_chunks(uint32_t ending_timestamp)
{
   // Ensure that we are in reading mode
   if (!is_reading)
      return 0;
#ifdef _TEST_IMU_DATA
   return (starting_page < current_page) ? (current_page - starting_page) : (BBM_LUT_BASE_ADDRESS - starting_page + current_page);
#else

   log_data_size = 0;
   if (ending_timestamp)
   {
      // Convert the ending timestamp to the appropriate format
      experiment_details_t details;
      storage_retrieve_experiment_details(&details);
      ending_timestamp = (ending_timestamp >= details.experiment_start_time) ? (1000 * (ending_timestamp - details.experiment_start_time)) : 0;

      // Search for the page that contains the ending timestamp
      bool timestamp_found = false;
      last_reading_page = reading_page;
      uint32_t previous_reading_page = last_reading_page;
      while (!timestamp_found && (last_reading_page != current_page))
      {
         if (is_bad_block(last_reading_page))
            last_reading_page = ((last_reading_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS) & 0xFFFFFFC0;
         else if (read_page(transfer_buffer, last_reading_page))
         {
            bool found_valid_timestamp = false;
            uint32_t num_bytes_retrieved = *(uint16_t*)(transfer_buffer+2);
            log_data_size += num_bytes_retrieved;
            for (uint32_t i = 0; !found_valid_timestamp && ((i + 14) < num_bytes_retrieved); ++i)
            {
               const uint32_t potential_timestamp1 = *(uint32_t*)(transfer_buffer + 5 + i);
               const uint32_t potential_timestamp2 = *(uint32_t*)(transfer_buffer + 14 + i);
               if ((transfer_buffer[4 + i] == STORAGE_TYPE_VOLTAGE) && ((potential_timestamp1 % 500) == 0) && transfer_buffer[13 + i] && (transfer_buffer[13 + i] < STORAGE_NUM_TYPES) && ((potential_timestamp2 % 500) == 0) && ((potential_timestamp2 - potential_timestamp1) <= (BATTERY_CHECK_INTERVAL_S * 1000)))
               {
                  found_valid_timestamp = true;
                  if (potential_timestamp1 > ending_timestamp)
                  {
                     last_reading_page = previous_reading_page;
                     log_data_size -= num_bytes_retrieved;
                     timestamp_found = true;
                  }
                  else
                  {
                     previous_reading_page = last_reading_page;
                     last_reading_page = (last_reading_page + 1) % BBM_LUT_BASE_ADDRESS;
                  }
               }
            }
            if (!found_valid_timestamp)
               last_reading_page = (last_reading_page + 1) % BBM_LUT_BASE_ADDRESS;
         }
         else
            last_reading_page = (last_reading_page + 1) % BBM_LUT_BASE_ADDRESS;
      }
   }
   else
   {
      uint32_t page = reading_page;
      last_reading_page = current_page;
      while (page != current_page)
      {
         if (is_bad_block(page))
            page = ((page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS) & 0xFFFFFFC0;
         else
         {
            if (read_page(transfer_buffer, page))
               log_data_size += *(uint16_t*)(transfer_buffer+2);
            page = (page + 1) % BBM_LUT_BASE_ADDRESS;
         }
      }
   }
   return (reading_page <= last_reading_page) ? (1 + last_reading_page - reading_page) : (BBM_LUT_BASE_ADDRESS - reading_page + last_reading_page + 1);
#endif
}

uint32_t storage_retrieve_num_data_bytes(void)
{
   // Return the total number of log data bytes available (must be called after "storage_retrieve_num_data_chunks()")
   return (last_reading_page == current_page) ? (log_data_size + cache_index) : log_data_size;
}

uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer)
{
   // Ensure that we are in reading mode
   if (!is_reading)
      return 0;

   // Determine if a full page of memory is available to read
   uint32_t num_bytes_retrieved = 0;
#ifdef _TEST_IMU_DATA
   if (reading_page == current_page)
   {
      // Return the valid available bytes
      memcpy(buffer, cache, cache_index);
      num_bytes_retrieved = cache_index;
      is_reading = false;
   }
#else
   if (reading_page == last_reading_page)
   {
      if (reading_page == current_page)
      {
         // Return the valid available bytes
         memcpy(buffer, cache, cache_index);
         num_bytes_retrieved = cache_index;
      }
      else if (read_page(buffer, reading_page))
      {
         num_bytes_retrieved = *(uint16_t*)(buffer+2);
         memmove(buffer, buffer + 4, num_bytes_retrieved);
      }
      is_reading = false;
   }
#endif
   else
   {
      // Read the next page of memory and update the reading metadata
      while (is_bad_block(reading_page))
         reading_page = ((reading_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS) & 0xFFFFFFC0;
      if (read_page(buffer, reading_page))
      {
         num_bytes_retrieved = *(uint16_t*)(buffer+2);
         memmove(buffer, buffer + 4, num_bytes_retrieved);
      }
      reading_page = (reading_page + 1) % BBM_LUT_BASE_ADDRESS;
   }
   return num_bytes_retrieved;
}

#else

void storage_init(void) {}
void storage_deinit(void) {}
void storage_disable(bool disable) {}
void storage_store_experiment_details(const experiment_details_t *details) {}
void storage_retrieve_experiment_details(experiment_details_t *details) { memset(details, 0, sizeof(*details)); };
void storage_store(const void *data, uint32_t data_length) {}
void storage_flush(bool write_partial_pages) {}
void storage_begin_reading(uint32_t starting_timestamp) {}
void storage_end_reading(void) {}
void storage_enter_maintenance_mode(void){}
void storage_exit_maintenance_mode(void) {}
uint32_t storage_retrieve_num_data_chunks(uint32_t ending_timestamp) { return 0; }
uint32_t storage_retrieve_num_data_bytes(void) { return 0; }
uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer) { return 0; }

#endif  // #if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)
