// Header inclusions ---------------------------------------------------------------------------------------------------

#include <stdarg.h>
#include <string.h>
#include "ble_config.h"
#include "ble_gap.h"
#include "nrf_delay.h"
#include "nrfx_gpiote.h"
#include "rtc.h"
#include "rtc_external.h"
#include "sd_card.h"
#include "simple_logger.h"
#include "squarepoint_interface.h"


// Static SD Card state variables --------------------------------------------------------------------------------------

#define EUI_LEN BLE_GAP_ADDR_LEN

static uint32_t _next_day_timestamp = 0;
static volatile uint16_t _sd_card_buffer_length = 0;
static const uint8_t _empty_eui[SQUAREPOINT_EUI_LEN] = { 0 };
static uint8_t _full_eui[EUI_LEN] = { 0 }, _sd_card_buffer[APP_SDCARD_BUFFER_LENGTH] = { 0 };
static char _log_ranges_buf[APP_LOG_BUFFER_LENGTH] = { 0 }, _log_info_buf[128] = { 0 };
static char _sd_write_buf[APP_LOG_CHUNK_SIZE + 1] = { 0 };
static char _sd_filename[16] = { 0 }, _sd_debug_filename[16] = { 0 };
static const char _sd_permissions[] = "a,r";
static bool _sd_debug_file_inited = false;
static nrfx_atomic_flag_t *_sd_card_inserted = NULL;


// Private helper functions --------------------------------------------------------------------------------------------

static void flush_sd_buffer(void)
{
   // Ensure there is data to flush
   if (!_sd_card_buffer_length)
      return;

   // Power ON the SD card
#ifndef PRINTF_TO_SD_CARD
   if (simple_logger_power_on())
   {
      simple_logger_power_off();
      return;
   }
#endif

   // Ensure that the SD card is present
   if (nrf_gpio_pin_read(CARRIER_SD_DETECT))
   {
      // No SD card detected
      printf("ERROR: SD card not detected!\n");
      nrfx_atomic_flag_clear(_sd_card_inserted);
      _sd_card_buffer_length = 0;
      memset(_sd_card_buffer, 0, sizeof(_sd_card_buffer));
#ifndef PRINTF_TO_SD_CARD
      simple_logger_power_off();
#endif
      return;
   }
   else
      nrfx_atomic_flag_set(_sd_card_inserted);

   // Send data in chunks of 254 bytes, as this is the maximum which the nRF DMA can handle
   uint8_t nr_writes = (_sd_card_buffer_length / APP_LOG_CHUNK_SIZE) + ((_sd_card_buffer_length % APP_LOG_CHUNK_SIZE) ? 1 : 0);
   for (uint8_t i = 0; i < nr_writes; ++i)
   {
      // Copy chunks to the write buffer and log data
      if (i == (nr_writes - 1))             // Last chunk
      {
         uint8_t rest_length = _sd_card_buffer_length - (i * APP_LOG_CHUNK_SIZE);
         memcpy(_sd_write_buf, _sd_card_buffer + (i * APP_LOG_CHUNK_SIZE), rest_length);
         _sd_write_buf[rest_length] = '\0';
      }
      else
      {
         memcpy(_sd_write_buf, _sd_card_buffer + (i * APP_LOG_CHUNK_SIZE), APP_LOG_CHUNK_SIZE);
         _sd_write_buf[APP_LOG_CHUNK_SIZE] = '\0';
      }
      simple_logger_log_string(_sd_write_buf);
   }

   // Reset the buffer and turn off power to the SD card
   _sd_card_buffer_length = 0;
   memset(_sd_card_buffer, 0, sizeof(_sd_card_buffer));
#ifndef PRINTF_TO_SD_CARD
   simple_logger_power_off();
#endif
}


// Public SD Card functionality ----------------------------------------------------------------------------------------

bool sd_card_init(nrfx_atomic_flag_t* sd_card_inserted_flag, const uint8_t* full_eui)
{
   // Store local variables
   _sd_card_inserted = sd_card_inserted_flag;
   memcpy(_full_eui, full_eui, sizeof(_full_eui));

   // Enable SD card
   nrf_gpio_cfg_input(CARRIER_SD_DETECT, NRF_GPIO_PIN_NOPULL);
   nrfx_gpiote_out_set(CARRIER_CS_SD);
   nrfx_gpiote_out_set(CARRIER_SD_ENABLE);

   // Initialize SD card logger
   if (simple_logger_init())
      return false;

   // Clean up any unexpected SD card files
   uint32_t file_size = 0;
   uint8_t continuation = 0;
   char file_name[16] = { 0 };
   while (simple_logger_list_files(file_name, &file_size, continuation++))
      if (file_name[2] != '@')
         simple_logger_delete_file(file_name);

   // Ensure SD card is physically present
   if (nrf_gpio_pin_read(CARRIER_SD_DETECT))
   {
      nrfx_atomic_flag_clear(_sd_card_inserted);
      return false;
   }

   // Clear SD card presence flag
   nrfx_atomic_flag_set(_sd_card_inserted);
   return true;
}

void sd_card_create_log(uint32_t current_time)
{
   // Flush any data waiting to be written to the SD card
   flush_sd_buffer();
   nrf_delay_ms(250);

   // Power ON the SD card
#ifndef PRINTF_TO_SD_CARD
   simple_logger_power_on();
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
      simple_logger_reinit(_sd_filename, _sd_permissions);

      // If no header, add it
      uint8_t ret_val = simple_logger_log_header("### HEADER for file \'%s\'; Device: %s; Firmware: %s; Date: 20%02u/%02u/%02u %02u:%02u:%02u; Timestamp: %lld\n", _sd_filename, EUI_string, FIRMWARE_VERSION, time.years, time.months, time.date, time.hours, time.minutes, time.seconds, curr_time);
      if (ret_val == SIMPLE_LOGGER_FILE_EXISTS)
         ret_val = simple_logger_log("### Device booted at 20%02u/%02u/%02u %02u:%02u:%02u; Timestamp: %lld\n", time.years, time.months, time.date, time.hours, time.minutes, time.seconds, curr_time);
   }
   else
   {
      // Create a log file with a generic name
      snprintf(_sd_filename, sizeof(_sd_filename), "%02X@data.log", _full_eui[0]);
      simple_logger_reinit(_sd_filename, _sd_permissions);

      // If no header, add it
      uint8_t ret_val = simple_logger_log_header("### HEADER for file \'%s\'; Device: %s; Firmware: %s\n", _sd_filename, EUI_string, FIRMWARE_VERSION);
      if (ret_val == SIMPLE_LOGGER_FILE_EXISTS)
         ret_val = simple_logger_log("### Device rebooted\n");
   }

#else

   // Create a log file with a generic name
   snprintf(_sd_filename, sizeof(_sd_filename), "%02X@data.log", _full_eui[0]);
   simple_logger_reinit(_sd_filename, _sd_permissions);

   // If no header, add it
   uint8_t ret_val = simple_logger_log_header("### HEADER for file \'%s\'; Device: %s; Firmware: %s\n", _sd_filename, EUI_string, FIRMWARE_VERSION);
   if (ret_val == SIMPLE_LOGGER_FILE_EXISTS)
      ret_val = simple_logger_log("### Device rebooted\n");

#endif

   // Power OFF the SD card
#ifndef PRINTF_TO_SD_CARD
   simple_logger_power_off();
#endif
}

void sd_card_write(const char *data, uint16_t length, bool flush)
{
   // Flush the buffer if requested or necessary
   if (flush || ((APP_SDCARD_BUFFER_LENGTH - _sd_card_buffer_length - 1) <= length))
      flush_sd_buffer();

   // Append data to the buffer
   memcpy(_sd_card_buffer + _sd_card_buffer_length, data, length);
   _sd_card_buffer_length += length;
}

bool sd_card_list_files(char *file_name, uint32_t *file_size, uint8_t continuation)
{
#ifndef PRINTF_TO_SD_CARD
   if (!continuation)
      simple_logger_power_on();
   uint8_t ret_val = simple_logger_list_files(file_name, file_size, continuation);
   if (!ret_val)
      simple_logger_power_off();
   return ret_val;
#else
   return simple_logger_list_files(file_name, file_size, continuation);
#endif
}

bool sd_card_erase_file(const char *file_name)
{
#ifndef PRINTF_TO_SD_CARD
   simple_logger_power_on();
   uint8_t ret_val = simple_logger_delete_file(file_name);
   simple_logger_power_off();
   return ret_val;
#else
   return simple_logger_delete_file(file_name);
#endif
}

bool sd_card_open_file_for_reading(const char *file_name)
{
#ifndef PRINTF_TO_SD_CARD
   simple_logger_power_on();
#endif
   return simple_logger_open_file_for_reading(file_name);
}

void sd_card_close_reading_file(void)
{
   simple_logger_close_reading_file();
#ifndef PRINTF_TO_SD_CARD
   simple_logger_power_off();
#endif
}

uint32_t sd_card_read_reading_file(uint8_t *data_buffer, uint32_t buffer_length)
{
   return simple_logger_read_reading_file(data_buffer, buffer_length);
}

int sd_card_printf(const char *__restrict format, ...)
{
   // Create an SD card debug log file
   if (!_sd_debug_file_inited)
   {
      snprintf(_sd_debug_filename, sizeof(_sd_filename), "%02X@dbg.log", _full_eui[0]);
      _sd_debug_file_inited = (simple_logger_init_debug(_sd_debug_filename) == 0);
   }

   // Print log messages to the console
   va_list argptr1;
   va_start(argptr1, format);
   vprintf(format, argptr1);
   va_end(argptr1);

   // Print log messages to the SD card
   va_list argptr2;
   va_start(argptr2, format);
   int res = simple_logger_printf(rtc_get_current_time(), format, argptr2);
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
      log_printf("WARNING: Attempting to log an incorrect number of ranges!\n");
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
      log_printf("INFO: Starting new SD card log file...new day detected\n");
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
      log_printf("INFO: Starting new SD card log file...new day detected\n");
      sd_card_create_log(current_time);
   }

   uint16_t bytes_written = (uint16_t)snprintf(_log_info_buf, sizeof(_log_info_buf), "### BATTERY VOLTAGE: %hu mV; Timestamp: %lu\n", battery_millivolts, current_time);
   sd_card_write(_log_info_buf, bytes_written, flush);
}

void sd_card_log_charging(bool plugged_in, bool is_charging, uint32_t current_time, bool flush)
{
   // Start a new log file if it is a new day
   if (_next_day_timestamp && (current_time >= _next_day_timestamp))
   {
      log_printf("INFO: Starting new SD card log file...new day detected\n");
      sd_card_create_log(current_time);
   }

   log_printf("INFO: Device is%s PLUGGED IN and%s CHARGING!\n", plugged_in ? "" : " NOT", is_charging ? "" : " NOT");
   uint16_t bytes_written = (uint16_t)snprintf(_log_info_buf, sizeof(_log_info_buf),
         "### CHARGING STATUS:%s PLUGGED IN and%s CHARGING; Timestamp: %lu\n", plugged_in ? "" : " NOT", is_charging ? "" : " NOT", current_time);
   sd_card_write(_log_info_buf, bytes_written, flush);
}

void sd_card_log_motion(bool in_motion, uint32_t current_time, bool flush)
{
   // Start a new log file if it is a new day
   if (_next_day_timestamp && (current_time >= _next_day_timestamp))
   {
      log_printf("INFO: Starting new SD card log file...new day detected\n");
      sd_card_create_log(current_time);
   }

   log_printf("INFO: Device is now %s\n", in_motion ? "IN MOTION" : "STATIONARY");
   uint16_t bytes_written = (uint16_t)snprintf(_log_info_buf, sizeof(_log_info_buf), "### MOTION CHANGE: %s; Timestamp: %lu\n", in_motion ? "IN MOTION" : "STATIONARY", current_time);
   sd_card_write(_log_info_buf, bytes_written, flush);
}
