// Header inclusions ---------------------------------------------------------------------------------------------------

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"

#include <stdarg.h>
#include <string.h>
#include "ble_config.h"
#include "ble_gap.h"
#include "diskio_blkdev.h"
#include "ff.h"
#include "nrf_delay.h"
#include "nrfx_gpiote.h"
#include "nrf_block_dev_sdc.h"
#include "rtc.h"
#include "rtc_external.h"
#include "sd_card.h"
#include "squarepoint_interface.h"

#pragma GCC diagnostic pop


// SD Card block device functions and variables ------------------------------------------------------------------------

#define EUI_LEN BLE_GAP_ADDR_LEN
NRF_BLOCK_DEV_SDC_DEFINE(_sdcard_dev,
      NRF_BLOCK_DEV_SDC_CONFIG(SDC_SECTOR_SIZE,
            APP_SDCARD_CONFIG(SD_CARD_SPI_MOSI, SD_CARD_SPI_MISO, SD_CARD_SPI_SCLK, SD_CARD_SPI_CS)),
      NFR_BLOCK_DEV_INFO_CONFIG("TotTag", "SDCard", "1.00"));
static diskio_blkdev_t drives[] = { DISKIO_BLOCKDEV_CONFIG(NRF_BLOCKDEV_BASE_ADDR(_sdcard_dev, block_dev), NULL) };


// Static SD Card state variables --------------------------------------------------------------------------------------

static uint32_t _next_day_timestamp = 0;
static volatile uint16_t _sd_card_buffer_length = 0;
static const uint8_t _empty_eui[SQUAREPOINT_EUI_LEN] = { 0 };
static uint8_t _full_eui[EUI_LEN] = { 0 }, _sd_card_buffer[APP_SDCARD_BUFFER_LENGTH] = { 0 };
static char _log_ranges_buf[APP_LOG_BUFFER_LENGTH] = { 0 }, _sd_write_buf[255] = { 0 };
static char _sd_filename[16] = { 0 }, _sd_debug_filename[16] = { 0 };
static bool _sd_debug_file_inited = false, _log_file_exists = false;
static nrfx_atomic_flag_t *_sd_card_inserted = NULL;
static FATFS _file_system = { 0 };
static DIR _dir = { 0 };
static FILINFO _file_info = { 0 };
static FIL _file = { 0 }, _debug_file = { 0 }, _file_for_reading = { 0 };


// Private helper functions --------------------------------------------------------------------------------------------

static bool sd_card_power_on(void)
{
   // Power on the SD card
   nrf_gpio_pin_set(SD_CARD_ENABLE);

   // Initialize the SD card
   DSTATUS disk_state = STA_NOINIT;
   for (uint32_t retries = 10; retries && disk_state; --retries)
      disk_state = disk_initialize(0);
   if (disk_state)
   {
      printf("ERROR: Unable to initialize SD card!\n");
      disk_uninitialize(0);
      nrfx_atomic_flag_clear(_sd_card_inserted);
      nrf_gpio_pin_clear(SD_CARD_ENABLE);
      _sd_card_buffer_length = 0;
      return false;
   }
   else
      nrfx_atomic_flag_set(_sd_card_inserted);

   // Mount the SD card and filesystem
   FRESULT ff_result = f_mount(&_file_system, "", 1);
   switch (ff_result)
   {
      case FR_OK:
         break;
      case FR_NOT_READY:
         printf("ERROR: Unable to access SD card!\n");
         disk_uninitialize(0);
         nrf_gpio_pin_clear(SD_CARD_ENABLE);
         return false;
      case FR_NO_FILESYSTEM:
      {
         BYTE work[512];
         ff_result = f_mkfs("", FM_ANY, 0, work, sizeof(work));
         if (ff_result != FR_OK)
         {
            printf("ERROR: Failed to create a new SD card filesystem: %d\n", ff_result);
            disk_uninitialize(0);
            nrf_gpio_pin_clear(SD_CARD_ENABLE);
            return false;
         }
         ff_result = f_mount(&_file_system, "", 1);
         if (ff_result != FR_OK)
         {
            printf("ERROR: Unable to mount SD card filesystem: %d\n", ff_result);
            disk_uninitialize(0);
            nrf_gpio_pin_clear(SD_CARD_ENABLE);
            return false;
         }
         break;
      }
      default:
         printf("ERROR: Unexpected error while mounting SD card: %d\n", ff_result);
         disk_uninitialize(0);
         nrf_gpio_pin_clear(SD_CARD_ENABLE);
         return false;
   }

   // Initialize the file pointer if a valid filename has been set
   if (_sd_filename[0])
   {
      // See if the file already exists
      FIL temp;
      ff_result = f_open(&temp, _sd_filename, FA_READ | FA_OPEN_EXISTING);
      f_close(&temp);
      if (ff_result == FR_OK)
         _log_file_exists = 1;
      else if (ff_result == FR_NO_FILE)
         _log_file_exists = 0;
      else
      {
         printf("ERROR: Unexpected error while attempting to open SD card log file: %d\n", ff_result);
#ifndef PRINTF_TO_SD_CARD
         disk_uninitialize(0);
         nrf_gpio_pin_clear(SD_CARD_ENABLE);
#endif
         return false;
      }

      // Open the file
      ff_result = f_open(&_file, _sd_filename, FA_WRITE | FA_READ | FA_OPEN_APPEND);
      if (ff_result != FR_OK)
      {
         printf("ERROR: Unexpected error while attempting to open SD card log file: %d\n", ff_result);
         disk_uninitialize(0);
         nrf_gpio_pin_clear(SD_CARD_ENABLE);
      }
   }

   return (ff_result == FR_OK);
}

static void sd_card_power_off(void)
{
#ifndef PRINTF_TO_SD_CARD

   // Write any buffered data to the SD card
   f_close(&_file);
   disk_uninitialize(0);

   // Power off the SD card
   nrf_gpio_pin_clear(SD_CARD_ENABLE);

#endif
}

static bool sd_card_list_next_file(char *filename, uint32_t *file_size, uint8_t continuation)
{
   // Open the root directory
   FRESULT res = FR_OK;
   if (!continuation)
   {
      f_closedir(&_dir);
      res = f_opendir(&_dir, "/");
   }

   // Read the next directory item
   if (res == FR_OK)
   {
      // Close the directory if no more contents exist
      res = f_readdir(&_dir, &_file_info);
      if ((res != FR_OK) || (_file_info.fname[0] == 0))
      {
         f_closedir(&_dir);
         return false;
      }

      // Ignore the system log file
      if ((_file_info.fname[0] == 'S') && (_file_info.fname[1] == 'Y') && (_file_info.fname[2] == 'S') &&
            (_file_info.fname[3] == 'T') && (_file_info.fname[4] == 'E') && (_file_info.fname[5] == 'M'))
         return sd_card_list_next_file(filename, file_size, 1);
      else
      {
         strcpy(filename, _file_info.fname);
         *file_size = (uint32_t)_file_info.fsize;
         return true;
      }
   }
   return false;
}

static __attribute__ ((format (printf, 1, 2))) uint8_t sd_card_log_header(const char *format, ...)
{
   // Print the header to a string
   va_list argptr;
   va_start(argptr, format);
   vsnprintf(_sd_write_buf, sizeof(_sd_write_buf) - 1, format, argptr);
   va_end(argptr);

   // Write the header string to the SD card if the log file is new
   if (!_log_file_exists)
   {
      if (f_puts(_sd_write_buf, &_file) < 0)
      {
         printf("ERROR: Problem writing header to SD card!\n");
         return 1;
      }
      _log_file_exists = 1;
      return (f_sync(&_file) != FR_OK) ? 2 : 0;
   }

   // Do not write the header if the log file already existed
   return 3;
}

// Public SD Card functionality ----------------------------------------------------------------------------------------

bool sd_card_init(nrfx_atomic_flag_t* sd_card_inserted_flag, const uint8_t* full_eui)
{
   // Store local variables
   _sd_card_inserted = sd_card_inserted_flag;
   memcpy(_full_eui, full_eui, sizeof(_full_eui));
   diskio_blockdev_register(drives, ARRAY_SIZE(drives));
   nrf_gpio_cfg_input(SD_CARD_DETECT, NRF_GPIO_PIN_NOPULL);

   // Initialize the SD card
   printf("INFO: Initializing SD Card...\n");
   if (!sd_card_power_on())
      return false;

   // Calculate the SD card capacity
   uint32_t blocks_per_mb = (1024uL * 1024uL) / _sdcard_dev.block_dev.p_ops->geometry(&_sdcard_dev.block_dev)->blk_size;
   uint32_t capacity = _sdcard_dev.block_dev.p_ops->geometry(&_sdcard_dev.block_dev)->blk_count / blocks_per_mb;
   printf("INFO:    Card capacity: %lu MB\n", capacity);

   // Clean up any unexpected SD card files
   uint32_t file_size = 0;
   uint8_t continuation = 0;
   char file_name[32] = { 0 };
   while (sd_card_list_next_file(file_name, &file_size, continuation++))
   {
      printf("INFO:    Existing file found: %s\n", file_name);
      if (file_name[2] != '@')
      {
         f_unlink(file_name);
         continuation = 0;
      }
   }

   // Power off the SD card
   sd_card_power_off();
   return true;
}

void sd_card_flush(void)
{
   // Ensure that there is data to flush
   if (!_sd_card_buffer_length)
      return;

   // Power ON the SD card
#ifndef PRINTF_TO_SD_CARD
   if (!sd_card_power_on())
   {
      printf("ERROR: Unable to power on the SD Card!\n");
      sd_card_power_off();
      return;
   }
#else
   if (nrf_gpio_pin_read(SD_CARD_DETECT))
   {
      // No SD card detected
      printf("ERROR: SD card not detected!\n");
      nrfx_atomic_flag_clear(_sd_card_inserted);
      _sd_card_buffer_length = 0;
      return;
   }
   else
      nrfx_atomic_flag_set(_sd_card_inserted);
#endif

   // Send data in chunks of 254 bytes, as this is the maximum which the nRF DMA can handle
   uint8_t nr_writes = (_sd_card_buffer_length / (sizeof(_sd_write_buf) - 1)) + ((_sd_card_buffer_length % (sizeof(_sd_write_buf) - 1)) ? 1 : 0);
   for (uint8_t i = 0; i < nr_writes; ++i)
   {
      // Copy chunks to the write buffer and log data
      if (i == (nr_writes - 1))             // Last chunk
      {
         uint8_t rest_length = _sd_card_buffer_length - (i * (sizeof(_sd_write_buf) - 1));
         memcpy(_sd_write_buf, _sd_card_buffer + (i * (sizeof(_sd_write_buf) - 1)), rest_length);
         _sd_write_buf[rest_length] = '\0';
      }
      else
      {
         memcpy(_sd_write_buf, _sd_card_buffer + (i * (sizeof(_sd_write_buf) - 1)), sizeof(_sd_write_buf) - 1);
         _sd_write_buf[sizeof(_sd_write_buf)-1] = '\0';
      }

      // Attempt to write the data string to the SD card
      if ((f_puts(_sd_write_buf, &_file) < 0) || (f_sync(&_file) != FR_OK))
         printf("ERROR: Problem writing data string to SD card!\n");
   }

   // Reset the buffer and turn off power to the SD card
   _sd_card_buffer_length = 0;
   sd_card_power_off();
}

bool sd_card_create_log(uint32_t current_time)
{
   // Flush any data waiting to be written to the SD card
   sd_card_flush();

   // Power ON the SD card
#ifndef PRINTF_TO_SD_CARD
   if (!sd_card_power_on())
   {
      printf("ERROR: Unable to power on the SD Card!\n");
      sd_card_power_off();
      return false;
   }
#endif

   // Create a string containing the current device's EUI
   char EUI_string[(3 * EUI_LEN) + 1] = { 0 };
   for (uint8_t i = 0; i < EUI_LEN; ++i)
      sprintf(EUI_string + (i * 3), "%02x:", _full_eui[EUI_LEN - 1 - i]);
   EUI_string[(3 * EUI_LEN) - 1] = '\0';

   // Initialize differently based on the board revision
#if (BOARD_V >= 0x0F)

   // Name the log file based on the date if the current time is valid
   if (current_time)
   {
      // Calculate the current local and UTC time and the timestamp of midnight local time
      time_t curr_time = (time_t)current_time;
      struct tm *t = gmtime(&curr_time);
      ab1815_time_t time = tm_to_ab1815(t);
      t->tm_sec = t->tm_min = t->tm_hour = 0;
      t->tm_mday += 1;
      _next_day_timestamp = mktime(t);

      // Create a log file name based on the current date
      snprintf(_sd_filename, sizeof(_sd_filename), "%02X@%02u-%02u.log", _full_eui[0], time.months, time.date);
      if (!sd_card_power_on())
      {
         printf("ERROR: Unable to re-initialize the SD card with the new file name: %s\n", _sd_filename);
         sd_card_power_off();
         return false;
      }

      // If no header, add it
      uint8_t ret_val = sd_card_log_header("### HEADER for file \'%s\'; Device: %s; Firmware: %s; Date: 20%02u/%02u/%02u %02u:%02u:%02u; Timestamp: %lld\n", _sd_filename, EUI_string, FIRMWARE_VERSION, time.years, time.months, time.date, time.hours, time.minutes, time.seconds, curr_time);
      if (ret_val == 3)
      {
         snprintf(_sd_write_buf, sizeof(_sd_write_buf), "### Device booted at 20%02u/%02u/%02u %02u:%02u:%02u; Timestamp: %lld\n", time.years, time.months, time.date, time.hours, time.minutes, time.seconds, curr_time);
         if (f_puts(_sd_write_buf, &_file) == FR_OK)
            ret_val = f_sync(&_file);
      }
      else if (ret_val)
      {
         printf("ERROR: Problem writing header to the SD card log file!\n");
         sd_card_power_off();
         return false;
      }
   }
   else
   {
      // Create a log file with a generic name
      snprintf(_sd_filename, sizeof(_sd_filename), "%02X@data.log", _full_eui[0]);
      if (!sd_card_power_on())
      {
         printf("ERROR: Unable to re-initialize the SD card with the new file name: %s\n", _sd_filename);
         sd_card_power_off();
         return false;
      }

      // If no header, add it
      uint8_t ret_val = sd_card_log_header("### HEADER for file \'%s\'; Device: %s; Firmware: %s\n", _sd_filename, EUI_string, FIRMWARE_VERSION);
      if (ret_val == 3)
      {
         snprintf(_sd_write_buf, sizeof(_sd_write_buf), "### Device rebooted\n");
         if (f_puts(_sd_write_buf, &_file) == FR_OK)
            ret_val = f_sync(&_file);
      }
      else if (ret_val)
      {
         printf("ERROR: Problem writing header to the SD card log file!\n");
         sd_card_power_off();
         return false;
      }
   }

#else

   // Create a log file with a generic name
   snprintf(_sd_filename, sizeof(_sd_filename), "%02X@data.log", _full_eui[0]);
   if (!sd_card_power_on())
   {
      printf("ERROR: Unable to re-initialize the SD card with the new file name: %s\n", _sd_filename);
      sd_card_power_off();
      return false;
   }

   // If no header, add it
   uint8_t ret_val = sd_card_log_header("### HEADER for file \'%s\'; Device: %s; Firmware: %s\n", _sd_filename, EUI_string, FIRMWARE_VERSION);
   if (ret_val == 3)
   {
      snprintf(_sd_write_buf, sizeof(_sd_write_buf), "### Device rebooted\n");
      if (f_puts(_sd_write_buf, &_file) == FR_OK)
         ret_val = f_sync(&_file);
   }
   else if (ret_val)
   {
      printf("ERROR: Problem writing header to the SD card log file!\n");
      sd_card_power_off();
      return false;
   }

#endif

   // Power OFF the SD card
   sd_card_power_off();
   return true;
}

void sd_card_write(const char *data, uint16_t length, bool flush)
{
   // Flush the buffer if necessary
   if ((APP_SDCARD_BUFFER_LENGTH - _sd_card_buffer_length - 1) <= length)
      sd_card_flush();

   // Append data to the buffer
   memcpy(_sd_card_buffer + _sd_card_buffer_length, data, length);
   _sd_card_buffer_length += length;

   // Flush the buffer if requested
   if (flush)
      sd_card_flush();
}

bool sd_card_list_files(char *file_name, uint32_t *file_size, uint8_t continuation)
{
#ifndef PRINTF_TO_SD_CARD
   if (!continuation)
      sd_card_power_on();
   bool ret_val = sd_card_list_next_file(file_name, file_size, continuation);
   if (!ret_val)
      sd_card_power_off();
   return ret_val;
#else
   return sd_card_list_next_file(file_name, file_size, continuation);
#endif
}

bool sd_card_erase_file(const char *file_name)
{
#ifndef PRINTF_TO_SD_CARD
   sd_card_power_on();
   bool ret_val = (f_unlink(file_name) == FR_OK);
   sd_card_power_off();
   return ret_val;
#else
   return (f_unlink(file_name) == FR_OK);
#endif
}

bool sd_card_open_file_for_reading(const char *file_name)
{
#ifndef PRINTF_TO_SD_CARD
   sd_card_power_on();
#endif
   return (f_open(&_file_for_reading, file_name, FA_READ | FA_OPEN_EXISTING) == FR_OK);
}

void sd_card_close_reading_file(void)
{
   f_close(&_file_for_reading);
   sd_card_power_off();
}

uint32_t sd_card_read_reading_file(uint8_t *data_buffer, uint32_t buffer_length)
{
   UINT bytes_read = 0;
   return (f_read(&_file_for_reading, data_buffer, buffer_length, &bytes_read) == FR_OK) ? (uint32_t)bytes_read : 0;
}

int sd_card_printf(const char *__restrict format, ...)
{
   // Create an SD card debug log file
   if (!_sd_debug_file_inited)
   {
      snprintf(_sd_debug_filename, sizeof(_sd_filename), "%02X@dbg.log", _full_eui[0]);
      _sd_debug_file_inited = (f_open(&_debug_file, _sd_debug_filename, FA_READ | FA_WRITE | FA_OPEN_APPEND) == FR_OK);
   }

   // Print log messages to the console
   va_list argptr1;
   va_start(argptr1, format);
   vprintf(format, argptr1);
   va_end(argptr1);

   // Print log messages to the SD card
   va_list argptr2;
   va_start(argptr2, format);
   snprintf(_sd_write_buf, sizeof(_sd_write_buf) - 1, "%lu: ", rtc_get_current_time());
   f_puts(_sd_write_buf, &_debug_file);
   vsnprintf(_sd_write_buf, sizeof(_sd_write_buf) - 1, format, argptr2);
   f_puts(_sd_write_buf, &_debug_file);
   int res = f_sync(&_debug_file);
   va_end(argptr2);
   return res;
}

void sd_card_log_ranges(const uint8_t *data, uint16_t length)
{
#ifndef BLE_CALIBRATION

   // Jump over interrupt reason and determine data length
   uint16_t offset_data = 1 + SQUAREPOINT_EUI_LEN, offset_buf = 0;
   uint8_t num_ranges = data[0];
   if (((length - offset_data - 4) / APP_LOG_RANGE_LENGTH) != num_ranges)
   {
      printf("WARNING: Attempting to log an incorrect number of ranges!\n");
      return;
   }

   // Get timestamp of ranges
   uint32_t current_timestamp = 0, range = 0;
   memcpy(&current_timestamp, data + offset_data + (num_ranges * APP_LOG_RANGE_LENGTH), sizeof(current_timestamp));

   // Log all ranges
   memset(_log_ranges_buf, 0, sizeof(_log_ranges_buf));
   for (uint8_t i = 0; i < num_ranges; ++i)
   {
      // Ignore blank EUIs
      if (memcmp(data + offset_data, _empty_eui, SQUAREPOINT_EUI_LEN) == 0)
      {
         offset_data += APP_LOG_RANGE_LENGTH;
         continue;
      }

      // Write Timestamp
      sprintf(_log_ranges_buf + offset_buf + 0, "%010lu\t", current_timestamp);

      // Write Full EUI
      sprintf(_log_ranges_buf + offset_buf + 11, "%02x", _full_eui[EUI_LEN - 1]);
      for (uint8_t j = 1; j < (EUI_LEN - SQUAREPOINT_EUI_LEN); ++j)
         sprintf(_log_ranges_buf + offset_buf + 10 + (j * 3), ":%02x", _full_eui[EUI_LEN - 1 - j]);
      for (int8_t j = (SQUAREPOINT_EUI_LEN - 1), index = 3 * (EUI_LEN - SQUAREPOINT_EUI_LEN); j >= 0; --j, index += 3)
         sprintf(_log_ranges_buf + offset_buf + 10 + index, ":%02x", data[offset_data + j]);
      sprintf(_log_ranges_buf + offset_buf + 28, "\t");

      // Write range
      memcpy(&range, data + offset_data + SQUAREPOINT_EUI_LEN, sizeof(range));
      if (range > APP_LOG_OUT_OF_RANGE_VALUE)
         range = APP_LOG_OUT_OF_RANGE_VALUE;
      sprintf(_log_ranges_buf + offset_buf + 29, "%06lu\n", range);

      // Update the data and buffer offsets
      offset_data += APP_LOG_RANGE_LENGTH;
      offset_buf += APP_LOG_BUFFER_LINE;
   }

   // Start a new log file if it is a new day
   if (_next_day_timestamp && (current_timestamp >= _next_day_timestamp))
   {
      printf("INFO: Starting new SD card log file...new day detected\n");
      sd_card_create_log(current_timestamp);
   }

   // Write buffer to the SD card
   sd_card_write(_log_ranges_buf, offset_buf, false);

#endif
}

void sd_card_log_battery(uint16_t battery_millivolts, uint32_t current_time, bool flush)
{
   // Start a new log file if it is a new day
   if (_next_day_timestamp && (current_time >= _next_day_timestamp))
   {
      printf("INFO: Starting new SD card log file...new day detected\n");
      sd_card_create_log(current_time);
   }

   uint16_t bytes_written = (uint16_t)snprintf(_sd_write_buf, sizeof(_sd_write_buf), "### BATTERY VOLTAGE: %hu mV; Timestamp: %lu\n", battery_millivolts, current_time);
   sd_card_write(_sd_write_buf, bytes_written, flush);
}

void sd_card_log_charging(bool plugged_in, bool is_charging, uint32_t current_time, bool flush)
{
   // Start a new log file if it is a new day
   if (_next_day_timestamp && (current_time >= _next_day_timestamp))
   {
      printf("INFO: Starting new SD card log file...new day detected\n");
      sd_card_create_log(current_time);
   }

   printf("INFO: Device is%s PLUGGED IN and%s CHARGING!\n", plugged_in ? "" : " NOT", is_charging ? "" : " NOT");
   uint16_t bytes_written = (uint16_t)snprintf(_sd_write_buf, sizeof(_sd_write_buf),
         "### CHARGING STATUS:%s PLUGGED IN and%s CHARGING; Timestamp: %lu\n", plugged_in ? "" : " NOT", is_charging ? "" : " NOT", current_time);
   sd_card_write(_sd_write_buf, bytes_written, flush);
}

void sd_card_log_motion(bool in_motion, uint32_t current_time, bool flush)
{
   // Start a new log file if it is a new day
   if (_next_day_timestamp && (current_time >= _next_day_timestamp))
   {
      printf("INFO: Starting new SD card log file...new day detected\n");
      sd_card_create_log(current_time);
   }

   printf("INFO: Device is now %s\n", in_motion ? "IN MOTION" : "STATIONARY");
   uint16_t bytes_written = (uint16_t)snprintf(_sd_write_buf, sizeof(_sd_write_buf), "### MOTION CHANGE: %s; Timestamp: %lu\n", in_motion ? "IN MOTION" : "STATIONARY", current_time);
   sd_card_write(_sd_write_buf, bytes_written, flush);
}
