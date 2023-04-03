#ifndef __SCHEDULING_SERVICE_HEADER_H__
#define __SCHEDULING_SERVICE_HEADER_H__

// Scheduling Handle Enumerations --------------------------------------------------------------------------------------

enum
{
   SCHEDULING_SERVICE_HANDLE = 0x0060,      // Scheduling service
   REQUEST_CHAR_HANDLE,                     // Network request characteristic
   REQUEST_HANDLE,                          // Network request
   REQUEST_DESC_HANDLE,                     // Network request description
   SCHEDULING_MAX_HANDLE                    // Maximum live statistics handle
};


// Public API ----------------------------------------------------------------------------------------------------------

void schedulingAddGroup(void);
void schedulingRegisterCallbacks(attsReadCback_t readCallback, attsWriteCback_t writeCallback);

#endif  // #ifndef __SCHEDULING_SERVICE_HEADER_H__
