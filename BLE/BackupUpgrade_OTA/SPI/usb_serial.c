#if  0
/********************************** (C) COPYRIGHT *******************************
 * File Name          : Main.c
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2020/08/06
 * Description        : ×Ô¶¨ÒåUSBÉè±¸£¨CH372Éè±¸£©£¬Ìá¹©8¸ö·Ç0Í¨µÀ(ÉÏ´«+ÏÂ´«)£¬ÊµÏÖÊý¾ÝÏÈÏÂ´«£¬È»ºóÊý¾ÝÄÚÈÝÈ¡·´ÉÏ´«
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *******************************************************************************/

#include "CH58x_common.h"

#define DevEP0SIZE    0x40
// Éè±¸ÃèÊö·û

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */
uint8_t DevConfig, Ready;
uint8_t SetupReqCode;
UINT16 SetupReqLen;
const uint8_t *pDescr;

#define DevEP0SIZE  0x40
// Éè±¸ÃèÊö·û
const uint8_t MyDevDescr[] = { 0x12,0x01,0x10,0x01,0xFF,0x00,0x00,DevEP0SIZE,
                             0x86,0x1A,0x23,0x75,0x63,0x02,0x00,0x02,
                             0x00,0x01 };
// ÅäÖÃÃèÊö·û
const uint8_t MyCfgDescr[] = {   0x09,0x02,0x27,0x00,0x01,0x01,0x00,0x80,0xf0,              //ÅäÖÃÃèÊö·û£¬½Ó¿ÚÃèÊö·û,¶ËµãÃèÊö·û
                                 0x09,0x04,0x00,0x00,0x03,0xff,0x01,0x02,0x00,
                                 0x07,0x05,0x82,0x02,0x20,0x00,0x00,                        //ÅúÁ¿ÉÏ´«¶Ëµã
                                 0x07,0x05,0x02,0x02,0x20,0x00,0x00,                        //ÅúÁ¿ÏÂ´«¶Ëµã
                                 0x07,0x05,0x81,0x03,0x08,0x00,0x01};                       //ÖÐ¶ÏÉÏ´«¶Ëµã
// ÓïÑÔÃèÊö·û
const uint8_t MyLangDescr[] = { 0x04, 0x03, 0x09, 0x04 };
// ³§¼ÒÐÅÏ¢
const uint8_t MyManuInfo[] = { 0x0E, 0x03, 'w', 0, 'c', 0, 'h', 0, '.', 0, 'c', 0, 'n', 0 };
// ²úÆ·ÐÅÏ¢
const uint8_t MyProdInfo[] = { 0x0C, 0x03, 'C', 0, 'H', 0, '5', 0, '7', 0, 'x', 0 };
/*²úÆ·ÃèÊö·û*/
const uint8_t StrDesc[28] =
{
  0x1C,0x03,0x55,0x00,0x53,0x00,0x42,0x00,
  0x32,0x00,0x2E,0x00,0x30,0x00,0x2D,0x00,
  0x53,0x00,0x65,0x00,0x72,0x00,0x69,0x00,
  0x61,0x00,0x6C,0x00
};

const uint8_t Return1[2] = {0x31,0x00};
const uint8_t Return2[2] = {0xC3,0x00};
const uint8_t Return3[2] = {0x9F,0xEE};

/*********************************************************************
 * LOCAL VARIABLES
 */

/******** ÓÃ»§×Ô¶¨Òå·ÖÅä¶ËµãRAM ****************************************/
__attribute__((aligned(4)))  uint8_t EP0_Databuf[64 + 64 + 64];    //ep0(64)+ep4_out(64)+ep4_in(64)
__attribute__((aligned(4)))  uint8_t EP1_Databuf[64 + 64];    //ep1_out(64)+ep1_in(64)
__attribute__((aligned(4)))  uint8_t EP2_Databuf[64 + 64];    //ep2_out(64)+ep2_in(64)
__attribute__((aligned(4)))  uint8_t EP3_Databuf[64 + 64];    //ep3_out(64)+ep3_in(64)

/*********************************************************************
 * PUBLIC FUNCTIONS
 */


/*********************************************************************
 * @fn      USB_DevTransProcess
 *
 * @brief   USB ´«Êä´¦Àíº¯Êý
 *
 * @return  none
 */
void USB_DevTransProcess( void )
{
  uint8_t len, chtype;
  uint8_t intflag, errflag = 0;

// Print_I3(""); 

  intflag = R8_USB_INT_FG;
  if ( intflag & RB_UIF_TRANSFER )
  {
    if ( ( R8_USB_INT_ST & MASK_UIS_TOKEN ) != MASK_UIS_TOKEN )    // ·Ç¿ÕÏÐ
    {
      switch ( R8_USB_INT_ST & ( MASK_UIS_TOKEN | MASK_UIS_ENDP ) )
      // ·ÖÎö²Ù×÷ÁîÅÆºÍ¶ËµãºÅ
      {
        case UIS_TOKEN_IN :
        {
          switch ( SetupReqCode )
          {
            case USB_GET_DESCRIPTOR :
              len = SetupReqLen >= DevEP0SIZE ?
                  DevEP0SIZE : SetupReqLen;    // ±¾´Î´«Êä³¤¶È
              memcpy( pEP0_DataBuf, pDescr, len ); /* ¼ÓÔØÉÏ´«Êý¾Ý */
              SetupReqLen -= len;
              pDescr += len;
              R8_UEP0_T_LEN = len;
              R8_UEP0_CTRL ^= RB_UEP_T_TOG;                             // ·­×ª
              break;
            case USB_SET_ADDRESS :
              R8_USB_DEV_AD = ( R8_USB_DEV_AD & RB_UDA_GP_BIT ) | SetupReqLen;
              R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
              break;
            default :
              R8_UEP0_T_LEN = 0;                                      // ×´Ì¬½×¶ÎÍê³ÉÖÐ¶Ï»òÕßÊÇÇ¿ÖÆÉÏ´«0³¤¶ÈÊý¾Ý°ü½áÊø¿ØÖÆ´«Êä
              R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
              break;
          }
        }
          break;

        case UIS_TOKEN_OUT :
        {
          len = R8_USB_RX_LEN;
        }
          break;

        case UIS_TOKEN_OUT | 1 :
        {
          if ( R8_USB_INT_ST & RB_UIS_TOG_OK )
          {                       // ²»Í¬²½µÄÊý¾Ý°ü½«¶ªÆú
            len = R8_USB_RX_LEN;
            DevEP1_OUT_Deal( len );
          }
        }
          break;

        case UIS_TOKEN_IN | 1 :
          R8_UEP1_CTRL = ( R8_UEP1_CTRL & ~MASK_UEP_T_RES ) | UEP_T_RES_NAK;
          break;

        case UIS_TOKEN_OUT | 2 :
        {
          if ( R8_USB_INT_ST & RB_UIS_TOG_OK )
          {                       // ²»Í¬²½µÄÊý¾Ý°ü½«¶ªÆú
            len = R8_USB_RX_LEN;
            DevEP2_OUT_Deal( len );
          }
        }
          break;

        case UIS_TOKEN_IN | 2 :
          R8_UEP2_CTRL = ( R8_UEP2_CTRL & ~MASK_UEP_T_RES ) | UEP_T_RES_NAK;
          break;

        case UIS_TOKEN_OUT | 3 :
        {
          if ( R8_USB_INT_ST & RB_UIS_TOG_OK )
          {                       // ²»Í¬²½µÄÊý¾Ý°ü½«¶ªÆú
            len = R8_USB_RX_LEN;
            DevEP3_OUT_Deal( len );
          }
        }
          break;

        case UIS_TOKEN_IN | 3 :
          R8_UEP3_CTRL = ( R8_UEP3_CTRL & ~MASK_UEP_T_RES ) | UEP_T_RES_NAK;
          break;

        case UIS_TOKEN_OUT | 4 :
        {
          if ( R8_USB_INT_ST & RB_UIS_TOG_OK )
          {
            R8_UEP4_CTRL ^= RB_UEP_R_TOG;
            len = R8_USB_RX_LEN;
            DevEP4_OUT_Deal( len );
          }
        }
          break;

        case UIS_TOKEN_IN | 4 :
          R8_UEP4_CTRL ^= RB_UEP_T_TOG;
          R8_UEP4_CTRL = ( R8_UEP4_CTRL & ~MASK_UEP_T_RES ) | UEP_T_RES_NAK;
          break;

        default :
          break;
      }
      R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
    if ( R8_USB_INT_ST & RB_UIS_SETUP_ACT )                  // Setup°ü´¦Àí
    {
      R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
      SetupReqLen = pSetupReqPak->wLength;
      SetupReqCode = pSetupReqPak->bRequest;
      chtype = pSetupReqPak->bRequestType;

      len = 0;
      errflag = 0;
      if ( ( pSetupReqPak->bRequestType & USB_REQ_TYP_MASK ) != USB_REQ_TYP_STANDARD )
      {
        if( pSetupReqPak->bRequestType == 0xC0 )
        {
          if(SetupReqCode==0x5F)
          {
            pDescr = Return1;
            len = sizeof(Return1);
          }
          else if(SetupReqCode==0x95)
          {
            if((pSetupReqPak->wValue)==0x18)
            {
              pDescr = Return2;
              len = sizeof(Return2);
            }
            else if((pSetupReqPak->wValue)==0x06)
            {
              pDescr = Return3;
              len = sizeof(Return3);
            }
          }
          else
          {
            errflag = 0xFF;
          }
          memcpy(pEP0_DataBuf,pDescr,len);
        }
        else
        {
          len = 0;
        }
      }
      else /* ±ê×¼ÇëÇó */
      {
        switch ( SetupReqCode )
        {
          case USB_GET_DESCRIPTOR :
          {
            switch ( ( ( pSetupReqPak->wValue ) >> 8 ) )
            {
              case USB_DESCR_TYP_DEVICE :
              {
                pDescr = MyDevDescr;
                len = sizeof(MyDevDescr);
              }
                break;

              case USB_DESCR_TYP_CONFIG :
              {
                pDescr = MyCfgDescr;
                len = sizeof(MyCfgDescr);
              }
                break;

              case USB_DESCR_TYP_REPORT :
//              {
//                if ( ( ( pSetupReqPak->wIndex ) & 0xff ) == 0 )                             //½Ó¿Ú0±¨±íÃèÊö·û
//                {
//                  pDescr = KeyRepDesc;                                  //Êý¾Ý×¼±¸ÉÏ´«
//                  len = sizeof( KeyRepDesc );
//                }
//                else if ( ( ( pSetupReqPak->wIndex ) & 0xff ) == 1 )                        //½Ó¿Ú1±¨±íÃèÊö·û
//                {
//                  pDescr = MouseRepDesc;                                //Êý¾Ý×¼±¸ÉÏ´«
//                  len = sizeof( MouseRepDesc );
//                  Ready = 1;                                            //Èç¹ûÓÐ¸ü¶à½Ó¿Ú£¬¸Ã±ê×¼Î»Ó¦¸ÃÔÚ×îºóÒ»¸ö½Ó¿ÚÅäÖÃÍê³ÉºóÓÐÐ§
//                }
//                else
//                  len = 0xff;                                           //±¾³ÌÐòÖ»ÓÐ2¸ö½Ó¿Ú£¬Õâ¾ä»°Õý³£²»¿ÉÄÜÖ´ÐÐ
//              }
                break;

              case USB_DESCR_TYP_STRING :
              {
                switch ( ( pSetupReqPak->wValue ) & 0xff )
                {
                  case 1 :
                    pDescr = MyManuInfo;
                    len = MyManuInfo[0];
                    break;
                  case 2 :
                    pDescr = StrDesc;
                    len = StrDesc[0];
                    break;
                  case 0 :
                    pDescr = MyLangDescr;
                    len = MyLangDescr[0];
                    break;
                  default :
                    errflag = 0xFF;                               // ²»Ö§³ÖµÄ×Ö·û´®ÃèÊö·û
                    break;
                }
              }
                break;

              default :
                errflag = 0xff;
                break;
            }
            if ( SetupReqLen > len )
              SetupReqLen = len;      //Êµ¼ÊÐèÉÏ´«×Ü³¤¶È
            len = ( SetupReqLen >= DevEP0SIZE ) ?
                DevEP0SIZE : SetupReqLen;
            memcpy( pEP0_DataBuf, pDescr, len );
            pDescr += len;
          }
            break;

          case USB_SET_ADDRESS :
            SetupReqLen = ( pSetupReqPak->wValue ) & 0xff;
            break;

          case USB_GET_CONFIGURATION :
            pEP0_DataBuf[0] = DevConfig;
            if ( SetupReqLen > 1 )
              SetupReqLen = 1;
            break;

          case USB_SET_CONFIGURATION :
            DevConfig = ( pSetupReqPak->wValue ) & 0xff;
            break;

          case USB_CLEAR_FEATURE :
          {
            if ( ( pSetupReqPak->bRequestType & USB_REQ_RECIP_MASK ) == USB_REQ_RECIP_ENDP )    // ¶Ëµã
            {
              switch ( ( pSetupReqPak->wIndex ) & 0xff )
              {
                case 0x82 :
                  R8_UEP2_CTRL = ( R8_UEP2_CTRL & ~( RB_UEP_T_TOG | MASK_UEP_T_RES ) ) | UEP_T_RES_NAK;
                  break;
                case 0x02 :
                  R8_UEP2_CTRL = ( R8_UEP2_CTRL & ~( RB_UEP_R_TOG | MASK_UEP_R_RES ) ) | UEP_R_RES_ACK;
                  break;
                case 0x81 :
                  R8_UEP1_CTRL = ( R8_UEP1_CTRL & ~( RB_UEP_T_TOG | MASK_UEP_T_RES ) ) | UEP_T_RES_NAK;
                  break;
                case 0x01 :
                  R8_UEP1_CTRL = ( R8_UEP1_CTRL & ~( RB_UEP_R_TOG | MASK_UEP_R_RES ) ) | UEP_R_RES_ACK;
                  break;
                default :
                  errflag = 0xFF;                                 // ²»Ö§³ÖµÄ¶Ëµã
                  break;
              }
            }
            else
              errflag = 0xFF;
          }
            break;

          case USB_GET_INTERFACE :
            pEP0_DataBuf[0] = 0x00;
            if ( SetupReqLen > 1 )
              SetupReqLen = 1;
            break;

          case USB_GET_STATUS :
            pEP0_DataBuf[0] = 0x00;
            pEP0_DataBuf[1] = 0x00;
            if ( SetupReqLen > 2 )
              SetupReqLen = 2;
            break;

          default :
            errflag = 0xff;
            break;
        }
      }
      if ( errflag == 0xff )        // ´íÎó»ò²»Ö§³Ö
      {
//                  SetupReqCode = 0xFF;
        R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;    // STALL
      }
      else
      {
        if ( chtype & 0x80 )     // ÉÏ´«
        {
          len = ( SetupReqLen > DevEP0SIZE ) ?
              DevEP0SIZE : SetupReqLen;
          SetupReqLen -= len;
        }
        else
          len = 0;        // ÏÂ´«
        R8_UEP0_T_LEN = len;
        R8_UEP0_CTRL = RB_UEP_R_TOG | RB_UEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;    // Ä¬ÈÏÊý¾Ý°üÊÇDATA1
      }

      R8_USB_INT_FG = RB_UIF_TRANSFER;
    }
  }
  else if ( intflag & RB_UIF_BUS_RST )
  {
    R8_USB_DEV_AD = 0;
    R8_UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    R8_UEP1_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP2_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_UEP3_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK | RB_UEP_AUTO_TOG;
    R8_USB_INT_FG = RB_UIF_BUS_RST;
  }
  else if ( intflag & RB_UIF_SUSPEND )
  {
    if ( R8_USB_MIS_ST & RB_UMS_SUSPEND )
    {
      ;
    }    // ¹ÒÆð
    else
    {
      ;
    }               // »½ÐÑ
    R8_USB_INT_FG = RB_UIF_SUSPEND;
  }
  else
  {
    R8_USB_INT_FG = intflag;
  }
}

/*********************************************************************
 * @fn      DebugInit
 *
 * @brief   µ÷ÊÔ³õÊ¼»¯
 *
 * @return  none
 */
void DebugInit(void)
{
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
}

// USB Êý¾Ý·¢ËÍÔÚÕâÀï
void USBSendData( uint8_t *SendBuf, uint8_t l)
{
   // Print_I3(""); 
   memcpy(pEP2_IN_DataBuf,SendBuf,l);
   DevEP2_IN_Deal( l );
}


/*********************************************************************
 * @fn      main
 *
 * @brief   Ö÷º¯Êý
 *
 * @return  none
 */

void debug_disp(char* pdat,UINT16 a)
{
       printf("%d=as;dfjalskdffjasdfaksdff\r\n",a);
       printf("[%s:%d]\r\n",__FUNCTION__, __LINE__);
       printf("%s\r\n",pdat);

//        printf("033[0m[%s033[0;31m:%d]033[0m",__FUNCTION__, __LINE__);
        printf("033[0m[033[0;31m:]033[0m %s %s",__FUNCTION__, __LINE__);
        printf("033[0m\r\n");
}


void   test_usg(void)
{
  UINT8 i,j;
  //UINT8  usbd[]={0x1B ,0x5B ,0x30 ,0x6D ,0x5B ,0x75 ,0x73 ,0x62 ,0x5F ,0x6D ,0x61 ,0x69 ,0x6E ,0x1B ,0x5B ,0x30,0x3B ,0x33 ,0x31 ,0x6D ,0x3A ,0x34 ,0x38 ,0x31 ,0x5D ,0x20 ,0x1B ,0x5B ,0x30 ,0x6D ,0x31 ,0x32 ,0x33 ,0x34 ,0x35 ,0x36 ,0x37 ,0x38 ,0x39 ,0x0D ,0x0A ,0x1B ,0x5B ,0x30 ,0x6D ,0x0D ,0x0A,0xFF};
    UINT8  usbd[]={0x1B ,0x5B ,0x30 ,0x6D ,0x5B ,0x75 ,0x73 ,0x62 ,0x5F ,0x6D ,0x61 ,0x69 ,0x6E ,0x1B ,0x5B ,0x30,0x3B ,0x33 ,0x31 ,0x6D ,0x3A ,0x34 ,0x38 ,0x31 ,0x5D ,0x20 ,0x1B ,0x5B ,0x30 ,0x6D ,0x31 ,0x32 ,0x33 ,0x34 ,0x35 ,0x36 ,0x37 ,0x38 ,0x39 ,0x0D ,0x0A ,0x1B ,0x5B ,0x30 ,0x6D ,0x0D ,0x0A,0xFF};
//UINT8  usbd[]={0x5B ,0x30 ,0x6D ,0x5B ,0x75 ,0x73 ,0x62 ,0x5F ,0x6D ,0x61 ,0x69 ,0x6E ,0x5B ,0x30,0x3B ,0x33 ,0x31 ,0x6D ,0x3A ,0x34 ,0x38 ,0x31 ,0x5D ,0x20 ,0x5B ,0x30 ,0x6D ,0x31 ,0x32 ,0x33 ,0x34 ,0x35 ,0x36 ,0x37 ,0x38 ,0x39 ,0x0D ,0x0A ,0x5B ,0x30 ,0x6D ,0x0D ,0x0A,0xFF};

//                     1B  5B     30  6D      5B                                                               30  3B 33 31 6D 30 3B 33 1B 5B 

  // UINT8  usbd[]={'1','1','1','1','1',0xFF};

  for (i = 0; i<200; i++)
  {
    j=i;
    if(usbd[i] == 0xFF)
        break;
    else
    {
      Tx_RS232(usbd[i]);
      USBSendData(&usbd[i],1);
    }  
  }
  j--;

if(j>=64)
{
   printf("Err\r\n");
   j=63;
}
//USBSendData((uint8_t *)usbd,(uint8_t)j);

//USBSendData((uint8_t *)usbd,(uint8_t)5);
//USBSendData((uint8_t *)usbd+5,(uint8_t)5);
//USBSendData((uint8_t *)usbd+5+5,(uint8_t)5);
//USBSendData((uint8_t *)usbd+5+5+5,(uint8_t)5);
}

//[usb_main:481] 123456789

//;31main[0;31main[0;31main[0;31musb_m0;31musb_m0;31m0;31m0;31mu[0m[0;31m0;31m0;31m0;3[0;31m[0m[0;31m0;31m0;31m0;31[


int usb_main(void){


    UINT16  a;
//    SetSysClock(CLK_SOURCE_PLL_60MHz);
//    DebugInit();
//    DBPRINT("start\n");
    // Print_I3(" ");

    pEP0_RAM_Addr = EP0_Databuf;
    pEP1_RAM_Addr = EP1_Databuf;
    pEP2_RAM_Addr = EP2_Databuf;
    pEP3_RAM_Addr = EP3_Databuf;
    USB_DeviceInit();
    PFIC_EnableIRQ(USB_IRQn);

    a=0;
    while(1)
    {
        a++;

        //debug_disp("as;dfjalskdffjasdfaksdff\r\n");
        Print_I3("123456789\r\n");    

        //break;

         // debug_disp("345634563456345",a);
        
//         printf("%d=as;dfjalskdffjasdfaksdff\r\n",a);
//         printf("[%s:%d]\r\n",__FUNCTION__, __LINE__);
//         printf("\r\n");

       // test_usg();
        mDelaymS(1000);

    }

}


//5B 75 73 62 5F 6D 61 69 6E 3A 34 38 31 5D 31 32 33 34 35 36 37 38 39 0D 0A 2E 2E 0D 0A 
//5B 75 73 62 5F 6D 61 69 6E 3A 34 38 31 5D 31 32 33 34 35 36 37 38 39 0D 0A 2E 2E 0D 0A 

//1B 5B 30 6D 5B 75 73 62 5F 6D 61 69 6E 1B 5B 30 3B 33 31 6D 3A 34 38 31 5D 20 1B 5B 30 6D 31 32 33 34 35 36 37 38 39 0D 0A 1B 5B 30 6D 0D 0A 



/*********************************************************************
 * @fn      DevEP1_OUT_Deal
 *
 * @brief   ¶Ëµã1Êý¾Ý´¦Àí
 *
 * @return  none
 */
void DevEP1_OUT_Deal(uint8_t l)
{ /* ÓÃ»§¿É×Ô¶¨Òå */
    uint8_t i;

    // Print_I3("");
    for(i = 0; i < l; i++)
    {
       // pEP1_IN_DataBuf[i] = ~pEP1_OUT_DataBuf[i];
        pEP1_IN_DataBuf[i] = pEP1_OUT_DataBuf[i];
    }
    DevEP1_IN_Deal(l);
}

/*********************************************************************
 * @fn      DevEP2_OUT_Deal
 *
 * @brief   ¶Ëµã2Êý¾Ý´¦Àí
 *
 * @return  none
 */
 //  USB µÄÊý¾Ý½ÓÊÕÔÚÕâÀï
void DevEP2_OUT_Deal(uint8_t l)
{ /* ÓÃ»§¿É×Ô¶¨Òå */
    uint8_t i;
    UINT8 SendBuf[100];

    if(l<100)
    {
        memcpy(SendBuf,pEP2_OUT_DataBuf,l);
    }
    else
    {
        memcpy(SendBuf,pEP2_OUT_DataBuf,100);
    }


    
//    Print_I3("");

    for(i = 0; i < l; i++)
    {
     //   pEP2_IN_DataBuf[i] = ~pEP2_OUT_DataBuf[i];
        pEP2_IN_DataBuf[i] = pEP2_OUT_DataBuf[i];
    }
    DevEP2_IN_Deal(l);
}

/*********************************************************************
 * @fn      DevEP3_OUT_Deal
 *
 * @brief   ¶Ëµã3Êý¾Ý´¦Àí
 *
 * @return  none
 */
void DevEP3_OUT_Deal(uint8_t l)
{ /* ÓÃ»§¿É×Ô¶¨Òå */
    uint8_t i;
    // Print_I3("");
    for(i = 0; i < l; i++)
    {
       // pEP3_IN_DataBuf[i] = ~pEP3_OUT_DataBuf[i];
        pEP3_IN_DataBuf[i] = pEP3_OUT_DataBuf[i];
    }
    DevEP3_IN_Deal(l);
}

/*********************************************************************
 * @fn      DevEP4_OUT_Deal
 *
 * @brief   ¶Ëµã4Êý¾Ý´¦Àí
 *
 * @return  none
 */
void DevEP4_OUT_Deal(uint8_t l)
{ /* ÓÃ»§¿É×Ô¶¨Òå */
    uint8_t i;
    // Print_I3("");
    for(i = 0; i < l; i++)
    {
       // pEP4_IN_DataBuf[i] = ~pEP4_OUT_DataBuf[i];
        pEP4_IN_DataBuf[i] = pEP4_OUT_DataBuf[i];
    }
    DevEP4_IN_Deal(l);
}

/*********************************************************************
 * @fn      USB_IRQHandler
 *
 * @brief   USBÖÐ¶Ïº¯Êý
 *
 * @return  none
 */
__INTERRUPT
__HIGH_CODE
void USB_IRQHandler(void) /* USBÖÐ¶Ï·þÎñ³ÌÐò,Ê¹ÓÃ¼Ä´æÆ÷×é1 */
{
    USB_DevTransProcess();
}
#endif


