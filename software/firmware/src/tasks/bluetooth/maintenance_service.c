// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "wsf_types.h"
#include "att_api.h"
#include "maintenance_service.h"
#include "util/bstream.h"


// TotTag Maintenance Services and Characteristics ---------------------------------------------------------------------

static const uint8_t maintenanceService[] = { BLE_MAINTENANCE_SERVICE_ID };
static const uint16_t maintenanceServiceLen = sizeof(maintenanceService);
static const uint8_t experimentDetailsChUuid[] = { BLE_MAINTENANCE_EXPERIMENT_CHAR };
static const uint8_t experimentDetailsChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(MAINTENANCE_EXPERIMENT_HANDLE), BLE_MAINTENANCE_EXPERIMENT_CHAR };
static const uint16_t experimentDetailsCharLen = sizeof(experimentDetailsChar);
static experiment_details_t experimentDetails = { 0 };
static const uint16_t experimentDetailsLen = sizeof(experimentDetails);
static const uint8_t experimentDetailsDesc[] = "ExperimentDetails";
static const uint16_t experimentDetailsDescLen = sizeof(experimentDetailsDesc);
static const uint8_t maintenanceCommandChUuid[] = { BLE_MAINTENANCE_COMMAND_CHAR };
static const uint8_t maintenanceCommandChar[] = { ATT_PROP_WRITE, UINT16_TO_BYTES(MAINTENANCE_COMMAND_HANDLE), BLE_MAINTENANCE_COMMAND_CHAR };
static const uint16_t maintenanceCommandCharLen = sizeof(maintenanceCommandChar);
static uint8_t maintenanceCommand[1+sizeof(experiment_details_t)] = { 0 };
static const uint16_t maintenanceCommandLen = sizeof(maintenanceCommand);
static const uint8_t maintenanceCommandDesc[] = "MaintenanceCommand";
static const uint16_t maintenanceCommandDescLen = sizeof(maintenanceCommandDesc);
static const uint8_t maintenanceResultChUuid[] = { BLE_MAINTENANCE_DATA_CHAR };
static const uint8_t maintenanceResultChar[] = { ATT_PROP_INDICATE, UINT16_TO_BYTES(MAINTENANCE_RESULT_HANDLE), BLE_MAINTENANCE_DATA_CHAR };
static const uint16_t maintenanceResultCharLen = sizeof(maintenanceResultChar);
static uint8_t maintenanceResult[] = { 0 };
static const uint16_t maintenanceResultLen = sizeof(maintenanceResult);
static const uint8_t maintenanceResultDesc[] = "MaintenanceData";
static const uint16_t maintenanceResultDescLen = sizeof(maintenanceResultDesc);
static uint8_t maintenanceResultCcc[] = { UINT16_TO_BYTES(0x0000) };
static const uint16_t maintenanceResultCccLen = sizeof(maintenanceResultCcc);

static const attsAttr_t maintenanceList[] =
{
   {
      attPrimSvcUuid,
      (uint8_t*)maintenanceService,
      (uint16_t*)&maintenanceServiceLen,
      sizeof(maintenanceService),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)experimentDetailsChar,
      (uint16_t*)&experimentDetailsCharLen,
      sizeof(experimentDetailsChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      experimentDetailsChUuid,
      (uint8_t*)&experimentDetails,
      (uint16_t*)&experimentDetailsLen,
      sizeof(experimentDetails),
      (ATTS_SET_UUID_128 | ATTS_SET_READ_CBACK),
      ATTS_PERMIT_READ
   },
   {
      attChUserDescUuid,
      (uint8_t*)experimentDetailsDesc,
      (uint16_t*)&experimentDetailsDescLen,
      sizeof(experimentDetailsDesc),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)maintenanceCommandChar,
      (uint16_t*)&maintenanceCommandCharLen,
      sizeof(maintenanceCommandChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      maintenanceCommandChUuid,
      (uint8_t*)maintenanceCommand,
      (uint16_t*)&maintenanceCommandLen,
      sizeof(maintenanceCommand),
      (ATTS_SET_UUID_128 | ATTS_SET_WRITE_CBACK | ATTS_SET_VARIABLE_LEN),
      ATTS_PERMIT_WRITE
   },
   {
      attChUserDescUuid,
      (uint8_t*)maintenanceCommandDesc,
      (uint16_t*)&maintenanceCommandDescLen,
      sizeof(maintenanceCommandDesc),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)maintenanceResultChar,
      (uint16_t*)&maintenanceResultCharLen,
      sizeof(maintenanceResultChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      maintenanceResultChUuid,
      (uint8_t*)maintenanceResult,
      (uint16_t*)&maintenanceResultLen,
      sizeof(maintenanceResult),
      (ATTS_SET_UUID_128 | ATTS_SET_VARIABLE_LEN),
      0
   },
   {
      attChUserDescUuid,
      (uint8_t*)maintenanceResultDesc,
      (uint16_t*)&maintenanceResultDescLen,
      sizeof(maintenanceResultDesc),
      0,
      ATTS_PERMIT_READ
   },
   {
      attCliChCfgUuid,
      (uint8_t*)maintenanceResultCcc,
      (uint16_t*)&maintenanceResultCccLen,
      sizeof(maintenanceResultCcc),
      ATTS_SET_CCC,
      (ATTS_PERMIT_READ | ATTS_PERMIT_WRITE)
   }
};

static attsGroup_t maintenanceGroup;


// Public API ----------------------------------------------------------------------------------------------------------

void deviceMaintenanceAddGroup(void)
{
   AttsAddGroup(&maintenanceGroup);
}

void deviceMaintenanceRegisterCallbacks(attsReadCback_t readCallback, attsWriteCback_t writeCallback)
{
   memset(&experimentDetails, 0, sizeof(experimentDetails));
   memset(maintenanceCommand, 0, sizeof(maintenanceCommand));
   memset(maintenanceResult, 0, sizeof(maintenanceResult));
   memset(maintenanceResultCcc, 0, sizeof(maintenanceResultCcc));
   maintenanceGroup = (attsGroup_t){ 0, (attsAttr_t*)maintenanceList, readCallback, writeCallback, MAINTENANCE_SERVICE_HANDLE, MAINTENANCE_MAX_HANDLE-1 };
}
