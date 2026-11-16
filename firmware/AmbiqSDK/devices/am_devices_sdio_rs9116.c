//*****************************************************************************
//
//! @file am_devices_sdio_rs9116.c
//!
//! @brief SiliconLabs RS9116 Wi-Fi device driver.
//!
//! @addtogroup RS9116 SDIO Wi-Fi Driver
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
#include "am_util_stdio.h"
#include "am_bsp.h"
#include "am_util.h"
#include "am_util_delay.h"
#include "am_devices_sdio_rs9116.h"

//*****************************************************************************
//
// Global variables.
//
//*****************************************************************************
am_hal_card_host_t *pSdhcCardHost = NULL;
am_hal_card_t       RS9116SdioCard;
uint8_t             sdio_init_done = 0;
uint32_t            ui32SdioModule = 0;

//*****************************************************************************
//
// @fn          void rsi_sdio_write_multiple()
// @brief       This API is used to write the packet on to the SDIO interface in block mode.
// @param[in]   tx_data is the buffer to be written to sdio.
// @param[in]   Addr of the mem to which the data has to be written.
// @param[in]   no_of_blocks is the blocks present to be transfered.
// @return      0 in case of success ,*              - 1 in case of failure
// @section description
//  This API is used to write the packet on to the SDIO interface
//
//*****************************************************************************
int16_t rsi_sdio_write_multiple(uint8_t *tx_data, uint32_t Addr, uint32_t no_of_blocks)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 1;
    uint32_t ui32BlkSize = 256;

    ui32Status = am_hal_sdio_card_multi_bytes_write_sync(&RS9116SdioCard, ui32Func, Addr, (uint8_t *)tx_data, no_of_blocks, ui32BlkSize, true);
    if ( (ui32Status & 0xFFFF) != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("RS9116 SDIO multiple blocks write fail. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    return AM_DEVICES_SDIO_RS9116_STATUS_SUCCESS;
}

//*****************************************************************************
//
// @fn          uint8_t rsi_sdio_read_multiple()
// @brief       API is used to read no of bytes in block mode from device.
// @param[in]   read_buff is the buffer to be stored with the data read from device.
// @param[in]   Addr of the mem to be read.
// @return      0 in case of success ,*              - 1 in case of failure
// @section description This function gets the packet coming from the module and
// copies to the buffer pointed
//
//*****************************************************************************
int8_t rsi_sdio_read_multiple(uint8_t *read_buff, uint32_t Addr, uint32_t no_of_blocks)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 1;
    uint32_t ui32BlkSize = 256;

    ui32Status = am_hal_sdio_card_multi_bytes_read_sync(&RS9116SdioCard, ui32Func, Addr, (uint8_t *)read_buff, no_of_blocks, ui32BlkSize, true);
    if ( (ui32Status & 0xFFFF) != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("RS9116 SDIO multiple blocks read fail. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    return AM_DEVICES_SDIO_RS9116_STATUS_SUCCESS;
}

//*****************************************************************************
//
// @fn          uint8_t sdio_reg_writeb()
// @brief       API is used to write 1 byte of data to sdio slave register space.
// @param[in]   Addr of the reg to be written.
// @param[in]   Buffer of data to be written to sdio slave reg.
// @return      0 in case of success ,*              - 1 in case of failure
// @section description This function writes 1 byte of data to the slave device
//
//*****************************************************************************
int8_t sdio_reg_writeb(uint32_t Addr, uint8_t *dBuf)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 0;
    uint8_t  ui8WrByte = 0;

    ui8WrByte = *dBuf;
    ui32Status = am_hal_sdio_card_byte_write(&RS9116SdioCard, ui32Func, Addr, ui8WrByte);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("RS9116 SDIO reg byte write fail. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    return AM_DEVICES_SDIO_RS9116_STATUS_SUCCESS;
}

//*****************************************************************************
//
// @fn          uint8_t sdio_reg_readb()
// @brief       API is used to read 1 byte of data from sdio slave register space.
// @param[in]   Addr of the reg to be read.
// @param[in]   Buffer of data to be read from sdio slave reg.
// @return      0 in case of success ,*              - 1 in case of failure
// @section description This function gets the 1 byte of data from the slave device
//
//*****************************************************************************
int8_t sdio_reg_readb(uint32_t Addr, uint8_t *dBuf)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 0;

    ui32Status = am_hal_sdio_card_byte_read(&RS9116SdioCard, ui32Func, Addr, (uint8_t *)dBuf);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("RS9116 SDIO reg byte read fail. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    return AM_DEVICES_SDIO_RS9116_STATUS_SUCCESS;
}

//*****************************************************************************
//
// @fn          int16_t rsi_sdio_readb()
// @brief       API is used to read n bytes of data from device space in byte mode.
// @param[in]   Addr of the data to be read.
// @param[in]   Buffer of data to be read from sdio device.
// @return      0 in case of success ,*              - 1 in case of failure
// @section description This function gets the n  bytes of data from the device
//
//*****************************************************************************
int16_t rsi_sdio_readb(uint32_t addr, uint16_t len, uint8_t *dBuf)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 1;
    uint32_t ui32BlkCnt = 0;

    ui32Status = am_hal_sdio_card_multi_bytes_read_sync(&RS9116SdioCard, ui32Func, addr, (uint8_t *)dBuf, ui32BlkCnt, len, true);
    if ( (ui32Status & 0xFFFF) != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("RS9116 SDIO multiple bytes read fail. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    return AM_DEVICES_SDIO_RS9116_STATUS_SUCCESS;
}

//*****************************************************************************
//
// @fn          int16_t rsi_sdio_writeb()
// @brief       API is used to write n bytes of data to device space in byte mode.
// @param[in]   Addr of the data to be written.
// @param[in]   Buffer of data to be written to sdio device.
// @return      0 in case of success ,*              - 1 in case of failure
// @section description This function writes the n bytes of data to the device
//
//*****************************************************************************
int16_t rsi_sdio_writeb(uint32_t addr, uint16_t len, uint8_t *dBuf)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 1;
    uint32_t ui32BlkCnt = 0;

    ui32Status = am_hal_sdio_card_multi_bytes_write_sync(&RS9116SdioCard, ui32Func, addr, (uint8_t *)dBuf, ui32BlkCnt, len, true);
    if ( (ui32Status & 0xFFFF) != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("RS9116 SDIO multiple bytes write fail. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    return AM_DEVICES_SDIO_RS9116_STATUS_SUCCESS;
}

//*****************************************************************************
//
// @fn                  int16_t rsi_sdio_init()
// @brief               Start the SDIO interface
// @param[in]           none
// @param[out]          none
// @return              errCode
// @section description
// This API initializes the Wi-Fi module Slave SDIO interface.
//
//*****************************************************************************
int32_t rsi_mcu_sdio_init(void)
{
    uint32_t ui32Status;
    uint32_t ui32BlkSize;
    uint32_t ui32FuncNum = 1;

    //
    // Configure SDIO PINs.
    //
#if defined(AM_PART_APOLLO5A) || defined(AM_PART_APOLLO5B)
    am_bsp_sdio_pins_enable(ui32SdioModule, AM_HAL_HOST_BUS_WIDTH_4);

    am_bsp_sdio_reset(ui32SdioModule);
#else
    am_bsp_sdio_pins_enable(AM_HAL_HOST_BUS_WIDTH_4);

    am_bsp_sdio_reset();
#endif

    //
    // Get the uderlying SDHC card host instance
    //
    pSdhcCardHost = am_hal_get_card_host((am_hal_host_inst_index_e)(AM_HAL_SDHC_CARD_HOST + ui32SdioModule), true);

    if (pSdhcCardHost == NULL)
    {
        am_util_debug_printf("No such card host and stop\n");
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }
    am_util_debug_printf("\n SDIO card host is found\n");

    //
    // Check if card is present
    //
    while (am_hal_card_host_find_card(pSdhcCardHost, &RS9116SdioCard) != AM_HAL_STATUS_SUCCESS)
    {
        am_util_debug_printf("No card is present now\n");
        am_util_delay_ms(1000);
        am_util_debug_printf("Checking if card is available again\n");
    }

    //
    // Initialize SDIO card
    //
    while (am_hal_card_init(&RS9116SdioCard, AM_HAL_CARD_TYPE_SDIO, NULL, AM_HAL_CARD_PWR_CTRL_SDHC_OFF) != AM_HAL_STATUS_SUCCESS)
    {
        am_util_delay_ms(1000);
        am_util_debug_printf("card init failed, try again\n");
    }

    //
    // Config SDIO card operation modes
    //
    while (am_hal_card_cfg_set(&RS9116SdioCard, AM_HAL_CARD_TYPE_SDIO,
        AM_HAL_HOST_BUS_WIDTH_4, 48000000, AM_HAL_HOST_BUS_VOLTAGE_1_8,
        AM_HAL_HOST_UHS_NONE) != AM_HAL_STATUS_SUCCESS)
    {
        am_util_delay_ms(1000);
        am_util_debug_printf("setting SDR50 failed\n");
    }

    //
    // Enable SDIO card function 1
    //
    ui32Status = am_hal_sdio_card_func_enable(&RS9116SdioCard, ui32FuncNum);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("Enable function 1 failed. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    //
    // Enable SDIO card function 1 interrupt
    //
    ui32Status = am_hal_sdio_card_func_interrupt_enable(&RS9116SdioCard, ui32FuncNum);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("Enable function 1 interrupt failed. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    //
    // Set SDIO card function 1 block size
    //
    ui32BlkSize = 256;
    ui32Status = am_hal_sdio_card_set_block_size(&RS9116SdioCard, ui32FuncNum, ui32BlkSize);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("Fail to set SDIO card function 1 block size. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    //
    // Get SDIO card function 1 block size
    //
    ui32Status = am_hal_sdio_card_get_block_size(&RS9116SdioCard, ui32FuncNum, &ui32BlkSize);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        am_util_debug_printf("Fail to check SDIO card function 1 block size. Status=0x%x\n", ui32Status);
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    sdio_init_done = 1;

    return AM_DEVICES_SDIO_RS9116_STATUS_SUCCESS;
}

//*****************************************************************************
//
// @fn                  int16_t rsi_sdio_apply_timing()
// @brief               set sdio tx/rx delay
// @param[in]           an averaged the TX/RX delay values
// @return              0 in case of success , 1 in case of failure
// @section description
// This API apply given TX/RX delay settings to target SDIO instance.
//
//*****************************************************************************
int32_t rsi_sdio_apply_timing(uint8_t ui8TxRxDelays[2])
{
    //
    // Get the uderlying SDHC card host instance
    //
    pSdhcCardHost = am_hal_get_card_host((am_hal_host_inst_index_e)(AM_HAL_SDHC_CARD_HOST + ui32SdioModule), true);
    if (pSdhcCardHost == NULL)
    {
        am_util_debug_printf("No such card host and stop\n");
        return AM_DEVICES_SDIO_RS9116_STATUS_ERROR;
    }

    am_hal_card_host_set_txrx_delay(pSdhcCardHost, ui8TxRxDelays);

    return AM_DEVICES_SDIO_RS9116_STATUS_SUCCESS;
}

//*****************************************************************************
//
// @fn          int16_t rsi_sdio_timing_scan()
// @brief       API is used to calibrate sdio tx/rx delay parameters.
// @param[in]   index to the underlying card host instances
// @param[in]   card UHS mode
// @param[in]   card bus clock speed
// @param[in]   SDIO bus width
// @param[in]   data buffer used to do the calibration
// @param[in]   SDIO card start address used to do the calibration
// @param[in]   SDIO card block number used to do the calibration
// @param[in]   SDIO card bus operation voltage
// @param[in]   the customer TX/RX delay values
// @param[in]   pointer to the User reset sdio card
// @return      0 in case of success ,*  - 1 in case of failure
// @section description
// This function get the tx rx delay setting by finding all workable TX/RX delay
// settings, then config the setting into current instance.
//
//*****************************************************************************
int32_t rsi_sdio_timing_scan(am_hal_host_inst_index_e eIndex,
                             am_hal_host_uhs_mode_e eUHSMode,
                             uint32_t ui32Clock,
                             am_hal_host_bus_width_e eBusWidth,
                             uint8_t *ui8CalibBuf,
                             uint32_t ui32StartAddr,
                             uint32_t ui32BlockCnt,
                             am_hal_host_bus_voltage_e eIoVoltage,
                             uint8_t ui8TxRxDelays[2],
                             am_hal_sdio_card_reset_func pSdioCardReset)
{
    return am_hal_sdio_card_calibrate(eIndex, eUHSMode, ui32Clock, eBusWidth,
                                      (uint8_t *)ui8CalibBuf, ui32StartAddr,
                                      ui32BlockCnt, eIoVoltage, ui8TxRxDelays, pSdioCardReset);
}

//*****************************************************************************
//
// End Doxygen group.
//! @}
//
//*****************************************************************************
