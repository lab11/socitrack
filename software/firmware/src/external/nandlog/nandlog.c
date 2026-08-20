// The nandlog log core. Everything chip-specific is behind nandlog_chip.h, everything platform-specific is
// behind nandlog_port.h, and the host's choices are in nandlog_conf.h.


// Header Inclusions ---------------------------------------------------------------------------------------------------

#include <string.h>
#include "nandlog.h"
#include "nandlog_chip.h"
#include "nandlog_port.h"

#if NANDLOG_HAS_HARDWARE


// Static Global Variables ---------------------------------------------------------------------------------------------

#define METADATA_RING_BLOCKS                        8

static uint8_t cache[2 * NANDLOG_MAX_PAGE_SIZE_BYTES], transfer_buffer[NANDLOG_MAX_PAGE_SIZE_BYTES];
static uint32_t page_size_bytes, pages_per_block, page_block_mask, data_bytes_per_page;
static uint32_t metadata_ring_pages, log_region_first_page, log_region_end_page, log_region_page_count;
static uint32_t seek_backtrack_limit_pages, erase_ahead_trigger_page;
static uint32_t retransmit_seqs[NANDLOG_MAX_RETRANSMIT_PAGES], retransmit_num_pages = 0;
static volatile uint32_t starting_page, current_page, reading_page, last_reading_page, cache_index, log_data_size;
static volatile bool is_reading, in_maintenance_mode, disabled, cache_overflowed, is_initialized = false, log_region_full;
static volatile uint32_t page_first_timestamp = NANDLOG_NO_TIMESTAMP, page_last_timestamp = NANDLOG_NO_TIMESTAMP;
static volatile uint32_t log_epoch, next_page_seq, page_record_count, metadata_ring_page;


// Private Helper Functions --------------------------------------------------------------------------------------------

static inline uint32_t log_wrap_page(uint32_t page)
{
   // Fold a page that has run past the end of the region back to its start
   return (page >= log_region_end_page) ? (log_region_first_page + ((page - log_region_first_page) % log_region_page_count)) : page;
}

static inline uint32_t log_next_page(uint32_t page)
{
   return log_wrap_page(page + 1);
}

static inline uint32_t log_prev_page(uint32_t page)
{
   return (page == log_region_first_page) ? (log_region_end_page - 1) : (page - 1);
}

static inline uint32_t log_next_block(uint32_t page)
{
   // First page of the block following the one containing 'page'
   return log_wrap_page((page + pages_per_block) & page_block_mask);
}

static inline uint32_t log_page_distance(uint32_t from, uint32_t to)
{
   // Forward distance from one page to another, accounting for wrap
   return (to >= from) ? (to - from) : (log_region_page_count - (from - to));
}


static bool transfer_block(uint32_t source, uint32_t destination, uint32_t num_pages)
{
   for (uint32_t i = 0, page = source; i < num_pages; ++i, ++page, ++destination)
   {
      if (!nandlog_chip_read_page(transfer_buffer, page))
         memset(transfer_buffer, 0xFF, page_size_bytes);
      if (!nandlog_chip_write_page(transfer_buffer, destination))
         return false;
   }
   return true;
}

static void erase_page_range(uint32_t starting_page, uint32_t ending_page)
{
   // Iterate through all blocks to be erased, retiring any that will not take an erase
   ending_page &= page_block_mask;
   starting_page &= page_block_mask;
   const uint8_t num_iterations = (starting_page <= ending_page) ? 1 : 2;
   uint32_t end = (starting_page <= ending_page) ? ending_page : (log_region_end_page - 1);
   for (uint8_t i = 0; i < num_iterations; ++i)
   {
      for (uint32_t page = starting_page; page <= end; page += pages_per_block)
         if (!nandlog_chip_erase_block(page))
            nandlog_chip_mark_bad_block(page);
      starting_page = 0;
      end = ending_page;
   }
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

static bool page_header_valid(const nandlog_page_header_t *header)
{
   // A page is trustworthy only if it carries the magic and its header checksums correctly
   return (header->magic == NANDLOG_PAGE_MAGIC) &&
          (header->header_crc == crc32_compute(header, NANDLOG_PAGE_HEADER_CRC_BYTES)) &&
          (header->payload_length <= data_bytes_per_page);
}

static void write_page(uint16_t data_length)
{
   // Wake the storage peripheral; each chip call opens and closes write protection for itself
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      nandlog_chip_low_power(false);
   }
   const uint32_t original_page = current_page;

   // Continue trying to write the current page to memory until successful
   bool success = false;
   while (!success)
   {
      // Build a self-describing, self-validating page: ordering metadata and time bounds in the header,
      // separate checksums over the header and the payload
      memset(transfer_buffer, 0xFF, page_size_bytes);
      nandlog_page_header_t *header = (nandlog_page_header_t*)transfer_buffer;
      header->magic = NANDLOG_PAGE_MAGIC;
      header->epoch = log_epoch;
      header->seq = next_page_seq;
      header->first_timestamp = page_first_timestamp;
      header->last_timestamp = page_last_timestamp;
      header->payload_length = data_length;
      header->record_count = (uint16_t)page_record_count;
      memcpy(transfer_buffer + sizeof(nandlog_page_header_t), cache, data_length);
      header->payload_crc = crc32_compute(transfer_buffer + sizeof(nandlog_page_header_t), data_length);
      header->header_crc = crc32_compute(header, NANDLOG_PAGE_HEADER_CRC_BYTES);

      // Add the current block to the list of bad blocks if unable to write, or if the page does not read
      // back exactly as written -- the header CRC makes that check exact rather than ECC-dependent
      if (nandlog_chip_write_page(transfer_buffer, current_page) && nandlog_chip_read_page(transfer_buffer, current_page) &&
          page_header_valid((const nandlog_page_header_t*)transfer_buffer))
         success = true;
      else
      {
         // Transfer any already-written pages in the current block to the next block
         uint32_t next_block = log_next_block(current_page);
         while (nandlog_chip_is_bad_block(next_block))
            next_block = log_next_block(next_block);

         // Erase the relocation target before transferring into it
         erase_page_range(next_block, next_block);
         transfer_block(original_page & page_block_mask, next_block, current_page & (pages_per_block - 1));
         nandlog_chip_mark_bad_block(current_page);
         current_page = next_block | (current_page & (pages_per_block - 1));
      }
   }

   if (!in_maintenance_mode)
   {
      nandlog_chip_low_power(true);
      nandlog_port_power(false);
   }
}

static void erase_ahead_of(uint32_t page)
{
   // Maintain a rolling window of erased blocks ahead of the write head
   uint32_t block = page & page_block_mask;
   for (uint32_t i = 0; i < NANDLOG_ERASE_AHEAD_BLOCKS; ++i)
   {
      block = log_next_block(block);
      while (nandlog_chip_is_bad_block(block))
         block = log_next_block(block);

      // Reaching the metadata block means the log has wrapped the entire array and memory is full
      if (block == (starting_page & page_block_mask))
         break;
      erase_page_range(block, block);
   }
}

static void erase_ahead_of_head(void)
{
   // Wake the storage peripheral around the erase, mirroring what write_page() does
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      nandlog_chip_low_power(false);
   }
   erase_ahead_of(current_page);
   if (!in_maintenance_mode)
   {
      nandlog_chip_low_power(true);
      nandlog_port_power(false);
   }
}

static void advance_write_head(void)
{
   // Step to the next good page
   current_page = log_next_page(current_page);
   while (nandlog_chip_is_bad_block(current_page))
      current_page = log_next_block(current_page);

   // Wrapping onto the first page of the epoch means every usable page has been consumed
   if (current_page == starting_page)
      log_region_full = true;

   // Top up the erased window from the middle of each block to separate block
   // erases from page writes
   if ((current_page & (pages_per_block - 1)) == erase_ahead_trigger_page)
      erase_ahead_of_head();
}

static void commit_current_page(void)
{
   // Write everything buffered as one page and reset the page-assembly state
   if (cache_index)
   {
      write_page((uint16_t)cache_index);
      ++next_page_seq;
      cache_index = page_record_count = 0;
      page_first_timestamp = page_last_timestamp = NANDLOG_NO_TIMESTAMP;
      cache_overflowed = false;
      advance_write_head();
   }
}

static bool meta_header_valid(const nandlog_meta_header_t *header)
{
   return (header->magic == NANDLOG_META_MAGIC) &&
          (header->header_crc == crc32_compute(header, NANDLOG_META_HEADER_CRC_BYTES)) &&
          (header->details_length <= NANDLOG_MAX_METADATA_BYTES) &&
          (header->log_start_page >= log_region_first_page) && (header->log_start_page < log_region_end_page);
}

static bool find_newest_metadata(uint32_t *ring_page, nandlog_meta_header_t *newest)
{
   // Scan the metadata ring and select the highest valid epoch
   bool found = false;
   for (uint32_t page = 0; page < metadata_ring_pages; ++page)
   {
      if (nandlog_chip_is_bad_block(page) || !nandlog_chip_read_page(transfer_buffer, page))
         continue;
      const nandlog_meta_header_t *header = (const nandlog_meta_header_t*)transfer_buffer;
      if (!meta_header_valid(header))
         continue;
      if (header->details_crc != crc32_compute(transfer_buffer + sizeof(nandlog_meta_header_t), header->details_length))
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
   uint32_t low = 0, high = log_region_page_count;
   while (low < high)
   {
      const uint32_t mid = low + ((high - low) / 2);
      const uint32_t page = log_wrap_page(log_start_page + mid);
      bool belongs = false;
      if (!nandlog_chip_is_bad_block(page) && nandlog_chip_read_page(transfer_buffer, page))
      {
         const nandlog_page_header_t *header = (const nandlog_page_header_t*)transfer_buffer;
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
      if (nandlog_chip_read_page(transfer_buffer, last))
      {
         const nandlog_page_header_t *header = (const nandlog_page_header_t*)transfer_buffer;
         if (page_header_valid(header) && (header->epoch == epoch))
            *head_seq = header->seq + 1;
      }
   }
   return log_wrap_page(log_start_page + low);
}

static uint32_t validated_payload_length(const uint8_t *page)
{
   // Payload length is only meaningful once the header it lives in has been checksummed
   const nandlog_page_header_t *header = (const nandlog_page_header_t*)page;
   return page_header_valid(header) ? header->payload_length : 0;
}

static uint32_t extract_page_payload(uint8_t *page)
{
   // Validate a page read into 'page' and strip its header in place, leaving only verified payload bytes
   const nandlog_page_header_t *header = (const nandlog_page_header_t*)page;
   if (!page_header_valid(header))
      return 0;
   const uint32_t data_length = header->payload_length;
   if (data_length && (header->payload_crc != crc32_compute(page + sizeof(nandlog_page_header_t), data_length)))
      return 0;
   memmove(page, page + sizeof(nandlog_page_header_t), data_length);
   return data_length;
}


static void resolve_geometry(void)
{
   // The chip is the only thing that knows its own shape, so the log asks rather than assumes
   const nandlog_geometry_t *geometry = nandlog_chip_geometry();
   page_size_bytes = geometry->page_size_bytes;
   pages_per_block = geometry->pages_per_block;
   page_block_mask = ~(pages_per_block - 1);
   data_bytes_per_page = page_size_bytes - sizeof(nandlog_page_header_t);
   metadata_ring_pages = METADATA_RING_BLOCKS * pages_per_block;
   log_region_first_page = metadata_ring_pages;
   log_region_end_page = (geometry->block_count - geometry->reserved_blocks) * pages_per_block;
   log_region_page_count = log_region_end_page - log_region_first_page;
   seek_backtrack_limit_pages = pages_per_block;
   erase_ahead_trigger_page = pages_per_block / 2;
}

static bool probe_epoch_page(uint32_t index, uint32_t page_count, nandlog_page_header_t *header, uint32_t *found_index)
{
   // First valid current-epoch page at or after 'index'. A probe can land inside a block that was skipped
   // as bad, so step forward rather than concluding the epoch has ended
   for (uint32_t i = index; i < page_count; ++i)
   {
      const uint32_t page = log_wrap_page(starting_page + i);
      if (!nandlog_chip_is_bad_block(page) && nandlog_chip_read_page(transfer_buffer, page))
      {
         const nandlog_page_header_t *candidate = (const nandlog_page_header_t*)transfer_buffer;
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
      nandlog_page_header_t header;
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
      while (result && (steps < seek_backtrack_limit_pages))
      {
         nandlog_page_header_t header;
         uint32_t found = 0;
         if (!probe_epoch_page(result - 1, page_count, &header, &found) || (found >= result) ||
             (header.last_timestamp < timestamp))
            break;
         result = found;
         ++steps;
      }
      if (steps >= seek_backtrack_limit_pages)
      {
         // Still finding qualifying pages at the limit means the log is badly out of order; fall back to the whole range
         nandlog_port_log("WARNING: Log timestamps are badly non-monotonic; ignoring the requested start time\n");
         result = 0;
      }
   }
   return result;
}


// Public API Functions ------------------------------------------------------------------------------------------------

bool nandlog_probe(void)
{
   // Presence only: bring the port up, ask the part to identify itself, and put everything back to sleep
   if (!nandlog_port_init())
      return false;
   const bool chip_present = nandlog_chip_probe();
   nandlog_chip_low_power(true);
   nandlog_port_power(false);
   nandlog_port_write_enable(false);
   return chip_present;
}

bool nandlog_init(void)
{
   // Return if already initialized
   if (is_initialized)
      return true;

   // Everything platform-specific about reaching the chip lives behind this call
   is_reading = in_maintenance_mode = disabled = false;
   if (!nandlog_port_init())
      return false;

   // Resolve the log region from the geometry the chip reports before anything can consult it
   resolve_geometry();

   // Confirm the chip is there and answering before anything is asked of it
   if (nandlog_chip_probe())
   {
      // Configure the chip and load or build its bad-block table
      nandlog_chip_init();

      // Recover the current epoch from the metadata ring, then locate the write head within it
      uint32_t metadata_page = 0;
      nandlog_meta_header_t metadata;
      memset(cache, 0, sizeof(cache));
      cache_index = last_reading_page = log_data_size = page_record_count = 0;
      page_first_timestamp = page_last_timestamp = NANDLOG_NO_TIMESTAMP;
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
         log_epoch = next_page_seq = 0;
         metadata_ring_page = metadata_ring_pages;
         starting_page = log_region_first_page;
         while (nandlog_chip_is_bad_block(starting_page))
            starting_page = log_next_block(starting_page);
         current_page = starting_page;
         log_region_full = false;
      }

      // Unconditionally re-establish the erased window ahead of the head
      erase_ahead_of(current_page);

      // Put the storage peripheral into Deep Sleep mode and disable writes
      nandlog_chip_low_power(true);
      nandlog_port_power(false);
      nandlog_port_write_enable(false);
      is_initialized = true;
      return true;
   }
   else
      return false;
}

void nandlog_deinit(void)
{
   // Only continue if initialized
   if (!is_initialized)
      return;

   // Disable all chip communications
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      nandlog_chip_low_power(false);
   }
   nandlog_port_deinit();
   is_reading = in_maintenance_mode = is_initialized = false;
}

uint32_t nandlog_data_bytes_per_page(void)
{
   // Answered from the chip rather than the cached copy so it is correct before nandlog_init() has run
   return nandlog_chip_geometry()->page_size_bytes - sizeof(nandlog_page_header_t);
}

uint32_t nandlog_epoch_page_count(void)
{
   // Physical pages spanned by the current epoch
   return log_region_full ? log_region_page_count : log_page_distance(starting_page, current_page);
}

bool nandlog_is_reading(void)
{
   return is_reading;
}

void nandlog_disable(bool disable)
{
   // Set the storage disabled flag
   disabled = disable;
}

void nandlog_reset_bad_block_table(void)
{
   // RECOVERY UTILITY. Discards the persisted bad-block table so it is rebuilt on the next boot. How much of
   // that is possible depends on the part, so the chip driver decides and reports whether it succeeded
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      nandlog_chip_low_power(false);
   }
   const bool cleared = nandlog_chip_reset_bad_blocks();
   if (!in_maintenance_mode)
   {
      nandlog_chip_low_power(true);
      nandlog_port_power(false);
   }
   if (cleared)
      nandlog_port_log("INFO: Bad-block table erased and verified clear; rebuilt on next boot\n");
   else
      nandlog_port_log("ERROR: Bad-block table NOT cleared\n");
}

bool nandlog_store_metadata(const void *blob, uint16_t length)
{
   // Ensure that the metadata length is within the expected bounds
   if (length > NANDLOG_MAX_METADATA_BYTES)
      return false;

   // Refuse loudly rather than silently discarding the write
   if (!in_maintenance_mode)
   {
      nandlog_port_log("ERROR: Refusing to store metadata details outside of maintenance mode\n");
      return false;
   }

   // Begin a new epoch whose log continues the wear sweep from the current head
   const uint32_t new_epoch = log_epoch + 1;
   uint32_t new_log_start = log_next_block(current_page);
   while (nandlog_chip_is_bad_block(new_log_start))
      new_log_start = log_next_block(new_log_start);

   // Establish the erased window before any data can land in it
   erase_page_range(new_log_start, new_log_start);
   erase_ahead_of(new_log_start);

   // Commit with a single metadata page program
   bool success = false;
   for (uint32_t attempt = 0; !success && (attempt < metadata_ring_pages); ++attempt)
   {
      const uint32_t slot = (metadata_ring_page >= metadata_ring_pages) ? 0 : ((metadata_ring_page + 1) % metadata_ring_pages);
      metadata_ring_page = slot;
      if (nandlog_chip_is_bad_block(slot))
         continue;

      // Erase on entry to each block of the ring, so a slot is always clean before it is programmed
      if ((slot & (pages_per_block - 1)) == 0)
         erase_page_range(slot, slot);
      memset(transfer_buffer, 0xFF, page_size_bytes);
      nandlog_meta_header_t *header = (nandlog_meta_header_t*)transfer_buffer;
      header->magic = NANDLOG_META_MAGIC;
      header->epoch = new_epoch;
      header->log_start_page = new_log_start;
      header->created_timestamp = 0;   // caller stamps its own metadata; the log does not read a clock
      header->details_length = (uint16_t)length;
      header->format_version = NANDLOG_FORMAT_VERSION;
      header->reserved = 0xFFFFFFFF;
      memcpy(transfer_buffer + sizeof(nandlog_meta_header_t), blob, length);
      header->details_crc = crc32_compute(transfer_buffer + sizeof(nandlog_meta_header_t), length);
      header->header_crc = crc32_compute(header, NANDLOG_META_HEADER_CRC_BYTES);

      success = nandlog_chip_write_page(transfer_buffer, slot) &&
                nandlog_chip_read_page(transfer_buffer, slot) &&
                meta_header_valid((const nandlog_meta_header_t*)transfer_buffer);
   }
   if (!success)
   {
      nandlog_port_log("ERROR: Unable to write metadata to any slot in the ring\n");
      return false;
   }

   // The new epoch is now live
   log_epoch = new_epoch;
   log_region_full = false;
   starting_page = current_page = new_log_start;
   next_page_seq = cache_index = page_record_count = 0;
   page_first_timestamp = page_last_timestamp = NANDLOG_NO_TIMESTAMP;
   return true;
}

void nandlog_retrieve_metadata(void *blob, uint16_t length)
{
   // Wake up the chip if need be
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      nandlog_chip_low_power(false);
   }

   // Metadata lives in a ring, validated by its own CRC, rather than in the log itself
   memset(blob, 0, length);
   if ((metadata_ring_page < metadata_ring_pages) && nandlog_chip_read_page(transfer_buffer, metadata_ring_page))
   {
      const nandlog_meta_header_t *header = (const nandlog_meta_header_t*)transfer_buffer;
      const uint8_t *stored = transfer_buffer + sizeof(nandlog_meta_header_t);
      if (meta_header_valid(header) && (header->details_crc == crc32_compute(stored, header->details_length)))
         memcpy(blob, stored, (header->details_length < length) ? header->details_length : length);
   }

   // Put the chip back to sleep if need be
   if (!in_maintenance_mode)
   {
      nandlog_chip_low_power(true);
      nandlog_port_power(false);
   }
}

void nandlog_store_record(uint8_t record_type, uint32_t timestamp, const void *data, uint32_t data_length)
{
   // Add a complete record to the page currently being assembled if storage is not disabled
   if (disabled)
      return;

   const uint32_t record_length = 1 + sizeof(timestamp) + data_length;
   if (record_length > data_bytes_per_page)
   {
      // A single record larger than a page cannot be represented; drop it rather than corrupt the stream
      cache_overflowed = true;
      return;
   }

   // A timestamp that precedes the page's current end means the time base moved backwards: either commit the
   // page here or nudge this record forward
   bool time_moved_backwards = false;
   if ((page_last_timestamp != NANDLOG_NO_TIMESTAMP) && (timestamp < page_last_timestamp))
   {
      if ((page_last_timestamp - timestamp) <= NANDLOG_TIMESTAMP_TOLERANCE_MS)
         timestamp = page_last_timestamp;
      else
         time_moved_backwards = true;
   }

   // Records are never split across pages, so a record that does not fit in what remains of the current
   // page commits that page and starts the next one
   if (((cache_index + record_length) > data_bytes_per_page) || time_moved_backwards)
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
   if (page_first_timestamp == NANDLOG_NO_TIMESTAMP)
      page_first_timestamp = timestamp;
   page_last_timestamp = timestamp;
   ++page_record_count;
}

void nandlog_flush(bool write_partial_pages)
{
   // Do not flush if currently reading or if the memory is full
   if (disabled || is_reading || log_region_full)
      return;

   // A page is committed as soon as the next record will not fit, so the only reason to
   // write here is an explicit request to commit a partial page: a timed flush or shutdown
   if (write_partial_pages || (cache_index >= data_bytes_per_page))
      commit_current_page();
}

bool nandlog_has_buffered_data(void)
{
   // Reports whether any collected data is sitting unwritten in RAM
   return (cache_index != 0);
}

void nandlog_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp)
{
   // Establish the span to be read
   reading_page = starting_page;
   last_reading_page = current_page;
   is_reading = in_maintenance_mode;
   const uint32_t page_count = nandlog_epoch_page_count();
   if (starting_timestamp)
   {
      const uint32_t index = seek_page_for_timestamp(starting_timestamp, page_count, true);
      reading_page = (index < page_count) ? log_wrap_page(starting_page + index) : current_page;
   }
   if (ending_timestamp)
      last_reading_page = log_wrap_page(starting_page + seek_page_for_timestamp(ending_timestamp, page_count, false));
}

void nandlog_end_reading(void)
{
   last_reading_page = 0;
   is_reading = false;
}

void nandlog_enter_maintenance_mode(void)
{
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      nandlog_chip_low_power(false);
   }
   in_maintenance_mode = true;
}

void nandlog_exit_maintenance_mode(void)
{
   nandlog_end_reading();
   if (in_maintenance_mode)
   {
      nandlog_chip_low_power(true);
      nandlog_port_power(false);
   }
   in_maintenance_mode = false;
}

uint32_t nandlog_retrieve_num_data_chunks(void)
{
   // Ensure that we are in reading mode
   if (!is_reading)
      return 0;

   // Sum the payload bytes across the selected span
   log_data_size = 0;
   for (uint32_t page = reading_page; page != last_reading_page; )
   {
      if (nandlog_chip_is_bad_block(page))
         page = log_next_block(page);
      else
      {
         if (nandlog_chip_read_page(transfer_buffer, page))
            log_data_size += validated_payload_length(transfer_buffer);
         page = log_next_page(page);
      }
   }

   // The trailing chunk is whatever is still buffered in RAM
   if (last_reading_page == current_page)
      return log_page_distance(reading_page, last_reading_page) + (cache_index ? 1 : 0);
   return 1 + log_page_distance(reading_page, last_reading_page);
}

uint32_t nandlog_retrieve_num_data_bytes(void)
{
   // Return the total number of log data bytes available (must be called after "nandlog_retrieve_num_data_chunks()")
   return (last_reading_page == current_page) ? (log_data_size + cache_index) : log_data_size;
}

uint32_t nandlog_retrieve_next_data_chunk(uint8_t *buffer)
{
   // Ensure that we are in reading mode
   if (!is_reading)
      return 0;

   // Determine if a full page of memory is available to read
   uint32_t num_bytes_retrieved = 0;
   if (reading_page == last_reading_page)
   {
      if (reading_page == current_page)
      {
         // Return the valid available bytes
         memcpy(buffer, cache, cache_index);
         num_bytes_retrieved = cache_index;
      }
      else if (nandlog_chip_read_page(buffer, reading_page))
         num_bytes_retrieved = extract_page_payload(buffer);
      is_reading = false;
   }
   else
   {
      // Read the next page of memory and update the reading metadata
      while (nandlog_chip_is_bad_block(reading_page))
         reading_page = log_next_block(reading_page);
      if (nandlog_chip_read_page(buffer, reading_page))
         num_bytes_retrieved = extract_page_payload(buffer);
      reading_page = log_next_page(reading_page);
   }
   return num_bytes_retrieved;
}

uint32_t nandlog_retrieve_next_page(uint8_t *buffer, nandlog_page_header_t *header)
{
   // Retrieve the next page together with the metadata the offload stream needs to frame it
   memset(header, 0, sizeof(*header));
   header->magic = NANDLOG_PAGE_MAGIC;
   header->epoch = log_epoch;
   header->first_timestamp = header->last_timestamp = NANDLOG_NO_TIMESTAMP;
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
      while (nandlog_chip_is_bad_block(reading_page))
         reading_page = log_next_block(reading_page);

   if (!length && !(is_last && (reading_page == current_page)))
   {
      // Capture the header before extract_page_payload() strips it out of the buffer
      const uint32_t position = log_page_distance(starting_page, reading_page);
      if (nandlog_chip_read_page(buffer, reading_page))
      {
         const nandlog_page_header_t stored = *(const nandlog_page_header_t*)buffer;
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

uint32_t nandlog_retrieve_page_by_seq(uint32_t seq, uint8_t *buffer, nandlog_page_header_t *header)
{
   // Fetch one specific page so the host can ask for the ones it lost
   memset(header, 0, sizeof(*header));
   header->magic = NANDLOG_PAGE_MAGIC;
   header->epoch = log_epoch;
   header->seq = seq;
   header->first_timestamp = header->last_timestamp = NANDLOG_NO_TIMESTAMP;
   if (!is_reading)
      return 0;

   const uint32_t page_count = nandlog_epoch_page_count();
   uint32_t low = 0, high = page_count;
   while (low < high)
   {
      const uint32_t mid = low + ((high - low) / 2);
      nandlog_page_header_t probe;
      uint32_t found = 0;
      if (!probe_epoch_page(mid, page_count, &probe, &found))
      {
         high = mid;                      // nothing valid from here on
         continue;
      }
      if (probe.seq == seq)
      {
         if (!nandlog_chip_read_page(buffer, log_wrap_page(starting_page + found)))
            return 0;
         const nandlog_page_header_t stored = *(const nandlog_page_header_t*)buffer;
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

uint32_t nandlog_read_recent_page(uint32_t pages_back, uint8_t *buffer, nandlog_page_header_t *header, bool *end_of_epoch)
{
   // Walk backwards from the write head, newest page first, deliberately record-agnostic
   // Returns the payload length or zero if that page could not be read
   if (end_of_epoch)
      *end_of_epoch = true;
   if (current_page == starting_page)
      return 0;
   uint32_t page = current_page;
   for (uint32_t step = 0; step <= pages_back; ++step)
      page = log_prev_page(page);

   // Wake up the chip if need be
   if (!in_maintenance_mode)
   {
      nandlog_port_power(true);
      nandlog_chip_low_power(false);
   }

   uint32_t length = 0;
   if (!nandlog_chip_is_bad_block(page) && nandlog_chip_read_page(transfer_buffer, page))
   {
      const nandlog_page_header_t *stored = (const nandlog_page_header_t*)transfer_buffer;
      if (page_header_valid(stored) && (stored->epoch == log_epoch))
      {
         if (header)
            memcpy(header, stored, sizeof(*header));
         length = extract_page_payload(transfer_buffer);
         if (length && buffer)
            memcpy(buffer, transfer_buffer, length);
         if (end_of_epoch)
            *end_of_epoch = (page == starting_page);
      }
   }
   else if (end_of_epoch)
      *end_of_epoch = false;

   // Put the chip back to sleep if need be
   if (!in_maintenance_mode)
   {
      nandlog_chip_low_power(true);
      nandlog_port_power(false);
   }
   return length;
}

void nandlog_retransmit_clear(void)
{
   retransmit_num_pages = 0;
}

uint32_t nandlog_retransmit_add(const uint32_t *seqs, uint32_t count)
{
   // Accumulate across commands: one offloading write cannot name every page a long transfer might have lost
   for (uint32_t i = 0; (i < count) && (retransmit_num_pages < NANDLOG_MAX_RETRANSMIT_PAGES); ++i)
      retransmit_seqs[retransmit_num_pages++] = seqs[i];
   return retransmit_num_pages;
}

uint32_t nandlog_retransmit_count(void)
{
   return retransmit_num_pages;
}

uint32_t nandlog_retransmit_total_bytes(void)
{
   // The host sizes its receive buffer from the stream header, so this total has to be exact rather than an upper bound
   uint32_t total = 0;
   for (uint32_t i = 0; i < retransmit_num_pages; ++i)
   {
      nandlog_page_header_t header;
      total += nandlog_retrieve_page_by_seq(retransmit_seqs[i], transfer_buffer, &header);
   }
   return total;
}

uint32_t nandlog_retrieve_retransmit_page(uint32_t index, uint8_t *buffer, nandlog_page_header_t *header)
{
   // Answer the index-th requested page. A page that is still unreadable comes back as a zero-length frame
   // carrying the requested sequence number, so the host can tell "still missing" from "never answered"
   if (index >= retransmit_num_pages)
   {
      memset(header, 0, sizeof(*header));
      header->magic = NANDLOG_PAGE_MAGIC;
      header->epoch = log_epoch;
      header->first_timestamp = header->last_timestamp = NANDLOG_NO_TIMESTAMP;
      return 0;
   }
   return nandlog_retrieve_page_by_seq(retransmit_seqs[index], buffer, header);
}

#else

bool nandlog_probe(void) { return true; }
uint32_t nandlog_epoch_page_count(void) { return 0; }
bool nandlog_is_reading(void) { return false; }
bool nandlog_init(void) { return true; }
uint32_t nandlog_data_bytes_per_page(void) { return NANDLOG_MAX_DATA_BYTES_PER_PAGE; }
void nandlog_deinit(void) {}
void nandlog_disable(bool disable) {}
void nandlog_reset_bad_block_table(void) {}
bool nandlog_store_metadata(const void *blob, uint16_t length) { (void)blob; (void)length; return true; }
void nandlog_retrieve_metadata(void *blob, uint16_t length) { memset(blob, 0, length); };
void nandlog_store_record(uint8_t record_type, uint32_t timestamp, const void *data, uint32_t data_length) {}
void nandlog_flush(bool write_partial_pages) {}
bool nandlog_has_buffered_data(void) { return false; }
void nandlog_begin_reading(uint32_t starting_timestamp, uint32_t ending_timestamp) {}
void nandlog_end_reading(void) {}
void nandlog_enter_maintenance_mode(void){}
void nandlog_exit_maintenance_mode(void) {}
uint32_t nandlog_retrieve_num_data_chunks(void) { return 0; }
uint32_t nandlog_retrieve_num_data_bytes(void) { return 0; }
uint32_t nandlog_retrieve_next_data_chunk(uint8_t *buffer) { return 0; }
uint32_t nandlog_retrieve_next_page(uint8_t *buffer, nandlog_page_header_t *header) { (void)header; return 0; }
uint32_t nandlog_retrieve_page_by_seq(uint32_t seq, uint8_t *buffer, nandlog_page_header_t *header) { (void)seq; (void)header; return 0; }
uint32_t nandlog_read_recent_page(uint32_t pages_back, uint8_t *buffer, nandlog_page_header_t *header, bool *end_of_epoch) { (void)pages_back; (void)buffer; (void)header; if (end_of_epoch) *end_of_epoch = true; return 0; }
void nandlog_retransmit_clear(void) {}
uint32_t nandlog_retransmit_add(const uint32_t *seqs, uint32_t count) { (void)seqs; (void)count; return 0; }
uint32_t nandlog_retransmit_count(void) { return 0; }
uint32_t nandlog_retransmit_total_bytes(void) { return 0; }
uint32_t nandlog_retrieve_retransmit_page(uint32_t index, uint8_t *buffer, nandlog_page_header_t *header) { (void)index; (void)header; return 0; }

#endif  // #if NANDLOG_HAS_HARDWARE
