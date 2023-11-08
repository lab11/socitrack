// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "buzzer.h"
#include "storage.h"


// Chip-Specific Definitions -------------------------------------------------------------------------------------------

#define STORAGE_DEVICE_ID                           { 0xEF, 0xBA, 0x21 }

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
#define COMMAND_WRITE_BBM_LUT                       0xA1
#define COMMAND_READ_BBM_LUT                        0xA5
#define COMMAND_ENABLE_RESET                        0x66
#define COMMAND_RESET_DEVICE                        0x99

#define STATUS_REGISTER_1                           0xA0
#define STATUS_REGISTER_2                           0xB0
#define STATUS_REGISTER_3                           0xC0
#define OTP_BASE_ADDRESS                            0x02
#define FIRST_BOOT_ADDRESS                          OTP_BASE_ADDRESS

#define STATUS_LUT_FULL                             0b01000000
#define STATUS_PAGE_FATAL_ERROR                     0b00100000
#define STATUS_PAGE_NONFATAL_ERROR                  0b00010000
#define STATUS_WRITE_FAILURE                        0b00001000
#define STATUS_ERASE_FAILURE                        0b00000100
#define STATUS_BUSY                                 0b00000001

#define BBM_INTERNAL_LUT_NUM_ENTRIES                20
#define BBM_EXTERNAL_LUT_NUM_ENTRIES                20
#define BBM_NUM_RESERVED_BLOCKS                     40
#define BBM_LUT_BASE_ADDRESS                        ((MEMORY_BLOCK_COUNT - BBM_NUM_RESERVED_BLOCKS) * MEMORY_PAGES_PER_BLOCK)


// Helper Structures ---------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__)) { uint16_t lba, pba; } bbm_lut_t;


// Static Global Variables ---------------------------------------------------------------------------------------------

static void *spi_handle;
static bbm_lut_t bad_block_lookup_table_internal[BBM_INTERNAL_LUT_NUM_ENTRIES];
static uint8_t cache[2 * MEMORY_PAGE_SIZE_BYTES], transfer_buffer[MEMORY_PAGE_SIZE_BYTES];
static uint32_t starting_page, current_page, reading_page, cache_index;
static bool is_reading, in_maintenance_mode, disabled;


// Private Helper Functions --------------------------------------------------------------------------------------------

static void spi_read(uint8_t command, const void *address, uint32_t address_length, void *read_buffer, uint32_t read_length)
{
   // Create the SPI transaction structure
   uint32_t instruction = command;
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

   // Repeat the transfer until it succeeds
   while (am_hal_iom_blocking_transfer(spi_handle, &spi_transaction) != AM_HAL_STATUS_SUCCESS);

   // Update the SPI transaction structure
   spi_transaction.eDirection = AM_HAL_IOM_RX;
   spi_transaction.ui32NumBytes = read_length;
   spi_transaction.pui32TxBuffer = NULL,
   spi_transaction.pui32RxBuffer = read_buffer;
   spi_transaction.bContinue = false;

   // Repeat the transfer until it succeeds
   while (am_hal_iom_blocking_transfer(spi_handle, &spi_transaction) != AM_HAL_STATUS_SUCCESS);
}

static void spi_write(uint8_t command, const void *address, uint32_t address_length, const void *write_buffer, uint32_t write_length)
{
   // Create the SPI transaction structure
   uint32_t instruction = command;
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

   // Repeat the transfer until it succeeds
   while (am_hal_iom_blocking_transfer(spi_handle, &spi_transaction) != AM_HAL_STATUS_SUCCESS);

   // Update the SPI transaction structure
   spi_transaction.ui32NumBytes = write_length;
   spi_transaction.pui32TxBuffer = (uint32_t*)write_buffer,
   spi_transaction.bContinue = false;

   // Repeat the transfer until it succeeds
   while (am_hal_iom_blocking_transfer(spi_handle, &spi_transaction) != AM_HAL_STATUS_SUCCESS);
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
   uint8_t device_id_read[4], device_id_known[3] = STORAGE_DEVICE_ID;
   spi_read(COMMAND_READ_DEVICE_ID, NULL, 0, device_id_read, sizeof(device_id_read));
   return (memcmp(device_id_read + 1, device_id_known, sizeof(device_id_known)) == 0);
}

static void wait_until_not_busy(void)
{
   while ((read_register(STATUS_REGISTER_3) & STATUS_BUSY) == STATUS_BUSY)
      am_hal_delay_us(1);
}

static bool write_page_raw(const uint8_t *data, uint32_t page_number)
{
   const uint16_t byte_offset = 0;
   const uint16_t page_number_reordered = (uint16_t)(((page_number & 0x0000FF00) >> 8) | ((page_number & 0x000000FF) << 8));
   for (uint8_t retry_index = 0; retry_index < MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL; ++retry_index)
   {
      wait_until_not_busy();
      spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      spi_write(COMMAND_PROGRAM_DATA_LOAD, &byte_offset, 2, data, MEMORY_PAGE_SIZE_BYTES);
      wait_until_not_busy();
      spi_write(COMMAND_PROGRAM_EXECUTE, &byte_offset, 1, &page_number_reordered, 2);
      wait_until_not_busy();
      if ((read_register(STATUS_REGISTER_3) & STATUS_WRITE_FAILURE) != STATUS_WRITE_FAILURE)
         return true;
   }
   return false;
}

static bool read_page(uint8_t *buffer, uint32_t page_number)
{
   const uint32_t byte_offset = 0;
   const uint16_t page_number_reordered = (uint16_t)(((page_number & 0x0000FF00) >> 8) | ((page_number & 0x000000FF) << 8));
   wait_until_not_busy();
   spi_write(COMMAND_PAGE_DATA_READ, &byte_offset, 1, &page_number_reordered, 2);
   wait_until_not_busy();
   spi_read(COMMAND_READ, &byte_offset, 3, buffer, MEMORY_PAGE_SIZE_BYTES);
   wait_until_not_busy();
   return (read_register(STATUS_REGISTER_3) & STATUS_PAGE_FATAL_ERROR) != STATUS_PAGE_FATAL_ERROR;
}

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

static void add_bad_block(uint16_t block_address)
{
   // Find first available workaround block
   uint16_t workaround_block = 0;
   for (uint32_t page = BBM_LUT_BASE_ADDRESS; !workaround_block && (page < MEMORY_PAGE_COUNT); page += MEMORY_PAGES_PER_BLOCK)
      if (read_page(transfer_buffer, page) && (transfer_buffer[0] == 0xFF))
      {
         // Ensure that the candidate block is not already in use
         workaround_block = (uint16_t)((page & 0x0000FFC0) >> 6);
         for (uint32_t i = 0; i < BBM_INTERNAL_LUT_NUM_ENTRIES; ++i)
            if (bad_block_lookup_table_internal[i].pba == workaround_block)
            {
               workaround_block = 0;
               break;
            }
      }

   // Update LUT with the workaround block
   if (workaround_block)
   {
      block_address = (uint16_t)(((uint32_t)block_address & 0x0000FFC0) >> 6);
      bbm_lut_t destination_address = {
         .lba = ((block_address << 8) & 0xFF00) | ((block_address >> 8) & 0x00FF),
         .pba = ((workaround_block << 8) & 0xFF00) | ((workaround_block >> 8) & 0x00FF)
      };
      spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      spi_write(COMMAND_WRITE_BBM_LUT, NULL, 0, &destination_address, sizeof(destination_address));
      wait_until_not_busy();

      // Update the bad block lookup table
      for (uint32_t i = 0; i < BBM_INTERNAL_LUT_NUM_ENTRIES; ++i)
         if ((bad_block_lookup_table_internal[i].pba == 0) && (bad_block_lookup_table_internal[i].lba == 0))
         {
            bad_block_lookup_table_internal[i].lba = block_address;
            bad_block_lookup_table_internal[i].pba = workaround_block;
            break;
         }
   }
}

static void write_page(uint16_t data_length)
{
   // Disable memory page write protection
   if (!in_maintenance_mode)
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, true);
   am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
   write_register(STATUS_REGISTER_1, 0b00000010);
   const uint32_t original_page = current_page;

   // Continue trying to write the current page to memory until successful
   bool success = false;
   while (!success)
   {
      // Fill up transfer buffer with current page data
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
         uint32_t next_block = ((current_page + MEMORY_PAGES_PER_BLOCK) & 0x0000FFC0) % BBM_LUT_BASE_ADDRESS;
         transfer_block(original_page & 0x0000FFC0, next_block, current_page & 0x003F);
         add_bad_block(current_page);
         current_page = (current_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS;
      }
   }

   // Re-enable memory page write protection
   write_register(STATUS_REGISTER_1, 0b01111110);
   am_hal_gpio_output_clear(PIN_STORAGE_WRITE_PROTECT);
   if (!in_maintenance_mode)
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
}

static void erase_block(uint32_t starting_page, uint32_t ending_page)
{
   // Disable memory page write protection
   am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
   write_register(STATUS_REGISTER_1, 0b00000010);

   // Iterate through all blocks to be erased
   ending_page &= 0x0000FFC0;
   starting_page &= 0x0000FFC0;
   const uint8_t num_iterations = (starting_page <= ending_page) ? 1 : 2;
   uint32_t end = (starting_page <= ending_page) ? ending_page : (BBM_LUT_BASE_ADDRESS - 1);
   for (uint8_t i = 0; i < num_iterations; ++i)
   {
      for (uint32_t page = starting_page; page <= end; page += MEMORY_PAGES_PER_BLOCK)
      {
         // Erase the current page and ensure that the command was successful
         const uint16_t page_number_reordered = (uint16_t)(((page & 0x0000FF00) >> 8) | ((page & 0x000000FF) << 8));
         wait_until_not_busy();
         spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
         spi_write(COMMAND_BLOCK_ERASE, &page, 1, &page_number_reordered, 2);
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

static void ensure_empty_memory(void)
{
   // Ensure that all memory blocks have been successfully erased
   bool current_block_erased = false;
   const uint32_t current_block = current_page & 0x0000FFC0;
   for (uint32_t block = 0; block < (MEMORY_BLOCK_COUNT - BBM_NUM_RESERVED_BLOCKS); ++block)
      if (!read_page(transfer_buffer, block * MEMORY_PAGES_PER_BLOCK) || (transfer_buffer[0] != 0xFF) || (transfer_buffer[1] != 0xFF))
      {
         erase_block(block * MEMORY_PAGES_PER_BLOCK, block * MEMORY_PAGES_PER_BLOCK);
         if (block == current_block)
            current_block_erased = true;
      }
   if (!current_block_erased)
      erase_block(current_page, current_page);
}

static bool is_first_boot(void)
{
   bool first_boot = false;
   uint8_t device_id[3] = STORAGE_DEVICE_ID;
   write_register(STATUS_REGISTER_2, 0b01011000);
   read_page(transfer_buffer, FIRST_BOOT_ADDRESS);
   if (memcmp(transfer_buffer, device_id, sizeof(device_id)))
   {
      memset(transfer_buffer, 0, sizeof(transfer_buffer));
      memcpy(transfer_buffer, device_id, sizeof(device_id));
      write_page_raw(transfer_buffer, FIRST_BOOT_ADDRESS);
      spi_write(COMMAND_WRITE_DISABLE, NULL, 0, NULL, 0);
      first_boot = true;
   }
   write_register(STATUS_REGISTER_2, 0b00011000);
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

   // Configure the memory chip
   const uint8_t status_register_1_bits = 0b01111110;
   const uint8_t status_register_2_bits = 0b00011000;
   write_register(STATUS_REGISTER_1, status_register_1_bits);
   write_register(STATUS_REGISTER_2, status_register_2_bits);

   // Retrieve the list of existing bad storage blocks
   uint8_t dummy_value = 0;
   spi_read(COMMAND_READ_BBM_LUT, &dummy_value, 1, &bad_block_lookup_table_internal, sizeof(bad_block_lookup_table_internal));
   for (uint32_t i = 0; i < BBM_INTERNAL_LUT_NUM_ENTRIES; ++i)
   {
      bad_block_lookup_table_internal[i].lba = (((bad_block_lookup_table_internal[i].lba << 8) & 0xFF00) | ((bad_block_lookup_table_internal[i].lba >> 8) & 0x00FF)) & 0x3FF;
      bad_block_lookup_table_internal[i].pba = (((bad_block_lookup_table_internal[i].pba << 8) & 0xFF00) | ((bad_block_lookup_table_internal[i].pba >> 8) & 0x00FF)) & 0x3FF;
   }

   // Check for bad storage blocks if this is the first boot
   if (is_first_boot())
   {
      write_register(STATUS_REGISTER_1, 0b00000010);
      for (uint32_t page = 0; page < BBM_LUT_BASE_ADDRESS; page += MEMORY_PAGES_PER_BLOCK)
         if (!read_page(transfer_buffer, page) || (transfer_buffer[0] != 0xFF))
            add_bad_block(page);
      write_register(STATUS_REGISTER_1, 0b01111110);
   }

   // Search for the starting page
   cache_index = 0;
   int32_t start_page = -1;
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
      bool curr_page_found = false;
      for ( ; !curr_page_found && (current_page != starting_page); current_page = (current_page + MEMORY_PAGES_PER_BLOCK) % BBM_LUT_BASE_ADDRESS)
         if (!read_page(transfer_buffer, current_page) || memcmp(transfer_buffer, "DA", 2))
         {
            curr_page_found = true;
            current_page = (current_page ? current_page : BBM_LUT_BASE_ADDRESS) - MEMORY_PAGES_PER_BLOCK;
            for (uint32_t i = 0; i < MEMORY_PAGES_PER_BLOCK; ++i)
               if (read_page(transfer_buffer, current_page + i) && (memcmp(transfer_buffer, "META", 4) == 0))
                  continue;
               else if (memcmp(transfer_buffer, "DA", 2) == 0)
               {
                  uint16_t len = *(uint16_t*)(transfer_buffer+2);
                  if (len < MEMORY_NUM_DATA_BYTES_PER_PAGE)
                  {
                     current_page = (current_page - MEMORY_PAGES_PER_BLOCK) + i;
                     cache_index = len;
                     memcpy(cache, transfer_buffer + 4, len);
                     break;
                  }
               }
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
      memset(transfer_buffer, 0, sizeof(transfer_buffer));
      memcpy(transfer_buffer, "META", 4);
      write_register(STATUS_REGISTER_1, 0b00000010);
      write_page_raw(transfer_buffer, starting_page);
      write_register(STATUS_REGISTER_1, 0b01111110);
   }

   // Put the storage SPI peripheral into Deep Sleep mode and disable writes
   configASSERT0(am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true));
   am_hal_gpio_output_clear(PIN_STORAGE_WRITE_PROTECT);
}

void storage_deinit(void)
{
   // Disable all SPI communications
   if (!in_maintenance_mode)
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, true);
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
      ensure_empty_memory();
      starting_page = (current_page + MEMORY_PAGES_PER_BLOCK) & 0x0000FFC0;
      current_page = (current_page + 1) % BBM_LUT_BASE_ADDRESS;
      cache_index = 0;

      // Write experiment details to storage
      bool success = false;
      while (!success)
      {
         // Disable memory page write protection
         am_hal_gpio_output_set(PIN_STORAGE_WRITE_PROTECT);
         write_register(STATUS_REGISTER_1, 0b00000010);

         // Perform the write
         memset(transfer_buffer, 0, sizeof(transfer_buffer));
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
            current_page = (starting_page + 1) % BBM_LUT_BASE_ADDRESS;
         }
      }
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
      memmove(cache, cache + MEMORY_NUM_DATA_BYTES_PER_PAGE, cache_index);
   }

   // Write a partial page of data if requested
   if (write_partial_pages && cache_index)
      write_page((uint16_t)cache_index);
}

#ifndef _TEST_BLUETOOTH

void storage_retrieve_experiment_details(experiment_details_t *details)
{
   // Retrieve experiment details
   if (!in_maintenance_mode)
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, true);
   if (read_page(transfer_buffer, starting_page))
      memcpy(details, transfer_buffer + 4, sizeof(*details));
   else
      memset(details, 0, sizeof(*details));
   if (!in_maintenance_mode)
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
}

void storage_begin_reading(void)
{
   reading_page = (starting_page + 1) % BBM_LUT_BASE_ADDRESS;
   is_reading = in_maintenance_mode;
}

void storage_end_reading(void)
{
   is_reading = false;
}

void storage_enter_maintenance_mode(void)
{
   if (!in_maintenance_mode)
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_WAKE, true);
   in_maintenance_mode = true;
}

void storage_exit_maintenance_mode(void)
{
   storage_end_reading();
   if (in_maintenance_mode)
      am_hal_iom_power_ctrl(spi_handle, AM_HAL_SYSCTRL_DEEPSLEEP, true);
   in_maintenance_mode = false;
}

uint32_t storage_retrieve_data_length(void)
{
   uint32_t data_length = (BBM_LUT_BASE_ADDRESS - starting_page - 1) * MEMORY_NUM_DATA_BYTES_PER_PAGE;
   if (starting_page < current_page)
      data_length = ((current_page - starting_page - 1) * MEMORY_NUM_DATA_BYTES_PER_PAGE) + cache_index;
   else
      data_length += (current_page * MEMORY_NUM_DATA_BYTES_PER_PAGE) + ((starting_page == current_page) ? 0 : cache_index);
   return data_length;
}

uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer)
{
   // Ensure that we are in reading mode
   if (!is_reading)
      return 0;

   // Determine if a full page of memory is available to read
   uint32_t num_bytes_retrieved = MEMORY_NUM_DATA_BYTES_PER_PAGE;
   if (reading_page == current_page)
   {
      // Return the valid available bytes
      memcpy(buffer, cache, cache_index);
      num_bytes_retrieved = cache_index;
      is_reading = false;
   }
   else
   {
      // Read the next page of memory and update the reading metadata
      if (read_page(buffer, reading_page))
         memmove(buffer, buffer + 4, MEMORY_NUM_DATA_BYTES_PER_PAGE);
      else
         memset(buffer, 0xFF, MEMORY_NUM_DATA_BYTES_PER_PAGE);
      reading_page = (reading_page + 1) % BBM_LUT_BASE_ADDRESS;
      if ((starting_page == current_page) && (reading_page == current_page))
         is_reading = false;
   }
   return num_bytes_retrieved;
}

#endif  // #ifndef _TEST_BLUETOOTH
