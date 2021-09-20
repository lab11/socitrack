#ifndef __SD_CARD_HEADER_H
#define __SD_CARD_HEADER_H

// Header inclusions ---------------------------------------------------------------------------------------------------

#include "nrfx_atomic.h"


// Public SD Card API --------------------------------------------------------------------------------------------------

bool sd_card_init(nrfx_atomic_flag_t* sd_card_inserted_flag, const uint8_t* full_eui);
bool sd_card_flush(void);
bool sd_card_create_log(uint32_t current_time, bool is_device_reboot);
void sd_card_write(const char *data, uint16_t length, bool flush);
bool sd_card_list_files(char *file_name, uint32_t *file_size, uint8_t continuation);
bool sd_card_erase_file(const char *file_name);
bool sd_card_open_file_for_reading(const char *file_name);
void sd_card_close_reading_file(void);
uint32_t sd_card_get_reading_file_size_bytes(void);
uint32_t sd_card_read_reading_file(uint8_t *data_buffer, uint32_t buffer_length);
int sd_card_printf(const char *__restrict format, ...) __attribute__ ((format (printf, 1, 2)));
void sd_card_log_ranges(const uint8_t *data, uint16_t length);
void sd_card_log_battery(uint16_t battery_millivolts, uint32_t current_time, bool flush);
void sd_card_log_charging(bool plugged_in, bool is_charging, uint32_t current_time, bool flush);
void sd_card_log_motion(bool in_motion, uint32_t current_time, bool flush);

#endif // #ifndef __SD_CARD_HEADER_H
