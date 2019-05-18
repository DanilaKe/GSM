#include "ril.h"
#include "ril_util.h"
#include "ril_sms.h"
#include "ril_telephony.h"
#include "ril_system.h"
#include "ql_stdlib.h"
#include "ql_error.h"
#include "ql_trace.h"
#include "ql_uart.h"
#include "ql_system.h"
#include "ql_memory.h"
#include "ql_timer.h"

#if (defined(__OCPU_RIL_SUPPORT__) && defined(__OCPU_RIL_SMS_SUPPORT__))
#define DEBUG_ENABLE 1
#if DEBUG_ENABLE > 0
#define DEBUG_PORT  UART_PORT1
#define DBG_BUF_LEN   512
static char DBG_BUFFER[DBG_BUF_LEN];
#define APP_DEBUG(FORMAT,...) {\
    Ql_memset(DBG_BUFFER, 0, DBG_BUF_LEN);\
    Ql_sprintf(DBG_BUFFER,FORMAT,##__VA_ARGS__); \
    if (UART_PORT2 == (DEBUG_PORT)) \
    {\
        Ql_Debug_Trace(DBG_BUFFER);\
    } else {\
        Ql_UART_Write((Enum_SerialPort)(DEBUG_PORT), (u8*)(DBG_BUFFER), Ql_strlen((const char *)(DBG_BUFFER)));\
    }\
}
#else
#define APP_DEBUG(FORMAT,...)
#endif

#define CON_SMS_BUF_MAX_CNT   (1)
#define CON_SMS_SEG_MAX_CHAR  (160)
#define CON_SMS_SEG_MAX_BYTE  (4 * CON_SMS_SEG_MAX_CHAR)
#define CON_SMS_MAX_SEG       (7)

typedef struct
{
    u8 aData[CON_SMS_SEG_MAX_BYTE];
    u16 uLen;
} ConSMSSegStruct;

typedef struct
{
    u16 uMsgRef;
    u8 uMsgTot;

    ConSMSSegStruct asSeg[CON_SMS_MAX_SEG];
    bool abSegValid[CON_SMS_MAX_SEG];
} ConSMSStruct;

static bool ConSMSBuf_IsIntact(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx,ST_RIL_SMS_Con *pCon);
static bool ConSMSBuf_AddSeg(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx,ST_RIL_SMS_Con *pCon,u8 *pData,u16 uLen);
static s8 ConSMSBuf_GetIndex(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,ST_RIL_SMS_Con *pCon);
static bool ConSMSBuf_ResetCtx(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx);
static void Erase_flags(void);

ConSMSStruct g_asConSMSBuf[CON_SMS_BUF_MAX_CNT];
char strPhNum[] = "375298244889";        // Number Phone
char pass[]="1111";                   // Password of board
Enum_PinName  device1 = PINNAME_NETLIGHT;
Enum_PinName  device2 = PINNAME_DTR;
Enum_PinName  device3 = PINNAME_RI;
Enum_PinName  device4 = PINNAME_DCD;

char r1_on[]="on 1";               // On device 1
char r1_off[]="off 1";             // Off device 1
char r2_on[]="on 2";               // On device 2
char r2_off[]="off 2";             // Off device 2
char r3_on[]="on 3";               // On device 3
char r3_off[]="off 3";             // Off device 3
char r4_on[]="on 4";               // On device 4
char r4_off[]="off 4";             // Off device 4
short mode1=FALSE;
short mode2=FALSE;
short mode3=FALSE;
short mode4=FALSE;
short flag1=FALSE;
short flag2=FALSE;
short flag3=FALSE;
short flag4=FALSE;
short flagS=FALSE;

static s8 ConSMSBuf_GetIndex(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,ST_RIL_SMS_Con *pCon)
{
	u8 uIdx = 0;
    if((NULL == pCSBuf) || (0 == uCSMaxCnt) || (NULL == pCon))
    {
        APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! Parameter is INVALID. pCSBuf:%x,uCSMaxCnt:%d,pCon:%x\r\n",pCSBuf,uCSMaxCnt,pCon);
        return -1;
    }
    if((pCon->msgTot) > CON_SMS_MAX_SEG)
    {
        APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! msgTot:%d is larger than limit:%d\r\n",pCon->msgTot,CON_SMS_MAX_SEG);
        return -1;
    }
	for(uIdx = 0; uIdx < uCSMaxCnt; uIdx++)  //Match all exist records
	{
        if((pCon->msgRef == pCSBuf[uIdx].uMsgRef) && (pCon->msgTot == pCSBuf[uIdx].uMsgTot))
        {
            return uIdx;
        }
	}
	for (uIdx = 0; uIdx < uCSMaxCnt; uIdx++)
	{
		if (0 == pCSBuf[uIdx].uMsgTot)  //Find the first unused record
		{
            pCSBuf[uIdx].uMsgTot = pCon->msgTot;
            pCSBuf[uIdx].uMsgRef = pCon->msgRef;
			return uIdx;
		}
	}
    APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! No avail index in ConSMSBuf,uCSMaxCnt:%d\r\n",uCSMaxCnt);
	return -1;
}

static bool ConSMSBuf_AddSeg(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx,ST_RIL_SMS_Con *pCon,u8 *pData,u16 uLen)
{
    u8 uSeg = 1;

    if((NULL == pCSBuf) || (0 == uCSMaxCnt) || (uIdx >= uCSMaxCnt) || (NULL == pCon) || (NULL == pData) || (uLen > (CON_SMS_SEG_MAX_CHAR * 4)))
    {
        APP_DEBUG("Enter ConSMSBuf_AddSeg,FAIL! Parameter is INVALID. pCSBuf:%x,uCSMaxCnt:%d,uIdx:%d,pCon:%x,pData:%x,uLen:%d\r\n",pCSBuf,uCSMaxCnt,uIdx,pCon,pData,uLen);
        return FALSE;
    }
    if((pCon->msgTot) > CON_SMS_MAX_SEG)
    {
        APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! msgTot:%d is larger than limit:%d\r\n",pCon->msgTot,CON_SMS_MAX_SEG);
        return FALSE;
    }
    uSeg = pCon->msgSeg;
    pCSBuf[uIdx].abSegValid[uSeg-1] = TRUE;
    Ql_memcpy(pCSBuf[uIdx].asSeg[uSeg-1].aData,pData,uLen);
    pCSBuf[uIdx].asSeg[uSeg-1].uLen = uLen;

	return TRUE;
}

static bool ConSMSBuf_IsIntact(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx,ST_RIL_SMS_Con *pCon)
{
    u8 uSeg = 1;

    if((NULL == pCSBuf) || (0 == uCSMaxCnt) || (uIdx >= uCSMaxCnt) || (NULL == pCon))
    {
        APP_DEBUG("Enter ConSMSBuf_IsIntact,FAIL! Parameter is INVALID. pCSBuf:%x,uCSMaxCnt:%d,uIdx:%d,pCon:%x\r\n",pCSBuf,uCSMaxCnt,uIdx,pCon);
        return FALSE;
    }

    if((pCon->msgTot) > CON_SMS_MAX_SEG)
    {
        APP_DEBUG("Enter ConSMSBuf_GetIndex,FAIL! msgTot:%d is larger than limit:%d\r\n",pCon->msgTot,CON_SMS_MAX_SEG);
        return FALSE;
    }

	for (uSeg = 1; uSeg <= (pCon->msgTot); uSeg++)
	{
        if(FALSE == pCSBuf[uIdx].abSegValid[uSeg-1])
        {
            APP_DEBUG("Enter ConSMSBuf_IsIntact,FAIL! uSeg:%d has not received!\r\n",uSeg);
            return FALSE;
        }
	}

    return TRUE;
}

/*****************************************************************************
 * FUNCTION
 *  ConSMSBuf_ResetCtx
 *
 * DESCRIPTION
 *  This function is used to reset ConSMSBuf context
 *
 * PARAMETERS
 *  <pCSBuf>     The SMS index in storage,it starts from 1
 *  <uCSMaxCnt>  TRUE: The module should reply a SMS to the sender; FALSE: The module only read this SMS.
 *  <uIdx>       Index of <pCSBuf> which will be stored
 *
 * RETURNS
 *  FALSE:   FAIL!
 *  TRUE: SUCCESS.
 *
 * NOTE
 *  1. This is an internal function
 *****************************************************************************/
static bool ConSMSBuf_ResetCtx(ConSMSStruct *pCSBuf,u8 uCSMaxCnt,u8 uIdx)
{
    if(    (NULL == pCSBuf) || (0 == uCSMaxCnt)
        || (uIdx >= uCSMaxCnt)
      )
    {
        APP_DEBUG("Enter ConSMSBuf_ResetCtx,FAIL! Parameter is INVALID. pCSBuf:%x,uCSMaxCnt:%d,uIdx:%d\r\n",pCSBuf,uCSMaxCnt,uIdx);
        return FALSE;
    }

    //Default reset
    Ql_memset(&pCSBuf[uIdx],0x00,sizeof(ConSMSStruct));

    //TODO: Add special reset here

    return TRUE;
}

/*****************************************************************************
 * FUNCTION
 *  SMS_Initialize
 *
 * DESCRIPTION
 *  Initialize SMS environment.
 *
 * PARAMETERS
 *  VOID
 *
 * RETURNS
 *  TRUE:  This function works SUCCESS.
 *  FALSE: This function works FAIL!
 *****************************************************************************/
static bool SMS_Initialize(void)
{
    s32 iResult = 0;
    u8  nCurrStorage = 0;
    u32 nUsed = 0;
    u32 nTotal = 0;

    // Set SMS storage:
    // By default, short message is stored into SIM card. You can change the storage to ME if needed, or
    // you can do it again to make sure the short message storage is SIM card.
    #if 0
    {
        iResult = RIL_SMS_SetStorage(RIL_SMS_STORAGE_TYPE_SM,&nUsed,&nTotal);
        if (RIL_ATRSP_SUCCESS != iResult)
        {
            APP_DEBUG("Fail to set SMS storage, cause:%d\r\n", iResult);
            return FALSE;
        }
        APP_DEBUG("<-- Set SMS storage to SM, nUsed:%u,nTotal:%u -->\r\n", nUsed, nTotal);

        iResult = RIL_SMS_GetStorage(&nCurrStorage, &nUsed ,&nTotal);
        if(RIL_ATRSP_SUCCESS != iResult)
        {
            APP_DEBUG("Fail to get SMS storage, cause:%d\r\n", iResult);
            return FALSE;
        }
        APP_DEBUG("<-- Check SMS storage: curMem=%d, used=%d, total=%d -->\r\n", nCurrStorage, nUsed, nTotal);
    }
    #endif

    // Enable new short message indication
    // By default, the auto-indication for new short message is enalbed. You can do it again to
    // make sure that the option is open.
    #if 0
    {
        iResult = Ql_RIL_SendATCmd("AT+CNMI=2,1",Ql_strlen("AT+CNMI=2,1"),NULL,NULL,0);
        if (RIL_AT_SUCCESS != iResult)
        {
            APP_DEBUG("Fail to send \"AT+CNMI=2,1\", cause:%d\r\n", iResult);
            return FALSE;
        }
        APP_DEBUG("<-- Enable new SMS indication -->\r\n");
    }
    #endif

    // Delete all existed short messages (if needed)
    iResult = RIL_SMS_DeleteSMS(0, RIL_SMS_DEL_ALL_MSG);
    if (iResult != RIL_AT_SUCCESS)
    {
        APP_DEBUG("Fail to delete all messages, iResult=%d,cause:%d\r\n", iResult, Ql_RIL_AT_GetErrCode());
        return FALSE;
    }
    APP_DEBUG("Delete all existed messages\r\n");

    return TRUE;
}

void SMS_TextMode_Read(u32 nIndex)
{
    s32 iResult;
    int i;
    u8 datat[1];
    ST_RIL_SMS_TextInfo *pTextInfo = NULL;
    ST_RIL_SMS_DeliverParam *pDeliverTextInfo = NULL;
    ST_RIL_SMS_SubmitParam *pSubmitTextInfo = NULL;
    LIB_SMS_CharSetEnum eCharSet = LIB_SMS_CHARSET_GSM;

    pTextInfo = Ql_MEM_Alloc(sizeof(ST_RIL_SMS_TextInfo));
    if (NULL == pTextInfo)
    {
        return;
    }

    Ql_memset(pTextInfo,0x00,sizeof(ST_RIL_SMS_TextInfo));
    iResult = RIL_SMS_ReadSMS_Text(nIndex, eCharSet, pTextInfo);
    if (iResult != RIL_AT_SUCCESS)
    {
        Ql_MEM_Free(pTextInfo);
        APP_DEBUG("< Fail to read PDU SMS, cause:%d >\r\n", iResult);
        return;
    }
    if (RIL_SMS_STATUS_TYPE_INVALID == (pTextInfo->status))
    {
        APP_DEBUG("<-- SMS[index=%d] doesn't exist -->\r\n", nIndex);
        return;
    }

    // Resolve the read short message
    if (LIB_SMS_PDU_TYPE_DELIVER == (pTextInfo->type))
    {
        pDeliverTextInfo = &((pTextInfo->param).deliverParam);
        APP_DEBUG("<-- Read short message (index:%u) with charset %d -->\r\n", nIndex, eCharSet);

        if(FALSE == pDeliverTextInfo->conPres) //Normal SMS
        {
            APP_DEBUG(
                "short message info: \r\n\tstatus:%u \r\n\ttype:%u \r\n\talpha:%u \r\n\tsca:%s \r\n\toa:%s \r\n\tscts:%s \r\n\tdata length:%u\r\ncp:0,cy:0,cr:0,ct:0,cs:0\r\n",
                    (pTextInfo->status),
                    (pTextInfo->type),
                    (pDeliverTextInfo->alpha),
                    (pTextInfo->sca),
                    (pDeliverTextInfo->oa),
                    (pDeliverTextInfo->scts),
                    (pDeliverTextInfo->length)
           );
        }
        else
        {
            APP_DEBUG(
                "short message info: \r\n\tstatus:%u \r\n\ttype:%u \r\n\talpha:%u \r\n\tsca:%s \r\n\toa:%s \r\n\tscts:%s \r\n\tdata length:%u\r\ncp:1,cy:%d,cr:%d,ct:%d,cs:%d\r\n",
                    (pTextInfo->status),
                    (pTextInfo->type),
                    (pDeliverTextInfo->alpha),
                    (pTextInfo->sca),
                    (pDeliverTextInfo->oa),
                    (pDeliverTextInfo->scts),
                    (pDeliverTextInfo->length),
                    pDeliverTextInfo->con.msgType,
                    pDeliverTextInfo->con.msgRef,
                    pDeliverTextInfo->con.msgTot,
                    pDeliverTextInfo->con.msgSeg
           );
        }

        APP_DEBUG("\r\n\tmessage content:");
        APP_DEBUG("%s\r\n",(pDeliverTextInfo->data));
        APP_DEBUG("\r\n");
    }
    else if (LIB_SMS_PDU_TYPE_SUBMIT == (pTextInfo->type))
    {// short messages in sent-list of drafts-list
    } else {
        APP_DEBUG("<-- Unkown short message type! type:%d -->\r\n", (pTextInfo->type));
    }
    Ql_MEM_Free(pTextInfo);
}

void SMS_TextMode_Send(int type)
{
    s32 iResult;
    u32 nMsgRef;
    char strTextMsg[] = "GSM Function is ready.\0";
    char str_on1[]="Device 1: ON";
    char str_off1[]="Device 1: OFF";
    char str_on2[]="Device 2: ON";
    char str_off2[]="Device 2: OFF";
    char str_on3[]="Device 3: ON";
    char str_off3[]="Device 3: OFF";
    char str_on4[]="Device 4: ON";
    char str_off4[]="Device 4: OFF";

    ST_RIL_SMS_SendExt sExt;

    //Initialize
    Ql_memset(&sExt,0x00,sizeof(sExt));

    if(1==type)
      {
         APP_DEBUG("< Send 'GSM Function is ready.' >\r\n");
         iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, strTextMsg, Ql_strlen(strTextMsg), &nMsgRef);
         if (iResult != RIL_AT_SUCCESS)
            {
              APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
              return;
            }
         APP_DEBUG("< Send SMS successfully, MsgRef:%u >\r\n", nMsgRef);
      }
    else
      {
    	if(flag1==TRUE)
    	           {
    		         switch (mode1)
    	                 {
    	                   case TRUE:
    	                         {
    	                        	 APP_DEBUG("< Send status 'on 1' of device >\r\n");
    	                             iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, str_on1, Ql_strlen(str_on1), &nMsgRef);
    	                             if (iResult != RIL_AT_SUCCESS)
    	                                {
    	                                  APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
    	                                  return;
    	                                }
    	                        	 break;
    	                         }
    	                   case FALSE:
    	                         {
    	                        	 APP_DEBUG("< Send status 'off 1' of device >\r\n");
    	                             iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, str_off1, Ql_strlen(str_off1), &nMsgRef);
    	                             if (iResult != RIL_AT_SUCCESS)
    	                                {
    	                                  APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
    	                                  return;
    	                                }
    	                        	 break;
    	                         }
    	                   default: break;

    	                 }
    	           }
    	if(flag2==TRUE)
    	           {
    		         switch (mode2)
    	                 {
    	                   case TRUE:
    	                         {
    	                        	 APP_DEBUG("< Send status 'on 2' of device >\r\n");
    	                             iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, str_on2, Ql_strlen(str_on2), &nMsgRef);
    	                             if (iResult != RIL_AT_SUCCESS)
    	                                {
    	                                  APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
    	                                  return;
    	                                }
    	                        	 break;
    	                         }
    	                   case FALSE:
    	                         {
    	                        	 APP_DEBUG("< Send status 'off 2' of device >\r\n");
    	                             iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, str_off2, Ql_strlen(str_off2), &nMsgRef);
    	                             if (iResult != RIL_AT_SUCCESS)
    	                                {
    	                                  APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
    	                                  return;
    	                                }
    	                        	 break;
    	                         }
    	                   default: break;

    	                 }
    	           }
    	if(flag3==TRUE)
    	           {
    		         switch (mode3)
    	                 {
    	                   case TRUE:
    	                         {
    	                        	 APP_DEBUG("< Send status 'on 3' of device >\r\n");
    	                             iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, str_on3, Ql_strlen(str_on3), &nMsgRef);
    	                             if (iResult != RIL_AT_SUCCESS)
    	                                {
    	                                  APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
    	                                  return;
    	                                }
    	                        	 break;
    	                         }
    	                   case FALSE:
    	                         {
    	                        	 APP_DEBUG("< Send status 'off 3' of device >\r\n");
    	                             iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, str_off3, Ql_strlen(str_off3), &nMsgRef);
    	                             if (iResult != RIL_AT_SUCCESS)
    	                                {
    	                                  APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
    	                                  return;
    	                                }
    	                        	 break;
    	                         }
    	                   default: break;

    	                 }
    	           }
    	if(flag4==TRUE)
    	           {
    		         switch (mode4)
    	                 {
    	                   case TRUE:
    	                         {
    	                        	 APP_DEBUG("< Send status 'on 4' of device >\r\n");
    	                             iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, str_on4, Ql_strlen(str_on4), &nMsgRef);
    	                             if (iResult != RIL_AT_SUCCESS)
    	                                {
    	                                  APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
    	                                  return;
    	                                }
    	                        	 break;
    	                         }
    	                   case FALSE:
    	                         {
    	                        	 APP_DEBUG("< Send status 'off 4' of device >\r\n");
    	                             iResult = RIL_SMS_SendSMS_Text(strPhNum, Ql_strlen(strPhNum), LIB_SMS_CHARSET_GSM, str_off4, Ql_strlen(str_off4), &nMsgRef);
    	                             if (iResult != RIL_AT_SUCCESS)
    	                                {
    	                                  APP_DEBUG("< Fail to send Text SMS, iResult=%d, cause:%d >\r\n", iResult, Ql_RIL_AT_GetErrCode());
    	                                  return;
    	                                }
    	                        	 break;
    	                         }
    	                   default: break;

    	                 }
    	           }

      }

}

void SMS_PDUMode_Read(u32 nIndex)
{
    s32 iResult;
    ST_RIL_SMS_PDUInfo *pPDUInfo = NULL;

    pPDUInfo = Ql_MEM_Alloc(sizeof(ST_RIL_SMS_PDUInfo));
    if (NULL == pPDUInfo)
    {
        return;
    }

    iResult = RIL_SMS_ReadSMS_PDU(nIndex, pPDUInfo);
    if (RIL_AT_SUCCESS != iResult)
    {
        Ql_MEM_Free(pPDUInfo);
        APP_DEBUG("< Fail to read PDU SMS, cause:%d >\r\n", iResult);
        return;
    }

    do
    {
        if (RIL_SMS_STATUS_TYPE_INVALID == (pPDUInfo->status))
        {
            APP_DEBUG("<-- SMS[index=%d] doesn't exist -->\r\n", nIndex);
            break;
        }

        APP_DEBUG("<-- Send Text SMS[index=%d] successfully -->\r\n", nIndex);
        APP_DEBUG("status:%u,data length:%u\r\n", (pPDUInfo->status), (pPDUInfo->length));
        APP_DEBUG("data = %s\r\n",(pPDUInfo->data));
    } while(0);

    Ql_MEM_Free(pPDUInfo);
}

void SMS_PDUMode_Send(void)
{
    s32 iResult;
    u32 nMsgRef;
    char pduStr[] = "1234923asdf";
    iResult = RIL_SMS_SendSMS_PDU(pduStr, sizeof(pduStr), &nMsgRef);
    if (RIL_AT_SUCCESS != iResult)
    {
        APP_DEBUG("< Fail to send PDU SMS, cause:%d >\r\n", iResult);
        return;
    }
    APP_DEBUG("< Send PDU SMS successfully, MsgRef:%u >\r\n", nMsgRef);

}

/*****************************************************************************
 * FUNCTION
 *  Hdlr_RecvNewSMS
 *
 * DESCRIPTION
 *  The handler function of new received SMS.
 *
 * PARAMETERS
 *  <nIndex>     The SMS index in storage,it starts from 1
 *  <bAutoReply> TRUE: The module should reply a SMS to the sender;
 *               FALSE: The module only read this SMS.
 *
 * RETURNS
 *  VOID
 *
 * NOTE
 *  1. This is an internal function
 *****************************************************************************/
static void Hdlr_RecvNewSMS(u32 nIndex, bool bAutoReply)
{
    s32 iResult = 0;
    u32 uMsgRef = 0;
    int i,smsmode=0;
    char *ptr1=0,*ptr2=0;
    char char_temp_gsm[1]={0x00},char_rec_gsm[1]={0x00};
    char control[]="control";                                             // Keyword for control device from your phone - Syntax for control device: #control <password>.
    ST_RIL_SMS_TextInfo *pTextInfo = NULL;
    ST_RIL_SMS_DeliverParam *pDeliverTextInfo = NULL;
    char aPhNum[RIL_SMS_PHONE_NUMBER_MAX_LEN] = {0,};
    const char aReplyCon[] = {"Module has received SMS."};
    bool bResult = FALSE;

    pTextInfo = Ql_MEM_Alloc(sizeof(ST_RIL_SMS_TextInfo));
    if (NULL == pTextInfo)
    {
        APP_DEBUG("%s/%d:Ql_MEM_Alloc FAIL! size:%u\r\n", sizeof(ST_RIL_SMS_TextInfo), __func__, __LINE__);
        return;
    }
    Ql_memset(pTextInfo, 0x00, sizeof(ST_RIL_SMS_TextInfo));
    iResult = RIL_SMS_ReadSMS_Text(nIndex, LIB_SMS_CHARSET_GSM, pTextInfo);
    if (iResult != RIL_AT_SUCCESS)
    {
        Ql_MEM_Free(pTextInfo);
        APP_DEBUG("Fail to read text SMS[%d], cause:%d\r\n", nIndex, iResult);
        return;
    }

    if ((LIB_SMS_PDU_TYPE_DELIVER != (pTextInfo->type)) || (RIL_SMS_STATUS_TYPE_INVALID == (pTextInfo->status)))
    {
        Ql_MEM_Free(pTextInfo);
        APP_DEBUG("WARNING: NOT a new received SMS.\r\n");
        return;
    }

    pDeliverTextInfo = &((pTextInfo->param).deliverParam);

    if(TRUE == pDeliverTextInfo->conPres)  //Receive CON-SMS segment
    {
        s8 iBufIdx = 0;
        u8 uSeg = 0;
        u16 uConLen = 0;

        iBufIdx = ConSMSBuf_GetIndex(g_asConSMSBuf,CON_SMS_BUF_MAX_CNT,&(pDeliverTextInfo->con));
        if(-1 == iBufIdx)
        {
            APP_DEBUG("Enter Hdlr_RecvNewSMS,WARNING! ConSMSBuf_GetIndex FAIL! Show this CON-SMS-SEG directly!\r\n");

            APP_DEBUG(
                "status:%u,type:%u,alpha:%u,sca:%s,oa:%s,scts:%s,data length:%u,cp:1,cy:%d,cr:%d,ct:%d,cs:%d\r\n",
                    (pTextInfo->status),
                    (pTextInfo->type),
                    (pDeliverTextInfo->alpha),
                    (pTextInfo->sca),
                    (pDeliverTextInfo->oa),
                    (pDeliverTextInfo->scts),
                    (pDeliverTextInfo->length),
                    pDeliverTextInfo->con.msgType,
                    pDeliverTextInfo->con.msgRef,
                    pDeliverTextInfo->con.msgTot,
                    pDeliverTextInfo->con.msgSeg
            );
            APP_DEBUG("data = %s\r\n",(pDeliverTextInfo->data));

            Ql_MEM_Free(pTextInfo);

            return;
        }

        bResult = ConSMSBuf_AddSeg(
                    g_asConSMSBuf,
                    CON_SMS_BUF_MAX_CNT,
                    iBufIdx,
                    &(pDeliverTextInfo->con),
                    (pDeliverTextInfo->data),
                    (pDeliverTextInfo->length)
        );
        if(FALSE == bResult)
        {
            APP_DEBUG("Enter Hdlr_RecvNewSMS,WARNING! ConSMSBuf_AddSeg FAIL! Show this CON-SMS-SEG directly!\r\n");

            APP_DEBUG(
                "status:%u,type:%u,alpha:%u,sca:%s,oa:%s,scts:%s,data length:%u,cp:1,cy:%d,cr:%d,ct:%d,cs:%d\r\n",
                (pTextInfo->status),
                (pTextInfo->type),
                (pDeliverTextInfo->alpha),
                (pTextInfo->sca),
                (pDeliverTextInfo->oa),
                (pDeliverTextInfo->scts),
                (pDeliverTextInfo->length),
                pDeliverTextInfo->con.msgType,
                pDeliverTextInfo->con.msgRef,
                pDeliverTextInfo->con.msgTot,
                pDeliverTextInfo->con.msgSeg
            );
            APP_DEBUG("data = %s\r\n",(pDeliverTextInfo->data));

            Ql_MEM_Free(pTextInfo);

            return;
        }

        bResult = ConSMSBuf_IsIntact(
                    g_asConSMSBuf,
                    CON_SMS_BUF_MAX_CNT,
                    iBufIdx,
                    &(pDeliverTextInfo->con)
        );
        if(FALSE == bResult)
        {
            APP_DEBUG(
                "Enter Hdlr_RecvNewSMS,WARNING! ConSMSBuf_IsIntact FAIL! Waiting. cp:1,cy:%d,cr:%d,ct:%d,cs:%d\r\n",
                pDeliverTextInfo->con.msgType,
                pDeliverTextInfo->con.msgRef,
                pDeliverTextInfo->con.msgTot,
                pDeliverTextInfo->con.msgSeg
            );

            Ql_MEM_Free(pTextInfo);

            return;
        }

        //Show the CON-SMS
        APP_DEBUG(
            "status:%u,type:%u,alpha:%u,sca:%s,oa:%s,scts:%s",
            (pTextInfo->status),
            (pTextInfo->type),
            (pDeliverTextInfo->alpha),
            (pTextInfo->sca),
            (pDeliverTextInfo->oa),
            (pDeliverTextInfo->scts)
        );

        uConLen = 0;
        for(uSeg = 1; uSeg <= pDeliverTextInfo->con.msgTot; uSeg++)
        {
            uConLen += g_asConSMSBuf[iBufIdx].asSeg[uSeg-1].uLen;
        }

        APP_DEBUG(",data length:%u",uConLen);
        APP_DEBUG("\r\n"); //Print CR LF

        for(uSeg = 1; uSeg <= pDeliverTextInfo->con.msgTot; uSeg++)
        {
            APP_DEBUG("data = %s ,len = %d",
                g_asConSMSBuf[iBufIdx].asSeg[uSeg-1].aData,
                g_asConSMSBuf[iBufIdx].asSeg[uSeg-1].uLen
            );
        }

        APP_DEBUG("\r\n"); //Print CR LF

        //Reset CON-SMS context
        bResult = ConSMSBuf_ResetCtx(g_asConSMSBuf,CON_SMS_BUF_MAX_CNT,iBufIdx);
        if(FALSE == bResult)
        {
            APP_DEBUG("Enter Hdlr_RecvNewSMS,WARNING! ConSMSBuf_ResetCtx FAIL! iBufIdx:%d\r\n",iBufIdx);
        }

        Ql_MEM_Free(pTextInfo);

        return;
    }

    APP_DEBUG("<-- RIL_SMS_ReadSMS_Text OK. eCharSet:LIB_SMS_CHARSET_GSM,nIndex:%u -->\r\n",nIndex);
    APP_DEBUG("status:%u,type:%u,alpha:%u,sca:%s,oa:%s,scts:%s,data length:%u\r\n",
        pTextInfo->status,
        pTextInfo->type,
        pDeliverTextInfo->alpha,
        pTextInfo->sca,
        pDeliverTextInfo->oa,
        pDeliverTextInfo->scts,
        pDeliverTextInfo->length);
    APP_DEBUG("data = %s\r\n",(pDeliverTextInfo->data));

    for(i=0;i<pDeliverTextInfo->length;i++)
       {
   // 	APP_DEBUG("data[%d]:%c.\r\n",i,(pDeliverTextInfo->data[i]));
    	char_rec_gsm[0]=pDeliverTextInfo->data[i];
    	if('#'==char_rec_gsm[0])
    	  {
//    		APP_DEBUG("char_temp_gsm:%c .i:%d\r\n",char_rec_gsm[0],i);
    		char_temp_gsm[0]=char_rec_gsm[0];
    	  }
    	if('.'==char_rec_gsm[0]&&'#'==char_temp_gsm[0])
    	   {
    		smsmode=1;
    	   }
       }

    if(smsmode==1)
      {
    	APP_DEBUG("<-- smsmode==1  -->\r\n");
        ptr1=Ql_strstr(pDeliverTextInfo->data,control);    // Checking SMS message

        if(Ql_strncmp(ptr1,control,7)==0)
          {
        	APP_DEBUG("<-- keyword 'control' is correct  -->\r\n");
        	APP_DEBUG("ptr1:%s\r\n",ptr1);
            if (ptr1[8]==pass[0] && ptr1[9]==pass[1] && ptr1[10]==pass[2] && ptr1[11]==pass[3] /*&& ptr1[12]==pass[4] && ptr1[13]==pass[5]*/)  //checking password
                {
            	  APP_DEBUG("<-- password is correct  -->\r\n");

            	  ptr2=Ql_strstr(pDeliverTextInfo->data,r1_on);                          // Checking for turn on device 1
            	  if (Ql_strncmp(ptr2,r1_on,4)==0)
            	     {
                  	   APP_DEBUG("ptr2:%s\r\n",ptr2);
            		   APP_DEBUG("<-- keyword 'on 1' is correct  -->\r\n");
            	       Ql_GPIO_SetLevel(device1, PINLEVEL_HIGH);
            	       APP_DEBUG("<-- Get the GPIO level value: %d -->\r\n", Ql_GPIO_GetLevel(device1));
            	       mode1=TRUE; flag1=TRUE;
            	       SMS_TextMode_Send(2);
            	       Erase_flags();
            	     }
            	  else
            	    {
            		  ptr2=Ql_strstr(pDeliverTextInfo->data,r1_off);                          // Checking for turn off device 1
            		  if (Ql_strncmp(ptr2,r1_off,5)==0)
            		     {
                     	   APP_DEBUG("ptr2:%s\r\n",ptr2);
               		       APP_DEBUG("<-- keyword 'off 1' is correct  -->\r\n");
               	           Ql_GPIO_SetLevel(device1, PINLEVEL_LOW);
               	           APP_DEBUG("<-- Get the GPIO level value: %d -->\r\n", Ql_GPIO_GetLevel(device1));
               	           mode1=FALSE; flag1=TRUE;
               	           SMS_TextMode_Send(2);
               	           Erase_flags();
            		     }
            	    }

            	  ptr2=Ql_strstr(pDeliverTextInfo->data,r2_on);                          // Checking for turn on device 2
            	  if (Ql_strncmp(ptr2,r2_on,4)==0)
            	     {
                  	   APP_DEBUG("ptr2:%s\r\n",ptr2);
            		   APP_DEBUG("<-- keyword 'on 2' is correct  -->\r\n");
            	       Ql_GPIO_SetLevel(device2, PINLEVEL_HIGH);
            	       APP_DEBUG("<-- Get the GPIO level value: %d -->\r\n", Ql_GPIO_GetLevel(device2));
            	       mode2=TRUE; flag2=TRUE;
            	       SMS_TextMode_Send(2);
            	       Erase_flags();
            	     }
            	  else
            	    {
            		  ptr2=Ql_strstr(pDeliverTextInfo->data,r2_off);                          // Checking for turn off device 2
            		  if (Ql_strncmp(ptr2,r2_off,5)==0)
            		     {
                     	   APP_DEBUG("ptr2:%s\r\n",ptr2);
               		       APP_DEBUG("<-- keyword 'off 2' is correct  -->\r\n");
               	           Ql_GPIO_SetLevel(device2, PINLEVEL_LOW);
               	           APP_DEBUG("<-- Get the GPIO level value: %d -->\r\n", Ql_GPIO_GetLevel(device2));
               	           mode2=FALSE; flag2=TRUE;
               	           SMS_TextMode_Send(2);
               	           Erase_flags();
            		     }
            	    }

            	  ptr2=Ql_strstr(pDeliverTextInfo->data,r3_on);                          // Checking for turn on device 3
            	  if (Ql_strncmp(ptr2,r3_on,4)==0)
            	     {
                  	   APP_DEBUG("ptr2:%s\r\n",ptr2);
            		   APP_DEBUG("<-- keyword 'on 3' is correct  -->\r\n");
            	       Ql_GPIO_SetLevel(device3, PINLEVEL_HIGH);
            	       APP_DEBUG("<-- Get the GPIO level value: %d -->\r\n", Ql_GPIO_GetLevel(device3));
            	       mode3=TRUE; flag3=TRUE;
            	       SMS_TextMode_Send(2);
            	       Erase_flags();
            	     }
            	  else
            	    {
            		  ptr2=Ql_strstr(pDeliverTextInfo->data,r3_off);                          // Checking for turn off device 3
            		  if (Ql_strncmp(ptr2,r3_off,5)==0)
            		     {
                     	   APP_DEBUG("ptr2:%s\r\n",ptr2);
               		       APP_DEBUG("<-- keyword 'off 3' is correct  -->\r\n");
               	           Ql_GPIO_SetLevel(device3, PINLEVEL_LOW);
               	           APP_DEBUG("<-- Get the GPIO level value: %d -->\r\n", Ql_GPIO_GetLevel(device3));
               	           mode3=FALSE; flag3=TRUE;
               	           SMS_TextMode_Send(2);
               	           Erase_flags();
            		     }
            	    }

            	  ptr2=Ql_strstr(pDeliverTextInfo->data,r4_on);                          // Checking for turn on device 4
            	  if (Ql_strncmp(ptr2,r4_on,4)==0)
            	     {
                  	   APP_DEBUG("ptr2:%s\r\n",ptr2);
            		   APP_DEBUG("<-- keyword 'on 4' is correct  -->\r\n");
            	       Ql_GPIO_SetLevel(device4, PINLEVEL_HIGH);
            	       APP_DEBUG("<-- Get the GPIO level value: %d -->\r\n", Ql_GPIO_GetLevel(device4));
            	       mode4=TRUE; flag4=TRUE;
            	       SMS_TextMode_Send(2);
            	       Erase_flags();
            	     }
            	  else
            	    {
            		  ptr2=Ql_strstr(pDeliverTextInfo->data,r4_off);                          // Checking for turn off device 4
            		  if (Ql_strncmp(ptr2,r4_off,5)==0)
            		     {
                     	   APP_DEBUG("ptr2:%s\r\n",ptr2);
               		       APP_DEBUG("<-- keyword 'off 4' is correct  -->\r\n");
               	           Ql_GPIO_SetLevel(device4, PINLEVEL_LOW);
               	           APP_DEBUG("<-- Get the GPIO level value: %d -->\r\n", Ql_GPIO_GetLevel(device4));
               	           mode4=FALSE; flag4=TRUE;
               	           SMS_TextMode_Send(2);
               	           Erase_flags();
            		     }
            	    }
                }
           }
      }

    Ql_strcpy(aPhNum, pDeliverTextInfo->oa);
    Ql_MEM_Free(pTextInfo);

    if (bAutoReply)
    {
        if (!Ql_strstr(aPhNum, "10086"))  // Not reply SMS from operator
        {
            APP_DEBUG("<-- Replying SMS... -->\r\n");
            iResult = RIL_SMS_SendSMS_Text(aPhNum, Ql_strlen(aPhNum),LIB_SMS_CHARSET_GSM,(u8*)aReplyCon,Ql_strlen(aReplyCon),&uMsgRef);
            if (iResult != RIL_AT_SUCCESS)
            {
                APP_DEBUG("RIL_SMS_SendSMS_Text FAIL! iResult:%u\r\n",iResult);
                return;
            }
            APP_DEBUG("<-- RIL_SMS_SendTextSMS OK. uMsgRef:%d -->\r\n", uMsgRef);
        }
    }
    return;
}

static void CallBack_UART_Hdlr(Enum_SerialPort port, Enum_UARTEventType msg, bool level, void* customizedPara)
{
}
static void InitSerialPort(void)
{
    s32 iResult = 0;

    //Register & Open UART port
    iResult = Ql_UART_Register(UART_PORT1, CallBack_UART_Hdlr, NULL);
    if (iResult != QL_RET_OK)
    {
        Ql_Debug_Trace("Fail to register UART port[%d]:%d\r\n",UART_PORT1);
    }

    iResult = Ql_UART_Open(UART_PORT1, 115200, FC_NONE);
    if (iResult != QL_RET_OK)
    {
        Ql_Debug_Trace("Fail to open UART port[%d], baud rate:115200, FC_NONE\r\n", UART_PORT1);
    }
}

// The function configure GPIOS of devices.
static void config_GPIO(void)
{
   // Initialize the GPIO pin (output, low level, pull down)
  Ql_GPIO_Init(device1, PINDIRECTION_OUT, PINLEVEL_LOW, PINPULLSEL_PULLDOWN);
  Ql_GPIO_Init(device2, PINDIRECTION_OUT, PINLEVEL_LOW, PINPULLSEL_PULLDOWN);
  Ql_GPIO_Init(device3, PINDIRECTION_OUT, PINLEVEL_LOW, PINPULLSEL_PULLDOWN);
  Ql_GPIO_Init(device4, PINDIRECTION_OUT, PINLEVEL_LOW, PINPULLSEL_PULLDOWN);

}

// The function erase all flag and mode sendSMS.
// This is an internal function.
static void Erase_flags(void)
{
 mode1=FALSE;
 mode2=FALSE;
 mode3=FALSE;
 mode4=FALSE;
 flag1=FALSE;
 flag2=FALSE;
 flag3=FALSE;
 flag4=FALSE;
 flagS=FALSE;
}

void proc_main_task(s32 iTaskID)
{
    s32 iResult = 0;
    ST_MSG taskMsg;
    // Configure output for devices
    config_GPIO();
    // Register & open UART port
    InitSerialPort();

    APP_DEBUG("Quectel M66\r\n");
    APP_DEBUG("Danila Kedrinski\r\n");
    APP_DEBUG("Coursework on the topic: ")
	APP_DEBUG("Development of a remote power management controller based on GSM communication channel.\r\n");

    // START MESSAGE LOOP OF THIS TASK
    while (TRUE)
    {
        s32 i = 0;

        Ql_memset(&taskMsg, 0x0, sizeof(ST_MSG));
        Ql_OS_GetMessage(&taskMsg);
        switch (taskMsg.message)
        {
        case MSG_ID_RIL_READY:
            {
                APP_DEBUG("<-- RIL is ready -->\r\n");
                Ql_RIL_Initialize(); // MUST call this function

                for(i = 0; i < CON_SMS_BUF_MAX_CNT; i++)
                {
                    ConSMSBuf_ResetCtx(g_asConSMSBuf,CON_SMS_BUF_MAX_CNT,i);
                }

                break;
            }
        case MSG_ID_URC_INDICATION:
            switch (taskMsg.param1)
            {
            case URC_SYS_INIT_STATE_IND:
                {
                    APP_DEBUG("<-- Sys Init Status %d -->\r\n", taskMsg.param2);
                    if (SYS_STATE_SMSOK == taskMsg.param2)
                    {
                        APP_DEBUG("\r\n<-- SMS module is ready -->\r\n");
                        APP_DEBUG("\r\n<-- Initialize SMS-related options -->\r\n");
                        iResult = SMS_Initialize();
                        if (!iResult)
                        {
                            APP_DEBUG("Fail to initialize SMS\r\n");
                        }

                        SMS_TextMode_Send(1);
                    }
                    break;
                }
            case URC_SIM_CARD_STATE_IND:
                {
                    APP_DEBUG("\r\n<-- SIM Card Status:%d -->\r\n", taskMsg.param2);
                }
                break;

            case URC_GSM_NW_STATE_IND:
                {
                    APP_DEBUG("\r\n<-- GSM Network Status:%d -->\r\n", taskMsg.param2);
                    break;
                }

            case URC_GPRS_NW_STATE_IND:
                {
                    APP_DEBUG("\r\n<-- GPRS Network Status:%d -->\r\n", taskMsg.param2);
                    break;
                }

            case URC_CFUN_STATE_IND:
                {
                    APP_DEBUG("\r\n<-- CFUN Status:%d -->\r\n", taskMsg.param2);
                    break;
                }

            case URC_COMING_CALL_IND:
                {
                    ST_ComingCall* pComingCall = (ST_ComingCall*)(taskMsg.param2);
                    APP_DEBUG("\r\n<-- Coming call, number:%s, type:%d -->\r\n", pComingCall->phoneNumber, pComingCall->type);
                    break;
               }

            case URC_NEW_SMS_IND:
                {
                    APP_DEBUG("\r\n<-- New SMS Arrives: index=%d\r\n", taskMsg.param2);
                    Hdlr_RecvNewSMS((taskMsg.param2), FALSE);
                    break;
                }

            case URC_MODULE_VOLTAGE_IND:
                {
                    APP_DEBUG("\r\n<-- VBatt Voltage Ind: type=%d\r\n", taskMsg.param2);
                    break;
                }

            default:
                break;
            }
            break;

        default:
            break;
        }
    }
}

#endif  // __OCPU_RIL_SUPPORT__ && __OCPU_RIL_SMS_SUPPORT__

