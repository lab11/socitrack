//*****************************************************************************
//
//! @file am_devices_mspi_psram_w958d6nw.c
//!
//! @brief Winbond MSPI PSRAM driver.
//!
//! @addtogroup mspi_psram_w958d6nw W958D6NW MSPI PSRAM Driver
//! @ingroup devices
//! @{
//
//*****************************************************************************

//*****************************************************************************
//
// Copyright (c) 2024, Ambiq Micro, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// Third party software included in this distribution is subject to the
// additional license terms as defined in the /docs/licenses directory.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// This is part of revision release_sdk_4_5_0-a1ef3b89f9 of the AmbiqSuite Development Package.
//
//*****************************************************************************
#include <string.h>
#include "am_mcu_apollo.h"
#include "am_devices_mspi_psram_w958d6nw.h"
#include "am_util_stdio.h"
#include "am_bsp.h"
#include "am_util.h"
#include "am_util_delay.h"

//*****************************************************************************
//
// Global variables.
//
//*****************************************************************************

//#define W958D6NW_USE_NON_DQS_MODE

#define AM_DEVICES_W958D6NW_PSRAM_TIMEOUT              1000000
#define PSRAM_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH   (8)     // there should be at least
                                                          // this amount of consecutive
                                                          // passing settings to be accepted.

#define W958D6NW_tHS_MAX_US 3
#define W958D6NW_tEXTHS_MIN_US 100
#define W958D6NW_tCSHS_US 1

am_hal_mspi_xip_config_t gWBDDRXipConfig[] =
{
  {
    .ui32APBaseAddr       = MSPI0_APERTURE_START_ADDR,
    .eAPMode              = AM_HAL_MSPI_AP_READ_WRITE,
    .eAPSize              = AM_HAL_MSPI_AP_SIZE64M,
    .scramblingStartAddr  = 0,
    .scramblingEndAddr    = 0,
  },
  {
    .ui32APBaseAddr       = MSPI1_APERTURE_START_ADDR,
    .eAPMode              = AM_HAL_MSPI_AP_READ_WRITE,
    .eAPSize              = AM_HAL_MSPI_AP_SIZE64M,
    .scramblingStartAddr  = 0,
    .scramblingEndAddr    = 0,
  },
  {
    .ui32APBaseAddr       = MSPI2_APERTURE_START_ADDR,
    .eAPMode              = AM_HAL_MSPI_AP_READ_WRITE,
    .eAPSize              = AM_HAL_MSPI_AP_SIZE64M,
    .scramblingStartAddr  = 0,
    .scramblingEndAddr    = 0,
  },
#if defined(AM_PART_APOLLO5_API)
  {
    .ui32APBaseAddr       = MSPI3_APERTURE_START_ADDR,
    .eAPMode              = AM_HAL_MSPI_AP_READ_WRITE,
    .eAPSize              = AM_HAL_MSPI_AP_SIZE64M,
    .scramblingStartAddr  = 0,
    .scramblingEndAddr    = 0,
  }
#endif
};

am_hal_mspi_dqs_t gWBDDRDqsCfg[] =
{
  {
#ifdef W958D6NW_USE_NON_DQS_MODE
    .bDQSEnable             = 0,
#else
    .bDQSEnable             = 1,
#endif
    .bDQSSyncNeg            = 0,
    .bEnableFineDelay       = 0,
    .ui8TxDQSDelay          = 0,
    .ui8RxDQSDelay          = 16,
    .ui8RxDQSDelayNeg       = 0,
    .bRxDQSDelayNegEN       = 0,
    .ui8RxDQSDelayHi        = 0,
    .ui8RxDQSDelayNegHi     = 0,
    .bRxDQSDelayHiEN        = 0,
  },
  {
#ifdef W958D6NW_USE_NON_DQS_MODE
    .bDQSEnable             = 0,
#else
    .bDQSEnable             = 1,
#endif
    .bDQSSyncNeg            = 0,
    .bEnableFineDelay       = 0,
    .ui8TxDQSDelay          = 0,
    .ui8RxDQSDelay          = 16,
    .ui8RxDQSDelayNeg       = 0,
    .bRxDQSDelayNegEN       = 0,
    .ui8RxDQSDelayHi        = 0,
    .ui8RxDQSDelayNegHi     = 0,
    .bRxDQSDelayHiEN        = 0,
  },
  {
#ifdef W958D6NW_USE_NON_DQS_MODE
    .bDQSEnable             = 0,
#else
    .bDQSEnable             = 1,
#endif
    .bDQSSyncNeg            = 0,
    .bEnableFineDelay       = 0,
    .ui8TxDQSDelay          = 0,
    .ui8RxDQSDelay          = 16,
    .ui8RxDQSDelayNeg       = 0,
    .bRxDQSDelayNegEN       = 0,
    .ui8RxDQSDelayHi        = 0,
    .ui8RxDQSDelayNegHi     = 0,
    .bRxDQSDelayHiEN        = 0,
  },
#if defined(AM_PART_APOLLO5_API)
  {
#ifdef W958D6NW_USE_NON_DQS_MODE
    .bDQSEnable             = 0,
#else
    .bDQSEnable             = 1,
#endif
    .bDQSSyncNeg            = 0,
    .bEnableFineDelay       = 0,
    .ui8TxDQSDelay          = 0,
    .ui8RxDQSDelay          = 16,
    .ui8RxDQSDelayNeg       = 0,
    .bRxDQSDelayNegEN       = 0,
    .ui8RxDQSDelayHi        = 0,
    .ui8RxDQSDelayNegHi     = 0,
    .bRxDQSDelayHiEN        = 0,
  }
#endif
};

am_hal_mspi_xip_misc_t gWBXipMiscCfg[] =
{
  {
    .ui32CEBreak        = 10,
    .bXIPBoundary       = true,
    .bXIPOdd            = true,
    .bAppndOdd          = false,
    .bBEOn              = false,
    .eBEPolarity        = AM_HAL_MSPI_BE_LOW_ENABLE,
  },
  {
    .ui32CEBreak        = 10,
    .bXIPBoundary       = true,
    .bXIPOdd            = true,
    .bAppndOdd          = false,
    .bBEOn              = false,
    .eBEPolarity        = AM_HAL_MSPI_BE_LOW_ENABLE,
  },
  {
    .ui32CEBreak        = 10,
    .bXIPBoundary       = true,
    .bXIPOdd            = true,
    .bAppndOdd          = false,
    .bBEOn              = false,
    .eBEPolarity        = AM_HAL_MSPI_BE_LOW_ENABLE,
  },
#if defined(AM_PART_APOLLO5_API)
  {
    .ui32CEBreak        = 10,
    .bXIPBoundary       = true,
    .bXIPOdd            = true,
    .bAppndOdd          = false,
    .bBEOn              = false,
    .eBEPolarity        = AM_HAL_MSPI_BE_LOW_ENABLE,
  }
#endif
};

am_hal_mspi_config_t gWBDDRMspiCfg =
{
  .ui32TCBSize          = 0,
  .pTCB                 = NULL,
  .bClkonD4             = 0
};

am_hal_mspi_rxcfg_t gWBMspiRxCfg =
{
    .ui8DQSturn         = 2,
    .bRxHI              = 0,
    .bTaForth           = 1,
    .bHyperIO           = 1,
    .ui8RxSmp           = 1,
    .bRBX               = 0,
    .bWBX               = 0,
    .bSCLKRxHalt        = 0,
    .bRxCapEXT          = 0,
    .ui8Sfturn          = 10,
};

am_hal_mspi_dev_config_t  WBDDROctalCE0MSPIConfig =
{
  .eAddrCfg             = AM_HAL_MSPI_ADDR_4_BYTE,
  .eInstrCfg            = AM_HAL_MSPI_INSTR_2_BYTE,
  .ui16ReadInstr        = AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ,
  .ui16WriteInstr       = AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_WRITE,
  .eDeviceConfig        = AM_HAL_MSPI_FLASH_OCTAL_DDR_CE0,
  .eSpiMode             = AM_HAL_MSPI_SPI_MODE_0,
  .bSendAddr            = true,
  .bSendInstr           = true,
  .bTurnaround          = true,
  .eClockFreq           = AM_HAL_MSPI_CLK_96MHZ,
#ifdef W958D6NW_USE_NON_DQS_MODE
  .ui8TurnAround        = 14,
  .ui8WriteLatency      = 14,
#else
  .ui8TurnAround        = 6,
  .ui8WriteLatency      = 6,
#endif
  .bEnWriteLatency      = true,
  .bEmulateDDR          = true,
#if defined(AM_PART_APOLLO5_API)
#if defined(USE_NEW_DDR)
  .bNewDDR              = true,
#else
  .bNewDDR              = false,
#endif
  .eCeLatency           = AM_HAL_MSPI_CE_LATENCY_NORMAL,
#endif

#if defined(APOLLO4_FPGA) || defined(APOLLO5_FPGA)
  .ui16DMATimeLimit     = 20,
#else
  .ui16DMATimeLimit     = 40,
#endif

  .eDMABoundary         = AM_HAL_MSPI_BOUNDARY_BREAK1K,
};

am_hal_mspi_dev_config_t  WBDDROctalCE1MSPIConfig =
{
  .eAddrCfg             = AM_HAL_MSPI_ADDR_4_BYTE,
  .eInstrCfg            = AM_HAL_MSPI_INSTR_2_BYTE,
  .ui16ReadInstr        = AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ,
  .ui16WriteInstr       = AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_WRITE,
  .eDeviceConfig        = AM_HAL_MSPI_FLASH_OCTAL_DDR_CE1,
  .eSpiMode             = AM_HAL_MSPI_SPI_MODE_0,
  .bSendAddr            = true,
  .bSendInstr           = true,
  .bTurnaround          = true,
  .eClockFreq           = AM_HAL_MSPI_CLK_96MHZ,
#ifdef W958D6NW_USE_NON_DQS_MODE
  .ui8TurnAround        = 14,
  .ui8WriteLatency      = 14,
#else
  .ui8TurnAround        = 6,
  .ui8WriteLatency      = 6,
#endif
  .bEnWriteLatency      = true,
  .bEmulateDDR          = true,
#if defined(AM_PART_APOLLO5_API)
#if defined(USE_NEW_DDR)
  .bNewDDR              = true,
#else
  .bNewDDR              = false,
#endif
  .eCeLatency           = AM_HAL_MSPI_CE_LATENCY_NORMAL,
#endif

#if defined(APOLLO4_FPGA) || defined(APOLLO5_FPGA)
  .ui16DMATimeLimit     = 20,
#else
  .ui16DMATimeLimit     = 40,
#endif

  .eDMABoundary         = AM_HAL_MSPI_BOUNDARY_BREAK1K,
};

am_hal_mspi_dev_config_t  WBDDRHEXCE0MSPIConfig =
{
  .eAddrCfg             = AM_HAL_MSPI_ADDR_4_BYTE,
  .eInstrCfg            = AM_HAL_MSPI_INSTR_2_BYTE,
  .ui16ReadInstr        = AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ,
  .ui16WriteInstr       = AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_WRITE,
  .eDeviceConfig        = AM_HAL_MSPI_FLASH_HEX_DDR_CE0,
  .eSpiMode             = AM_HAL_MSPI_SPI_MODE_0,
  .bSendAddr            = true,
  .bSendInstr           = true,
  .bTurnaround          = true,
  .eClockFreq           = AM_HAL_MSPI_CLK_96MHZ,
#ifdef W958D6NW_USE_NON_DQS_MODE
  .ui8TurnAround        = 14,
  .ui8WriteLatency      = 14,
#else
  .ui8TurnAround        = 6,
  .ui8WriteLatency      = 6,
#endif
  .bEnWriteLatency      = true,
  .bEmulateDDR          = true,
#if defined(AM_PART_APOLLO5_API)
#if defined(USE_NEW_DDR)
  .bNewDDR              = true,
#else
  .bNewDDR              = false,
#endif
  .eCeLatency           = AM_HAL_MSPI_CE_LATENCY_NORMAL,
#endif

#if defined(APOLLO4_FPGA) || defined(APOLLO5_FPGA)
  .ui16DMATimeLimit     = 20,
#else
  .ui16DMATimeLimit     = 40,
#endif

  .eDMABoundary         = AM_HAL_MSPI_BOUNDARY_BREAK1K,
};

am_hal_mspi_dev_config_t  WBDDRHEXCE1MSPIConfig =
{
  .eAddrCfg             = AM_HAL_MSPI_ADDR_4_BYTE,
  .eInstrCfg            = AM_HAL_MSPI_INSTR_2_BYTE,
  .ui16ReadInstr        = AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ,
  .ui16WriteInstr       = AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_WRITE,
  .eDeviceConfig        = AM_HAL_MSPI_FLASH_HEX_DDR_CE1,
  .eSpiMode             = AM_HAL_MSPI_SPI_MODE_0,
  .bSendAddr            = true,
  .bSendInstr           = true,
  .bTurnaround          = true,
  .eClockFreq           = AM_HAL_MSPI_CLK_96MHZ,
#ifdef W958D6NW_USE_NON_DQS_MODE
  .ui8TurnAround        = 14,
  .ui8WriteLatency      = 14,
#else
  .ui8TurnAround        = 6,
  .ui8WriteLatency      = 6,
#endif
  .bEnWriteLatency      = true,
  .bEmulateDDR          = true,
#if defined(AM_PART_APOLLO5_API)
#if defined(USE_NEW_DDR)
  .bNewDDR              = true,
#else
  .bNewDDR              = false,
#endif
  .eCeLatency           = AM_HAL_MSPI_CE_LATENCY_NORMAL,
#endif

#if defined(APOLLO4_FPGA) || defined(APOLLO5_FPGA)
  .ui16DMATimeLimit     = 20,
#else
  .ui16DMATimeLimit     = 40,
#endif

  .eDMABoundary         = AM_HAL_MSPI_BOUNDARY_BREAK1K,
};

typedef struct
{
  uint32_t                      ui32Module;
  void                          *pMspiHandle;
  am_hal_mspi_device_e          eDeviceConfig;
  bool                          bOccupied;
  am_devices_mspi_psram_info_t  sDeviceInfo;
} am_devices_mspi_psram_t;

am_devices_mspi_psram_t gWBDDRPsram[AM_DEVICES_MSPI_PSRAM_MAX_DEVICE_NUM];

//*****************************************************************************
//
//! @brief
//! @param pCallbackCtxt
//! @param status
//
//*****************************************************************************

void pfnMSPI_WBPSRAM_DDR_Callback(void *pCallbackCtxt, uint32_t status)
{
#if defined(AM_PART_APOLLO5_API)
  //
  // Flush and invalidate whole cache
  // Recommend user to manage cache coherency based on application usage
  //
  am_hal_cachectrl_dcache_invalidate(NULL, true);
#endif
  // Set the DMA complete flag.
  *(volatile uint32_t *)pCallbackCtxt = status;
}

//*****************************************************************************
//
//! @brief Generic Command Write function.
//! @param pMspiHandle
//! @param ui16Instr
//! @param bSendAddr
//! @param ui32Addr
//! @param pData
//! @param ui32NumBytes
//! @return
//
//*****************************************************************************
static uint32_t
am_device_command_write(void *pMspiHandle,
                        uint16_t ui16Instr,
                        bool bSendAddr,
                        uint32_t ui32Addr,
                        uint32_t *pData,
                        uint32_t ui32NumBytes)
{
  am_hal_mspi_pio_transfer_t  Transaction;

  // Create the individual write transaction.
  Transaction.ui32NumBytes            = ui32NumBytes;
  Transaction.bScrambling             = false;
  Transaction.eDirection              = AM_HAL_MSPI_TX;
  Transaction.bSendAddr               = bSendAddr;
  Transaction.ui32DeviceAddr          = ui32Addr;
  Transaction.bSendInstr              = true;
  Transaction.ui16DeviceInstr         = ui16Instr;
  Transaction.bTurnaround             = false;
  Transaction.bDCX                    = false;
  Transaction.bEnWRLatency            = false;
  Transaction.bContinue               = false;  // MSPI CONT is deprecated for Apollo4
  Transaction.pui32Buffer             = pData;

#if defined (USE_NEW_DDR) && !defined(AM_PART_APOLLO5A)
  if ( ui32NumBytes < 4 )
  {
    *pData = *pData << (4 - ui32NumBytes) * 8;
  }
#endif

  // Execute the transction over MSPI.
  return am_hal_mspi_blocking_transfer(pMspiHandle,
                                       &Transaction,
                                       AM_DEVICES_W958D6NW_PSRAM_TIMEOUT );
}

//*****************************************************************************
//
//! @brief Generic Command Read function.
//! @param pMspiHandle
//! @param ui16Instr
//! @param bSendAddr
//! @param ui32Addr
//! @param pData
//! @param ui32NumBytes
//! @return
//
//*****************************************************************************
static uint32_t
am_device_command_read(void *pMspiHandle,
                       uint16_t ui16Instr,
                       bool bSendAddr,
                       uint32_t ui32Addr,
                       uint32_t *pData,
                       uint32_t ui32NumBytes)
{
  am_hal_mspi_pio_transfer_t  Transaction;

  // Create the individual write transaction.
  Transaction.ui32NumBytes            = ui32NumBytes;
  Transaction.bScrambling             = false;
  Transaction.eDirection              = AM_HAL_MSPI_RX;
  Transaction.bSendAddr               = bSendAddr;
  Transaction.ui32DeviceAddr          = ui32Addr;
  Transaction.bSendInstr              = true;
  Transaction.ui16DeviceInstr         = ui16Instr;
  Transaction.bTurnaround             = true;
  Transaction.bDCX                    = false;
  Transaction.bEnWRLatency            = true;
  Transaction.bContinue               = false;  // MSPI CONT is deprecated for Apollo4
  Transaction.pui32Buffer             = pData;

  // Execute the transction over MSPI.
  return am_hal_mspi_blocking_transfer(pMspiHandle,
                                       &Transaction,
                                       AM_DEVICES_W958D6NW_PSRAM_TIMEOUT );
}

static inline uint8_t
am_devices_mspi_psram_w958d6nw_get_ilc(am_devices_mspi_psram_w958d6nw_ilc_e eWLC, uint8_t *pILC)
{
    switch(eWLC)
    {
      case AM_DEVICES_MSPI_PSRAM_W958D6NW_ILC_3:
          *pILC = 3;
          break;
      case AM_DEVICES_MSPI_PSRAM_W958D6NW_ILC_4:
          *pILC = 4;
          break;
      case AM_DEVICES_MSPI_PSRAM_W958D6NW_ILC_5:
          *pILC = 5;
          break;
      case AM_DEVICES_MSPI_PSRAM_W958D6NW_ILC_6:
          *pILC = 6;
          break;
      case AM_DEVICES_MSPI_PSRAM_W958D6NW_ILC_7:
          *pILC = 7;
          break;
      default:
          return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

#if defined(AM_PART_APOLLO5_API)
static uint32_t
am_devices_mspi_psram_w958d6nw_config(void *pHandle, am_hal_mspi_dev_config_t *pMSPISetting)
{
    uint8_t ui8ILC;
    uint32_t ui32Rawdata;
    uint32_t ui32Status;

    switch(pMSPISetting->eClockFreq)
    {
      case AM_HAL_MSPI_CLK_250MHZ:
        ui8ILC = AM_DEVICES_MSPI_PSRAM_W958D6NW_ILC_5;
        break;
      case AM_HAL_MSPI_CLK_192MHZ:
        ui8ILC = AM_DEVICES_MSPI_PSRAM_W958D6NW_ILC_4;
        break;
      default:
        ui8ILC = AM_DEVICES_MSPI_PSRAM_W958D6NW_ILC_3;
        break;
    }


    ui32Status = am_device_command_read(pHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_CFG0_ADDR, &ui32Rawdata, 2);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("Failed to read PSRAM Latency Code!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    ui32Rawdata &= 0x0000FF0F;
    ui32Rawdata |= (uint32_t)ui8ILC << 4;

#ifdef W958D6NW_USE_NON_DQS_MODE
    ui32Rawdata = ( ui32Rawdata & 0xFFF0 ) | 0x000C; // Fixed Latency, burst length=128
    am_util_debug_printf("Set PSRAM Register CFG0 for NON-DQS mode\n");
#else
    ui32Rawdata = ( ui32Rawdata & 0xFFF0 ) | 0x0004; // Variable Latency, burst length=128
    am_util_debug_printf("Set PSRAM Register CFG0 for DQS mode\n");
#endif

    ui32Status = am_device_command_write(pHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_WRITE_REGISTER, true, PSRAM_W958D6NW_REG_CFG0_ADDR, &ui32Rawdata, 2);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("Failed to write PSRAM Latency Code!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    am_devices_mspi_psram_w958d6nw_get_ilc(ui8ILC, &pMSPISetting->ui8WriteLatency);

    pMSPISetting->ui8WriteLatency *= 2;
    pMSPISetting->ui8TurnAround = pMSPISetting->ui8WriteLatency;

    uint32_t ui32RawDataArray[3];
    am_util_debug_printf("Verify PSRAM Register CFG0\n");
    ui32Status = am_device_command_read(pHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_CFG0_ADDR, (uint32_t *)&ui32RawDataArray, 12);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("Failed to read PSRAM Register CFG0!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    else
    {
        uint16_t ui16PsramReg = ui32RawDataArray[2] & 0x0000FFFF;
        am_util_debug_printf("PSRAM Register CFG0 = 0x%04X\n\n", ui16PsramReg);
        ui16PsramReg &= 0x00FF;
        if ((ui16PsramReg >> 4) != ui8ILC)
        {
            am_util_debug_printf("Config PSRAM Latency Code failed!\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }
        ui16PsramReg &= 0x000F;
#ifdef W958D6NW_USE_NON_DQS_MODE
        if (ui16PsramReg != 0x000C)
        {
            am_util_debug_printf("Config PSRAM non DQS mode failed!\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }
#else
        if (ui16PsramReg != 0x0004)
        {
            am_util_debug_printf("Config PSRAM DQS mode failed!\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }
#endif
    }

    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}
#endif

//*****************************************************************************
//
//! @brief Reset the external psram
//
//*****************************************************************************
static void
am_devices_mspi_psram_w958d6nw_reset(uint32_t ui32Module, am_hal_mspi_device_e eMSPIDevice)
{
  //
  // Pull down PSRAM reset pin low for hardware reset
  //
  uint32_t ui32PinNum;
  am_hal_gpio_pincfg_t sPinCfg;
#if defined(AM_PART_APOLLO5_API)
  am_bsp_mspi_reset_pincfg_get(ui32Module, eMSPIDevice, &ui32PinNum, &sPinCfg);
  am_hal_gpio_pinconfig(ui32PinNum, sPinCfg);
#else
  ui32PinNum = AM_BSP_GPIO_PSRAM_RESET;
  sPinCfg = am_hal_gpio_pincfg_output;
  am_hal_gpio_pinconfig(ui32PinNum, sPinCfg);
#endif
  am_hal_gpio_output_set(ui32PinNum);
  am_util_delay_us(200);
  am_hal_gpio_output_clear(ui32PinNum);
  am_util_delay_us(100);
  am_hal_gpio_output_set(ui32PinNum);

}

//*****************************************************************************
//
//! @brief Configure the external psram and returns status.
//!
//! @param pMspiHandle - Pointer to MSPI instance handle.
//! @param pPsramInfo - Pointer to device info to be returned
//!
//! This function configure the write and read latency of the
//! psram and return status
//!
//! @return 32-bit status
//
//*****************************************************************************
static uint32_t
am_devices_mspi_psram_w958d6nw_device_init(void *pMspiHandle, am_devices_mspi_psram_info_t *pPsramInfo)
{
  uint32_t     ui32Status;
  uint32_t     ui32Rawdata;
  uint8_t      ui8VendorIDReg = 0;
  uint8_t      ui8DeviceIDReg = 0;
  uint16_t     ui16PsramReg = 0;

  //
  // Read PSRAM Register ID0
  //
  am_util_debug_printf("Read PSRAM Register ID0\n");
  ui32Status = am_device_command_read(pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_ID0_ADDR, &ui32Rawdata, 2);
  if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
  {
      am_util_debug_printf("Failed to read PSRAM Register ID0!\n");
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }
  else
  {
      ui16PsramReg = (uint16_t)ui32Rawdata;
      am_util_debug_printf("PSRAM Register ID0 = 0x%04X\n", ui16PsramReg);
      ui8VendorIDReg = ui16PsramReg & 0xF;
      pPsramInfo->ui8VendorId = ui8VendorIDReg;
      //
      // Density calculation
      // * 2^(row addr bit count + column addr bit count) * 2^ [word/column, word->Byte] * 32 [bit-address] / 1024 [Byte->kB]
      // * = 2^(row addr bit count + column addr bit count + 5 - 10)
      // */
      pPsramInfo->ui32DeviceSizeKb = 1UL << ((((ui16PsramReg & 0x1F00) >> 8) + 1) + (((ui16PsramReg & 0xF0) >> 4) + 1) + 5 - 10);
      if ( ui8VendorIDReg  == 0x6 )
      {
        am_util_debug_printf("PSRAM Vendor ID =  0110(Winbond)\n\n");
      }
      else
      {
        am_util_debug_printf("Fail to get correct PSRAM Vendor ID!\n\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
      }
  }

  //
  // Read PSRAM Register ID1
  //
  am_util_debug_printf("Read PSRAM Register ID1\n");
  ui32Status = am_device_command_read(pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_ID1_ADDR, &ui32Rawdata, 2);
  if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
  {
      am_util_debug_printf("Failed to read PSRAM Register ID1!\n");
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }
  else
  {
      ui16PsramReg = (uint16_t)ui32Rawdata;
      am_util_debug_printf("PSRAM Register ID1 = 0x%04X\n", ui16PsramReg);
      ui8DeviceIDReg = ui16PsramReg & 0xF;
      pPsramInfo->ui8DeviceId = ui8DeviceIDReg;
      if ( ui8DeviceIDReg  == 0x9 )
      {
        am_util_debug_printf("PSRAM Device Type =  1001(HyperIO)\n\n");
      }
      else
      {
        am_util_debug_printf("Fail to get correct PSRAM Device Type!\n\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
      }
  }

  //
  // Read PSRAM Register CFG0
  //
  am_util_debug_printf("Read PSRAM Register CFG0\n");
  ui32Status = am_device_command_read(pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_CFG0_ADDR, &ui32Rawdata, 2);
  if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
  {
      am_util_debug_printf("Failed to read PSRAM Register CFG0!\n");
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }
  else
  {
      ui16PsramReg = (uint16_t)ui32Rawdata;
      am_util_debug_printf("PSRAM Register CFG0 = 0x%04X\n\n", ui16PsramReg);
  }

  //
  //Change PSRAM register default setting
  //
#ifdef W958D6NW_USE_NON_DQS_MODE
  ui32Rawdata = ( ui32Rawdata & 0xFF00 ) | 0x2C; // Fixed Latency(7 clock), burst length=128
  am_util_debug_printf("Set PSRAM Register CFG0 for NON-DQS mode\n");
#else
  ui32Rawdata = ( ui32Rawdata & 0xFF00 ) | 0xE4; // Variable Latency(3 clock), burst length=128
  am_util_debug_printf("Set PSRAM Register CFG0 for DQS mode\n");
#endif

  //
  // Write PSRAM Register CFG0
  //
  ui32Status = am_device_command_write(pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_WRITE_REGISTER, true, PSRAM_W958D6NW_REG_CFG0_ADDR, &ui32Rawdata, 2);
  if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
  {
      am_util_debug_printf("Failed to write PSRAM Register CFG0!\n");
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }
  else
  {
     am_util_debug_printf("Set PSRAM Register CFG0 into 0x%04X\n", ui32Rawdata);
  }

  //
  // Read PSRAM Register CFG0 again
  //
  am_util_debug_printf("Read PSRAM Register CFG0\n");
  ui32Status = am_device_command_read(pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_CFG0_ADDR, &ui32Rawdata, 2);
  if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
  {
      am_util_debug_printf("Failed to read PSRAM Register CFG0!\n");
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }
  else
  {
      ui16PsramReg = (uint16_t)ui32Rawdata;
      am_util_debug_printf("PSRAM Register CFG0 = 0x%04X\n\n", ui16PsramReg);
  }

  //
  // Read PSRAM Register CFG1
  //
  am_util_debug_printf("Read PSRAM Register CFG1\n");
  ui32Status = am_device_command_read(pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_CFG1_ADDR, &ui32Rawdata, 2);
  if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
  {
      am_util_debug_printf("Failed to read PSRAM Register CFG1!\n");
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }
  else
  {
      ui16PsramReg = (uint16_t)ui32Rawdata;
      am_util_debug_printf("PSRAM Register CFG1 = 0x%04X\n\n", ui16PsramReg);
  }

  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;

}

//*****************************************************************************
//
//! @brief This function takes care of splitting the transaction as needed, if the transaction crosses
//! PSRAM page boundary or because of tCEM restrictions, if hardware does not support it
//!
//! @param pPsram
//! @param bHiPrio
//! @param bWrite
//! @param pui8Buffer
//! @param ui32Address
//! @param ui32NumBytes
//! @param ui32PauseCondition
//! @param ui32StatusSetClr
//! @param pfnCallback
//! @param pCallbackCtxt
//! @return status
//
//*****************************************************************************

static uint32_t
psram_nonblocking_transfer(am_devices_mspi_psram_t *pPsram,
                           bool bHiPrio,
                           bool bWrite,
                           uint8_t *pui8Buffer,
                           uint32_t ui32Address,
                           uint32_t ui32NumBytes,
                           uint32_t ui32PauseCondition,
                           uint32_t ui32StatusSetClr,
                           am_hal_mspi_callback_t pfnCallback,
                           void *pCallbackCtxt)
{
  uint32_t ui32Status = AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
  am_hal_mspi_dma_transfer_t    Transaction;

  // Set the DMA priority
  Transaction.ui8Priority = 1;

  // Set the transfer direction to RX (Read)
  Transaction.eDirection = bWrite ? AM_HAL_MSPI_TX: AM_HAL_MSPI_RX;

  // Initialize the CQ stimulus.
  Transaction.ui32PauseCondition = ui32PauseCondition;
  // Initialize the post-processing
  Transaction.ui32StatusSetClr = 0;

  // Need to be aware of page size
  while (ui32NumBytes)
  {
    uint32_t size;
    if ((ui32Address & 0x3) &&
        ((AM_DEVICES_MSPI_PSRAM_PAGE_SIZE - (ui32Address & (AM_DEVICES_MSPI_PSRAM_PAGE_SIZE - 1))) < ui32NumBytes))
    {
      // Hardware does not support Page splitting if address is not word aligned
      // Need to split the transaction
      size = 4 - (ui32Address & 0x3);
    }
    else
    {
      size = ui32NumBytes;
    }

    bool bLast = (size == ui32NumBytes);
    // Set the transfer count in bytes.
    Transaction.ui32TransferCount = size;

    // Set the address to read data from.
    Transaction.ui32DeviceAddress = ui32Address;

    // Set the target SRAM buffer address.
    Transaction.ui32SRAMAddress = (uint32_t)pui8Buffer;

    if (bLast)
    {
      Transaction.ui32StatusSetClr = ui32StatusSetClr;
    }

    if (bHiPrio)
    {
      ui32Status = am_hal_mspi_highprio_transfer(pPsram->pMspiHandle, &Transaction, AM_HAL_MSPI_TRANS_DMA,
                                                 bLast ? pfnCallback : NULL,
                                                 bLast ? pCallbackCtxt : NULL);
    }
    else
    {
      ui32Status = am_hal_mspi_nonblocking_transfer(pPsram->pMspiHandle, &Transaction, AM_HAL_MSPI_TRANS_DMA,
                                                    bLast ? pfnCallback : NULL,
                                                    bLast ? pCallbackCtxt : NULL);
    }
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      break;
    }
    ui32Address += size;
    ui32NumBytes -= size;
    pui8Buffer += size;

    Transaction.ui32PauseCondition = 0;
  }
  return ui32Status;
}

//*****************************************************************************
//
// Initialize the mspi_psram driver.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_init(uint32_t ui32Module, am_devices_mspi_psram_config_t *pDevCfg, void **ppHandle, void **ppMspiHandle)
{
    uint32_t                    ui32Status;
    am_hal_mspi_dev_config_t    *mspiMemDevCfg;
    am_hal_mspi_dev_config_t    mspiRegDevCfg;
    void                        *pMspiHandle;
    uint32_t                    ui32Index = 0;

    if ((ui32Module > AM_REG_MSPI_NUM_MODULES) || (pDevCfg == NULL))
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Enable fault detection.
    //
#if !defined(AM_PART_APOLLO5_API)
    am_hal_fault_capture_enable();
#endif

    // Allocate a vacant device handle
    for ( ui32Index = 0; ui32Index < AM_DEVICES_MSPI_PSRAM_MAX_DEVICE_NUM; ui32Index++ )
    {
        if ( gWBDDRPsram[ui32Index].bOccupied == false )
        {
            break;
        }
    }
    if ( ui32Index == AM_DEVICES_MSPI_PSRAM_MAX_DEVICE_NUM)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    am_util_debug_printf("\nStart PSRAM Initialization\n");

    //
    // Configure the MSPI into Octal mode for PSRAM register access and Hex mode for data access.
    //
    switch (pDevCfg->eDeviceConfig)
    {
        case AM_HAL_MSPI_FLASH_OCTAL_DDR_CE0:
            mspiMemDevCfg = &WBDDROctalCE0MSPIConfig;
            mspiRegDevCfg = WBDDROctalCE0MSPIConfig;
            break;
        case AM_HAL_MSPI_FLASH_OCTAL_DDR_CE1:
            mspiMemDevCfg = &WBDDROctalCE1MSPIConfig;
            mspiRegDevCfg = WBDDROctalCE1MSPIConfig;
            break;
        case AM_HAL_MSPI_FLASH_HEX_DDR_CE0:
            mspiMemDevCfg = &WBDDRHEXCE0MSPIConfig;
            mspiRegDevCfg = WBDDROctalCE0MSPIConfig;
            break;
        case AM_HAL_MSPI_FLASH_HEX_DDR_CE1:
            mspiMemDevCfg = &WBDDRHEXCE1MSPIConfig;
            mspiRegDevCfg = WBDDROctalCE1MSPIConfig;
            break;
        default:
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    mspiMemDevCfg->eClockFreq = pDevCfg->eClockFreq;
    mspiRegDevCfg.eClockFreq = AM_HAL_MSPI_CLK_48MHZ;

    // First configure in HEX mode and reset
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_initialize(ui32Module, &pMspiHandle))
    {
        am_util_debug_printf("Error - Failed to initialize MSPI.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_power_control(pMspiHandle, AM_HAL_SYSCTRL_WAKE, false))
    {
        am_util_debug_printf("Error - Failed to power on MSPI.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    am_hal_mspi_config_t    mspiCfg = gWBDDRMspiCfg;
    mspiCfg.ui32TCBSize = pDevCfg->ui32NBTxnBufLength;
    mspiCfg.pTCB = pDevCfg->pNBTxnBuf;
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_configure(pMspiHandle, &mspiCfg))
    {
        am_util_debug_printf("Error - Failed to configure MSPI device.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_device_configure(pMspiHandle, &mspiRegDevCfg))
    {
        am_util_debug_printf("Error - Failed to configure MSPI device.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    am_hal_mspi_xip_config_t    xipCfg = gWBDDRXipConfig[ui32Module];

    xipCfg.scramblingStartAddr = pDevCfg->ui32ScramblingStartAddr;
    xipCfg.scramblingEndAddr = pDevCfg->ui32ScramblingEndAddr;
    ui32Status = am_hal_mspi_control(pMspiHandle, AM_HAL_MSPI_REQ_XIP_CONFIG, &xipCfg);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    gWBDDRPsram[ui32Index].sDeviceInfo.ui32BaseAddr = xipCfg.ui32APBaseAddr;

    am_hal_mspi_xip_misc_t    xipMiscCfg = gWBXipMiscCfg[ui32Module];
    ui32Status = am_hal_mspi_control(pMspiHandle, AM_HAL_MSPI_REQ_XIP_MISC_CONFIG, &xipMiscCfg);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    am_hal_mspi_dqs_t dqsCfg = gWBDDRDqsCfg[ui32Module];
    ui32Status = am_hal_mspi_control(pMspiHandle, AM_HAL_MSPI_REQ_DQS, &dqsCfg);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    am_hal_mspi_rxcfg_t RxCfg = gWBMspiRxCfg;
    ui32Status = am_hal_mspi_control(pMspiHandle, AM_HAL_MSPI_REQ_RXCFG, &RxCfg);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Enable DDR emulation in MSPI
    //
    ui32Status = am_hal_mspi_control(pMspiHandle, AM_HAL_MSPI_REQ_DDR_EN, NULL);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_enable(pMspiHandle))
    {
        am_util_debug_printf("Error - Failed to enable MSPI.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    am_bsp_mspi_pins_enable(ui32Module, mspiRegDevCfg.eDeviceConfig);

    am_util_delay_us(150);

    //
    // Reset PSRAM
    //
    am_devices_mspi_psram_w958d6nw_reset(ui32Module, mspiRegDevCfg.eDeviceConfig);

    //
    // Switch to big endian for accessing PSRAM registers
    //
    ui32Status = am_hal_mspi_control(pMspiHandle, AM_HAL_MSPI_REQ_BIG_ENDIAN, NULL);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Initialize device
    //
    if (AM_HAL_STATUS_SUCCESS != am_devices_mspi_psram_w958d6nw_device_init(pMspiHandle, &gWBDDRPsram[ui32Index].sDeviceInfo))
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

#if defined(AM_PART_APOLLO5_API)
    //
    // Config device Latency code
    //
    if ( AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != am_devices_mspi_psram_w958d6nw_config(pMspiHandle, mspiMemDevCfg) )
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
#endif

    //
    // Switch back to little endian for accessing PSRAM memory
    //
    ui32Status = am_hal_mspi_control(pMspiHandle, AM_HAL_MSPI_REQ_LITTLE_ENDIAN, NULL);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Disable MSPI defore re-configuring it
    //
    ui32Status = am_hal_mspi_disable(pMspiHandle);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Reconfig MSPI device settings
    //
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_device_configure(pMspiHandle, mspiMemDevCfg))
    {
        am_util_debug_printf("Error - Failed to reconfig MSPI device.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

  #if defined (AM_PART_APOLLO5_API)
    if (AM_HAL_MSPI_CLK_250MHZ == pDevCfg->eClockFreq
     || AM_HAL_MSPI_CLK_192MHZ == pDevCfg->eClockFreq
     || AM_HAL_MSPI_CLK_125MHZ == pDevCfg->eClockFreq
     || AM_HAL_MSPI_CLK_96MHZ == pDevCfg->eClockFreq)
    {
      RxCfg.ui8RxSmp = 2;
    }
  #else
    if (AM_HAL_MSPI_CLK_96MHZ == pDevCfg->eClockFreq)
    {
      RxCfg.ui8RxSmp = 2;
    }
  #endif
    ui32Status = am_hal_mspi_control(pMspiHandle, AM_HAL_MSPI_REQ_RXCFG, &RxCfg);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Re-Enable MSPI
    //
    ui32Status = am_hal_mspi_enable(pMspiHandle);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Re-config the MSPI pins.
    //
    am_bsp_mspi_pins_enable(ui32Module, mspiMemDevCfg->eDeviceConfig);

    //
    // Enable MSPI interrupts.
    //
    ui32Status = am_hal_mspi_interrupt_clear(pMspiHandle, AM_HAL_MSPI_INT_CQUPD | AM_HAL_MSPI_INT_ERR );
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    ui32Status = am_hal_mspi_interrupt_enable(pMspiHandle, AM_HAL_MSPI_INT_CQUPD | AM_HAL_MSPI_INT_ERR );
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Return the handle.
    //
    gWBDDRPsram[ui32Index].bOccupied = true;
    *ppHandle = (void *)&gWBDDRPsram[ui32Index];
    *ppMspiHandle = gWBDDRPsram[ui32Index].pMspiHandle = pMspiHandle;
    gWBDDRPsram[ui32Index].ui32Module = ui32Module;
    gWBDDRPsram[ui32Index].eDeviceConfig = mspiMemDevCfg->eDeviceConfig;

    //
    // Return the status.
    //
    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// DeInitialize the mspi_psram driver.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_deinit(void *pHandle)
{
    uint32_t    ui32Status;
    am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

    //
    // Disable and clear the interrupts to start with.
    //
    ui32Status = am_hal_mspi_interrupt_disable(pPsram->pMspiHandle, 0xFFFFFFFF);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    ui32Status = am_hal_mspi_interrupt_clear(pPsram->pMspiHandle, 0xFFFFFFFF);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Disable MSPI instance.
    //
    ui32Status = am_hal_mspi_disable(pPsram->pMspiHandle);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    //
    // Disable power to the MSPI instance.
    //
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_power_control(pPsram->pMspiHandle, AM_HAL_SYSCTRL_DEEPSLEEP, false))
    {
        am_util_debug_printf("Error - Failed to power on MSPI.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    //
    // Deinitialize the MPSI instance.
    //
    ui32Status = am_hal_mspi_deinitialize(pPsram->pMspiHandle);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    // Free this device handle
    pPsram->bOccupied = false;

    //
    // Return the status.
    //
    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
//Reads the contents of the external PSRAM into a buffer.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_read(void *pHandle,
                                        uint8_t *pui8RxBuffer,
                                        uint32_t ui32ReadAddress,
                                        uint32_t ui32NumBytes,
                                        bool bWaitForCompletion)
{
  uint32_t                      ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  if (bWaitForCompletion)
  {
    // Start the transaction.
    volatile uint32_t ui32DMAStatus = 0xFFFFFFFF;
    ui32Status = psram_nonblocking_transfer(pPsram, false, false,
                                            pui8RxBuffer,
                                            ui32ReadAddress,
                                            ui32NumBytes,
                                            0,
                                            0,
                                            pfnMSPI_WBPSRAM_DDR_Callback,
                                            (void *)&ui32DMAStatus);

    // Check the transaction status.
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    // Wait for DMA Complete or Timeout
    for (uint32_t i = 0; i < AM_DEVICES_W958D6NW_PSRAM_TIMEOUT ; i++)
    {
      if (ui32DMAStatus != 0xFFFFFFFF)
      {
        break;
      }
      //
      // Call the BOOTROM cycle function to delay for about 1 microsecond.
      //
      am_hal_delay_us(1);
    }

    // Check the status.
    if (ui32DMAStatus != AM_HAL_STATUS_SUCCESS)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
  }
  else
  {
    // Check the transaction status.
    ui32Status = psram_nonblocking_transfer(pPsram, false, false,
                                            pui8RxBuffer,
                                            ui32ReadAddress,
                                            ui32NumBytes,
                                            0,
                                            0,
                                            NULL,
                                            NULL);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
  }
  //
  // Return the status.
  //
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Reads the contents of the external PSRAM into a buffer.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_read_adv(void *pHandle,
                                            uint8_t *pui8RxBuffer,
                                            uint32_t ui32ReadAddress,
                                            uint32_t ui32NumBytes,
                                            uint32_t ui32PauseCondition,
                                            uint32_t ui32StatusSetClr,
                                            am_hal_mspi_callback_t pfnCallback,
                                            void *pCallbackCtxt)
{
  uint32_t                      ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  ui32Status = psram_nonblocking_transfer(pPsram, false, false,
                                          pui8RxBuffer,
                                          ui32ReadAddress,
                                          ui32NumBytes,
                                          ui32PauseCondition,
                                          ui32StatusSetClr,
                                          pfnCallback,
                                          pCallbackCtxt);

  // Check the transaction status.
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  //
  // Return the status.
  //
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Reads the contents of the external psram into a buffer.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_read_hiprio(void *pHandle,
                                               uint8_t *pui8RxBuffer,
                                               uint32_t ui32ReadAddress,
                                               uint32_t ui32NumBytes,
                                               am_hal_mspi_callback_t pfnCallback,
                                               void *pCallbackCtxt)
{
  uint32_t                      ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  ui32Status = psram_nonblocking_transfer(pPsram, true, false,
                                          pui8RxBuffer,
                                          ui32ReadAddress,
                                          ui32NumBytes,
                                          0,
                                          0,
                                          pfnCallback,
                                          pCallbackCtxt);

  // Check the transaction status.
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  //
  // Return the status.
  //
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Programs the given range of psram addresses.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_nonblocking_read(void *pHandle,
                                                    uint8_t *pui8RxBuffer,
                                                    uint32_t ui32ReadAddress,
                                                    uint32_t ui32NumBytes,
                                                    am_hal_mspi_callback_t pfnCallback,
                                                    void *pCallbackCtxt)
{
  uint32_t                      ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  // Check the transaction status.
  ui32Status = psram_nonblocking_transfer(pPsram, false, false,
                                          pui8RxBuffer,
                                          ui32ReadAddress,
                                          ui32NumBytes,
                                          0,
                                          0,
                                          pfnCallback,
                                          pCallbackCtxt);
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  //
  // Return the status.
  //
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Programs the given range of psram addresses.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_write(void *pHandle,
                                         uint8_t *pui8TxBuffer,
                                         uint32_t ui32WriteAddress,
                                         uint32_t ui32NumBytes,
                                         bool bWaitForCompletion)
{
  uint32_t                      ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  if (bWaitForCompletion)
  {
    // Start the transaction.
    volatile uint32_t ui32DMAStatus = 0xFFFFFFFF;
    ui32Status = psram_nonblocking_transfer(pPsram, false, true,
                                            pui8TxBuffer,
                                            ui32WriteAddress,
                                            ui32NumBytes,
                                            0,
                                            0,
                                            pfnMSPI_WBPSRAM_DDR_Callback,
                                            (void *)&ui32DMAStatus);

    // Check the transaction status.
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    // Wait for DMA Complete or Timeout
    for (uint32_t i = 0; i < AM_DEVICES_W958D6NW_PSRAM_TIMEOUT ; i++)
    {
      if (ui32DMAStatus != 0xFFFFFFFF)
      {
        break;
      }
      //
      // Call the BOOTROM cycle function to delay for about 1 microsecond.
      //
      am_hal_delay_us(1);
    }

    // Check the status.
    if (ui32DMAStatus != AM_HAL_STATUS_SUCCESS)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
  }
  else
  {
    // Check the transaction status.
    ui32Status = psram_nonblocking_transfer(pPsram, false, true,
                                            pui8TxBuffer,
                                            ui32WriteAddress,
                                            ui32NumBytes,
                                            0,
                                            0,
                                            NULL,
                                            NULL);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
  }

  //
  // Return the status.
  //
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Programs the given range of psram addresses.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_write_adv(void *pHandle,
                                             uint8_t *puiTxBuffer,
                                             uint32_t ui32WriteAddress,
                                             uint32_t ui32NumBytes,
                                             uint32_t ui32PauseCondition,
                                             uint32_t ui32StatusSetClr,
                                             am_hal_mspi_callback_t pfnCallback,
                                             void *pCallbackCtxt)
{
  uint32_t                      ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  ui32Status = psram_nonblocking_transfer(pPsram, false, true,
                                          puiTxBuffer,
                                          ui32WriteAddress,
                                          ui32NumBytes,
                                          ui32PauseCondition,
                                          ui32StatusSetClr,
                                          pfnCallback,
                                          pCallbackCtxt);

  // Check the transaction status.
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  //
  // Return the status.
  //
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Programs the given range of psram addresses.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_write_hiprio(void *pHandle,
                                                uint8_t *pui8TxBuffer,
                                                uint32_t ui32WriteAddress,
                                                uint32_t ui32NumBytes,
                                                am_hal_mspi_callback_t pfnCallback,
                                                void *pCallbackCtxt)
{
  uint32_t                      ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  // Check the transaction status.
  ui32Status = psram_nonblocking_transfer(pPsram, true, true,
                                          pui8TxBuffer,
                                          ui32WriteAddress,
                                          ui32NumBytes,
                                          0,
                                          0,
                                          pfnCallback,
                                          pCallbackCtxt);

  // Check the transaction status.
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  //
  // Return the status.
  //
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_nonblocking_write(void *pHandle,
                                                     uint8_t *pui8TxBuffer,
                                                     uint32_t ui32WriteAddress,
                                                     uint32_t ui32NumBytes,
                                                     am_hal_mspi_callback_t pfnCallback,
                                                     void *pCallbackCtxt)
{
  uint32_t                      ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  // Check the transaction status.
  ui32Status = psram_nonblocking_transfer(pPsram, false, true,
                                          pui8TxBuffer,
                                          ui32WriteAddress,
                                          ui32NumBytes,
                                          0,
                                          0,
                                          pfnCallback,
                                          pCallbackCtxt);
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  //
  // Return the status.
  //
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Sets up the MSPI and external psram into XIP mode.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_enable_xip(void *pHandle)
{
  uint32_t ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  //
  // Enable XIP on the MSPI.
  //
  ui32Status = am_hal_mspi_control(pPsram->pMspiHandle, AM_HAL_MSPI_REQ_XIP_EN, NULL);
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Removes the MSPI and external psram from XIP mode.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_disable_xip(void *pHandle)
{
  uint32_t ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  //
  // Disable XIP on the MSPI.
  //
  ui32Status = am_hal_mspi_control(pPsram->pMspiHandle, AM_HAL_MSPI_REQ_XIP_DIS, NULL);
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Sets up the MSPI and external psram into scrambling mode.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_enable_scrambling(void *pHandle)
{
  uint32_t ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  //
  // Enable scrambling on the MSPI.
  //
  ui32Status = am_hal_mspi_control(pPsram->pMspiHandle, AM_HAL_MSPI_REQ_SCRAMB_EN, NULL);
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }
  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Removes the MSPI and external psram from scrambling mode.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_disable_scrambling(void *pHandle)
{
  uint32_t ui32Status;
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

  //
  // Disable Scrambling on the MSPI.
  //
  ui32Status = am_hal_mspi_control(pPsram->pMspiHandle, AM_HAL_MSPI_REQ_SCRAMB_DIS, NULL);
  if (AM_HAL_STATUS_SUCCESS != ui32Status)
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Reads the ID of the external psram and returns the value.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_ddr_id(void *pHandle)
{
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;
  uint32_t ui32DeviceID;
  if ( pPsram->pMspiHandle == NULL || !pPsram->bOccupied )
  {
    return 0xFFFFFFFF;
  }

  ui32DeviceID = pPsram->sDeviceInfo.ui8DeviceId;
  ui32DeviceID |= (uint32_t)pPsram->sDeviceInfo.ui8VendorId << 8;
  return ui32DeviceID;
}

//*****************************************************************************
//
//   Reads the info of the external psram and returns the value.
//
//*****************************************************************************
uint32_t am_devices_mspi_psram_w958d6nw_ddr_info(void *pHandle, am_devices_mspi_psram_info_t *pPsramInfo)
{
  am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;
  if ( pPsram->pMspiHandle == NULL || !pPsram->bOccupied )
  {
    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
  }

  *pPsramInfo = pPsram->sDeviceInfo;

  return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
//! @brief write and read back check.
//!
//! @param psMSPISettings - MSPI device structure describing the target spi psram.
//! @param pHandle - MSPI handler which needs to be return
//!
//! This function should be called before any other am_devices_mspi_psram
//! functions. It is used to set tell the other functions how to communicate
//! with the external psram hardware.
//!
//! @return status.
//
//*****************************************************************************
#define PSRAM_CHECK_DATA_SIZE_BYTES  256
//*****************************************************************************
//
//! @brief
//! @param pattern_index
//! @param buff
//! @param len
//! @return
//
//*****************************************************************************
static int prepare_test_pattern(uint32_t pattern_index, uint8_t* buff, uint32_t len)
{
    uint32_t *pui32TxPtr = (uint32_t*)buff;
    uint8_t  *pui8TxPtr  = (uint8_t*)buff;

    // length has to be multiple of 4 bytes
    if ( len % 4 )
    {
        return -1;
    }

    switch ( pattern_index )
    {
        case 0:
            // 0x5555AAAA
            for (uint32_t i = 0; i < len / 4; i++)
            {
               pui32TxPtr[i] = (0x5555AAAA);
            }
            break;
        case 1:
            // 0xFFFF0000
            for (uint32_t i = 0; i < len / 4; i++)
            {
               pui32TxPtr[i] = (0xFFFF0000);
            }
            break;
        case 2:
            // walking
            for (uint32_t i = 0; i < len; i++)
            {
               pui8TxPtr[i] = 0x01 << (i % 8);
            }
            break;
        case 3:
            // incremental from 1
            for (uint32_t i = 0; i < len; i++)
            {
               pui8TxPtr[i] = ((i + 1) & 0xFF);
            }
            break;
        case 4:
            // decremental from 0xff
            for ( uint32_t i = 0; i < len; i++ )
            {
                // decrement starting from 0xff
                pui8TxPtr[i] = (0xff - i) & 0xFF;
            }
            break;
        default:
            // incremental from 1
            for (uint32_t i = 0; i < len; i++)
            {
               pui8TxPtr[i] = ((i + 1) & 0xFF);
            }
            break;

    }

    return 0;
}

//*****************************************************************************
//
//! @brief
//! @param length
//! @param address
//! @return
//
//*****************************************************************************
bool
psram_check(uint32_t length, uint32_t address)
{
    // Try to use as less ram as possible in stack
    uint32_t ui32NumberOfBytesLeft = length;
    uint32_t ui32TestBytes = 0;
    uint32_t ui32AddressOffset = 0;
    uint8_t ui8PatternCounter = 0;
    uint8_t ui8TxBuffer[PSRAM_CHECK_DATA_SIZE_BYTES];
    uint8_t ui8RxBuffer[PSRAM_CHECK_DATA_SIZE_BYTES];

    while ( ui32NumberOfBytesLeft )
    {
        if ( ui32NumberOfBytesLeft > PSRAM_CHECK_DATA_SIZE_BYTES )
        {
            ui32TestBytes = PSRAM_CHECK_DATA_SIZE_BYTES;
            ui32NumberOfBytesLeft -= PSRAM_CHECK_DATA_SIZE_BYTES;
        }
        else
        {
            ui32TestBytes = ui32NumberOfBytesLeft;
            ui32NumberOfBytesLeft = 0;
        }

        //
        // Write to target address with test pattern with given length
        // Use 5 patterns: 0x5555AAAA, 0xFFFF0000, Walking, incremental and decremental
        //

        prepare_test_pattern((ui8PatternCounter) % 5, ui8TxBuffer, ui32TestBytes);
        ui8PatternCounter++;

        // write to target address
        am_hal_sysctrl_bus_write_flush();
        uint8_t * xipPointer = (uint8_t *)(address + ui32AddressOffset);
        memcpy(xipPointer, (uint8_t*)ui8TxBuffer, ui32TestBytes);

        //
        // Read back data
        //
        am_hal_sysctrl_bus_write_flush();
        xipPointer = (uint8_t *)(address + ui32AddressOffset);
        memcpy((uint8_t*)ui8RxBuffer, xipPointer, ui32TestBytes);

        //
        // Verify the result
        //
        if ( memcmp(ui8RxBuffer, ui8TxBuffer, ui32TestBytes) )
        {
            //am_util_debug_printf("    Failed to verify at offset 0x%08x!\n", ui32AddressOffset);
            // verify failed, return directly
            return true;
        }

        ui32AddressOffset += ui32TestBytes;
    }

    return false;
}

//*****************************************************************************
//
//!   @brief Count the longest consecutive 1s in a 32bit word
//!
//! @param pVal
//!
//! @return
//
//*****************************************************************************
static uint32_t
count_consecutive_ones(uint32_t* pVal)
{
    uint32_t count = 0;
    uint32_t data = *pVal;

    while ( data )
    {
        data = (data & (data << 1));
        count++;
    }
    return count;
}

//*****************************************************************************
//
//! @brief Find and return the mid point of the longest continuous 1s in a 32bit word
//! @param pVal
//! @return
//
//*****************************************************************************
static uint32_t
find_mid_point(uint32_t* pVal)
{
    uint32_t pattern_len = 0;
    uint32_t max_len = 0;
    uint32_t pick_point = 0;
    bool pattern_start = false;
    uint32_t val = *pVal;
    uint8_t remainder = 0;
    bool pick_point_flag = false;

    for ( uint32_t i = 0; i < 32; i++ )
    {
        if ( val & (0x01 << i) )
        {
            pattern_start = true;
            pattern_len++;
        }
        else
        {
            if ( pattern_start == true )
            {
                pattern_start = false;
                pick_point_flag = true;
            }
        }
        if ( (i == 31) && ( pattern_start == true ) )
        {
            pick_point_flag = true;
        }

        if (pick_point_flag == true)
        {
            if ( pattern_len > max_len )
            {
                max_len = pattern_len;
                pick_point = i - 1 - pattern_len / 2;
                remainder = pattern_len % 2;
            }
            pattern_len = 0;
            pick_point_flag = false;
        }
    }

    //
    // check the passing window side
    //

    if ( (pick_point < 16) && (val & 0x00000002) )
    {
        // window is likely on low side
        pick_point = pick_point - remainder;    // minus only when pattern length is odd
    }
    else if ( (pick_point > 15) && (val & 0x40000000) )
    {
        // window is likely on high side
        pick_point = pick_point + 1;
    }
    else
    {
        // window is in the middle, no action
    }

    return pick_point;
}

#if defined(AM_PART_APOLLO4P) || defined(AM_PART_APOLLO4L)

//*****************************************************************************
//
//! @brief Checks PSRAM timing and determine a delay setting.
//!
//! @param pDeviceID - Pointer to the return buffer for the Device ID.
//!
//! This function scans through the delay settings of MSPI DDR mode and selects
//! the best parameter to use by tuning TURNAROUND/RXNEG/RXDQSDELAY0 values.
//! This function is only valid in DDR mode and ENABLEDQS0 = 0.
//!
//! @return 32-bit status, scan result in structure type
//
//*****************************************************************************
#define PSRAM_TIMING_SCAN_SIZE_BYTES (128*AM_DEVICES_MSPI_PSRAM_PAGE_SIZE)
static const uint32_t ui32MspiXipBaseAddress[] =
{
    MSPI0_APERTURE_START_ADDR, // mspi0
    MSPI1_APERTURE_START_ADDR, // mspi1
    MSPI2_APERTURE_START_ADDR, // mspi2
};

//*****************************************************************************
//
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_hex_ddr_init_timing_check(uint32_t module,
                                                am_devices_mspi_psram_config_t *pDevCfg,
                                                am_devices_mspi_psram_ddr_timing_config_t *pDevDdrCfg)
{
    uint32_t ui32Status;
    void *pDevHandle;
    void *pHandle;
    uint32_t ui32ResultArray;

    am_hal_mspi_dqs_t scanCfg = gWBDDRDqsCfg[module];
    //
    // initialize interface
    //
    am_hal_mspi_dev_config_t    *psMSPISettings;
    switch (pDevCfg->eDeviceConfig)
    {
        case AM_HAL_MSPI_FLASH_HEX_DDR_CE0:
            psMSPISettings = &WBDDRHEXCE0MSPIConfig;
            break;
        case AM_HAL_MSPI_FLASH_HEX_DDR_CE1:
            psMSPISettings = &WBDDRHEXCE1MSPIConfig;
            break;
        default:
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    uint16_t timeLimit = psMSPISettings->ui16DMATimeLimit;                  // save original setting here
    am_hal_mspi_dma_boundary_e dmaBound0 = psMSPISettings->eDMABoundary;    // save original setting here
    psMSPISettings->ui16DMATimeLimit    = 0;
    psMSPISettings->eDMABoundary        = AM_HAL_MSPI_BOUNDARY_NONE;
    ui32Status = am_devices_mspi_psram_w958d6nw_ddr_init(module, pDevCfg, &pDevHandle, &pHandle);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("    Failed to configure the MSPI and PSRAM Device correctly!\n");
        return ui32Status;
    }
    psMSPISettings->ui16DMATimeLimit = timeLimit;   // restore original setting here
    psMSPISettings->eDMABoundary = dmaBound0;       // restore original setting here

    //
    // Put the MSPI into XIP mode.
    //
    ui32Status = am_devices_mspi_psram_w958d6nw_ddr_enable_xip(pDevHandle);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("    Failed to disable XIP mode in the MSPI!\n");
        return ui32Status;
    }

    //
    // Start scan loop
    //
    for ( uint8_t RxDqs_Index = 0; RxDqs_Index <= 31; RxDqs_Index++ )
    {
        // set RXDQSDELAY0 value
        scanCfg.ui8RxDQSDelay   = RxDqs_Index;
        // apply settings
        ui32Status = am_hal_mspi_control(pHandle, AM_HAL_MSPI_REQ_DQS, &scanCfg);
        if (AM_HAL_STATUS_SUCCESS != ui32Status)
        {
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        // run data check
        if ( false == psram_check(PSRAM_TIMING_SCAN_SIZE_BYTES, ui32MspiXipBaseAddress[module] + RxDqs_Index) )
        {
            // data check pass
            ui32ResultArray |= 0x01 << RxDqs_Index;
        }
        else
        {
            // data check failed
        }
    }

    //
    // Check result
    //
    uint32_t ui32Result = 0;
    ui32Result = count_consecutive_ones(&ui32ResultArray);

    //
    // print result for test
    //
    am_util_debug_printf("    Setting  = 0x%08X\n",  ui32ResultArray);

#if defined(AM_DEBUG_PRINTF)
    am_util_debug_printf("    Max length = %d \n", ui32Result);
#endif
    am_util_stdio_printf("    Timing Scan found a window %d fine steps wide.\n", ui32Result);

    //
    // Check consecutive passing settings
    //
    if ( ui32Result < PSRAM_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH )
    {
        // too short is the passing settings
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Find RXDQSDELAY Value
    //
    uint32_t dqsdelay = find_mid_point(&ui32ResultArray);

#if defined(AM_DEBUG_PRINTF)
    am_util_stdio_printf("Timing Scan set the fine delay to %d steps.\n", dqsdelay);
#endif

    //
    // Deinitialize the MSPI interface
    //
    am_devices_mspi_psram_w958d6nw_ddr_deinit(pDevHandle);

    //
    // Set output values
    //
    pDevDdrCfg->ui32Rxdqsdelay = dqsdelay;
    pDevDdrCfg->ui32Rxneg = 0;
#ifdef W958D6NW_USE_NON_DQS_MODE
    pDevDdrCfg->ui32Turnaround = 14;
#else
    pDevDdrCfg->ui32Turnaround = 6;
#endif

    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Apply given DDR timing settings to target MSPI instance.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_apply_hex_ddr_timing(void *pHandle,
                                           am_devices_mspi_psram_ddr_timing_config_t *pDevDdrCfg)
{
    am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;
    am_hal_mspi_dqs_t applyCfg = gWBDDRDqsCfg[pPsram->ui32Module];
    // apply timing settings
    applyCfg.ui8RxDQSDelay      = pDevDdrCfg->ui32Rxdqsdelay;

#if defined(AM_DEBUG_PRINTF)
    am_util_stdio_printf("    Timing Scan set the RxDQSDelay = %d .\n", applyCfg.ui8RxDQSDelay);
#endif

    return am_hal_mspi_control(pPsram->pMspiHandle, AM_HAL_MSPI_REQ_DQS, &applyCfg);

}
#endif

#if defined (AM_PART_APOLLO5_API)

//*****************************************************************************
//
// Configurable parameters for a scan
//
//*****************************************************************************
#define PSRAM_TIMING_SCAN_SIZE_BYTES (4*AM_DEVICES_MSPI_PSRAM_PAGE_SIZE)

#define SCAN_TXDQSDELAY
#define SCAN_RXDQSDELAY

#if defined(SCAN_TXNEG)
#define SCAN_TXNEG_START 0
#define SCAN_TXNEG_END   1
#endif

#if defined(SCAN_RXNEG)
#define SCAN_RXNEG_START 0
#define SCAN_RXNEG_END   1
#endif

#if defined(SCAN_RXCAP)
#define SCAN_RXCAP_START 0
#define SCAN_RXCAP_END   1
#endif

#if defined(SCAN_TURNAROUND)
#define SCAN_TURNAROUND_START 0
#define SCAN_TURNAROUND_END   1
#endif

#if defined(SCAN_TXDQSDELAY)
#define SCAN_TXDQSDELAY_START 0
#define SCAN_TXDQSDELAY_END   10
#endif

#if defined(SCAN_RXDQSDELAY)
#define SCAN_RXDQSDELAY_START 0
#define SCAN_RXDQSDELAY_END   31
#endif

//*****************************************************************************
//
// Checks PSRAM timing and determine a delay setting.
//
//*****************************************************************************
uint32_t am_devices_mspi_psram_hex_ddr_init_timing_check(uint32_t ui32Module,
                                                am_devices_mspi_psram_config_t *pDevCfg,
                                                am_devices_mspi_psram_ddr_timing_config_t *pDevDdrCfg)
{
    uint32_t ui32Status;
    am_devices_mspi_psram_t *pPsram;
    void *pHandle;
    uint32_t Txdqsdelay = 0;
    uint32_t Rxdqsdelay = 0;

    uint32_t ui32CheckAddress;
    uint32_t ui32CCOResult = 0;
    uint32_t ui32TxResult = 0;
    uint32_t ui32RxResultArray[32] = {0};

    am_hal_mspi_timing_scan_t scanCfg;
    //
    // initialize interface
    //
    ui32Status = am_devices_mspi_psram_w958d6nw_ddr_init(ui32Module, pDevCfg, (void **)&pPsram, &pHandle);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("    Failed to configure the MSPI and PSRAM Device correctly!\n");
        return ui32Status;
    }

    //
    //get value configured by init & hal
    //
    ui32Status = am_hal_mspi_control(pPsram->pMspiHandle, AM_HAL_MSPI_REQ_TIMING_SCAN_GET, &scanCfg);
    if (AM_HAL_STATUS_SUCCESS != ui32Status)
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Put the MSPI into XIP mode.
    //
    ui32Status = am_devices_mspi_psram_w958d6nw_ddr_enable_xip(pPsram);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("    Failed to disable XIP mode in the MSPI!\n");
        return ui32Status;
    }

    am_util_debug_printf("    Start Use XIP to Timing Scan!\n");
    uint32_t ui32TxNeg = scanCfg.bTxNeg;
    uint32_t ui32RxNeg = scanCfg.bRxNeg;
    uint32_t ui32RxCap = scanCfg.bRxCap;
    uint32_t ui32Turnaround = scanCfg.ui8Turnaround;
#if defined(SCAN_TXNEG)
    for ( ui32TxNeg = SCAN_TXNEG_START; ui32TxNeg <= SCAN_TXNEG_END; ui32TxNeg++ )
#endif
    {
        scanCfg.bTxNeg = (bool)ui32TxNeg;
#if defined(SCAN_RXNEG)
        for ( ui32RxNeg = SCAN_RXNEG_START; ui32RxNeg <= SCAN_RXNEG_END; ui32RxNeg++ )
#endif
        {
            scanCfg.bRxNeg = (bool)ui32RxNeg;
#if defined(SCAN_RXCAP)
            for ( ui32RxCap = SCAN_RXCAP_START; ui32RxCap <= SCAN_RXCAP_END; ui32RxCap++ )
#endif
            {
                scanCfg.bRxCap = (bool)ui32RxCap;
#if defined(SCAN_TURNAROUND)
                for (scanCfg.ui8Turnaround = ui32Turnaround + SCAN_TURNAROUND_START; scanCfg.ui8Turnaround <= ui32Turnaround + SCAN_TURNAROUND_END; scanCfg.ui8Turnaround++ )
#endif
                {
                    am_util_debug_printf("    TxNeg=%d, RxNeg=%d, RxCap=%d, Turnaround=%d\n", scanCfg.bTxNeg, scanCfg.bRxNeg, scanCfg.bRxCap, scanCfg.ui8Turnaround)
                    ui32TxResult = 0;
                    memset(ui32RxResultArray, 0, sizeof(ui32RxResultArray));
#if defined(SCAN_TXDQSDELAY)
                    for (scanCfg.ui8TxDQSDelay = SCAN_TXDQSDELAY_START; scanCfg.ui8TxDQSDelay <= SCAN_TXDQSDELAY_END; scanCfg.ui8TxDQSDelay++)
#endif
                    {
#if defined(SCAN_RXDQSDELAY)
                        for (scanCfg.ui8RxDQSDelay = SCAN_RXDQSDELAY_START; scanCfg.ui8RxDQSDelay <= SCAN_RXDQSDELAY_END; scanCfg.ui8RxDQSDelay++)
#endif
                        {
                            //
                            // apply settings
                            //
                            ui32Status = am_hal_mspi_control(pPsram->pMspiHandle, AM_HAL_MSPI_REQ_TIMING_SCAN, &scanCfg);
                            if (AM_HAL_STATUS_SUCCESS != ui32Status)
                            {
                                return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
                            }
                            uint32_t ui32Offset = (scanCfg.bTxNeg + scanCfg.bRxNeg + scanCfg.bRxCap + scanCfg.ui8Turnaround) * PSRAM_TIMING_SCAN_SIZE_BYTES + (scanCfg.ui8TxDQSDelay + scanCfg.ui8RxDQSDelay) * 2;
                            ui32CheckAddress = gWBDDRXipConfig[ui32Module].ui32APBaseAddr + ui32Offset;
                            //
                            // run data check
                            //
                            if ( psram_check(PSRAM_TIMING_SCAN_SIZE_BYTES, ui32CheckAddress) )
                            {
                                //
                                // data check failed
                                //
                            }
                            else
                            {
                                //
                                // data check pass
                                //
                                ui32RxResultArray[scanCfg.ui8TxDQSDelay] |= 0x01 << scanCfg.ui8RxDQSDelay;
                            }
                        }
#if defined(SCAN_RXDQSDELAY)
                        ui32CCOResult = count_consecutive_ones(&ui32RxResultArray[scanCfg.ui8TxDQSDelay]);
                        if ( ui32CCOResult > PSRAM_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH )
                        {
                            ui32TxResult |= 0x01 << scanCfg.ui8TxDQSDelay;
                        }
                        am_util_debug_printf("    TxDQSDelay: %d, RxDQSDelay Scan = 0x%08X, Window size = %d\n", scanCfg.ui8TxDQSDelay, ui32RxResultArray[scanCfg.ui8TxDQSDelay], ui32CCOResult);
#else
                        if ( ui32RxResultArray[scanCfg.ui8TxDQSDelay] != 0 )
                        {
                            ui32TxResult |= 0x01 << scanCfg.ui8TxDQSDelay;
                        }
                        am_util_debug_printf("    TxDQSDelay: %d, RxDQSDelay Scan = 0x%08X\n", scanCfg.ui8TxDQSDelay, ui32RxResultArray[scanCfg.ui8TxDQSDelay]);
#endif
                    }
                    //
                    // Check Result
                    //
                    if ( ui32TxResult == 0 )
                    {
                        //
                        // no window is found
                        //
#if defined(SCAN_TXNEG) || defined(SCAN_RXNEG) || defined(SCAN_RXCAP) || defined(SCAN_TURNAROUND)
                        continue;
#else
                        am_util_debug_printf("Timing Scan found no window!\n");
                        //
                        // Deinitialize the MSPI interface
                        //
                        am_devices_mspi_psram_w958d6nw_ddr_deinit(pPsram);
                        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
#endif
                    }
#if defined(SCAN_TXDQSDELAY)
                    //
                    // Find TXDQSDELAY Value
                    //
                    Txdqsdelay = find_mid_point(&ui32TxResult);
#else
                    Txdqsdelay = scanCfg.ui8TxDQSDelay;
#endif

#if defined(SCAN_RXDQSDELAY)
                    //
                    // Find RXDQSDELAY Value
                    //
                    Rxdqsdelay = find_mid_point(&ui32RxResultArray[Txdqsdelay]);
#else
                    Rxdqsdelay = scanCfg.ui8RxDQSDelay;
#endif

                    am_util_debug_printf("Selected timing scan setting: TxNeg=%d, RxNeg=%d, RxCap=%d, Turnaround=%d, TxDQSDelay=%d, RxDQSDelay=%d\n", scanCfg.bTxNeg, scanCfg.bRxNeg, scanCfg.bRxCap, scanCfg.ui8Turnaround, Txdqsdelay, Rxdqsdelay);

                    scanCfg.ui8TxDQSDelay = Txdqsdelay;
                    scanCfg.ui8RxDQSDelay = Rxdqsdelay;

                    //
                    // Set output values
                    //
                    *pDevDdrCfg = scanCfg;
                    am_devices_mspi_psram_w958d6nw_ddr_deinit(pPsram);
                    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
                }
            }
        }
    }
    am_util_debug_printf("Timing Scan found no window!\n");
    //
    // Deinitialize the MSPI interface
    //
    am_devices_mspi_psram_w958d6nw_ddr_deinit(pPsram);

    return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
}

//*****************************************************************************
//
//  Apply given DDR timing settings to target MSPI instance.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_apply_hex_ddr_timing(void *pHandle,
                                           am_devices_mspi_psram_ddr_timing_config_t *pDevDdrCfg)
{
    am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;
    if ( pHandle == NULL )
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    //
    // apply timing settings
    //
    am_util_stdio_printf("    Apply Timing TxNeg=%d, RxNeg=%d, RxCap=%d, Turnaround=%d, TxDQSDelay=%d, RxDQSDelay=%d\n", pDevDdrCfg->bTxNeg,
                                                                                                                         pDevDdrCfg->bRxNeg,
                                                                                                                         pDevDdrCfg->bRxCap,
                                                                                                                         pDevDdrCfg->ui8Turnaround,
                                                                                                                         pDevDdrCfg->ui8TxDQSDelay,
                                                                                                                         pDevDdrCfg->ui8RxDQSDelay);
    return am_hal_mspi_control(pPsram->pMspiHandle, AM_HAL_MSPI_REQ_TIMING_SCAN, pDevDdrCfg);

}

//*****************************************************************************
//
// Enter hybrid sleep part of apollo5a CE workaround
//
// Send a command to enter hybrid sleep mode.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_enter_hybridsleep(void *pHandle)
{
    uint32_t     ui32Status;
    uint32_t     ui32Rawdata = 0;
    am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;
    if ( pHandle == NULL )
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    am_hal_mspi_device_e lcl_devCfg = pPsram->eDeviceConfig;
    am_hal_mspi_dev_config_t    mspiRegDevCfg;
    //
    // Reconfigure MSPI to OCTAL
    //
    switch (pPsram->eDeviceConfig)
    {
        case AM_HAL_MSPI_FLASH_OCTAL_DDR_CE0:
        case AM_HAL_MSPI_FLASH_HEX_DDR_CE0:
            lcl_devCfg = AM_HAL_MSPI_FLASH_OCTAL_DDR_CE0;
            mspiRegDevCfg = WBDDROctalCE0MSPIConfig;
            break;
        case AM_HAL_MSPI_FLASH_OCTAL_DDR_CE1:
        case AM_HAL_MSPI_FLASH_HEX_DDR_CE1:
            lcl_devCfg = AM_HAL_MSPI_FLASH_OCTAL_DDR_CE1;
            mspiRegDevCfg = WBDDROctalCE1MSPIConfig;
            break;
        default:
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Disable MSPI defore re-configuring it
    //
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_disable(pPsram->pMspiHandle))
    {
        am_util_debug_printf("Error - Failed to Disable MSPI.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    mspiRegDevCfg.eClockFreq = AM_HAL_MSPI_CLK_48MHZ;

    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_device_configure(pPsram->pMspiHandle, &mspiRegDevCfg))
    {
        am_util_debug_printf("Error - Failed to configure MSPI device.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Re-Enable MSPI
    //
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_enable(pPsram->pMspiHandle))
    {
        am_util_debug_printf("Error - Failed to Enable MSPI!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Re-config the MSPI pins.
    //
    am_bsp_mspi_pins_enable(pPsram->ui32Module, lcl_devCfg);

    //
    // Read PSRAM Register CFG1
    //
    am_util_debug_printf("Read PSRAM Register CFG1\n");
    ui32Status = am_device_command_read(pPsram->pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_CFG1_ADDR, &ui32Rawdata, 2);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("Failed to read PSRAM Register CFG1!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    else
    {
        am_util_debug_printf("PSRAM Register CFG1 = 0x%04X\n\n", ui32Rawdata);
    }

    //
    // For CR1:
    //          [15-8] - When writing this register, these bits should keep FFh for future compatibility.
    //          [7]    - When writing this register, these bits should keep 1b for future compatibility.
    //          [5]    - Hybrid Sleep - 1b - Writing 1 to CR1[5] causes the device to enter Hybrid Sleep (HS) State
    //
    ui32Rawdata |= 0xFFa0;

    //
    // Send command to enter hybrid sleep
    //
    if (AM_HAL_STATUS_SUCCESS != am_device_command_write(pPsram->pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_WRITE_REGISTER, true, PSRAM_W958D6NW_REG_CFG1_ADDR, &ui32Rawdata, 2))
    {
        am_util_debug_printf("Failed to write PSRAM CFG1 register!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    // tHSIN min = 3us
    am_util_delay_us(W958D6NW_tHS_MAX_US);

    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Enter hybrid sleep
//
// This function soft-resets the device to bring it out of hybrid sleep.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_exit_hybridsleep(void *pHandle)
{
    am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

    if ( pHandle == NULL )
    {
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    uint32_t ui32PIOBuffer = 0;
    //
    // Send dummy command to pull CE for tXPHS
    //
    if (AM_HAL_STATUS_SUCCESS != am_device_command_write(pPsram->pMspiHandle, 0x0000, true, 0, &ui32PIOBuffer, 2))
    {
      am_util_debug_printf("Error - Failed to send dummy command.\n");
      return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Delay after setting pin high to allow for device to accept command
    // and exit hybrid sleep mode.
    //
    am_util_delay_us(W958D6NW_tEXTHS_MIN_US);

    //
    // Disable MSPI defore re-configuring it
    //
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_disable(pPsram->pMspiHandle))
    {
        am_util_debug_printf("Error - Failed to Disable MSPI.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Reconfigure MSPI
    //
    am_hal_mspi_dev_config_t*    mspiMemDevCfg;
    switch (pPsram->eDeviceConfig)
    {
        case AM_HAL_MSPI_FLASH_OCTAL_DDR_CE0:
        case AM_HAL_MSPI_FLASH_HEX_DDR_CE0:
            mspiMemDevCfg = &WBDDRHEXCE0MSPIConfig;
            break;
        case AM_HAL_MSPI_FLASH_OCTAL_DDR_CE1:
        case AM_HAL_MSPI_FLASH_HEX_DDR_CE1:
            mspiMemDevCfg = &WBDDRHEXCE1MSPIConfig;
            break;
        default:
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_device_configure(pPsram->pMspiHandle, mspiMemDevCfg))
    {
        am_util_debug_printf("Error - Failed to configure MSPI device.\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Re-Enable MSPI
    //
    if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_enable(pPsram->pMspiHandle))
    {
        am_util_debug_printf("Error - Failed to Enable MSPI!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    //
    // Re-config the MSPI pins.
    //
    am_bsp_mspi_pins_enable(pPsram->ui32Module, pPsram->eDeviceConfig);

    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}
#else
//*****************************************************************************
//
// Enter hybrid sleep
//
// Send a command to enter hybrid sleep mode.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_enter_hybridsleep(void *pHandle)
{
    uint32_t     ui32Status;
    uint32_t     ui32Rawdata = 0;
    am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;

    //
    // Due to MSPI check prior to transfer, user cannot write to PSRAM_W958D6NW_REG_CFG1_ADDR (0x801) in HEX Mode
    // Device address must be word-aligned in HEX PIO mode
    //
    if ((pPsram->eDeviceConfig == AM_HAL_MSPI_FLASH_HEX_DDR_CE0) || (pPsram->eDeviceConfig == AM_HAL_MSPI_FLASH_HEX_DDR_CE1))
    {
        am_hal_mspi_device_e lcl_devCfg = pPsram->eDeviceConfig;

        switch (pPsram->eDeviceConfig)
        {
            case AM_HAL_MSPI_FLASH_HEX_DDR_CE0:
                lcl_devCfg = AM_HAL_MSPI_FLASH_OCTAL_DDR_CE0;
                break;
            case AM_HAL_MSPI_FLASH_HEX_DDR_CE1:
                lcl_devCfg = AM_HAL_MSPI_FLASH_OCTAL_DDR_CE1;
                break;
            default:
                return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        //
        // Disable MSPI defore re-configuring it
        //
        if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_disable(pPsram->pMspiHandle))
        {
            am_util_debug_printf("Error - Failed to Disable MSPI.\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        //
        // Reconfigure MSPI to OCTAL
        //
        am_hal_mspi_dev_config_t*    mspiRegDevCfg;
        switch (pPsram->eDeviceConfig)
        {
            case AM_HAL_MSPI_FLASH_HEX_DDR_CE0:
                mspiRegDevCfg = &WBDDROctalCE0MSPIConfig;
                break;
            case AM_HAL_MSPI_FLASH_HEX_DDR_CE1:
                mspiRegDevCfg = &WBDDROctalCE1MSPIConfig;
                break;
            default:
                return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_device_configure(pPsram->pMspiHandle, mspiRegDevCfg))
        {
            am_util_debug_printf("Error - Failed to configure MSPI device.\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        //
        // Re-Enable MSPI
        //
        if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_enable(pPsram->pMspiHandle))
        {
            am_util_debug_printf("Error - Failed to Enable MSPI!\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        //
        // Re-config the MSPI pins.
        //
        am_bsp_mspi_pins_enable(pPsram->ui32Module, lcl_devCfg);
    }

    //
    // Read PSRAM Register CFG1
    //
    am_util_debug_printf("Read PSRAM Register CFG1\n");
    ui32Status = am_device_command_read(pPsram->pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_READ_REGISTER, true, PSRAM_W958D6NW_REG_CFG1_ADDR, &ui32Rawdata, 2);
    if (AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS != ui32Status)
    {
        am_util_debug_printf("Failed to read PSRAM Register CFG1!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }
    else
    {
        am_util_debug_printf("PSRAM Register CFG1 = 0x%04X\n\n", ui32Rawdata);
    }

    //
    // For CR1:
    //          [15-8] - When writing this register, these bits should keep FFh for future compatibility.
    //          [7]    - When writing this register, these bits should keep 1b for future compatibility.
    //          [5]    - Hybrid Sleep - 1b - Writing 1 to CR1[5] causes the device to enter Hybrid Sleep (HS) State
    //
    ui32Rawdata |= 0xFFa0;

    //
    // Send command to enter hybrid sleep
    //
    if (AM_HAL_STATUS_SUCCESS != am_device_command_write(pPsram->pMspiHandle, AM_DEVICES_MSPI_PSRAM_W958D6NW_DDR_WRITE_REGISTER, true, PSRAM_W958D6NW_REG_CFG1_ADDR, &ui32Rawdata, 2))
    {
        am_util_debug_printf("Failed to write PSRAM CFG1 register!\n");
        return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
    }

    // tHSIN min = 3us
    am_util_delay_us(W958D6NW_tHS_MAX_US);

    return AM_DEVICES_MSPI_PSRAM_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Enter hybrid sleep
//
// This function soft-resets the device to bring it out of hybrid sleep.
//
//*****************************************************************************
uint32_t
am_devices_mspi_psram_w958d6nw_exit_hybridsleep(void *pHandle)
{
    am_devices_mspi_psram_t *pPsram = (am_devices_mspi_psram_t *)pHandle;
    uint32_t pin_num = 0;
    am_hal_gpio_pincfg_t gpio_pincfg = AM_HAL_GPIO_PINCFG_DEFAULT;

    if ( pHandle == NULL )
    {
        return AM_HAL_STATUS_FAIL;
    }

    am_bsp_mspi_ce_pincfg_get(pPsram->ui32Module, pPsram->eDeviceConfig, &pin_num, &gpio_pincfg);

    //
    // Configure CE pin to output and hold high
    //
    am_hal_gpio_output_set(pin_num);
    am_hal_gpio_pinconfig(pin_num, am_hal_gpio_pincfg_output);

    //
    // Start wake pulse on CE
    //
    am_hal_gpio_output_clear(pin_num);

    //
    // hold CE pin for 60ns - 3000ns
    //
    am_util_delay_us(W958D6NW_tCSHS_US);

    //
    // Set pin to high to finish wake
    //
    am_hal_gpio_output_set(pin_num);

    //
    // Reconfigure pin for CE on PSRAM
    //
    am_hal_gpio_pinconfig(pin_num, gpio_pincfg);

    //
    // Delay after setting pin high to allow for device to accept command
    // and exit hybrid sleep mode.
    //
    am_util_delay_us(W958D6NW_tEXTHS_MIN_US);

    if ( (pPsram->eDeviceConfig == AM_HAL_MSPI_FLASH_HEX_DDR_CE0) || (pPsram->eDeviceConfig == AM_HAL_MSPI_FLASH_HEX_DDR_CE1) )
    {
        //
        // Disable MSPI defore re-configuring it
        //
        if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_disable(pPsram->pMspiHandle))
        {
            am_util_debug_printf("Error - Failed to Disable MSPI.\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        //
        // Reconfigure MSPI to HEX
        //
        am_hal_mspi_dev_config_t*    mspiMemDevCfg;
        switch (pPsram->eDeviceConfig)
        {
            case AM_HAL_MSPI_FLASH_HEX_DDR_CE0:
                mspiMemDevCfg = &WBDDRHEXCE0MSPIConfig;
                break;
            case AM_HAL_MSPI_FLASH_HEX_DDR_CE1:
                mspiMemDevCfg = &WBDDRHEXCE1MSPIConfig;
                break;
            default:
                return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_device_configure(pPsram->pMspiHandle, mspiMemDevCfg))
        {
            am_util_debug_printf("Error - Failed to configure MSPI device.\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        //
        // Re-Enable MSPI
        //
        if (AM_HAL_STATUS_SUCCESS != am_hal_mspi_enable(pPsram->pMspiHandle))
        {
            am_util_debug_printf("Error - Failed to Enable MSPI!\n");
            return AM_DEVICES_MSPI_PSRAM_STATUS_ERROR;
        }

        //
        // Re-config the MSPI pins.
        //
        am_bsp_mspi_pins_enable(pPsram->ui32Module, pPsram->eDeviceConfig);
    }

    return AM_HAL_STATUS_SUCCESS;
}
#endif
//*****************************************************************************
//
// End Doxygen group.
//! @}
//
//*****************************************************************************
