// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_config.h"
#include "wsf_types.h"
#include "att_api.h"
#include "live_stats_service.h"
#include "util/bstream.h"


// Live Statistics Services and Characteristics ------------------------------------------------------------------------

static const uint8_t liveStatsService[] = { BLE_LIVE_STATS_SERVICE_ID };
static const uint16_t liveStatsServiceLen = sizeof(liveStatsService);
static const uint8_t battChUuid[] = { BLE_LIVE_STATS_BATTERY_CHAR };
static const uint8_t batteryLevelChar[] = { ATT_PROP_READ, UINT16_TO_BYTES(BATTERY_LEVEL_HANDLE), BLE_LIVE_STATS_BATTERY_CHAR };
static const uint16_t batteryLevelCharLen = sizeof(batteryLevelChar);
static uint16_t batteryLevel = 0;
static const uint16_t batteryLevelLen = sizeof(batteryLevel);
static const uint8_t batteryLevelDesc[] = "BatteryVoltage";
static const uint16_t batteryLevelDescLen = sizeof(batteryLevelDesc);
static const uint8_t timestampChUuid[] = { BLE_LIVE_STATS_TIMESTAMP_CHAR };
static const uint8_t timestampChar[] = { ATT_PROP_READ | ATT_PROP_WRITE, UINT16_TO_BYTES(TIMESTAMP_HANDLE), BLE_LIVE_STATS_TIMESTAMP_CHAR };
static const uint16_t timestampCharLen = sizeof(timestampChar);
static uint32_t timestamp = 0;
static const uint16_t timestampLen = sizeof(timestamp);
static const uint8_t timestampDesc[] = "CurrentTimestamp";
static const uint16_t timestampDescLen = sizeof(timestampDesc);
static const uint8_t findMyTottagChUuid[] = { BLE_LIVE_STATS_FINDMYTOTTAG_CHAR };
static const uint8_t findMyTottagChar[] = { ATT_PROP_WRITE, UINT16_TO_BYTES(FIND_MY_TOTTAG_HANDLE), BLE_LIVE_STATS_FINDMYTOTTAG_CHAR };
static const uint16_t findMyTottagCharLen = sizeof(findMyTottagChar);
static uint32_t findMyTottagDuration = 0;
static const uint16_t findMyTottagDurationLen = sizeof(findMyTottagDuration);
static const uint8_t findMyTottagDesc[] = "FindMyTottagRequest";
static const uint16_t findMyTottagDescLen = sizeof(findMyTottagDesc);
static const uint8_t rangingChUuid[] = { BLE_LIVE_STATS_RANGING_CHAR };
static const uint8_t rangesChar[] = { ATT_PROP_NOTIFY, UINT16_TO_BYTES(RANGES_HANDLE), BLE_LIVE_STATS_RANGING_CHAR };
static const uint16_t rangesCharLen = sizeof(rangesChar);
static uint8_t ranges[20] = { 0 };
static const uint16_t rangesLen = sizeof(ranges);
static const uint8_t rangesDesc[] = "LiveRangingResults";
static const uint16_t rangesDescLen = sizeof(rangesDesc);
static uint8_t rangesCcc[] = { UINT16_TO_BYTES(0x0000) };
static const uint16_t rangesCccLen = sizeof(rangesCcc);

static const attsAttr_t liveStatsList[] =
{
   {
      attPrimSvcUuid,
      (uint8_t*)liveStatsService,
      (uint16_t*)&liveStatsServiceLen,
      sizeof(liveStatsService),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)batteryLevelChar,
      (uint16_t*)&batteryLevelCharLen,
      sizeof(batteryLevelChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      battChUuid,
      (uint8_t*)&batteryLevel,
      (uint16_t*)&batteryLevelLen,
      sizeof(batteryLevel),
      (ATTS_SET_UUID_128 | ATTS_SET_READ_CBACK),
      ATTS_PERMIT_READ
   },
   {
      attChUserDescUuid,
      (uint8_t*)batteryLevelDesc,
      (uint16_t*)&batteryLevelDescLen,
      sizeof(batteryLevelDesc),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)timestampChar,
      (uint16_t*)&timestampCharLen,
      sizeof(timestampChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      timestampChUuid,
      (uint8_t*)&timestamp,
      (uint16_t*)&timestampLen,
      sizeof(timestamp),
      (ATTS_SET_UUID_128 | ATTS_SET_READ_CBACK | ATTS_SET_WRITE_CBACK),
      ATTS_PERMIT_READ | ATTS_PERMIT_WRITE
   },
   {
      attChUserDescUuid,
      (uint8_t*)timestampDesc,
      (uint16_t*)&timestampDescLen,
      sizeof(timestampDesc),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)findMyTottagChar,
      (uint16_t*)&findMyTottagCharLen,
      sizeof(findMyTottagChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      findMyTottagChUuid,
      (uint8_t*)&findMyTottagDuration,
      (uint16_t*)&findMyTottagDurationLen,
      sizeof(findMyTottagDuration),
      (ATTS_SET_UUID_128 | ATTS_SET_WRITE_CBACK),
      ATTS_PERMIT_WRITE
   },
   {
      attChUserDescUuid,
      (uint8_t*)findMyTottagDesc,
      (uint16_t*)&findMyTottagDescLen,
      sizeof(findMyTottagDesc),
      0,
      ATTS_PERMIT_READ
   },
   {
      attChUuid,
      (uint8_t*)rangesChar,
      (uint16_t*)&rangesCharLen,
      sizeof(rangesChar),
      0,
      ATTS_PERMIT_READ
   },
   {
      rangingChUuid,
      (uint8_t*)ranges,
      (uint16_t*)&rangesLen,
      sizeof(ranges),
      (ATTS_SET_UUID_128 | ATTS_SET_VARIABLE_LEN),
      0
   },
   {
      attChUserDescUuid,
      (uint8_t*)rangesDesc,
      (uint16_t*)&rangesDescLen,
      sizeof(rangesDesc),
      0,
      ATTS_PERMIT_READ
   },
   {
      attCliChCfgUuid,
      (uint8_t*)rangesCcc,
      (uint16_t*)&rangesCccLen,
      sizeof(rangesCcc),
      ATTS_SET_CCC,
      (ATTS_PERMIT_READ | ATTS_PERMIT_WRITE)
   }
};

static attsGroup_t liveStatsGroup = { 0, (attsAttr_t*)liveStatsList, NULL, NULL, LIVE_STATS_SERVICE_HANDLE, LIVE_STATS_MAX_HANDLE-1 };


// Public API ----------------------------------------------------------------------------------------------------------

void liveStatsAddGroup(void)
{
   AttsAddGroup(&liveStatsGroup);
}

void liveStatsRegisterCallbacks(attsReadCback_t readCallback, attsWriteCback_t writeCallback)
{
   liveStatsGroup.readCback = readCallback;
   liveStatsGroup.writeCback = writeCallback;
}
