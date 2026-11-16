// Host harness for legacy_log_recovery.c: runs the recovery firmware's own scanning and framing code, unmodified,
// against the nandlog SPI-level simulator, so the dump it produces can be checked end to end without a tag.
//
//    legacy_log_recovery_sim <W25N01GW|AS5F18G04SND> <image> <stream-out> [uid-hex]
//
// <image> holds the pages to load, as repeated [page:u32][length:u32][bytes], each written into the main area of
// that page (the spare area stays erased). The emulated part is otherwise blank. <stream-out> receives exactly
// the bytes the firmware would have sent over USB or RTT.
//
// The run fails, whatever the stream says, if the firmware programmed or erased anything or if a single byte of
// the emulated array differs afterwards. That is the property the firmware exists to have.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nandlog_chip.h"
#include "nandlog_port_sim.h"

bool legacy_recovery_host_prepare(void);
void legacy_recovery_host_dump(uint32_t first_page, uint32_t page_count);

static nandlog_geometry_t geometry;
static FILE *stream_out;
static uint8_t uid[6] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 };


// Hooks Required by the Firmware and the Simulator --------------------------------------------------------------------

const nandlog_geometry_t *nandlog_chip_geometry(void)
{
   return &geometry;
}

bool legacy_recovery_host_emit(const void *data, uint32_t length)
{
   return fwrite(data, 1, length, stream_out) == length;
}

void legacy_recovery_host_uid(uint8_t *out)
{
   memcpy(out, uid, sizeof(uid));
}


// Helpers -------------------------------------------------------------------------------------------------------------

static uint64_t array_fingerprint(void)
{
   // FNV-1a over every byte of every page, spare included
   const uint32_t stride = geometry.page_size_bytes + geometry.spare_size_bytes;
   const uint32_t pages = geometry.pages_per_block * geometry.block_count;
   uint64_t hash = 1469598103934665603ull;
   for (uint32_t page = 0; page < pages; ++page)
   {
      const uint8_t *bytes = nandlog_sim_raw_page(page);
      for (uint32_t i = 0; i < stride; ++i)
         hash = (hash ^ bytes[i]) * 1099511628211ull;
   }
   return hash;
}

static bool load_image(const char *path)
{
   FILE *image = fopen(path, "rb");
   if (!image)
      return false;
   uint32_t header[2];
   uint32_t loaded = 0;
   while (fread(header, sizeof(uint32_t), 2, image) == 2)
   {
      uint8_t *page = nandlog_sim_raw_page(header[0]);
      if (!page || (header[1] > geometry.page_size_bytes) || (fread(page, 1, header[1], image) != header[1]))
      {
         fprintf(stderr, "SIM: malformed image entry for page %u\n", header[0]);
         fclose(image);
         return false;
      }
      ++loaded;
   }
   fclose(image);
   printf("SIM: loaded %u pages\n", loaded);
   return true;
}


// Entry Point ---------------------------------------------------------------------------------------------------------

int main(int argc, char **argv)
{
   if ((argc < 4) || (argc > 5))
   {
      fprintf(stderr, "usage: %s <W25N01GW|AS5F18G04SND> <image> <stream-out> [uid-hex]\n", argv[0]);
      return 2;
   }

   // The simulator takes its geometry from nandlog_chip_geometry() and its identity from here
   const uint8_t winbond_id[3] = { 0xEF, 0xBA, 0x21 }, alliance_id[3] = { 0x8D, 0x00, 0x00 };
   const uint8_t *device_id;
   if (strcmp(argv[1], "W25N01GW") == 0)
   {
      geometry = (nandlog_geometry_t){ 2048, 64, 64, 1024, 40 };
      device_id = winbond_id;
   }
   else if (strcmp(argv[1], "AS5F18G04SND") == 0)
   {
      geometry = (nandlog_geometry_t){ 4096, 256, 64, 4096, 80 };
      device_id = alliance_id;
   }
   else
   {
      fprintf(stderr, "unknown part '%s'\n", argv[1]);
      return 2;
   }
   if (argc == 5)
      for (int i = 0; i < 6; ++i)
         sscanf(argv[4] + (2 * i), "%2hhx", &uid[5 - i]);

   nandlog_sim_create(device_id, 3);
   if (!load_image(argv[2]))
      return 1;
   stream_out = fopen(argv[3], "wb");
   if (!stream_out)
      return 1;

   const uint64_t before = array_fingerprint();
   legacy_recovery_host_prepare();
   legacy_recovery_host_dump(0, 0);
   fclose(stream_out);
   const uint64_t after = array_fingerprint();

   const nandlog_sim_counters_t counters = nandlog_sim_counters();
   printf("SIM: %u page reads, %u page programs, %u block erases\n", counters.page_reads, counters.page_writes, counters.block_erases);
   if (counters.page_writes || counters.block_erases || (before != after))
   {
      fprintf(stderr, "SIM: FAIL -- the recovery firmware modified the flash\n");
      return 1;
   }
   printf("SIM: flash unchanged\n");
   nandlog_sim_destroy();
   return 0;
}
