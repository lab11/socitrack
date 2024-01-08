// Header Inclusions ---------------------------------------------------------------------------------------------------

#include "app_tasks.h"
#include "app_api.h"
#include "att_handler.h"
#include "bluetooth.h"
#include "dm_handler.h"
#include "hci_drv_cooper.h"
#include "hci_handler.h"
#include "l2c_api.h"
#include "l2c_handler.h"
#include "logging.h"
#include "wsf_buf.h"


// Static Global Variables ---------------------------------------------------------------------------------------------

#define WSF_BUF_POOLS 5
static uint32_t g_pui32BufMem[(WSF_BUF_POOLS*16 + 16*8 + 32*4 + 64*6 + 280*14 + 424*8) / sizeof(uint32_t)];
static wsfBufPoolDesc_t g_psPoolDescriptors[WSF_BUF_POOLS];


// Private Helper Functions --------------------------------------------------------------------------------------------

static void ble_stack_init(void)
{
   // Set up timers for the WSF scheduler
   WsfOsInit();
   WsfTimerInit();

   // Initialize a buffer pool for WSF dynamic memory needs
   const uint16_t wsfBufMemLen = WsfBufInit(sizeof(g_pui32BufMem), (uint8_t*)g_pui32BufMem, WSF_BUF_POOLS, g_psPoolDescriptors);
   if (wsfBufMemLen > sizeof(g_pui32BufMem))
      print("ERROR: Memory pool is too small by %d\n", wsfBufMemLen - sizeof(g_pui32BufMem));

   // Initialize the WSF security service
   SecInit();
   SecAesInit();
   SecCmacInit();
   SecEccInit();

   // Set up callback functions for the various layers of the BLE stack
   HciHandlerInit(WsfOsSetNextHandler(HciHandler));
   DmDevVsInit(0);
   DmAdvInit();
   DmScanInit();
   DmPhyInit();
   DmConnInit();
   DmConnSlaveInit();
   DmHandlerInit(WsfOsSetNextHandler(DmHandler));
   L2cSlaveHandlerInit(WsfOsSetNextHandler(L2cSlaveHandler));
   L2cSlaveInit();
   L2cInit();

   AttHandlerInit(WsfOsSetNextHandler(AttHandler));
   AttsInit();
   AttsIndInit();
   HciSetMaxRxAclLen(BLE_DESIRED_MTU + 4);

   AppHandlerInit(WsfOsSetNextHandler(AppHandler));
   HciDrvHandlerInit(WsfOsSetNextHandler(HciDrvHandler));
}


// Public API Functions ------------------------------------------------------------------------------------------------

void BLETask(void *params)
{
   // Initialize static variables
   g_psPoolDescriptors[0] = (wsfBufPoolDesc_t){16, 8};
   g_psPoolDescriptors[1] = (wsfBufPoolDesc_t){32, 4};
   g_psPoolDescriptors[2] = (wsfBufPoolDesc_t){64, 6};
   g_psPoolDescriptors[3] = (wsfBufPoolDesc_t){280, 14};
   g_psPoolDescriptors[4] = (wsfBufPoolDesc_t){424, 8};

   // Initialize the BLE stack and start the BLE profile
   ble_stack_init();
   bluetooth_start();

   // Loop forever handling BLE events
   while (true)
      wsfOsDispatcher();
}
