#ifndef __LIVE_STATS_FUNCTIONALITY_HEADER_H__
#define __LIVE_STATS_FUNCTIONALITY_HEADER_H__

// Public API ----------------------------------------------------------------------------------------------------------

uint8_t handleLiveStatsRead(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, attsAttr_t *pAttr);
uint8_t handleLiveStatsWrite(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr);
void updateRangeResults(dmConnId_t connId, const uint8_t *results, uint16_t results_length);
void updateImuData(dmConnId_t connId, const uint8_t *results, uint16_t results_length);

#endif  // #ifndef __LIVE_STATS_FUNCTIONALITY_HEADER_H__
