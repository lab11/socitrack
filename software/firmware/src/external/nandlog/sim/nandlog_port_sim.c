// A RAM-backed implementation of nandlog_port.h, for running the log on a host.
//
// The porting seam is at the SPI command level, so this is not a stub that pretends to be a log -- it is an
// emulation of a NAND part, and the real chip driver runs unmodified on top of it. That is what makes a host
// test worth anything: the addressing, the status-register handshakes, the busy-waiting, the bad-block
// bookkeeping and the power-fail behaviour are all the shipping code, exercised at memory speed.
//
// NAND semantics are honoured rather than approximated. Erase sets bits to one; programming may only clear
// them, so a page programmed twice keeps the AND of both writes, and a partial program that is interrupted
// leaves exactly what was written before the interruption. Getting this wrong would make the simulator agree
// with a buggy log.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nandlog_chip.h"
#include "nandlog_port.h"
#include "nandlog_port_sim.h"


// Emulated Part -------------------------------------------------------------------------------------------------------

#define SIM_COMMAND_READ_DEVICE_ID                  0x9F
#define SIM_COMMAND_DEVICE_RESET                    0xFF
#define SIM_COMMAND_READ_STATUS_REGISTER            0x0F
#define SIM_COMMAND_WRITE_STATUS_REGISTER           0x1F
#define SIM_COMMAND_WRITE_ENABLE                    0x06
#define SIM_COMMAND_WRITE_DISABLE                   0x04
#define SIM_COMMAND_BLOCK_ERASE                     0xD8
#define SIM_COMMAND_PROGRAM_DATA_LOAD               0x02
#define SIM_COMMAND_PROGRAM_EXECUTE                 0x10
#define SIM_COMMAND_PAGE_DATA_READ                  0x13
#define SIM_COMMAND_READ                            0x03

#define SIM_STATUS_REGISTER_1                       0xA0
#define SIM_STATUS_REGISTER_2                       0xB0
#define SIM_STATUS_REGISTER_3                       0xC0

#define SIM_STATUS_PAGE_FATAL_ERROR                 0b00100000
#define SIM_STATUS_WRITE_FAILURE                    0b00001000
#define SIM_STATUS_ERASE_FAILURE                    0b00000100
#define SIM_STATUS_BUSY                             0b00000001

#define SIM_CONFIG_OTP_ENTER                        0b01000000

// The OTP region is a handful of pages mapped over the array when the configuration bit is set. Only the low
// pages are ever touched, so a small window is enough
#define SIM_OTP_PAGES                               8


// Simulated State -----------------------------------------------------------------------------------------------------

static uint8_t *array;                    // main array plus spare, one contiguous block of page-with-spare records
static uint8_t *otp;                      // the OTP window, which erase never reaches
static uint32_t page_size, spare_size, page_stride, page_count;
static uint8_t status_register[3];        // indexed by (register number >> 4) - 0x0A
static uint8_t program_buffer[NANDLOG_SIM_MAX_PAGE_WITH_SPARE];
static uint32_t program_buffer_length;
static uint32_t read_page_address;        // the page most recently latched by PAGE_DATA_READ
static bool write_latch;                  // set by WRITE_ENABLE, cleared by the operation that consumes it
static bool port_open, powered, write_permitted;

static nandlog_sim_faults_t faults;
static nandlog_sim_counters_t counters;
static bool fatal_expected, fatal_seen;
static char fatal_reason[128];
static bool verbose;

// Set when a program or erase should be abandoned part-way, to emulate losing power mid-operation
static uint32_t power_fail_after_ops;
static bool power_failed;
static jmp_buf fatal_jump;


// Private Helper Functions --------------------------------------------------------------------------------------------

static uint8_t *page_at(uint32_t page)
{
   if ((status_register[1] & SIM_CONFIG_OTP_ENTER) && (page < SIM_OTP_PAGES))
      return otp + ((size_t)page * page_stride);
   return array + ((size_t)(page % page_count) * page_stride);
}

static uint32_t address_from(const uint8_t *bytes)
{
   return ((uint32_t)bytes[0] << 16) | ((uint32_t)bytes[1] << 8) | (uint32_t)bytes[2];
}

static bool block_in_list(const uint32_t *list, uint32_t count, uint32_t page)
{
   const uint32_t block = page / faults.pages_per_block;
   for (uint32_t i = 0; i < count; ++i)
      if (list[i] == block)
         return true;
   return false;
}

static bool consume_power_fail(void)
{
   // Returns true when this operation is the one that gets cut short
   if (!power_fail_after_ops)
      return false;
   if (--power_fail_after_ops)
      return false;
   power_failed = true;
   return true;
}


// Simulator Control ---------------------------------------------------------------------------------------------------

void nandlog_sim_create(const uint8_t *device_id, uint32_t device_id_length)
{
   // Geometry comes from the compiled chip driver, so the emulated part cannot be the wrong shape for it
   const nandlog_geometry_t *geometry = nandlog_chip_geometry();
   nandlog_sim_destroy();
   page_size = geometry->page_size_bytes;
   spare_size = geometry->spare_size_bytes;
   page_stride = page_size + spare_size;
   page_count = geometry->pages_per_block * geometry->block_count;
   array = malloc((size_t)page_count * page_stride);
   otp = malloc((size_t)SIM_OTP_PAGES * page_stride);
   if (!array || !otp)
   {
      fprintf(stderr, "SIM: out of memory for a %u-page part\n", page_count);
      exit(1);
   }
   memset(array, 0xFF, (size_t)page_count * page_stride);
   memset(otp, 0xFF, (size_t)SIM_OTP_PAGES * page_stride);
   memset(&counters, 0, sizeof(counters));
   memset(&faults, 0, sizeof(faults));
   faults.pages_per_block = geometry->pages_per_block;
   memcpy(faults.device_id, device_id, (device_id_length < sizeof(faults.device_id)) ? device_id_length : sizeof(faults.device_id));
   status_register[0] = status_register[1] = status_register[2] = 0;
   program_buffer_length = read_page_address = 0;
   write_latch = powered = write_permitted = false;
   port_open = false;
   power_fail_after_ops = 0;
   power_failed = fatal_expected = fatal_seen = false;
}

void nandlog_sim_destroy(void)
{
   free(array);
   free(otp);
   array = otp = NULL;
}

void nandlog_sim_set_faults(const nandlog_sim_faults_t *new_faults)
{
   const uint32_t pages_per_block = faults.pages_per_block;
   faults = *new_faults;
   if (!faults.pages_per_block)
      faults.pages_per_block = pages_per_block;
}

void nandlog_sim_power_fail_after(uint32_t operations)
{
   power_fail_after_ops = operations;
   power_failed = false;
}

bool nandlog_sim_power_failed(void)
{
   return power_failed;
}

jmp_buf *nandlog_sim_fatal_jump(void)
{
   return &fatal_jump;
}

bool nandlog_sim_dump(const char *path, uint32_t first_page, uint32_t num_pages)
{
   if (first_page >= page_count)
      return false;
   if (num_pages > (page_count - first_page))
      num_pages = page_count - first_page;
   FILE *f = fopen(path, "wb");
   if (!f)
      return false;
   const size_t bytes = (size_t)num_pages * page_stride;
   const bool ok = (fwrite(array + ((size_t)first_page * page_stride), 1, bytes, f) == bytes);
   fclose(f);
   return ok;
}

nandlog_sim_counters_t nandlog_sim_counters(void)
{
   return counters;
}

void nandlog_sim_set_verbose(bool on)
{
   verbose = on;
}

void nandlog_sim_expect_fatal(bool expected)
{
   fatal_expected = expected;
   fatal_seen = false;
}

bool nandlog_sim_fatal_seen(const char **reason)
{
   if (reason)
      *reason = fatal_reason;
   return fatal_seen;
}

uint8_t *nandlog_sim_raw_page(uint32_t page)
{
   return (page < page_count) ? (array + ((size_t)page * page_stride)) : NULL;
}

void nandlog_sim_corrupt(uint32_t page, uint32_t offset, uint8_t mask)
{
   // Clear bits in place, which is the one direction flash can rot without an erase
   uint8_t *p = nandlog_sim_raw_page(page);
   if (p && (offset < page_stride))
      p[offset] &= (uint8_t)~mask;
}


// Port Implementation -------------------------------------------------------------------------------------------------

bool nandlog_port_init(void)
{
   if (!array)
   {
      fprintf(stderr, "SIM: nandlog_port_init() before nandlog_sim_create()\n");
      exit(1);
   }
   port_open = powered = true;
   return true;
}

void nandlog_port_deinit(void)
{
   port_open = false;
}

void nandlog_port_write_enable(bool enable)
{
   write_permitted = enable;
}

void nandlog_port_power(bool awake)
{
   powered = awake;
}

void nandlog_port_delay_us(uint32_t microseconds)
{
   (void)microseconds;
}

void nandlog_port_delay_ms(uint32_t milliseconds)
{
   (void)milliseconds;
}

void nandlog_port_log(const char *format, ...)
{
   if (!verbose)
      return;
   va_list args;
   va_start(args, format);
   vprintf(format, args);
   va_end(args);
}

void nandlog_port_lock(void) {}

void nandlog_port_unlock(void) {}

void nandlog_port_unwritable(void)
{
   fprintf(stderr, "SIM: log reported the part as unwritable\n");
}

void nandlog_port_fatal(const char *reason)
{
   fatal_seen = true;
   snprintf(fatal_reason, sizeof(fatal_reason), "%s", reason ? reason : "");
   if (!fatal_expected)
   {
      fprintf(stderr, "SIM: unexpected fatal fault: %s\n", fatal_reason);
      exit(1);
   }
   // A real port resets the system here. The host equivalent is to unwind to the test, which the test does by
   // checking nandlog_sim_fatal_seen() -- so returning is only safe because the caller is about to be stopped
   longjmp(fatal_jump, 1);
}

void nandlog_port_spi_read(uint8_t command, const void *address, uint32_t address_length, void *read_buffer, uint32_t read_length)
{
   const uint8_t *addr = (const uint8_t*)address;
   uint8_t *out = (uint8_t*)read_buffer;
   ++counters.spi_reads;

   switch (command)
   {
      case SIM_COMMAND_READ_DEVICE_ID:
      {
         // Two shapes are in use: a leading dummy byte then three identity bytes, or a one-byte address
         // selecting a single identity byte. Serving both keeps one simulator usable by either driver
         for (uint32_t i = 0; i < read_length; ++i)
            out[i] = 0x00;
         if ((address_length == 1) && (read_length == 1))
            out[0] = faults.device_id[0];
         else if (read_length >= 4)
            memcpy(out + 1, faults.device_id, 3);
         else
            memcpy(out, faults.device_id, (read_length < 3) ? read_length : 3);
         break;
      }

      case SIM_COMMAND_READ_STATUS_REGISTER:
      {
         const uint8_t which = addr ? addr[0] : SIM_STATUS_REGISTER_3;
         uint8_t value = status_register[(which >> 4) - 0x0A];
         if (which == SIM_STATUS_REGISTER_3)
         {
            // The part is never busy from the host's point of view: operations complete within their call
            value &= (uint8_t)~SIM_STATUS_BUSY;
            if (faults.stay_busy)
               value |= SIM_STATUS_BUSY;
         }
         if (read_length)
            out[0] = value;
         break;
      }

      case SIM_COMMAND_READ:
      {
         const uint8_t *src = page_at(read_page_address);
         const uint32_t available = page_stride;
         memset(out, 0xFF, read_length);
         memcpy(out, src, (read_length < available) ? read_length : available);
         break;
      }

      default:
         memset(out, 0xFF, read_length);
         break;
   }
}

void nandlog_port_spi_write(uint8_t command, const void *address, uint32_t address_length, const void *write_buffer, uint32_t write_length)
{
   const uint8_t *addr = (const uint8_t*)address;
   const uint8_t *in = (const uint8_t*)write_buffer;
   ++counters.spi_writes;

   switch (command)
   {
      case SIM_COMMAND_WRITE_STATUS_REGISTER:
         if (addr && write_length)
            status_register[(addr[0] >> 4) - 0x0A] = in[0];
         break;

      case SIM_COMMAND_WRITE_ENABLE:
         write_latch = true;
         break;

      case SIM_COMMAND_WRITE_DISABLE:
         write_latch = false;
         break;

      case SIM_COMMAND_PAGE_DATA_READ:
         read_page_address = address_from(in);
         ++counters.page_reads;
         break;

      case SIM_COMMAND_PROGRAM_DATA_LOAD:
         program_buffer_length = (write_length > sizeof(program_buffer)) ? sizeof(program_buffer) : write_length;
         memcpy(program_buffer, in, program_buffer_length);
         break;

      case SIM_COMMAND_PROGRAM_EXECUTE:
      {
         const uint32_t page = address_from(in);
         status_register[2] &= (uint8_t)~SIM_STATUS_WRITE_FAILURE;
         ++counters.page_writes;

         if (!write_latch || !write_permitted)
         {
            // A program with the latch or the pin against it is refused, exactly as the part would
            status_register[2] |= SIM_STATUS_WRITE_FAILURE;
            break;
         }
         write_latch = false;

         if (block_in_list(faults.unwritable_blocks, faults.num_unwritable_blocks, page))
         {
            status_register[2] |= SIM_STATUS_WRITE_FAILURE;
            break;
         }

         // Program clears bits and never sets them, so the result is the AND of what is there and what is
         // being written. A power failure part-way leaves the prefix that made it
         uint8_t *dst = page_at(page);
         uint32_t length = program_buffer_length;
         if (consume_power_fail())
            length /= 2;
         for (uint32_t i = 0; i < length; ++i)
            dst[i] &= program_buffer[i];
         break;
      }

      case SIM_COMMAND_BLOCK_ERASE:
      {
         const uint32_t page = address_from(in);
         const uint32_t block_start = (page / faults.pages_per_block) * faults.pages_per_block;
         status_register[2] &= (uint8_t)~SIM_STATUS_ERASE_FAILURE;
         ++counters.block_erases;

         if (!write_latch || !write_permitted)
         {
            status_register[2] |= SIM_STATUS_ERASE_FAILURE;
            break;
         }
         write_latch = false;

         if (block_in_list(faults.unerasable_blocks, faults.num_unerasable_blocks, page))
         {
            status_register[2] |= SIM_STATUS_ERASE_FAILURE;
            break;
         }

         uint32_t pages_to_erase = faults.pages_per_block;
         if (consume_power_fail())
            pages_to_erase /= 2;
         for (uint32_t i = 0; i < pages_to_erase; ++i)
            if ((block_start + i) < page_count)
               memset(array + ((size_t)(block_start + i) * page_stride), 0xFF, page_stride);
         break;
      }

      case SIM_COMMAND_DEVICE_RESET:
         write_latch = false;
         break;

      default:
         (void)address_length;
         break;
   }
}
