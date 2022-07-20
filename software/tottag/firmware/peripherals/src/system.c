// Header inclusions ---------------------------------------------------------------------------------------------------

#include "system.h"

void initialize_gpio(void)
{
   // Initialize the GPIO subsystem
   if (!nrfx_gpiote_is_init())
      nrfx_gpiote_init();
}
