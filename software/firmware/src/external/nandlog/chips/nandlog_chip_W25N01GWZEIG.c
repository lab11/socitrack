// Winbond W25N01GWZEIG SPI NAND driver

// Chip Declaration ----------------------------------------------------------------------------------------------------

#define NANDLOG_CHIP_NAME                           "W25N01GWZEIG"
#define NANDLOG_CHIP_PAGE_SIZE_BYTES                2048
#define NANDLOG_CHIP_SPARE_SIZE_BYTES               64
#define NANDLOG_CHIP_PAGES_PER_BLOCK                64
#define NANDLOG_CHIP_BLOCK_COUNT                    1024
#define NANDLOG_CHIP_RESERVED_BLOCKS                40

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
#define COMMAND_WRITE_BBM_LUT                       0xA1
#define COMMAND_READ_BBM_LUT                        0xA5


// Status Registers ----------------------------------------------------------------------------------------------------

#define STATUS_REGISTER_1                           0xA0
#define STATUS_REGISTER_2                           0xB0
#define STATUS_REGISTER_3                           0xC0

#define STATUS_LUT_FULL                             0b01000000
#define STATUS_PAGE_FATAL_ERROR                     0b00100000
#define STATUS_WRITE_FAILURE                        0b00001000
#define STATUS_ERASE_FAILURE                        0b00000100
#define STATUS_BUSY                                 0b00000001

// Block-protect patterns for status register 1: everything locked, or the whole array writable
#define PROTECT_ALL                                 0b01111110
#define PROTECT_NONE                                0b00000010

// Status register 2 in normal operation, and the same with the OTP pages mapped over the main array
#define CONFIG_NORMAL                               0b00011001
#define CONFIG_OTP                                  0b01011001

// Page of the OTP region carrying the marker that says this part has been initialized before
#define FIRST_BOOT_ADDRESS                          0x02

// Entries in the chip's hardware LUT. Fixed by the part, not a policy choice
#define BBM_LUT_NUM_ENTRIES                         20

// A block address is carried in bits 6..15 of a page address, and the LUT holds it in that shifted form
#define PAGE_TO_LUT_BLOCK(page)                     (((page) & 0x0000FFC0) >> 6)
#define LUT_ADDRESS_MASK                            0x3FF


// Helper Structures ---------------------------------------------------------------------------------------------------

typedef struct __attribute__ ((__packed__)) { uint16_t lba, pba; } bbm_lut_t;


// Static Global Variables ---------------------------------------------------------------------------------------------

static uint8_t scratch[NANDLOG_CHIP_PAGE_WITH_SPARE_SIZE_BYTES];
static bbm_lut_t bad_block_lookup_table[BBM_LUT_NUM_ENTRIES];

// The identity this part reports, which doubles as the marker written to its OTP page on a first boot
static const uint8_t device_id[3] = { 0xEF, 0xBA, 0x21 };


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline uint16_t byte_swap16(uint16_t value)
{
   // The LUT is transferred most significant byte first, unlike everything else on this bus
   return (uint16_t)(((value << 8) & 0xFF00) | ((value >> 8) & 0x00FF));
}

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
   uint8_t device_id_read[4];
   nandlog_port_spi_read(COMMAND_READ_DEVICE_ID, NULL, 0, device_id_read, sizeof(device_id_read));
   return (memcmp(device_id_read + 1, device_id, sizeof(device_id)) == 0);
}

static void load_bad_block_table(void)
{
   // Read the chip's own LUT and byte-swap it into host order once, so every later comparison is plain
   uint8_t dummy_value = 0;
   nandlog_port_spi_read(COMMAND_READ_BBM_LUT, &dummy_value, 1, &bad_block_lookup_table, sizeof(bad_block_lookup_table));
   for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
   {
      bad_block_lookup_table[i].lba = byte_swap16(bad_block_lookup_table[i].lba) & LUT_ADDRESS_MASK;
      bad_block_lookup_table[i].pba = byte_swap16(bad_block_lookup_table[i].pba) & LUT_ADDRESS_MASK;
   }
}

static bool is_first_boot(void)
{
   bool first_boot = false;
   write_register(STATUS_REGISTER_2, CONFIG_OTP);
   read_page_raw(scratch, FIRST_BOOT_ADDRESS, NANDLOG_CHIP_PAGE_SIZE_BYTES);
   if (memcmp(scratch, device_id, sizeof(device_id)))
   {
      memset(scratch, 0, NANDLOG_CHIP_PAGE_SIZE_BYTES);
      memcpy(scratch, device_id, sizeof(device_id));
      write_page_raw(scratch, FIRST_BOOT_ADDRESS);
      nandlog_port_spi_write(COMMAND_WRITE_DISABLE, NULL, 0, NULL, 0);
      first_boot = true;
   }
   write_register(STATUS_REGISTER_2, CONFIG_NORMAL);
   return first_boot;
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

   // Mirror the chip's existing remappings into RAM
   load_bad_block_table();

   // Check for factory-marked bad blocks if this is the first boot. This part cannot read a page's spare
   // area, so the marker is looked for at the start of the block's first page instead
   if (is_first_boot())
   {
      write_register(STATUS_REGISTER_1, PROTECT_NONE);
      for (uint32_t page = 0; page < NANDLOG_CHIP_RESERVED_BASE_PAGE; page += NANDLOG_CHIP_PAGES_PER_BLOCK)
         if (!read_page_raw(scratch, page, NANDLOG_CHIP_PAGE_SIZE_BYTES) || (scratch[0] != 0xFF))
            nandlog_chip_mark_bad_block(page);
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
   const uint16_t block = (uint16_t)PAGE_TO_LUT_BLOCK(page);
   for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
      if (bad_block_lookup_table[i].lba == block)
         return true;
   return false;
}

void nandlog_chip_mark_bad_block(uint32_t page)
{
   // Find first available workaround block
   uint16_t workaround_block = 0;
   for (uint32_t candidate = NANDLOG_CHIP_RESERVED_BASE_PAGE; !workaround_block && (candidate < NANDLOG_CHIP_PAGE_COUNT); candidate += NANDLOG_CHIP_PAGES_PER_BLOCK)
      if (read_page_raw(scratch, candidate, NANDLOG_CHIP_PAGE_SIZE_BYTES) && (scratch[0] == 0xFF))
      {
         // Ensure that the candidate block is not already in use
         workaround_block = (uint16_t)PAGE_TO_LUT_BLOCK(candidate);
         for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
            if (bad_block_lookup_table[i].pba == workaround_block)
            {
               workaround_block = 0;
               break;
            }
      }

   // The LUT is full, or every spare is already spoken for; there is nothing further this part can do
   if (!workaround_block)
   {
      nandlog_port_log("ERROR: No spare block available to remap block %u\n", (uint32_t)PAGE_TO_LUT_BLOCK(page));
      return;
   }

   // Hand the remapping to the chip, which is what makes it survive a power cycle
   const uint16_t block = (uint16_t)PAGE_TO_LUT_BLOCK(page);
   bbm_lut_t destination_address = { .lba = byte_swap16(block), .pba = byte_swap16(workaround_block) };
   write_protect(false);
   nandlog_port_spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
   nandlog_port_spi_write(COMMAND_WRITE_BBM_LUT, NULL, 0, &destination_address, sizeof(destination_address));
   wait_until_not_busy();
   write_protect(true);

   // Mirror the new entry into the in-RAM copy, in the first slot the chip has not already used
   for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
      if ((bad_block_lookup_table[i].pba == 0) && (bad_block_lookup_table[i].lba == 0))
      {
         bad_block_lookup_table[i].lba = block;
         bad_block_lookup_table[i].pba = workaround_block;
         break;
      }
}

bool nandlog_chip_reset_bad_blocks(void)
{
   // Erase the spare blocks so their contents cannot be mistaken for live data, which is as far as this part
   // can go: entries in the chip's hardware LUT are one-way and no command removes them. The in-RAM mirror is
   // deliberately left alone, because the chip is still remapping every block it names
   write_protect(false);
   for (uint32_t page = NANDLOG_CHIP_RESERVED_BASE_PAGE; page < NANDLOG_CHIP_PAGE_COUNT; page += NANDLOG_CHIP_PAGES_PER_BLOCK)
      erase_block_raw(page);      // erase failures are deliberately ignored
   write_protect(true);

   nandlog_port_log("WARNING: This part manages bad blocks in hardware; its LUT cannot be cleared\n");
   return true;
}
