// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "logging.h"


// Public API Functions ------------------------------------------------------------------------------------------------

void logging_init(void)
{
#if defined(ENABLE_LOGGING) && ((7-ENABLE_LOGGING-7 == 14) || (7-ENABLE_LOGGING-7 != 0))

#if (REVISION_ID == REVISION_I) || (REVISION_ID == REVISION_APOLLO4_EVB)
   SEGGER_RTT_ConfigUpBuffer(0, NULL, NULL, 0, SEGGER_RTT_MODE_NO_BLOCK_SKIP);
#else
   am_bsp_itm_printf_enable();
#endif  // #if REVISION_ID == REVISION_I

#elif (REVISION_ID != REVISION_I) && (REVISION_ID != REVISION_APOLLO4_EVB)

   am_bsp_itm_printf_disable();

#endif
}

void logging_disable(void)
{
#if defined(ENABLE_LOGGING) && ((7-ENABLE_LOGGING-7 == 14) || (7-ENABLE_LOGGING-7 != 0))
#if (REVISION_ID != REVISION_I) && (REVISION_ID != REVISION_APOLLO4_EVB)
   am_bsp_itm_printf_disable();
#endif  // #if REVISION_ID == REVISION_I
#endif
}

#if defined(ENABLE_LOGGING) && ((7-ENABLE_LOGGING-7 == 14) || (7-ENABLE_LOGGING-7 != 0))

void print_reset_reason(const am_hal_reset_status_t* reason)
{
   print("\n----------------------------------------\n");
   print("Reset Reasons: ");
   if (reason->bEXTStat)
      print("External Reset, ");
   if (reason->bPORStat)
      print("HW Power-On Reset, ");
   if (reason->bBODStat)
      print("Brown-Out Reset, ");
   if (reason->bSWPORStat)
      print("SW Power-On Reset, ");
   if (reason->bSWPOIStat)
      print("SW Power-On Initialization, ");
   if (reason->bDBGRStat)
      print("Debugger Reset, ");
   if (reason->bWDTStat)
      print("Watch Dog Timer Reset, ");
   if (reason->bBOUnregStat)
      print("Unregulated Supply Brownout, ");
   if (reason->bBOCOREStat)
      print("Core Regulator Brownout, ");
   if (reason->bBOMEMStat)
      print("Memory Regulator Brownout, ");
   if (reason->bBOHPMEMStat)
      print("High-Power Memory Regulator Brownout, ");
   if (reason->bBOLPCOREStat)
      print("Low-Power Core Regulator Brownout, ");
   print("\n");
}

void print_ranges(uint32_t timestamp, const uint8_t* range_data, uint32_t range_data_length)
{
   print("%u ranges @ Timestamp %u:\n", range_data[0], timestamp);
   for (uint8_t i = 0; i < range_data[0]; ++i)
      print("   Range to 0x%02X: %d\n", range_data[1 + (i*COMPRESSED_RANGE_DATUM_LENGTH)], (int32_t)(*((int16_t*)(range_data + 2 + (i*COMPRESSED_RANGE_DATUM_LENGTH)))));
}

#endif
