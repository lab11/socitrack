/*************************************************************************************************/
/*!
 *  \file   hci_evt.c
 *
 *  \brief  HCI event module.
 *
 *          $Date: 2017-06-15 16:53:37 -0500 (Thu, 15 Jun 2017) $
 *          $Revision: 12305 $
 *
 *  Copyright (c) 2009-2017 ARM Ltd., all rights reserved.
 *  ARM Ltd. confidential and proprietary.
 *
 *  IMPORTANT.  Your use of this file is governed by a Software License Agreement
 *  ("Agreement") that must be accepted in order to download or otherwise receive a
 *  copy of this file.  You may not use or copy this file for any purpose other than
 *  as described in the Agreement.  If you do not agree to all of the terms of the
 *  Agreement do not use this file and delete all copies in your possession or control;
 *  if you do not have a copy of the Agreement, you must contact ARM Ltd. prior
 *  to any use, copying or further distribution of this software.
 */
/*************************************************************************************************/

#include <string.h>
#include "wsf_types.h"
#include "wsf_buf.h"
#include "wsf_trace.h"
#include "wsf_math.h"
#include "bstream.h"
#include "hci_api.h"
#include "hci_main.h"
#include "hci_evt.h"
#include "hci_cmd.h"
#include "hci_core.h"

/**************************************************************************************************
  Macros
**************************************************************************************************/

/* Maximum number of reports that can fit in an advertising report event */
#define HCI_MAX_REPORTS               15

/* Length of fixed parameters in an advertising report event */
#define HCI_LE_ADV_REPORT_FIXED_LEN   2

/* Length of fixed parameters in each individual report */
#define HCI_LE_ADV_REPORT_INDIV_LEN   10

/**************************************************************************************************
  Data Types
**************************************************************************************************/

/* Event parsing function type */
typedef void (*hciEvtParse_t)(hciEvt_t *pMsg, uint8_t *p, uint8_t len);

/**************************************************************************************************
  Local Declarations
**************************************************************************************************/

/* Event parsing functions */
static void hciEvtParseLeConnCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeEnhancedConnCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseDisconnectCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeConnUpdateCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeCreateConnCancelCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadRssiCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadChanMapCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadTxPwrLvlCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadRemoteVerInfoCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadLeRemoteFeatCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeLtkReqReplCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeLtkReqNegReplCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseEncKeyRefreshCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseEncChange(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeLtkReq(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseVendorSpecCmdStatus(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseVendorSpecCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseVendorSpec(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseHwError(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeEncryptCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeRandCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeAddDevToResListCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeRemDevFromResListCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeClearResListCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeReadPeerResAddrCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeReadLocalResAddrCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeSetAddrResEnableCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseRemConnParamRepCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseRemConnParamNegRepCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadDefDataLenCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseWriteDefDataLenCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseSetDataLenCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadMaxDataLenCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseRemConnParamReq(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseDataLenChange(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadPubKeyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseGenDhKeyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseWriteAuthTimeoutCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseAuthTimeoutExpiredEvt(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadPhyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseSetDefPhyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParsePhyUpdateCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeScanTimeout(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeAdvSetTerm(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeScanReqRcvd(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLePerAdvSyncEst(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLePerAdvSyncLost(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);

static void hciEvtProcessLeConnIQReport(uint8_t *p, uint8_t len);
static void hciEvtProcessLeConlessIQReport(uint8_t *p, uint8_t len);
static void hciEvtParseLeSetConnCteRcvParm(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeSetConnCteTxParm(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeConnCteReqEn(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeConnCteRspEn(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeCisEst(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeCisReq(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeReqPeerScaCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeSetCigParamsCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeRemoveCigCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeSetupIsoDataPathCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeRemoveIsoDataPathCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseConfigDataPathCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadLocalSupCodecsCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadLocalSupCodecCapCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseReadLocalSupCtrDlyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeCreateBigCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeTerminateBigCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeBigSyncEst(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeBigSyncLost(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeBigTermSyncCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len);
static void hciEvtParseLeBigInfoAdvRpt(hciEvt_t *pMsg, uint8_t *p, uint8_t len);


/**************************************************************************************************
  Local Variables
**************************************************************************************************/

/* Event parsing function lookup table, indexed by internal callback event value */
static const hciEvtParse_t hciEvtParseFcnTbl[] =
{
  NULL,
  hciEvtParseLeConnCmpl,
  hciEvtParseLeEnhancedConnCmpl,
  hciEvtParseDisconnectCmpl,
  hciEvtParseLeConnUpdateCmpl,
  hciEvtParseLeCreateConnCancelCmdCmpl,
  NULL,
  hciEvtParseReadRssiCmdCmpl,
  hciEvtParseReadChanMapCmdCmpl,
  hciEvtParseReadTxPwrLvlCmdCmpl,
  hciEvtParseReadRemoteVerInfoCmpl,
  hciEvtParseReadLeRemoteFeatCmpl,
  hciEvtParseLeLtkReqReplCmdCmpl,
  hciEvtParseLeLtkReqNegReplCmdCmpl,
  hciEvtParseEncKeyRefreshCmpl,
  hciEvtParseEncChange,
  hciEvtParseLeLtkReq,
  hciEvtParseVendorSpecCmdStatus,
  hciEvtParseVendorSpecCmdCmpl,
  hciEvtParseVendorSpec,
  hciEvtParseHwError,
  hciEvtParseLeAddDevToResListCmdCmpl,
  hciEvtParseLeRemDevFromResListCmdCmpl,
  hciEvtParseLeClearResListCmdCmpl,
  hciEvtParseLeReadPeerResAddrCmdCmpl,
  hciEvtParseLeReadLocalResAddrCmdCmpl,
  hciEvtParseLeSetAddrResEnableCmdCmpl,
  hciEvtParseLeEncryptCmdCmpl,
  hciEvtParseLeRandCmdCmpl,
  hciEvtParseRemConnParamRepCmdCmpl,
  hciEvtParseRemConnParamNegRepCmdCmpl,
  hciEvtParseReadDefDataLenCmdCmpl,
  hciEvtParseWriteDefDataLenCmdCmpl,
  hciEvtParseSetDataLenCmdCmpl,
  hciEvtParseReadMaxDataLenCmdCmpl,
  hciEvtParseRemConnParamReq,
  hciEvtParseDataLenChange,
  hciEvtParseReadPubKeyCmdCmpl,
  hciEvtParseGenDhKeyCmdCmpl,
  hciEvtParseWriteAuthTimeoutCmdCmpl,
  hciEvtParseAuthTimeoutExpiredEvt,
  hciEvtParseReadPhyCmdCmpl,
  hciEvtParseSetDefPhyCmdCmpl,
  hciEvtParsePhyUpdateCmpl,
  NULL,
  hciEvtParseLeScanTimeout,
  hciEvtParseLeAdvSetTerm,
  hciEvtParseLeScanReqRcvd,
  hciEvtParseLePerAdvSyncEst,
  NULL,
  hciEvtParseLePerAdvSyncLost,
  NULL,
  hciEvtParseLeCmdCmpl,
  hciEvtParseLeCmdCmpl,
  hciEvtParseLeCmdCmpl,
  hciEvtParseLeCmdCmpl,
  hciEvtParseLeCmdCmpl,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  hciEvtParseLeSetConnCteRcvParm,
  hciEvtParseLeSetConnCteTxParm,
  hciEvtParseLeConnCteReqEn,
  hciEvtParseLeConnCteRspEn,
  hciEvtParseLeCisEst,
  hciEvtParseLeCisReq,
  hciEvtParseDisconnectCmpl,
  hciEvtParseLeReqPeerScaCmpl,
  hciEvtParseLeSetCigParamsCmdCmpl,
  hciEvtParseLeRemoveCigCmdCmpl,
  hciEvtParseLeSetupIsoDataPathCmdCmpl,
  hciEvtParseLeRemoveIsoDataPathCmdCmpl,
  hciEvtParseConfigDataPathCmdCmpl,
  hciEvtParseReadLocalSupCodecsCmdCmpl,
  hciEvtParseReadLocalSupCodecCapCmdCmpl,
  hciEvtParseReadLocalSupCtrDlyCmdCmpl,
  hciEvtParseLeCreateBigCmpl,
  hciEvtParseLeTerminateBigCmpl,
  hciEvtParseLeBigSyncEst,
  hciEvtParseLeBigSyncLost,
  hciEvtParseLeBigTermSyncCmpl,
  hciEvtParseLeBigInfoAdvRpt
};

/* HCI event structure length table, indexed by internal callback event value */
static const uint8_t hciEvtCbackLen[] =
{
  sizeof(wsfMsgHdr_t),
  sizeof(hciLeConnCmplEvt_t),
  sizeof(hciLeConnCmplEvt_t),
  sizeof(hciDisconnectCmplEvt_t),
  sizeof(hciLeConnUpdateCmplEvt_t),
  sizeof(hciLeCreateConnCancelCmdCmplEvt_t),
  sizeof(hciLeAdvReportEvt_t),
  sizeof(hciReadRssiCmdCmplEvt_t),
  sizeof(hciReadChanMapCmdCmplEvt_t),
  sizeof(hciReadTxPwrLvlCmdCmplEvt_t),
  sizeof(hciReadRemoteVerInfoCmplEvt_t),
  sizeof(hciLeReadRemoteFeatCmplEvt_t),
  sizeof(hciLeLtkReqReplCmdCmplEvt_t),
  sizeof(hciLeLtkReqNegReplCmdCmplEvt_t),
  sizeof(hciEncKeyRefreshCmpl_t),
  sizeof(hciEncChangeEvt_t),
  sizeof(hciLeLtkReqEvt_t),
  sizeof(hciVendorSpecCmdStatusEvt_t),
  sizeof(hciVendorSpecCmdCmplEvt_t),
  sizeof(hciVendorSpecEvt_t),
  sizeof(hciHwErrorEvt_t),
  sizeof(hciLeAddDevToResListCmdCmplEvt_t),
  sizeof(hciLeRemDevFromResListCmdCmplEvt_t),
  sizeof(hciLeClearResListCmdCmplEvt_t),
  sizeof(hciLeReadPeerResAddrCmdCmplEvt_t),
  sizeof(hciLeReadLocalResAddrCmdCmplEvt_t),
  sizeof(hciLeSetAddrResEnableCmdCmplEvt_t),
  sizeof(hciLeEncryptCmdCmplEvt_t),
  sizeof(hciLeRandCmdCmplEvt_t),
  sizeof(hciLeRemConnParamRepEvt_t),
  sizeof(hciLeRemConnParamNegRepEvt_t),
  sizeof(hciLeReadDefDataLenEvt_t),
  sizeof(hciLeWriteDefDataLenEvt_t),
  sizeof(hciLeSetDataLenEvt_t),
  sizeof(hciLeReadMaxDataLenEvt_t),
  sizeof(hciLeRemConnParamReqEvt_t),
  sizeof(hciLeDataLenChangeEvt_t),
  sizeof(hciLeP256CmplEvt_t),
  sizeof(hciLeGenDhKeyEvt_t),
  sizeof(hciWriteAuthPayloadToCmdCmplEvt_t),
  sizeof(hciAuthPayloadToExpiredEvt_t),
  sizeof(hciLeReadPhyCmdCmplEvt_t),
  sizeof(hciLeSetDefPhyCmdCmplEvt_t),
  sizeof(hciLePhyUpdateEvt_t),
  sizeof(hciLeExtAdvReportEvt_t),
  sizeof(hciLeScanTimeoutEvt_t),
  sizeof(hciLeAdvSetTermEvt_t),
  sizeof(hciLeScanReqRcvdEvt_t),
  sizeof(hciLePerAdvSyncEstEvt_t),
  sizeof(hciLePerAdvReportEvt_t),
  sizeof(hciLePerAdvSyncLostEvt_t),
  sizeof(wsfMsgHdr_t),
  sizeof(wsfMsgHdr_t),
  sizeof(wsfMsgHdr_t),
  sizeof(wsfMsgHdr_t),
  sizeof(wsfMsgHdr_t),
  sizeof(wsfMsgHdr_t),

  sizeof(wsfMsgHdr_t),
  sizeof(wsfMsgHdr_t),
  sizeof(wsfMsgHdr_t),
  sizeof(wsfMsgHdr_t),
  sizeof(hciLeConnIQReportEvt_t),
  sizeof(wsfMsgHdr_t),

  sizeof(hciLeSetConnCteRxParamsCmdCmplEvt_t),
  sizeof(hciLeSetConnCteTxParamsCmdCmplEvt_t),
  sizeof(hciLeConnCteReqEnableCmdCmplEvt_t),
  sizeof(hciLeConnCteRspEnableCmdCmplEvt_t),
  sizeof(HciLeCisEstEvt_t),
  sizeof(HciLeCisReqEvt_t),
  sizeof(hciDisconnectCmplEvt_t),
  sizeof(HciLeReqPeerScaCmplEvt_t_t),
  sizeof(hciLeSetCigParamsCmdCmplEvt_t),
  sizeof(hciLeRemoveCigCmdCmplEvt_t),
  sizeof(hciLeSetupIsoDataPathCmdCmplEvt_t),
  sizeof(hciLeRemoveIsoDataPathCmdCmplEvt_t),
  sizeof(hciConfigDataPathCmdCmplEvt_t),
  sizeof(hciReadLocalSupCodecsCmdCmplEvt_t),
  sizeof(hciReadLocalSupCodecCapCmdCmplEvt_t),
  sizeof(hciReadLocalSupCtrDlyCmdCmplEvt_t),
  sizeof(HciLeCreateBigCmplEvt_t),
  sizeof(HciLeTerminateBigCmplEvt_t),
  sizeof(HciLeBigSyncEstEvt_t),
  sizeof(HciLeBigSyncLostEvt_t),
  sizeof(HciLeBigTermSyncCmplEvt_t),
  sizeof(HciLeBigInfoAdvRptEvt_t)
};

/* Global event statistics. */
static hciEvtStats_t hciEvtStats = {0};


/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeConnCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeConnCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leConnCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leConnCmpl.handle, p);
  BSTREAM_TO_UINT8(pMsg->leConnCmpl.role, p);
  BSTREAM_TO_UINT8(pMsg->leConnCmpl.addrType, p);
  BSTREAM_TO_BDA(pMsg->leConnCmpl.peerAddr, p);
  BSTREAM_TO_UINT16(pMsg->leConnCmpl.connInterval, p);
  BSTREAM_TO_UINT16(pMsg->leConnCmpl.connLatency, p);
  BSTREAM_TO_UINT16(pMsg->leConnCmpl.supTimeout, p);
  BSTREAM_TO_UINT8(pMsg->leConnCmpl.clockAccuracy, p);

  /* zero out unused fields */
  memset(pMsg->leConnCmpl.localRpa, 0, BDA_ADDR_LEN);
  memset(pMsg->leConnCmpl.peerRpa, 0, BDA_ADDR_LEN);

  pMsg->hdr.param = pMsg->leConnCmpl.handle;
  pMsg->hdr.status = pMsg->leConnCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeEnhancedConnCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeEnhancedConnCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leConnCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leConnCmpl.handle, p);
  BSTREAM_TO_UINT8(pMsg->leConnCmpl.role, p);
  BSTREAM_TO_UINT8(pMsg->leConnCmpl.addrType, p);
  BSTREAM_TO_BDA(pMsg->leConnCmpl.peerAddr, p);
  BSTREAM_TO_BDA(pMsg->leConnCmpl.localRpa, p);
  BSTREAM_TO_BDA(pMsg->leConnCmpl.peerRpa, p);
  BSTREAM_TO_UINT16(pMsg->leConnCmpl.connInterval, p);
  BSTREAM_TO_UINT16(pMsg->leConnCmpl.connLatency, p);
  BSTREAM_TO_UINT16(pMsg->leConnCmpl.supTimeout, p);
  BSTREAM_TO_UINT8(pMsg->leConnCmpl.clockAccuracy, p);

  pMsg->hdr.param = pMsg->leConnCmpl.handle;
  pMsg->hdr.status = pMsg->leConnCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseDisconnectCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseDisconnectCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->disconnectCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->disconnectCmpl.handle, p);
  BSTREAM_TO_UINT8(pMsg->disconnectCmpl.reason, p);

  pMsg->hdr.param = pMsg->disconnectCmpl.handle;
  pMsg->hdr.status = pMsg->disconnectCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeConnUpdateCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeConnUpdateCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leConnUpdateCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leConnUpdateCmpl.handle, p);
  BSTREAM_TO_UINT16(pMsg->leConnUpdateCmpl.connInterval, p);
  BSTREAM_TO_UINT16(pMsg->leConnUpdateCmpl.connLatency, p);
  BSTREAM_TO_UINT16(pMsg->leConnUpdateCmpl.supTimeout, p);

  pMsg->hdr.param = pMsg->leConnUpdateCmpl.handle;
  pMsg->hdr.status = pMsg->leConnUpdateCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeCreateConnCancelCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeCreateConnCancelCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leCreateConnCancelCmdCmpl.status, p);

  pMsg->hdr.status = pMsg->leCreateConnCancelCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseReadRssiCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadRssiCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->readRssiCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->readRssiCmdCmpl.handle, p);
  BSTREAM_TO_UINT8(pMsg->readRssiCmdCmpl.rssi, p);

  pMsg->hdr.param = pMsg->readRssiCmdCmpl.handle;
  pMsg->hdr.status = pMsg->readRssiCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseReadChanMapCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadChanMapCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->readChanMapCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->readChanMapCmdCmpl.handle, p);

  memcpy(pMsg->readChanMapCmdCmpl.chanMap, p, HCI_CHAN_MAP_LEN);

  pMsg->hdr.param = pMsg->readChanMapCmdCmpl.handle;
  pMsg->hdr.status = pMsg->readChanMapCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtReadTxPwrLvlCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadTxPwrLvlCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->readTxPwrLvlCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->readTxPwrLvlCmdCmpl.handle, p);
  BSTREAM_TO_INT8(pMsg->readTxPwrLvlCmdCmpl.pwrLvl, p);

  pMsg->hdr.param = pMsg->readTxPwrLvlCmdCmpl.handle;
  pMsg->hdr.status = pMsg->readTxPwrLvlCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseReadRemoteVerInfoCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadRemoteVerInfoCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->readRemoteVerInfoCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->readRemoteVerInfoCmpl.handle, p);
  BSTREAM_TO_UINT8(pMsg->readRemoteVerInfoCmpl.version, p);
  BSTREAM_TO_UINT16(pMsg->readRemoteVerInfoCmpl.mfrName, p);
  BSTREAM_TO_UINT16(pMsg->readRemoteVerInfoCmpl.subversion, p);

  pMsg->hdr.param = pMsg->readRemoteVerInfoCmpl.handle;
  pMsg->hdr.status = pMsg->readRemoteVerInfoCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeReadRemoteFeatCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadLeRemoteFeatCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leReadRemoteFeatCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leReadRemoteFeatCmpl.handle, p);
  memcpy(&pMsg->leReadRemoteFeatCmpl.features, p, HCI_FEAT_LEN);

  pMsg->hdr.param = pMsg->leReadRemoteFeatCmpl.handle;
  pMsg->hdr.status = pMsg->leReadRemoteFeatCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeLtkReqReplCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeLtkReqReplCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leLtkReqReplCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leLtkReqReplCmdCmpl.handle, p);

  pMsg->hdr.param = pMsg->leLtkReqReplCmdCmpl.handle;
  pMsg->hdr.status = pMsg->leLtkReqReplCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeLtkReqNegReplCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeLtkReqNegReplCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leLtkReqNegReplCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leLtkReqNegReplCmdCmpl.handle, p);

  pMsg->hdr.param = pMsg->leLtkReqNegReplCmdCmpl.handle;
  pMsg->hdr.status = pMsg->leLtkReqNegReplCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseEncKeyRefreshCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseEncKeyRefreshCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->encKeyRefreshCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->encKeyRefreshCmpl.handle, p);

  pMsg->hdr.param = pMsg->encKeyRefreshCmpl.handle;
  pMsg->hdr.status = pMsg->encKeyRefreshCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseEncChange
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseEncChange(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->encChange.status, p);
  BSTREAM_TO_UINT16(pMsg->encChange.handle, p);
  BSTREAM_TO_UINT8(pMsg->encChange.enabled, p);

  pMsg->hdr.param = pMsg->encChange.handle;
  pMsg->hdr.status = pMsg->encChange.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeLtkReq
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeLtkReq(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT16(pMsg->leLtkReq.handle, p);

  memcpy(pMsg->leLtkReq.randNum, p, HCI_RAND_LEN);
  p += HCI_RAND_LEN;

  BSTREAM_TO_UINT16(pMsg->leLtkReq.encDiversifier, p);

  pMsg->hdr.param = pMsg->leLtkReq.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseVendorSpecCmdStatus
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseVendorSpecCmdStatus(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT16(pMsg->vendorSpecCmdStatus.opcode, p);
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseVendorSpecCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseVendorSpecCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  // the 'len' parameter includes Num_HCI_Command_Packets(1byte),Command_Opcode(2byte) and Return_Parameters,
  // exclude the 1byte Num_HCI_Command_Packets
  len = len - 1;

  if ((len >= 3) && ((len - 3) <= HCI_EVT_VENDOR_SPEC_CMD_CMPL_PARAM_MAX_LEN))
  {
    /* roll pointer back to opcode */
    p -= 2;

    BSTREAM_TO_UINT16(pMsg->vendorSpecCmdCmpl.opcode, p);
    BSTREAM_TO_UINT8(pMsg->hdr.status, p);
    memcpy(&pMsg->vendorSpecCmdCmpl.param[0], p, len - 3);
  }
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseVendorSpec
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseVendorSpec(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  if ((len > 0) && (len <= HCI_EVT_VENDOR_SPEC_EVT_PARAM_MAX_LEN))
  {
    memcpy(pMsg->vendorSpec.param, p, len);
  }
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseHwError
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseHwError(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->hwError.code, p);
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeEncryptCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeEncryptCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leEncryptCmdCmpl.status, p);
  memcpy(pMsg->leEncryptCmdCmpl.data, p, HCI_ENCRYPT_DATA_LEN);

  pMsg->hdr.status = pMsg->leEncryptCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeRandCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeRandCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leRandCmdCmpl.status, p);
  memcpy(pMsg->leRandCmdCmpl.randNum, p, HCI_RAND_LEN);

  pMsg->hdr.status = pMsg->leRandCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeAddDevToResListCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeAddDevToResListCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leAddDevToResListCmdCmpl.status, p);

  pMsg->hdr.status = pMsg->leAddDevToResListCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeRemDevFromResListCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeRemDevFromResListCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leRemDevFromResListCmdCmpl.status, p);

  pMsg->hdr.status = pMsg->leRemDevFromResListCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeClearResListCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeClearResListCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leClearResListCmdCmpl.status, p);

  pMsg->hdr.status = pMsg->leClearResListCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeReadPeerResAddrCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeReadPeerResAddrCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leReadPeerResAddrCmdCmpl.status, p);
  BSTREAM_TO_BDA(pMsg->leReadPeerResAddrCmdCmpl.peerRpa, p);

  pMsg->hdr.status = pMsg->leReadPeerResAddrCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeReadLocalResAddrCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeReadLocalResAddrCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leReadLocalResAddrCmdCmpl.status, p);
  BSTREAM_TO_BDA(pMsg->leReadLocalResAddrCmdCmpl.localRpa, p);

  pMsg->hdr.status = pMsg->leReadLocalResAddrCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeSetAddrResEnableCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeSetAddrResEnableCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leSetAddrResEnableCmdCmpl.status, p);

  pMsg->hdr.status = pMsg->leSetAddrResEnableCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseRemConnParamRepCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseRemConnParamRepCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leRemConnParamRepCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leRemConnParamRepCmdCmpl.handle, p);

  pMsg->hdr.status = pMsg->leRemConnParamRepCmdCmpl.status;
  pMsg->hdr.param = pMsg->leRemConnParamRepCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseRemConnParamNegRepCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseRemConnParamNegRepCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leRemConnParamNegRepCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leRemConnParamNegRepCmdCmpl.handle, p);

  pMsg->hdr.status = pMsg->leRemConnParamNegRepCmdCmpl.status;
  pMsg->hdr.param = pMsg->leRemConnParamNegRepCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseReadDefDataLenCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadDefDataLenCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leReadDefDataLenCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leReadDefDataLenCmdCmpl.suggestedMaxTxOctets, p);
  BSTREAM_TO_UINT16(pMsg->leReadDefDataLenCmdCmpl.suggestedMaxTxTime, p);

  pMsg->hdr.status = pMsg->leReadDefDataLenCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseWriteDefDataLenCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseWriteDefDataLenCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leWriteDefDataLenCmdCmpl.status, p);

  pMsg->hdr.status = pMsg->leWriteDefDataLenCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseSetDataLenCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseSetDataLenCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leSetDataLenCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leSetDataLenCmdCmpl.handle, p);

  pMsg->hdr.status = pMsg->leSetDataLenCmdCmpl.status;
  pMsg->hdr.param = pMsg->leSetDataLenCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseReadMaxDataLenCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadMaxDataLenCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leReadMaxDataLenCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leReadMaxDataLenCmdCmpl.supportedMaxTxOctets, p);
  BSTREAM_TO_UINT16(pMsg->leReadMaxDataLenCmdCmpl.supportedMaxTxTime, p);
  BSTREAM_TO_UINT16(pMsg->leReadMaxDataLenCmdCmpl.supportedMaxRxOctets, p);
  BSTREAM_TO_UINT16(pMsg->leReadMaxDataLenCmdCmpl.supportedMaxRxTime, p);

  pMsg->hdr.status = pMsg->leReadMaxDataLenCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseRemConnParamReq
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseRemConnParamReq(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT16(pMsg->leRemConnParamReq.handle, p);
  BSTREAM_TO_UINT16(pMsg->leRemConnParamReq.intervalMin, p);
  BSTREAM_TO_UINT16(pMsg->leRemConnParamReq.intervalMax, p);
  BSTREAM_TO_UINT16(pMsg->leRemConnParamReq.latency, p);
  BSTREAM_TO_UINT16(pMsg->leRemConnParamReq.timeout, p);

  pMsg->hdr.param = pMsg->leRemConnParamReq.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseDataLenChange
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseDataLenChange(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT16(pMsg->leDataLenChange.handle, p);
  BSTREAM_TO_UINT16(pMsg->leDataLenChange.maxTxOctets, p);
  BSTREAM_TO_UINT16(pMsg->leDataLenChange.maxTxTime, p);
  BSTREAM_TO_UINT16(pMsg->leDataLenChange.maxRxOctets, p);
  BSTREAM_TO_UINT16(pMsg->leDataLenChange.maxRxTime, p);

  pMsg->hdr.param = pMsg->leDataLenChange.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseReadPubKeyCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadPubKeyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leP256.status, p);
  memcpy(pMsg->leP256.key, p, HCI_P256_KEY_LEN);

  pMsg->hdr.status = pMsg->leP256.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseGenDhKeyCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseGenDhKeyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leGenDHKey.status, p);
  memcpy(pMsg->leGenDHKey.key, p, HCI_DH_KEY_LEN);

  pMsg->hdr.status = pMsg->leGenDHKey.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseWriteAuthTimeoutCmdCmpl
 *
 *  \brief  Process HCI write authenticated payload timeout command complete event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
void hciEvtParseWriteAuthTimeoutCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->writeAuthPayloadToCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->writeAuthPayloadToCmdCmpl.handle, p);

  pMsg->hdr.status = pMsg->writeAuthPayloadToCmdCmpl.status;
  pMsg->hdr.param = pMsg->writeAuthPayloadToCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseAuthTimeoutExpiredEvt
 *
 *  \brief  Process HCI authenticated payload timeout expired event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
void hciEvtParseAuthTimeoutExpiredEvt(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT16(pMsg->authPayloadToExpired.handle, p);

  pMsg->hdr.param = pMsg->authPayloadToExpired.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseReadPhyCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadPhyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leReadPhyCmdCmpl.status, p);
  BSTREAM_TO_UINT16(pMsg->leReadPhyCmdCmpl.handle, p);
  BSTREAM_TO_UINT8(pMsg->leReadPhyCmdCmpl.txPhy, p);
  BSTREAM_TO_UINT8(pMsg->leReadPhyCmdCmpl.rxPhy, p);

  pMsg->hdr.status = pMsg->leReadPhyCmdCmpl.status;
  pMsg->hdr.param = pMsg->leReadPhyCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseSetDefPhyCmdCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseSetDefPhyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leSetDefPhyCmdCmpl.status, p);

  pMsg->hdr.status = pMsg->leSetDefPhyCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParsePhyUpdateCmpl
 *
 *  \brief  Parse an HCI event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParsePhyUpdateCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->lePhyUpdate.status, p);
  BSTREAM_TO_UINT16(pMsg->lePhyUpdate.handle, p);
  BSTREAM_TO_UINT8(pMsg->lePhyUpdate.txPhy, p);
  BSTREAM_TO_UINT8(pMsg->lePhyUpdate.rxPhy, p);

  pMsg->hdr.status = pMsg->lePhyUpdate.status;
  pMsg->hdr.param = pMsg->lePhyUpdate.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessLeAdvReport
 *
 *  \brief  Process an HCI LE advertising report.
 *
 *  \param  p    Buffer containing HCI event, points to start of parameters.
 *  \param  len  Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtProcessLeAdvReport(uint8_t *p, uint8_t len)
{
  hciLeAdvReportEvt_t *pMsg;
  uint8_t             i;

  /* get number of reports */
  BSTREAM_TO_UINT8(i, p);

  HCI_TRACE_INFO1("HCI Adv report, num reports: %d", i);

  /* sanity check num reports */
  if (i > HCI_MAX_REPORTS)
  {
    return;
  }

  /* allocate temp buffer that can hold max length adv/scan rsp data */
  if ((pMsg = WsfBufAlloc(sizeof(hciLeAdvReportEvt_t) + HCI_ADV_DATA_LEN)) != NULL)
  {
    /* parse each report and execute callback */
    while (i-- > 0)
    {
      BSTREAM_TO_UINT8(pMsg->eventType, p);
      BSTREAM_TO_UINT8(pMsg->addrType, p);
      BSTREAM_TO_BDA(pMsg->addr, p);
      BSTREAM_TO_UINT8(pMsg->len, p);

      HCI_TRACE_INFO1("HCI Adv report, data len: %d", pMsg->len);

      /* sanity check on report length; quit if invalid */
      if (pMsg->len > HCI_ADV_DATA_LEN)
      {
        HCI_TRACE_WARN0("Invalid adv report data len");
        break;
      }

      /* Copy data to space after end of report struct */
      pMsg->pData = (uint8_t *) (pMsg + 1);
      memcpy(pMsg->pData, p, pMsg->len);
      p += pMsg->len;

      BSTREAM_TO_UINT8(pMsg->rssi, p);

      /* zero out unused fields */
      pMsg->directAddrType = 0;
      memset(pMsg->directAddr, 0, BDA_ADDR_LEN);

      /* initialize message header */
      pMsg->hdr.param = 0;
      pMsg->hdr.event = HCI_LE_ADV_REPORT_CBACK_EVT;
      pMsg->hdr.status = 0;

      /* execute callback */
      (*hciCb.evtCback)((hciEvt_t *) pMsg);
    }

    /* free buffer */
    WsfBufFree(pMsg);
  }
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessLeExtAdvReport
 *
 *  \brief  Process an HCI LE extended advertising report.
 *
 *  \param  p    Buffer containing HCI event, points to start of parameters.
 *  \param  len  Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtProcessLeExtAdvReport(uint8_t *p, uint8_t len)
{
  hciLeExtAdvReportEvt_t *pMsg;
  uint8_t                i;
  uint8_t                *ptr;
  uint8_t                maxLen;
  uint8_t                dataLen;

  /* get number of reports without advancing pointer */
  i = *p;

  HCI_TRACE_INFO1("HCI Ext Adv report, num reports: %d", i);

  /* sanity check num reports */
  if (i > HCI_MAX_REPORTS)
  {
    return;
  }

  ptr = p + 1;
  maxLen = 0;

  /* find out max length ext adv/scan rsp data */
  while (i-- > 0)
  {
    ptr += HCI_EXT_ADV_RPT_DATA_LEN_OFFSET;
    BSTREAM_TO_UINT8(dataLen, ptr);
    ptr += dataLen;

    /* if len greater than max len seen so far */
    if (dataLen > maxLen)
    {
      /* update max len */
      maxLen = dataLen;
    }
  }

  /* allocate temp buffer that can hold max length ext adv/scan rsp data */
  if ((pMsg = WsfBufAlloc(sizeof(hciLeExtAdvReportEvt_t) + maxLen)) != NULL)
  {
    /* get number of reports */
    BSTREAM_TO_UINT8(i, p);

    /* parse each report and execute callback */
    while (i-- > 0)
    {
      BSTREAM_TO_UINT16(pMsg->eventType, p);
      BSTREAM_TO_UINT8(pMsg->addrType, p);
      BSTREAM_TO_BDA(pMsg->addr, p);
      BSTREAM_TO_UINT8(pMsg->priPhy, p);
      BSTREAM_TO_UINT8(pMsg->secPhy, p);
      BSTREAM_TO_UINT8(pMsg->advSid, p);
      BSTREAM_TO_INT8(pMsg->txPower, p);
      BSTREAM_TO_INT8(pMsg->rssi, p);
      BSTREAM_TO_UINT16(pMsg->perAdvInter, p);
      BSTREAM_TO_UINT8(pMsg->directAddrType, p);
      BSTREAM_TO_BDA(pMsg->directAddr, p);
      BSTREAM_TO_UINT8(pMsg->len, p);

      HCI_TRACE_INFO1("HCI Ext Adv report, data len: %d", pMsg->len);

      /* sanity check on report length; quit if invalid */
      if (pMsg->len > HCI_EXT_ADV_RPT_DATA_LEN)
      {
        HCI_TRACE_WARN0("Invalid ext adv report data len");
        break;
      }

      /* Copy data to space after end of report struct */
      pMsg->pData = (uint8_t *)(pMsg + 1);
      memcpy(pMsg->pData, p, pMsg->len);
      p += pMsg->len;

      /* initialize message header */
      pMsg->hdr.param = 0;
      pMsg->hdr.event = HCI_LE_EXT_ADV_REPORT_CBACK_EVT;
      pMsg->hdr.status = 0;

      /* execute callback */
      (*hciCb.evtCback)((hciEvt_t *)pMsg);
    }

    /* free buffer */
    WsfBufFree(pMsg);
  }
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeScanTimeout
 *
 *  \brief  Parse an HCI LE scan timeout event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeScanTimeout(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  /* empty */
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeAdvSetTerm
 *
 *  \brief  Parse an HCI LE advertising set terminated event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeAdvSetTerm(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leAdvSetTerm.status, p);
  BSTREAM_TO_UINT8(pMsg->leAdvSetTerm.advHandle, p);
  BSTREAM_TO_UINT16(pMsg->leAdvSetTerm.handle, p);
  BSTREAM_TO_UINT8(pMsg->leAdvSetTerm.numComplEvts, p);

  pMsg->hdr.status = pMsg->leAdvSetTerm.status;
  pMsg->hdr.param = pMsg->leAdvSetTerm.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeScanReqRcvd
 *
 *  \brief  Parse an HCI LE scan request received event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeScanReqRcvd(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leScanReqRcvd.advHandle, p);
  BSTREAM_TO_UINT8(pMsg->leScanReqRcvd.scanAddrType, p);
  BSTREAM_TO_BDA(pMsg->leScanReqRcvd.scanAddr, p);
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLePerAdvSyncEst
 *
 *  \brief  Parse an HCI LE periodic advertising sync established event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLePerAdvSyncEst(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->lePerAdvSyncEst.status, p);
  BSTREAM_TO_UINT16(pMsg->lePerAdvSyncEst.syncHandle, p);
  BSTREAM_TO_UINT8(pMsg->lePerAdvSyncEst.advSid, p);
  BSTREAM_TO_UINT8(pMsg->lePerAdvSyncEst.advAddrType, p);
  BSTREAM_TO_BDA(pMsg->lePerAdvSyncEst.advAddr, p);
  BSTREAM_TO_UINT8(pMsg->lePerAdvSyncEst.advPhy, p);
  BSTREAM_TO_UINT16(pMsg->lePerAdvSyncEst.perAdvInterval, p);
  BSTREAM_TO_UINT8(pMsg->lePerAdvSyncEst.clockAccuracy, p);

  pMsg->hdr.status = pMsg->lePerAdvSyncEst.status;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessLePerAdvReport
 *
 *  \brief  Process an HCI LE periodic advertising report event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtProcessLePerAdvReport(uint8_t *p, uint8_t len)
{
  hciLePerAdvReportEvt_t *pMsg;
  uint8_t                dataLen;

  HCI_TRACE_INFO0("HCI Per Adv report");

  /* get report data length */
  dataLen = p[HCI_PER_ADV_RPT_DATA_LEN_OFFSET];

  /* sanity check on report length; quit if invalid */
  if (dataLen > HCI_PER_ADV_RPT_DATA_LEN)
  {
    HCI_TRACE_WARN1("Invalid per adv report data len: %d", dataLen);
    return;
  }

  /* allocate temp buffer that can hold max length periodic adv report data */
  if ((pMsg = WsfBufAlloc(sizeof(hciLePerAdvReportEvt_t) + dataLen)) != NULL)
  {
    /* parse report and execute callback */
    BSTREAM_TO_UINT16(pMsg->syncHandle, p);
    BSTREAM_TO_UINT8(pMsg->txPower, p);
    BSTREAM_TO_UINT8(pMsg->rssi, p);
    BSTREAM_TO_UINT8(pMsg->unused, p);
    BSTREAM_TO_UINT8(pMsg->status, p);
    BSTREAM_TO_UINT8(pMsg->len, p);

    HCI_TRACE_INFO1("HCI Per Adv report, data len: %d", pMsg->len);

    /* Copy data to space after end of report struct */
    pMsg->pData = (uint8_t *)(pMsg + 1);
    memcpy(pMsg->pData, p, pMsg->len);

    /* initialize message header */
    pMsg->hdr.param = 0;
    pMsg->hdr.event = HCI_LE_PER_ADV_REPORT_CBACK_EVT;
    pMsg->hdr.status = pMsg->status;

    /* execute callback */
    (*hciCb.evtCback)((hciEvt_t *)pMsg);

    /* free buffer */
    WsfBufFree(pMsg);
  }
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeCmdCmpl
 *
 *  \brief  Parse an HCI LE complete event containing a one byte status.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->hdr.status, p);
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessLeConnIQReport
 *
 *  \brief  Process an HCI LE Connection IQ report.
 *
 *  \param  p    Buffer containing HCI event, points to start of parameters.
 *  \param  len  Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtProcessLeConnIQReport(uint8_t *p, uint8_t len)
{
    hciLeConnIQReportEvt_t *pMsg;
    APP_TRACE_INFO0("hciEvtProcessLeConnIQReport");

    if ((pMsg = WsfBufAlloc(sizeof(hciLeConnIQReportEvt_t) + HCI_IQ_RPT_SAMPLE_CNT_MAX*2)) != NULL)
    {
        APP_TRACE_INFO0("rcv Le ConnIQ Report...");
        BSTREAM_TO_UINT16(pMsg->handle, p);
        BSTREAM_TO_UINT8(pMsg->rxPhy, p);
        BSTREAM_TO_UINT8(pMsg->dataChIdx, p);
        BSTREAM_TO_UINT16(pMsg->rssi, p);
        BSTREAM_TO_UINT8(pMsg->rssiAntennaId, p);
        BSTREAM_TO_UINT8(pMsg->cteType, p);
        BSTREAM_TO_UINT8(pMsg->slotDurations, p);
        BSTREAM_TO_UINT8(pMsg->pktStatus, p);
        BSTREAM_TO_UINT16(pMsg->connEvtCnt, p);
        BSTREAM_TO_UINT8(pMsg->sampleCnt, p);

        /* Copy IQ sample data to space after end of report struct */
        pMsg->pISample = (int8_t *) (pMsg + 1);
        memcpy(pMsg->pISample, p, pMsg->sampleCnt);
        p += pMsg->sampleCnt;

        pMsg->pQSample = (int8_t *) (pMsg + 1)+HCI_IQ_RPT_SAMPLE_CNT_MAX;
        memcpy(pMsg->pISample, p, pMsg->sampleCnt);

        pMsg->hdr.param = pMsg->handle;
        pMsg->hdr.status = pMsg->pktStatus;
        pMsg->hdr.event = HCI_LE_CONN_IQ_REPORT_CBACK_EVT;

        /* execute callback */
        (*hciCb.evtCback)((hciEvt_t *) pMsg);

        /* free buffer */
        WsfBufFree(pMsg);
    }
}


/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessLeConlessIQReport
 *
 *  \brief  Process an HCI LE Connectionless IQ report.
 *
 *  \param  p    Buffer containing HCI event, points to start of parameters.
 *  \param  len  Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtProcessLeConlessIQReport(uint8_t *p, uint8_t len)
{
    hciLeConlessIQReportEvt_t *pMsg;

    if ((pMsg = WsfBufAlloc(sizeof(hciLeConlessIQReportEvt_t) + HCI_IQ_RPT_SAMPLE_CNT_MAX*2)) != NULL)
    {
        APP_TRACE_INFO0("rcv Le Conntless IQ Report...");
        BSTREAM_TO_UINT16(pMsg->synHdl, p);
        BSTREAM_TO_UINT8(pMsg->chIdx, p);
        BSTREAM_TO_UINT16(pMsg->rssi, p);
        BSTREAM_TO_UINT8(pMsg->rssiAntennaId, p);
        BSTREAM_TO_UINT8(pMsg->cteType, p);
        BSTREAM_TO_UINT8(pMsg->slotDurations, p);
        BSTREAM_TO_UINT8(pMsg->pktStatus, p);
        BSTREAM_TO_UINT16(pMsg->paEvtCnt, p);
        BSTREAM_TO_UINT8(pMsg->sampleCnt, p);

        /* Copy IQ sample data to space after end of report struct */
        pMsg->pISample = (int8_t *) (pMsg + 1);
        memcpy(pMsg->pISample, p, pMsg->sampleCnt);
        p += pMsg->sampleCnt;

        pMsg->pQSample = (int8_t *) (pMsg + 1)+HCI_IQ_RPT_SAMPLE_CNT_MAX;
        memcpy(pMsg->pISample, p, pMsg->sampleCnt);

        pMsg->hdr.param = pMsg->synHdl;
        pMsg->hdr.status = pMsg->pktStatus;
        pMsg->hdr.event = HCI_LE_CONNLESS_IQ_REPORT_CBACK_EVT;

        /* execute callback */
        (*hciCb.evtCback)((hciEvt_t *) pMsg);

        /* free buffer */
        WsfBufFree(pMsg);
    }
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeSetConnCteRcvParm
 *
 *  \brief  Parse an HCI LE set connection CTE receive parameters event
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeSetConnCteRcvParm(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
    BSTREAM_TO_UINT8(pMsg->leSetConnCteRxParamsCmdCmpl.status, p);
    BSTREAM_TO_UINT16(pMsg->leSetConnCteRxParamsCmdCmpl.handle, p);

    pMsg->hdr.status = pMsg->leSetConnCteRxParamsCmdCmpl.status;
    pMsg->hdr.param = pMsg->leSetConnCteRxParamsCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeSetConnCteTxParm
 *
 *  \brief  Parse an HCI LE set connection CTE transmit parameters event
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeSetConnCteTxParm(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
    BSTREAM_TO_UINT8(pMsg->leSetConnCteTxParamsCmdCmpl.status, p);
    BSTREAM_TO_UINT16(pMsg->leSetConnCteTxParamsCmdCmpl.handle, p);

    pMsg->hdr.status = pMsg->leSetConnCteTxParamsCmdCmpl.status;
    pMsg->hdr.param = pMsg->leSetConnCteTxParamsCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeConnCteReqEn
 *
 *  \brief  Parse an HCI LE set connection CTE request enable  event
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeConnCteReqEn(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
    BSTREAM_TO_UINT8(pMsg->leConnCteReqEnableCmdCmpl.status, p);
    BSTREAM_TO_UINT16(pMsg->leConnCteReqEnableCmdCmpl.handle, p);

    pMsg->hdr.status = pMsg->leConnCteReqEnableCmdCmpl.status;
    pMsg->hdr.param = pMsg->leConnCteReqEnableCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLeConnCteRspEn
 *
 *  \brief  Parse an HCI LE set connection CTE response enable  event
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeConnCteRspEn(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
    BSTREAM_TO_UINT8(pMsg->leConnCteRspEnableCmdCmpl.status, p);
    BSTREAM_TO_UINT16(pMsg->leConnCteRspEnableCmdCmpl.handle, p);

    pMsg->hdr.status = pMsg->leConnCteRspEnableCmdCmpl.status;
    pMsg->hdr.param = pMsg->leConnCteRspEnableCmdCmpl.handle;
}


/*************************************************************************************************/
/*!
 *  \fn     hciEvtParseLePerAdvSyncLost
 *
 *  \brief  Parse an HCI LE periodic advertising synch lost event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLePerAdvSyncLost(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT16(pMsg->lePerAdvSyncLost.syncHandle, p);
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessLeDirectAdvReport
 *
 *  \brief  Process an HCI LE direct advertising report.
 *
 *  \param  p    Buffer containing HCI event, points to start of parameters.
 *  \param  len  Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtProcessLeDirectAdvReport(uint8_t *p, uint8_t len)
{
  hciLeAdvReportEvt_t *pMsg;
  uint8_t             i;

  /* get number of reports */
  BSTREAM_TO_UINT8(i, p);

  HCI_TRACE_INFO1("HCI Adv report, num reports: %d", i);

  /* sanity check num reports */
  if (i > HCI_MAX_REPORTS)
  {
    return;
  }

  /* allocate temp buffer that can hold max length adv/scan rsp data */
  if ((pMsg = WsfBufAlloc(sizeof(hciLeAdvReportEvt_t))) != NULL)
  {
    /* parse each report and execute callback */
    while (i-- > 0)
    {
      BSTREAM_TO_UINT8(pMsg->eventType, p);
      BSTREAM_TO_UINT8(pMsg->addrType, p);
      BSTREAM_TO_BDA(pMsg->addr, p);
      BSTREAM_TO_UINT8(pMsg->directAddrType, p);
      BSTREAM_TO_BDA(pMsg->directAddr, p);
      BSTREAM_TO_UINT8(pMsg->rssi, p);

      /* zero out unused fields */
      pMsg->len = 0;
      pMsg->pData = NULL;

      /* initialize message header */
      pMsg->hdr.param = 0;
      pMsg->hdr.event = HCI_LE_ADV_REPORT_CBACK_EVT;
      pMsg->hdr.status = 0;

      /* execute callback */
      (*hciCb.evtCback)((hciEvt_t *) pMsg);
    }

    /* free buffer */
    WsfBufFree(pMsg);
  }
}

/*************************************************************************************************/
/*!
 *  \brief  Process an HCI LE CIS established event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeCisEst(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leCisEst.status, p);
  BSTREAM_TO_UINT16(pMsg->leCisEst.cisHandle, p);
  BSTREAM_TO_UINT24(pMsg->leCisEst.cigSyncDelayUsec, p);
  BSTREAM_TO_UINT24(pMsg->leCisEst.cisSyncDelayUsec, p);
  BSTREAM_TO_UINT24(pMsg->leCisEst.transLatMToSUsec, p);
  BSTREAM_TO_UINT24(pMsg->leCisEst.transLatSToMUsec, p);
  BSTREAM_TO_UINT8(pMsg->leCisEst.phyMToS, p);
  BSTREAM_TO_UINT8(pMsg->leCisEst.phySToM, p);
  BSTREAM_TO_UINT8(pMsg->leCisEst.nse, p);
  BSTREAM_TO_UINT8(pMsg->leCisEst.bnMToS, p);
  BSTREAM_TO_UINT8(pMsg->leCisEst.bnSToM, p);
  BSTREAM_TO_UINT8(pMsg->leCisEst.ftMToS, p);
  BSTREAM_TO_UINT8(pMsg->leCisEst.ftSToM, p);
  BSTREAM_TO_UINT16(pMsg->leCisEst.maxPduMToS, p);
  BSTREAM_TO_UINT16(pMsg->leCisEst.maxPduSToM, p);
  BSTREAM_TO_UINT16(pMsg->leCisEst.isoInterval, p);

  pMsg->hdr.status = pMsg->leCisEst.status;
  pMsg->hdr.param = pMsg->leCisEst.cisHandle;
}

/*************************************************************************************************/
/*!
 *  \brief  Process an HCI LE CIS request event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeCisReq(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT16(pMsg->leCisReq.aclHandle, p);
  BSTREAM_TO_UINT16(pMsg->leCisReq.cisHandle, p);
  BSTREAM_TO_UINT8(pMsg->leCisReq.cigId, p);
  BSTREAM_TO_UINT8(pMsg->leCisReq.cisId, p);

  pMsg->hdr.param = pMsg->leCisReq.cisHandle;
}

/*************************************************************************************************/
/*!
 *  \brief  Process an HCI LE request peer SCA complete event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeReqPeerScaCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leReqPeerSca.status, p);
  BSTREAM_TO_UINT16(pMsg->leReqPeerSca.handle, p);
  BSTREAM_TO_UINT8(pMsg->leReqPeerSca.peerSca, p);

  pMsg->hdr.status = pMsg->leReqPeerSca.status;
  pMsg->hdr.param = pMsg->leReqPeerSca.handle;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI Command Complete event for the LE set CIG parameters command.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeSetCigParamsCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  uint16_t *pCisHandle = pMsg->leSetCigParamsCmdCmpl.cisHandle;

  BSTREAM_TO_UINT8(pMsg->leSetCigParamsCmdCmpl.status, p);
  BSTREAM_TO_UINT8(pMsg->leSetCigParamsCmdCmpl.cigId, p);
  BSTREAM_TO_UINT8(pMsg->leSetCigParamsCmdCmpl.numCis, p);

  if (pMsg->leSetCigParamsCmdCmpl.numCis > HCI_MAX_CIS_COUNT)
  {
    pMsg->leSetCigParamsCmdCmpl.numCis = HCI_MAX_CIS_COUNT;
  }

  for (uint8_t i = pMsg->leSetCigParamsCmdCmpl.numCis; i > 0; i--, pCisHandle++)
  {
    BSTREAM_TO_UINT16(*pCisHandle, p);
  }

  pMsg->hdr.status = pMsg->leSetCigParamsCmdCmpl.status;
  pMsg->hdr.param = pMsg->leSetCigParamsCmdCmpl.cigId;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI Command Complete event for the LE remove CIG command.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeRemoveCigCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leRemoveCigCmdCmpl.status, p);
  BSTREAM_TO_UINT8(pMsg->leRemoveCigCmdCmpl.cigId, p);

  pMsg->hdr.status = pMsg->leRemoveCigCmdCmpl.status;
  pMsg->hdr.param = pMsg->leRemoveCigCmdCmpl.cigId;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI Command Complete event for the LE setup ISO data path command.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeSetupIsoDataPathCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leSetupIsoDataPathCmdCmpl.status, p);
  BSTREAM_TO_UINT8(pMsg->leSetupIsoDataPathCmdCmpl.handle, p);

  pMsg->hdr.status = pMsg->leSetupIsoDataPathCmdCmpl.status;
  pMsg->hdr.param = pMsg->leSetupIsoDataPathCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI Command Complete event for the LE remove ISO data path command.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeRemoveIsoDataPathCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leRemoveIsoDataPathCmdCmpl.status, p);
  BSTREAM_TO_UINT8(pMsg->leRemoveIsoDataPathCmdCmpl.handle, p);

  pMsg->hdr.status = pMsg->leRemoveIsoDataPathCmdCmpl.status;
  pMsg->hdr.param = pMsg->leRemoveIsoDataPathCmdCmpl.handle;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI Command Complete event for the configure data path command.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseConfigDataPathCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->configDataPathCmdCmpl.status, p);

  pMsg->hdr.status = pMsg->configDataPathCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI Command Complete event for the read local supported codecs command.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadLocalSupCodecsCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  uint8_t numCodecs;

  BSTREAM_TO_UINT8(pMsg->readLocalSupCodecsCmdCmpl.status, p);
  BSTREAM_TO_UINT8(numCodecs, p);

  pMsg->readLocalSupCodecsCmdCmpl.numStdCodecs = WSF_MIN(numCodecs, HCI_MAX_CODEC);
  for (uint8_t i = 0; i < pMsg->readLocalSupCodecsCmdCmpl.numStdCodecs; i++)
  {
    BSTREAM_TO_UINT8(pMsg->readLocalSupCodecsCmdCmpl.stdCodecs[i].codecId, p);
    BSTREAM_TO_UINT8(pMsg->readLocalSupCodecsCmdCmpl.stdCodecTrans[i], p);
  }

  if (numCodecs > HCI_MAX_CODEC)
  {
    /* ignore remaining standard codec info */
    HCI_TRACE_WARN1("Standard codec info block(s) dropped: %d", numCodecs - HCI_MAX_CODEC);

    p += (numCodecs - HCI_MAX_CODEC) * 2;
  }

  BSTREAM_TO_UINT8(numCodecs, p);

  pMsg->readLocalSupCodecsCmdCmpl.numVsCodecs = WSF_MIN(numCodecs, HCI_MAX_CODEC);
  for (uint8_t i = 0; i < pMsg->readLocalSupCodecsCmdCmpl.numVsCodecs; i++)
  {
    BSTREAM_TO_UINT16(pMsg->readLocalSupCodecsCmdCmpl.vsCodecs[i].compId, p);
    BSTREAM_TO_UINT16(pMsg->readLocalSupCodecsCmdCmpl.vsCodecs[i].codecId, p);
    BSTREAM_TO_UINT8(pMsg->readLocalSupCodecsCmdCmpl.vsCodecTrans[i], p);
  }

  if (numCodecs > HCI_MAX_CODEC)
  {
    /* ignore remaining vendor-specific codec info */
    HCI_TRACE_WARN1("Vendor-specific codec info block(s) dropped: %d", numCodecs - HCI_MAX_CODEC);
  }

  pMsg->hdr.status = pMsg->readLocalSupCodecsCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI Command Complete event for the read local supported codec capabilities
 *          command.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadLocalSupCodecCapCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  uint8_t numCodecs, dataLen;

  BSTREAM_TO_UINT8(pMsg->readLocalSupCodecCapCmdCmpl.status, p);
  BSTREAM_TO_UINT8(numCodecs, p);

  pMsg->readLocalSupCodecCapCmdCmpl.numCodecCaps = WSF_MIN(numCodecs, HCI_MAX_CODEC);
  for (uint8_t i = 0; i < pMsg->readLocalSupCodecCapCmdCmpl.numCodecCaps; i++)
  {
    BSTREAM_TO_UINT8(dataLen, p);

    pMsg->readLocalSupCodecCapCmdCmpl.codecCap[i].len = WSF_MIN(dataLen, HCI_CODEC_CAP_DATA_LEN);
    memcpy(pMsg->readLocalSupCodecCapCmdCmpl.codecCap[i].data, p,
           pMsg->readLocalSupCodecCapCmdCmpl.codecCap[i].len);
    p += dataLen;

    if (dataLen > HCI_CODEC_CAP_DATA_LEN)
    {
      /* ignore remaining codec-specfic data */
      HCI_TRACE_WARN1("Codec-specific data dropped: %d", dataLen - HCI_CODEC_CAP_DATA_LEN);
    }
  }

  if (numCodecs > HCI_MAX_CODEC)
  {
    /* ignore remaining codec capability info */
    HCI_TRACE_WARN1("Codec capability block(s) dropped: %d", numCodecs - HCI_MAX_CODEC);
  }

  pMsg->hdr.status = pMsg->readLocalSupCodecCapCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI Command Complete event for the read local supported controller delay
 *          command.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseReadLocalSupCtrDlyCmdCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->readLocalSupCtrDlyCmdCmpl.status, p);
  BSTREAM_TO_UINT24(pMsg->readLocalSupCtrDlyCmdCmpl.minDly, p);
  BSTREAM_TO_UINT24(pMsg->readLocalSupCtrDlyCmdCmpl.maxDly, p);

  pMsg->hdr.status = pMsg->readLocalSupCtrDlyCmdCmpl.status;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI LE create BIG complete event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeCreateBigCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  uint8_t numBis;

  BSTREAM_TO_UINT8(pMsg->leCreateBigCmpl.status, p);
  BSTREAM_TO_UINT8(pMsg->leCreateBigCmpl.bigHandle, p);
  BSTREAM_TO_UINT24(pMsg->leCreateBigCmpl.syncDelayUsec, p);
  BSTREAM_TO_UINT24(pMsg->leCreateBigCmpl.transLatUsec, p);
  BSTREAM_TO_UINT8(pMsg->leCreateBigCmpl.phy, p);
  BSTREAM_TO_UINT8(pMsg->leCreateBigCmpl.nse, p);
  BSTREAM_TO_UINT8(pMsg->leCreateBigCmpl.bn, p);
  BSTREAM_TO_UINT8(pMsg->leCreateBigCmpl.pto, p);
  BSTREAM_TO_UINT8(pMsg->leCreateBigCmpl.irc, p);
  BSTREAM_TO_UINT16(pMsg->leCreateBigCmpl.maxPdu, p);
  BSTREAM_TO_UINT16(pMsg->leCreateBigCmpl.isoInterval, p);
  BSTREAM_TO_UINT8(numBis, p);

  pMsg->leCreateBigCmpl.numBis = WSF_MIN(numBis, HCI_MAX_BIS_COUNT);
  for (uint8_t i = 0; i < pMsg->leCreateBigCmpl.numBis; i++)
  {
    BSTREAM_TO_UINT16(pMsg->leCreateBigCmpl.bisHandle[i], p);
  }

  if (numBis > HCI_MAX_BIS_COUNT)
  {
    /* ignore remaining BIS handles */
    HCI_TRACE_WARN1("BIS handle(s) dropped: %d", numBis - HCI_MAX_BIS_COUNT);
  }

  pMsg->hdr.status = pMsg->leCreateBigCmpl.status;
  pMsg->hdr.param = pMsg->leCreateBigCmpl.bigHandle;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI LE terminate BIG complete event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeTerminateBigCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leTerminateBigCmpl.bigHandle, p);
  BSTREAM_TO_UINT8(pMsg->leTerminateBigCmpl.reason, p);

  pMsg->hdr.status = pMsg->leTerminateBigCmpl.reason;
  pMsg->hdr.param = pMsg->leTerminateBigCmpl.bigHandle;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI LE BIG sync established event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeBigSyncEst(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  uint8_t numBis;

  BSTREAM_TO_UINT8(pMsg->leBigSyncEst.status, p);
  BSTREAM_TO_UINT8(pMsg->leBigSyncEst.bigHandle, p);
  BSTREAM_TO_UINT24(pMsg->leBigSyncEst.transLatUsec, p);
  BSTREAM_TO_UINT8(pMsg->leBigSyncEst.nse, p);
  BSTREAM_TO_UINT8(pMsg->leBigSyncEst.bn, p);
  BSTREAM_TO_UINT8(pMsg->leBigSyncEst.pto, p);
  BSTREAM_TO_UINT8(pMsg->leBigSyncEst.irc, p);
  BSTREAM_TO_UINT16(pMsg->leBigSyncEst.maxPdu, p);
  BSTREAM_TO_UINT16(pMsg->leBigSyncEst.isoInterval, p);
  BSTREAM_TO_UINT8(numBis, p);

  pMsg->leBigSyncEst.numBis = WSF_MIN(numBis, HCI_MAX_BIS_COUNT);
  for (uint8_t i = 0; i < pMsg->leBigSyncEst.numBis; i++)
  {
    BSTREAM_TO_UINT16(pMsg->leBigSyncEst.bisHandle[i], p);
  }

  if (numBis > HCI_MAX_BIS_COUNT)
  {
    /* ignore remaining BIS handles */
    HCI_TRACE_WARN1("BIS handle(s) dropped: %d", numBis - HCI_MAX_BIS_COUNT);
  }

  pMsg->hdr.status = pMsg->leBigSyncEst.status;
  pMsg->hdr.param = pMsg->leBigSyncEst.bigHandle;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI LE BIG sync lost event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeBigSyncLost(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leBigSyncLost.bigHandle, p);
  BSTREAM_TO_UINT8(pMsg->leBigSyncLost.reason, p);

  pMsg->hdr.status = pMsg->leBigSyncLost.reason;
  pMsg->hdr.param = pMsg->leBigSyncLost.bigHandle;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI LE BIG terminate sync complete event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeBigTermSyncCmpl(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT8(pMsg->leBigTermSyncCmpl.status, p);
  BSTREAM_TO_UINT8(pMsg->leBigTermSyncCmpl.bigHandle, p);

  pMsg->hdr.status = pMsg->leBigTermSyncCmpl.status;
  pMsg->hdr.param = pMsg->leBigTermSyncCmpl.bigHandle;
}

/*************************************************************************************************/
/*!
 *  \brief  Parse an HCI LE BIG Info advertising report event.
 *
 *  \param  pMsg    Pointer to output event message structure.
 *  \param  p       Pointer to input HCI event parameter byte stream.
 *  \param  len     Parameter byte stream length.
 *
 *  \return None.
 */
/*************************************************************************************************/
static void hciEvtParseLeBigInfoAdvRpt(hciEvt_t *pMsg, uint8_t *p, uint8_t len)
{
  BSTREAM_TO_UINT16(pMsg->leBigInfoAdvRpt.syncHandle, p);
  BSTREAM_TO_UINT8(pMsg->leBigInfoAdvRpt.numBis, p);
  BSTREAM_TO_UINT8(pMsg->leBigInfoAdvRpt.nse, p);
  BSTREAM_TO_UINT16(pMsg->leBigInfoAdvRpt.isoInterv, p);
  BSTREAM_TO_UINT8(pMsg->leBigInfoAdvRpt.bn, p);
  BSTREAM_TO_UINT8(pMsg->leBigInfoAdvRpt.pto, p);
  BSTREAM_TO_UINT8(pMsg->leBigInfoAdvRpt.irc, p);
  BSTREAM_TO_UINT16(pMsg->leBigInfoAdvRpt.maxPdu, p);
  BSTREAM_TO_UINT24(pMsg->leBigInfoAdvRpt.sduInterv, p);
  BSTREAM_TO_UINT16(pMsg->leBigInfoAdvRpt.maxSdu, p);
  BSTREAM_TO_UINT8(pMsg->leBigInfoAdvRpt.phy, p);
  BSTREAM_TO_UINT8(pMsg->leBigInfoAdvRpt.framing, p);
  BSTREAM_TO_UINT8(pMsg->leBigInfoAdvRpt.encrypt, p);

  pMsg->hdr.status = HCI_SUCCESS;
  pMsg->hdr.param = pMsg->leBigInfoAdvRpt.syncHandle;
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtCmdStatusFailure
 *
 *  \brief  Process HCI command status event with failure status.
 *
 *  \param  status  Event status.
 *  \param  opcode  Command opcode for this event.
 *
 *  \return None.
 */
/*************************************************************************************************/
void hciEvtCmdStatusFailure(uint8_t status, uint16_t opcode)
{
#if 0
  handle these events:
  translate the command status event into other appropriate event
  HCI_OPCODE_DISCONNECT
  HCI_OPCODE_LE_CREATE_CONN
  HCI_OPCODE_LE_CONN_UPDATE
  HCI_OPCODE_LE_READ_REMOTE_FEAT
  HCI_OPCODE_LE_START_ENCRYPTION
  HCI_OPCODE_READ_REMOTE_VER_INFO
#endif

  if((opcode == HCI_OPCODE_LE_GENERATE_DHKEY)&&(status==HCI_ERR_INVALID_PARAM))
  {
    hciEvt_t      *pMsg;
    uint8_t       cbackEvt = 0;
    hciEvtCback_t cback = hciCb.secCback;

    cbackEvt = HCI_LE_GENERATE_DHKEY_CMPL_CBACK_EVT;

    /* allocate temp buffer */
    if ((pMsg = WsfBufAlloc(sizeof(wsfMsgHdr_t))) != NULL)
    {
      /* initialize message header */
      pMsg->hdr.param = 0;
      pMsg->hdr.event = cbackEvt;
      pMsg->hdr.status = status;

      /* execute callback */
      (*cback)(pMsg);

      /* free buffer */
      WsfBufFree(pMsg);
    }
 }
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessCmdStatus
 *
 *  \brief  Process HCI command status event.
 *
 *  \param  p    Buffer containing HCI event, points to start of parameters.
 *
 *  \return None.
 */
/*************************************************************************************************/
void hciEvtProcessCmdStatus(uint8_t *p)
{
  uint8_t   status;
  uint8_t   numPkts;
  uint16_t  opcode;

  BSTREAM_TO_UINT8(status, p);
  BSTREAM_TO_UINT8(numPkts, p);
  BSTREAM_TO_UINT16(opcode, p);

  if (status != HCI_SUCCESS)  /* optional: or vendor specific */
  {
    hciEvtCmdStatusFailure(status, opcode);
  }

  /* optional:  handle vendor-specific command status event */

  hciCmdRecvCmpl(numPkts);
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessCmdCmpl
 *
 *  \brief  Process HCI command complete event.
 *
 *  \param  p    Buffer containing command complete event, points to start of parameters.
 *
 *  \return None.
 */
/*************************************************************************************************/
void hciEvtProcessCmdCmpl(uint8_t *p, uint8_t len)
{
  uint8_t       numPkts;
  uint16_t      opcode;
  hciEvt_t      *pMsg;
  uint8_t       cbackEvt = 0;
  hciEvtCback_t cback = hciCb.evtCback;

  BSTREAM_TO_UINT8(numPkts, p);
  BSTREAM_TO_UINT16(opcode, p);

  /* convert opcode to internal event code and perform special handling */
  switch (opcode)
  {
  case HCI_OPCODE_LE_CREATE_CONN_CANCEL:
    cbackEvt = HCI_LE_CREATE_CONN_CANCEL_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_ENCRYPT:
    cbackEvt = HCI_LE_ENCRYPT_CMD_CMPL_CBACK_EVT;
    cback = hciCb.secCback;
    break;

  case HCI_OPCODE_LE_REM_CONN_PARAM_REP:
    cbackEvt = HCI_LE_REM_CONN_PARAM_REP_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_REM_CONN_PARAM_NEG_REP:
    cbackEvt = HCI_LE_REM_CONN_PARAM_NEG_REP_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_READ_DEF_DATA_LEN:
    cbackEvt = HCI_LE_READ_DEF_DATA_LEN_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_WRITE_DEF_DATA_LEN:
    cbackEvt = HCI_LE_WRITE_DEF_DATA_LEN_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_DATA_LEN:
    cbackEvt = HCI_LE_SET_DATA_LEN_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_READ_MAX_DATA_LEN:
    cbackEvt = HCI_LE_READ_MAX_DATA_LEN_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_LTK_REQ_REPL:
    cbackEvt = HCI_LE_LTK_REQ_REPL_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_LTK_REQ_NEG_REPL:
    cbackEvt = HCI_LE_LTK_REQ_NEG_REPL_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_RAND:
    cbackEvt = HCI_LE_RAND_CMD_CMPL_CBACK_EVT;
    cback = hciCb.secCback;
    break;

  case HCI_OPCODE_LE_READ_CHAN_MAP:
    cbackEvt = HCI_LE_READ_CHAN_MAP_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_READ_RSSI:
    cbackEvt = HCI_READ_RSSI_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_READ_TX_PWR_LVL:
    cbackEvt = HCI_READ_TX_PWR_LVL_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_ADD_DEV_RES_LIST:
    cbackEvt = HCI_LE_ADD_DEV_TO_RES_LIST_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_REMOVE_DEV_RES_LIST:
    cbackEvt = HCI_LE_REM_DEV_FROM_RES_LIST_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_CLEAR_RES_LIST:
    cbackEvt = HCI_LE_CLEAR_RES_LIST_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_READ_PEER_RES_ADDR:
    cbackEvt = HCI_LE_READ_PEER_RES_ADDR_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_READ_LOCAL_RES_ADDR:
    cbackEvt = HCI_LE_READ_LOCAL_RES_ADDR_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_ADDR_RES_ENABLE:
    cbackEvt = HCI_LE_SET_ADDR_RES_ENABLE_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_WRITE_AUTH_PAYLOAD_TO:
    cbackEvt = HCI_WRITE_AUTH_PAYLOAD_TO_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_READ_PHY:
    cbackEvt = HCI_LE_READ_PHY_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_DEF_PHY:
    cbackEvt = HCI_LE_SET_DEF_PHY_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_SCAN_ENABLE:
    cbackEvt = HCI_LE_SCAN_ENABLE_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_ADV_ENABLE:
    cbackEvt = HCI_LE_ADV_ENABLE_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_EXT_SCAN_ENABLE:
    cbackEvt = HCI_LE_EXT_SCAN_ENABLE_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_EXT_ADV_ENABLE:
    cbackEvt = HCI_LE_EXT_ADV_ENABLE_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_PER_ADV_ENABLE:
    cbackEvt = HCI_LE_PER_ADV_ENABLE_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_CONN_CTE_RX_PARAMS:
    cbackEvt = HCI_LE_SET_CONN_CTE_RX_PARAMS_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SET_CONN_CTE_TX_PARAMS:
    cbackEvt = HCI_LE_SET_CONN_CTE_TX_PARAMS_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_CONN_CTE_REQ_ENABLE:
    cbackEvt = HCI_LE_CONN_CTE_REQ_ENABLE_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_CONN_CTE_RSP_ENABLE:
    cbackEvt = HCI_LE_CONN_CTE_RSP_ENABLE_CMD_CMPL_CBACK_EVT;
    break;


  case HCI_OPCODE_LE_SET_CIG_PARAMS:
    cbackEvt = HCI_LE_SET_CIG_PARAMS_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_REMOVE_CIG:
    cbackEvt = HCI_LE_REMOVE_CIG_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_BIG_TERMINATE_SYNC:
    cbackEvt = HCI_LE_BIG_TERM_SYNC_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_SETUP_ISO_DATA_PATH:
    cbackEvt = HCI_LE_SETUP_ISO_DATA_PATH_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_LE_REMOVE_ISO_DATA_PATH:
    cbackEvt = HCI_LE_REMOVE_ISO_DATA_PATH_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_CONFIG_DATA_PATH:
    cbackEvt = HCI_CONFIG_DATA_PATH_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_READ_LOCAL_SUP_CODECS:
    cbackEvt = HCI_READ_LOCAL_SUP_CODECS_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_READ_LOCAL_SUP_CODEC_CAP:
    cbackEvt = HCI_READ_LOCAL_SUP_CODEC_CAP_CMD_CMPL_CBACK_EVT;
    break;

  case HCI_OPCODE_READ_LOCAL_SUP_CONTROLLER_DLY:
    cbackEvt = HCI_READ_LOCAL_SUP_CTR_DLY_CMD_CMPL_CBACK_EVT;
    break;

  default:
    /* test for vendor specific command completion OGF. */
    if (HCI_OGF(opcode) == HCI_OGF_VENDOR_SPEC)
    {
      cbackEvt = hciCoreVsCmdCmplRcvd(opcode, p, len);
    }
    break;
  }

  /* if callback is executed for this event */
  if (cbackEvt != 0)
  {
    /* allocate temp buffer */
    if ((pMsg = WsfBufAlloc(hciEvtCbackLen[cbackEvt])) != NULL)
    {
      /* initialize message header */
      pMsg->hdr.param = 0;
      pMsg->hdr.event = cbackEvt;
      pMsg->hdr.status = 0;

      /* execute parsing function for the event */
      (*hciEvtParseFcnTbl[cbackEvt])(pMsg, p, len);

      /* execute callback */
      (*cback)(pMsg);

      /* free buffer */
      WsfBufFree(pMsg);
    }
  }

  hciCmdRecvCmpl(numPkts);
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtProcessMsg
 *
 *  \brief  Process received HCI events.
 *
 *  \param  pEvt    Buffer containing HCI event.
 *
 *  \return None.
 */
/*************************************************************************************************/
void hciEvtProcessMsg(uint8_t *pEvt)
{
  uint8_t   evt;
  uint8_t   subEvt;
  uint8_t   len;
  uint8_t   cbackEvt = 0;
  hciEvt_t  *pMsg;
  uint16_t  handle;
  hciEvtCback_t cback = hciCb.evtCback;

  /* parse HCI event header */
  BSTREAM_TO_UINT8(evt, pEvt);
  BSTREAM_TO_UINT8(len, pEvt);

  /* convert hci event code to internal event code and perform special handling */
  switch (evt)
  {
    case HCI_CMD_STATUS_EVT:
      /* special handling for command status event */
      hciEvtStats.numCmdStatusEvt++;
      hciEvtProcessCmdStatus(pEvt);
      break;

    case HCI_CMD_CMPL_EVT:
      /* special handling for command complete event */
      hciEvtStats.numCmdCmplEvt++;
      hciEvtProcessCmdCmpl(pEvt, len);
#if defined(HCI_CONN_CANCEL_WORKAROUND) && (HCI_CONN_CANCEL_WORKAROUND == TRUE)
      /* workaround for controllers that don't send an LE connection complete event
       * after a connection cancel command
       */
      {
        uint16_t opcode;
        BYTES_TO_UINT16(opcode, (pEvt + 1));
        if (opcode == HCI_OPCODE_LE_CREATE_CONN_CANCEL)
        {
          cbackEvt = HCI_LE_CONN_CMPL_CBACK_EVT;
        }
      }
#endif
      break;

    case HCI_NUM_CMPL_PKTS_EVT:
      /* handled internally by hci */
      hciCoreNumCmplPkts(pEvt);
      hciEvtStats.numCmplPktsEvt++;
      break;

    case HCI_LE_META_EVT:
      BSTREAM_TO_UINT8(subEvt, pEvt);
      hciEvtStats.numLeMetaEvt++;
      APP_TRACE_INFO1("LE meta sub event = 0x%x", subEvt);
      switch (subEvt)
      {
        case HCI_LE_CONN_CMPL_EVT:
          /* if connection created successfully */
          if (*pEvt == HCI_SUCCESS)
          {
            BYTES_TO_UINT16(handle, (pEvt + 1));
            hciCoreConnOpen(handle);
          }
          cbackEvt = HCI_LE_CONN_CMPL_CBACK_EVT;
          break;

        case HCI_LE_ADV_REPORT_EVT:
          /* special case for advertising report */
          hciEvtProcessLeAdvReport(pEvt, len);
          break;

        case HCI_LE_CONN_UPDATE_CMPL_EVT:
          cbackEvt = HCI_LE_CONN_UPDATE_CMPL_CBACK_EVT;
          break;

        case HCI_LE_READ_REMOTE_FEAT_CMPL_EVT:
          cbackEvt = HCI_LE_READ_REMOTE_FEAT_CMPL_CBACK_EVT;
          break;

        case HCI_LE_LTK_REQ_EVT:
          cbackEvt = HCI_LE_LTK_REQ_CBACK_EVT;
          break;

        case HCI_LE_ENHANCED_CONN_CMPL_EVT:
          /* if connection created successfully */
          if (*pEvt == HCI_SUCCESS)
          {
            BYTES_TO_UINT16(handle, (pEvt + 1));
            hciCoreConnOpen(handle);
          }
          cbackEvt = HCI_LE_ENHANCED_CONN_CMPL_CBACK_EVT;
          break;

        case HCI_LE_DIRECT_ADV_REPORT_EVT:
          /* special case for direct advertising report */
          hciEvtProcessLeDirectAdvReport(pEvt, len);
          break;

        case HCI_LE_REM_CONN_PARAM_REQ_EVT:
          cbackEvt = HCI_LE_REM_CONN_PARAM_REQ_CBACK_EVT;
          break;

        case HCI_LE_DATA_LEN_CHANGE_EVT:
          cbackEvt = HCI_LE_DATA_LEN_CHANGE_CBACK_EVT;
          break;

        case HCI_LE_READ_LOCAL_P256_PUB_KEY_CMPL_EVT:
          cback = hciCb.secCback;
          cbackEvt = HCI_LE_READ_LOCAL_P256_PUB_KEY_CMPL_CBACK_EVT;
          break;

        case HCI_LE_GENERATE_DHKEY_CMPL_EVT:
          cback = hciCb.secCback;
          cbackEvt = HCI_LE_GENERATE_DHKEY_CMPL_CBACK_EVT;
          break;

        case HCI_LE_PHY_UPDATE_CMPL_EVT:
          cbackEvt = HCI_LE_PHY_UPDATE_CMPL_CBACK_EVT;
          break;

        case HCI_LE_EXT_ADV_REPORT_EVT:
          /* special case for extended advertising report */
          hciEvtProcessLeExtAdvReport(pEvt, len);
          break;

        case HCI_LE_SCAN_TIMEOUT_EVT:
          cbackEvt = HCI_LE_SCAN_TIMEOUT_CBACK_EVT;
          break;

        case HCI_LE_ADV_SET_TERM_EVT:
          cbackEvt = HCI_LE_ADV_SET_TERM_CBACK_EVT;
          break;

        case HCI_LE_SCAN_REQ_RCVD_EVT:
          cbackEvt = HCI_LE_SCAN_REQ_RCVD_CBACK_EVT;
          break;

        case HCI_LE_PER_ADV_SYNC_EST_EVT:
          cbackEvt = HCI_LE_PER_ADV_SYNC_EST_CBACK_EVT;
          break;

        case HCI_LE_PER_ADV_REPORT_EVT:
          /* special case for periodic advertising report */
          hciEvtProcessLePerAdvReport(pEvt, len);
          break;

        case HCI_LE_PER_ADV_SYNC_LOST_EVT:
          cbackEvt = HCI_LE_PER_ADV_SYNC_LOST_CBACK_EVT;
          break;

        case HCI_LE_CONN_IQ_REPORT_EVT:
          /* special case for LE Connection IQ report */
          hciEvtProcessLeConnIQReport(pEvt, len);

          break;
        case HCI_LE_CTE_REQ_FAILED_EVT:
        APP_TRACE_INFO0("cte req failed....");
        break;
        case HCI_LE_CONNLESS_IQ_REPORT_EVT:
          /* special case for LE Connectionless IQ report */
          hciEvtProcessLeConlessIQReport(pEvt, len);

          break;

        case HCI_LE_CIS_EST_EVT:
          /* if CIS connection created successfully */
          if (*pEvt == HCI_SUCCESS)
          {
            BYTES_TO_UINT16(handle, (pEvt + 1));
            hciCoreCisOpen(handle);
          }
          cbackEvt = HCI_LE_CIS_EST_CBACK_EVT;
          break;

        case HCI_LE_CIS_REQ_EVT:
          cbackEvt = HCI_LE_CIS_REQ_CBACK_EVT;
          break;

        case HCI_LE_CREATE_BIG_CMPL_EVT:
          cbackEvt = HCI_LE_CREATE_BIG_CMPL_CBACK_EVT;
          break;

        case HCI_LE_TERMINATE_BIG_CMPL_EVT:
          cbackEvt = HCI_LE_TERM_BIG_CMPL_CBACK_EVT;
          break;

        case HCI_LE_BIG_SYNC_EST_EVT:
          cbackEvt = HCI_LE_BIG_SYNC_EST_CBACK_EVT;
          break;

        case HCI_LE_BIG_SYNC_LOST_EVT:
          cbackEvt = HCI_LE_BIG_SYNC_LOST_CBACK_EVT;
          break;

        case HCI_LE_REQ_PEER_SCA_CMPLT_EVT:
          cbackEvt = HCI_LE_REQ_PEER_SCA_CBACK_EVT;
          break;

        case HCI_LE_BIG_INFO_ADV_REPORT_EVT:
          cbackEvt = HCI_LE_BIG_INFO_ADV_REPORT_CBACK_EVT;
          break;

        default:
          break;
      }
      break;

    case HCI_DISCONNECT_CMPL_EVT:
      hciEvtStats.numDiscCmplEvt++;

      /* if disconnect is for CIS connection */
      BYTES_TO_UINT16(handle, (pEvt + 1));
      if (hciCoreCisByHandle(handle) != NULL)
      {
        cbackEvt = HCI_CIS_DISCONNECT_CMPL_CBACK_EVT;
      }
      else
      {
        cbackEvt = HCI_DISCONNECT_CMPL_CBACK_EVT;
      }
      break;

    case HCI_ENC_CHANGE_EVT:
      hciEvtStats.numEncChangeEvt++;
      cbackEvt = HCI_ENC_CHANGE_CBACK_EVT;
      break;

    case HCI_READ_REMOTE_VER_INFO_CMPL_EVT:
      hciEvtStats.numReadRemoteVerInfoCmpEvt++;
      cbackEvt = HCI_READ_REMOTE_VER_INFO_CMPL_CBACK_EVT;
      break;

    case HCI_ENC_KEY_REFRESH_CMPL_EVT:
      hciEvtStats.numEncKeyRefreshCmplEvt++;
      cbackEvt = HCI_ENC_KEY_REFRESH_CMPL_CBACK_EVT;
      break;

    case HCI_DATA_BUF_OVERFLOW_EVT:
      /* handled internally by hci */
      /* optional */
      hciEvtStats.numDataBufOverflowEvt++;
      break;

    case HCI_HW_ERROR_EVT:
      hciEvtStats.numHwErrorEvt++;
      cbackEvt = HCI_HW_ERROR_CBACK_EVT;
      break;

    case HCI_AUTH_PAYLOAD_TIMEOUT_EVT:
      hciEvtStats.numAuthToEvt++;
      cbackEvt = HCI_AUTH_PAYLOAD_TO_EXPIRED_CBACK_EVT;
      break;

    case HCI_VENDOR_SPEC_EVT:
      /* special case for vendor specific event */
      /* optional */
#if defined (HCI_NONSTANDARD_VS_CMPL) && (HCI_NONSTANDARD_VS_CMPL == TRUE)
      /* for nonstandard controllers that send a vendor-specific event instead
       * of a command complete event
       */
      hciCmdRecvCmpl(1);
#endif
      hciEvtStats.numVendorSpecEvt++;
      cbackEvt = HCI_VENDOR_SPEC_CBACK_EVT;
      break;

    default:
      break;
  }

  /* if callback is executed for this event */
  if (cbackEvt != 0)
  {
    /* allocate temp buffer */
    if ((pMsg = WsfBufAlloc(hciEvtCbackLen[cbackEvt])) != NULL)
    {
      /* initialize message header */
      pMsg->hdr.param = 0;
      pMsg->hdr.event = cbackEvt;
      pMsg->hdr.status = 0;

      /* execute parsing function for the event */
      (*hciEvtParseFcnTbl[cbackEvt])(pMsg, pEvt, len);

      /* execute callback */
      (*cback)(pMsg);

      /* free buffer */
      WsfBufFree(pMsg);
    }

    /* execute core procedure for connection close after callback */
    if (cbackEvt == HCI_DISCONNECT_CMPL_CBACK_EVT)
    {
      BYTES_TO_UINT16(handle, (pEvt + 1));
      hciCoreConnClose(handle);
    }
    else if (cbackEvt == HCI_CIS_DISCONNECT_CMPL_CBACK_EVT)
    {
      BYTES_TO_UINT16(handle, (pEvt + 1));
      hciCoreCisClose(handle);
    }
  }
}

/*************************************************************************************************/
/*!
 *  \fn     hciEvtGetStats
 *
 *  \brief  Get event statistics.
 *
 *  \return Event statistics.
 */
/*************************************************************************************************/
hciEvtStats_t *hciEvtGetStats(void)
{
  return &hciEvtStats;
}
