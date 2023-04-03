#ifndef __SCHEDULING_FUNCTIONALITY_HEADER_H__
#define __SCHEDULING_FUNCTIONALITY_HEADER_H__

// Public API ----------------------------------------------------------------------------------------------------------

uint8_t handleSchedulingRead(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, attsAttr_t *pAttr);
uint8_t handleSchedulingWrite(dmConnId_t connId, uint16_t handle, uint8_t operation, uint16_t offset, uint16_t len, uint8_t *pValue, attsAttr_t *pAttr);

#endif  // #ifndef __SCHEDULING_FUNCTIONALITY_HEADER_H__
