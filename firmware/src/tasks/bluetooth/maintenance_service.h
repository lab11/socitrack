#ifndef __MAINTENANCE_SERVICE_HEADER_H__
#define __MAINTENANCE_SERVICE_HEADER_H__

// TotTag Maintenance Handle Enumerations ------------------------------------------------------------------------------

enum
{
   MAINTENANCE_SERVICE_HANDLE = 0x0070,     // TotTag maintenance service
   MAINTENANCE_EXPERIMENT_CHAR_HANDLE,      // Current experiment details characteristic
   MAINTENANCE_EXPERIMENT_HANDLE,           // Current experiment details
   MAINTENANCE_EXPERIMENT_DESC_HANDLE,      // Current experiment details description
   MAINTENANCE_COMMAND_CHAR_HANDLE,         // Maintenance command characteristic
   MAINTENANCE_COMMAND_HANDLE,              // Maintenance command
   MAINTENANCE_COMMAND_DESC_HANDLE,         // Maintenance command description
   MAINTENANCE_RESULT_CHAR_HANDLE,          // Maintenance command result characteristic
   MAINTENANCE_RESULT_HANDLE,               // Maintenance command result
   MAINTENANCE_RESULT_DESC_HANDLE,          // Maintenance command result description
   MAINTENANCE_RESULT_CCC_HANDLE,           // Maintenance command result client characteristic configuration descriptor
   MAINTENANCE_MAX_HANDLE                   // Maximum live statistics handle
};


// Public API ----------------------------------------------------------------------------------------------------------

void deviceMaintenanceAddGroup(void);
void deviceMaintenanceRegisterCallbacks(attsReadCback_t readCallback, attsWriteCback_t writeCallback);

#endif  // #ifndef __MAINTENANCE_SERVICE_HEADER_H__
