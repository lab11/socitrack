// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "wsf_types.h"
#include "att_api.h"
#include "device_info_service.h"
#include "util/bstream.h"


// Device Information Services and Characteristics ---------------------------------------------------------------------

static const uint8_t deviceInfoService[] = { UINT16_TO_BYTES(ATT_UUID_DEVICE_INFO_SERVICE) };
static const uint16_t deviceInfoServiceLen = sizeof(deviceInfoService);
static const uint8_t deviceInfoManufacturerChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(DEVICE_INFO_MANUFACTURER_HANDLE), UINT16_TO_BYTES(ATT_UUID_MANUFACTURER_NAME) };
static const uint16_t deviceInfoManufacturerCharLen = sizeof(deviceInfoManufacturerChar);
static const uint8_t deviceInfoManufacturer[] = MANUFACTURER;
static const uint16_t deviceInfoManufacturerLen = sizeof(deviceInfoManufacturer);
static const uint8_t deviceInfoSysIdChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(DEVICE_INFO_SYSID_HANDLE), UINT16_TO_BYTES(ATT_UUID_SYSTEM_ID) };
static const uint16_t deviceInfoSysIdCharLen = sizeof(deviceInfoSysIdChar);
static uint8_t deviceInfoSysId[8] = { 0 };
static const uint16_t deviceInfoSysIdLen = sizeof(deviceInfoSysId);
static const uint8_t deviceInfoModelNumChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(DEVICE_INFO_MODEL_NUM_HANDLE), UINT16_TO_BYTES(ATT_UUID_MODEL_NUMBER) };
static const uint16_t deviceInfoModelNumCharLen = sizeof(deviceInfoModelNumChar);
static const uint8_t deviceInfoModelNum[] = HW_MODEL;
static const uint16_t deviceInfoModelNumLen = sizeof(deviceInfoModelNum);
static const uint8_t deviceInfoFirmwareVerChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(DEVICE_INFO_FIRMWARE_VER_HANDLE), UINT16_TO_BYTES(ATT_UUID_FIRMWARE_REV) };
static const uint16_t deviceInfoFirmwareVerCharLen = sizeof(deviceInfoFirmwareVerChar);
static const uint8_t deviceInfoFirmwareVer[] = FW_REVISION;
static const uint16_t deviceInfoFirmwareVerLen = sizeof(deviceInfoFirmwareVer);
static const uint8_t deviceInfoHardwareVerChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(DEVICE_INFO_HARDWARE_VER_HANDLE), UINT16_TO_BYTES(ATT_UUID_HARDWARE_REV) };
static const uint16_t deviceInfoHardwareVerCharLen = sizeof(deviceInfoHardwareVerChar);
static const uint8_t deviceInfoHardwareVer[] = HW_REVISION;
static const uint16_t deviceInfoHardwareVerLen = sizeof(deviceInfoHardwareVer);

static const attsAttr_t deviceInfoList[] =
{
   {
      attPrimSvcUuid,
      (uint8_t*)deviceInfoService,
      (uint16_t*)&deviceInfoServiceLen,
      sizeof(deviceInfoService),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)deviceInfoManufacturerChar,
      (uint16_t*)&deviceInfoManufacturerCharLen,
      sizeof(deviceInfoManufacturerChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attMfnsChUuid,
      (uint8_t*)deviceInfoManufacturer,
      (uint16_t*)&deviceInfoManufacturerLen,
      sizeof(deviceInfoManufacturer),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)deviceInfoSysIdChar,
      (uint16_t*)&deviceInfoSysIdCharLen,
      sizeof(deviceInfoSysIdChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attSidChUuid,
      (uint8_t*)deviceInfoSysId,
      (uint16_t*)&deviceInfoSysIdLen,
      sizeof(deviceInfoSysId),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)deviceInfoModelNumChar,
      (uint16_t*)&deviceInfoModelNumCharLen,
      sizeof(deviceInfoModelNumChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attMnsChUuid,
      (uint8_t*)deviceInfoModelNum,
      (uint16_t*)&deviceInfoModelNumLen,
      sizeof(deviceInfoModelNum),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)deviceInfoFirmwareVerChar,
      (uint16_t*)&deviceInfoFirmwareVerCharLen,
      sizeof(deviceInfoFirmwareVer),
      0,
      ATTS_PERMIT_READ
   },
   {
      attFrsChUuid,
      (uint8_t*)deviceInfoFirmwareVer,
      (uint16_t*)&deviceInfoFirmwareVerLen,
      sizeof(deviceInfoFirmwareVer),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)deviceInfoHardwareVerChar,
      (uint16_t*)&deviceInfoHardwareVerCharLen,
      sizeof(deviceInfoHardwareVerChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      attHrsChUuid,
      (uint8_t*)deviceInfoHardwareVer,
      (uint16_t*)&deviceInfoHardwareVerLen,
      sizeof(deviceInfoHardwareVer),
      0,
      ATTS_PERMIT_READ
   }
};

static attsGroup_t deviceInfoGroup = { 0, (attsAttr_t*)deviceInfoList, 0, 0, DEVICE_INFO_SERVICE_HANDLE, DEVICE_INFO_MAX_HANDLE-1 };


// Public API ----------------------------------------------------------------------------------------------------------

void deviceInfoAddGroup(void)
{
   AttsAddGroup(&deviceInfoGroup);
}
