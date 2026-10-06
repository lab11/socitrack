#ifndef __RADIO_TEST_HEADER_H__
#define __RADIO_TEST_HEADER_H__

// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"


// Radio Test Definitions ----------------------------------------------------------------------------------------------

#define RADIO_TEST_MAX_SECONDS                      3600
#define RADIO_TEST_LIST_WAIT_S                      120


// Public API ----------------------------------------------------------------------------------------------------------

bool radio_test_boot(experiment_details_t *details);
bool radio_test_running(void);
bool radio_test_waiting_for_devices(void);
uint32_t radio_test_seconds_left(void);
bool radio_test_start(uint32_t start_time, uint32_t end_time, const uint8_t *uids, uint8_t num_devices);
void radio_test_stop(void);
void radio_test_check_end(void);

#endif  // #ifndef __RADIO_TEST_HEADER_H__
