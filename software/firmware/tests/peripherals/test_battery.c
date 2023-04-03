#include "battery.h"
#include "logging.h"
#include "system.h"

static void battery_event_handler(battery_event_t event)
{
   switch (event)
   {
      case BATTERY_PLUGGED:
         print("Received BATTERY_PLUGGED event!\n");
         break;
      case BATTERY_UNPLUGGED:
         print("Received BATTERY_UNPLUGGED event!\n");
         break;
      case BATTERY_CHARGING:
         print("Received BATTERY_CHARGING event!\n");
         break;
      case BATTERY_NOT_CHARGING:
         print("Received BATTERY_NOT_CHARGING event!\n");
         break;
      default:
         print("Received UNKNOWN event: %u!\n", event);
         break;
   }
}

int main(void)
{
   // Set up system hardware
   setup_hardware();
   battery_monitor_init();
   system_enable_interrupts(true);

   // Retrieve battery status every 15 seconds or upon a charging status change
   battery_register_event_callback(battery_event_handler);
   while (true)
   {
      uint32_t battery_voltage = battery_monitor_get_level_mV();
      print("Battery Status: %s and %s, %u mV, Level is %s\n",
            battery_monitor_is_plugged_in() ? "PLUGGED IN" : "UNPLUGGED",
            battery_monitor_is_charging() ? "CHARGING" : "NOT CHARGING",
            battery_voltage,
            (battery_voltage <= BATTERY_EMPTY) ? "EMPTY" :
               (battery_voltage <= BATTERY_CRITICAL) ? "CRITICAL" :
                  (battery_voltage <= BATTERY_NOMINAL) ? "NOMINAL" : "FULL");
      am_hal_delay_us(15000000);
   }

   // Should never reach this point
   return 0;
}
