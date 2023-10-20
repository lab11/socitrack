// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "wsf_types.h"
#include "att_api.h"
#include "gap_gatt_service.h"
#include "util/bstream.h"


// GAP Services and Characteristics ------------------------------------------------------------------------------------

static const uint8_t gapServiceVal[] = { UINT16_TO_BYTES(ATT_UUID_GAP_SERVICE) };
static const uint16_t gapServiceLen = sizeof(gapServiceVal);
static const uint8_t gapDeviceNameChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(GAP_DEVICE_NAME_HANDLE), UINT16_TO_BYTES(ATT_UUID_DEVICE_NAME) };
static const uint16_t gapDeviceNameCharLen = sizeof(gapDeviceNameChar);
static const uint8_t gapDeviceName[ATT_DEFAULT_PAYLOAD_LEN] = "TotTag";
static const uint16_t gapDeviceNameLen = 6;
static const uint8_t gapAppearanceChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(GAP_APPEARANCE_HANDLE), UINT16_TO_BYTES(ATT_UUID_APPEARANCE) };
static const uint16_t gapAppearanceCharLen = sizeof(gapAppearanceChar);
static const uint8_t gapAppearance[] = { UINT16_TO_BYTES(CH_APPEAR_SENSOR_PROXIMITY) };
static const uint16_t gapAppearanceLen = sizeof(gapAppearance);
static const uint8_t gapCentralAddrResChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(GAP_CENTRAL_ADDR_RES_HANDLE), UINT16_TO_BYTES(ATT_UUID_CAR) };
static const uint16_t gapCentralAddrResCharLen = sizeof(gapCentralAddrResChar);
static uint8_t gapCentralAddrRes[] = { 0 };
static const uint16_t gapCentralAddrResLen = sizeof(gapCentralAddrRes);

static const attsAttr_t gapList[] =
{
   {
      attPrimSvcUuid,
      (uint8_t*)gapServiceVal,
      (uint16_t*)&gapServiceLen,
      sizeof(gapServiceVal),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)gapDeviceNameChar,
      (uint16_t*)&gapDeviceNameCharLen,
      sizeof(gapDeviceNameChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attDnChUuid,
      (uint8_t*)gapDeviceName,
      (uint16_t*)&gapDeviceNameLen,
      sizeof(gapDeviceName),
      (ATTS_SET_VARIABLE_LEN | ATTS_SET_WRITE_CBACK),
      (ATTS_PERMIT_READ | ATTS_PERMIT_WRITE)
   },
   {
      attChUuid,
      (uint8_t*)gapAppearanceChar,
      (uint16_t*)&gapAppearanceCharLen,
      sizeof(gapAppearanceChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attApChUuid,
      (uint8_t*)gapAppearance,
      (uint16_t*)&gapAppearanceLen,
      sizeof(gapAppearance),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)gapCentralAddrResChar,
      (uint16_t *)&gapCentralAddrResCharLen,
      sizeof(gapCentralAddrResChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attCarChUuid,
      (uint8_t*)gapCentralAddrRes,
      (uint16_t*)&gapCentralAddrResLen,
      sizeof(gapCentralAddrRes),
      0,
      ATTS_PERMIT_READ
   },
};

static attsGroup_t gapGroup;


// GATT Services and Characteristics -----------------------------------------------------------------------------------

static const uint8_t gattServiceVal[] = { UINT16_TO_BYTES(ATT_UUID_GATT_SERVICE) };
static const uint16_t gattServiceLen = sizeof(gattServiceVal);
static const uint8_t gattServiceChangedChar[] = { ATT_PROP_INDICATE, UINT16_TO_BYTES(GATT_SERVICE_CHANGED_HANDLE), UINT16_TO_BYTES(ATT_UUID_SERVICE_CHANGED) };
static const uint16_t gattServiceChangedCharLen = sizeof(gattServiceChangedChar);
static const uint8_t gattServiceChanged[] = { UINT16_TO_BYTES(0x0001), UINT16_TO_BYTES(0xFFFF) };
static const uint16_t gattServiceChangedLen = sizeof(gattServiceChanged);
static uint8_t gattServiceChangedCcc[] = { UINT16_TO_BYTES(0x0000) };
static const uint16_t gattServiceChangedCccLen = sizeof(gattServiceChangedCcc);
static const uint8_t gattCsfChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(GATT_CSF_HANDLE), UINT16_TO_BYTES(ATT_UUID_CLIENT_SUPPORTED_FEATURES) };
static const uint16_t gattCsfCharLen = sizeof(gattCsfChar);
static uint8_t gattCsf[ATT_CSF_LEN] = { 0 };
static const uint16_t gattCsfLen = sizeof(gattCsf);
static const uint8_t gattDbHashChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(GATT_DB_HASH_HANDLE), UINT16_TO_BYTES(ATT_UUID_DATABASE_HASH) };
static const uint16_t gattDbHashCharLen = sizeof(gattDbHashChar);
static uint8_t gattDbHash[ATT_DATABASE_HASH_LEN] = { 0 };
static const uint16_t gattDbHashLen = sizeof(gattDbHash);

static const attsAttr_t gattList[] =
{
   {
      attPrimSvcUuid,
      (uint8_t*)gattServiceVal,
      (uint16_t*)&gattServiceLen,
      sizeof(gattServiceVal),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)gattServiceChangedChar,
      (uint16_t*)&gattServiceChangedCharLen,
      sizeof(gattServiceChangedChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attScChUuid,
      (uint8_t*)gattServiceChanged,
      (uint16_t*)&gattServiceChangedLen,
      sizeof(gattServiceChanged),
      0,
      0
   },
   {
      attCliChCfgUuid,
      (uint8_t*)gattServiceChangedCcc,
      (uint16_t*)&gattServiceChangedCccLen,
      sizeof(gattServiceChangedCcc),
      ATTS_SET_CCC,
      (ATTS_PERMIT_READ | ATTS_PERMIT_WRITE)
   },
   {
      attChUuid,
      (uint8_t*)gattCsfChar,
      (uint16_t*)&gattCsfCharLen,
      sizeof(gattCsfChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attGattCsfChUuid,
      (uint8_t*)gattCsf,
      (uint16_t*)&gattCsfLen,
      sizeof(gattCsf),
      (ATTS_SET_READ_CBACK | ATTS_SET_WRITE_CBACK),
      (ATTS_PERMIT_READ | ATTS_PERMIT_WRITE)
   },
   {
      attChUuid,
      (uint8_t*)gattDbHashChar,
      (uint16_t*)&gattDbHashCharLen,
      sizeof(gattDbHashChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attGattDbhChUuid,
      (uint8_t*)gattDbHash,
      (uint16_t*)&gattDbHashLen,
      sizeof(gattDbHash),
      ATTS_SET_READ_CBACK,
      ATTS_PERMIT_READ
   }
};

static attsGroup_t gattGroup;

void SvcCoreGapCentAddrResUpdate(bool_t value) { gapCentralAddrRes[0] = value; }


// Public API ----------------------------------------------------------------------------------------------------------

void gapGattAddGroup(void)
{
   AttsAddGroup(&gapGroup);
   AttsAddGroup(&gattGroup);
}

void gapGattRegisterCallbacks(attsReadCback_t readCallback, attsWriteCback_t writeCallback)
{
   memset(gapCentralAddrRes, 0, sizeof(gapCentralAddrRes));
   memset(gattServiceChangedCcc, 0, sizeof(gattServiceChangedCcc));
   memset(gattCsf, 0, sizeof(gattCsf));
   memset(gattDbHash, 0, sizeof(gattDbHash));
   gapGroup = (attsGroup_t){ 0, (attsAttr_t*)gapList, 0, 0, GAP_SERVICE_HANDLE, GAP_MAX_HANDLE-1 };
   gattGroup = (attsGroup_t){ 0, (attsAttr_t*)gattList, readCallback, writeCallback, GATT_SERVICE_HANDLE, GATT_MAX_HANDLE-1 };
}
