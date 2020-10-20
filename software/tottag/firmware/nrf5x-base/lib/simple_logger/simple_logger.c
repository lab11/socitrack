#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "simple_logger.h"
#include "chanfs/ff.h"
#include "chanfs/diskio.h"

static uint8_t simple_logger_inited = 0;
static uint8_t simple_logger_file_exists = 0;
static uint8_t header_written = 0;
static uint8_t error_count = 0;

static DIR root_dir = { 0 };
static FILINFO file_info = { 0 };
const char *file = NULL;

// Define your own buffer size if required; 256 used as default
#ifndef SIMPLE_LOGGER_BUFFER_SIZE
#define SIMPLE_LOGGER_BUFFER_SIZE	256
#endif

static char buffer[SIMPLE_LOGGER_BUFFER_SIZE];
static char header_buffer[SIMPLE_LOGGER_BUFFER_SIZE];
static uint32_t buffer_size = SIMPLE_LOGGER_BUFFER_SIZE;

static FIL simple_logger_fpointer = { 0 };
static FATFS simple_logger_fs = { 0 };
static uint8_t simple_logger_opts;
static BYTE work[FF_MAX_SS];		/* Work area (larger is better for processing time) */

static FIL file_for_reading;

static void error(void)
{
   if (++error_count > 20)
   {
      disk_restart();
      error_count = 0;
   }
}

/*-----------------------------------------------------------------------*/
/* SD card function                                                		 */
/*-----------------------------------------------------------------------*/

// An SD card was inserted after being gone for a bit
// Let's reopen the file, and try to rewrite the header if it's necessary
static uint8_t logger_init(uint8_t force_complete_init)
{
   // Mount the file system if not already initialized
   volatile FRESULT res = FR_OK;
   if (force_complete_init || !simple_logger_inited)
   {
      // See if the file system already exists
      res = f_mount(&simple_logger_fs, "", 1);
      while (res != FR_OK)
      {
         switch (res)
         {
            case FR_NOT_READY:
               // No disk found; check "Card Detected" signal before calling this function
               printf("ERROR: Unable to access SD card!\n");
               return res;
            case FR_NO_FILESYSTEM:
               // No existing file system
               res = f_mkfs("", 0, work, sizeof(work));
               if (res != FR_OK)
               {
                  printf("ERROR: Failed to create a new SD card filesystem: %d\n", res);
                  return res;
               }

               // Retry mounting now
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
      }
   }
   else
      simple_logger_inited = 0;

   // See if the file already exists
   FIL temp;
   res = f_open(&temp, file, FA_READ | FA_OPEN_EXISTING);
   if (res == FR_NO_FILE)
      simple_logger_file_exists = 0;
   else if (res == FR_OK)
   {
      simple_logger_file_exists = 1;
      res = f_close(&temp);
   }

   // Open the file and set the write cursor
   res |= f_open(&simple_logger_fpointer, file, simple_logger_opts);
   if (simple_logger_opts & FA_OPEN_ALWAYS)
   {
      // We are in append mode and should move to the end
      res |= f_lseek(&simple_logger_fpointer, f_size(&simple_logger_fpointer));
   }
   if (header_written && !simple_logger_file_exists)
   {
      f_puts(header_buffer, &simple_logger_fpointer);
      res |= f_sync(&simple_logger_fpointer);
   }

   simple_logger_inited = 1;
   return res;
}

uint8_t simple_logger_init(void)
{
   // See if the file system already exists
   simple_logger_inited = 0;
   volatile FRESULT res = f_mount(&simple_logger_fs, "", 1);
   switch (res)
   {
      case FR_OK:
         break;
      case FR_NOT_READY:
         printf("ERROR: No SD card detected!\n");
         break;
      case FR_NO_FILESYSTEM:
         // No existing file system
         res = f_mkfs("", 0, work, sizeof(work));
         if (res != FR_OK)
            printf("ERROR: Failed to create a new filesystem on the SD card!\n");
         else
            res = f_mount(&simple_logger_fs, "", 1);
         break;
      default:
         printf("ERROR: Unexpected error while mounting SD card: %d\n", res);
         break;
   }

   // Set initialized flag
   simple_logger_inited = 1;
   return res;
}

uint8_t simple_logger_reinit(const char *filename, const char *permissions)
{
   // Close current file and reset filename
   header_written = 0;
   if (file)
      f_close(&simple_logger_fpointer);
   file = filename;

   // Set write/append permissions
   if((permissions[0] != 'w'  && permissions[0] != 'a') || (permissions[1] != '\0' && permissions[2] != 'r') )
      return SIMPLE_LOGGER_BAD_PERMISSIONS;
   if(permissions[0] == 'w')
      simple_logger_opts = (FA_WRITE | FA_CREATE_ALWAYS);
   else if (permissions[0] == 'a')
      simple_logger_opts = (FA_WRITE | FA_OPEN_ALWAYS);
   else
      return SIMPLE_LOGGER_BAD_PERMISSIONS;

   // Set read permission
   if (permissions[1] == ',' && permissions[2] == 'r')
      simple_logger_opts |= FA_READ;

   return logger_init(0);
}

// Re-enable the SD card and initialize again
uint8_t simple_logger_power_on()
{
   // Enable SD card
   disk_enable();

   // Re-initialize
   uint8_t res = logger_init(1), num_retries = 10;
   while (--num_retries && (res != FR_OK))
      res = logger_init(1);
   return res;
}

void simple_logger_power_off()
{
   // Flush any open files and disable the SD card
   f_close(&simple_logger_fpointer);
   disk_disable();
}

uint8_t simple_logger_log_string(const char *str)
{
   f_puts(str, &simple_logger_fpointer);
   FRESULT res = f_sync(&simple_logger_fpointer);
   if (res != FR_OK)
   {
      res = logger_init(1);
      if (res == FR_OK)
      {
         f_puts(str, &simple_logger_fpointer);
         res = f_sync(&simple_logger_fpointer);
      }
      else
         error();
   }
   return res;
}

uint8_t simple_logger_log_header(const char *format, ...)
{
   header_written = 1;

   va_list argptr;
   va_start(argptr, format);
   vsnprintf(header_buffer, buffer_size, format, argptr);
   va_end(argptr);

   if (!simple_logger_file_exists)
   {
      f_puts(header_buffer, &simple_logger_fpointer);
      FRESULT res = f_sync(&simple_logger_fpointer);
      if (res != FR_OK)
      {
         res = logger_init(1);
         if (res != FR_OK)
            error();
         return res;
      }
      return res;
   }
   else
      return SIMPLE_LOGGER_FILE_EXISTS;
}

// Log data
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

// Read data
uint8_t simple_logger_read(uint8_t* buf, uint8_t buf_len)
{
   // Buffer should be cleared before calling this function
   UINT read_len = 0;

   // Set read/write pointer back by amount we want to read
   FRESULT res = f_lseek(&simple_logger_fpointer, f_size(&simple_logger_fpointer) - buf_len);
   if (res != FR_OK)
   {
      printf("ERROR: Failed reverting R/W pointer: %i\n", res);
      error();
   }

   // Read string
   res = f_read(&simple_logger_fpointer, (void*)buf, buf_len, &read_len);
   if (read_len != buf_len)
      printf("ERROR: Should have read %i bytes, but only read %i\n", buf_len, read_len);
   if (res != FR_OK)
   {
      printf("ERROR: Failed reading from SD card: %i\n", res);
      error();
   }

   return res;
}

uint8_t simple_logger_list_files(char *file_name, uint32_t *file_size, uint8_t continuation)
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
         return simple_logger_list_files(file_name, file_size, 1);
      else
      {
         memcpy(file_name, file_info.fname, strlen(file_info.fname));
         *file_size = (uint32_t)file_info.fsize;
         return 1;
      }
   }
   return 0;
}

uint8_t simple_logger_delete_file(const char *file_name)
{
   return (f_unlink(file_name) == FR_OK);
}

uint8_t simple_logger_open_file_for_reading(const char *file_name)
{
   return (f_open(&file_for_reading, file_name, FA_READ | FA_OPEN_EXISTING) == FR_OK);
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
