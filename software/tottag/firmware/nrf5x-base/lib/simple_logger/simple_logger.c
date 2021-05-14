#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "simple_logger.h"
#include "chanfs/ff.h"
#include "chanfs/diskio.h"
#include "nrf_delay.h"

// Define your own buffer size if required; 256 used as default
#ifndef SIMPLE_LOGGER_BUFFER_SIZE
#define SIMPLE_LOGGER_BUFFER_SIZE	256
#endif

static volatile uint8_t log_file_exists = 0;
static const char *file_name = NULL;
static DIR root_dir = { 0 };
static FILINFO file_info = { 0 };

static char buffer[SIMPLE_LOGGER_BUFFER_SIZE];
static char header_buffer[SIMPLE_LOGGER_BUFFER_SIZE];
static uint32_t buffer_size = SIMPLE_LOGGER_BUFFER_SIZE;

static FIL debug_file = { 0 };
static FIL file_for_reading = { 0 };
static FIL simple_logger_fpointer = { 0 };
static FATFS simple_logger_fs = { 0 };
static BYTE work[FF_MAX_SS];		/* Work area (larger is better for processing time) */

/*-----------------------------------------------------------------------*/
/* SD card function                                                		 */
/*-----------------------------------------------------------------------*/

uint8_t simple_logger_init(void)
{
   // Mount the file system
   volatile FRESULT res = f_mount(&simple_logger_fs, "", 1);
   switch (res)
   {
      case FR_OK:
         break;
      case FR_NOT_READY:
         printf("ERROR: Unable to access SD card!\n");
         return res;
      case FR_NO_FILESYSTEM:
         res = f_mkfs("", 0, work, sizeof(work));
         if (res != FR_OK)
         {
            printf("ERROR: Failed to create a new SD card filesystem: %d\n", res);
            return res;
         }
         res = f_mount(&simple_logger_fs, "", 1);
         if (res != FR_OK)
         {
            printf("ERROR: Unable to mount SD card filesystem: %d\n", res);
            return res;
         }
         break;
      default:
         printf("ERROR: Unexpected error while mounting SD card: %d\n", res);
         return res;
   }

   // Initialize the file pointer if a valid filename has been set
   if (file_name)
   {
      // See if the file already exists
      FIL temp;
      res = f_open(&temp, file_name, FA_READ | FA_OPEN_EXISTING);
      f_close(&temp);
      if (res == FR_OK)
         log_file_exists = 1;
      else if (res == FR_NO_FILE)
         log_file_exists = 0;
      else
      {
         printf("ERROR: Unexpected error while attempting to open SD card log file: %d\n", res);
         return res;
      }

      // Open the file
      res = f_open(&simple_logger_fpointer, file_name, FA_WRITE | FA_READ | FA_OPEN_APPEND);
   }

   // Return success or failure
   return res;
}

uint8_t simple_logger_reinit(const char *filename)
{
   // Close current file and reset the filename
   f_close(&simple_logger_fpointer);
   file_name = filename;
   return simple_logger_init();
}

uint8_t simple_logger_init_debug(const char *filename)
{
   // Open or create the debugging log file in append mode
   return f_open(&debug_file, filename, FA_READ | FA_WRITE | FA_OPEN_APPEND);
}

// Re-enable the SD card and initialize again
uint8_t simple_logger_power_on()
{
   // Enable SD card
   disk_enable();
   nrf_delay_ms(250);

   // Re-initialize
   uint8_t res = simple_logger_init(), num_retries = 10;
   while (--num_retries && (res != FR_OK))
   {
      nrf_delay_ms(25);
      res = simple_logger_init();
   }
   return res;
}

void simple_logger_power_off()
{
   // Flush any open files and disable the SD card
   f_close(&simple_logger_fpointer);
   nrf_delay_ms(250);
   disk_disable();
}

uint8_t simple_logger_log_string(const char *str)
{
   // Attempt to write the data string
   if (f_puts(str, &simple_logger_fpointer) < 0)
   {
      printf("ERROR: Problem writing data string to SD card!\n");
      return FR_DISK_ERR;
   }

   // Sync any cached data to the SD card
   FRESULT res = f_sync(&simple_logger_fpointer);
   if (res != FR_OK)
   {
      res = simple_logger_init();
      if (res == FR_OK)
      {
         if (f_puts(str, &simple_logger_fpointer) < 0)
         {
            printf("ERROR: Problem writing data string to SD card!\n");
            return FR_DISK_ERR;
         }
         res = f_sync(&simple_logger_fpointer);
      }
   }
   return res;
}

uint8_t simple_logger_log(const char *format, ...)
{
   // ATTENTION: Make sure all strings are <= 255 bytes; the nRF SPI implementation does not allow longer transaction
   // Note: This is due to the underlying DMA being restricted to 255 bytes; for more information, see https://devzone.nordicsemi.com/f/nordic-q-a/16580/send-more-than-255-bytes-by-spi-nrf52
   va_list argptr;
   va_start(argptr, format);
   vsnprintf(buffer, buffer_size, format, argptr);
   va_end(argptr);
   return simple_logger_log_string(buffer);
}

uint8_t simple_logger_log_header(const char *format, ...)
{
   // Print the header to a string
   va_list argptr;
   va_start(argptr, format);
   vsnprintf(header_buffer, buffer_size, format, argptr);
   va_end(argptr);

   // Write the header string to the SD card if the log file is new
   if (!log_file_exists)
   {
      if (f_puts(header_buffer, &simple_logger_fpointer) < 0)
      {
         printf("ERROR: Problem writing header to SD card!\n");
         return FR_DISK_ERR;
      }
      log_file_exists = 1;
      return f_sync(&simple_logger_fpointer);
   }

   // Do not write the header if the log file already existed
   return SIMPLE_LOGGER_FILE_EXISTS;
}

uint8_t simple_logger_list_files(char *filename, uint32_t *file_size, uint8_t continuation)
{
   // Open the root directory
   FRESULT res = FR_OK;
   if (!continuation)
   {
      f_closedir(&root_dir);
      res = f_opendir(&root_dir, "/");
   }

   // Read the next directory item
   if (res == FR_OK)
   {
      // Close the directory if no more contents exist
      res = f_readdir(&root_dir, &file_info);
      if ((res != FR_OK) || (file_info.fname[0] == 0))
      {
         f_closedir(&root_dir);
         return 0;
      }

      // Ignore the system log file
      if ((file_info.fname[0] == 'S') && (file_info.fname[1] == 'Y') && (file_info.fname[2] == 'S') &&
            (file_info.fname[3] == 'T') && (file_info.fname[4] == 'E') && (file_info.fname[5] == 'M'))
         return simple_logger_list_files(filename, file_size, 1);
      else
      {
         memcpy(filename, file_info.fname, strlen(file_info.fname));
         *file_size = (uint32_t)file_info.fsize;
         return 1;
      }
   }
   return 0;
}

uint8_t simple_logger_delete_file(const char *filename)
{
   return (f_unlink(filename) == FR_OK);
}

uint8_t simple_logger_open_file_for_reading(const char *filename)
{
   return (f_open(&file_for_reading, filename, FA_READ | FA_OPEN_EXISTING) == FR_OK);
}

void simple_logger_close_reading_file(void)
{
   f_close(&file_for_reading);
}

uint32_t simple_logger_read_reading_file(uint8_t *data_buffer, uint32_t buffer_length)
{
   UINT bytes_read = 0;
   return (f_read(&file_for_reading, data_buffer, buffer_length, &bytes_read) == FR_OK) ? (uint32_t)bytes_read : 0;
}

uint8_t simple_logger_printf(uint32_t timestamp, const char *format, va_list ap)
{
   snprintf(buffer, buffer_size, "%lu: ", timestamp);
   f_puts(buffer, &debug_file);
   vsnprintf(buffer, buffer_size, format, ap);
   f_puts(buffer, &debug_file);
   return f_sync(&debug_file);
}
