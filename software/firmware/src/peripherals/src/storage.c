// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "buzzer.h"
#include "logging.h"
#include "rtc.h"
#include "nandlog_port.h"
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

#define SEED_SEARCH_MAX_PAGES                       8
#define SEEK_BACKTRACK_LIMIT_PAGES                  MEMORY_PAGES_PER_BLOCK

#define METADATA_RING_BLOCKS                        8
#define METADATA_RING_PAGES                         (METADATA_RING_BLOCKS * MEMORY_PAGES_PER_BLOCK)
#define LOG_REGION_FIRST_PAGE                       METADATA_RING_PAGES
#define LOG_REGION_END_PAGE                         BBM_LUT_BASE_ADDRESS
#define LOG_REGION_PAGE_COUNT                       (LOG_REGION_END_PAGE - LOG_REGION_FIRST_PAGE)
#define PAGE_BLOCK_MASK                             (~(uint32_t)(MEMORY_PAGES_PER_BLOCK - 1))


// Helper Structures ---------------------------------------------------------------------------------------------------

#if REVISION_ID < REVISION_N
typedef struct __attribute__ ((__packed__)) { uint16_t lba, pba; } bbm_lut_t;
#else
typedef uint32_t bbm_lut_t;
static volatile uint32_t bbm_index, bbm_storage_page;
#endif


// Static Global Variables ---------------------------------------------------------------------------------------------

static bbm_lut_t bad_block_lookup_table[BBM_TABLE_SIZE];
static uint8_t cache[2 * MEMORY_PAGE_SIZE_BYTES], transfer_buffer[MEMORY_PAGE_SIZE_BYTES + MEMORY_ECC_BYTES_PER_PAGE];
static uint32_t retransmit_seqs[STORAGE_MAX_RETRANSMIT_PAGES], retransmit_num_pages = 0;
static volatile uint32_t starting_page, current_page, reading_page, last_reading_page, cache_index, log_data_size;
static volatile bool is_reading, in_maintenance_mode, disabled, cache_overflowed, is_initialized = false, log_region_full;
static volatile uint32_t page_first_timestamp = STORAGE_NO_TIMESTAMP, page_last_timestamp = STORAGE_NO_TIMESTAMP;
static volatile uint32_t log_epoch, next_page_seq, page_record_count, metadata_ring_page;


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline uint32_t log_wrap_page(uint32_t page)
{
   // Fold a page that has run past the end of the region back to its start
   return (page >= LOG_REGION_END_PAGE)
             ? (LOG_REGION_FIRST_PAGE + ((page - LOG_REGION_FIRST_PAGE) % LOG_REGION_PAGE_COUNT))
             : page;
}

static inline uint32_t log_next_page(uint32_t page)
{
   return log_wrap_page(page + 1);
}

static inline uint32_t log_prev_page(uint32_t page)
{
   return (page == LOG_REGION_FIRST_PAGE) ? (LOG_REGION_END_PAGE - 1) : (page - 1);
}

static inline uint32_t log_next_block(uint32_t page)
{
   // First page of the block following the one containing 'page'
   return log_wrap_page((page + MEMORY_PAGES_PER_BLOCK) & PAGE_BLOCK_MASK);
}

static inline uint32_t log_page_distance(uint32_t from, uint32_t to)
{
   // Forward distance from one page to another, accounting for wrap
   return (to >= from) ? (to - from) : (LOG_REGION_PAGE_COUNT - (from - to));
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

static bool verify_device_id(void)
{
#if REVISION_ID < REVISION_N
   uint8_t device_id_read[4], device_id_known[3] = STORAGE_DEVICE_ID;
   nandlog_port_spi_read(COMMAND_READ_DEVICE_ID, NULL, 0, device_id_read, sizeof(device_id_read));
   return (memcmp(device_id_read + 1, device_id_known, sizeof(device_id_known)) == 0);
#else
   static const uint8_t address = 0x01;
   uint8_t device_id_read[1], device_id_known[1] = STORAGE_DEVICE_ID;
   nandlog_port_spi_read(COMMAND_READ_DEVICE_ID, &address, sizeof(address), device_id_read, sizeof(device_id_read));
   return (memcmp(device_id_read, device_id_known, sizeof(device_id_known)) == 0);
#endif
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
   print("ERROR: Storage flash never cleared BUSY after %u polls (>= %u ms); resetting\n", (uint32_t)STORAGE_BUSY_TIMEOUT_POLLS, (uint32_t)STORAGE_BUSY_TIMEOUT_MS);
   system_reset(true);
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
      nandlog_port_spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      nandlog_port_spi_write(COMMAND_PROGRAM_DATA_LOAD, &byte_offset, 2, data, MEMORY_PAGE_SIZE_BYTES);
      wait_until_not_busy();
      nandlog_port_spi_write(COMMAND_PROGRAM_EXECUTE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
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
   nandlog_port_spi_write(COMMAND_PAGE_DATA_READ, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
   wait_until_not_busy();
   nandlog_port_spi_read(COMMAND_READ, &byte_offset, 3, buffer, MEMORY_PAGE_SIZE_BYTES);
   wait_until_not_busy();
   return (read_register(STATUS_REGISTER_3) & STATUS_PAGE_FATAL_ERROR) != STATUS_PAGE_FATAL_ERROR;
}

#if REVISION_ID > REVISION_M
static bool read_page_with_spare_data(uint8_t *buffer, uint32_t page_number)
{
   const uint32_t byte_offset = 0;
   const uint8_t page_number_reordered[] = { (uint8_t)((page_number & 0x00FF0000) >> 16), (uint8_t)((page_number & 0x0000FF00) >> 8), (uint8_t)(page_number & 0x000000FF) };
   wait_until_not_busy();
   nandlog_port_spi_write(COMMAND_PAGE_DATA_READ, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
   wait_until_not_busy();
   nandlog_port_spi_read(COMMAND_READ, &byte_offset, 3, buffer, MEMORY_PAGE_SIZE_BYTES + MEMORY_ECC_BYTES_PER_PAGE);
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
      nandlog_port_spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      nandlog_port_spi_write(COMMAND_WRITE_BBM_LUT, NULL, 0, &destination_address, sizeof(destination_address));
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
   nandlog_port_write_enable(true);
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
      nandlog_port_spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      nandlog_port_spi_write(COMMAND_BLOCK_ERASE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
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
   nandlog_port_write_enable(false);
}

#endif

static void erase_block(uint32_t starting_page, uint32_t ending_page)
{
   // Disable memory page write protection
   nandlog_port_write_enable(true);
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
         nandlog_port_spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
         nandlog_port_spi_write(COMMAND_BLOCK_ERASE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
         wait_until_not_busy();
         if ((read_register(STATUS_REGISTER_3) & STATUS_ERASE_FAILURE) == STATUS_ERASE_FAILURE)
            add_bad_block(page);
      }
      starting_page = 0;
      end = ending_page;
   }

   // Re-enable memory page write protection
   write_register(STATUS_REGISTER_1, 0b01111110);
   nandlog_port_write_enable(false);
}

static uint32_t crc32_compute(const void *data, uint32_t length)
{
   // Reflects IEEE 802.3 CRC-32 (poly 0xEDB88320, init and final XOR 0xFFFFFFFF). Byte-for-byte identical
   // to Python's zlib.crc32, so the host verifies device-computed values with no bespoke implementation
   static const uint32_t table[256] = {
      0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
      0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
      0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
      0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
      0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
      0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
      0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
      0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
      0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
      0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
      0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
      0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
      0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
      0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
      0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
      0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
      0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
      0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
      0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
      0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
      0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
      0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
      0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
      0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
      0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
      0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
      0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
      0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
      0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
      0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
      0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
      0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D
   };
   const uint8_t *bytes = (const uint8_t*)data;
   uint32_t crc = 0xFFFFFFFF;
   for (uint32_t i = 0; i < length; ++i)
      crc = (crc >> 8) ^ table[(crc ^ bytes[i]) & 0xFF];
   return ~crc;
}

static bool page_header_valid(const storage_page_header_t *header)
{
   // A page is trustworthy only if it carries the magic and its header checksums correctly
   return (header->magic == STORAGE_PAGE_MAGIC) &&
          (header->header_crc == crc32_compute(header, STORAGE_PAGE_HEADER_CRC_BYTES)) &&
          (header->payload_length <= MEMORY_NUM_DATA_BYTES_PER_PAGE);
}

static void write_page(uint16_t data_length)
{
   // Disable memory page write protection
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      exit_low_power_mode();
   }
   nandlog_port_write_enable(true);
   write_register(STATUS_REGISTER_1, 0b00000010);
   const uint32_t original_page = current_page;

   // Continue trying to write the current page to memory until successful
   bool success = false;
   while (!success)
   {
      // Build a self-describing, self-validating page: ordering metadata and time bounds in the header,
      // separate checksums over the header and the payload
      memset(transfer_buffer, 0xFF, MEMORY_PAGE_SIZE_BYTES);
      storage_page_header_t *header = (storage_page_header_t*)transfer_buffer;
      header->magic = STORAGE_PAGE_MAGIC;
      header->epoch = log_epoch;
      header->seq = next_page_seq;
      header->first_timestamp = page_first_timestamp;
      header->last_timestamp = page_last_timestamp;
      header->payload_length = data_length;
      header->record_count = (uint16_t)page_record_count;
      memcpy(transfer_buffer + sizeof(storage_page_header_t), cache, data_length);
      header->payload_crc = crc32_compute(transfer_buffer + sizeof(storage_page_header_t), data_length);
      header->header_crc = crc32_compute(header, STORAGE_PAGE_HEADER_CRC_BYTES);

      // Add the current block to the list of bad blocks if unable to write, or if the page does not read
      // back exactly as written -- the header CRC makes that check exact rather than ECC-dependent
      if (write_page_raw(transfer_buffer, current_page) && read_page(transfer_buffer, current_page) &&
          page_header_valid((const storage_page_header_t*)transfer_buffer))
         success = true;
      else
      {
         // Transfer any already-written pages in the current block to the next block
         uint32_t next_block = log_next_block(current_page);
         while (is_bad_block(next_block))
            next_block = log_next_block(next_block);

         // Erase the relocation target before transferring into it
         erase_block(next_block, next_block);
         nandlog_port_write_enable(true);
         write_register(STATUS_REGISTER_1, 0b00000010);

         transfer_block(original_page & 0xFFFFFFC0, next_block, current_page & 0x003F);
         add_bad_block(current_page);
         current_page = next_block | (current_page & 0x003F);
      }
   }

   // Re-enable memory page write protection
   write_register(STATUS_REGISTER_1, 0b01111110);
   nandlog_port_write_enable(false);
   if (!in_maintenance_mode)
   {
      enter_low_power_mode();
      nandlog_port_power(false);
   }
}

static void erase_ahead_of(uint32_t page)
{
   // Maintain a rolling window of erased blocks ahead of the write head
   uint32_t block = page & 0xFFFFFFC0;
   for (uint32_t i = 0; i < ERASE_AHEAD_BLOCKS; ++i)
   {
      block = log_next_block(block);
      while (is_bad_block(block))
         block = log_next_block(block);

      // Reaching the metadata block means the log has wrapped the entire array and memory is full
      if (block == (starting_page & 0xFFFFFFC0))
         break;
      erase_block(block, block);
   }
}

static void erase_ahead_of_head(void)
{
   // Wake the storage peripheral around the erase, mirroring what write_page() does
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      exit_low_power_mode();
   }
   erase_ahead_of(current_page);
   if (!in_maintenance_mode)
   {
      enter_low_power_mode();
      nandlog_port_power(false);
   }
}

static void advance_write_head(void)
{
   // Step to the next good page
   current_page = log_next_page(current_page);
   while (is_bad_block(current_page))
      current_page = log_next_block(current_page);

   // Wrapping onto the first page of the epoch means every usable page has been consumed
   if (current_page == starting_page)
      log_region_full = true;

   // Top up the erased window from the middle of each block to separate block
   // erases from page writes
   if ((current_page & (MEMORY_PAGES_PER_BLOCK - 1)) == ERASE_AHEAD_TRIGGER_PAGE)
      erase_ahead_of_head();
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
      nandlog_port_spi_write(COMMAND_WRITE_DISABLE, NULL, 0, NULL, 0);
      first_boot = true;
   }
#if REVISION_ID < REVISION_N
   write_register(STATUS_REGISTER_2, 0b00011001);
#else
   write_register(STATUS_REGISTER_2, 0b00010000);
#endif
   return first_boot;
}

static bool meta_header_valid(const storage_meta_header_t *header)
{
   return (header->magic == STORAGE_META_MAGIC) &&
          (header->header_crc == crc32_compute(header, STORAGE_META_HEADER_CRC_BYTES)) &&
          (header->details_length == sizeof(experiment_details_t)) &&
          (header->log_start_page >= LOG_REGION_FIRST_PAGE) && (header->log_start_page < LOG_REGION_END_PAGE);
}

static bool find_newest_metadata(uint32_t *ring_page, storage_meta_header_t *newest)
{
   // Scan the metadata ring and select the highest valid epoch
   bool found = false;
   for (uint32_t page = 0; page < METADATA_RING_PAGES; ++page)
   {
      if (is_bad_block(page) || !read_page(transfer_buffer, page))
         continue;
      const storage_meta_header_t *header = (const storage_meta_header_t*)transfer_buffer;
      if (!meta_header_valid(header))
         continue;
      if (header->details_crc != crc32_compute(transfer_buffer + sizeof(storage_meta_header_t), header->details_length))
         continue;
      if (!found || (header->epoch > newest->epoch))
      {
         memcpy(newest, header, sizeof(*newest));
         *ring_page = page;
         found = true;
      }
   }
   return found;
}

static uint32_t recover_write_head(uint32_t epoch, uint32_t log_start_page, uint32_t *head_seq)
{
   // Binary search for the first page that does NOT belong to this epoch
   uint32_t low = 0, high = LOG_REGION_PAGE_COUNT;
   while (low < high)
   {
      const uint32_t mid = low + ((high - low) / 2);
      const uint32_t page = log_wrap_page(log_start_page + mid);
      bool belongs = false;
      if (!is_bad_block(page) && read_page(transfer_buffer, page))
      {
         const storage_page_header_t *header = (const storage_page_header_t*)transfer_buffer;
         belongs = page_header_valid(header) && (header->epoch == epoch);
      }
      if (belongs)
         low = mid + 1;
      else
         high = mid;
   }

   // 'low' is now the number of pages this epoch occupies. Read the last one for its sequence number so the
   // next page continues the series.
   *head_seq = 0;
   if (low)
   {
      const uint32_t last = log_wrap_page(log_start_page + low - 1);
      if (read_page(transfer_buffer, last))
      {
         const storage_page_header_t *header = (const storage_page_header_t*)transfer_buffer;
         if (page_header_valid(header) && (header->epoch == epoch))
            *head_seq = header->seq + 1;
      }
   }
   return log_wrap_page(log_start_page + low);
}

static uint32_t validated_payload_length(const uint8_t *page)
{
   // Payload length is only meaningful once the header it lives in has been checksummed
   const storage_page_header_t *header = (const storage_page_header_t*)page;
   return page_header_valid(header) ? header->payload_length : 0;
}

static uint32_t extract_page_payload(uint8_t *page)
{
   // Validate a page read into 'page' and strip its header in place, leaving only verified payload bytes
   const storage_page_header_t *header = (const storage_page_header_t*)page;
   if (!page_header_valid(header))
      return 0;
   const uint32_t data_length = header->payload_length;
   if (data_length && (header->payload_crc != crc32_compute(page + sizeof(storage_page_header_t), data_length)))
      return 0;
   memmove(page, page + sizeof(storage_page_header_t), data_length);
   return data_length;
}


// Public API Functions ------------------------------------------------------------------------------------------------

bool storage_init(void)
{
   // Return if already initialized
   if (is_initialized)
      return true;

   // Everything platform-specific about reaching the chip lives behind this call
   is_reading = in_maintenance_mode = disabled = false;
   if (!nandlog_port_init())
      return false;

   // Wait until the chip becomes accessible
   int retries;
   for (retries = 0; (retries < 1000) && !verify_device_id(); ++retries)
      nandlog_port_delay_ms(1);
#ifdef _MANUFACTURING_TEST_
   enter_low_power_mode();
   nandlog_port_power(false);
   nandlog_port_write_enable(false);
   return retries < 1000;
#endif
   nandlog_port_delay_ms(3);
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
   nandlog_port_spi_read(COMMAND_READ_BBM_LUT, &dummy_value, 1, &bad_block_lookup_table, sizeof(bad_block_lookup_table));
   for (uint32_t i = 0; i < BBM_LUT_NUM_ENTRIES; ++i)
   {
      bad_block_lookup_table[i].lba = (((bad_block_lookup_table[i].lba << 8) & 0xFF00) | ((bad_block_lookup_table[i].lba >> 8) & 0x00FF)) & 0x3FF;
      bad_block_lookup_table[i].pba = (((bad_block_lookup_table[i].pba << 8) & 0xFF00) | ((bad_block_lookup_table[i].pba >> 8) & 0x00FF)) & 0x3FF;
   }
#else
   bbm_index = 0;
   memset(bad_block_lookup_table, 0xFF, sizeof(bad_block_lookup_table));
   for (bbm_storage_page = MEMORY_MAX_PAGE_ADDRESS - MEMORY_PAGES_PER_BLOCK; bbm_storage_page >= BBM_LUT_BASE_ADDRESS; bbm_storage_page -= MEMORY_PAGES_PER_BLOCK)
      if (read_page(transfer_buffer, bbm_storage_page) && (memcmp(transfer_buffer, "BBM_", 4) == 0))
      {
         memcpy((uint32_t*)&bbm_index, transfer_buffer + 4, sizeof(bbm_index));
         memcpy(bad_block_lookup_table, transfer_buffer + 4 + sizeof(bbm_index), sizeof(bad_block_lookup_table));

         // Refuse to trust an implausible count; treat as "table unusable" rather than acting on it
         if (bbm_index > BBM_NUM_RESERVED_BLOCKS)
         {
            print("WARNING: Bad-block table reports %u entries, exceeding the %u-block reserve; ignoring it\n", (uint32_t)bbm_index, (uint32_t)BBM_NUM_RESERVED_BLOCKS);
            bbm_index = 0;
            memset(bad_block_lookup_table, 0xFF, sizeof(bad_block_lookup_table));
         }
         break;
      }
#endif

   // The search loop above exits one block BELOW the reserve when it finds no marker
#if REVISION_ID >= REVISION_N
   if (bbm_storage_page < BBM_LUT_BASE_ADDRESS)
      bbm_storage_page = BBM_LUT_BASE_ADDRESS;
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

   // Recover the current epoch from the metadata ring, then locate the write head within it
   cache_index = last_reading_page = log_data_size = 0;
   page_record_count = 0;
   page_first_timestamp = page_last_timestamp = STORAGE_NO_TIMESTAMP;
   memset(cache, 0, sizeof(cache));

   storage_meta_header_t metadata;
   uint32_t metadata_page = 0;
   if (find_newest_metadata(&metadata_page, &metadata))
   {
      log_epoch = metadata.epoch;
      starting_page = metadata.log_start_page;
      metadata_ring_page = metadata_page;
      current_page = recover_write_head(log_epoch, starting_page, (uint32_t*)&next_page_seq);
      log_region_full = next_page_seq && (current_page == starting_page);
   }
   else
   {
      // Nothing valid in the ring: either a factory-fresh part or a device migrating from a previous on-flash format
      log_epoch = 0;
      next_page_seq = 0;
      metadata_ring_page = METADATA_RING_PAGES;    // no metadata written yet
      starting_page = LOG_REGION_FIRST_PAGE;
      while (is_bad_block(starting_page))
         starting_page = log_next_block(starting_page);
      current_page = starting_page;
      log_region_full = false;
   }

   // Unconditionally re-establish the erased window ahead of the head
   erase_ahead_of(current_page);

   // Put the storage SPI peripheral into Deep Sleep mode and disable writes
   enter_low_power_mode();
   nandlog_port_power(false);
   nandlog_port_write_enable(false);
   is_initialized = true;
   return true;
}

void storage_deinit(void)
{
   // Only continue if initialized
   if (!is_initialized)
      return;

   // Disable all SPI communications
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      exit_low_power_mode();
   }
   nandlog_port_deinit();
   is_reading = in_maintenance_mode = false;
   is_initialized = false;
}

void storage_reset_bad_block_table(void)
{
   // RECOVERY UTILITY. Erases a persisted bad-block table so it is rebuilt as empty on the next boot
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      exit_low_power_mode();
   }

   nandlog_port_write_enable(true);
   write_register(STATUS_REGISTER_1, 0b00000010);
   for (uint32_t page = BBM_LUT_BASE_ADDRESS; page < MEMORY_MAX_PAGE_ADDRESS; page += MEMORY_PAGES_PER_BLOCK)
   {
      const uint8_t page_number_reordered[] = {
         (uint8_t)((page & 0x00FF0000) >> 16), (uint8_t)((page & 0x0000FF00) >> 8), (uint8_t)(page & 0x000000FF) };
      wait_until_not_busy();
      nandlog_port_spi_write(COMMAND_WRITE_ENABLE, NULL, 0, NULL, 0);
      nandlog_port_spi_write(COMMAND_BLOCK_ERASE, NULL, 0, page_number_reordered, sizeof(page_number_reordered));
      wait_until_not_busy();
      // Erase failures are deliberately ignored
   }
   write_register(STATUS_REGISTER_1, 0b01111110);
   nandlog_port_write_enable(false);

   // Verify rather than assume
   uint32_t markers_remaining = 0;
   for (uint32_t page = BBM_LUT_BASE_ADDRESS; page < MEMORY_MAX_PAGE_ADDRESS; page += MEMORY_PAGES_PER_BLOCK)
      if (read_page(transfer_buffer, page) && (memcmp(transfer_buffer, "BBM_", 4) == 0))
         ++markers_remaining;

#if REVISION_ID >= REVISION_N
   bbm_index = 0;
   bbm_storage_page = BBM_LUT_BASE_ADDRESS;
#endif
   memset(bad_block_lookup_table, 0xFF, sizeof(bad_block_lookup_table));

   if (!in_maintenance_mode)
   {
      enter_low_power_mode();
      nandlog_port_power(false);
   }

   if (markers_remaining)
      print("ERROR: Bad-block table NOT cleared -- %u marker page(s) still present\n", markers_remaining);
   else
      print("INFO: Bad-block table erased and verified clear; rebuilt empty on next boot\n");
}

void storage_disable(bool disable)
{
   // Set the storage disabled flag
   disabled = disable;
}

bool storage_store_experiment_details(const experiment_details_t *details)
{
   // Refuse loudly rather than silently discarding the write
   if (!in_maintenance_mode)
   {
      print("ERROR: Refusing to store experiment details outside of maintenance mode\n");
      return false;
   }

   // Begin a new epoch whose log continues the wear sweep from the current head
   const uint32_t new_epoch = log_epoch + 1;
   uint32_t new_log_start = log_next_block(current_page);
   while (is_bad_block(new_log_start))
      new_log_start = log_next_block(new_log_start);

   // Establish the erased window before any data can land in it
   erase_block(new_log_start, new_log_start);
   erase_ahead_of(new_log_start);

   // Commit with a single metadata page program
   bool success = false;
   for (uint32_t attempt = 0; !success && (attempt < METADATA_RING_PAGES); ++attempt)
   {
      const uint32_t slot = (metadata_ring_page >= METADATA_RING_PAGES)
                               ? 0 : ((metadata_ring_page + 1) % METADATA_RING_PAGES);
      metadata_ring_page = slot;
      if (is_bad_block(slot))
         continue;

      // Erase on entry to each block of the ring, so a slot is always clean before it is programmed
      if ((slot & (MEMORY_PAGES_PER_BLOCK - 1)) == 0)
         erase_block(slot, slot);
      nandlog_port_write_enable(true);
      write_register(STATUS_REGISTER_1, 0b00000010);

      memset(transfer_buffer, 0xFF, MEMORY_PAGE_SIZE_BYTES);
      storage_meta_header_t *header = (storage_meta_header_t*)transfer_buffer;
      header->magic = STORAGE_META_MAGIC;
      header->epoch = new_epoch;
      header->log_start_page = new_log_start;
      header->created_timestamp = rtc_get_timestamp();
      header->details_length = (uint16_t)sizeof(*details);
      header->format_version = STORAGE_FORMAT_VERSION;
      header->reserved = 0xFFFFFFFF;
      memcpy(transfer_buffer + sizeof(storage_meta_header_t), details, sizeof(*details));
      header->details_crc = crc32_compute(transfer_buffer + sizeof(storage_meta_header_t), sizeof(*details));
      header->header_crc = crc32_compute(header, STORAGE_META_HEADER_CRC_BYTES);

      success = write_page_raw(transfer_buffer, slot) && read_page(transfer_buffer, slot) &&
                meta_header_valid((const storage_meta_header_t*)transfer_buffer);

      write_register(STATUS_REGISTER_1, 0b01111110);
      nandlog_port_write_enable(false);
      // Deliberately do NOT call add_bad_block() here
   }
   if (!success)
   {
      print("ERROR: Unable to write experiment metadata to any slot in the ring\n");
      return false;
   }

   // The new epoch is now live
   log_epoch = new_epoch;
   starting_page = new_log_start;
   current_page = new_log_start;
   log_region_full = false;
   next_page_seq = cache_index = page_record_count = 0;
   page_first_timestamp = page_last_timestamp = STORAGE_NO_TIMESTAMP;

   // Determine whether there is an active experiment taking place
   uint32_t timestamp = rtc_get_timestamp(), time_of_day = rtc_get_time_of_day();
   bool valid_experiment = rtc_is_valid() && details->num_devices && !details->is_terminated;
   bool active_experiment = valid_experiment &&
         (timestamp >= details->experiment_start_time) && (timestamp < details->experiment_end_time) &&
         (!details->use_daily_times ||
            ((details->daily_start_time < details->daily_end_time) &&
               (time_of_day >= details->daily_start_time) && (time_of_day < details->daily_end_time)) ||
            ((details->daily_start_time > details->daily_end_time) &&
               ((time_of_day >= details->daily_start_time) || (time_of_day < details->daily_end_time))));
   storage_disable(!active_experiment);
   return true;
}

static void commit_current_page(void)
{
   // Write everything buffered as one page and reset the page-assembly state
   if (!cache_index)
      return;
   write_page((uint16_t)cache_index);
   ++next_page_seq;
   cache_index = page_record_count = 0;
   page_first_timestamp = page_last_timestamp = STORAGE_NO_TIMESTAMP;
   cache_overflowed = false;
   advance_write_head();
}

void storage_store_record(uint8_t record_type, uint32_t timestamp, const void *data, uint32_t data_length)
{
   // Add a complete record to the page currently being assembled, if storage is not disabled
   if (disabled)
      return;

   const uint32_t record_length = 1 + sizeof(timestamp) + data_length;
   if (record_length > MEMORY_NUM_DATA_BYTES_PER_PAGE)
   {
      // A single record larger than a page cannot be represented; drop it rather than corrupt the stream
      cache_overflowed = true;
      return;
   }

   // A timestamp that precedes the page's current end means the time base moved backwards.
   // Commit the page first, so no page ever advertises a last_timestamp earlier than its first
   const bool time_moved_backwards = (page_last_timestamp != STORAGE_NO_TIMESTAMP) &&
                                     (timestamp < page_last_timestamp);

   // Records are never split across pages, so a record that does not fit in what remains of the current
   // page commits that page and starts the next one
   if (((cache_index + record_length) > MEMORY_NUM_DATA_BYTES_PER_PAGE) || time_moved_backwards)
   {
      // Writing is impossible while a download is in progress or once the array is full
      if (is_reading || log_region_full)
      {
         cache_overflowed = true;
         return;
      }
      commit_current_page();
   }

   memcpy(cache + cache_index, &record_type, sizeof(record_type));
   memcpy(cache + cache_index + sizeof(record_type), &timestamp, sizeof(timestamp));
   if (data_length)
      memcpy(cache + cache_index + sizeof(record_type) + sizeof(timestamp), data, data_length);
   cache_index += record_length;

   // Track the time bounds and record count that this page's header will advertise
   if (page_first_timestamp == STORAGE_NO_TIMESTAMP)
      page_first_timestamp = timestamp;
   page_last_timestamp = timestamp;
   ++page_record_count;
}

void storage_flush(bool write_partial_pages)
{
   // Do not flush if currently reading or if memory is full
   if (disabled || is_reading || log_region_full)
      return;

   // A page is committed as soon as the next record will not fit, so the only reason to
   // write here is an explicit request to commit a partial page: a timed flush or shutdown
   if (write_partial_pages || (cache_index >= MEMORY_NUM_DATA_BYTES_PER_PAGE))
      commit_current_page();
}

bool storage_has_buffered_data(void)
{
   // Reports whether any collected data is sitting unwritten in RAM
   return (cache_index != 0);
}

void storage_retrieve_experiment_details(experiment_details_t *details)
{
   // Retrieve experiment details
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      exit_low_power_mode();
   }

   // Details live in the metadata ring, validated by their own CRC, rather than in the log itself
   memset(details, 0, sizeof(*details));
   if ((metadata_ring_page < METADATA_RING_PAGES) && read_page(transfer_buffer, metadata_ring_page))
   {
      const storage_meta_header_t *header = (const storage_meta_header_t*)transfer_buffer;
      const uint8_t *blob = transfer_buffer + sizeof(storage_meta_header_t);
      if (meta_header_valid(header) && (header->details_crc == crc32_compute(blob, header->details_length)))
         memcpy(details, blob, sizeof(*details));
   }

   if (!in_maintenance_mode)
   {
      enter_low_power_mode();
      nandlog_port_power(false);
   }
}

static uint32_t epoch_page_count(void)
{
   // Physical pages spanned by the current epoch. Bad blocks skipped at write time are counted, since the
   // head jumped over them, so an index into this range can land on an unwritten page
   return log_region_full ? LOG_REGION_PAGE_COUNT : log_page_distance(starting_page, current_page);
}

static bool probe_epoch_page(uint32_t index, uint32_t page_count, storage_page_header_t *header, uint32_t *found_index)
{
   // First valid current-epoch page at or after 'index'. A probe can land inside a block that was skipped
   // as bad, so step forward rather than concluding the epoch has ended
   for (uint32_t i = index; i < page_count; ++i)
   {
      const uint32_t page = log_wrap_page(starting_page + i);
      if (!is_bad_block(page) && read_page(transfer_buffer, page))
      {
         const storage_page_header_t *candidate = (const storage_page_header_t*)transfer_buffer;
         if (page_header_valid(candidate) && (candidate->epoch == log_epoch))
         {
            memcpy(header, candidate, sizeof(*header));
            *found_index = i;
            return true;
         }
      }
   }
   return false;
}

static uint32_t seek_page_for_timestamp(uint32_t timestamp, uint32_t page_count, bool at_or_after)
{
   // Binary search the epoch for a time boundary using the per-page header bounds. Timestamps increase
   // monotonically with sequence number, so "this page ends at or after T" is monotone over the range
   uint32_t low = 0, high = page_count, result = at_or_after ? page_count : 0;
   while (low < high)
   {
      const uint32_t mid = low + ((high - low) / 2);
      storage_page_header_t header;
      uint32_t found = 0;
      if (!probe_epoch_page(mid, page_count, &header, &found))
      {
         high = mid;                    // nothing valid from here on
         continue;
      }
      if (at_or_after ? (header.last_timestamp >= timestamp) : (header.first_timestamp > timestamp))
      {
         result = at_or_after ? found : (found ? (found - 1) : 0);
         high = mid;
      }
      else
      {
         if (!at_or_after)
            result = found;
         low = found + 1;
      }
   }

   // Monotonicity is an assumption, not a guarantee, so verify the answer rather than trust it
   if (at_or_after && result)
   {
      uint32_t steps = 0;
      while (result && (steps < SEEK_BACKTRACK_LIMIT_PAGES))
      {
         storage_page_header_t header;
         uint32_t found = 0;
         if (!probe_epoch_page(result - 1, page_count, &header, &found) || (found >= result) ||
             (header.last_timestamp < timestamp))
            break;
         result = found;
         ++steps;
      }
      if (steps >= SEEK_BACKTRACK_LIMIT_PAGES)
      {
         // Still finding qualifying pages at the limit means the log is badly out of order; fall back to the whole range
         print("WARNING: log timestamps are badly non-monotonic; ignoring the requested start time\n");
         result = 0;
      }
   }
   return result;
}

void storage_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp)
{
   // Update the data reading details
   reading_page = starting_page;
   last_reading_page = starting_page;
   is_reading = in_maintenance_mode;
#ifndef _TEST_IMU_DATA
   (void)ending_timestamp;
   if (!starting_timestamp)
      return;

   // Convert the caller's absolute timestamp into the experiment-relative milliseconds the headers carry
   experiment_details_t details;
   storage_retrieve_experiment_details(&details);
   const uint32_t relative_start = (starting_timestamp >= details.experiment_start_time)
                                      ? (1000 * (starting_timestamp - details.experiment_start_time)) : 0;
   if (!relative_start)
      return;

   const uint32_t page_count = epoch_page_count();
   const uint32_t index = seek_page_for_timestamp(relative_start, page_count, true);
   reading_page = (index < page_count) ? log_wrap_page(starting_page + index) : current_page;
   last_reading_page = reading_page;
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
      nandlog_port_power(true);
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
      nandlog_port_power(false);
   }
   in_maintenance_mode = false;
}

uint32_t storage_retrieve_num_data_chunks(uint32_t ending_timestamp)
{
   // Ensure that we are in reading mode
   if (!is_reading)
      return 0;
#ifdef _TEST_IMU_DATA
   return log_page_distance(starting_page, current_page);
#else

   log_data_size = 0;
   if (ending_timestamp)
   {
      // Convert to experiment-relative milliseconds and binary-search the headers for the last page whose
      // first record falls at or before the requested end
      experiment_details_t details;
      storage_retrieve_experiment_details(&details);
      const uint32_t relative_end = (ending_timestamp >= details.experiment_start_time)
                                       ? (1000 * (ending_timestamp - details.experiment_start_time)) : 0;
      const uint32_t page_count = epoch_page_count();
      const uint32_t end_index = seek_page_for_timestamp(relative_end, page_count, false);
      last_reading_page = log_wrap_page(starting_page + end_index);
   }
   else
      last_reading_page = current_page;

   // Sum the payload bytes across the selected span. The boundaries above are exact; this pass exists only to report a byte total
   for (uint32_t page = reading_page; page != last_reading_page; )
   {
      if (is_bad_block(page))
         page = log_next_block(page);
      else
      {
         if (read_page(transfer_buffer, page))
            log_data_size += validated_payload_length(transfer_buffer);
         page = log_next_page(page);
      }
   }

   // The trailing chunk is whatever is still buffered in RAM, which is NOT a stored page. Counting it
   // when it is empty makes the device announce a page it then sends with a zero length, which the host
   // can only read as a lost page. In the time-bounded case last_reading_page is a real page and the +1
   // makes that range inclusive instead.
   if (last_reading_page == current_page)
      return log_page_distance(reading_page, last_reading_page) + (cache_index ? 1 : 0);
   return 1 + log_page_distance(reading_page, last_reading_page);
#endif
}

uint32_t storage_retrieve_num_data_bytes(void)
{
   // Return the total number of log data bytes available (must be called after "storage_retrieve_num_data_chunks()")
   return (last_reading_page == current_page) ? (log_data_size + cache_index) : log_data_size;
}

static uint32_t stored_record_length(const uint8_t *payload, uint32_t offset, uint32_t length)
{
   // Structural length of the record at 'offset'. Every record is [type:1][timestamp:4][data], so the
   // length is fixed by the type except where the data itself begins with a count.
   switch (payload[offset])
   {
      case STORAGE_TYPE_VOLTAGE:
         return 9;
      case STORAGE_TYPE_CHARGING_EVENT:
      case STORAGE_TYPE_MOTION:
         return 6;
      case STORAGE_TYPE_RANGES:
         return ((offset + 6) <= length) ? (6 + (payload[offset + 5] * COMPRESSED_RANGE_DATUM_LENGTH)) : 0;
      case STORAGE_TYPE_IMU:
         return ((offset + 6) <= length) ? (5 + payload[offset + 5]) : 0;   // the length byte counts itself
      case STORAGE_TYPE_BLE_SCAN:
         return ((offset + 6) <= length) ? (6 + payload[offset + 5]) : 0;
      case STORAGE_TYPE_RESET_REASON:
         return 7;
      case STORAGE_TYPE_TIME_ANCHOR:
         return 9;
      default:
         return 0;
   }
}

static bool last_time_anchor_in_page(const uint8_t *payload, uint32_t length, uint32_t *experiment_ms, uint32_t *rtc)
{
   // Keep the newest anchor in this page; payloads are record-aligned so a forward walk is exact
   bool found = false;
   uint32_t offset = 0;
   while ((offset + 5) < length)
   {
      const uint32_t record_length = stored_record_length(payload, offset, length);
      if (!record_length || ((offset + record_length) > length))
         break;
      if (payload[offset] == STORAGE_TYPE_TIME_ANCHOR)
      {
         memcpy(experiment_ms, payload + offset + 1, sizeof(*experiment_ms));
         memcpy(rtc, payload + offset + 5, sizeof(*rtc));
         found = true;
      }
      offset += record_length;
   }
   return found;
}

bool storage_recover_time_anchor(uint32_t *experiment_ms, uint32_t *rtc)
{
   // The newest anchor pairs an experiment timestamp with the raw RTC value at the instant it was written.
   // That pair fixes the network offset independently of WHEN it is read back
   if (current_page == starting_page)
      return false;

   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      exit_low_power_mode();
   }

   bool found = false;
   uint32_t page = current_page;
   for (uint32_t back = 0; back < SEED_SEARCH_MAX_PAGES; ++back)
   {
      page = log_prev_page(page);
      if (is_bad_block(page) || !read_page(transfer_buffer, page))
         continue;
      const storage_page_header_t *header = (const storage_page_header_t*)transfer_buffer;
      if (!page_header_valid(header) || (header->epoch != log_epoch))
         break;                              // walked off the start of this epoch
      const uint32_t length = extract_page_payload(transfer_buffer);
      if (length && last_time_anchor_in_page(transfer_buffer, length, experiment_ms, rtc))
      {
         found = true;
         break;
      }
      if (page == starting_page)
         break;
   }

   if (!in_maintenance_mode)
   {
      enter_low_power_mode();
      nandlog_port_power(false);
   }
   return found;
}

uint32_t storage_retrieve_page_by_seq(uint32_t seq, uint8_t *buffer, storage_page_header_t *header)
{
   // Fetch one specific page so the host can ask for the ones it lost
   memset(header, 0, sizeof(*header));
   header->magic = STORAGE_PAGE_MAGIC;
   header->epoch = log_epoch;
   header->seq = seq;
   header->first_timestamp = header->last_timestamp = STORAGE_NO_TIMESTAMP;
   if (!is_reading)
      return 0;

   const uint32_t page_count = epoch_page_count();
   uint32_t low = 0, high = page_count;
   while (low < high)
   {
      const uint32_t mid = low + ((high - low) / 2);
      storage_page_header_t probe;
      uint32_t found = 0;
      if (!probe_epoch_page(mid, page_count, &probe, &found))
      {
         high = mid;                      // nothing valid from here on
         continue;
      }
      if (probe.seq == seq)
      {
         if (!read_page(buffer, log_wrap_page(starting_page + found)))
            return 0;
         const storage_page_header_t stored = *(const storage_page_header_t*)buffer;
         const uint32_t length = extract_page_payload(buffer);
         if (!length)
            return 0;                     // failed validation again; still a gap
         *header = stored;
         return length;
      }
      if (probe.seq < seq)
         low = found + 1;
      else
         high = mid;
   }
   return 0;
}

void storage_retransmit_clear(void)
{
   retransmit_num_pages = 0;
}

uint32_t storage_retransmit_add(const uint32_t *seqs, uint32_t count)
{
   // Accumulate across commands: one BLE write cannot name every page a long transfer might have lost
   for (uint32_t i = 0; (i < count) && (retransmit_num_pages < STORAGE_MAX_RETRANSMIT_PAGES); ++i)
      retransmit_seqs[retransmit_num_pages++] = seqs[i];
   return retransmit_num_pages;
}

uint32_t storage_retransmit_count(void)
{
   return retransmit_num_pages;
}

uint32_t storage_retransmit_total_bytes(void)
{
   // The host sizes its receive buffer from the stream header, so this total has to be exact rather than an upper bound
   uint32_t total = 0;
   for (uint32_t i = 0; i < retransmit_num_pages; ++i)
   {
      storage_page_header_t header;
      total += storage_retrieve_page_by_seq(retransmit_seqs[i], transfer_buffer, &header);
   }
   return total;
}

uint32_t storage_retrieve_retransmit_page(uint32_t index, uint8_t *buffer, storage_page_header_t *header)
{
   // Answer the index-th requested page. A page that is still unreadable comes back as a zero-length frame
   // carrying the requested sequence number, so the host can tell "still missing" from "never answered"
   if (index >= retransmit_num_pages)
   {
      memset(header, 0, sizeof(*header));
      header->magic = STORAGE_PAGE_MAGIC;
      header->epoch = log_epoch;
      header->first_timestamp = header->last_timestamp = STORAGE_NO_TIMESTAMP;
      return 0;
   }
   return storage_retrieve_page_by_seq(retransmit_seqs[index], buffer, header);
}

uint32_t storage_retrieve_next_page(uint8_t *buffer, storage_page_header_t *header)
{
   // Retrieve the next page together with the metadata the offload stream needs to frame it
   memset(header, 0, sizeof(*header));
   header->magic = STORAGE_PAGE_MAGIC;
   header->epoch = log_epoch;
   header->first_timestamp = header->last_timestamp = STORAGE_NO_TIMESTAMP;
   if (!is_reading)
      return 0;

   uint32_t length = 0;
   bool is_last = false;
   if (reading_page == last_reading_page)
   {
      is_last = true;
      if (reading_page == current_page)
      {
         // Whatever is still buffered in RAM, described by the header fields being accumulated for it
         memcpy(buffer, cache, cache_index);
         length = cache_index;
         header->seq = next_page_seq;
         header->first_timestamp = page_first_timestamp;
         header->last_timestamp = page_last_timestamp;
         header->record_count = (uint16_t)page_record_count;
      }
   }
   else
      while (is_bad_block(reading_page))
         reading_page = log_next_block(reading_page);

   if (!length && !(is_last && (reading_page == current_page)))
   {
      // Capture the header before extract_page_payload() strips it out of the buffer
      const uint32_t position = log_page_distance(starting_page, reading_page);
      if (read_page(buffer, reading_page))
      {
         const storage_page_header_t stored = *(const storage_page_header_t*)buffer;
         length = extract_page_payload(buffer);
         if (length || page_header_valid(&stored))
         {
            header->seq = stored.seq;
            header->first_timestamp = stored.first_timestamp;
            header->last_timestamp = stored.last_timestamp;
            header->record_count = stored.record_count;
         }
         else
            header->seq = position;   // header unreadable, so identify the gap by position instead
      }
      else
         header->seq = position;
   }

   header->payload_length = (uint16_t)length;
   header->payload_crc = length ? crc32_compute(buffer, length) : 0;
   if (is_last)
      is_reading = false;
   else
      reading_page = log_next_page(reading_page);
   return length;
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
         num_bytes_retrieved = extract_page_payload(buffer);
      is_reading = false;
   }
#endif
   else
   {
      // Read the next page of memory and update the reading metadata
      while (is_bad_block(reading_page))
         reading_page = log_next_block(reading_page);
      if (read_page(buffer, reading_page))
         num_bytes_retrieved = extract_page_payload(buffer);
      reading_page = log_next_page(reading_page);
   }
   return num_bytes_retrieved;
}

#else

bool storage_init(void) { return true; }
void storage_deinit(void) {}
void storage_disable(bool disable) {}
void storage_reset_bad_block_table(void) {}
bool storage_store_experiment_details(const experiment_details_t *details) { return true; }
void storage_retrieve_experiment_details(experiment_details_t *details) { memset(details, 0, sizeof(*details)); };
void storage_store_record(uint8_t record_type, uint32_t timestamp, const void *data, uint32_t data_length) {}
void storage_flush(bool write_partial_pages) {}
bool storage_has_buffered_data(void) { return false; }
void storage_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp) {}
void storage_end_reading(void) {}
void storage_enter_maintenance_mode(void){}
void storage_exit_maintenance_mode(void) {}
uint32_t storage_retrieve_num_data_chunks(uint32_t ending_timestamp) { return 0; }
uint32_t storage_retrieve_num_data_bytes(void) { return 0; }
uint32_t storage_retrieve_next_data_chunk(uint8_t *buffer) { return 0; }
uint32_t storage_retrieve_next_page(uint8_t *buffer, storage_page_header_t *header) { (void)header; return 0; }
uint32_t storage_retrieve_page_by_seq(uint32_t seq, uint8_t *buffer, storage_page_header_t *header) { (void)seq; (void)header; return 0; }
bool storage_recover_time_anchor(uint32_t *experiment_ms, uint32_t *rtc) { (void)experiment_ms; (void)rtc; return false; }
void storage_retransmit_clear(void) {}
uint32_t storage_retransmit_add(const uint32_t *seqs, uint32_t count) { (void)seqs; (void)count; return 0; }
uint32_t storage_retransmit_count(void) { return 0; }
uint32_t storage_retransmit_total_bytes(void) { return 0; }
uint32_t storage_retrieve_retransmit_page(uint32_t index, uint8_t *buffer, storage_page_header_t *header) { (void)index; (void)header; return 0; }

#endif  // #if REVISION_ID != REVISION_APOLLO4_EVB && !defined(_TEST_NO_STORAGE)
