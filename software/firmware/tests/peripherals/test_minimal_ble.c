#include "logging.h"
#include "system.h"
#include "hci_drv_cooper.h"
#include "hci_drv_apollo.h"

int main(void)
{
   // Set up system hardware
   setup_hardware();
   
   configASSERT0(HciDrvRadioBoot(false));
   
   print("before the loop");
   
   while (true)
   {
	   ; 
   }
   // Should never reach this point
   return 0;
}