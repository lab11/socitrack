//*****************************************************************************
//
//! @file am_hal_card.c
//!
//! @brief Functions for interfacing with the card host.
//!
//! @addtogroup card_4p Card Functionality for SD/MMC/eMMC/SDIO
//! @ingroup apollo4p_hal
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "am_mcu_apollo.h"
#include "am_util_stdio.h"
#include "am_util_debug.h"
#include "am_util_delay.h"

//
// Private internal functions
//

//
//! CMD0 - go idle
//
static inline uint32_t am_hal_sdmmc_cmd0_go_idle(am_hal_card_t *pCard)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx   = MMC_CMD_GO_IDLE_STATE;
    cmd.ui32Arg  = 0x0;
    cmd.ui32RespType = MMC_RSP_NONE;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    return cmd.eError;
}

//
//! CMD1 - send operation condition
//
static inline uint32_t am_hal_sdmmc_cmd1_send_op_cond(am_hal_card_t *pCard)
{
    uint32_t ui32CardOCR = 0;
    uint8_t ui8Tries = 100;

    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_SEND_OP_COND;
    cmd.ui32Arg = SECTOR_MODE << 28;
    cmd.ui32RespType = MMC_RSP_R3;

    do
    {
        pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

        if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
        {
            ui32CardOCR = cmd.ui32Resp[0];
            cmd.ui32Arg |= ui32CardOCR;
        }

        if ( ui32CardOCR & CARD_RDY_BIT )
        {
            pCard->ui32OCR = ui32CardOCR;
            break;
        }

        am_util_delay_ms(10);
    } while (--ui8Tries != 0);

    return ui8Tries == 0x0 ? AM_HAL_CMD_ERR_TIMEOUT : cmd.eError;
}

//
//! CMD2 - send card identification
//
static inline uint32_t am_hal_sdmmc_cmd2_send_cid(am_hal_card_t *pCard)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx   = MMC_CMD_ALL_SEND_CID;
    cmd.ui32Arg  = 0x0;
    cmd.ui32RespType = MMC_RSP_R2;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
    {
        memcpy((void *)pCard->ui32CID, (void *)&cmd.ui32Resp[0], 16);
        pCard->bCidValid = 1;
    }

    return cmd.eError;
}

//
//! CMD3 - set the relative card address
//
static inline uint32_t am_hal_sdmmc_cmd3_set_rca(am_hal_card_t *pCard, uint32_t ui32RCA)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx   = MMC_CMD_SET_RELATIVE_ADDR;
    cmd.ui32Arg  = ui32RCA << 16;

    if ( (pCard->eType == AM_HAL_CARD_TYPE_EMMC) || (pCard->eType == AM_HAL_CARD_TYPE_MMC) )
    {
        cmd.ui32RespType = MMC_RSP_R1;
    }
    else
    {
        cmd.ui32RespType = MMC_RSP_R6;
    }

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
    {
        if ( (pCard->eType == AM_HAL_CARD_TYPE_EMMC) || (pCard->eType == AM_HAL_CARD_TYPE_MMC) )
        {
            pCard->ui32RCA = ui32RCA;
        }
        else
        {
            pCard->ui32RCA = cmd.ui32Resp[0] >> 16;
            AM_HAL_CARD_DEBUG("Get SD card responds with new RCA:0x%x\n", pCard->ui32RCA);
        }
    }

    return cmd.eError;
}

//
//! CMD5 - Sleep/Awake
//
static uint32_t am_hal_sdmmc_cmd5(am_hal_card_t *pCard, bool sleep)
{
    uint32_t ui32Status;
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx   = MMC_CMD_SLEEP_AWAKE;
    cmd.ui32Arg  = pCard->ui32RCA << 16 | sleep << 15;
    cmd.ui32RespType = MMC_RSP_R1b;
    cmd.bCheckBusyCmd = true;

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
    {
        if ( sleep == true )
        {
            pCard->eState = AM_HAL_CARD_STATE_SLP;
        }
        else
        {
            pCard->eState = AM_HAL_CARD_STATE_STDY;
        }
    }

    if ((ui32Status = pHost->ops->card_busy(pHost->pHandle, 20)) != AM_HAL_STATUS_SUCCESS)
    {
        return ui32Status;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! CMD7 - select the card
//

static uint32_t am_hal_sdmmc_cmd7_card_select(am_hal_card_t *pCard)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx   = MMC_CMD_SELECT_CARD;
    cmd.ui32Arg  = pCard->ui32RCA << 16;

    //
    // RCA of zero is not valid for a card select, so this
    // is a card deselect which requires no response
    //
    if ( pCard->ui32RCA == 0x0 )
    {
        cmd.ui32RespType = MMC_RSP_NONE;
    }
    else
    {
        cmd.ui32RespType = MMC_RSP_R1;
    }

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
    {
        //
        // For a card deselect, the state is stand-by-state
        //
        if ( pCard->ui32RCA == 0x0 )
        {
            pCard->eState = AM_HAL_CARD_STATE_STDY;
        }
        //
        // for a card select, the state is the transfer state
        //
        else
        {
            pCard->eState = AM_HAL_CARD_STATE_TRANS;
        }
    }

    return cmd.eError;
}

//
//! CMD7 - select/deselect the card
//
static uint32_t am_hal_sdmmc_cmd7_card_deselect(am_hal_card_t *pCard)
{
    uint32_t ui32RCA = pCard->ui32RCA;
    uint32_t ui32Status = AM_HAL_STATUS_SUCCESS;

    pCard->ui32RCA = 0;
    if ( AM_HAL_CMD_ERR_NONE != am_hal_sdmmc_cmd7_card_select(pCard) )
    {
        AM_HAL_CARD_DEBUG("sleep - am_hal_sdmmc_cmd7_card_select Failed\n");
        ui32Status = AM_HAL_STATUS_FAIL;
    }
    pCard->ui32RCA = ui32RCA;

    return ui32Status;
}

//
//! CMD8 - get the 512 bytes ext csd
//
static uint32_t am_hal_sdmmc_cmd8_send_ext_csd(am_hal_card_t *pCard)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0x0, sizeof(cmd));
    memset(&cmd_data, 0x0, sizeof(cmd_data));

    cmd.ui8Idx   = MMC_CMD_SEND_EXT_CSD;
    cmd.ui32Arg  = pCard->ui32RCA << 16;
    cmd.ui32RespType = MMC_RSP_R1;

    cmd_data.ui8IovCnt = 0;
    cmd_data.pui8Buf = (uint8_t *)pCard->ui32ExtCSD;
    cmd_data.ui32BlkCnt = 1;
    cmd_data.ui32BlkSize = 512;
    cmd_data.dir = AM_HAL_DATA_DIR_READ;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);

    if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
    {
        pCard->bExtCsdValid = 1;
    }

    return cmd.eError;
}

//
//! CMD9 - get the CSD
//
static inline uint32_t am_hal_sdmmc_cmd9_send_csd(am_hal_card_t *pCard)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx   = MMC_CMD_SEND_CSD;
    cmd.ui32Arg  = pCard->ui32RCA << 16;
    cmd.ui32RespType = MMC_RSP_R2;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
    {
        memcpy((void *)pCard->ui32CSD, (void *)&cmd.ui32Resp[0], 16);
        pCard->bCsdValid = 1;
    }

    return cmd.eError;
}

//
//! CMD5 for SDIO card - send operation condition
//
static inline uint32_t am_hal_sdio_cmd5_io_send_op_cond(am_hal_card_t *pCard, uint32_t ui32OCR)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;
    uint32_t ui32CardOCR = 0;
    uint8_t ui8Tries = 100;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = SDIO_CMD_IO_SEND_OP_COND;
    cmd.ui32Arg = ui32OCR & 0x01FFFF00;
    cmd.ui32RespType = MMC_RSP_R4;

    do
    {
        pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

        if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
        {
            ui32CardOCR = cmd.ui32Resp[0];
        }

        if ( ui32CardOCR & CARD_RDY_BIT )
        {
            pCard->ui32OCR = ui32CardOCR;
            pCard->ui32FuncNum = (ui32CardOCR >> 28) & 0x7;
            break;
        }

        am_util_delay_ms(10);
    } while (--ui8Tries != 0);

    return ui8Tries == 0x0 ? AM_HAL_CMD_ERR_TIMEOUT : cmd.eError;
}

//
//! CMD8 for SD card and SDIO card - send interface condition
//
static inline uint32_t am_hal_sdio_cmd8_send_if_cond(am_hal_card_t *pCard, uint32_t ui32OCR)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = SDIO_CMD_SEND_IF_COND;
    cmd.ui32RespType = MMC_RSP_R7;
    cmd.ui32Arg = ui32OCR;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
    {
        if ( (cmd.ui32Resp[0] & 0xFF) != 0xAA )
        {
            AM_HAL_CARD_DEBUG("CMD8 response ERROR!\n");
            return AM_HAL_CMD_ERR_INVALID_RESPONSE;
        }

        if ( (cmd.ui32Resp[0] & 0x0F00) == SD_CARD_2P7_3P6_VOLTAGE_SUPPLY )
        {
            AM_HAL_CARD_DEBUG("Card support 2.7 ~ 3.6 V\n");
        }
        else
        {
            AM_HAL_CARD_DEBUG("Card voltage accepted result:0x%x\n", cmd.ui32Resp[0]);
        }
    }
    else if ( cmd.eError == AM_HAL_CMD_ERR_TIMEOUT )
    {
        AM_HAL_CARD_DEBUG("Card CMD8 no resp, voltage mismatch or Ver1.X SD or not SD\r\n");
    }
    else if ( cmd.eError == AM_HAL_CMD_ERR_CRC )
    {
        AM_HAL_CARD_DEBUG("Card CMD8 crc err\r\n");
    }

    return cmd.eError;
}

//
//! CMD52 for SDIO card -  read/write card register and CIS table
//
static inline uint32_t am_hal_sdio_cmd52_io_rw_direct(am_hal_card_t *pCard,
                                                      uint32_t ui32Func,
                                                      uint32_t ui32Addr,
                                                      uint8_t ui8WrData,
                                                      uint8_t *ui8RdData,
                                                      bool bRead)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = SDIO_CMD_IO_RW_DIRECT;
    cmd.ui32RespType = MMC_RSP_R5;

    if ( bRead )
    {
        cmd.ui32Arg = (ui32Func << 28) | (ui32Addr << 9);
    }
    else
    {
        cmd.ui32Arg = 0x80000000 | (ui32Func << 28) | (ui32Addr << 9) | ui8WrData;
    }

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SDIO card CMD52 ERROR!\n");
    }

    if ( ui8RdData )
    {
        *ui8RdData = cmd.ui32Resp[0] & 0xFF;
    }

    return cmd.eError;
}

//
//! CMD53 for SDIO card - read/write data from/to card's SQU memory
//
static inline uint32_t am_hal_sdio_cmd53_io_rw_extended(am_hal_card_t *pCard,
                                                        uint32_t ui32Func,
                                                        uint32_t ui32Addr,
                                                        uint8_t *pui8Buf,
                                                        uint32_t ui32BlkCnt,
                                                        uint32_t ui32BlkSize,
                                                        bool bIncrAddr,
                                                        bool bRead,
                                                        bool bASync)
{
    uint32_t ui32Status;

    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.bASync = bASync;
    cmd.ui8Idx = SDIO_CMD_IO_RW_EXTENDED;
    cmd.ui32RespType = MMC_RSP_R5;
    cmd.ui32Arg = (ui32Func << 28) | (ui32Addr << 9);
    cmd.ui32Arg |= bRead ? 0x0 : 0x80000000;
    cmd.ui32Arg |= bIncrAddr ? 0x04000000 : 0x0;

    if ( ui32BlkCnt == 0 && ui32BlkSize <= 512 )
    {
        cmd.ui32Arg |= (ui32BlkSize == 512) ? 0 : ui32BlkSize;
    }
    else
    {
        cmd.ui32Arg |= 0x08000000 | ui32BlkCnt;
    }

    memset((void *)&cmd_data, 0x0, sizeof(cmd_data));
    cmd_data.ui8IovCnt = 0;
    cmd_data.pui8Buf = pui8Buf;
    cmd_data.ui32BlkCnt = ui32BlkCnt;
    cmd_data.ui32BlkSize = ui32BlkSize;
    cmd_data.dir = bRead ? AM_HAL_DATA_DIR_READ : AM_HAL_DATA_DIR_WRITE;

    if ( cmd.bASync )
    {
        pHost->AsyncCmd = cmd;
        pHost->AsyncCmdData = cmd_data;
    }

    ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);

    return ui32Status;
}

//
//! CMD11 - Switch SD Card signal voltage to 1.8 V
//
static inline uint32_t am_hal_sdcard_cmd11_voltage_switch(am_hal_card_t *pCard)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx   = SDIO_CMD_VOLTAGE_SWITCH;
    cmd.ui32Arg  = 0;
    cmd.ui32RespType = MMC_RSP_R1;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    return cmd.eError;
}

//
//! ACMD6 for SD card - set bus width
//
static inline uint32_t am_hal_sdcard_acmd6_set_bus_width(am_hal_card_t *pCard, am_hal_host_bus_width_e eBusWidth)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_APP_CMD;
    cmd.ui32Arg = pCard->ui32RCA << 16;
    cmd.ui32RespType = MMC_RSP_R1;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD card CMD55 response ERROR!\n");
        return cmd.eError;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_SWITCH;

    if ( eBusWidth == AM_HAL_HOST_BUS_WIDTH_4 )
    {
        cmd.ui32Arg = 2;
    }
    else
    {
        cmd.ui32Arg = 0;
    }

    cmd.ui32RespType = MMC_RSP_R1;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError != AM_HAL_CMD_ERR_NONE )
    {
        if ( cmd.eError != AM_HAL_CMD_ERR_NONE && cmd.eError != AM_HAL_CMD_ERR_CRC )
        {
            AM_HAL_CARD_DEBUG("SD card send CMD6 err:%d\r\n", cmd.eError);
            return cmd.eError;
        }
    }

    return cmd.eError;
}

//
//! CMD41 for SD card - send host capacity support information
//
static inline uint32_t am_hal_sdcard_acmd41_send_op_cond(am_hal_card_t *pCard, uint32_t ui32Ocr, uint32_t *ui32Resp)
{
    am_hal_card_cmd_t cmd;
    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_APP_CMD;
    cmd.ui32Arg = 0;
    cmd.ui32RespType = MMC_RSP_R1;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD card CMD55 response ERROR!\n");
        return cmd.eError;
    }

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = SD_CMD_SEND_OP_COND;
    cmd.ui32Arg = ui32Ocr;
    cmd.ui32RespType = MMC_RSP_R3;

    pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL);

    if ( cmd.eError != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD card send CMD41 err:%d\r\n", cmd.eError);
        return cmd.eError;
    }

    *ui32Resp = cmd.ui32Resp[0];

    return cmd.eError;
}

//
//! Read SDIO card common control registers(CCCR)
//
static uint32_t
am_hal_sdio_card_read_cccr(am_hal_card_t *pCard)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 0;
    uint8_t  ui8CccrReg;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_CCCR_REV, 0, &ui8CccrReg, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO CCCR revision register \n");
        return ui32Status;
    }
    pCard->cccr.ui8CccrRev = ui8CccrReg & 0x0F;

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_SD_REV, 0, &ui8CccrReg, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SD Spec revision register \n");
        return ui32Status;
    }
    pCard->cccr.ui8SdRev = ui8CccrReg & 0x0F;

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_CARD_CAPAB, 0, &ui8CccrReg, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO CCCR card capability register \n");
        return ui32Status;
    }
    if ( ui8CccrReg & SDIO_CCCR_CAP_SMB )
    {
        pCard->cccr.bMultiBlock = 1;
    }
    if ( ui8CccrReg & SDIO_CCCR_CAP_LSC )
    {
        pCard->cccr.bFullSpeed = 1;
    }
    if ( ui8CccrReg & SDIO_CCCR_CAP_4BLS )
    {
        pCard->cccr.bWideBus = 1;
    }

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_PWR_CNTRL, 0, &ui8CccrReg, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO CCCR power control register \n");
        return ui32Status;
    }
    if ( ui8CccrReg & SDIO_POWER_SMPC )
    {
        pCard->cccr.bHighPower = 1;
    }

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_BUS_SPEED_SELECT, 0, &ui8CccrReg, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO CCCR bus speed select register \n");
        return ui32Status;
    }
    if ( ui8CccrReg & SDIO_SPEED_SHS )
    {
        pCard->cccr.bHighSpeed = 1;
    }

    AM_HAL_CARD_DEBUG("SDIO Card cccr.ui8CccrRev  = 0x%x \n", pCard->cccr.ui8CccrRev);
    AM_HAL_CARD_DEBUG("SDIO Card cccr.ui8SdRev    = 0x%x \n", pCard->cccr.ui8SdRev);
    AM_HAL_CARD_DEBUG("SDIO Card cccr.bMultiBlock = 0x%x \n", pCard->cccr.bMultiBlock);
    AM_HAL_CARD_DEBUG("SDIO Card cccr.bFullSpeed  = 0x%x \n", pCard->cccr.bFullSpeed);
    AM_HAL_CARD_DEBUG("SDIO Card cccr.bWideBus    = 0x%x \n", pCard->cccr.bWideBus);
    AM_HAL_CARD_DEBUG("SDIO Card cccr.bHighPower  = 0x%x \n", pCard->cccr.bHighPower);
    AM_HAL_CARD_DEBUG("SDIO Card cccr.bHighSpeed  = 0x%x \n", pCard->cccr.bHighSpeed);

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Read SDIO card information structure (CIS)
//
static uint32_t
am_hal_sdio_card_read_cis(am_hal_card_t *pCard)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 0;
    uint32_t ui32CisAddr = 0;
    uint32_t ui32LoopCnt = 512;
    uint8_t  ui8RdData[4];
    uint8_t  ui8TupleCode, ui8TupleLink;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    for ( uint8_t i = 0; i < 3; i++ )
    {
        ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_CIS_PTR1 + i, 0, &ui8RdData[i], true);
        if ( ui32Status != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Fail to read SDIO common CIS pointer register \n");
            return ui32Status;
        }
        ui32CisAddr |= ui8RdData[i] << ( i * 8);
    }

    do
    {
        ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, ui32CisAddr, 0, &ui8TupleCode, true);
        if ( ui32Status != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Fail to read SDIO tuple code \n");
            return ui32Status;
        }

        if ( (ui8TupleCode == SDIO_CIS_TPL_NULL) || (ui8TupleCode == SDIO_CIS_TPL_END) )
        {
            AM_HAL_CARD_DEBUG("CIS tuple code is NULL or END \n\n");
            break;
        }

        ui32CisAddr += 1;
        ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, ui32CisAddr, 0, &ui8TupleLink, true);
        if ( ui32Status != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Fail to read SDIO tuple link register \n");
            return ui32Status;
        }

        if ( ui8TupleCode == SDIO_CIS_TPL_MANFID )
        {
            for ( uint8_t i = 0; i < 4; i++ )
            {
                ui32CisAddr += 1;
                ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, ui32CisAddr, 0, &ui8RdData[i], true);
                if ( ui32Status != AM_HAL_STATUS_SUCCESS )
                {
                    AM_HAL_CARD_DEBUG("Fail to read SDIO tuple MID register \n");
                    return ui32Status;
                }
            }
            pCard->cis.ui32Vendor = (ui8RdData[1] << 8) | ui8RdData[0];
            pCard->cis.ui32Device = (ui8RdData[3] << 8) | ui8RdData[2];

        }
        else if ( ui8TupleCode == SDIO_CIS_TPL_FUNCE )
        {
            ui32CisAddr += 1; // Skip TPLFE_TYPE register address
            for ( uint8_t i = 0; i < 3; i++ )
            {
                ui32CisAddr += 1;
                ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, ui32CisAddr, 0, &ui8RdData[i], true);
                if ( ui32Status != AM_HAL_STATUS_SUCCESS )
                {
                    AM_HAL_CARD_DEBUG("Fail to read SDIO tuple FUNCE register \n");
                    return ui32Status;
                }
            }
            pCard->cis.ui32BlkSize = (ui8RdData[1] << 8) | ui8RdData[0];
            pCard->cis.ui32MaxSpeed = ui8RdData[2];
        }
        ui32CisAddr += (ui8TupleLink + 1);
    } while (--ui32LoopCnt != 0 );

    AM_HAL_CARD_DEBUG("SDIO Card cis.ui32Vendor   = 0x%x \n", pCard->cis.ui32Vendor);
    AM_HAL_CARD_DEBUG("SDIO Card cis.ui32Device   = 0x%x \n", pCard->cis.ui32Device);
    AM_HAL_CARD_DEBUG("SDIO Card cis.ui32BlkSize  = 0x%x \n", pCard->cis.ui32BlkSize);
    AM_HAL_CARD_DEBUG("SDIO Card cis.ui32MaxSpeed = 0x%x \n", pCard->cis.ui32MaxSpeed);

    return ui32LoopCnt == 0 ? AM_HAL_STATUS_FAIL : AM_HAL_STATUS_SUCCESS;
}

//
//! Set SDIO card bus width
//
static uint32_t
am_hal_sdio_card_set_bus_width(am_hal_card_t *pCard, am_hal_host_bus_width_e eBusWidth)
{
    uint32_t ui32Status;
    uint32_t ui32Func = 0;
    am_hal_card_host_t *pHost = pCard->pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    switch ( eBusWidth )
    {
        case AM_HAL_HOST_BUS_WIDTH_1:
            ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_BUS_CNTRL, 0, NULL, false);
            if ( ui32Status != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Fail to set SDIO card into 1-bit mode. ERROR!\n");
                return ui32Status;
            }
            else
            {
                AM_HAL_CARD_DEBUG("SDIO card is set into 1-bit mode\n");
            }
            break;

        case AM_HAL_HOST_BUS_WIDTH_4:
            ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_BUS_CNTRL, 2, NULL, false);
            if ( ui32Status != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Fail to set SDIO card into 4-bit mode. ERROR!\n");
                return ui32Status;
            }
            else
            {
                AM_HAL_CARD_DEBUG("SDIO card is set into 4-bit mode\n");
            }
            break;

        case AM_HAL_HOST_BUS_WIDTH_8:
            AM_HAL_CARD_DEBUG("8 bit width is not supported for SDIO card\n");
            return AM_HAL_STATUS_INVALID_OPERATION;

        default:
            break;
    }

    if ( pHost->ops->set_bus_width(pHost->pHandle, eBusWidth) != AM_HAL_STATUS_SUCCESS )
    {
        return AM_HAL_STATUS_FAIL;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Set SDIO card clock speed
//
static uint32_t
am_hal_sdio_card_set_speed(am_hal_card_t *pCard, uint32_t ui32Clock)
{
    uint32_t ui32Status = 0;
    uint32_t ui32Func = 0;
    uint8_t  ui8SpeedReg = 0;

    am_hal_card_host_t *pHost = pCard->pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( ui32Clock > SDIO_CARD_MAX_SPEED_LIMIT)
    {
        AM_HAL_CARD_DEBUG("Clock speed setting is out of range\n");
        return AM_HAL_STATUS_OUT_OF_RANGE;
    }
    else if (ui32Clock > SDIO_CARD_FULL_SPEED_LIMIT)
    {
        ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_BUS_SPEED_SELECT, 0, &ui8SpeedReg, true);
        if ( ui32Status != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Fail to read SDIO card bus speed register\n");
            return ui32Status;
        }

        if ( ui8SpeedReg & SDIO_HIGHSPEED_SUPPORT )
        {
            //
            // Set SDIO card into High Speed mode. Write 1 to SDIO_BUS_SPEED_SELECT register.
            //
            ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_BUS_SPEED_SELECT, 1, NULL, false);
            if ( ui32Status != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Fail to set SDIO card into high speed mode\n");
                return ui32Status;
            }

            ui8SpeedReg = 0;
            ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_BUS_SPEED_SELECT, 0, &ui8SpeedReg, true);
            if ( ui32Status != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Fail to read SDIO card bus speed register\n");
                return ui32Status;
            }

            if ( ui8SpeedReg & SDIO_HIGHSPEED_ENABLED )
            {
                AM_HAL_CARD_DEBUG("SDIO card is set into high speed mode\n");
            }
            else
            {
                AM_HAL_CARD_DEBUG("SDIO card high speed mode check failed\n");
                return AM_HAL_STATUS_FAIL;
            }
        }
        else
        {
                AM_HAL_CARD_DEBUG("This SDIO card does not support high speed mode\n");
                return AM_HAL_STATUS_INVALID_OPERATION;
        }
    }
    else
    {
        //
        // Set SDIO card into Full Speed mode. Write 0 to SDIO_BUS_SPEED_SELECT register.
        //
        ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_BUS_SPEED_SELECT, 0, NULL, false);
        if ( ui32Status != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Fail to set SDIO card into full speed mode\n");
            return ui32Status;
        }

        ui8SpeedReg = 0;
        ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, SDIO_CCCR_BUS_SPEED_SELECT, 0, &ui8SpeedReg, true);
        if ( ui32Status != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Fail to read SDIO card bus speed register\n");
            return ui32Status;
        }

        if ( ((ui8SpeedReg >> 1) & 0x1) == SDIO_FULLSPEED_ENABLED )
        {
            AM_HAL_CARD_DEBUG("SDIO card is set into full speed mode\n");
        }
        else
        {
            AM_HAL_CARD_DEBUG("SDIO card full speed mode check failed\n");
            return AM_HAL_STATUS_FAIL;
        }
    }

    //
    // Set SDIO host controller clock speed.
    //
    if ( pHost->ops->set_bus_clock(pHost->pHandle, ui32Clock) != AM_HAL_STATUS_SUCCESS )
    {
        return AM_HAL_STATUS_FAIL;
    }
    else
    {
        AM_HAL_CARD_DEBUG("SDIO clock frequency is %dHz\n", ui32Clock);
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Enable SDIO card function
//
uint32_t
am_hal_sdio_card_func_enable(am_hal_card_t *pCard, uint32_t ui32Func)
{
    uint32_t ui32Status;
    uint8_t  ui8Tries = 0;
    uint8_t  ui8RegData = 0;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( (ui32Func < 1) || (ui32Func > pCard->ui32FuncNum) )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_IO_ENABLE, 0, &ui8RegData, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO card IO Enable register\n");
        return ui32Status;
    }
    else
    {
        AM_HAL_CARD_DEBUG("Register SDIO_CCCR_IO_ENABLE = 0x%x \n", ui8RegData);
    }

    ui8RegData |= (1 << ui32Func);
    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_IO_ENABLE, ui8RegData, NULL, false);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to write SDIO card IO Enable register\n");
        return ui32Status;
    }

    do
    {
        ui8Tries++;
        ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_IO_RDY, 0, &ui8RegData, true);
        if ( ui32Status != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Fail to read SDIO card IO Ready register. Tried %d times\n", ui8Tries);
            return ui32Status;
        }
        if ( ui8RegData & (1 << ui32Func) )
        {
            AM_HAL_CARD_DEBUG("SDIO card IO Function %d is ready\n", ui32Func);
            break;
        }
        am_util_delay_ms(10);
    } while (ui8Tries < 100);

    return (ui8Tries >= 100) ? AM_HAL_STATUS_TIMEOUT : AM_HAL_STATUS_SUCCESS;
}

//
//! Disable SDIO card function
//
uint32_t
am_hal_sdio_card_func_disable(am_hal_card_t *pCard, uint32_t ui32Func)
{
    uint32_t ui32Status;
    uint8_t  ui8RegData = 0;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( (ui32Func < 1) || (ui32Func > pCard->ui32FuncNum) )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_IO_ENABLE, 0, &ui8RegData, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO card IO Enable register\n");
        return ui32Status;
    }
    else
    {
        AM_HAL_CARD_DEBUG("Register SDIO_CCCR_IO_ENABLE = 0x%x \n", ui8RegData);
    }

    ui8RegData &= ~(1 << ui32Func);
    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_IO_ENABLE, ui8RegData, NULL, false);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to Disable SDIO card IO function\n");
        return ui32Status;
    }
    else
    {
        AM_HAL_CARD_DEBUG("SDIO card IO function %d is disabled\n", ui32Func);
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Enable SDIO card function's interrupt
//
uint32_t
am_hal_sdio_card_func_interrupt_enable(am_hal_card_t *pCard, uint32_t ui32Func)
{
    uint32_t ui32Status;
    uint8_t  ui8RegData = 0;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( (ui32Func < 1) || (ui32Func > pCard->ui32FuncNum) )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_INT_ENABLE, 0, &ui8RegData, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO card IO Interrupt Enable register\n");
        return ui32Status;
    }
    else
    {
        AM_HAL_CARD_DEBUG("Register SDIO_CCCR_INT_ENABLE = 0x%x \n", ui8RegData);
    }

    ui8RegData |= ((1 << ui32Func) | 0x1);
    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_INT_ENABLE, ui8RegData, NULL, false);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to Enable SDIO card IO function's interrupt\n");
        return ui32Status;
    }
    else
    {
        AM_HAL_CARD_DEBUG("SDIO card IO function %d interrupt is enabled\n", ui32Func);
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Disable SDIO card function's interrupt
//
uint32_t
am_hal_sdio_card_func_interrupt_disable(am_hal_card_t *pCard, uint32_t ui32Func)
{
    uint32_t ui32Status;
    uint8_t  ui8RegData = 0;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( (ui32Func < 1) || (ui32Func > pCard->ui32FuncNum) )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_INT_ENABLE, 0, &ui8RegData, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO card IO Interrupt Enable register\n");
        return ui32Status;
    }
    else
    {
        AM_HAL_CARD_DEBUG("Register SDIO_CCCR_INT_ENABLE = 0x%x \n", ui8RegData);
    }

    ui8RegData &= ~(1 << ui32Func);
    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, SDIO_CCCR_INT_ENABLE, ui8RegData, NULL, false);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to disable SDIO card IO function's interrupt\n");
        return ui32Status;
    }
    else
    {
        AM_HAL_CARD_DEBUG("SDIO card IO function %d interrupt is disabled\n", ui32Func);
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Get SDIO card function block size
//
uint32_t
am_hal_sdio_card_get_block_size(am_hal_card_t *pCard, uint32_t ui32Func, uint32_t *pui32BlkSize)
{
    uint32_t ui32Status;
    uint32_t ui32RegAddr = 0;
    uint8_t  ui8RegData = 0;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( ui32Func > pCard->ui32FuncNum )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    ui32RegAddr = SDIO_CCCR_FN_BLK_SIZE1 + (ui32Func << 8);
    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, ui32RegAddr, 0, &ui8RegData, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO card function %d block size low byte\n", ui32Func);
        return ui32Status;
    }
    else
    {
        *pui32BlkSize = ui8RegData;
    }

    ui32RegAddr = SDIO_CCCR_FN_BLK_SIZE2 + (ui32Func << 8);
    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, ui32RegAddr, 0, &ui8RegData, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read SDIO card function %d block size high byte\n", ui32Func);
        return ui32Status;
    }
    else
    {
        *pui32BlkSize |= (ui8RegData << 8);
        AM_HAL_CARD_DEBUG("SDIO card function %d block size = %d\n", ui32Func, *pui32BlkSize);
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Set SDIO card function block size
//
uint32_t
am_hal_sdio_card_set_block_size(am_hal_card_t *pCard, uint32_t ui32Func, uint32_t ui32BlkSize)
{
    uint32_t ui32Status;
    uint32_t ui32RegAddr = 0;
    uint8_t  ui8WrData = 0;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( ui32Func > pCard->ui32FuncNum )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    ui32RegAddr = SDIO_CCCR_FN_BLK_SIZE1 + (ui32Func << 8);
    ui8WrData = ui32BlkSize & 0xFF;
    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, ui32RegAddr, ui8WrData, NULL, false);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to write SDIO card function %d block size low byte\n", ui32Func);
        return ui32Status;
    }

    ui32RegAddr = SDIO_CCCR_FN_BLK_SIZE2 + (ui32Func << 8);
    ui8WrData = (ui32BlkSize & 0xFF00) >> 8;
    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, 0, ui32RegAddr, ui8WrData, NULL, false);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to write SDIO card function %d block size high byte\n", ui32Func);
        return ui32Status;
    }

    AM_HAL_CARD_DEBUG("SDIO card function %d block size is set into %d\n", ui32Func, ui32BlkSize);

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Set SD card bus width
//
static uint32_t
am_hal_sd_card_set_bus_width(am_hal_card_t *pCard, am_hal_host_bus_width_e eBusWidth)
{
    uint32_t ui32Status;

    am_hal_card_host_t *pHost = pCard->pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    ui32Status = am_hal_sdcard_acmd6_set_bus_width(pCard, eBusWidth);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    if ( pHost->ops->set_bus_width(pHost->pHandle, eBusWidth) != AM_HAL_STATUS_SUCCESS )
    {
        return AM_HAL_STATUS_FAIL;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Check SD card speed with CMD6
//
static uint32_t
am_hal_sd_card_check_speed(am_hal_card_t *pCard)
{
    am_hal_card_host_t *pHost;
    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;

    uint32_t ui32Status = 0;
    uint8_t ui8Resp[64] = {0};

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    pHost = pCard->pHost;

    memset((void *)&cmd, 0x0, sizeof(cmd));
    memset((void *)&cmd_data, 0x0, sizeof(cmd_data));

    cmd.ui8Idx = MMC_CMD_SWITCH;
    cmd.ui32Arg = SD_CARD_CHECK_FUNCTION | SD_CARD_SWITCH_ARGUMENT | SD_CARD_CHECK_ALL_SPEED;
    cmd.ui32RespType = MMC_RSP_R1;
    cmd.bASync = 0;
    cmd.bAutoCMD23 = false;

    cmd_data.ui8IovCnt = 0;
    cmd_data.pui8Buf = ui8Resp;
    cmd_data.ui32BlkCnt = 1;
    cmd_data.ui32BlkSize = 64;
    cmd_data.dir = AM_HAL_DATA_DIR_READ;

    ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);
    if ( (ui32Status & 0xFFFF) != AM_HAL_CMD_ERR_NONE)
    {
        return ui32Status;
    }

    if ( pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_1_8_V )
    {
        if ( ui8Resp[13] & ( 1 << SD_CARD_SDR12 ) )
        {
            pCard->sdcard.bSupportSDR12 = 1;
            AM_HAL_CARD_DEBUG("Card support SDR12 speed\n");
        }
        else
        {
            pCard->sdcard.bSupportSDR12 = 0;
            AM_HAL_CARD_DEBUG("Card not support SDR12 speed\n");
        }

        if ( ui8Resp[13] & ( 1 << SD_CARD_SDR25 ) )
        {
            pCard->sdcard.bSupportSDR25 = 1;
            AM_HAL_CARD_DEBUG("Card support SDR25 speed\n");
        }
        else
        {
            pCard->sdcard.bSupportSDR25 = 0;
            AM_HAL_CARD_DEBUG("Card not support SDR25 speed\n");
        }

        if ( ui8Resp[13] & ( 1 << SD_CARD_SDR50 ) )
        {
            pCard->sdcard.bSupportSDR50 = 1;
            AM_HAL_CARD_DEBUG("Card support SDR50 speed\n");
        }
        else
        {
            pCard->sdcard.bSupportSDR50 = 0;
            AM_HAL_CARD_DEBUG("Card not support SDR50 speed\n");
        }

        if ( ui8Resp[13] & ( 1 << SD_CARD_SDR104 ) )
        {
            pCard->sdcard.bSupportSDR104 = 1;
            AM_HAL_CARD_DEBUG("Card support SDR104 speed\n");
        }
        else
        {
            pCard->sdcard.bSupportSDR104 = 0;
            AM_HAL_CARD_DEBUG("Card not support SDR104 speed\n");
        }

        if ( ui8Resp[13] & ( 1 << SD_CARD_DDR50 ) )
        {
            pCard->sdcard.bSupportDDR50 = 1;
            AM_HAL_CARD_DEBUG("Card support DDR50 speed\n");
        }
        else
        {
            pCard->sdcard.bSupportDDR50 = 0;
            AM_HAL_CARD_DEBUG("Card not support DDR50 speed\n");
        }

    }
    else
    {
        if ( ui8Resp[13] & 0x1 )
        {
            pCard->sdcard.bSupportDefaultSpeed = 1;
            AM_HAL_CARD_DEBUG("Card support Default speed\n");
        }
        else
        {
            pCard->sdcard.bSupportDefaultSpeed = 0;
            AM_HAL_CARD_DEBUG("Card not support Default speed\n");
        }

        if ( ui8Resp[13] & 0x2 )
        {
            pCard->sdcard.bSupportHighSpeed = 1;
            AM_HAL_CARD_DEBUG("Card support High speed\n");
        }
        else
        {
            pCard->sdcard.bSupportHighSpeed = 0;
            AM_HAL_CARD_DEBUG("Card not support High speed\n");
        }
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Set SD card clock speed
//
static uint32_t
am_hal_sd_card_set_speed(am_hal_card_t *pCard, uint32_t ui32Clock)
{
    uint32_t ui32Status = 0;
    uint32_t ui32Mode = 0;
    uint8_t ui8Resp[64] = {0};
    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;

    am_hal_card_host_t *pHost = pCard->pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( am_hal_sd_card_check_speed(pCard) != AM_HAL_STATUS_SUCCESS )
    {
        return AM_HAL_STATUS_FAIL;
    }

    if ( pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_3_3_V )
    {
        if ( (ui32Clock > 25000000) && (pCard->sdcard.bSupportHighSpeed == 0) )
        {
            AM_HAL_CARD_DEBUG("Setting high speed failed, Card not support High speed\n");
            return AM_HAL_STATUS_FAIL;
        }
        else if ( ui32Clock > 25000000 )
        {
            ui32Mode |= SD_CARD_HIGH_SPEED;
        }
        else
        {
            ui32Mode |= SD_CARD_DEFAULT_SPEED;
        }
    }
    else
    {
        ui32Mode = pCard->cfg.eUHSMode;
        if (((pCard->cfg.eUHSMode == AM_HAL_HOST_UHS_SDR104) &&
            (ui32Clock > MMC_HS_MAX_SPEED_LIMIT) &&
            (ui32Clock < MMC_HS200_MAX_SPEED_LIMIT)))
        {
            ui32Mode = AM_HAL_HOST_UHS_SDR50;
        }

        if (((pCard->cfg.eUHSMode == AM_HAL_HOST_UHS_SDR104) && (pCard->sdcard.bSupportSDR104 == false)) ||
            ((pCard->cfg.eUHSMode == AM_HAL_HOST_UHS_DDR50) && (pCard->sdcard.bSupportDDR50 == false))
           )
           {
                AM_HAL_CARD_DEBUG(" Card not support USH mode: %x\n", pCard->cfg.eUHSMode);
                return AM_HAL_STATUS_FAIL;
           }
    }

    //
    // Set bus speed used CMD6
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    memset((void *)&cmd_data, 0x0, sizeof(cmd_data));

    cmd.ui8Idx = MMC_CMD_SWITCH;
    cmd.ui32Arg = SD_CARD_SWITCH_FUNCTION | SD_CARD_SWITCH_ARGUMENT | ui32Mode;
    cmd.ui32RespType = MMC_RSP_R1;
    cmd.bASync = 0;
    cmd.bAutoCMD23 =  false;

    cmd_data.ui8IovCnt = 0;
    cmd_data.pui8Buf = ui8Resp;
    cmd_data.ui32BlkCnt = 1;
    cmd_data.ui32BlkSize = 64;
    cmd_data.dir = AM_HAL_DATA_DIR_READ;

    ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);
    if ( (ui32Status & 0xFFFF) != AM_HAL_CMD_ERR_NONE)
    {
        return ui32Status;
    }

    uint8_t ui32AccessMode = ui8Resp[16] & 0xF;

    if ( pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_3_3_V )
    {
        if ( ui32AccessMode && ( ui32Clock > 25000000 ) )
        {
            if ( ui32AccessMode == SD_CARD_HIGH_SPEED )
            {
                AM_HAL_CARD_DEBUG("Succeed on Switching High-Speed\n");
                pCard->sdcard.bHighSpeed = 1;
            }
            else
            {
                AM_HAL_CARD_DEBUG("Failed on Switching High-Speed\n"); // Quirk
                return AM_HAL_STATUS_FAIL;
            }
        }
        else
        {
            if ( ui32AccessMode == SD_CARD_DEFAULT_SPEED )
            {
                AM_HAL_CARD_DEBUG("Succeed on Switching Default-Speed\n");
                pCard->sdcard.bHighSpeed = 0;
            }
            else
            {
                AM_HAL_CARD_DEBUG("Failed on Switching Default-Speed\n");
                return AM_HAL_STATUS_FAIL;
            }
        }
    }
    else
    {
        if ( ui32AccessMode == ui32Mode )
        {
            AM_HAL_CARD_DEBUG("Succeed on Switching Speed: %x\n", ui32Mode);
        }
        else
        {
            AM_HAL_CARD_DEBUG("Failed on Switching High-Speed\n"); // Quirk
            return AM_HAL_STATUS_FAIL;
        }
    }

    //
    // Set SDIO host controller clock speed.
    //
    if ( pHost->ops->set_bus_clock(pHost->pHandle, ui32Clock) != AM_HAL_STATUS_SUCCESS )
    {
        return AM_HAL_STATUS_FAIL;
    }
    else
    {
        AM_HAL_CARD_DEBUG("SDIO clock frequency is %dHz\n", ui32Clock);
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Enable SD Card detect function
//
uint32_t
am_hal_sd_card_enable_card_detect(am_hal_card_t *pCard, am_hal_host_event_cb_t pfunCallback)
{
    uint32_t ui32Status;

    am_hal_card_host_t *pHost = pCard->pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost || !pfunCallback )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    //
    // Register card interrupt callback function
    //
    ui32Status = am_hal_card_register_evt_callback(pCard, pfunCallback);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    ui32Status = am_hal_sdhc_intr_signal_enable(pHost->pHandle,  SDIO_INTSIG_CARDREMOVALEN_Msk |
                                                                 SDIO_INTSIG_CARDINSERTEN_Msk);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    ui32Status = am_hal_sdhc_intr_status_enable(pHost->pHandle, SDIO_INTENABLE_CARDREMOVALSTATUSENABLE_Msk |
                                                                SDIO_INTENABLE_CARDINSERTIONSTATUSENABLE_Msk);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    return ui32Status;
}

//
//! Disable SD Card detect function
//
uint32_t
am_hal_sd_card_disable_card_detect(am_hal_card_t *pCard)
{
    uint32_t ui32Status;

    am_hal_card_host_t *pHost = pCard->pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    ui32Status = am_hal_sdhc_intr_signal_disable(pHost->pHandle, SDIO_INTSIG_CARDINTEN_Msk |
                                                             SDIO_INTSIG_CARDREMOVALEN_Msk |
                                                              SDIO_INTSIG_CARDINSERTEN_Msk);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    ui32Status = am_hal_sdhc_intr_status_disable(pHost->pHandle, SDIO_INTENABLE_CARDINTERRUPTSTATUSENABLE_Msk |
                                                                   SDIO_INTENABLE_CARDREMOVALSTATUSENABLE_Msk |
                                                                 SDIO_INTENABLE_CARDINSERTIONSTATUSENABLE_Msk);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    return ui32Status;
}

//
//! SD Card write protect detect function
//
uint32_t
am_hal_sd_card_write_protect_detect(am_hal_card_t *pCard)
{
    am_hal_card_host_t *pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    pHost = pCard->pHost;

    if ( pHost->ops->get_wr_protect(pHost->pHandle) )
    {
        pCard->sdcard.bWrProtect = false;
    }
    else
    {
        pCard->sdcard.bWrProtect = true;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! SD Card set voltage
//
static uint32_t
am_hal_sd_card_set_voltage(am_hal_card_t *pCard, am_hal_host_bus_voltage_e eIoVoltage)
{
    uint32_t ui32RetVal = AM_HAL_STATUS_SUCCESS;

#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    //
    // if current voltage equal to setting voltage, do not power cycle and then return
    //
    if ( (pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_3_3_V &&
                    eIoVoltage == AM_HAL_HOST_BUS_VOLTAGE_3_3) ||
         (pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_1_8_V &&
                    eIoVoltage == AM_HAL_HOST_BUS_VOLTAGE_1_8) )
    {
        return AM_HAL_STATUS_SUCCESS;
    }

    //
    // SD card power cycle
    //
    if ( pCard->pCardPwrCtrlFunc != NULL )
    {
        pCard->pCardPwrCtrlFunc(AM_HAL_CARD_PWR_CYCLE);
    }

    //
    // Wait about 74 cycles after powered on
    //
    am_util_delay_us(200);

    //
    // Reset the card
    //
    if ( am_hal_sdmmc_cmd0_go_idle(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("CMD0 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Send CMD8 for interface condition.
    //
    uint32_t ui32OCR = SD_CARD_CHECK_PATTERN | SD_CARD_2P7_3P6_VOLTAGE_SUPPLY;
    if ( am_hal_sdio_cmd8_send_if_cond(pCard, ui32OCR) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("CMD8 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // need retry more than 1s, a ACMD41 need about 3.2ms
    //
    uint32_t ui32Retries = 0x200;
    uint32_t ui32Resp0;

    while (ui32Retries--)
    {
        ui32RetVal = am_hal_sdcard_acmd41_send_op_cond(pCard, SD_CARD_DEFAULT_OCR, &ui32Resp0);
        if ( ui32RetVal != AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("SD Card CMD55&CMD41 error:%d\n", ui32RetVal);
        }

        if ( ui32Resp0 & SD_CARD_OCR_MSK_BUSY )
        {
            //
            // Check the sd card capacity and card type
            //
            if ( ui32Resp0 & SD_CARD_OCR_MSK_HC )
            {
                pCard->bHighCapcity = 1;
                AM_HAL_CARD_DEBUG("High Capacity SD Memory Card\r\n");
            }
            else
            {
                pCard->bHighCapcity = 0;
                AM_HAL_CARD_DEBUG("Standard Capacity SD Memory Card\r\n");
            }

            //
            // Check the sd card if support 1.8V signaling
            //
            if ( ui32Resp0 & SD_CARD_1P8V_MSK )
            {
                pCard->sdcard.bSupportLowVolt = true;
                AM_HAL_CARD_DEBUG("SD Card Support 1.8v Switching\r\n");
            }
            else
            {
                pCard->sdcard.ui32CurrentVolt = AM_HAL_SD_CARD_3_3_V;
                pCard->sdcard.bSupportLowVolt = false;
                AM_HAL_CARD_DEBUG("SD Card not support 1.8V, working at 3.3V signaling\r\n");
            }

            if ( eIoVoltage == AM_HAL_HOST_BUS_VOLTAGE_1_8 && pCard->sdcard.bSupportLowVolt )
            {
                //
                // Switch to 1.8V signaling
                //
                ui32RetVal = am_hal_sdcard_cmd11_voltage_switch(pCard);
                if ( ui32RetVal == AM_HAL_CMD_ERR_NONE )
                {
                    pCard->pCardPwrCtrlFunc(AM_HAL_CARD_PWR_SWITCH);
                    pCard->sdcard.ui32CurrentVolt = AM_HAL_SD_CARD_1_8_V;
                    AM_HAL_CARD_DEBUG("SD Card Switch 1.8v Successfully\r\n");
                }
                else
                {
                    AM_HAL_CARD_DEBUG("SD Card Switch to 1.8V  failed:%d\n", ui32RetVal);
                    return AM_HAL_STATUS_FAIL;
                }
            }

            break;
        }
    }

    if ( !ui32Retries )
    {
        AM_HAL_CARD_DEBUG("SD Card send cmd55&cmd41 retry time out\r\n");
        return AM_HAL_CMD_ERR_TIMEOUT;
    }

    AM_HAL_CARD_DEBUG("SD Card send cmd55&cmd41 complete, card is ready\r\n");

    //
    // Get CID
    //
    if ( am_hal_sdmmc_cmd2_send_cid(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD Card CMD2 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Get RCA
    //
    if ( am_hal_sdmmc_cmd3_set_rca(pCard, 0) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD Card CMD3 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Get the card CSD
    //
    if ( am_hal_sdmmc_cmd9_send_csd(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD Card CMD9 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Select the card as Transfer state
    //
    if ( am_hal_sdmmc_cmd7_card_select(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD Card CMD7 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Configure the card bus width as 4 in default, only support 4 bit width in 1.8V
    //
    if ( pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_1_8_V )
    {
        if ( am_hal_sd_card_set_bus_width(pCard, AM_HAL_HOST_BUS_WIDTH_4) != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Failed to change bus width\n");
            return AM_HAL_STATUS_FAIL;
        }
    }

    AM_HAL_CARD_DEBUG("SD Card sdcard.ui32CurrentVolt  = 0x%x \n", pCard->sdcard.ui32CurrentVolt);
    AM_HAL_CARD_DEBUG("SD Card set voltage done\n");

    return AM_HAL_STATUS_SUCCESS;
}

//
//! SD Card get max block count
//
uint32_t
am_hal_sd_card_get_block_count(am_hal_card_t *pCard)
{
#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    uint32_t ui32CSDVersion;
    uint32_t ui32MaxBlks = 0;
    ui32CSDVersion = am_hal_card_get_csd_field(pCard, 126, 2);
    if ( ui32CSDVersion == 0 )
    {
        uint32_t ui32CSizeMult = am_hal_card_get_csd_field(pCard, 47, 3);
        uint32_t ui32CSize = am_hal_card_get_csd_field(pCard, 62, 12);
        uint32_t ui32RdBlockLen = am_hal_card_get_csd_field(pCard, 80, 4);

        uint32_t ui32Mult = 1 << ( ui32CSizeMult + 2 );
        uint32_t ui32BlockLen = 1 << ui32RdBlockLen;

        //
        // CSD 1.0: Memory capacity = ( C_SIZE + 1 ) * ui32Mult * ui32BlockLen.
        //
        ui32MaxBlks = ( ui32CSize + 1 ) * ui32Mult * ui32BlockLen / 512;
    }
    else if ( ui32CSDVersion == 1 )
    {
        uint32_t ui32CSize = am_hal_card_get_csd_field(pCard, 48, 22);

        //
        // CSD 2.0: Memory capacity = ( C_SIZE + 1 ) * 512K.
        //
        ui32MaxBlks = (ui32CSize + 1) * 1024;
    }

    return ui32MaxBlks;
}

//
//! Card Cache Control
//
static uint32_t
am_hal_card_cache_ctrl(am_hal_card_t *pCard, bool bCacheEnable)
{
    uint32_t ui32Mode;
#ifndef AM_HAL_DISABLE_API_VALIDATION
        if ( !pCard || !pCard->pHost )
        {
            AM_HAL_CARD_DEBUG("This card is not connected to a host\n");
            return AM_HAL_STATUS_INVALID_ARG;
        }
#endif // AM_HAL_DISABLE_API_VALIDATION

    if (pCard->ui32CacheSize > 0)
    {
        ui32Mode = MMC_EXT_MODE_WRITE_BYTE | (MMC_EXT_REGS_CACHE_CTRL << 16) | (bCacheEnable ? (1 << 8) : 0);
        return am_hal_card_mode_switch(pCard, ui32Mode, DEFAULT_CMD6_TIMEOUT_MS);
    }

    return AM_HAL_STATUS_INVALID_OPERATION;
}

//
//! Card Cache On
//
static inline uint32_t
am_hal_card_cache_on(am_hal_card_t *pCard)
{
    return am_hal_card_cache_ctrl(pCard, true);
}

//
//! Card Cache Off
//
static inline uint32_t
am_hal_card_cache_off(am_hal_card_t *pCard)
{
    return am_hal_card_cache_ctrl(pCard, false);
}

//
//! Card Power Notification
//
static uint32_t
am_hal_card_pwr_notification(am_hal_card_t *pCard,
                             uint8_t ui8NotifyType)
{
    uint32_t ui32Mode;
    uint32_t ui32Timeout;

#ifndef AM_HAL_DISABLE_API_VALIDATION
        if ( !pCard || !pCard->pHost )
        {
            AM_HAL_CARD_DEBUG("This card is not connected to a host\n");
            return AM_HAL_STATUS_INVALID_ARG;
        }
#endif // AM_HAL_DISABLE_API_VALIDATION

    if (pCard->ui8ExtCSDRev < 6)
    {
        AM_HAL_CARD_DEBUG("this eMMC card type is not supporting notification\n");
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    ui32Mode = MMC_EXT_MODE_WRITE_BYTE | MMC_EXT_REGS_PWR_OFF_NOTIFY << 16 | ui8NotifyType << 8;

    //
    // Timeout Selection based upon notification type
    //
    switch (ui8NotifyType)
    {
        case MMC_EXT_CSD_POWER_OFF_LONG:
            ui32Timeout = pCard->ui32PowerOffNotificationLongTimeout;
            break;
        case MMC_EXT_CSD_SLEEP_NOTIFICATION:
            ui32Timeout = pCard->ui32SleepNotificationTimeout;
            break;
        case MMC_EXT_CSD_POWERED_ON:
            ui32Timeout = DEFAULT_CMD6_TIMEOUT_MS;
            break;
        default:
            AM_HAL_CARD_DEBUG("Unknown notification type\n");
            return AM_HAL_STATUS_INVALID_ARG;
    }

    if (am_hal_card_mode_switch(pCard, ui32Mode, ui32Timeout) != AM_HAL_STATUS_SUCCESS)
    {
        AM_HAL_CARD_DEBUG("Unknown notification type\n");
        return AM_HAL_STATUS_FAIL;
    }

    pCard->ui8PowerOffNotification = ui8NotifyType;

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Card MMC Init
//
static uint32_t
am_hal_card_mmc_init(am_hal_card_t *pCard)
{
    uint32_t ui32RetVal = AM_HAL_STATUS_SUCCESS;

    if ( pCard->bCardTypeDetect == false )
    {
        //
        // Wait about 74 cycles after powered on
        //
        am_hal_delay_us(200);

        //
        // Reset the card
        //
        if ( am_hal_sdmmc_cmd0_go_idle(pCard) != AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("CMD0 Failed\n");
            return AM_HAL_STATUS_FAIL;
        }

        //
        // Set the operation condition of the card
        //
        if ( am_hal_sdmmc_cmd1_send_op_cond(pCard) != AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("CMD1 Failed\n");
            return AM_HAL_STATUS_FAIL;
        }
    }

    //
    // Get the CID information of the card
    //
    if ( am_hal_sdmmc_cmd2_send_cid(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("CMD2 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Set relative card address to 0x1
    //
    if ( am_hal_sdmmc_cmd3_set_rca(pCard, 0x1) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("CMD3 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Get the card CSD
    //
    if ( am_hal_sdmmc_cmd9_send_csd(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("CMD9 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Select the card
    //
    if ( am_hal_sdmmc_cmd7_card_select(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("CMD7 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Get the ext csd
    //
    if ( am_hal_sdmmc_cmd8_send_ext_csd(pCard) != AM_HAL_CMD_ERR_NONE )
    {
       AM_HAL_CARD_DEBUG("CMD8 Failed\n");
       return AM_HAL_STATUS_FAIL;
    }

    //
    // Parse the CSD and EXT CSD to know the feature of card
    //
    uint32_t ui32CSize = am_hal_unstuff_bits(pCard->ui32CSD, 62, 12);
    pCard->bHighCapcity = (ui32CSize == 0xFFF) ? true : false;

    //
    // High capacity card
    //
    if ( pCard->bHighCapcity )
    {
        //
        // get blksize and capacity information from the ext csd
        //
        pCard->ui32BlkSize = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 61, 1) ? 4096 : 512;
        pCard->bUseBlkEmulation = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 62, 1) ? false : true;
        pCard->ui32NativeBlkSize = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 63, 1) ? 4096 : 512;
        pCard->ui16CmdClass = am_hal_unstuff_bits(pCard->ui32CSD, 84, 12);
        pCard->ui8SpecVer = am_hal_unstuff_bits(pCard->ui32CSD, 122, 4);
        pCard->ui32RpmbSizeMult = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 168, 1);
        pCard->ui8ExtCSDRev = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 192, 1);
        pCard->ui8DeviceType = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 196, 1);
        pCard->ui32MaxBlks = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 212, 4);
        pCard->ui32Capacity = pCard->ui32MaxBlks*pCard->ui32BlkSize;
        pCard->ui32CacheSize = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 249, 4);
        pCard->ui8SecureErase = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 231, 1);
        pCard->ui32GenericCmd6Timeout = DEFAULT_CMD6_TIMEOUT_MS;
        if (pCard->ui8ExtCSDRev >= 6)
        {
            pCard->ui32SleepNotificationTimeout = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 216, 1)*10;
            pCard->ui32PowerOffNotificationLongTimeout = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 247, 1)*10;
            pCard->ui32GenericCmd6Timeout = am_hal_unstuff_bytes(pCard->ui32ExtCSD, 248, 1)*10;
        }
    }
    else
    {
        //
        // non high capacity card gets blksize and capacity information from the CSD
        //
    }

    //
    // Always enable the cache if it's supported
    //
    ui32RetVal = am_hal_card_cache_on(pCard);
    if ( AM_HAL_STATUS_SUCCESS == ui32RetVal )
    {
        //
        // Reread the card to get the updated cache status.
        //
        ui32RetVal = am_hal_card_get_ext_csd_field(pCard, MMC_EXT_REGS_CACHE_CTRL, 1);
        AM_HAL_CARD_DEBUG("\r\nExtCsd CACHE_ON - %s\r\n", ((ui32RetVal == 1) ? "Pass" : "Fail"));
    }
    else
    {
        AM_HAL_CARD_DEBUG("\r\am_hal_card_cache_on - status %x\r\n", ui32RetVal);
    }

    //
    // Always enable the notifcaition if it's supported
    //
    pCard->ui8PowerOffNotification = MMC_EXT_CSD_NO_POWER_NOTIFICATION;
    if (am_hal_card_pwr_notification(pCard, MMC_EXT_CSD_POWERED_ON) == AM_HAL_STATUS_SUCCESS)
    {
        pCard->ui8PowerOffNotification = MMC_EXT_CSD_POWERED_ON;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! SD Card Init
//
static uint32_t
am_hal_sd_card_init(am_hal_card_t *pCard, am_hal_card_pwr_ctrl_func pCardPwrCtrlFunc)
{
    uint32_t ui32RetVal = AM_HAL_STATUS_SUCCESS;

    if ( pCard->bCardTypeDetect == false )
    {
        //
        // Wait about 74 cycles after powered on
        //
        am_hal_delay_us(200);

        //
        // Reset the card
        //
        if ( am_hal_sdmmc_cmd0_go_idle(pCard) != AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("CMD0 Failed\n");
            return AM_HAL_STATUS_FAIL;
        }

        //
        // Send CMD8 for interface condition.
        //
        uint32_t ui32OCR = SD_CARD_CHECK_PATTERN | SD_CARD_2P7_3P6_VOLTAGE_SUPPLY;
        ui32RetVal = am_hal_sdio_cmd8_send_if_cond(pCard, ui32OCR);
        if ( ui32RetVal != AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("SD Card CMD8 Failed\n");
            return AM_HAL_STATUS_FAIL;
        }
    }

    //
    // need retry more than 1s, a ACMD41 need about 3.2ms
    //
    uint32_t ui32Retries = 0x200;
    uint32_t ui32Resp0;

    while (ui32Retries--)
    {
        ui32RetVal = am_hal_sdcard_acmd41_send_op_cond(pCard, SD_CARD_DEFAULT_OCR, &ui32Resp0);
        if ( ui32RetVal != AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("SD Card CMD55&CMD41 error:%d\n", ui32RetVal);
        }

        if ( ui32Resp0 & SD_CARD_OCR_MSK_BUSY )
        {
            //
            // Check the sd card capacity and card type
            //
            if ( ui32Resp0 & SD_CARD_OCR_MSK_HC )
            {
                pCard->bHighCapcity = 1;
                AM_HAL_CARD_DEBUG("High Capacity SD Memory Card\r\n");
            }
            else
            {
                pCard->bHighCapcity = 0;
                AM_HAL_CARD_DEBUG("Standard Capacity SD Memory Card\r\n");
            }

            //
            // Check the sd card if support 1.8V signaling
            //
            if ( ui32Resp0 & SD_CARD_1P8V_MSK )
            {
                pCard->sdcard.bSupportLowVolt = true;
                AM_HAL_CARD_DEBUG("SD Card Support 1.8v Switching\r\n");
            }
            else
            {
                pCard->sdcard.bSupportLowVolt = false;
                AM_HAL_CARD_DEBUG("SD Card not support 1.8V, working at 3.3V signaling\r\n");
            }

            break;
        }
    }

    if ( !ui32Retries )
    {
        AM_HAL_CARD_DEBUG("SD Card send cmd55&cmd41 retry time out\r\n");
        return AM_HAL_CMD_ERR_TIMEOUT;
    }

    AM_HAL_CARD_DEBUG("SD Card send cmd55&cmd41 complete, card is ready\r\n");

    //
    // Init sd card working at 3.3V signaling
    //
    pCard->sdcard.ui32CurrentVolt = AM_HAL_SD_CARD_3_3_V;

    //
    // Get CID
    //
    if ( am_hal_sdmmc_cmd2_send_cid(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD Card CMD2 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Get RCA
    //
    if ( am_hal_sdmmc_cmd3_set_rca(pCard, 0) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD Card CMD3 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Get the card CSD
    //
    if ( am_hal_sdmmc_cmd9_send_csd(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD Card CMD9 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Select the card as Transfer state
    //
    if ( am_hal_sdmmc_cmd7_card_select(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SD Card CMD7 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Get CSD version information from sd card SDSC is CSD1.0 SDHC and SDXC is CSD2.0
    //
    pCard->sdcard.ui32CsdVersion = am_hal_unstuff_bits(pCard->ui32CSD, 126, 2);
    if ( pCard->sdcard.ui32CsdVersion == 0)
    {
        pCard->eType = AM_HAL_CARD_TYPE_SDSC;

        pCard->sdcard.ui32CSizeMult = am_hal_unstuff_bits(pCard->ui32CSD, 47, 3);
        pCard->sdcard.ui32CSize = am_hal_unstuff_bits(pCard->ui32CSD, 62, 12);
        pCard->sdcard.ui32RdBlockLen = am_hal_unstuff_bits(pCard->ui32CSD, 80, 4);

        uint32_t ui32Mult = 1 << ( pCard->sdcard.ui32CSizeMult + 2 );
        uint32_t ui32BlockLen = 1 << pCard->sdcard.ui32RdBlockLen;

        //
        // CSD 1.0: Memory capacity = ( C_SIZE + 1 ) * ui32Mult * ui32BlockLen.
        //
        pCard->ui32MaxBlks = ( pCard->sdcard.ui32CSize + 1 ) * ui32Mult * ui32BlockLen / 512;
        pCard->ui32BlkSize = ui32BlockLen;
        AM_HAL_CARD_DEBUG("This is a SDSC Card\n");
    }
    else if ( pCard->sdcard.ui32CsdVersion == 1 )
    {
        pCard->sdcard.ui32CSize = am_hal_unstuff_bits(pCard->ui32CSD, 48, 22);
        uint32_t ui32ReadBlLen  = am_hal_unstuff_bits(pCard->ui32CSD, 80, 4);

        //
        // SDHC and SDXC block is 512 byte as spec requirement, ui32ReadBlLen is fixed to 9h.
        //
        pCard->ui32BlkSize = 1 << ui32ReadBlLen;

        //
        // CSD 2.0: Memory capacity = ( C_SIZE + 1 ) * 512K.
        //
        pCard->ui32MaxBlks = (pCard->sdcard.ui32CSize + 1) * 1024;
        if ( pCard->ui32MaxBlks <= SDHC_MAX_BLK_CNT )
        {
            pCard->eType = AM_HAL_CARD_TYPE_SDHC;
            AM_HAL_CARD_DEBUG("This is a SDHC Card\n");
        }
        else
        {
            pCard->eType = AM_HAL_CARD_TYPE_SDXC;
            AM_HAL_CARD_DEBUG("This is a SDXC Card\n");
        }
    }
    else
    {
        AM_HAL_CARD_DEBUG("Current driver don't support CSD is Version3.0 or Higher \n");
        return AM_HAL_STATUS_FAIL;
    }

    if ( pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_3_3_V )
    {
        AM_HAL_CARD_DEBUG("SD Card sdcard.bHighSpeed   = 0x%x \n", pCard->sdcard.bHighSpeed);
        AM_HAL_CARD_DEBUG("SD Card sdcard.bWrProtect   = 0x%x \n", pCard->sdcard.bWrProtect);
    }

    AM_HAL_CARD_DEBUG("SD Card sdcard.ui32CurrentVolt  = 0x%x \n", pCard->sdcard.ui32CurrentVolt);
    AM_HAL_CARD_DEBUG("SD Card sdcard.ui32CSize  = 0x%x \n", pCard->sdcard.ui32CSize);
    if ( pCard->sdcard.ui32CsdVersion == 0)
    {
        AM_HAL_CARD_DEBUG("SD Card sdcard.ui32CSizeMult   = 0x%x \n", pCard->sdcard.ui32CSizeMult);
        AM_HAL_CARD_DEBUG("SD Card sdcard.ui32RdBlockLen = 0x%x \n", pCard->sdcard.ui32RdBlockLen);
    }

    //
    // Always enable the cache if it's supported
    //
    ui32RetVal = am_hal_card_cache_on(pCard);
    if ( AM_HAL_STATUS_SUCCESS != ui32RetVal )
    {
        AM_HAL_CARD_DEBUG("\r\am_hal_card_cache_on - status %x\r\n", ui32RetVal);
    }

    AM_HAL_CARD_DEBUG("SD Card initialize is done\n");

    return AM_HAL_STATUS_SUCCESS;
}

static uint32_t
am_hal_card_sdio_init(am_hal_card_t *pCard)
{
    uint32_t ui32OCR = 0;

    if ( pCard->bCardTypeDetect == false )
    {
        //
        // Wait about 74 cycles after powered on
        //
        am_hal_delay_us(200);

        //
        // Reset the card
        //
        if ( am_hal_sdmmc_cmd0_go_idle(pCard) != AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("CMD0 Failed\n");
            return AM_HAL_STATUS_FAIL;
        }

        //
        // Send CMD8 for SDHC or SDXC support.
        //
        ui32OCR |= (MMC_VDD_18_19 | MMC_VDD_17_18);
        if ( am_hal_sdio_cmd8_send_if_cond(pCard, ui32OCR) == AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("SDIO CMD8 Passed.\n");
        }

        //
        // Send CMD5
        //
        ui32OCR = 0;
        if ( am_hal_sdio_cmd5_io_send_op_cond(pCard, ui32OCR) != AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("SDIO CMD5 Error\n");
            return AM_HAL_STATUS_FAIL;
        }
    }

    //
    // Send CMD5 again
    //
    ui32OCR = pCard->ui32OCR;
    if ( am_hal_sdio_cmd5_io_send_op_cond(pCard, ui32OCR) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SDIO Second CMD5 Error\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Send CMD3
    //
    if ( am_hal_sdmmc_cmd3_set_rca(pCard, 0) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SDIO CMD3 Error\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Select the card
    //
    if ( am_hal_sdmmc_cmd7_card_select(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SDIO CMD7 Error\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Read CCCR
    //
    if ( am_hal_sdio_card_read_cccr(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SDIO read CCCR Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Read CIS
    //
    if ( am_hal_sdio_card_read_cis(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("SDIO read CIS Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    return AM_HAL_STATUS_SUCCESS;
}

static uint32_t
am_hal_card_sd_init(am_hal_card_t *pCard)
{
    AM_HAL_CARD_DEBUG("card type %d is not supported yet\n", pCard->eType);
    return AM_HAL_STATUS_FAIL;
}

static uint32_t
am_hal_card_type_detect(am_hal_card_t *pCard)
{
    uint32_t ui32RetVal = AM_HAL_STATUS_SUCCESS;

    //
    // Wait about 74 cycles after powered on
    //
    am_util_delay_us(200);

    //
    // Reset the card
    //
    if ( am_hal_sdmmc_cmd0_go_idle(pCard) != AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("CMD0 Failed\n");
        return AM_HAL_STATUS_FAIL;
    }

    //
    // Send CMD8 for interface condition.
    //
    uint32_t ui32OCR = SD_CARD_CHECK_PATTERN | SD_CARD_2P7_3P6_VOLTAGE_SUPPLY;
    ui32RetVal = am_hal_sdio_cmd8_send_if_cond(pCard, ui32OCR);
    if ( ui32RetVal == AM_HAL_CMD_ERR_NONE )
    {
            AM_HAL_CARD_DEBUG("Detected SD Card device\n");
            pCard->eType = AM_HAL_CARD_TYPE_SDHC;
            return AM_HAL_STATUS_SUCCESS;
    }

    //
    //  Check for SDIO/MMC Cards
    //
    ui32OCR = 0;
    ui32RetVal = am_hal_sdmmc_cmd1_send_op_cond(pCard);
    if ( ui32RetVal == AM_HAL_CMD_ERR_NONE )
    {
        AM_HAL_CARD_DEBUG("Detected MMC device\n");
        pCard->eType = AM_HAL_CARD_TYPE_EMMC;
    }
    else
    {
        ui32RetVal = am_hal_sdio_cmd5_io_send_op_cond(pCard, ui32OCR);
        if ( ui32RetVal == AM_HAL_CMD_ERR_NONE )
        {
            AM_HAL_CARD_DEBUG("Detected SDIO Card device\n");
            pCard->eType = AM_HAL_CARD_TYPE_SDIO;
        }
        else
        {
            AM_HAL_CARD_DEBUG("NO Device Found...\n");
            return AM_HAL_STATUS_FAIL;
        }
    }

    return AM_HAL_STATUS_SUCCESS;
}

static uint32_t
am_hal_card_set_bus_width(am_hal_card_t *pCard, am_hal_host_bus_width_e eBusWidth)
{
    uint32_t ui32Mode;
    uint32_t ui32Status;

    am_hal_card_host_t *pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    pHost = pCard->pHost;
    ui32Mode = 0x0;
    if ( eBusWidth != pHost->eBusWidth || pHost->eUHSMode != pCard->cfg.eUHSMode)
    {
        switch ( eBusWidth )
        {
            case AM_HAL_HOST_BUS_WIDTH_1:
                //
                // 3 << 24 | 183 << 16 | 0 << 8
                // 0x03b70000
                //
                ui32Mode = MMC_EXT_MODE_WRITE_BYTE | MMC_EXT_REGS_BUS_WIDTH | MMC_EXT_SET_BUS_WIDTH1;
                break;
            case AM_HAL_HOST_BUS_WIDTH_4:
                //
                // 3 << 24 | 183 << 16 | 1 << 8
                // 0x03b70100
                //
                if (pCard->cfg.eUHSMode == AM_HAL_HOST_UHS_DDR50)
                {
                    ui32Mode = MMC_EXT_MODE_WRITE_BYTE | MMC_EXT_REGS_BUS_WIDTH | MMC_EXT_SET_BUS_WIDTH4_DDR;
                }
                else
                {
                    ui32Mode = MMC_EXT_MODE_WRITE_BYTE | MMC_EXT_REGS_BUS_WIDTH | MMC_EXT_SET_BUS_WIDTH4;
                }
                break;
            case AM_HAL_HOST_BUS_WIDTH_8:
                //
                // 3 << 24 | 183 << 16 | 2 << 8
                // 0x03b70200
                //
                if (pCard->cfg.eUHSMode == AM_HAL_HOST_UHS_DDR50)
                {
                    ui32Mode = MMC_EXT_MODE_WRITE_BYTE | MMC_EXT_REGS_BUS_WIDTH | MMC_EXT_SET_BUS_WIDTH8_DDR;
                }
                else
                {
                    ui32Mode = MMC_EXT_MODE_WRITE_BYTE | MMC_EXT_REGS_BUS_WIDTH | MMC_EXT_SET_BUS_WIDTH8;
                }
                break;
        }

        if ( (ui32Status = am_hal_card_mode_switch(pCard, ui32Mode, DEFAULT_CMD6_TIMEOUT_MS)) != AM_HAL_STATUS_SUCCESS )
        {
            return ui32Status;
        }

        if ( pHost->ops->set_bus_width(pHost->pHandle, eBusWidth) != AM_HAL_STATUS_SUCCESS )
        {
            return AM_HAL_STATUS_FAIL;
        }
    }

    return AM_HAL_STATUS_SUCCESS;
}

static uint32_t
am_hal_card_set_uhs_mode(am_hal_card_t *pCard, am_hal_host_uhs_mode_e eUHSMode)
{
    am_hal_card_host_t *pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    pHost = pCard->pHost;

    //
    // Already in this UHS mode
    //
    if ( pHost->eUHSMode == eUHSMode )
    {
        return AM_HAL_STATUS_SUCCESS;
    }

    if ( eUHSMode == AM_HAL_HOST_UHS_DDR50 )
    {
        if ( pCard->eType == AM_HAL_CARD_TYPE_EMMC ||
            pCard->eType == AM_HAL_CARD_TYPE_MMC  ||
            pCard->eType == AM_HAL_CARD_TYPE_SDSC ||
            pCard->eType == AM_HAL_CARD_TYPE_SDHC ||
            pCard->eType == AM_HAL_CARD_TYPE_SDXC )
        {
            //
            // Card does not support DDR52 mode
            //
            if ( pCard->eType == AM_HAL_CARD_TYPE_EMMC || pCard->eType == AM_HAL_CARD_TYPE_MMC )
            {
                if ( (pCard->ui8DeviceType & HS_DDR_52MHZ_18_3V) == 0x0 )
                {
                    return AM_HAL_STATUS_FAIL;
                }
            }
            else
            {
                if ( pCard->sdcard.bSupportDDR50 == false)
                {
                    return AM_HAL_STATUS_FAIL;
                }
            }

            if (pHost->ops->set_uhs_mode)
            {
                return pHost->ops->set_uhs_mode(pHost->pHandle, eUHSMode);
            }
        }
        else
        {
            if (pHost->ops->set_uhs_mode)
            {
                return pHost->ops->set_uhs_mode(pHost->pHandle, eUHSMode);
            }
        }
    }
    else
    {
        if (pHost->ops->set_uhs_mode)
        {
            return pHost->ops->set_uhs_mode(pHost->pHandle, AM_HAL_HOST_UHS_NONE);
        }
    }

    return AM_HAL_STATUS_SUCCESS;
}

static uint32_t
am_hal_card_set_voltage(am_hal_card_t *pCard, am_hal_host_bus_voltage_e eBusVoltage)
{
    am_hal_card_host_t *pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    pHost = pCard->pHost;

    if ( eBusVoltage != pHost->eBusVoltage )
    {
        if ( pHost->ops->set_bus_voltage(pHost->pHandle, eBusVoltage) != AM_HAL_STATUS_SUCCESS )
        {
            return AM_HAL_STATUS_FAIL;
        }
    }

    return AM_HAL_STATUS_SUCCESS;
}

static uint32_t
am_hal_card_set_speed(am_hal_card_t *pCard, uint32_t ui32Clock)
{
    uint32_t ui32Status;
    uint32_t ui32Mode;
    uint32_t ui32HSMode = 1;
    am_hal_card_host_t *pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    pHost = pCard->pHost;

    if ((pCard->cfg.eUHSMode == AM_HAL_HOST_UHS_SDR104) &&
        (ui32Clock > MMC_HS_MAX_SPEED_LIMIT) &&
        (ui32Clock <= MMC_HS200_MAX_SPEED_LIMIT))
    {
        ui32HSMode = 2;
    }

    if ( (pCard->ui8SpecVer >= 4) && (ui32Clock >= MMC_LEGACY_HS) )
    {
        //
        // Already in the high speed mode
        //
        if ( am_hal_unstuff_bytes(pCard->ui32ExtCSD, 185, 1) != ui32HSMode )
        {
            //
            // 0x03B90100
            //
            ui32Mode = MMC_EXT_MODE_WRITE_BYTE | MMC_EXT_REGS_HIGH_SPEED | (ui32HSMode << 8);
            if ( (ui32Status = am_hal_card_mode_switch(pCard, ui32Mode, DEFAULT_CMD6_TIMEOUT_MS)) != AM_HAL_STATUS_SUCCESS )
            {
                return ui32Status;
            }

            //
            // read back the ext_csd and check 'HS_TIMING' is set or not
            //
            if ( am_hal_sdmmc_cmd8_send_ext_csd(pCard) != AM_HAL_CMD_ERR_NONE )
            {
                return AM_HAL_STATUS_FAIL;
            }

            if ( am_hal_unstuff_bytes(pCard->ui32ExtCSD, 185, 1) != ui32HSMode )
            {
                AM_HAL_CARD_DEBUG("%d - Failed to switch to high speed mode\n", __FUNCTION__);
                return AM_HAL_STATUS_FAIL;
            }
        }
    }

    if ( pHost->ops->set_bus_clock(pHost->pHandle, ui32Clock) != AM_HAL_STATUS_SUCCESS )
    {
        return AM_HAL_STATUS_FAIL;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
// Static helper function:
//  Count the longest consecutive 1s in a 32bit word
//
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

//
// Static helper function:
//  Find and return the mid point of the longest continuous 1s in a 32bit word
//
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

        if ( pick_point_flag == true )
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

uint32_t
am_hal_card_emmc_calibrate(am_hal_host_uhs_mode_e eUHSMode,
                           uint32_t ui32Clock,
                           am_hal_host_bus_width_e eBusWidth,
                           uint8_t *ui8CalibBuf,
                           uint32_t ui32StartBlk,
                           uint32_t ui32BlkCnt,
                           uint8_t ui8TxRxDelays[2])
{
    am_hal_card_t eMMCard;
    am_hal_card_host_t *pSdhcCardHost;

    uint32_t i;
    uint32_t len = ui32BlkCnt*512;
    uint8_t  ui8TxDelay = 0;
    uint8_t  ui8RxDelay = 0;
    uint32_t ui32TxResult = 0;
    uint32_t ui32RxResultArray[16] = {0};

#ifdef AM_DEBUG_PRINTF
    if (eUHSMode == AM_HAL_HOST_UHS_DDR50)
    {
        AM_HAL_CARD_DEBUG("eMMC Timing Scan for DDR %d Hz %d bit mode...\n", ui32Clock, eBusWidth);
    }
    else
    {
        AM_HAL_CARD_DEBUG("eMMC Timing Scan for SDR %d Hz %d bit mode...\n", ui32Clock, eBusWidth);
    }
#endif

    for (int j = 0; j < SDIO_SCAN_TXDELAY_MAX; j++)
    {
        for (int k = 0; k < SDIO_SCAN_RXDELAY_MAX; k++)
        {
            //
            // Get the uderlying SDHC card host instance
            //
            pSdhcCardHost = am_hal_get_card_host(AM_HAL_SDHC_CARD_HOST, true);

            if (pSdhcCardHost == NULL)
            {
                AM_HAL_CARD_DEBUG("No such card host and stop\n");
                return AM_HAL_STATUS_FAIL;
            }

            ui8TxRxDelays[0] = j;
            ui8TxRxDelays[1] = k;
            AM_HAL_CARD_DEBUG("Test TX delay setting SDIOOTAPDLYSEL = %d, RX delay setting SDIOITAPDLYSEL = %d\n", j, k);
            am_hal_card_host_set_txrx_delay(pSdhcCardHost, ui8TxRxDelays);

            //
            // check if card is present
            //
            if (am_hal_card_host_find_card(pSdhcCardHost, &eMMCard) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("No card is present now\n");
                return AM_HAL_STATUS_FAIL;
            }

            if (am_hal_card_init(&eMMCard, AM_HAL_CARD_TYPE_EMMC, NULL, AM_HAL_CARD_PWR_CTRL_NONE) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("card initialization failed\n");
                return AM_HAL_STATUS_FAIL;
            }

            if (am_hal_card_cfg_set(&eMMCard, AM_HAL_CARD_TYPE_EMMC,
                eBusWidth, ui32Clock, AM_HAL_HOST_BUS_VOLTAGE_1_8,
                eUHSMode) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("card config failed\n");
                continue;
            }

            for (i = 0; i < len; i++)
            {
                ui8CalibBuf[i] = i % 256;
            }

            am_hal_card_block_write_sync(&eMMCard, ui32StartBlk, ui32BlkCnt, (uint8_t *)ui8CalibBuf);

            memset((void *)ui8CalibBuf, 0x0, len);
            am_hal_card_block_read_sync(&eMMCard, ui32StartBlk, ui32BlkCnt, (uint8_t *)ui8CalibBuf);

            for (i = 0; i < len; i++)
            {
                if (ui8CalibBuf[i] != i % 256)
                {
                    break;
                }
            }

            if (i == len)
            {
                ui32RxResultArray[j] |= 0x01 << k;
                AM_HAL_CARD_DEBUG("TX Delay SDIOOTAPDLYSEL = %d, RX Delay SDIOITAPDLYSEL = %d works\n", j, k);
            }
            else
            {
                AM_HAL_CARD_DEBUG("TX Delay SDIOOTAPDLYSEL = %d, RX Delay SDIOITAPDLYSEL = %d doesn't work\n", j, k);
            }
        }
    }

#ifdef AM_DEBUG_PRINTF
    AM_HAL_CARD_DEBUG("\nSDIO TX RX Delay Scan Result:\n");
    for (i = 0; i < SDIO_SCAN_TXDELAY_MAX; i++ )
    {
        AM_HAL_CARD_DEBUG("TX_Delay = %2d, RX_Delay Window = 0x%08X\n", i, ui32RxResultArray[i]);
    }
#endif

    uint32_t ui32Result = 0;
    for (i = 0; i < SDIO_SCAN_TXDELAY_MAX; i++ )
    {
        ui32Result = count_consecutive_ones(&ui32RxResultArray[i]);
        if ( ui32Result >= SDIO_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH )
        {
            ui32TxResult |= 0x01 << i;
        }
    }

    //
    // Check consecutive passing settings
    //
    if ( ui32TxResult == 0 )
    {
        //
        // No acceptable window
        //
        return AM_HAL_STATUS_FAIL;
    }
    else
    {
        ui32Result = count_consecutive_ones(&ui32TxResult);
        if ( ui32Result < SDIO_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH )
        {
            //
            // No acceptable window
            //
            return AM_HAL_STATUS_FAIL;
        }
    }

    //
    // Find TX Delay Value
    //
    ui8TxDelay = find_mid_point(&ui32TxResult);

    AM_HAL_CARD_DEBUG("\nSDIO Timing Scan found a RX delay window %X with TX delay Setting = %d.\n", ui32RxResultArray[ui8TxDelay], ui8TxDelay);

    //
    // Find RX Delay Value
    //
    ui8RxDelay = find_mid_point(&ui32RxResultArray[ui8TxDelay]);
    ui8TxRxDelays[0] = ui8TxDelay;
    ui8TxRxDelays[1] = ui8RxDelay;

    AM_HAL_CARD_DEBUG("Timing Scan set the SDIO TX delay to %d and RX delay to %d.\n", ui8TxDelay, ui8RxDelay);

    //
    // Force the card host to reset with the calibration settings
    //
    pSdhcCardHost = am_hal_get_card_host(AM_HAL_SDHC_CARD_HOST, true);

    if (pSdhcCardHost == NULL)
    {
        AM_HAL_CARD_DEBUG("No such card host and stop\n");
        return AM_HAL_STATUS_FAIL;
    }
    am_hal_card_host_set_txrx_delay(pSdhcCardHost, ui8TxRxDelays);

    return AM_HAL_STATUS_SUCCESS;
}

uint32_t
am_hal_sd_card_calibrate(am_hal_host_uhs_mode_e eUHSMode,
                         uint32_t ui32Clock,
                         am_hal_host_bus_width_e eBusWidth,
                         uint8_t *ui8CalibBuf,
                         uint32_t ui32StartBlk,
                         uint32_t ui32BlkCnt,
                         uint8_t ui8TxRxDelays[2],
                         am_hal_card_pwr_ctrl_func pCardPwrCtrlFunc)
{
    am_hal_card_t SdCard;
    am_hal_card_host_t *pSdhcCardHost;

    uint32_t i;
    uint32_t len = ui32BlkCnt*512;
    uint8_t  ui8TxDelay = 0;
    uint8_t  ui8RxDelay = 0;
    uint32_t ui32TxResult = 0;
    uint32_t ui32RxResultArray[16] = {0};
    uint32_t ui32RxIndexEnd = SDIO_SCAN_RXDELAY_MAX;

#ifdef AM_DEBUG_PRINTF
    if (eUHSMode == AM_HAL_HOST_UHS_DDR50)
    {
        AM_HAL_CARD_DEBUG("SD Card Timing Scan for DDR %d Hz %d bit mode...\n", ui32Clock, eBusWidth);
    }
    else
    {
        AM_HAL_CARD_DEBUG("SD Card Timing Scan for SDR %d Hz %d bit mode...\n", ui32Clock, eBusWidth);
    }
#endif

    if ( eUHSMode == AM_HAL_HOST_UHS_SDR104 )
    {
        ui32RxIndexEnd = 15;
    }

    for (int j = 0; j < SDIO_SCAN_TXDELAY_MAX; j++)
    {
        for (int k = 0; k < ui32RxIndexEnd; k++)
        {
            //
            // Get the uderlying SDHC card host instance
            //
            pSdhcCardHost = am_hal_get_card_host(AM_HAL_SDHC_CARD_HOST, true);

            if (pSdhcCardHost == NULL)
            {
                AM_HAL_CARD_DEBUG("No such card host and stop\n");
                return AM_HAL_STATUS_FAIL;
            }

            ui8TxRxDelays[0] = j;
            ui8TxRxDelays[1] = k;
            AM_HAL_CARD_DEBUG("Test TX delay setting SDIOOTAPDLYSEL = %d, RX delay setting SDIOITAPDLYSEL = %d\n", j, k);
            am_hal_card_host_set_txrx_delay(pSdhcCardHost, ui8TxRxDelays);

            //
            // check if card is present
            //
            if (am_hal_card_host_find_card(pSdhcCardHost, &SdCard) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("No card is present now\n");
                return AM_HAL_STATUS_FAIL;
            }

            if (am_hal_card_init(&SdCard, AM_HAL_CARD_TYPE_SDHC, pCardPwrCtrlFunc, AM_HAL_CARD_PWR_CTRL_NONE) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("card initialization failed\n");
                return AM_HAL_STATUS_FAIL;
            }

            if (am_hal_card_cfg_set(&SdCard, AM_HAL_CARD_TYPE_SDHC,
                eBusWidth, ui32Clock, AM_HAL_HOST_BUS_VOLTAGE_1_8,
                eUHSMode) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("card config failed\n");
                continue;
            }

            for (i = 0; i < len; i++)
            {
                ui8CalibBuf[i] = i % 256;
            }

            am_hal_sd_card_block_write_sync(&SdCard, ui32StartBlk, ui32BlkCnt, (uint8_t *)ui8CalibBuf);
            memset((void *)ui8CalibBuf, 0x0, len);
            am_hal_sd_card_block_read_sync(&SdCard, ui32StartBlk, ui32BlkCnt, (uint8_t *)ui8CalibBuf);

            for (i = 0; i < len; i++)
            {
                if (ui8CalibBuf[i] != i % 256)
                {
                    break;
                }
            }

            if (i == len)
            {
                ui32RxResultArray[j] |= 0x01 << k;
                AM_HAL_CARD_DEBUG("TX Delay SDIOOTAPDLYSEL = %d, RX Delay SDIOITAPDLYSEL = %d works\n", j, k);
            }
            else
            {
                AM_HAL_CARD_DEBUG("TX Delay SDIOOTAPDLYSEL = %d, RX Delay SDIOITAPDLYSEL = %d doesn't work\n", j, k);
            }
        }
    }

#ifdef AM_DEBUG_PRINTF
    AM_HAL_CARD_DEBUG("\nSDIO TX RX Delay Scan Result:\n");
    for (i = 0; i < SDIO_SCAN_TXDELAY_MAX; i++ )
    {
        AM_HAL_CARD_DEBUG("TX_Delay = %2d, RX_Delay Window = 0x%08X\n", i, ui32RxResultArray[i]);
    }
#endif

    uint32_t ui32Result = 0;
    for (i = 0; i < SDIO_SCAN_TXDELAY_MAX; i++ )
    {
        ui32Result = count_consecutive_ones(&ui32RxResultArray[i]);
        if ( ui32Result >= SDIO_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH )
        {
            ui32TxResult |= 0x01 << i;
        }
    }

    //
    // Check consecutive passing settings
    //
    if ( ui32TxResult == 0 )
    {
        //
        // No acceptable window
        //
        return AM_HAL_STATUS_FAIL;
    }
    else
    {
        ui32Result = count_consecutive_ones(&ui32TxResult);
        if ( ui32Result < SDIO_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH )
        {
            //
            // No acceptable window
            //
            return AM_HAL_STATUS_FAIL;
        }
    }

    //
    // Find TX Delay Value
    //
    ui8TxDelay = find_mid_point(&ui32TxResult);

    AM_HAL_CARD_DEBUG("\nSDIO Timing Scan found a RX delay window %X with TX delay Setting = %d.\n", ui32RxResultArray[ui8TxDelay], ui8TxDelay);

    //
    // Find RX Delay Value
    //
    ui8RxDelay = find_mid_point(&ui32RxResultArray[ui8TxDelay]);
    ui8TxRxDelays[0] = ui8TxDelay;
    ui8TxRxDelays[1] = ui8RxDelay;

    AM_HAL_CARD_DEBUG("Timing Scan set the SDIO TX delay to %d and RX delay to %d.\n", ui8TxDelay, ui8RxDelay);

    am_hal_card_host_set_txrx_delay(pSdhcCardHost, ui8TxRxDelays);

    return AM_HAL_STATUS_SUCCESS;
}

//
// Public functions
//

//*****************************************************************************
//
// Power off the SDHC or eMMC CARD
//
//*****************************************************************************
uint32_t
am_hal_card_pwrctrl_sleep(am_hal_card_t *pCard)
{
    am_hal_card_host_t *pHost;
    uint32_t ui32Status = AM_HAL_STATUS_SUCCESS;

#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    pHost = pCard->pHost;

    switch (pCard->eCardPwrCtrlPolicy)
    {
        case AM_HAL_CARD_PWR_CTRL_SDHC_AND_CARD_OFF:
            //
            // Flush the cache before power off the eMMC card
            //
            if ( (ui32Status = am_hal_card_cache_off(pCard)) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("sdhc & card power off - am_hal_card_cache_off Failed\n");
                return ui32Status;
            }

            //
            // Host is going to power off the device
            //
            if ((ui32Status = am_hal_card_pwr_notification(pCard, MMC_EXT_CSD_POWER_OFF_LONG)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("sdhc & card power off - am_hal_card_cache_off Failed\n");
                return ui32Status;
            }

            //
            // Deselect card
            //
            if ((ui32Status = am_hal_sdmmc_cmd7_card_deselect(pCard)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("sdhc & card sleep - Failed to deselect the card\n");
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Goto Sleep state before power off the card
            //
            if ( AM_HAL_CMD_ERR_NONE != am_hal_sdmmc_cmd5(pCard, true) )
            {
                AM_HAL_CARD_DEBUG("sdhc & card sleep - am_hal_sdmmc_cmd5 Failed\n");
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Call User Set Callback Function for Power Control
            //
            if (pCard->pCardPwrCtrlFunc)
            {
                pCard->pCardPwrCtrlFunc(AM_HAL_CARD_PWR_OFF);
            }

            pCard->eState = AM_HAL_CARD_STATE_PWROFF;

            if ((ui32Status = pHost->ops->deinit(pHost->pHandle)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("sdhc & card power off - host deinit failed\n");
                return ui32Status;
            }
            break;

        case AM_HAL_CARD_PWR_CTRL_SDHC_OFF_AND_CARD_SLEEP:

            //
            // Flush the cache before power off the eMMC card
            //
            if ( (ui32Status = am_hal_card_cache_off(pCard)) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("sdhc & card sleep - am_hal_card_cache_off Failed\n");
                return ui32Status;
            }

            //
            // Host is going to put the device in Sleep Mode
            //
            if ( (ui32Status = am_hal_card_pwr_notification(pCard, MMC_EXT_CSD_SLEEP_NOTIFICATION)) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("sdhc & card sleep - am_hal_card_pwr_notification Failed\n");
                return ui32Status;
            }

            //
            // Deselect card
            //
            if ((ui32Status = am_hal_sdmmc_cmd7_card_deselect(pCard)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("sdhc & card sleep - Failed to deselect the card\n");
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Goto Sleep state
            //
            if ( AM_HAL_CMD_ERR_NONE != am_hal_sdmmc_cmd5(pCard, true) )
            {
                AM_HAL_CARD_DEBUG("sdhc & card sleep - am_hal_sdmmc_cmd5 Failed\n");
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Power off the SDHC controller
            //
            if ((ui32Status = pHost->ops->pwr_ctrl(pHost->pHandle, false)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("sdhc & card sleep - emmc card power control failed\n");
                return ui32Status;
            }
            break;

        case AM_HAL_CARD_PWR_CTRL_SDHC_OFF:
            //
            // Power off the SDHC controller
            //
            if ((ui32Status = pHost->ops->pwr_ctrl(pHost->pHandle, false)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("sdhc - emmc card power control failed\n");
                return ui32Status;
            }
            break;

        default:
            break;

    }

    return AM_HAL_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Power on the SDHC or eMMC CARD
//
//*****************************************************************************
uint32_t
am_hal_card_pwrctrl_wakeup(am_hal_card_t *pCard)
{
    uint32_t ui32Status;
    am_hal_card_host_t *pHost;

#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    pHost = pCard->pHost;

    switch (pCard->eCardPwrCtrlPolicy)
    {
        case AM_HAL_CARD_PWR_CTRL_SDHC_AND_CARD_OFF:
            //
            // ReInit the SDHC controller
            //
            if ((ui32Status = pHost->ops->init(pHost)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("wakeup reinit sdhc controller failed\n");
                return ui32Status;
            }

            //
            // ReInit the card again
            //
            if ((ui32Status = am_hal_card_init(pCard, pCard->eType,
                pCard->pCardPwrCtrlFunc, pCard->eCardPwrCtrlPolicy)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("wakeup reinit sd card failed\n");
                return ui32Status;
            }

            //
            // Reconfig the card with the original settings
            //
            if ((ui32Status = am_hal_card_cfg_set(pCard, pCard->eType, pCard->cfg.eBusWidth,
                pCard->cfg.ui32Clock, pCard->cfg.eIoVoltage, pCard->cfg.eUHSMode)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("wakeup configure sd card failed\n");
                return ui32Status;
            }
            break;

        case AM_HAL_CARD_PWR_CTRL_SDHC_OFF_AND_CARD_SLEEP:
            //
            // Power on the SDHC controller
            //
            if ((ui32Status = pHost->ops->pwr_ctrl(pHost->pHandle, true)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("wakeup - power on the sdhc controller failed\n");
                return ui32Status;
            }

            //
            // Exit Sleep state
            //
            if ( AM_HAL_CMD_ERR_NONE != am_hal_sdmmc_cmd5(pCard, false) )
            {
                AM_HAL_CARD_DEBUG("wakeup - am_hal_sdmmc_cmd5 Failed\n");
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Reselect card
            //
            if ( AM_HAL_CMD_ERR_NONE != am_hal_sdmmc_cmd7_card_select(pCard) )
            {
                AM_HAL_CARD_DEBUG("wakeup - am_hal_sdmmc_cmd7_card_select Failed\n");
                return AM_HAL_STATUS_FAIL;
            }
            break;

        case AM_HAL_CARD_PWR_CTRL_SDHC_OFF:
            //
            // Power on the SDHC controller
            //
            if ((ui32Status = pHost->ops->pwr_ctrl(pHost->pHandle, true)) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("wakeup - power on the sdhc controller failed\n");
                return ui32Status;
            }
            break;

        default:
            break;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Get the card instance function
//
//*****************************************************************************
uint32_t
am_hal_card_host_find_card(am_hal_card_host_t *pHost, am_hal_card_t *pCard)
{

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pHost || !pCard )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pHost->bCardInSlot ||
        pHost->ops->get_cd(pHost->pHandle) )
    {
        pCard->eState = AM_HAL_CARD_STATE_PRESENT;
        pCard->pHost  = pHost;
        //
        // Fill the default setting with the host's current value
        //
        pCard->cfg.eBusWidth = pHost->eBusWidth;
        pCard->cfg.eIoVoltage = pHost->eBusVoltage;
        pCard->cfg.ui32Clock = pHost->ui32MinClock;
        pCard->cfg.eUHSMode = pHost->eUHSMode;
        pCard->eState = AM_HAL_CARD_STATE_PWROFF;
        return AM_HAL_STATUS_SUCCESS;
    }
    else
    {
        pCard->eState = AM_HAL_CARD_STATE_NOT_PRESENT;
        return AM_HAL_STATUS_FAIL;
    }
}

//*****************************************************************************
//
// Set the card operation configurations
//
//*****************************************************************************
uint32_t
am_hal_card_cfg_set(am_hal_card_t *pCard, am_hal_card_type_e eType,
                    am_hal_host_bus_width_e eBusWidth,
                    uint32_t ui32Clock,
                    am_hal_host_bus_voltage_e eIoVoltage,
                    am_hal_host_uhs_mode_e eUHSMode)
{
#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        AM_HAL_CARD_DEBUG("This card is not connected to a host\n");
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    //
    // Checking the validation of the clock settings
    // Clock should not exceed the capabilities of the SDHC
    // DDR mode, max speed is not higer than 48MHz
    // SDR mode, max speed is not higer than 96MHz
    //
    if ((ui32Clock > pCard->pHost->ui32MaxClock) || (ui32Clock < pCard->pHost->ui32MinClock) ||
        ((eUHSMode == AM_HAL_HOST_UHS_DDR50) && (ui32Clock > MMC_HS_MAX_SPEED_LIMIT)) ||
        ((eUHSMode == AM_HAL_HOST_UHS_NONE) && (ui32Clock > MMC_HS200_MAX_SPEED_LIMIT)) ||
        (ui32Clock > MMC_HS200_MAX_SPEED_LIMIT))
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

    pCard->cfg.eBusWidth = eBusWidth;
    pCard->cfg.ui32Clock = ui32Clock;
    pCard->cfg.eIoVoltage = eIoVoltage;
    pCard->cfg.eUHSMode = eUHSMode;

    if ( pCard->eType == AM_HAL_CARD_TYPE_EMMC )
    {
        //
        // Configure the card voltage
        //
        if ( am_hal_card_set_voltage(pCard, pCard->cfg.eIoVoltage) != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Failed to change bus voltage\n");
            return AM_HAL_STATUS_FAIL;
        }

        //
        // Checking the validation of the clock settings
        // Clock should not exceed the capabilities of the SDHC
        // DDR mode, max speed is not higer than 48MHz
        // SDR mode, max speed is not higer than 96MHz
        // Legacy card, max speed is not higher than 26MHz
        //
        if ((ui32Clock > MMC_LEGACY_HS) && (pCard->ui8SpecVer < 4))
        {
            return AM_HAL_STATUS_INVALID_ARG;
        }

        if ( eUHSMode == AM_HAL_HOST_UHS_SDR104 )
        {
            //
            // Configure the card bus width
            //
            if ( am_hal_card_set_bus_width(pCard, pCard->cfg.eBusWidth) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Failed to change bus width\n");
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Configure the card bus speed
            //
            if ( am_hal_card_set_speed(pCard, pCard->cfg.ui32Clock) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Failed to change bus speed\n");
                return AM_HAL_STATUS_FAIL;
            }
        }
        else
        {
            //
            // Configure the card bus speed
            //
            if ( am_hal_card_set_speed(pCard, pCard->cfg.ui32Clock) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Failed to change bus speed\n");
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Configure the card bus width
            //
            if ( am_hal_card_set_bus_width(pCard, pCard->cfg.eBusWidth) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Failed to change bus width\n");
                return AM_HAL_STATUS_FAIL;
            }
        }

        //
        // Select the UHS mode
        //
        if (am_hal_card_set_uhs_mode(pCard, pCard->cfg.eUHSMode) != AM_HAL_STATUS_SUCCESS)
        {
            AM_HAL_CARD_DEBUG("Failed to change UHS mode\n");
            return AM_HAL_STATUS_FAIL;
        }
    }
    else if ( pCard->eType == AM_HAL_CARD_TYPE_SDIO )
    {
        //
        // Configure the card bus speed
        //
        if ( am_hal_sdio_card_set_speed(pCard, pCard->cfg.ui32Clock) != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Failed to change bus speed\n");
            return AM_HAL_STATUS_FAIL;
        }

        //
        // Configure the card bus width
        //
        if ( am_hal_sdio_card_set_bus_width(pCard, pCard->cfg.eBusWidth) != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Failed to change bus width\n");
            return AM_HAL_STATUS_FAIL;
        }
    }
    else if ( pCard->eType == AM_HAL_CARD_TYPE_SDSC ||
              pCard->eType == AM_HAL_CARD_TYPE_SDHC ||
              pCard->eType == AM_HAL_CARD_TYPE_SDXC )
    {
        //
        // Reinit sd card when switch working voltage
        //
        if ( am_hal_sd_card_set_voltage(pCard, eIoVoltage)!= AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Failed to set sd card voltage\n");
            return AM_HAL_STATUS_FAIL;
        }

        //
        // Configure the card bus speed
        //
        if ( am_hal_sd_card_set_speed(pCard, pCard->cfg.ui32Clock) != AM_HAL_STATUS_SUCCESS )
        {
            AM_HAL_CARD_DEBUG("Failed to change bus speed\n");
            return AM_HAL_STATUS_FAIL;
        }

        //
        // Configure the card bus width when work at 3.3V
        //
        if ( pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_3_3_V )
        {
            if ( am_hal_sd_card_set_bus_width(pCard, pCard->cfg.eBusWidth) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Failed to change bus width\n");
                return AM_HAL_STATUS_FAIL;
            }
        }

        //
        // Select the UHS mode
        //
        if ( pCard->sdcard.ui32CurrentVolt == AM_HAL_SD_CARD_1_8_V )
        {
            if (am_hal_card_set_uhs_mode(pCard, pCard->cfg.eUHSMode) != AM_HAL_STATUS_SUCCESS)
            {
                AM_HAL_CARD_DEBUG("Failed to change UHS mode\n");
                return AM_HAL_STATUS_FAIL;
            }
        }
    }
    else
    {
        AM_HAL_CARD_DEBUG("Card Tpye is not supported!\n");
        return AM_HAL_STATUS_FAIL;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//*****************************************************************************
//
// Initialize the card instance function
//
//*****************************************************************************
uint32_t
am_hal_card_init(am_hal_card_t *pCard,
                 am_hal_card_type_e eType,
                 am_hal_card_pwr_ctrl_func pCardPwrCtrlFunc,
                 am_hal_card_pwr_ctrl_policy_e eCardPwrCtrlPolicy)
{
#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        AM_HAL_CARD_DEBUG("This card is not connected to a host\n");
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    //
    // Turn on the eMMC card power supply firstly or sd card power cycle
    //
    if (pCardPwrCtrlFunc != NULL)
    {
        AM_HAL_CARD_DEBUG("turn on the eMMC/sdcard power supply\n");
        pCardPwrCtrlFunc(AM_HAL_CARD_PWR_CYCLE);
    }

    pCard->pCardPwrCtrlFunc = pCardPwrCtrlFunc;
    pCard->eState = AM_HAL_CARD_STATE_PWRON;
    pCard->eCardPwrCtrlPolicy = eCardPwrCtrlPolicy;
    pCard->bCardTypeDetect = false;
    pCard->eType = eType;

    AM_HAL_CARD_DEBUG("card type is %d\n", pCard->eType);

    //
    // Detect card type
    //
    if ( pCard->eType == AM_HAL_CARD_TYPE_UNKNOWN )
    {
        AM_HAL_CARD_DEBUG("enter into card type detect\n");
        if ( am_hal_card_type_detect(pCard) != AM_HAL_STATUS_SUCCESS )
        {
            return AM_HAL_STATUS_FAIL;
        }

        pCard->bCardTypeDetect = true;
    }

    switch (pCard->eType)
    {
        case AM_HAL_CARD_TYPE_UNKNOWN:
            AM_HAL_CARD_DEBUG("card type is unknown:%d\n", pCard->eType);
            return AM_HAL_STATUS_FAIL;
            break;
        case AM_HAL_CARD_TYPE_MMC:
        case AM_HAL_CARD_TYPE_EMMC:
            if ( am_hal_card_mmc_init(pCard) != AM_HAL_STATUS_SUCCESS )
            {
                return AM_HAL_STATUS_FAIL;
            }
            break;
        case AM_HAL_CARD_TYPE_SDSC:
        case AM_HAL_CARD_TYPE_SDHC:
        case AM_HAL_CARD_TYPE_SDXC:
            if ( am_hal_sd_card_init(pCard, pCardPwrCtrlFunc) != AM_HAL_STATUS_SUCCESS )
            {
                return AM_HAL_STATUS_FAIL;
            }
            break;
        case AM_HAL_CARD_TYPE_SDIO:
            if ( am_hal_card_sdio_init(pCard) != AM_HAL_STATUS_SUCCESS )
            {
                return AM_HAL_STATUS_FAIL;
            }
            break;
        case AM_HAL_CARD_TYPE_COMBO:
            if ( am_hal_card_sd_init(pCard) != AM_HAL_STATUS_SUCCESS )
            {
                return AM_HAL_STATUS_FAIL;
            }
            break;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//*****************************************************************************
//
// De-Initialize the card instance function
//
//*****************************************************************************
uint32_t
am_hal_card_deinit(am_hal_card_t *pCard)
{
#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        AM_HAL_CARD_DEBUG("This card is not connected to a host\n");
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    //
    // Turn off the eMMC card power supply firstly
    //
    if (pCard->pCardPwrCtrlFunc != NULL)
    {
        AM_HAL_CARD_DEBUG("turn off the eMMC/sdcard power supply\n");
        pCard->pCardPwrCtrlFunc(AM_HAL_CARD_PWR_OFF);
    }

    memset((void *)pCard, 0x0, sizeof(am_hal_card_t));

    return AM_HAL_STATUS_SUCCESS;
}

static uint32_t
am_hal_card_block_rw(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf, bool bRead, bool bASync)
{
    uint32_t ui32Status;

    am_hal_card_host_t *pHost;
    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    pHost = pCard->pHost;

    //
    // Check if the start block number and block count is valid or not
    //
    if ( ui32Blk >= pCard->ui32MaxBlks || (ui32BlkCnt > 0xFFFF) || (ui32Blk + ui32BlkCnt) > pCard->ui32MaxBlks )
    {
        return AM_HAL_STATUS_OUT_OF_RANGE;
    }

    if ( pHost->eXferMode == AM_HAL_HOST_XFER_ADMA && ui32BlkCnt > pHost->ui32MaxADMA2BlkNums )
    {
        ui32BlkCnt = pHost->ui32MaxADMA2BlkNums;
    }

#ifndef ENABLE_SDHC_AUTO_CMD23_FEATURE
    //
    // Send CMD23 firstly for mulitple blocks transfer
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    if ( ui32BlkCnt > 1 )
    {
        cmd.ui8Idx = MMC_CMD_SET_BLOCK_COUNT;
        cmd.ui32Arg = ui32BlkCnt;
        cmd.ui32RespType = MMC_RSP_R1;

        if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
        {
            return ui32Status;
        }
    }
#endif

    memset((void *)&cmd, 0x0, sizeof(cmd));
    memset((void *)&cmd_data, 0x0, sizeof(cmd_data));
    if ( bRead )
    {
        cmd.ui8Idx = ui32BlkCnt > 1 ? MMC_CMD_READ_MULTIPLE_BLOCK : MMC_CMD_READ_SINGLE_BLOCK;
    }
    else
    {
        cmd.ui8Idx = ui32BlkCnt > 1 ? MMC_CMD_WRITE_MULTIPLE_BLOCK : MMC_CMD_WRITE_SINGLE_BLOCK;
    }

    cmd.ui32Arg = pCard->bHighCapcity ? ui32Blk : ui32Blk * pCard->ui32BlkSize;
    cmd.ui32RespType = MMC_RSP_R1;
    cmd.bASync = bASync;
#ifdef ENABLE_SDHC_AUTO_CMD23_FEATURE
    cmd.bAutoCMD23 = ui32BlkCnt > 1 ? true : false;
#endif

    cmd_data.ui8IovCnt = 0;
    cmd_data.pui8Buf = pui8Buf;
    cmd_data.ui32BlkCnt = ui32BlkCnt;
    cmd_data.ui32BlkSize = pCard->ui32BlkSize;
    cmd_data.dir = bRead ? AM_HAL_DATA_DIR_READ : AM_HAL_DATA_DIR_WRITE;

    if ( cmd.bASync )
    {
        pHost->AsyncCmd = cmd;
        pHost->AsyncCmdData = cmd_data;
    }

    ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);

    return ui32Status;
}

static uint32_t
am_hal_emmc_card_scatter_rw(am_hal_card_t *pCard, uint32_t ui32Blk, am_hal_card_iovec_t *pIoVec, uint8_t ui8IovCnt, bool bRead, bool bASync)
{
    uint32_t ui32Status;

    am_hal_card_host_t *pHost;
    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;
    uint32_t ui32BlkCnt = 0;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost || !pIoVec || !ui8IovCnt)
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    for (uint8_t i = 0; i < ui8IovCnt ; i++ )
    {
        if ( pIoVec[i].pIovBase == NULL  || pIoVec[i].ui32IovLen == 0 )
        {
            return AM_HAL_STATUS_INVALID_ARG;
        }

        if ( (pIoVec[i].ui32IovLen % pCard->ui32BlkSize) != 0 )
        {
            return AM_HAL_STATUS_INVALID_ARG;
        }

        ui32BlkCnt += pIoVec[i].ui32IovLen / pCard->ui32BlkSize;
    }

    pHost = pCard->pHost;

    //
    // Check transfer mode
    //
    if ( pHost->eXferMode != AM_HAL_HOST_XFER_ADMA )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    // Check if the start block number and block count is valid or not
    //
    if ( ui32Blk >= pCard->ui32MaxBlks || (ui32BlkCnt > 0xFFFF) || (ui32Blk + ui32BlkCnt) > pCard->ui32MaxBlks )
    {
        return AM_HAL_STATUS_OUT_OF_RANGE;
    }

    if ( ui32BlkCnt > pHost->ui32MaxADMA2BlkNums )
    {
        ui32BlkCnt = pHost->ui32MaxADMA2BlkNums;
    }

#ifndef ENABLE_SDHC_AUTO_CMD23_FEATURE
    //
    // Send CMD23 firstly for mulitple blocks transfer
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    if ( ui32BlkCnt > 1 )
    {
        cmd.ui8Idx = MMC_CMD_SET_BLOCK_COUNT;
        cmd.ui32Arg = ui32BlkCnt;
        cmd.ui32RespType = MMC_RSP_R1;

        if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
        {
            return ui32Status;
        }
    }
#endif

    memset((void *)&cmd, 0x0, sizeof(cmd));
    memset((void *)&cmd_data, 0x0, sizeof(cmd_data));
    if ( bRead )
    {
        cmd.ui8Idx = ui32BlkCnt > 1 ? MMC_CMD_READ_MULTIPLE_BLOCK : MMC_CMD_READ_SINGLE_BLOCK;
    }
    else
    {
        cmd.ui8Idx = ui32BlkCnt > 1 ? MMC_CMD_WRITE_MULTIPLE_BLOCK : MMC_CMD_WRITE_SINGLE_BLOCK;
    }

    cmd.ui32Arg = pCard->bHighCapcity ? ui32Blk : ui32Blk * pCard->ui32BlkSize;
    cmd.ui32RespType = MMC_RSP_R1;
    cmd.bASync = bASync;
#ifdef ENABLE_SDHC_AUTO_CMD23_FEATURE
    cmd.bAutoCMD23 = ui32BlkCnt > 1 ? true : false;
#endif

    cmd_data.ui32BlkCnt = ui32BlkCnt;
    cmd_data.ui32BlkSize = pCard->ui32BlkSize;
    cmd_data.dir = bRead ? AM_HAL_DATA_DIR_READ : AM_HAL_DATA_DIR_WRITE;
    cmd_data.pIov = pIoVec;
    cmd_data.ui8IovCnt = ui8IovCnt;

    if ( cmd.bASync )
    {
        pHost->AsyncCmd = cmd;
        pHost->AsyncCmdData = cmd_data;
    }

    ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);

    return ui32Status;
}

//*****************************************************************************
//
// synchronous block-oriented read function
//
//*****************************************************************************
uint32_t
am_hal_card_block_read_sync(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf)
{
    return am_hal_card_block_rw(pCard, ui32Blk, ui32BlkCnt, pui8Buf, true, false);
}

//*****************************************************************************
//
// synchronous block-oriented write function
//
//*****************************************************************************
uint32_t
am_hal_card_block_write_sync(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf)
{
    return am_hal_card_block_rw(pCard, ui32Blk, ui32BlkCnt, pui8Buf, false, false);
}

//*****************************************************************************
//
// emmc scatter write synchronous function
//
//*****************************************************************************
uint32_t
am_hal_emmc_card_scatter_write_sync(am_hal_card_t *pCard, uint32_t ui32Blk, am_hal_card_iovec_t *pIoVec, uint8_t ui8IovCnt)
{
    return am_hal_emmc_card_scatter_rw(pCard, ui32Blk, pIoVec, ui8IovCnt, false, false);
}

//*****************************************************************************
//
// emmc scatter read synchronous function
//
//*****************************************************************************
uint32_t
am_hal_emmc_card_scatter_read_sync(am_hal_card_t *pCard, uint32_t ui32Blk, am_hal_card_iovec_t *pIoVec, uint8_t ui8IovCnt)
{
    return am_hal_emmc_card_scatter_rw(pCard, ui32Blk, pIoVec, ui8IovCnt, true, false);
}

//*****************************************************************************
//
// asynchronous block-oriented read function
//
//*****************************************************************************
uint32_t
am_hal_card_block_read_async(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf)
{
    return am_hal_card_block_rw(pCard, ui32Blk, ui32BlkCnt, pui8Buf, true, true);
}

//*****************************************************************************
//
// asynchronous block-oriented write function
//
//*****************************************************************************
uint32_t
am_hal_card_block_write_async(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf)
{
    return am_hal_card_block_rw(pCard, ui32Blk, ui32BlkCnt, pui8Buf, false, true);
}

//*****************************************************************************
//
// emmc scatter write asynchronous function
//
//*****************************************************************************
uint32_t
am_hal_emmc_card_scatter_write_async(am_hal_card_t *pCard, uint32_t ui32Blk, am_hal_card_iovec_t *pIoVec, uint8_t ui8IovCnt)
{
    return am_hal_emmc_card_scatter_rw(pCard, ui32Blk, pIoVec, ui8IovCnt, false, true);
}

//*****************************************************************************
//
// emmc scatter read asynchronous function
//
//*****************************************************************************
uint32_t
am_hal_emmc_card_scatter_read_async(am_hal_card_t *pCard, uint32_t ui32Blk, am_hal_card_iovec_t *pIoVec, uint8_t ui8IovCnt)
{
    return am_hal_emmc_card_scatter_rw(pCard, ui32Blk, pIoVec, ui8IovCnt, true, true);
}

//*****************************************************************************
//
// block-oriented erase function
//
//*****************************************************************************
uint32_t
am_hal_card_block_erase(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, am_hal_card_erase_type_t erasetype, uint32_t ui32TimeoutMS)
{
    uint32_t ui32Status;
    am_hal_card_host_t *pHost;
    am_hal_card_cmd_t cmd;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    //
    // Check if the start block number and block count is valid or not
    //
    if ( ui32Blk >= pCard->ui32MaxBlks || ui32Blk + ui32BlkCnt > pCard->ui32MaxBlks )
    {
        return AM_HAL_STATUS_OUT_OF_RANGE;
    }

    if ( erasetype == AM_HAL_SECURE_ERASE || erasetype == AM_HAL_SECURE_TRIM1 || erasetype == AM_HAL_SECURE_TRIM2 )
    {
        if ( !(pCard->ui8SecureErase & SEC_ER_EN) )
        {
            AM_HAL_CARD_DEBUG("This device does not support Secure Erase or Trim\n");
            return AM_HAL_STATUS_INVALID_ARG;
        }
    }

    if ( erasetype == AM_HAL_TRIM || erasetype == AM_HAL_SECURE_TRIM1 || erasetype == AM_HAL_SECURE_TRIM2 )
    {
        if ( !(pCard->ui8SecureErase & SEC_GB_CL_EN) )
        {
            AM_HAL_CARD_DEBUG("This device does not support Trim operation\n");
            return AM_HAL_STATUS_INVALID_ARG;
        }
    }

    pHost = pCard->pHost;

    //
    // Define the erase start address
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_ERASE_GROUP_START;
    cmd.ui32Arg = pCard->bHighCapcity ? ui32Blk : ui32Blk * pCard->ui32BlkSize;
    cmd.ui32RespType = MMC_RSP_R1;

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    //
    // Define the erase end address
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_ERASE_GROUP_END;
    cmd.ui32Arg = pCard->bHighCapcity ? (ui32Blk + ui32BlkCnt - 1) : (ui32Blk + ui32BlkCnt - 1) * pCard->ui32BlkSize;
    cmd.ui32RespType = MMC_RSP_R1;

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    //
    // Do the erase job
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    cmd.bCheckBusyCmd = true;
    cmd.ui8Idx = MMC_CMD_ERASE;
    cmd.ui32Arg = (uint32_t)erasetype;
    cmd.ui32RespType = MMC_RSP_R1b;

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    return pHost->ops->card_busy(pHost->pHandle, ui32TimeoutMS);
}

//*****************************************************************************
//
// Get the card status function
//
//*****************************************************************************
uint32_t
am_hal_card_status(am_hal_card_t *pCard, uint32_t *pui32Status)
{
    uint32_t ui32Status;
    am_hal_card_cmd_t cmd;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState < AM_HAL_CARD_STATE_STDY )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_SEND_STATUS;
    cmd.ui32Arg = pCard->ui32RCA << 16;
    cmd.ui32RespType = MMC_RSP_R1;

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    if ( cmd.eError == AM_HAL_CMD_ERR_NONE )
    {
        *pui32Status = cmd.ui32Resp[0];
    }

    return ui32Status;
}

//*****************************************************************************
//
// Get the card state function
//
//*****************************************************************************
am_hal_card_state_e am_hal_card_state(am_hal_card_t *pCard)
{
    uint32_t ui32Status = 0;
    uint32_t ui32State;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_CARD_STATE_ERROR;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( am_hal_card_status(pCard, &ui32Status) != AM_HAL_STATUS_SUCCESS )
    {
        return AM_HAL_CARD_STATE_ERROR;
    }
    else
    {
        ui32State = ui32Status >> 9 & 0xF;
        if ( ui32State <= 10 )
        {
            pCard->eState = (am_hal_card_state_e)(ui32State + 2);
        }
        else
        {
            return AM_HAL_CARD_STATE_ERROR;
        }
    }

    return pCard->eState;
}

//*****************************************************************************
//
// CMD6 - Mode switch
//
//*****************************************************************************
uint32_t
am_hal_card_mode_switch(am_hal_card_t *pCard, uint32_t ui32Mode, uint32_t ui32Timeout)
{
    uint32_t ui32Status;
    uint32_t ui32CardStatus;
    am_hal_card_cmd_t cmd;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    am_hal_card_host_t *pHost = pCard->pHost;

    memset(&cmd, 0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_SWITCH;
    cmd.ui32Arg = ui32Mode;
    pCard->bExtCsdValid = 0;

    if ( pCard->eType == AM_HAL_CARD_TYPE_MMC || pCard->eType == AM_HAL_CARD_TYPE_EMMC )
    {
        cmd.bCheckBusyCmd = true;
        cmd.ui32RespType = MMC_RSP_R1b;
    }
    else
    {
        cmd.ui32RespType = MMC_RSP_R1;
    }

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    if ( pCard->eType == AM_HAL_CARD_TYPE_MMC || pCard->eType == AM_HAL_CARD_TYPE_EMMC )
    {
        if ((ui32Status = pHost->ops->card_busy(pHost->pHandle, DEFAULT_GET_STATUS_TIMEOUT_MS)) != AM_HAL_STATUS_SUCCESS)
        {
            return ui32Status;
        }
    }

    ui32CardStatus = 0x0;

    do
    {
        ui32Status = am_hal_card_status(pCard, &ui32CardStatus);
        if (ui32Status == AM_HAL_STATUS_SUCCESS && (ui32CardStatus & MMC_STATUS_SWITCH_ERROR))
        {
            AM_HAL_CARD_DEBUG("switch failed - mode is 0x%x !\n", ui32Mode);
            return AM_HAL_STATUS_FAIL;
        }
        if (ui32Status == AM_HAL_STATUS_SUCCESS && (ui32CardStatus & MMC_STATUS_RDY_FOR_DATA))
        {
            AM_HAL_CARD_DEBUG("switch succeeded - mode is 0x%x !\n", ui32Mode);
            return AM_HAL_STATUS_SUCCESS;
        }
        am_util_delay_ms(1);
    } while (ui32Timeout--);

    return ui32Timeout ? AM_HAL_STATUS_SUCCESS : AM_HAL_STATUS_FAIL;
}

//*****************************************************************************
//
// Read blocks of data from the card (GEN_CMD)
//
//*****************************************************************************
static uint32_t
am_hal_card_cmd56_read(am_hal_card_t *pCard, uint32_t ui32Arg, uint8_t *pui8Buf, bool bASync)
{
    uint32_t ui32Status;

    am_hal_card_host_t *pHost;
    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    pHost = pCard->pHost;

    memset((void *)&cmd, 0x0, sizeof(cmd));
    cmd.ui8Idx = MMC_CMD_CMD56;
    cmd.ui32Arg = ui32Arg;
    cmd.ui32RespType = MMC_RSP_R1;
    cmd.bASync = bASync;

    memset((void *)&cmd_data, 0x0, sizeof(cmd_data));
    cmd_data.ui8IovCnt = 0;
    cmd_data.pui8Buf = pui8Buf;
    cmd_data.ui32BlkCnt = 1;
    cmd_data.ui32BlkSize = 512;
    cmd_data.dir = AM_HAL_DATA_DIR_READ;

    if ( cmd.bASync )
    {
        pHost->AsyncCmd = cmd;
        pHost->AsyncCmdData = cmd_data;
    }

    ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);

    return ui32Status;
}

//*****************************************************************************
//
// Read blocks of data from the card (GEN_CMD) asynchronously
//
//*****************************************************************************
uint32_t
am_hal_card_cmd56_read_async(am_hal_card_t *pCard, uint32_t ui32Arg, uint8_t *pui8Buf)
{
    return am_hal_card_cmd56_read(pCard, ui32Arg, pui8Buf, true);
}

//*****************************************************************************
//
// Read blocks of data from the card (GEN_CMD) synchronously
//
//*****************************************************************************
uint32_t
am_hal_card_cmd56_read_sync(am_hal_card_t *pCard, uint32_t ui32Arg, uint8_t *pui8Buf)
{
    return am_hal_card_cmd56_read(pCard, ui32Arg, pui8Buf, false);
}

//*****************************************************************************
//
// register the card event call back function
//
//*****************************************************************************
uint32_t
am_hal_card_register_evt_callback(am_hal_card_t *pCard, am_hal_host_event_cb_t pfunCallback)
{
#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        AM_HAL_CARD_DEBUG("both Card and Host are needed to be initialized firstly\n");
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    pCard->pHost->pfunEvtCallback = pfunCallback;

    return AM_HAL_STATUS_SUCCESS;
}

//*****************************************************************************
//
// card cid information parse function
//
//*****************************************************************************
uint32_t
am_hal_card_get_cid_field(am_hal_card_t *pCard, uint16_t ui16Offset, uint8_t ui8Size)
{
#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->bCidValid )
    {
        return 0xFFFFFFFF;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    return am_hal_unstuff_bits(pCard->ui32CID, ui16Offset, ui8Size);


}

//*****************************************************************************
//
// card csd information parse function
//
//*****************************************************************************
uint32_t
am_hal_card_get_csd_field(am_hal_card_t *pCard, uint16_t ui16Offset, uint8_t ui8Size)
{
#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->bCsdValid )
    {
        return 0xFFFFFFFF;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    return am_hal_unstuff_bits(pCard->ui32CSD, ui16Offset, ui8Size);

}

//*****************************************************************************
//
// card ext csd information parse function
//
//*****************************************************************************
uint32_t
am_hal_card_get_ext_csd_field(am_hal_card_t *pCard, uint16_t ui16Offset, uint8_t ui8Size)
{

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost)
    {
        return 0xFFFFFFFF;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard->bExtCsdValid )
    {
        if (am_hal_sdmmc_cmd8_send_ext_csd(pCard) != AM_HAL_CMD_ERR_NONE)
        {
            return 0xFFFFFFFF;
        }
        pCard->bExtCsdValid = 1;
    }

    return am_hal_unstuff_bytes(pCard->ui32ExtCSD, ui16Offset, ui8Size);

}

//*****************************************************************************
//
// Get the card information function
//
//*****************************************************************************
uint32_t
am_hal_card_get_info(am_hal_card_t *pCard, am_hal_card_info_t *pCardInfo)
{
#ifndef AM_HAL_DISABLE_API_VALIDATION
    if ( !pCard || !pCard->pHost )
    {
        AM_HAL_CARD_DEBUG("both Card and Host are needed to be initialized firstly\n");
        return AM_HAL_STATUS_INVALID_ARG;
    }
#endif // AM_HAL_DISABLE_API_VALIDATION

    pCardInfo->eType        = pCard->eType;
    pCardInfo->ui16CmdClass = pCard->ui16CmdClass;
    pCardInfo->ui32RCA       = pCard->ui32RCA;
    pCardInfo->ui32MaxBlks  = pCard->ui32MaxBlks;
    pCardInfo->ui32BlkSize  = pCard->ui32BlkSize;
    if ( pCard->bHighCapcity )
    {
        pCardInfo->ui32LogiMaxBlks = pCard->ui32MaxBlks;
        pCardInfo->ui32LogiBlkSize = pCard->ui32BlkSize;
    }
    else
    {
        pCardInfo->ui32LogiMaxBlks = (pCard->ui32MaxBlks) * ((pCard->ui32BlkSize) / 512);
        pCardInfo->ui32LogiBlkSize = 512;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Read single byte from SDIO card
//
uint32_t
am_hal_sdio_card_byte_read(am_hal_card_t *pCard, uint32_t ui32Func, uint32_t ui32Addr, uint8_t *pui8Data)
{
    uint32_t ui32Status;

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, ui32Addr, 0, pui8Data, true);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to read byte from SDIO card\n");
        return ui32Status;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Write single byte to SDIO card
//
uint32_t
am_hal_sdio_card_byte_write(am_hal_card_t *pCard, uint32_t ui32Func, uint32_t ui32Addr, uint8_t ui8Data)
{
    uint32_t ui32Status;

    ui32Status = am_hal_sdio_cmd52_io_rw_direct(pCard, ui32Func, ui32Addr, ui8Data, NULL, false);
    if ( ui32Status != AM_HAL_STATUS_SUCCESS )
    {
        AM_HAL_CARD_DEBUG("Fail to write byte to SDIO card\n");
        return ui32Status;
    }

    return AM_HAL_STATUS_SUCCESS;
}

//
//! Read multiple bytes/blocks from SDIO card in sync/blocking mode
//
uint32_t
am_hal_sdio_card_multi_bytes_read_sync(am_hal_card_t *pCard,
                                       uint32_t ui32Func,
                                       uint32_t ui32Addr,
                                       uint8_t *pui8Buf,
                                       uint32_t ui32BlkCnt,
                                       uint32_t ui32BlkSize,
                                       bool bIncrAddr)
{
   return am_hal_sdio_cmd53_io_rw_extended(pCard, ui32Func, ui32Addr, pui8Buf, ui32BlkCnt, ui32BlkSize, bIncrAddr, true, false);
}

//
//! Write multiple bytes/blocks to SDIO card in sync/blocking mode
//
uint32_t
am_hal_sdio_card_multi_bytes_write_sync(am_hal_card_t *pCard,
                                        uint32_t ui32Func,
                                        uint32_t ui32Addr,
                                        uint8_t *pui8Buf,
                                        uint32_t ui32BlkCnt,
                                        uint32_t ui32BlkSize,
                                        bool bIncrAddr)
{
   return am_hal_sdio_cmd53_io_rw_extended(pCard, ui32Func, ui32Addr, pui8Buf, ui32BlkCnt, ui32BlkSize, bIncrAddr, false, false);
}

//
//! Read multiple bytes/blocks from SDIO card in async/non-blocking mode
//
uint32_t
am_hal_sdio_card_multi_bytes_read_async(am_hal_card_t *pCard,
                                        uint32_t ui32Func,
                                        uint32_t ui32Addr,
                                        uint8_t *pui8Buf,
                                        uint32_t ui32BlkCnt,
                                        uint32_t ui32BlkSize,
                                        bool bIncrAddr)
{
   return am_hal_sdio_cmd53_io_rw_extended(pCard, ui32Func, ui32Addr, pui8Buf, ui32BlkCnt, ui32BlkSize, bIncrAddr, true, true);
}

//
//! Write multiple bytes/blocks to SDIO card in async/non-blocking mode
//
uint32_t
am_hal_sdio_card_multi_bytes_write_async(am_hal_card_t *pCard,
                                         uint32_t ui32Func,
                                         uint32_t ui32Addr,
                                         uint8_t *pui8Buf,
                                         uint32_t ui32BlkCnt,
                                         uint32_t ui32BlkSize,
                                         bool bIncrAddr)
{
   return am_hal_sdio_cmd53_io_rw_extended(pCard, ui32Func, ui32Addr, pui8Buf, ui32BlkCnt, ui32BlkSize, bIncrAddr, false, true);
}

uint32_t
am_hal_sdio_card_calibrate(am_hal_host_inst_index_e eIndex,
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
    am_hal_card_host_t *pSdhcCardHost;
    am_hal_card_t SdioCard;

    uint32_t ui32Status;
    uint32_t ui32BlkSize;
    uint32_t ui32FuncNum = 1;

    uint32_t i;
    uint32_t ui32BufLength;
    uint8_t  ui8TxDelay = 0;
    uint8_t  ui8RxDelay = 0;
    uint32_t ui32TxResult = 0;
    uint32_t ui32RxResultArray[16] = {0};
    uint32_t ui32MisMatchCnt = 0;
    uint32_t ui32WrErrCnt = 0;

#ifdef AM_DEBUG_PRINTF
    if (eUHSMode == AM_HAL_HOST_UHS_DDR50)
    {
        AM_HAL_CARD_DEBUG("SDIO:%d Card Timing Scan for DDR %d Hz %d bit mode...\n", eIndex, ui32Clock, eBusWidth);
    }
    else
    {
        AM_HAL_CARD_DEBUG("SDIO:%d Card Timing Scan for SDR %d Hz %d bit mode...\n", eIndex, ui32Clock, eBusWidth);
    }
#endif

    for (int j = 0; j < SDIO_SCAN_TXDELAY_MAX; j++)
    {
        ui32MisMatchCnt = 0;
        ui32WrErrCnt = 0;

        for (int k = 0; k < SDIO_SCAN_RXDELAY_MAX; k++)
        {
            if ( (ui32MisMatchCnt >= SDIO_CARD_MISMATCH_CNT_MAX)
                 || (ui32WrErrCnt >= SDIO_CARD_MISMATCH_CNT_MAX) )
            {
                break;
            }

            //
            // Configure SDIO PINs.
            //
            pSdioCardReset();

            //
            // Get the uderlying SDHC card host instance
            //
            pSdhcCardHost = am_hal_get_card_host((am_hal_host_inst_index_e)(AM_HAL_SDHC_CARD_HOST + eIndex), true);
            if ( pSdhcCardHost == NULL )
            {
                AM_HAL_CARD_DEBUG("No such card host and stop\n");
                return AM_HAL_STATUS_FAIL;
            }

            ui8TxRxDelays[0] = j;
            ui8TxRxDelays[1] = k;
            AM_HAL_CARD_DEBUG("Test TX delay setting SDIOOTAPDLYSEL = %d, RX delay setting SDIOITAPDLYSEL = %d\n", j, k);
            am_hal_card_host_set_txrx_delay(pSdhcCardHost, ui8TxRxDelays);

            //
            // check if card is present
            //
            if ( am_hal_card_host_find_card(pSdhcCardHost, &SdioCard) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("No card is present now\n");
                return AM_HAL_STATUS_FAIL;
            }

            if ( am_hal_card_init(&SdioCard, AM_HAL_CARD_TYPE_SDIO, NULL, AM_HAL_CARD_PWR_CTRL_SDHC_OFF) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("card initialization failed\n");
                continue;
            }

            if ( am_hal_card_cfg_set(&SdioCard, AM_HAL_CARD_TYPE_SDIO,
                eBusWidth, ui32Clock, eIoVoltage, AM_HAL_HOST_UHS_NONE) != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("sdio card config failed\n");
                continue;
            }

            //
            // Enable SDIO card function 1
            //
            ui32Status = am_hal_sdio_card_func_enable(&SdioCard, ui32FuncNum);
            if ( ui32Status != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Enable function 1 failed. Status=0x%x\n", ui32Status);
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Enable SDIO card function 1 interrupt
            //
            ui32Status = am_hal_sdio_card_func_interrupt_enable(&SdioCard, ui32FuncNum);
            if ( ui32Status != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Enable function 1 interrupt failed. Status=0x%x\n", ui32Status);
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Set SDIO card function 1 block size
            //
            ui32BlkSize = 256;
            ui32Status = am_hal_sdio_card_set_block_size(&SdioCard, ui32FuncNum, ui32BlkSize);
            if ( ui32Status != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Fail to set SDIO card function 1 block size. Status=0x%x\n", ui32Status);
                return AM_HAL_STATUS_FAIL;
            }

            //
            // Get SDIO card function 1 block size
            //
            ui32Status = am_hal_sdio_card_get_block_size(&SdioCard, ui32FuncNum, &ui32BlkSize);
            if ( ui32Status != AM_HAL_STATUS_SUCCESS )
            {
                AM_HAL_CARD_DEBUG("Fail to check SDIO card function 1 block size. Status=0x%x\n", ui32Status);
                return AM_HAL_STATUS_FAIL;
            }

            ui32BufLength = ui32BlockCnt*ui32BlkSize;

            for (i = 0; i < ui32BufLength; i++)
            {
                ui8CalibBuf[i] = i % 256;
            }

            ui32Status = am_hal_sdio_card_multi_bytes_write_sync(&SdioCard, ui32FuncNum, ui32StartAddr, (uint8_t *)ui8CalibBuf, ui32BlockCnt, ui32BlkSize, true);
            if ( (ui32Status & 0xffff) != AM_HAL_STATUS_SUCCESS )
            {
                ui32WrErrCnt ++;
                AM_HAL_CARD_DEBUG("Sdio Card calibration write fail. Status=0x%x\n", ui32Status & 0xffff);
                continue;
            }

            memset((void *)ui8CalibBuf, 0x0, ui32BufLength);

            ui32Status = am_hal_sdio_card_multi_bytes_read_sync(&SdioCard, ui32FuncNum, ui32StartAddr, (uint8_t *)ui8CalibBuf, ui32BlockCnt, ui32BlkSize, true);
            if ( (ui32Status & 0xffff)  != AM_HAL_STATUS_SUCCESS )
            {
                ui32WrErrCnt ++;
                AM_HAL_CARD_DEBUG("Sdio Card calibration read fail. Status=0x%x\n", ui32Status & 0xffff);
                continue;
            }

            for (i = 0; i < ui32BufLength; i++)
            {
                if (ui8CalibBuf[i] != i % 256)
                {
                    ui32MisMatchCnt ++;
                    break;
                }
            }

            if (i == ui32BufLength)
            {
                ui32RxResultArray[j] |= 0x01 << k;
                AM_HAL_CARD_DEBUG("TX Delay SDIOOTAPDLYSEL = %d, RX Delay SDIOITAPDLYSEL = %d works\n", j, k);
            }
            else
            {
                AM_HAL_CARD_DEBUG("TX Delay SDIOOTAPDLYSEL = %d, RX Delay SDIOITAPDLYSEL = %d doesn't work\n", j, k);
            }
        }
    }

#ifdef AM_DEBUG_PRINTF
    AM_HAL_CARD_DEBUG("\nSDIO TX RX Delay Scan Result:\n");
    for (i = 0; i < SDIO_SCAN_TXDELAY_MAX; i++ )
    {
        AM_HAL_CARD_DEBUG("TX_Delay = %2d, RX_Delay Window = 0x%08X\n", i, ui32RxResultArray[i]);
    }
#endif

    uint32_t ui32Result = 0;
    for (i = 0; i < SDIO_SCAN_TXDELAY_MAX; i++ )
    {
        ui32Result = count_consecutive_ones(&ui32RxResultArray[i]);
        if ( ui32Result >= SDIO_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH )
        {
            ui32TxResult |= 0x01 << i;
        }
    }

    //
    // Check consecutive passing settings
    //
    if ( ui32TxResult == 0 )
    {
        //
        // No acceptable window
        //
        return AM_HAL_STATUS_FAIL;
    }
    else
    {
        ui32Result = count_consecutive_ones(&ui32TxResult);
        if ( ui32Result < SDIO_TIMING_SCAN_MIN_ACCEPTANCE_LENGTH )
        {
            //
            // No acceptable window
            //
            return AM_HAL_STATUS_FAIL;
        }
    }

    //
    // Find TX Delay Value
    //
    ui8TxDelay = find_mid_point(&ui32TxResult);

    AM_HAL_CARD_DEBUG("\nSDIO Card Timing Scan found a RX delay window %X with TX delay Setting = %d.\n", ui32RxResultArray[ui8TxDelay], ui8TxDelay);

    //
    // Find RX Delay Value
    //
    ui8RxDelay = find_mid_point(&ui32RxResultArray[ui8TxDelay]);
    ui8TxRxDelays[0] = ui8TxDelay;
    ui8TxRxDelays[1] = ui8RxDelay;

    AM_HAL_CARD_DEBUG("Timing Scan set the SDIO TX delay to %d and RX delay to %d.\n", ui8TxDelay, ui8RxDelay);

    //
    // Force the card host to reset with the calibration settings
    //
    pSdhcCardHost = am_hal_get_card_host(eIndex, true);

    if (pSdhcCardHost == NULL)
    {
        AM_HAL_CARD_DEBUG("No such card host and stop\n");
        return AM_HAL_STATUS_FAIL;
    }
    am_hal_card_host_set_txrx_delay(pSdhcCardHost, ui8TxRxDelays);

    return AM_HAL_STATUS_SUCCESS;
}

uint32_t
am_hal_card_block_rpmb_rw(am_hal_card_t *pCard, uint8_t *pui8Buf, bool bRead, bool bRelWrite)
{
    uint32_t ui32Status;

    am_hal_card_host_t *pHost;
    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    pHost = pCard->pHost;

    //
    // Send CMD23 firstly for mulitple blocks transfer
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));

    cmd.ui8Idx = MMC_CMD_SET_BLOCK_COUNT;
    cmd.ui32RespType = MMC_RSP_R1;
    cmd.ui32Arg = 1;

    if ( bRelWrite )
    {
        cmd.ui32Arg |= ((uint32_t)0x01 << 31);
    }

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    memset((void *)&cmd, 0x0, sizeof(cmd));
    memset((void *)&cmd_data, 0x0, sizeof(cmd_data));
    if ( bRead )
    {
        cmd.ui8Idx = MMC_CMD_READ_MULTIPLE_BLOCK;
    }
    else
    {
        cmd.ui8Idx = MMC_CMD_WRITE_MULTIPLE_BLOCK;
    }

    cmd.ui32Arg = 0;
    cmd.ui32RespType = MMC_RSP_R1;
    cmd.bASync = false;
    cmd.bAutoCMD23 = false;

    cmd_data.ui8IovCnt = 0;
    cmd_data.ui32BlkCnt = 1;
    cmd_data.pui8Buf = pui8Buf;
    cmd_data.ui32BlkSize = 512;
    cmd_data.dir = bRead ? AM_HAL_DATA_DIR_READ : AM_HAL_DATA_DIR_WRITE;

    ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);

    return ui32Status;
}

static uint32_t
am_hal_sd_card_block_rw(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf, bool bRead, bool bASync)
{
    uint32_t ui32Status;

    am_hal_card_host_t *pHost;
    am_hal_card_cmd_t cmd;
    am_hal_card_cmd_data_t cmd_data;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    pHost = pCard->pHost;

    //
    // Check if the start block number and block count is valid or not
    //
    if ( ui32Blk >= pCard->ui32MaxBlks || (ui32BlkCnt > 0xFFFF) || (ui32Blk + ui32BlkCnt) > pCard->ui32MaxBlks )
    {
        return AM_HAL_STATUS_OUT_OF_RANGE;
    }

    if ( pHost->eXferMode == AM_HAL_HOST_XFER_ADMA && ui32BlkCnt > pHost->ui32MaxADMA2BlkNums )
    {
        ui32BlkCnt = pHost->ui32MaxADMA2BlkNums;
    }

    memset((void *)&cmd, 0x0, sizeof(cmd));
    memset((void *)&cmd_data, 0x0, sizeof(cmd_data));
    if ( bRead )
    {
        cmd.ui8Idx = ui32BlkCnt > 1 ? MMC_CMD_READ_MULTIPLE_BLOCK : MMC_CMD_READ_SINGLE_BLOCK;
    }
    else
    {
        cmd.ui8Idx = ui32BlkCnt > 1 ? MMC_CMD_WRITE_MULTIPLE_BLOCK : MMC_CMD_WRITE_SINGLE_BLOCK;
    }

    cmd.ui32Arg = pCard->bHighCapcity ? ui32Blk : ui32Blk * pCard->ui32BlkSize;
    cmd.ui32RespType = MMC_RSP_R1;
    cmd.bASync = bASync;
    cmd.bAutoCMD12 = ui32BlkCnt > 1 ? true : false;

    cmd_data.ui8IovCnt = 0;
    cmd_data.pui8Buf = pui8Buf;
    cmd_data.ui32BlkCnt = ui32BlkCnt;
    cmd_data.ui32BlkSize = pCard->ui32BlkSize;
    cmd_data.dir = bRead ? AM_HAL_DATA_DIR_READ : AM_HAL_DATA_DIR_WRITE;

    if ( cmd.bASync )
    {
        pHost->AsyncCmd = cmd;
        pHost->AsyncCmdData = cmd_data;
    }

    ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, &cmd_data);

    return ui32Status;
}

//*****************************************************************************
//
// SD card synchronous block-oriented read function
//
//*****************************************************************************
uint32_t
am_hal_sd_card_block_read_sync(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf)
{
    return am_hal_sd_card_block_rw(pCard, ui32Blk, ui32BlkCnt, pui8Buf, true, false);
}

//*****************************************************************************
//
// SD card synchronous block-oriented write function
//
//*****************************************************************************
uint32_t
am_hal_sd_card_block_write_sync(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf)
{
    if ( pCard->sdcard.bWrProtect )
    {
        AM_HAL_CARD_DEBUG("SD Card is protected, fail to write sd card\n");
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    return am_hal_sd_card_block_rw(pCard, ui32Blk, ui32BlkCnt, pui8Buf, false, false);
}

//*****************************************************************************
//
// SD card asynchronous block-oriented read function
//
//*****************************************************************************
uint32_t
am_hal_sd_card_block_read_async(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf)
{
    return am_hal_sd_card_block_rw(pCard, ui32Blk, ui32BlkCnt, pui8Buf, true, true);
}

//*****************************************************************************
//
// SD card asynchronous block-oriented write function
//
//*****************************************************************************
uint32_t
am_hal_sd_card_block_write_async(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint8_t *pui8Buf)
{
    if ( pCard->sdcard.bWrProtect )
    {
        AM_HAL_CARD_DEBUG("SD Card is protected, fail to write sd card\n");
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    return am_hal_sd_card_block_rw(pCard, ui32Blk, ui32BlkCnt, pui8Buf, false, true);
}

//*****************************************************************************
//
// SD card block-oriented erase function
//
//*****************************************************************************
uint32_t
am_hal_sd_card_block_erase(am_hal_card_t *pCard, uint32_t ui32Blk, uint32_t ui32BlkCnt, uint32_t ui32TimeoutMS)
{
    uint32_t ui32Status;
    am_hal_card_host_t *pHost;
    am_hal_card_cmd_t cmd;

#ifndef AM_HAL_DISABLE_API_VALIDATION

    if ( !pCard || !pCard->pHost )
    {
        return AM_HAL_STATUS_INVALID_ARG;
    }

#endif // AM_HAL_DISABLE_API_VALIDATION

    if ( pCard->eState != AM_HAL_CARD_STATE_TRANS )
    {
        return AM_HAL_STATUS_INVALID_OPERATION;
    }

    //
    // Check if the start block number and block count is valid or not
    //
    if ( ui32Blk >= pCard->ui32MaxBlks || ui32Blk + ui32BlkCnt > pCard->ui32MaxBlks )
    {
        return AM_HAL_STATUS_OUT_OF_RANGE;
    }

   pHost = pCard->pHost;

    //
    // Define the erase start address
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    cmd.ui8Idx = SD_CMD_ERASE_WR_BLK_START;
    cmd.ui32Arg = pCard->bHighCapcity ? ui32Blk : ui32Blk * pCard->ui32BlkSize;
    cmd.ui32RespType = MMC_RSP_R1;

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    //
    // Define the erase end address
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    cmd.ui8Idx = SD_CMD_ERASE_WR_BLK_END;
    cmd.ui32Arg = pCard->bHighCapcity ? (ui32Blk + ui32BlkCnt - 1) : (ui32Blk + ui32BlkCnt - 1) * pCard->ui32BlkSize;
    cmd.ui32RespType = MMC_RSP_R1;

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    //
    // Do the erase job
    //
    memset((void *)&cmd, 0x0, sizeof(cmd));
    cmd.bCheckBusyCmd = true;
    cmd.ui8Idx = MMC_CMD_ERASE;
    cmd.ui32Arg = 0;
    cmd.ui32RespType = MMC_RSP_R1b;

    if ( (ui32Status = pHost->ops->execute_cmd(pHost->pHandle, &cmd, NULL)) != AM_HAL_STATUS_SUCCESS )
    {
        return ui32Status;
    }

    return pHost->ops->card_busy(pHost->pHandle, ui32TimeoutMS);
}

//*****************************************************************************
//
// End Doxygen group.
//! @}
//
//*****************************************************************************
