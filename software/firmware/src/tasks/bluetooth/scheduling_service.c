// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "wsf_types.h"
#include "att_api.h"
#include "scheduling_service.h"
#include "util/bstream.h"


// Scheduling Services and Characteristics -----------------------------------------------------------------------------

static const uint8_t schedulingService[] = { BLE_SCHEDULING_SERVICE_ID };
static const uint16_t schedulingServiceLen = sizeof(schedulingService);
static const uint8_t networkRequestChUuid[] = { BLE_SCHEDULING_REQUEST_CHAR };
static const uint8_t networkRequestChar[] = { ATT_PROP_WRITE, UINT16_TO_BYTES(REQUEST_HANDLE), BLE_SCHEDULING_REQUEST_CHAR };
static const uint16_t networkRequestCharLen = sizeof(networkRequestChar);
static uint8_t networkRequest[EUI_LEN] = { 0 };
static const uint16_t networkRequestLen = sizeof(networkRequest);
static const uint8_t networkRequestDesc[] = "NetworkRequest";
static const uint16_t networkRequestDescLen = sizeof(networkRequestDesc);

static const attsAttr_t schedulingList[] =
{
   {
      attPrimSvcUuid,
      (uint8_t*)schedulingService,
      (uint16_t*)&schedulingServiceLen,
      sizeof(schedulingService),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)networkRequestChar,
      (uint16_t*)&networkRequestCharLen,
      sizeof(networkRequestChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      networkRequestChUuid,
      (uint8_t*)networkRequest,
      (uint16_t*)&networkRequestLen,
      sizeof(networkRequest),
      (ATTS_SET_UUID_128 | ATTS_SET_WRITE_CBACK),
      ATTS_PERMIT_WRITE
   },
   {
      attChUserDescUuid,
      (uint8_t*)networkRequestDesc,
      (uint16_t*)&networkRequestDescLen,
      sizeof(networkRequestDesc),
      0,
      ATTS_PERMIT_READ
   }
};

static attsGroup_t schedulingGroup;


// Public API ----------------------------------------------------------------------------------------------------------

void schedulingAddGroup(void)
{
   AttsAddGroup(&schedulingGroup);
}

void schedulingRegisterCallbacks(attsReadCback_t readCallback, attsWriteCback_t writeCallback)
{
   memset(networkRequest, 0, sizeof(networkRequest));
   schedulingGroup = (attsGroup_t){ 0, (attsAttr_t*)schedulingList, readCallback, writeCallback, SCHEDULING_SERVICE_HANDLE, SCHEDULING_MAX_HANDLE-1 };
}
