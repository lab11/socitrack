#include "app_tasks.h"
#include "system.h"

int main(void)
{
   // Set up hardware and run all application tasks
   setup_hardware();
   run_tasks();
   return 0;
}
