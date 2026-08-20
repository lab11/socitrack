// Alliance Memory AS5F18G04SND SPI NAND driver

// Chip Declaration ----------------------------------------------------------------------------------------------------

#define NANDLOG_CHIP_NAME                           "AS5F18G04SND"
#define NANDLOG_CHIP_PAGE_SIZE_BYTES                4096
#define NANDLOG_CHIP_SPARE_SIZE_BYTES               256
#define NANDLOG_CHIP_PAGES_PER_BLOCK                64
#define NANDLOG_CHIP_BLOCK_COUNT                    4096
#define NANDLOG_CHIP_RESERVED_BLOCKS                80

#include "nandlog_chip_common.h"


// Command Set ---------------------------------------------------------------------------------------------------------

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


// Status Registers ----------------------------------------------------------------------------------------------------

#define STATUS_REGISTER_1                           0xA0
#define STATUS_REGISTER_2                           0xB0
#define STATUS_REGISTER_3                           0xC0

#define STATUS_PAGE_FATAL_ERROR                     0b00100000
#define STATUS_WRITE_FAILURE                        0b00001000
#define STATUS_ERASE_FAILURE                        0b00000100
#define STATUS_BUSY                                 0b00000001

// Block-protect patterns for status register 1: everything locked, or the whole array writable
#define PROTECT_ALL                                 0b01111110
#define PROTECT_NONE                                0b00000010

// Status register 2 in normal operation, and the same with the OTP pages mapped over the main array
#define CONFIG_NORMAL                               0b00010000
#define CONFIG_OTP                                  0b01010000

// Page of the OTP region carrying the marker that says this part has been initialized before
#define FIRST_BOOT_ADDRESS                          0x03

// Entries the in-RAM table can hold. Larger than the reserve, so any plausible table fits
#define BBM_TABLE_SIZE                              256

// Marker identifying the persisted table, followed by the entry count and the table itself
#define BBM_MARKER                                  "BBM_"
#define BBM_MARKER_LENGTH                           4


// Static Global Variables ---------------------------------------------------------------------------------------------

static uint8_t scratch[NANDLOG_CHIP_PAGE_WITH_SPARE_SIZE_BYTES];
static uint32_t bad_block_lookup_table[BBM_TABLE_SIZE];
static uint32_t bbm_index, bbm_storage_page;

// The identity this part reports, and the marker written to its OTP page on a first boot
static const uint8_t device_id[1] = { 0x8D };
static const uint8_t first_boot_marker[3] = { 0x8D, 0x00, 0x00 };


// Private Helper Functions --------------------------------------------------------------------------------------------

static void split_page_address(uint32_t page, uint8_t *out)
{
   // Page addresses go out most significant byte first
   out[0] = (uint8_t)((page & 0x00FF0000) >> 16);
   out[1] = (uint8_t)((page & 0x0000FF00) >> 8);
   out[2] = (uint8_t)(page & 0x000000FF);
}

static uint8_t read_register(uint8_t register_number)
{
   static uint8_t register_value;
   nandlog_port_spi_read(COMMAND_READ_STATUS_REGISTER, &register_number, 1, &register_value, 1);
   return register_value;
}

static void write_register(uint8_t register_number, uint8_t value)
{
   nandlog_port_spi_write(COMMAND_WRITE_STATUS_REGISTER, &register_number, 1, &value, 1);
}

static void wait_until_not_busy(void)
{
   for (uint32_t polls_remaining = STORAGE_BUSY_TIMEOUT_POLLS; polls_remaining; --polls_remaining)
   {
      if ((read_register(STATUS_REGISTER_3) & STATUS_BUSY) != STATUS_BUSY)
         return;
      nandlog_port_delay_us(STORAGE_BUSY_POLL_INTERVAL_US);
   }

   // Reset immediately rather than flushing first
   nandlog_port_log("ERROR: Storage flash never cleared BUSY after %u polls (>= %u ms); resetting\n", (uint32_t)STORAGE_BUSY_TIMEOUT_POLLS, (uint32_t)STORAGE_BUSY_TIMEOUT_MS);
   nandlog_port_fatal("SPI transfer failed after all retries");
}

static void write_protect(bool protect)
{
   // Order matters in both directions
   if (protect)
   {
      write_register(STATUS_REGISTER_1, PROTECT_ALL);
      nandlog_port_write_enable(false);
   }
   else
   {
      nandlog_port_write_enable(true);
      write_register(STATUS_REGISTER_1, PROTECT_NONE);
   }
}

static bool read_page_raw(uint8_t *buffer, uint32_t page, uint32_t length)
{
   const uint32_t byte_offset = 0;
   uint8_t page_number_reordered[3];
   split_page_address(page, page_number_reordered);
   wait_until_not_busy();
   nandlog_port_spi_write(COMMAND_PAGE_DATA_READ, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
   wait_until_not_busy();
   nandlog_port_spi_read(COMMAND_READ, &byte_offset, 3, buffer, length);
   wait_until_not_busy();
   return (read_register(STATUS_REGISTER_3) & STATUS_PAGE_FATAL_ERROR) != STATUS_PAGE_FATAL_ERROR;
}

static bool write_page_raw(const uint8_t *data, uint32_t page)
{
   // Assumes write protection has already been opened by the caller
   const uint16_t byte_offset = 0;
   uint8_t page_number_reordered[3];
   split_page_address(page, page_number_reordered);
   for (uint8_t retry_index = 0; retry_index < MEMORY_NUM_BLOCK_ERRORS_BEFORE_REMOVAL; ++retry_index)
   {
      wait_until_not_busy();
      nandlog_port_spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      nandlog_port_spi_write(COMMAND_PROGRAM_DATA_LOAD, &byte_offset, 2, data, NANDLOG_CHIP_PAGE_SIZE_BYTES);
      wait_until_not_busy();
      nandlog_port_spi_write(COMMAND_PROGRAM_EXECUTE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
      wait_until_not_busy();
      if ((read_register(STATUS_REGISTER_3) & STATUS_WRITE_FAILURE) != STATUS_WRITE_FAILURE)
         return true;
   }
   return false;
}

static bool erase_block_raw(uint32_t page)
{
   // Assumes write protection has already been opened by the caller
   uint8_t page_number_reordered[3];
   split_page_address(page & NANDLOG_CHIP_BLOCK_MASK, page_number_reordered);
   wait_until_not_busy();
   nandlog_port_spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
   nandlog_port_spi_write(COMMAND_BLOCK_ERASE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
   wait_until_not_busy();
   return (read_register(STATUS_REGISTER_3) & STATUS_ERASE_FAILURE) != STATUS_ERASE_FAILURE;
}

static bool verify_device_id(void)
{
   static const uint8_t address = 0x01;
   uint8_t device_id_read[sizeof(device_id)];
   nandlog_port_spi_read(COMMAND_READ_DEVICE_ID, &address, sizeof(address), device_id_read, sizeof(device_id_read));
   return (memcmp(device_id_read, device_id, sizeof(device_id)) == 0);
}

static void clear_bad_block_table(void)
{
   bbm_index = 0;
   memset(bad_block_lookup_table, 0xFF, sizeof(bad_block_lookup_table));
}

static bool record_bad_block(uint32_t page)
{
   // Refuse to run off the end of the table
   if (bbm_index >= BBM_TABLE_SIZE)
   {
      nandlog_port_log("ERROR: Bad-block table is full at %u entries; block %u not retired\n", (uint32_t)BBM_TABLE_SIZE, (uint32_t)(page / NANDLOG_CHIP_PAGES_PER_BLOCK));
      return false;
   }
   bad_block_lookup_table[bbm_index++] = page & NANDLOG_CHIP_BLOCK_MASK;
   return true;
}

static void persist_bad_block_table(void)
{
   // Assumes write protection has already been opened by the caller
   memset(scratch, 0, NANDLOG_CHIP_PAGE_SIZE_BYTES);
   memcpy(scratch, BBM_MARKER, BBM_MARKER_LENGTH);
   memcpy(scratch + BBM_MARKER_LENGTH, &bbm_index, sizeof(bbm_index));
   memcpy(scratch + BBM_MARKER_LENGTH + sizeof(bbm_index), bad_block_lookup_table, sizeof(bad_block_lookup_table));
   write_page_raw(scratch, bbm_storage_page);
}

static void load_bad_block_table(void)
{
   // Search the reserve from the top down for the most recently written marker page
   clear_bad_block_table();
   for (bbm_storage_page = NANDLOG_CHIP_PAGE_COUNT - NANDLOG_CHIP_PAGES_PER_BLOCK; bbm_storage_page >= NANDLOG_CHIP_RESERVED_BASE_PAGE; bbm_storage_page -= NANDLOG_CHIP_PAGES_PER_BLOCK)
      if (read_page_raw(scratch, bbm_storage_page, NANDLOG_CHIP_PAGE_SIZE_BYTES) && (memcmp(scratch, BBM_MARKER, BBM_MARKER_LENGTH) == 0))
      {
         memcpy(&bbm_index, scratch + BBM_MARKER_LENGTH, sizeof(bbm_index));
         memcpy(bad_block_lookup_table, scratch + BBM_MARKER_LENGTH + sizeof(bbm_index), sizeof(bad_block_lookup_table));

         // Refuse to trust an implausible count; treat as "table unusable" rather than acting on it
         if (bbm_index > NANDLOG_CHIP_RESERVED_BLOCKS)
         {
            nandlog_port_log("WARNING: Bad-block table reports %u entries, exceeding the %u-block reserve; ignoring it\n", (uint32_t)bbm_index, (uint32_t)NANDLOG_CHIP_RESERVED_BLOCKS);
            clear_bad_block_table();
         }
         break;
      }

   // The search loop above exits one block BELOW the reserve when it finds no marker
   if (bbm_storage_page < NANDLOG_CHIP_RESERVED_BASE_PAGE)
      bbm_storage_page = NANDLOG_CHIP_RESERVED_BASE_PAGE;
}

static bool is_first_boot(void)
{
   bool first_boot = false;
   write_register(STATUS_REGISTER_2, CONFIG_OTP);
   read_page_raw(scratch, FIRST_BOOT_ADDRESS, NANDLOG_CHIP_PAGE_SIZE_BYTES);
   if (memcmp(scratch, first_boot_marker, sizeof(first_boot_marker)))
   {
      memset(scratch, 0, NANDLOG_CHIP_PAGE_SIZE_BYTES);
      memcpy(scratch, first_boot_marker, sizeof(first_boot_marker));
      write_page_raw(scratch, FIRST_BOOT_ADDRESS);
      nandlog_port_spi_write(COMMAND_WRITE_DISABLE, NULL, 0, NULL, 0);
      first_boot = true;
   }
   write_register(STATUS_REGISTER_2, CONFIG_NORMAL);
   return first_boot;
}

static void build_bad_block_table_from_factory_markers(void)
{
   // A factory-marked bad block carries a non-0xFF byte at the start of its spare area. Blocks already in
   // the table are skipped rather than the table being rebuilt, so anything loaded earlier survives the scan
   for (uint32_t page = 0; page < NANDLOG_CHIP_PAGE_COUNT; page += NANDLOG_CHIP_PAGES_PER_BLOCK)
      if ((!read_page_raw(scratch, page, NANDLOG_CHIP_PAGE_WITH_SPARE_SIZE_BYTES) ||
           (scratch[NANDLOG_CHIP_PAGE_SIZE_BYTES] != 0xFF)) && !nandlog_chip_is_bad_block(page))
         record_bad_block(page);

   // Persist into the first block of the reserve that is usable
   bbm_storage_page = NANDLOG_CHIP_RESERVED_BASE_PAGE;
   while (nandlog_chip_is_bad_block(bbm_storage_page))
      bbm_storage_page = (bbm_storage_page + NANDLOG_CHIP_PAGES_PER_BLOCK) & NANDLOG_CHIP_BLOCK_MASK;
   persist_bad_block_table();
}


// Chip Interface Functions --------------------------------------------------------------------------------------------

const nandlog_geometry_t *nandlog_chip_geometry(void)
{
   static const nandlog_geometry_t geometry = NANDLOG_CHIP_GEOMETRY_INITIALIZER;
   return &geometry;
}

bool nandlog_chip_probe(void)
{
   // Wait until the chip becomes accessible
   for (uint32_t retries = 0; retries < 1000; ++retries)
   {
      if (verify_device_id())
         return true;
      nandlog_port_delay_ms(1);
   }
   return false;
}

void nandlog_chip_init(void)
{
   // Programming the OTP marker below relies on the write-protect pin being released, as it is from reset
   nandlog_port_write_enable(true);
   nandlog_port_delay_ms(3);
   wait_until_not_busy();

   // Configure the memory chip
   write_register(STATUS_REGISTER_1, PROTECT_ALL);
   write_register(STATUS_REGISTER_2, CONFIG_NORMAL);

   // Retrieve the list of existing bad storage blocks, or scan for them if this is the first boot
   load_bad_block_table();
   if (is_first_boot())
   {
      write_register(STATUS_REGISTER_1, PROTECT_NONE);
      build_bad_block_table_from_factory_markers();
      write_register(STATUS_REGISTER_1, PROTECT_ALL);
   }
}

void nandlog_chip_low_power(bool sleep)
{
   // This part has no deep-power-down command worth the wake-up latency
   (void)sleep;
}

bool nandlog_chip_read_page(uint8_t *buffer, uint32_t page)
{
   return read_page_raw(buffer, page, NANDLOG_CHIP_PAGE_SIZE_BYTES);
}

bool nandlog_chip_write_page(const uint8_t *data, uint32_t page)
{
   write_protect(false);
   const bool success = write_page_raw(data, page);
   write_protect(true);
   return success;
}

bool nandlog_chip_erase_block(uint32_t page)
{
   write_protect(false);
   const bool success = erase_block_raw(page);
   write_protect(true);
   return success;
}

bool nandlog_chip_is_bad_block(uint32_t page)
{
   // Search for the block address in the bad block lookup table
   page &= NANDLOG_CHIP_BLOCK_MASK;
   for (uint32_t i = 0; i < bbm_index; ++i)
      if (bad_block_lookup_table[i] == page)
         return true;
   return false;
}

void nandlog_chip_mark_bad_block(uint32_t page)
{
   // Disable memory page write protection
   write_protect(false);

   // Erase the table's storage page, stepping to another block of the reserve if it will not take an erase
   bool success = false;
   while (!success)
   {
      if (!erase_block_raw(bbm_storage_page))
      {
         uint32_t next_bbm_storage_page = (bbm_storage_page + NANDLOG_CHIP_PAGES_PER_BLOCK) & NANDLOG_CHIP_BLOCK_MASK;
         while (nandlog_chip_is_bad_block(next_bbm_storage_page))
            next_bbm_storage_page = (next_bbm_storage_page + NANDLOG_CHIP_PAGES_PER_BLOCK) & NANDLOG_CHIP_BLOCK_MASK;
         if (next_bbm_storage_page < NANDLOG_CHIP_PAGE_COUNT)
            bbm_storage_page = next_bbm_storage_page;
         else
            success = true;
      }
      else
         success = true;
   }

   // Update the table with the bad block and write it back out
   record_bad_block(page);
   persist_bad_block_table();

   // Re-enable memory page write protection
   write_protect(true);
}

bool nandlog_chip_reset_bad_blocks(void)
{
   // Erase every block of the reserve, so no marker page survives to be found on the next boot
   write_protect(false);
   for (uint32_t page = NANDLOG_CHIP_RESERVED_BASE_PAGE; page < NANDLOG_CHIP_PAGE_COUNT; page += NANDLOG_CHIP_PAGES_PER_BLOCK)
      erase_block_raw(page);      // erase failures are deliberately ignored
   write_protect(true);

   // Verify rather than assume
   uint32_t markers_remaining = 0;
   for (uint32_t page = NANDLOG_CHIP_RESERVED_BASE_PAGE; page < NANDLOG_CHIP_PAGE_COUNT; page += NANDLOG_CHIP_PAGES_PER_BLOCK)
      if (read_page_raw(scratch, page, NANDLOG_CHIP_PAGE_SIZE_BYTES) && (memcmp(scratch, BBM_MARKER, BBM_MARKER_LENGTH) == 0))
         ++markers_remaining;

   clear_bad_block_table();
   bbm_storage_page = NANDLOG_CHIP_RESERVED_BASE_PAGE;
   if (markers_remaining)
      nandlog_port_log("ERROR: %u bad-block marker page(s) still present after erase\n", markers_remaining);
   return (markers_remaining == 0);
}
