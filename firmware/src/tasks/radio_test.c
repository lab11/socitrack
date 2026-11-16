// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"
#include "radio_test.h"
#include "rtc.h"
#include "system.h"
#include "timers.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

#define RADIO_TEST_SCRATCH_MAGIC                    0xA7000000u
#define RADIO_TEST_SCRATCH_MAGIC_MASK               0xFF000000u
#define RADIO_TEST_SCRATCH_END_MASK                 0x00FFFFFFu
#define RADIO_TEST_END_UNIT_S                       4u
#define RADIO_TEST_RESTART_DELAY_MS                 300

typedef struct
{
   uint32_t key;
   uint32_t start_time, end_time;
   uint8_t num_devices;
   uint8_t uids[MAX_NUM_RANGING_DEVICES][EUI_LEN];
   uint32_t check;
} radio_test_request_t;

static radio_test_request_t request __attribute__ ((section(".noinit")));
static experiment_details_t *test_details;
static volatile bool running, has_devices;
static StaticTimer_t restart_timer_buffer;
static TimerHandle_t restart_timer;


// Private Helper Functions --------------------------------------------------------------------------------------------

static uint32_t scratch_key(uint32_t end_time)
{
   return RADIO_TEST_SCRATCH_MAGIC | ((end_time / RADIO_TEST_END_UNIT_S) & RADIO_TEST_SCRATCH_END_MASK);
}

static uint32_t request_check(const radio_test_request_t *candidate)
{
   // FNV-1a over everything but the check itself, so memory left over from before the reset cannot pass
   uint32_t hash = 2166136261u;
   const uint8_t *bytes = (const uint8_t*)candidate;
   for (uint32_t i = 0; i < offsetof(radio_test_request_t, check); ++i)
      hash = (hash ^ bytes[i]) * 16777619u;
   return hash;
}

static void clear_request(void)
{
   MCUCTRL->SCRATCH1 = 0;
   memset(&request, 0, sizeof(request));
}

static void fill_details(void)
{
   // The device list as the scheduler, the range filters, and the Bluetooth whitelist all expect a deployment to give it
   memset(test_details, 0, sizeof(*test_details));
   test_details->experiment_start_time = request.start_time;
   test_details->experiment_end_time = request.end_time;
   test_details->num_devices = request.num_devices;
   memcpy(test_details->uids, request.uids, sizeof(request.uids));
}

static void restart_callback(TimerHandle_t timer)
{
   system_reset(false);
}

static void restart_soon(void)
{
   // Let the Bluetooth response go out before the restart
   if (!restart_timer)
      restart_timer = xTimerCreateStatic("RadioTest", pdMS_TO_TICKS(RADIO_TEST_RESTART_DELAY_MS), pdFALSE, NULL, restart_callback, &restart_timer_buffer);
   if (!restart_timer || (xTimerStart(restart_timer, 0) != pdPASS))
      system_reset(false);
}


// Public API Functions ------------------------------------------------------------------------------------------------

bool radio_test_boot(experiment_details_t *details)
{
   // Decide whether this boot runs a radio test, filling details with as much of it as survived the restart
   test_details = details;
   running = has_devices = false;
   const uint32_t scratch = MCUCTRL->SCRATCH1;
   if ((scratch & RADIO_TEST_SCRATCH_MAGIC_MASK) != RADIO_TEST_SCRATCH_MAGIC)
      return false;
   const uint32_t now = rtc_get_timestamp();
   const uint32_t units_left = ((scratch & RADIO_TEST_SCRATCH_END_MASK) - (now / RADIO_TEST_END_UNIT_S)) & RADIO_TEST_SCRATCH_END_MASK;
   if (!rtc_is_valid() || !units_left || (units_left > ((RADIO_TEST_MAX_SECONDS / RADIO_TEST_END_UNIT_S) + 1)))
   {
      clear_request();
      return false;
   }

   // Run the test whether or not the list made it
   running = true;
   if ((request.key == scratch) && (request.check == request_check(&request)) && request.num_devices && (request.num_devices <= MAX_NUM_RANGING_DEVICES) && (request.end_time > now))
   {
      fill_details();
      has_devices = true;
   }
   else
   {
      memset(details, 0, sizeof(*details));
      details->experiment_start_time = now;
      details->experiment_end_time = now + (units_left * RADIO_TEST_END_UNIT_S);
   }
   print("INFO: Booting into a radio test, %u s left, device list %s\n", details->experiment_end_time - now, has_devices ? "kept" : "to be resent");
   return true;
}

bool radio_test_running(void)
{
   return running;
}

bool radio_test_waiting_for_devices(void)
{
   return running && !has_devices;
}

uint32_t radio_test_seconds_left(void)
{
   const uint32_t now = rtc_get_timestamp();
   return (running && (test_details->experiment_end_time > now)) ? (test_details->experiment_end_time - now) : 0;
}

bool radio_test_start(uint32_t start_time, uint32_t end_time, const uint8_t *uids, uint8_t num_devices)
{
   // Refuse anything that could not run as asked
   const uint32_t now = rtc_get_timestamp();
   if (!rtc_is_valid() || !num_devices || (num_devices > MAX_NUM_RANGING_DEVICES) || (end_time <= now) || (end_time <= start_time) || ((end_time - now) > RADIO_TEST_MAX_SECONDS))
      return false;
   uint8_t own_uid[EUI_LEN];
   system_read_UID(own_uid, sizeof(own_uid));
   bool listed = false;
   for (uint8_t i = 0; !listed && (i < num_devices); ++i)
      listed = (memcmp(uids + (i * EUI_LEN), own_uid, EUI_LEN) == 0);
   if (!listed)
      return false;

   // A test already under way only ever needs the list it is waiting for
   if (running && has_devices)
      return true;
   memset(&request, 0, sizeof(request));
   request.key = scratch_key(end_time);
   request.start_time = start_time;
   request.end_time = end_time;
   request.num_devices = num_devices;
   memcpy(request.uids, uids, (uint32_t)num_devices * EUI_LEN);
   request.check = request_check(&request);
   MCUCTRL->SCRATCH1 = request.key;
   if (running)
   {
      // Restarted without its list: take this one and carry on, aligning the epoch with the rest of the test
      fill_details();
      app_set_experiment_start_time(request.start_time);
      has_devices = true;
   }
   else
      restart_soon();
   return true;
}

void radio_test_stop(void)
{
   // Forget the test, and leave it if this boot is running one
   const bool was_running = running;
   clear_request();
   if (was_running)
      restart_soon();
}

void radio_test_check_end(void)
{
   // A test ends with a restart into whatever the device would otherwise be doing
   if (running && !radio_test_seconds_left())
   {
      print("INFO: Radio test complete...restarting\n");
      clear_request();
      running = false;
      system_reset(false);
   }
}
