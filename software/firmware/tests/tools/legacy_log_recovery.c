// Legacy Log Recovery: a READ-ONLY raw dump of the storage flash, for tags still carrying logs written by the
// firmware that predates the nandlog redesign (anything before cf7249d5).
//
// That firmware kept no durable record of where its log was. It found the log at boot by pattern-matching
// "META" and "DA" markers (see doc/Storage_Redesign.md §1), and when that search failed it wrote a fresh,
// empty metadata page and carried on -- which is how a tag ends up reporting that it has no log while
// every page of it is still sitting on the flash. The data is not gone; only the pointer to it is.
//
// So this firmware does not try to find the log. It reads every page of the part, as it is, and sends each
// one that is not erased to a host, which does the reconstruction (management/dashboard/legacy_recovery.py).
// Keeping the intelligence on the host means a better heuristic never requires reflashing a tag, and the
// raw dump it produces can be archived and re-analysed long after the tag itself has been wiped.
//
// NOTHING HERE WRITES TO THE ARRAY. The only commands this file can issue are READ ID, READ and WRITE STATUS
// REGISTER, PAGE DATA READ, READ, and (Winbond only) READ BBM LUT. There is no WRITE ENABLE anywhere, so
// the part's write latch can never be set and a program or erase could not take effect even if one were
// sent. The status-register writes put the part into the same locked, ECC-on configuration the old
// firmware itself applied at every boot, and the write-protect pin is then held low for good measure.
// Deliberately not called: nandlog_chip_init(), which on a part it believes is new programs an OTP marker
// and writes a bad-block table.
//
// Transports:
//    make log_recovery          USB CDC, driven by the host (revisions O and P; M and N have no USB)
//    make log_recovery_segger   SEGGER RTT channel 1 via a J-Link, starts by itself (any revision)
//
// Both carry the same frame stream, described in the "Wire Format" section below.


// Header Inclusions ---------------------------------------------------------------------------------------------------

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "nandlog_port.h"

#ifdef LEGACY_RECOVERY_HOST

// Built on a host against the nandlog SPI simulator; the harness supplies these
#include <stdio.h>
extern bool legacy_recovery_host_emit(const void *data, uint32_t length);
extern void legacy_recovery_host_uid(uint8_t *uid);
#define print(...)                                  printf(__VA_ARGS__)
#define RECOVERY_HW_REVISION                        0
#define RECOVERY_FW_REVISION                        "host simulation"
#define RECOVERY_TRANSPORT                          TRANSPORT_HOST

#else

#include "app_tasks.h"
#include "led.h"
#include "logging.h"
#include "system.h"
#ifdef __USE_SEGGER__
#define RECOVERY_TRANSPORT                          TRANSPORT_RTT
#else
#include "tusb.h"
#include "usb.h"
#define RECOVERY_TRANSPORT                          TRANSPORT_USB
#if REVISION_ID <= REVISION_N
#error "Revisions M and N have no USB port: build 'make log_recovery_segger' and read the dump over a J-Link instead"
#endif
#endif
#define RECOVERY_HW_REVISION                        REVISION_ID
#define RECOVERY_FW_REVISION                        FW_REVISION

#endif  // #ifdef LEGACY_RECOVERY_HOST


// Wire Format ---------------------------------------------------------------------------------------------------------
//
// A stream of frames, all multi-byte fields little-endian:
//
//    "TTRC"  type:u8  flags:u8  length:u16  payload[length]  crc32:u32
//
// The CRC (IEEE 802.3, as zlib.crc32) covers type through the end of the payload. The sync word lets a host
// that attaches part-way through, or loses bytes, find the next frame; RTT in particular gives no guarantee
// the reader was attached from the first byte.
//
// A dump is INFO, then (Winbond parts) LUT, then one PAGE frame per page that is not erased, with a PROGRESS
// frame every PROGRESS_INTERVAL_PAGES pages scanned, then END. A page that is absent from the dump was read
// successfully and found to be all 0xFF.

#define FRAME_SYNC                                  "TTRC"
#define FRAME_SYNC_LENGTH                           4
#define PROTOCOL_VERSION                            1

#define FRAME_INFO                                  0x01
#define FRAME_LUT                                   0x02
#define FRAME_PAGE                                  0x03
#define FRAME_PROGRESS                              0x04
#define FRAME_END                                   0x05
#define FRAME_ERROR                                 0x06

#define PAGE_FLAG_UNCORRECTABLE                     0x01    // every read reported an uncorrectable ECC error

#define TRANSPORT_HOST                              0
#define TRANSPORT_USB                               1
#define TRANSPORT_RTT                               2

#define PROGRESS_INTERVAL_PAGES                     4096
#define MAX_READS_PER_PAGE                          4       // an uncorrectable page is read this many times

// Host commands, USB only
#define COMMAND_INFO                                'I'     // no arguments; replies INFO (and LUT)
#define COMMAND_DUMP                                'D'     // first_page:u32 page_count:u32 (0 = to the end)

typedef struct __attribute__ ((__packed__))
{
   uint16_t protocol_version;
   uint8_t hw_revision;              // REVISION_ID this firmware was built for (0x13 = M ... 0x16 = P)
   uint8_t chip;                     // CHIP_* below, or CHIP_UNKNOWN when no supported part answered
   uint8_t chip_id[4];               // identity bytes as read, for a part this firmware does not recognise
   uint32_t page_size_bytes, spare_size_bytes, pages_per_block, block_count, reserved_blocks;
   uint8_t uid[6];                   // this tag's EUI, least significant byte first
   uint8_t status_registers[3];      // SR1..SR3 as found, before this firmware configured the part
   uint8_t transport;
   char firmware[32];
} info_frame_t;

typedef struct __attribute__ ((__packed__))
{
   uint32_t page;
   uint8_t status_register_3;        // after the read that is being reported: ECC and error bits, raw
   uint8_t reads;                    // how many reads it took, 1 unless the first was uncorrectable
   uint16_t reserved;
} page_frame_header_t;

typedef struct __attribute__ ((__packed__))
{
   uint32_t pages_scanned, pages_sent, pages_uncorrectable, elapsed_ms;
} progress_frame_t;


// Supported Parts -----------------------------------------------------------------------------------------------------
//
// The geometry and configuration each part had under the OLD firmware, which is what matters here: the old
// storage.c used the same values as today's chip drivers, including the size of the reserve at the top.
// The part is identified at run time rather than taken from the build revision, so a tag whose revision was
// guessed wrong still dumps correctly.

#define CHIP_UNKNOWN                                0
#define CHIP_W25N01GW                               1       // revision M
#define CHIP_AS5F18G04SND                           2       // revisions N, O and P

#define COMMAND_READ_DEVICE_ID                      0x9F
#define COMMAND_READ_STATUS_REGISTER                0x0F
#define COMMAND_WRITE_STATUS_REGISTER               0x1F
#define COMMAND_PAGE_DATA_READ                      0x13
#define COMMAND_READ                                0x03
#define COMMAND_READ_BBM_LUT                        0xA5

#define STATUS_REGISTER_1                           0xA0
#define STATUS_REGISTER_2                           0xB0
#define STATUS_REGISTER_3                           0xC0

#define STATUS_PAGE_FATAL_ERROR                     0b00100000
#define STATUS_BUSY                                 0b00000001
#define PROTECT_ALL                                 0b01111110

#define MAX_PAGE_SIZE_BYTES                         4096
#define W25N_LUT_BYTES                              (20 * 4)

typedef struct
{
   uint8_t chip;
   const char *name;
   uint32_t page_size_bytes, spare_size_bytes, pages_per_block, block_count, reserved_blocks;
   uint8_t config_register_2;        // ECC on, buffered reads: exactly what the old firmware set
} chip_description_t;

static const chip_description_t supported_chips[] = {
   { CHIP_W25N01GW,     "W25N01GW",     2048,  64, 64, 1024, 40, 0b00011001 },
   { CHIP_AS5F18G04SND, "AS5F18G04SND", 4096, 256, 64, 4096, 80, 0b00010000 },
};


// Static Global Variables ---------------------------------------------------------------------------------------------

static const chip_description_t *chip;
static uint8_t chip_id[4], found_status_registers[3];
static uint8_t page_buffer[MAX_PAGE_SIZE_BYTES] __attribute__ ((aligned (4)));   // scanned a word at a time
static uint8_t frame_buffer[FRAME_SYNC_LENGTH + 4 + sizeof(page_frame_header_t) + MAX_PAGE_SIZE_BYTES + 4];
static uint32_t crc_table[256];


// Transport -----------------------------------------------------------------------------------------------------------

static uint32_t now_ms(void)
{
#ifdef LEGACY_RECOVERY_HOST
   return 0;
#else
   return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
#endif
}

static bool emit(const void *data, uint32_t length)
{
   // False means the host has gone away and the dump should stop
#if RECOVERY_TRANSPORT == TRANSPORT_HOST
   return legacy_recovery_host_emit(data, length);
#elif RECOVERY_TRANSPORT == TRANSPORT_RTT
   // The channel was configured SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL by logging_init(), so this waits for the
   // J-Link to drain it rather than dropping anything
   SEGGER_RTT_Write(1, data, length);
   return true;
#else
   const uint8_t *bytes = (const uint8_t*)data;
   while (length)
   {
      if (!tud_cdc_connected())
         return false;
      const uint32_t written = tud_cdc_write_available() ? tud_cdc_write(bytes, length) : 0;
      if (written)
      {
         bytes += written;
         length -= written;
      }
      else
      {
         // A full FIFO must have a transfer draining it, or nothing ever will
         tud_cdc_write_flush();
         taskYIELD();
      }
   }
   return true;
#endif
}

static void emit_flush(void)
{
   // TinyUSB only starts a transfer by itself once a full packet is queued, so without this the tail of a frame
   // sits in the FIFO for as long as nothing follows it -- which, past the end of a log, is until the scan ends
#if RECOVERY_TRANSPORT == TRANSPORT_USB
   tud_cdc_write_flush();
#endif
}


// Framing -------------------------------------------------------------------------------------------------------------

static void crc32_init(void)
{
   for (uint32_t i = 0; i < 256; ++i)
   {
      uint32_t value = i;
      for (uint32_t bit = 0; bit < 8; ++bit)
         value = (value & 1) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
      crc_table[i] = value;
   }
}

static uint32_t crc32(const uint8_t *data, uint32_t length)
{
   uint32_t crc = 0xFFFFFFFFu;
   while (length--)
      crc = crc_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
   return crc ^ 0xFFFFFFFFu;
}

static bool send_frame(uint8_t type, uint8_t flags, const void *header, uint32_t header_length, const void *body, uint32_t body_length)
{
   // Assembled in one buffer so the frame goes out as a single transport write
   const uint16_t payload_length = (uint16_t)(header_length + body_length);
   uint8_t *out = frame_buffer;
   memcpy(out, FRAME_SYNC, FRAME_SYNC_LENGTH);
   out += FRAME_SYNC_LENGTH;
   *out++ = type;
   *out++ = flags;
   memcpy(out, &payload_length, sizeof(payload_length));
   out += sizeof(payload_length);
   if (header_length)
      memcpy(out, header, header_length);
   out += header_length;
   if (body_length)
      memcpy(out, body, body_length);
   out += body_length;
   const uint32_t crc = crc32(frame_buffer + FRAME_SYNC_LENGTH, (uint32_t)(out - frame_buffer - FRAME_SYNC_LENGTH));
   memcpy(out, &crc, sizeof(crc));
   out += sizeof(crc);
   if (!emit(frame_buffer, (uint32_t)(out - frame_buffer)))
      return false;
   emit_flush();
   return true;
}

static void send_error(const char *message)
{
   print("ERROR: %s\n", message);
   send_frame(FRAME_ERROR, 0, NULL, 0, message, (uint32_t)strlen(message));
}


// Read-Only Flash Access ----------------------------------------------------------------------------------------------

static uint8_t read_register(uint8_t register_number)
{
   uint8_t value = 0;
   nandlog_port_transfer_read(COMMAND_READ_STATUS_REGISTER, &register_number, 1, &value, 1);
   return value;
}

static void write_register(uint8_t register_number, uint8_t value)
{
   // Configuration only. Nothing here can alter the array: there is no write enable, and neither value used
   // below sets a lock or OTP bit
   nandlog_port_transfer_write(COMMAND_WRITE_STATUS_REGISTER, &register_number, 1, &value, 1);
}

static bool wait_until_not_busy(void)
{
   // Half a second, as the log itself allows; a read that never completes is reported, never retried forever
   for (uint32_t polls = 0; polls < 50000; ++polls)
   {
      if ((read_register(STATUS_REGISTER_3) & STATUS_BUSY) != STATUS_BUSY)
         return true;
      nandlog_port_delay_us(10);
   }
   return false;
}

static const chip_description_t *identify_chip(void)
{
   // The two parts answer READ ID differently: the Alliance part takes an address byte and returns one byte,
   // the Winbond part returns a dummy byte then three. Each is distinct under the other's form
   static const uint8_t address = 0x01;
   for (uint32_t attempt = 0; attempt < 1000; ++attempt)
   {
      memset(chip_id, 0, sizeof(chip_id));
      nandlog_port_transfer_read(COMMAND_READ_DEVICE_ID, &address, sizeof(address), chip_id, 1);
      if (chip_id[0] == 0x8D)
         return &supported_chips[1];
      nandlog_port_transfer_read(COMMAND_READ_DEVICE_ID, NULL, 0, chip_id, sizeof(chip_id));
      if ((chip_id[1] == 0xEF) && (chip_id[2] == 0xBA) && (chip_id[3] == 0x21))
         return &supported_chips[0];
      nandlog_port_delay_ms(1);
   }
   return NULL;
}

static bool read_page(uint32_t page, uint8_t *status_register_3)
{
   // False means every read either timed out or reported an uncorrectable error; the buffer then holds what
   // the last read returned, which for an ECC failure is the raw array contents and still worth having
   const uint8_t page_address[3] = { (uint8_t)(page >> 16), (uint8_t)(page >> 8), (uint8_t)page };
   const uint8_t column_and_dummy[3] = { 0, 0, 0 };
   if (!wait_until_not_busy())
      return false;
   nandlog_port_transfer_write(COMMAND_PAGE_DATA_READ, NULL, 0, page_address, sizeof(page_address));
   if (!wait_until_not_busy())
      return false;
   nandlog_port_transfer_read(COMMAND_READ, column_and_dummy, sizeof(column_and_dummy), page_buffer, chip->page_size_bytes);
   *status_register_3 = read_register(STATUS_REGISTER_3);
   return (*status_register_3 & STATUS_PAGE_FATAL_ERROR) != STATUS_PAGE_FATAL_ERROR;
}

static bool page_is_erased(void)
{
   const uint32_t *words = (const uint32_t*)page_buffer;
   for (uint32_t i = 0; i < (chip->page_size_bytes / sizeof(uint32_t)); ++i)
      if (words[i] != 0xFFFFFFFFu)
         return false;
   return true;
}


// Recovery ------------------------------------------------------------------------------------------------------------

static bool prepare_flash(void)
{
   // Bring up the bus, then immediately assert write protection at the pin; the part is only configured once
   // it is known to be one of the two this firmware understands
   crc32_init();
   nandlog_port_init();
   nandlog_port_delay_ms(3);
   chip = identify_chip();
   if (chip)
   {
      wait_until_not_busy();
      found_status_registers[0] = read_register(STATUS_REGISTER_1);
      found_status_registers[1] = read_register(STATUS_REGISTER_2);
      found_status_registers[2] = read_register(STATUS_REGISTER_3);
      write_register(STATUS_REGISTER_1, PROTECT_ALL);
      write_register(STATUS_REGISTER_2, chip->config_register_2);
      print("Legacy Log Recovery: found %s (%u pages of %u bytes)\n", chip->name, chip->block_count * chip->pages_per_block, chip->page_size_bytes);
   }
   else
      print("Legacy Log Recovery: no supported flash part answered (ID %02X %02X %02X %02X)\n", chip_id[0], chip_id[1], chip_id[2], chip_id[3]);
   nandlog_port_write_enable(false);
   return chip != NULL;
}

static bool send_info(void)
{
   info_frame_t info;
   memset(&info, 0, sizeof(info));
   info.protocol_version = PROTOCOL_VERSION;
   info.hw_revision = RECOVERY_HW_REVISION;
   info.chip = chip ? chip->chip : CHIP_UNKNOWN;
   memcpy(info.chip_id, chip_id, sizeof(info.chip_id));
   if (chip)
   {
      info.page_size_bytes = chip->page_size_bytes;
      info.spare_size_bytes = chip->spare_size_bytes;
      info.pages_per_block = chip->pages_per_block;
      info.block_count = chip->block_count;
      info.reserved_blocks = chip->reserved_blocks;
   }
#ifdef LEGACY_RECOVERY_HOST
   legacy_recovery_host_uid(info.uid);
#else
   system_read_UID(info.uid, sizeof(info.uid));
#endif
   memcpy(info.status_registers, found_status_registers, sizeof(info.status_registers));
   info.transport = RECOVERY_TRANSPORT;
   strncpy(info.firmware, RECOVERY_FW_REVISION, sizeof(info.firmware) - 1);
   if (!send_frame(FRAME_INFO, 0, &info, sizeof(info), NULL, 0))
      return false;

   // The Winbond part remaps bad blocks in hardware, so which logical blocks the old firmware retired lives
   // in the chip's own table rather than on a page. Sent raw; the host decodes it
   if (chip && (chip->chip == CHIP_W25N01GW))
   {
      uint8_t lut[W25N_LUT_BYTES];
      const uint8_t dummy = 0;
      memset(lut, 0, sizeof(lut));
      nandlog_port_transfer_read(COMMAND_READ_BBM_LUT, &dummy, sizeof(dummy), lut, sizeof(lut));
      if (!send_frame(FRAME_LUT, 0, lut, sizeof(lut), NULL, 0))
         return false;
   }
   return true;
}

static void dump_pages(uint32_t first_page, uint32_t page_count)
{
   // Every page, reserve included: the reserve is where the Alliance part's bad-block table lives, and the
   // host decides what to ignore
   if (!send_info())
      return;
   if (!chip)
   {
      send_error("No supported flash part answered; nothing can be read");
      return;
   }
   const uint32_t total_pages = chip->block_count * chip->pages_per_block;
   if (first_page >= total_pages)
      first_page = total_pages;
   if (!page_count || (page_count > (total_pages - first_page)))
      page_count = total_pages - first_page;

   print("Legacy Log Recovery: reading pages %u to %u...\n", first_page, first_page + page_count - 1);
#ifndef LEGACY_RECOVERY_HOST
   led_on(LED_YELLOW);
#endif
   const uint32_t started = now_ms();
   progress_frame_t progress = { 0 };
   for (uint32_t page = first_page; page < (first_page + page_count); ++page)
   {
      // A page that will not read cleanly is tried again before it is reported: a marginal cell sometimes
      // reads correctly on a second attempt, and it costs nothing when it does not
      uint8_t status_register_3 = 0, reads = 0;
      bool clean = false;
      while (!clean && (reads < MAX_READS_PER_PAGE))
      {
         clean = read_page(page, &status_register_3);
         ++reads;
      }

      if (!clean || !page_is_erased())
      {
         const page_frame_header_t header = { .page = page, .status_register_3 = status_register_3, .reads = reads, .reserved = 0 };
         if (!send_frame(FRAME_PAGE, clean ? 0 : PAGE_FLAG_UNCORRECTABLE, &header, sizeof(header), page_buffer, chip->page_size_bytes))
         {
            print("Legacy Log Recovery: host disconnected at page %u\n", page);
            return;
         }
         ++progress.pages_sent;
         if (!clean)
            ++progress.pages_uncorrectable;
      }

      if ((++progress.pages_scanned % PROGRESS_INTERVAL_PAGES) == 0)
      {
         progress.elapsed_ms = now_ms() - started;
         if (!send_frame(FRAME_PROGRESS, 0, &progress, sizeof(progress), NULL, 0))
            return;
      }
   }

   progress.elapsed_ms = now_ms() - started;
   send_frame(FRAME_END, 0, &progress, sizeof(progress), NULL, 0);
   print("Legacy Log Recovery: done: %u pages read, %u sent, %u uncorrectable, %u ms\n",
         progress.pages_scanned, progress.pages_sent, progress.pages_uncorrectable, progress.elapsed_ms);
#ifndef LEGACY_RECOVERY_HOST
   led_off(LED_YELLOW);
   led_on(progress.pages_uncorrectable ? LED_RED : LED_GREEN);
#endif
}

#ifdef LEGACY_RECOVERY_HOST

// Entry points for the host harness
bool legacy_recovery_host_prepare(void) { return prepare_flash(); }
void legacy_recovery_host_dump(uint32_t first_page, uint32_t page_count) { dump_pages(first_page, page_count); }

#else


// Device Tasks --------------------------------------------------------------------------------------------------------

#if RECOVERY_TRANSPORT == TRANSPORT_USB

static bool usb_read_exact(void *data, uint32_t length)
{
   // Arguments can trail their command byte into the next USB packet
   uint32_t have = 0;
   for (uint32_t attempt = 0; (have < length) && (attempt < 1000); ++attempt)
   {
      have += tud_cdc_read((uint8_t*)data + have, length - have);
      if (have < length)
         vTaskDelay(1);
   }
   return have == length;
}

static void RecoveryTask(void *params)
{
   // Nothing is read until the host asks, so a tag left plugged in does no work
   while (true)
   {
      uint8_t command = 0;
      if (tud_cdc_available() && (tud_cdc_read(&command, 1) == 1))
      {
         if (command == COMMAND_INFO)
            send_info();
         else if (command == COMMAND_DUMP)
         {
            uint32_t arguments[2] = { 0, 0 };
            if (usb_read_exact(arguments, sizeof(arguments)))
               dump_pages(arguments[0], arguments[1]);
         }
      }
      else
         vTaskDelay(1);
   }
}

#else

static void RecoveryTask(void *params)
{
   // RTT has no way for the host to ask, so dump once, straight away. The channel blocks when full, so a
   // J-Link attached late loses nothing; it only makes the dump wait for it
   vTaskDelay(pdMS_TO_TICKS(2000));
   dump_pages(0, 0);
   while (true)
      vTaskDelay(portMAX_DELAY);
}

#endif  // #if RECOVERY_TRANSPORT == TRANSPORT_USB

int main(void)
{
   // Set up only what reading the flash and reporting it needs. No storage task, no nandlog, no radio
   setup_hardware();
   leds_init();
   prepare_flash();
#if RECOVERY_TRANSPORT == TRANSPORT_USB
   usb_init();
   if (!usb_cable_connected())
      print("Legacy Log Recovery: waiting for a USB cable (the tag restarts when one is plugged in)\n");
#endif

   // Create the tasks and start the scheduler
   static StaticTask_t recovery_task_tcb;
   static StackType_t recovery_task_stack[2 * configMINIMAL_STACK_SIZE];
   xTaskCreateStatic(RecoveryTask, "RecoveryTask", 2 * configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES - 2, recovery_task_stack, &recovery_task_tcb);
#if RECOVERY_TRANSPORT == TRANSPORT_USB
   static StaticTask_t usb_task_tcb;
   static StackType_t usb_task_stack[configMINIMAL_STACK_SIZE];
   xTaskCreateStatic(UsbTask, "UsbTask", configMINIMAL_STACK_SIZE, NULL, configMAX_PRIORITIES - 1, usb_task_stack, &usb_task_tcb);
#endif
   vTaskStartScheduler();

   // Should never reach this point
   return 0;
}

#endif  // #ifdef LEGACY_RECOVERY_HOST
