

#include "CONFIG.h"
#include "peripheral.h"
#include "app_uart.h"
#include "app_cfg.h"
#include "xtinfoservice.h"
#include "commoninfo.h"

#ifdef ENABLE_SOFTWARE_TO_XT

extern UINT8 Flash_Buffer[];


XtNotifyDataCallback_t mNotifyCb = NULL;
uint16_t mConnHandle = 0;


#define   UC7279_Num_a   					1
#define   UC7279_Num_b   					2
#define   UC7279_Num_x   					3

unsigned char  UC7279_Chip_Num=UC7279_Num_x;

void XT_3Color_800_480_init(void)
{
  int     length;

	Print_I3("---XT_3Color_800_480_init 11111111111111111111111");
    global_BOE_DEVICE_STATUS.fInitDriver = Is_Yes;
	global_BOE_DEVICE_STATUS.fScreenType = SCREEN_TYPE_IMG_AB;
	global_BOE_DEVICE_STATUS.fIsNeedStandby = 1;
    Display_Picture_To_3Color_x(0,1);
    
    length = SCREEN_DATA_START;
    Display_Picture_To_3Color_x(0,length);
}

void XT_3Color_800_480_RED(void)
{    
      int     length;
    Print_I3("XT_3Color_800_480_RED "); 
    length = SCREEN_BLACK_WHITE_COLOR_3_MAX;
    Display_Picture_To_3Color_x(0,length);
}

void XT_3Color_800_480_end(void)
{
      int     length;
   Print_I3("XT_3Color_800_480_end "); 
   length = SCREEN_RED_COLOR_3_MAX - 1;
   Display_Picture_To_3Color_x(0,length);
   
   tmos_start_task(main_task_ID,EVENT_Test_Msg ,2*1600);

}

//=================================================================================
//=================================================================================
//=================================================================================



/*******************************************************************************
* @brief 包处理回调函数
*        对每包数据头部判断，校验以及数据解析
* @param 无返回值
*                   
* @return 无返回值

*******************************************************************************/
<<<<<<< HEAD
void gen_cmdPack(UINT8 *buff,UINT8 type,UINT8 cmd,UINT8 *param,UINT8 plen)
{
	UINT32 calcsum = 0;
    CMD_PACK_S *cmdPack = (CMD_PACK_S *)buff;
    memcpy(cmdPack->head,PACKHEAD,3);
    cmdPack->type = type;
	cmdPack->cmd  = cmd ;
	if(param != NULL)
	{
	    memcpy(cmdPack->param,param,plen);
	}
	cmdPack->cmdLen = plen + CPHLEN  ;
	calcsum = dataCheckSum(cmdPack->param,plen);
	calcsum += cmd;
	cmdPack->checkSum = (UINT8) calcsum; 
}

void setXtNofityCallBack(uint16_t connHandle, XtNotifyDataCallback_t rCb) {	
	Print_I3("setXtNofityCallBack 0000000000000000 \r\n");
	mNotifyCb = rCb;
	mConnHandle = connHandle;
}

/*******************************************************************************
* @brief ble回复函数
*        实现数据ble回复
*
* @param [in] data 需要上行的数据
*        [in] dlen 上行的数据长度
* @return 无返回值
*******************************************************************************/
void BlePackSend(UINT8 cmd,UINT8 *data,UINT8 dlen)
{

    UINT8 i;
    UINT8 retbuff[240];// 240
    Print_I3("BlePackSend \r\n");    
	CMD_PACK_S *cmdpack = (CMD_PACK_S *)retbuff;
	gen_cmdPack(retbuff, TYPE_HOSTPACK,cmd,data,dlen);
	if(mNotifyCb != NULL){
		Print_I3("BlePackSend cmdLen:%d\r\n",cmdpack->cmdLen);    
		mNotifyCb(mConnHandle, retbuff, cmdpack->cmdLen);
	}
}

/*******************************************************************************
* @brief 申请flash空间
*        当数据量大于FSAVERSIZE时擦除flash空间。
*        当数据量小于FSAVERSIZE忽略直接返回      
*
* @param [in] cmd  指针指向下行的指令 
*    
* @return 无返回值
*******************************************************************************/
static void ApplicationFlash(UINT8 *cmd)
{
	/*UINT8 retval[2];
    gFileSize=cmd[0]<<24|cmd[1]<<16|cmd[2]<<8|cmd[3];


    retval[1]=0;

    Print_I3("gFileSize=%d",gFileSize);    

    //printf("size %02X  %02X %02X\r\n",cmd[0],cmd[1],cmd[2],cmd[3]);    

    
	if (gFileSize > MAXSAVEFLASH)
	{
		retval[0] = RECMDFAILED;
       Print_I3("Er gFileSize=%d",gFileSize);            
	}	
    else
    {		
		if(gFileSize > FSAVERSIZE)  // 20480/1024=20K
		{
			X_PRINT("Erase flash size:%d..........\r\n",gFileSize);            
		}
		retval[0] = RECMDSUCCESS;
    }
	memset(bbitmap,0x00,sizeof(bbitmap)); */
	UINT8 retval[2];
	retval[0] = RECMDSUCCESS;
	retval[1]=0;
    BlePackSend(CMD_EREFLASH,retval,2);
}

/*******************************************************************************
* @brief 指令处理函数
*        指令处理的入口      
*
* @param [in] cmd  指针指向下行的指令 
*        [in] clen 指令数据的长度
* @return 无返回值
*******************************************************************************/
static void CMDProcess(CMD_PACK_S *cmdpack)
{
    UINT16 i;
    Print_I3("CMD%02X\r\n",cmdpack->cmd);
    
    switch(cmdpack->cmd)
	{
		case CMD_EREFLASH:     
            ApplicationFlash(cmdpack->param);      
            Print_I3("CMD ERE FLASH..applly flash \r\n");
            break; /**<申请flash  */
        
		case CMD_GETBMAP :
            //UpperBitmap(cmdpack->param);      
            Print_I3("CMDGETBMAP..获取bitmap \r\n");
            break; /**<获取bitmap */
        
		case CMD_PICDIS:

            //UINT8 head[3] ; /**<包头*/
            //UINT8 type    ; /**<数据的类型*/
            //UINT8 cmdLen  ; /**<指令长度  */
            //UINT8 checkSum; /**<校验和*/
            //UINT8 cmd     ; /**<指令码*/
            //UINT8 param[CPARAMLEN]; /**<指令包参数长度*/

            Print_I3("type=%02x\r\n",cmdpack->type);
            Print_I3("cmdLen=%02x\r\n",cmdpack->cmdLen);
            Print_I3("checkSum=%02x\r\n",cmdpack->checkSum);
            Print_I3("cmd=%02x\r\n",cmdpack->cmd);

            //03 01 AE 01 D8 39 00 20 C8   A
            //03 02 EA 00 D8 39 00 20 C8   B
            //03 03 2C 01 D8 39 00 20 C8   AB
            //printf("disp=%02x %02x %02x %02x\r\n",cmdpack->param[0],cmdpack->param[1],cmdpack->param[2],cmdpack->param[3]);
            for(i=0;i<cmdpack->cmdLen;i++)
            {
             	Print_I3("%02X ",cmdpack->param[i]);
            }
			Print_I3("\r\n");

            //单面推图：0:无参数,1:带参数
            //消息：2:无参数
            //双面推图：3:带参数
            //prism推图: 4:带参数

            //1）单面推图：图片序号（1B）+刷新标志（1B）+ 参数
            //   图片序号：1 ~ 6， 0 时图片全删除
            //   刷新标志：0: 保存并立即刷新，1: 保存但不刷新
            //             2： 图片轮播       3：定时任务
            //   参    数：图片轮播（1B）： 1-255分钟
            //             定时任务（2B）： 时分
            //2）双面推图：1:A面，2：B面，3 A+B面
            //prism推图：动画类型（1B）: 0:无动画；1-255：不同动画类型


            // Last 146 75 19346   19456=110
            /*Total_data+=Bluetooth_Dat_counter-17-21;
            Print_I3("Last %d %d %d",Bluetooth_Dat_counter,Flash_256byte_counter,Total_data);                        
            TDX_SPI_FLASH_W_256Bytes(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size);                
            MassCMDProcess(cmdpack->param,CPARALENS(cmdpack->cmdLen));

            //03 01 AE 01 D8 39 00 20 C8   A
            //03 02 EA 00 D8 39 00 20 C8   B
            //03 03 2C 01 D8 39 00 20 C8   AB
            if(cmdpack->param[1]==1) // A
            {
                Print_I3("CMD PICDIS..A Screen");
                File_Qutity=1;
            }
            else if(cmdpack->param[1]==2) // B
            {
                Print_I3("CMD PICDIS..B Screen");
                File_Qutity=2;
            }
            else //if(cmdpack->param[1]==3) // AB
            {
                Print_I3("CMD PICDIS..AB Screen");
                File_Qutity=3;
            }
            tmos_start_task(main_task_ID,EVENT_Test_only ,3200);*/
         
			break; /**<指令集合   */
        
		case CMD_OTA     : 
            //OTAProcess(cmdpack->param);      
            Print_I3("CMDOTA..OTA\r\n");
            break; /**<OTA */
        
		case CMD_ACESS   : 
            //AcessPorcess(cmdpack->param);
            Print_I3("CMDACESS..r\n");
            break; /**<入网设置*/
        
 		case CMD_SETMODE :
            //SetBleWorkMode(cmdpack->param);
            Print_I3("CMDSETMODE.设置工作模式.\r\n");
            break; /**<设置工作模式*/
        
		case CMD_LEDCONTROL:
            //CMDLedContral((cmdpack->param),CPARALENS(cmdpack->cmdLen)); 
            Print_I3("CMDLEDCONTROL..r\n");
		    break;
        
	    default:
            Print_I3("default..r\n");
            break;
	} 		
}

// 处理分片数据
void process_chunk(unsigned char *data, int len) {
    static char leftover_char = '\0'; // 静态变量存储上一个奇数分片的最后一个字符
    static int has_leftover = 0;     // 标记是否有上一个分片遗留的字符
    unsigned char *merged_data = NULL;

    if (has_leftover) {
        // 如果有上一个分片遗留的字符，先将其与当前分片数据合并
        merged_data = (char *)malloc(len + 2);
        merged_data[0] = leftover_char;
        memcpy(merged_data + 1, data, len);
        merged_data[len + 1] = '\0';
        data = merged_data;
        len++;
        has_leftover = 0;
    }

    while (len > 0) {
        if (len % 2 == 0) {
            // 长度为偶数，直接解析
            //parse_even_data(data, len);
            Print_I3("process_chunk 000000000000000000000 len=%d\n",len);
            hex_dump(data,len);
            len = 0;
        } else {
            // 长度为奇数，记录最后一个字符，处理前面的偶数长度部分
            leftover_char = data[len - 1];
            has_leftover = 1;
			Print_I3("process_chunk 111111111111111111111 len=%d\n",len-1);
            //parse_even_data(data, len - 1);
            hex_dump(data,len - 1);
            len = 0;
        }
    }

    if (merged_data != NULL) {
        free(merged_data);
    }
}

void PicDataSolution(uint8_t *RWBuff, uint32_t bLen)
{
	Print_I3("PicDataSolution 000000000000000000000\n");
	//hex_dump(RWBuff,bLen);
	//process_chunk(RWBuff,bLen);
}

static int WritePicToEink(uint32_t Addr,uint8_t *RWBuff,uint32_t bLen,UINT8 for_flash)
{
	UINT16 i,j,Dat,L;
	stCPHead dCPHead;
	uint32_t PicSize=0;

	//image_size_bw=48000
	//0x00,0x00,0x00,0x00,
	//0x00,0x00,0x00,0x00,
	//0x00,0x00,0x03,0x20,
	//0x00,0x00,0x01,0xE0,
	//0x01,
	//0x00,0x00,0x4A,0xDC,
	//0x26,

	Print_I3("L=%d",bLen);
	dCPHead.X_pos =ByteTOInt(RWBuff+0 );
	dCPHead.Y_pos =ByteTOInt(RWBuff+4 );
	dCPHead.Width =ByteTOInt(RWBuff+8 );
	dCPHead.Heigth=ByteTOInt(RWBuff+12);
	dCPHead.CompType=RWBuff[16];
	dCPHead.DataSize=ByteTOInt(RWBuff+17);
	PicSize=dCPHead.Width*dCPHead.Heigth/8;

	image_size_bw = PicSize;
	image_size_bw_add=0;    
	Print_I3("X=%d Y=%d W=%d H=%d Type=%d D-Size=%d\r\n",dCPHead.X_pos,dCPHead.Y_pos,dCPHead.Width,dCPHead.Heigth,dCPHead.CompType,dCPHead.DataSize);
	Print_I3("image_size_bw=%d %d\r\n",image_size_bw,image_size_bw_add);

	//p_dat_1 = RWBuff+21;
	L =  bLen-21;

	PicDataSolution(RWBuff+21,L);
	/*for(i = 0; i <20; i++)
	{              
		Print_I3("%02X ",RWBuff+21+i); 
	}
	Print_I3("==\r\n");*/

	/*switch(dCPHead.CompType)
	{
		case 0x01:
			test_flag_1 =1;

			remainder_no_Zero=0;
			if(L%2 != 0)
			{
				remainder_no_Zero=p_dat_1[L-1];
				Print_I3("RLE L=%d re=%x\r\n",L,remainder_no_Zero);    
			}
			else
			{
				Print_I3("RLE L=%d no re\r\n",L);  
			}
			total_data=L;
			image_size_bw_add=0;
			for(i=0;i<L;i+=2)
			{            
				if(p_dat_1[i]==0){
					Print_I3("er\r\n");
					X_PRINT("(%02X-%02X)",
					p_dat_1[i],p_dat_1[i+1]);
				}

				Dat=(~(p_dat_1[i+1]));
				//Dat=p_dat_1[i+1];
				//for(j=0;j<p_dat_1[i];j++)
				//{
				//  SPI0_MasterSendByte(Dat);
				//}
				image_size_bw_add +=p_dat_1[i];
				SPI0_MasterTrans_0x(Dat,p_dat_1[i]);

				WWDG_SetCounter(0);//喂狗                              
			}            
			printf("S=%d %d\r\n\r\n",image_size_bw_add,total_data);                            
			//return UnCompressWriteEINK(Addr+21,PicSize,packbuff,bLen); //RLE重复压缩算法
		break;
		default  :
			printf("不压缩\r\n");  
			//return WriteEINK(Addr+21,PicSize,packbuff,bLen);           //不压缩
		break;
	}*/
}

uint32_t ParsePicInfo(uint32_t Addr,uint8_t *RWBuff,uint32_t bLen,UINT8 for_flash)
{
	UINT16 i=0;

	UINT16 L;
	stCFHead dCFHead;
	//Qflash_Read(Addr,RWBuff,1100);

	// 0x58,0x54,0x45,0x4B,
	// 0x00,0x1E,0xE0,0x64,
	// 0x00,0x00,0x95,0xF7,
	// 0x02,
	// 0x00,0x00,0x00,0x15,
	// 0x00,

	Print_I3("ParsePicInfo L=%d",bLen);

	memcpy(dCFHead.FHead,RWBuff,4);   //获得文件头部
	dCFHead.CheckSum=RWBuff[7];       //获得checksum
	dCFHead.FileSize=ByteTOInt(RWBuff+8);  //获得数据大小
	dCFHead.FileQutity=RWBuff[12];      //图片数量
	//dCFHead.FileOffset=ByteTOInt(RWBuff+13);  //获得数据大小

	Print_I3("EinkDisPlay HEAD:%c%c%c%c\r\n",dCFHead.FHead[0],dCFHead.FHead[1],dCFHead.FHead[2],dCFHead.FHead[3]);
	Print_I3("EinkDisPlay Picture Qutity:%x,File Size:%x,CheckSum(L):%x\r\n",dCFHead.FileQutity,dCFHead.FileSize,dCFHead.CheckSum) ;
	for(int i=0; i<dCFHead.FileQutity; i++){
		Print_I3("Picture offset:%x \r\n",ByteTOInt(RWBuff+13+i*4));
	}

	if(memcmp(dCFHead.FHead,FILEHEAD,4)!=0)
	{
		PRINT("File Format erro\r\n");
		return 0;
	}
	//stCPHead *pictHeads = (stCPHead *)malloc(dCFHead.FileQutity * sizeof(stCPHead));

	//WritePicToEink(0, RWBuff, bLen-17, for_flash);
	/*if(dCFHead.FileQutity ==1)
	{
		File_Qutity=1;
		WritePicToEink(0, RWBuff+17, bLen-17, for_flash);
	}
	else // if(dCFHead.FileQutity ==2)
	{
		File_Qutity=2;
		File_2_addr = (RWBuff[17]<<24)|(RWBuff[18]<<16)|(RWBuff[19]<<8)|(RWBuff[20]);
		Print_I3("File-2-Addr=%x",File_2_addr);
		WritePicToEink(0, RWBuff+17+4, bLen-17-4, for_flash);
	}*/

	return 0;
}

void PackProcessCallBack_Save_To_Flash(uint8_t *p_data, DATA_PACK_S *dataPack, unsigned char w_len)
{     
	UINT16 i,j,Dat,L;
	UINT16 dataLen = 0;

	PRINT("L=%d\r\n",w_len);
	if (memcmp(dataPack->head,PACKHEAD,3) == 0)
	{            
		if (dataPack->type == TYPE_DATAPACK)
		{
			PRINT("TYPEDATAPACK 0000000000000000000000\r\n");  	
            PRINT("head-3B-XTE %02X %02X %02X\r\n",dataPack->head[0],dataPack->head[1],dataPack->head[2]);    
            PRINT("type 1B data=0x02 %02X \r\n",dataPack->type);    
            PRINT("dataLen 2B total-from start to end-%02X %02X\r\n",dataPack->dataLen[0],dataPack->dataLen[1]);        
            PRINT("checkSum-1B-from start to end-%02X %02X-get low %02X \r\n",dataPack->checkSum);    
            PRINT("totalPack-1B-totalPack %02X \r\n",dataPack->totalPack);    
            PRINT("packNumber-1B-packNumber %02X \r\n",dataPack->packNumber);        
            PRINT("data %02X %02X %02X %02X %02X\r\n",dataPack->data[0],dataPack->data[1],dataPack->data[2],dataPack->data[3]);

            PRINT("package %02x-%02x----%x%x %d\r\n",dataPack->totalPack,dataPack->packNumber,dataPack->dataLen[1],dataPack->dataLen[0],j);

			if(memcmp(p_data+DPHLEN,FILEHEAD,FILEHEADLEN)==0)
            {          
               	ParsePicInfo(p_data+DPHLEN, w_len-DPHLEN);
            }
			else
			{
				//ParsePicInfo(p_data+DPHLEN+, w_len-DPHLEN);
				PicDataSolution(p_data+DPHLEN,w_len-DPHLEN);
            }
		}
		else if (dataPack->type == TYPE_SLAVEPACK)
		{           
			PRINT("TYPESLAVEPACK 0000000000000000000000\r\n");    
			//File_head =0;

			if (cmdPackCheck(TRUE,(CMD_PACK_S *)dataPack) <= 0)
			{
				PRINT("cmd pack check erro...\r\n");
				return;
				//continue;
			}	
			PRINT("cmd pack check success...\r\n");
			CMDProcess((CMD_PACK_S *)dataPack);		
		}
	} 
	else
	{
		PRINT("other cmd 0000000000000000000000\r\n");  	
		PicDataSolution(p_data,w_len);
	}
}

/*********************************************************************
 * @fn      on_bleuartServiceEvt
 *
 * @brief   ble uart service callback handler
 *
 * @return  NULL
 */
void on_bleuartServiceEvt(UINT16 connection_handle, ble_uart_evt_t *p_evt)
{
    UINT16 i;
    
    switch(p_evt->type)
    {
        case BLE_UART_EVT_TX_NOTI_DISABLED:
            PRINT("%02x:bleuart_EVT_TX_NOTI_DISABLED\r\n", connection_handle);
            break;
        case BLE_UART_EVT_TX_NOTI_ENABLED:
            PRINT("%02x:bleuart_EVT_TX_NOTI_ENABLED\r\n", connection_handle);
            break;
        case BLE_UART_EVT_BLE_DATA_RECIEVED:      
            PackProcessCallBack_Save_To_Flash(p_evt->data.p_data, (DATA_PACK_S *)p_evt->data.p_data,p_evt->data.length);
            break;
        default:
            break;
    }
}

#endif
#if 0
/********************************************************************* * INCLUDES */
#include "CONFIG.h"
#include "devinfoservice.h"
#include "peripheral.h"
#include "app_uart.h"
#include "app_cfg.h"
#include "xtinfoservice.h"
#include "commoninfo.h"

#define  Save_to_Flash_xt 0

/********************************************************************* * CONSTANTS */
/********************************************************************* * TYPEDEFS */
/********************************************************************* * GLOBAL VARIABLES */
UINT8 to_test_buffer[BLE_BUFF_MAX_LEN - 4 - 3];

/********************************************************************* * EXTERNAL VARIABLES */
/********************************************************************* * EXTERNAL FUNCTIONS */
/********************************************************************* * LOCAL VARIABLES */
=======
>>>>>>> 3b39af9a7212b117e89bd93e5b39565b889dd7fa
#define Bww_Data  1
#define RED_Data  2
#define Ove_Data  3
#define Err_Data  4


UINT16 total_data;
UINT8   test_flag_1;



UINT8   File_head;
UINT8   remainder_no_Zero;
UINT8   BW_BR;
UINT32  image_size_bw;
UINT32  image_size_bw_add;


UINT32  Total_data;


UINT16 Bluetooth_Dat_counter;
UINT16 Flash_256byte_counter;
UINT16 Flash_image_counter;
#define  Max_image_Size  64   // 64K




/*=======================宏定义/重定义======================================*/  
#define PACKHEAD  "XTE"        /**<数据\指令包头部*/ 
#define CPARAMLEN 80          /**<指令包参数长度*/
#define PACKDATALEN  12 // 1211         /**<数据包数据长度*/
#define CPHLEN 7               /**<指令头部长度*/
#define DPHLEN 9               /**<数据头部长度*/
#define CPARALENS(x) x-CPHLEN  /**<指令参数有效长度*/
#define DDATALENS(x) x-DPHLEN  /**<实际有效数据的长度*/
#define TODATAPACK PACKDATALEN + DPHLEN /**<数据包总长度*/
#define TOCMDPACK  CPARAMLEN + CPHLEN /**<指令包总长度*/
//#define Print_I3   NS_LOG_INFO   



#define CMDEREFLASH  0x01  /**<申请flash  */
#define CMDGETBMAP   0x02  /**<获得bitmap */
#define CMDDEVINFO   0x03  /**< 获取设备信息*/
#define CMDPICDIS    0x04  /**<显示图片*/
#define CMDOTA       0x05  /**< OTA */
#define CMDACESS     0x06  /**<入网**/
#define CMDSYNCTM    0x07  /**<时间同步*/
#define CMDDISCOVDEV 0x08  /**<开始探索设备*/
#define CMDGETBUFF   0x09  /**<获取数据buff大小*/
#define CMDSCANDEV   0x0A  /**<标签上报*/
//#define CMDDISCON    0x0B  /**<断开连接*/
#define CMDLEDCONTROL	0x0B  /**<LED控制*/
#define CMDSETMODE   0x0D  /**<设置广播模式*/

#define TYPESLAVEPACK    0x01  /**<从机指令包*/
#define TYPEDATAPACK     0x02  /**<数据包*/
#define TYPEMASTEPACK    0x03  /**<主机指令包*/
#define TYPEHOSTPACK     0x04  /**<串口主返回指令包*/

#define RECMDSUCCESS  255  /**<指令执行成功*/
#define RECMDFAILED   104  /**<指令执行失败*/
#define RECMDADDSUC   105  /**<指令添加成功*/



#define   PACKBCOU     4     /**<缓冲区数量*/
#define   PACKBUFF     4880  /**<接收包缓冲区*/
#define   FSAVERSIZE   20480 /**<存放RAM的数据长度*/
#define   MAXSAVEFLASH 307200 /**<内部flash最大存储*/

#define FLSHDATASTART 0x1040000        /**<文件数据存放起始地址*/


#define  false     0
#define  true     1

UINT8   g_taskSuccess   = false;  /**< true 任务发送成功**/
UINT8   referPicture    = false;

UINT8 	referMessage 	= false;
UINT8   resetAfterdiscon= false;
UINT8   g_packdown      = false;
UINT8   loopflag  =false;   /**<接收包处理标志*/



#define  X_PRINT printf

typedef struct _cmdPack{
    UINT8 head[3] ; /**<包头*/
	UINT8 type    ; /**<数据的类型*/
	UINT8 cmdLen  ; /**<指令长度  */
	UINT8 checkSum; /**<校验和*/
	UINT8 cmd     ; /**<指令码*/
	UINT8 param[CPARAMLEN]; /**<指令包参数长度*/
}CMD_PACK_S;


typedef struct _dataPack{
    UINT8  head[3]   ; /**<包头*/
	UINT8  type      ; /**<数据的类型*/
	UINT8  dataLen[2] ; /**<数据长度  */
	UINT8  checkSum  ; /**<校验和*/
	UINT8  totalPack ; /**<总包数*/
	UINT8  packNumber; /**<包编号*/
	UINT8  data[PACKDATALEN]; /**<指令包参数长度*/
	//UINT8*  pdata;
}DATA_PACK_S;


typedef struct CompPictHead { /**<压缩图片头部*/
	uint32_t X_pos;
	uint32_t Y_pos;
	uint32_t Width;
	uint32_t Heigth;
	uint32_t DataSize;
	uint8_t  CompType;
	uint8_t  recv[3]; 
} stCPHead;

typedef struct CompFileHead { /**<压缩文件头部结构体  */  
    uint8_t  FHead[4];        /**<文件头部            */
    uint8_t  CheckSum;        /**<文件累加和，取最低位*/
    uint8_t  FileQutity;      /**<文件数量            */
    uint8_t  recv;            /**<保留                */
    uint32_t FileSize;        /**<文件大小            */
}stCFHead;


UINT16  gPackLen=0;  /**<包总长度*/
UINT16  gPackRev=0;  /**<已经接收的包的长度*/
UINT32  gFileSize=0; /**<文件的大小*/
UINT8   g_totalPack = 0;

UINT8   bbitmap[15];        /**<接收数据包的bitmp*/


UINT8   *p_dat_1;
UINT8    File_Qutity;
UINT32   File_2_addr;



void setXtNofityCallBack(uint16_t connHandle, XtNotifyDataCallback_t rCb) {	
	Print_I3("setXtNofityCallBack 0000000000000000 \r\n");
	mNotifyCb = rCb;
	mConnHandle = connHandle;
}

/*******************************************************************************
* @brief ble回复函数
*        实现数据ble回复
*
* @param [in] data 需要上行的数据
*        [in] dlen 上行的数据长度
* @return 无返回值
*******************************************************************************/
void BlePackSend(UINT8 cmd,UINT8 *data,UINT8 dlen)
{

    UINT8 i;
    UINT8 retbuff[240];// 240
    Print_I3("BlePackSend \r\n");    
	CMD_PACK_S *cmdpack = (CMD_PACK_S *)retbuff;
	//gen_cmdPack(retbuff, TYPE_HOSTPACK,cmd,data,dlen);
	gen_cmdPack(retbuff, TYPEHOSTPACK,cmd,data,dlen);    
	if(mNotifyCb != NULL){
		Print_I3("BlePackSend cmdLen:%d\r\n",cmdpack->cmdLen);    
		mNotifyCb(mConnHandle, retbuff, cmdpack->cmdLen);
	}
}



uint32_t ByteTOInt(uint8_t *buff)
{
	return (buff[0]<<24|buff[1]<<16|buff[2]<<8|buff[3]);
}

static int WritePicToEink(uint32_t Addr,uint8_t *RWBuff,uint32_t bLen,UINT8 for_flash)
{
    UINT16 i,j,Dat,L;
	stCPHead dCPHead;
	uint32_t PicSize=0;
    
	//Qflash_Read(Addr,RWBuff,sizeof(stCPHead));

//image_size_bw=48000
//0x00,0x00,0x00,0x00,
//0x00,0x00,0x00,0x00,
//0x00,0x00,0x03,0x20,
//0x00,0x00,0x01,0xE0,
//0x01,
//0x00,0x00,0x4A,0xDC,
//0x26,

    Print_I3("L=%d",bLen);
//    for(i = 0; i <21; i++)
//    {              
//      PRINT("%02X ",RWBuff[i]); 
//    }
//    X_PRINT("QQ\r\n");
//    for(i = 21; i <(21+10); i++)
//    {              
//      PRINT("%02X ",RWBuff[i]); 
//    }
//    X_PRINT("RR\r\n");
    

    
	dCPHead.X_pos =ByteTOInt(RWBuff+0 );
	dCPHead.Y_pos =ByteTOInt(RWBuff+4 );
	dCPHead.Width =ByteTOInt(RWBuff+8 );
	dCPHead.Heigth=ByteTOInt(RWBuff+12);
    dCPHead.CompType=RWBuff[16];
    dCPHead.DataSize=ByteTOInt(RWBuff+17);
    PicSize=dCPHead.Width*dCPHead.Heigth/8;

    image_size_bw = PicSize;
    image_size_bw_add=0;    
    X_PRINT("X=%d Y=%d W=%d H=%d Type=%d D-Size=%d\r\n",dCPHead.X_pos,dCPHead.Y_pos,dCPHead.Width,dCPHead.Heigth,dCPHead.CompType,dCPHead.DataSize);
    X_PRINT("image_size_bw=%d %d\r\n",image_size_bw,image_size_bw_add);

    if(for_flash==1)
    {return;}


    p_dat_1 = RWBuff+21;
    L =  bLen-21;
    
    for(i = 0; i <20; i++)
    {              
      PRINT("%02X ",p_dat_1[i]); 
    }
    X_PRINT("==\r\n");
    
	switch(dCPHead.CompType)
	{
		case 0x01:

            test_flag_1 =1;
                
            remainder_no_Zero=0;
            if(L%2 != 0)
            {
               remainder_no_Zero=p_dat_1[L-1];
               Print_I3("RLE L=%d re=%x\r\n",L,remainder_no_Zero);    
            }
            else
            {
               Print_I3("RLE L=%d no re\r\n",L);  
            }
            total_data=L;
            image_size_bw_add=0;
            for(i=0;i<L;i+=2)
            {            
                if(p_dat_1[i]==0){Print_I3("er\r\n");X_PRINT("(%02X-%02X)",p_dat_1[i],p_dat_1[i+1]);}
                
                Dat=(~(p_dat_1[i+1]));
                //Dat=p_dat_1[i+1];
    			//for(j=0;j<p_dat_1[i];j++)
                //{
    			//  SPI0_MasterSendByte(Dat);
    			//}
    			image_size_bw_add +=p_dat_1[i];
                SPI0_MasterTrans_0x(Dat,p_dat_1[i]);
                
                WWDG_SetCounter(0);//喂狗                              
            }            
            printf("S=%d %d\r\n\r\n",image_size_bw_add,total_data);                            
            //return UnCompressWriteEINK(Addr+21,PicSize,packbuff,bLen); //RLE重复压缩算法
           break;
		default  :
   		   printf("不压缩\r\n");  
		  //return WriteEINK(Addr+21,PicSize,packbuff,bLen);           //不压缩
		break;
    }
}


uint32_t EinkDisPlay(uint32_t Addr,uint8_t *RWBuff,uint32_t bLen,UINT8 for_flash)
{
    UINT16 i=0;

    UINT16 L;
	stCFHead dCFHead;
	//Qflash_Read(Addr,RWBuff,1100);

// 0x58,0x54,0x45,0x4B,
// 0x00,0x1E,0xE0,0x64,
// 0x00,0x00,0x95,0xF7,
// 0x02,
// 0x00,0x00,0x00,0x15,
// 0x00,

    Print_I3("L=%d",bLen);
    
	memcpy(dCFHead.FHead,RWBuff,4);   //获得文件头部
	dCFHead.CheckSum=RWBuff[7];       //获得checksum
	dCFHead.FileSize=ByteTOInt(RWBuff+8);  //获得数据大小
	dCFHead.FileQutity=RWBuff[12];      //图片数量
	
	//X_PRINT("Eink DisPlay is Start\r\n"); 
	//X_PRINT("Picture File Head Infomation\r\n");
	X_PRINT("HEAD:%c%c%c%c\r\n",dCFHead.FHead[0],dCFHead.FHead[1],dCFHead.FHead[2],dCFHead.FHead[3]);
	X_PRINT("Picture Qutity:%d,File Size:%d,CheckSum(L):%d\r\n",dCFHead.FileQutity,dCFHead.FileSize,dCFHead.CheckSum) ;

    //L=0;
    //for(i = 0; i <(17+1+21); i++)
    //{              
    //  PRINT("0x%02X,",RWBuff[i]); 
    //  L++;if(L>10) {L=0;printf("\r\n");}
    //}
    //X_PRINT("\r\n");

    
    if(memcmp(dCFHead.FHead,"XTEK",4)!=0)
    {
 	 X_PRINT("File Format erro\r\n");
 	 return 0;
    }

    if(dCFHead.FileQutity ==1)
    {
        File_Qutity=1;
        p_dat_1=RWBuff+17;
        L = bLen-17;
        WritePicToEink(0,p_dat_1,L,for_flash);
    }
    else // if(dCFHead.FileQutity ==2)
    {
        File_Qutity=2;
        File_2_addr = (RWBuff[17]<<24)|(RWBuff[18]<<16)|(RWBuff[19]<<8)|(RWBuff[20]);
        Print_I3("File-2-Addr=%x",File_2_addr);
        p_dat_1=RWBuff+17+4;
        L = bLen-17-4;
        WritePicToEink(0,p_dat_1,L,for_flash);
    }
    
    //for(i=0;i<dCFHead.FileQutity;i++)
    //{
    //WritePicToEink(Addr+ByteTOInt(&RWBuff[13+i*4]),RWBuff,bLen);        
    //}
    
 //  EinkBusyFlag=true;
   return 0;
}





/*******************************************************************************
* @brief 计算一个数组的累加和
* @param [in]  data       指针指向一个数组
*        [in]  dlen       数据长度
* @return 返回计算结果
*******************************************************************************/
static UINT32 dataCheckSum(UINT8 *data,UINT16 dlen)
{
	int i=0;
	UINT32 temp = 0;
	for (i = 0; i < dlen; i++)
	{
		temp += data[i];
	}
    return temp;	
}




/*******************************************************************************
* @brief 生成指令包    
* @param [in]  [out]buff  指针指向buff,将指令添加到buff
*        [in]  type       包的类型
*        [in]  cmd        指令
*        [in]  param      指针指向指令的参数
*        [in]  plen       指令的长度
* @return 无返回值
*******************************************************************************/
void gen_cmdPack(UINT8 *buff,UINT8 type,UINT8 cmd,UINT8 *param,UINT8 plen)
{
	UINT32 calcsum = 0;
    CMD_PACK_S *cmdPack = (CMD_PACK_S *)buff;
    memcpy(cmdPack->head,PACKHEAD,3);
    cmdPack->type = type;
	cmdPack->cmd  = cmd ;
	if(param != NULL)
	{
	    memcpy(cmdPack->param,param,plen);
	}
	cmdPack->cmdLen = plen + CPHLEN  ;
	calcsum = dataCheckSum(cmdPack->param,plen);
	calcsum += cmd;
	cmdPack->checkSum = (UINT8) calcsum; 
}



/*******************************************************************************
* @brief ble回复函数
*        实现数据ble回复
*
* @param [in] data 需要上行的数据
*        [in] dlen 上行的数据长度
* @return 无返回值
*******************************************************************************/
//void BlePackSend(UINT8 cmd,UINT8 *data,UINT8 dlen)
//{

//    UINT8 i;
//    UINT8 retbuff[240];// 240
//    Print_I3("\r\n");    
//	CMD_PACK_S *cmdpack = (CMD_PACK_S *)retbuff;
//	gen_cmdPack(retbuff, TYPEHOSTPACK,cmd,data,dlen);
//	rdtss_send_notify(retbuff,cmdpack->cmdLen);
//}

/*******************************************************************************
* @brief 设置bitmap
*
* @param [in] pos  位置
*        [in] bmap 指向bitmap
* @return 无返回值
*******************************************************************************/
static void setblock(int pos,UINT8 *bmap)
{
	bmap[pos/8]|=0x80>>(pos%8); 	
}




/*******************************************************************************
* @brief 获得数据包的长度
* @param [in]  pack  指向数据包或者指令包
*        [in]  isDataPack  true 数据包 false 指令包
* @return 数据长度
*******************************************************************************/
UINT16 get_packLength(void *pack)
{
    int dataLen = 0;
    DATA_PACK_S *datapack = (DATA_PACK_S *)pack;
    if(memcmp(datapack->head,PACKHEAD,3)!=0)
    {
    	return 0;
    }
    (datapack->type == TYPEDATAPACK)?(dataLen =(datapack->dataLen[0] << 8)\
    |datapack->dataLen[1]):(dataLen = datapack->dataLen[0]);
	
    return dataLen;	
}



/*******************************************************************************
* @brief 获得一个空的，或者有数据的buff 
* @param [in]  flag       true:申请，false:读出
*        [in]  ctype      指令类型 表示指令类型标号,0xff时无效
*        [OUT] pbuff      指向数据包缓存区
*        [in]  packlen    每包数据的长度      
*        [in]  plen       缓冲区大小
* @return 缓冲块的地址,失败返回NULL
*******************************************************************************/
DATA_PACK_S *get_packBuff(UINT8 *Head,UINT8 ctype,UINT8 *pbuff,UINT16 packlen,UINT8 plen,UINT8 flag)
{
	UINT8  i = 0;
	DATA_PACK_S *s_xtPack = NULL; 
	DATA_PACK_S *s_xrPack = NULL;
	for (i=0;i<plen;i++)
    {
		if (flag)   
		{
            s_xtPack = (DATA_PACK_S *)(pbuff + (i * packlen));
			//s_xtPack = (xtbpack *)(pbuff +ga_packOffset[i]);
			if (memcmp(s_xtPack->head,Head,3) != 0)
			{
				return s_xtPack;
			}
		}
        else
	    {		
		    s_xtPack = (DATA_PACK_S *)(pbuff + (i * packlen));
			//s_xtPack = (xtbpack *)(pbuff +ga_packOffset[i]);
			if (memcmp(s_xtPack->head,Head,3) == 0)
			{
				if (s_xtPack->type == ctype)
				{
					s_xrPack = s_xtPack;
					
					continue;
				}
				
				return s_xtPack;
			}
		}
	}
	
	return s_xrPack;
}

/*******************************************************************************
* @brief 数据包头部，校验确认
* @param [in]  onlyhead   true:只校验头部，false:校验头部和数据
*        [in]  datapack   数据包指针 
* @return  >0 校验成功 否则失败
*******************************************************************************/
int dataPackCheck(UINT8 onlyhead,DATA_PACK_S  *datapack)
{
	UINT32 checksum = 0;
	int dataLen = 0; // (datapack->dataLen[0] << 8)|datapack->dataLen[1];
	dataLen = get_packLength(datapack);
	dataLen -= DPHLEN;
    if (memcmp(datapack->head,PACKHEAD,3) != 0)
    {
		Print_I3("head erro\r\n");
		return -1;
	}

    //printf("dataLen=%d\r\n",dataLen);
    
	if (onlyhead == true)
	{
		return 1;
	}

	checksum  = dataCheckSum(datapack->data,dataLen);
	checksum += datapack->totalPack;
	checksum += datapack->packNumber;
	if (datapack->checkSum == (UINT8)checksum)
	{
        printf("OK checksum %02x %02x\r\n",datapack->checkSum,checksum);
	    return  1;
	}
    Print_I3("Err checksum %02x %02x\r\n",datapack->checkSum,checksum);    
    
	#if 0
	for(int i=0; i<dataLen; i++)
		printf("%02X " ,datapack->data[i]);
	printf("\r\n");
	printf("dataLen:%d datapack->checkSum:%d checksum:%d \r\n", dataLen, datapack->checkSum, checksum);
	#endif
    
	return  0;
}


typedef enum
{
	no_color = 0,
	green = 1,
	blue,
	cyan,
}rgb_color;

void CMDLedContral(UINT8 *cmd,UINT8 clen)
{
	rgb_color led_status;
	UINT8 retval[5]={0};
	UINT16 time_out = 0;
	
	retval[0] = RECMDFAILED;
	
	time_out = cmd[2]<<8|cmd[3];
	Print_I3("status is %x----COLOR is %X-----time is %d\n",cmd[0],cmd[1],time_out);
	switch(cmd[0])
	{
		case 0:
            //led_status = Rgb_Read();

            //retval[0] = RECMDSUCCESS;
            //retval[1] = cmd[0];	
            //retval[2] = 0;
            //if(led_status == cyan)
            //retval[3] = cmdcyan;
            //else if(led_status == blue)
            //retval[3] = cmdblue;
            //else if(led_status == green)
            //retval[3] = cmdgreen;
            //else if(led_status == no_color)
            //retval[3] = 0;
			BlePackSend(CMDLEDCONTROL,retval,4);
		break;
		
		case 1: //off
            //switch(cmd[1])
            //{
            //	case  0x00:		break;
            //	case  cmdred:	break;
            //	case  cmdgreen:	
            //		Rgb_Led_Off(green);	
            //	break;
            //	case  cmdblue:	
            //		Rgb_Led_Off(blue);	
            //	break;
            //	case  cmdyellow:break;
            //	case  cmdpurple:break;
            //	case  cmdcyan:	
            //		Rgb_Led_Off(cyan);	
            //	break;
            //	case  cmdwhite:	
            //		Rgb_Led_Off(no_color);	
            //	break;
            //	default:break;
            //}
            //retval[0] = RECMDSUCCESS;
            //retval[1] = cmd[0];
            //retval[2] = 0;
            //retval[3] = cmd[1];
			BlePackSend(CMDLEDCONTROL,retval,4);
		break;
		
		case 2: //on
            //switch(cmd[1])
            //{
            //	case  0x00:		break;
            //	case  cmdred:	break;
            //	case  cmdgreen:	
            //		Rgb_Led_OnOff(green, time_out);	
            //	break;
            //	case  cmdblue:	
            //		Rgb_Led_OnOff(blue, time_out);	
            //	break;
            //	case  cmdyellow:break;
            //	case  cmdpurple:break;
            //	case  cmdcyan:	
            //		Rgb_Led_OnOff(cyan, time_out);	
            //	break;
            //	case  cmdwhite:	break;
            //	default:break;
            //}
            //retval[0] = RECMDSUCCESS;
            //retval[1] = cmd[0];
            //retval[2] = 0;
            //retval[3] = cmd[1];
			BlePackSend(CMDLEDCONTROL,retval,4);
		break;
		
		default:break;
	}
	
}

/*******************************************************************************
* @brief 设置工作模式
*          
*
* @param [in] cmd  指针指向下行的指令 
* @return 无返回值
*******************************************************************************/
static void SetBleWorkMode(uint8_t *cmd)
{
	uint8_t retval[2];

    Print_I3("\r\n");
    //if(cmd[0] == 0x01)
    //{
    //	gSystemParam.Mode = 0xff;
    //}
    //else
    //{
    //	gSystemParam.Mode = 0x13;
    //}
	retval[0] = RECMDSUCCESS;
	retval[1] = RECMDSUCCESS;
	BlePackSend(CMDSETMODE,retval,2);
	//delay_n_10us(1000);
	//printf("WorkMode:%d\r\n",gSystemParam.Mode);
	//SystemParamerRW(&gSystemParam,SYSPARAM,false);
	resetAfterdiscon=true;
}

/*******************************************************************************
* @brief 入网指令
*        初次配网时,用于设置分组
*
* @param [in] cmd  指针指向下行的指令 
*        
* @return 无返回值
*******************************************************************************/
static void AcessPorcess(UINT8 *cmd)
{
	UINT8 retval[2];

    Print_I3("\r\n");
    
	//memcpy(&gSystemParam.Group[0],&cmd[0],2); /**<更新gSystemParam*/
	//advModeInit(&gSystemParam);
	retval[0] = RECMDSUCCESS;
	retval[1] = RECMDSUCCESS;
	BlePackSend(CMDACESS,retval,2);
	//delay_n_10us(1000);
	//SystemParamerRW(&gSystemParam,SYSPARAM,false);
	
}


/*******************************************************************************
* @brief 确认bitmap
*        确认bitmap是否完整
*       
*
* @param [in] num 数据包的长度 
*        [in] bip 指向bitmap
* @return 0:成功，-1:失败
*******************************************************************************/
int checkBitmap(uint8_t num,uint8_t *bip)
{
	 int i;
	 for(i=0;i<(num/8);i++)
	 {
		if(bip[i]!=0xff) 
		{
			return -1;
		}
	 }
	 for(i=0;i<(num%8);i++)
	 {
		if((bip[num/8]&(0x80>>i))==0)
		{
			return -1;
        }			
	 }
	 return 0;
}


uint8_t checkPack(uint8_t *data,uint8_t totalpack)
{
	uint8_t ind = 0;
  	uint8_t i;
	Print_I3("\r\n");
    for (ind=0;ind<15;ind++)
    {
    	printf("%02X ",bbitmap[ind]);
    }
	ind = 0;
    if ((totalpack > 0)&&(checkBitmap(totalpack,bbitmap) < 0))
    {
    	memcpy(data,bbitmap,totalpack/8+1);
    	ind += totalpack/8+1;
    }

    for (i=0;i<15;i++)
    {
    	printf("%02X ",data[i]);
    }

    printf("\r\n");
    
	return ind;
}

/*******************************************************************************
* @brief OTA 功能
*        
*        可实现APP和boot的OTA
*        
*
* @param [in] cmd  指针指向下行的指令 
*    
* @return 无返回值
*******************************************************************************/
static void OTAProcess(UINT8 *cmd)
{
//	int i=0;
//	UINT8  otaFlag=0;
//	UINT8  fVersion[7];
//	UINT8  fBuff[128];
//	UINT8  TarCRC  = 0;
//	uint32_t HeadCRC = 0;
//	uint32_t CalCRC  = 0;
//	uint32_t DataCount = 0; //(gFileSize-15) / 128;
//	uint32_t DataSurp  = 0; //(gFileSize-15) % 128;
//    i = checkPack(&fBuff[1],g_totalPack);

    Print_I3("\r\n");

    
//	if (i > 0)
//	{
//	    fBuff[0] = RECMDFAILED;
//		BlePackSend(CMDOTA,fBuff,i + 1);
//		return ;
//	}    
//	if (gFileSize > FSAVERSIZE)
//	{
//		Qflash_Read(FLSHDATASTART,fBuff,15);
//	}
//	else
//	{
//		memcpy(fBuff,arrFileSaveRAM,15); /*<如果存储再RAM,直接从RAM获取*/
//	}
//	
//    Print_I3("FUNC:%s,start\r\n",__func__);
//	TarCRC=fBuff[3];              /*<获得文件校验和*/
//	gFileSize = ByteTOInt(fBuff+4);;
//	memcpy(fVersion,fBuff+8,7);   /*<获得版本号*/
//	lastCheckSum(&HeadCRC,fBuff+4,15-4);  /*<计算头部校验和*/
//	Print_I3("FUNC:%s,ota file size:%d\r\n",__func__,gFileSize);
//	DataCount = (gFileSize-15) / 128;
//	DataSurp  = (gFileSize-15) % 128;
//	if(!memcmp(fBuff+8,"OTAT",4))
//	{
//		otaFlag=0x01;
//	    Print_I3("OTA Application....\r\n");
//	}
//	if(!memcmp(fBuff+8,"OBOT",4))
//	{
//		 Print_I3("OTA BootLoad....\r\n");
//	     otaFlag=0x02;
//	}
//	if(otaFlag)
//	{   
//		if (gFileSize > FSAVERSIZE)
//		{
//	        for(i=0;i<DataCount;i++)
//		    {
//		        Qflash_Read((FLSHDATASTART+15)+(i*128),fBuff,128);
//		    	lastCheckSum(&CalCRC,fBuff,128);
//		    }
//		    if(DataSurp>0)
//		    {
//		    	Qflash_Read((FLSHDATASTART+15)+(i*128),fBuff,DataSurp);
//		    	lastCheckSum(&CalCRC,fBuff,DataSurp);
//		    }
//	    }
//		else
//		{
//		     CalCRC=0;
//			 lastCheckSum(&CalCRC,arrFileSaveRAM+15,gFileSize-15);
//		}
//	    CalCRC+=HeadCRC;
//		Print_I3("OTA Calc CRC Value:%x--%x  Head CRC:%d\r\n",CalCRC,TarCRC,HeadCRC);
//		if ((UINT8)CalCRC == TarCRC)
//		{
//		    Print_I3("FUNC:%s,OTA CRC  is OK\r\n",__func__);
//			Qflash_Read(FLSHDATASTART+(DEVICETYPESTART-APPCODESTART)+15,fBuff,16);
//			Print_I3("FUNC:%s,device type:%s\r\n",__func__,fBuff);

//			if(otaFlag==0x01 && !memcmp(Devicetype,fBuff,sizeof(Devicetype)))
//			{
//				Print_I3("FUNC:%s,APP OTA Start\r\n",__func__);
//				gSystemParam.OTAFlag=0x12;
//				memcpy(gSystemParam.FWversion,&fVersion[4],3);
//				NS_LOG_INFO("SYSTEM START...\r\n");
//				NS_LOG_INFO("OTA Flag:%x,\r\n",gSystemParam.OTAFlag);
//				NS_LOG_INFO("SYSTEM Version:%x,%x,%x,\r\n",\
//				gSystemParam.FWversion[0],gSystemParam.FWversion[1],gSystemParam.FWversion[2]);
//				SystemParamerRW(&gSystemParam,SYSPARAM,0);
//				
//				fBuff[0] = RECMDSUCCESS;
//				BlePackSend(CMDOTA,fBuff,1);
//				//ledTrunOnTimer(blue,1000);
//				delay_n_10us(1000);
//				resetAfterdiscon= true;
//				g_taskSuccess   = true;
//				Print_I3("OTA APP Check sum OK!\r\n");
//				Print_I3("Set OTA flag(0x12),retset!\r\n");
//				return ;
//			}
//			else
//			{
//				Print_I3("FUNC:%s,APP %.*s not for %s\r\n",__func__,
//									sizeof(Devicetype),fBuff,Devicetype);
//			}
//			if(otaFlag==0x02)
//			{   
//				Print_I3("FUNC:%s,boot OTA Start\r\n",__func__);
//				otaFlag=10;  
//				do
//				{  
//					memset(&gSystemParam, 0xFF, sizeof(SysParam));
//					gSystemParam.OTAFlag=0x00;
//					memcpy(gSystemParam.BootVersion,&fVersion[4],3);
//					NS_LOG_INFO("SYSTEM START...\r\n");
//					NS_LOG_INFO("OTA Flag:%x,\r\n",gSystemParam.OTAFlag);
//					NS_LOG_INFO("SYSTEM Version:%x,%x,%x,\r\n",\
//					gSystemParam.BootVersion[0],gSystemParam.BootVersion[1],gSystemParam.BootVersion[2]);
//					SystemParamerRW(&gSystemParam,SYSPARAM,0);
//					for(int i=0;i<gFileSize;i+=4096)  /**<擦除flash*/
//					{
//						Qflash_Erase_Sector(BOOTCODESTART+i);
//					}
//					if (gFileSize <= FSAVERSIZE)
//					{
//						Qflash_Write(BOOTCODESTART,arrFileSaveRAM+15,gFileSize-15);
//					}
//					else
//					{
//						for(i=0;i<DataCount;i++)
//						{
//							Qflash_Read((FLSHDATASTART+15)+(i*128),fBuff,128);
//							Qflash_Write(BOOTCODESTART+(i*128),fBuff,128);
//						}
//						if(DataSurp>0)
//						{
//							Qflash_Read((FLSHDATASTART+15)+(i*128),fBuff,DataSurp);
//							Qflash_Write(BOOTCODESTART+(i*128),fBuff,DataSurp);
//						}
//					} 
//					CalCRC=0;
//					for(i=0;i<DataCount;i++)
//					{
//						Qflash_Read(BOOTCODESTART+(i*128),fBuff,128);
//						lastCheckSum(&CalCRC,fBuff,128);
//					}
//					if(DataSurp>0)
//					{
//						Qflash_Read(BOOTCODESTART+(i*128),fBuff,DataSurp);
//						lastCheckSum(&CalCRC,fBuff,DataSurp);
//					}
//					Print_I3("OTA Calc CRC Value:%x--%x  Head CRC:%d\r\n",CalCRC,TarCRC,HeadCRC);
//					CalCRC+=HeadCRC;
//					if ((UINT8)CalCRC == TarCRC)
//					{
//						Print_I3("OTA boot Check sum OK!\r\n");
//						Print_I3("OTA boot success!\r\n");
//						fBuff[0] = RECMDSUCCESS;
//						BlePackSend(CMDOTA,fBuff,1);
//						resetAfterdiscon=true;
//						g_taskSuccess   = true;
//						return ;
//					}
//				}while(otaFlag--);
//			}
//		}
//	 }
//	 Print_I3("FUNC:%s,BLE Slave OTA failed \r\n",__func__);
//	 fBuff[0] = RECMDFAILED;
//	 BlePackSend(CMDOTA,fBuff,1);
}


/*******************************************************************************
* @brief 返回bitmap
*        当申请获取bitmap时调用此函数   
*        将返回当前bitmap.
*
* @param [in] cmd  指针指向下行的指令 
*    
* @return 无返回值
*******************************************************************************/
static void UpperBitmap(UINT8 *cmd)
{
	UINT16 tPack = 0;

    Print_I3("\r\n");
    
	tPack = (g_totalPack / 8) +1;
    BlePackSend(CMDGETBMAP,bbitmap,tPack);
}


/*******************************************************************************
* @brief 集合指令处理函数
*        可以多条或者一条指令下发，再此函数会
*        对指令逐条执行
*
* @param [in] cmd  指针指向下行的指令 
*        [in] clen 指令数据的长度
* @return 无返回值
*******************************************************************************/
//const char devtest[52] = "1637,ELS2.9,168X384$BWR$C,TAG111111111,313805006f20";
static void MassCMDProcess(UINT8 *cmd, UINT8 clen)
{
	UINT8 ind = 0;
 	UINT8 i;
	UINT8 retval[20];



    for(i=0;i<clen;i++)
    {
      printf("%02x ",cmd[i]);
    }
    Print_I3("---\r\n");

	retval[0] = RECMDSUCCESS;
	retval[1] = 0;    
    
//    ind = checkPack(&retval[1],g_totalPack);
//    printf("IND:%d,TOTAL:%d\r\n",ind,g_totalPack);
//    if (ind > 0)
//    {
//       retval[0] = RECMDFAILED;
//       Print_I3("er\r\n");
//    }
	
	if (retval[0] == RECMDSUCCESS)
	{
		g_taskSuccess = true;
		switch(cmd[0])
		{
			case 0: referPicture = true;
              printf("referPicture\r\n");
              break;
			case 0x02: referMessage = true; 
              printf("referMessage\r\n");
              break;
		}
	}	
	
	BlePackSend(CMDPICDIS,retval,ind + 1);  // +1
//    DelayMs(200);
//	BlePackSend(CMDPICDIS,retval,ind + 1);  // +1    
	
//	if(gSystemParam.Factory == 0xFF)
//	{
//		gSystemParam.Factory = 0;
//		SystemParamerRW(&gSystemParam, SYSPARAM, 0);
//		resetAfterdiscon = 1;
//	}
}


/*******************************************************************************
* @brief 申请flash空间
*        当数据量大于FSAVERSIZE时擦除flash空间。
*        当数据量小于FSAVERSIZE忽略直接返回      
*
* @param [in] cmd  指针指向下行的指令 
*    
* @return 无返回值
*******************************************************************************/
static void ApplicationFlash(UINT8 *cmd)
{
	UINT8 retval[2];
    gFileSize=cmd[0]<<24|cmd[1]<<16|cmd[2]<<8|cmd[3];


    retval[1]=0;

    Print_I3("gFileSize=%d",gFileSize);    

    //printf("size %02X  %02X %02X\r\n",cmd[0],cmd[1],cmd[2],cmd[3]);    

    
	if (gFileSize > MAXSAVEFLASH)
	{
		retval[0] = RECMDFAILED;
       Print_I3("Er gFileSize=%d",gFileSize);            
	}	
    else
    {		
		if(gFileSize > FSAVERSIZE)  // 20480/1024=20K
		{
			X_PRINT("Erase flash size:%d..........\r\n",gFileSize);            
           //for(int i=0;i<gFileSize;i+=4096)  /**<擦除flash*/
           //{
           //	Qflash_Erase_Sector(FLSHDATASTART+i);
           //}
		}
		retval[0] = RECMDSUCCESS;
    }
	memset(bbitmap,0x00,sizeof(bbitmap)); /**<清除bbitmap*/
    BlePackSend(CMDEREFLASH,retval,2);
}


/*******************************************************************************
* @brief 指令处理函数
*        指令处理的入口      
*
* @param [in] cmd  指针指向下行的指令 
*        [in] clen 指令数据的长度
* @return 无返回值
*******************************************************************************/
static void CMDProcess(CMD_PACK_S *cmdpack)
{
    UINT16 i;
    Print_I3("CMD%02X\r\n",cmdpack->cmd);
    
    switch(cmdpack->cmd)
	{
		case CMDEREFLASH:
            Bluetooth_Dat_counter=0;
            Flash_256byte_counter=0;
            Flash_image_counter=0;
            Total_data=0;
            
            #ifdef Save_to_Flash_xt        
             Start_Flash_power();
             Set_Spi1_output_init();    
             SPI_FLASH_ReadManuID_DeviceID(0x000000);
             SPI_FLASH_ReadManuID_DeviceID(0x000000);
             SPI_FLASH_ReadManuID_DeviceID(0x000000);
             //SPI_FLASH_SectorErase_2(0); // 4K
             SPI_FLASH_BulkErase_2(0);  // 64K          
             SPI_FLASH_BulkErase_2(1);  // 64K          
             SPI_FLASH_BulkErase_2(2);  // 64K          
             File_Qutity=1;
            #endif            
                
            ApplicationFlash(cmdpack->param);      
            printf("CMD ERE FLASH..申请flashr \r\n");
            break; /**<申请flash  */
        
		case CMDGETBMAP :
            UpperBitmap(cmdpack->param);      
            printf("CMDGETBMAP..获取bitmap \rr\n");
            break; /**<获取bitmap */
        
		case CMDPICDIS:

            //UINT8 head[3] ; /**<包头*/
            //UINT8 type    ; /**<数据的类型*/
            //UINT8 cmdLen  ; /**<指令长度  */
            //UINT8 checkSum; /**<校验和*/
            //UINT8 cmd     ; /**<指令码*/
            //UINT8 param[CPARAMLEN]; /**<指令包参数长度*/

            printf("type=%02x\r\n",cmdpack->type);
            printf("cmdLen=%02x\r\n",cmdpack->cmdLen);
            printf("checkSum=%02x\r\n",cmdpack->checkSum);
            printf("cmd=%02x\r\n",cmdpack->cmd);

            //03 01 AE 01 D8 39 00 20 C8   A
            //03 02 EA 00 D8 39 00 20 C8   B
            //03 03 2C 01 D8 39 00 20 C8   AB
            
            
            
            //printf("disp=%02x %02x %02x %02x\r\n",cmdpack->param[0],cmdpack->param[1],cmdpack->param[2],cmdpack->param[3]);
            for(i=0;i<cmdpack->cmdLen;i++)
            {
             printf("%02X ",cmdpack->param[i]);
            }printf("\r\n");

            
            //单面推图：0:无参数,1:带参数
            //消息：2:无参数
            //双面推图：3:带参数
            //prism推图: 4:带参数

            //1）单面推图：图片序号（1B）+刷新标志（1B）+ 参数
            //   图片序号：1 ~ 6， 0 时图片全删除
            //   刷新标志：0: 保存并立即刷新，1: 保存但不刷新
            //             2： 图片轮播       3：定时任务
            //   参    数：图片轮播（1B）： 1-255分钟
            //             定时任务（2B）： 时分
            //2）双面推图：1:A面，2：B面，3 A+B面
            //prism推图：动画类型（1B）: 0:无动画；1-255：不同动画类型


            // Last 146 75 19346   19456=110
            Total_data+=Bluetooth_Dat_counter-17-21;
            Print_I3("Last %d %d %d",Bluetooth_Dat_counter,Flash_256byte_counter,Total_data);            
            //for(i=0;i<Bluetooth_Dat_counter;i++)
            //{
            //  printf("%02X ",Flash_Buffer[i]);
            //}
            //printf("\r\n");
            
            TDX_SPI_FLASH_W_256Bytes_xt(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size);        
        
            MassCMDProcess(cmdpack->param,CPARALENS(cmdpack->cmdLen));



            //03 01 AE 01 D8 39 00 20 C8   A
            //03 02 EA 00 D8 39 00 20 C8   B
            //03 03 2C 01 D8 39 00 20 C8   AB
            if(cmdpack->param[1]==1) // A
            {
                Print_I3("CMD PICDIS..A Screen");
                //Xin_Tai(1);
                File_Qutity=1;
            }
            else if(cmdpack->param[1]==2) // B
            {
                Print_I3("CMD PICDIS..B Screen");
                //Xin_Tai(2);
                File_Qutity=2;
            }
            else //if(cmdpack->param[1]==3) // AB
            {
                Print_I3("CMD PICDIS..AB Screen");
                //Xin_Tai(1);
                //AB_only();
                File_Qutity=3;
                //Xin_Tai(2);
            }
            tmos_start_task(main_task_ID,EVENT_Test_only ,3200);


            //Stop_advertising();

            //for(i=0;i<10;i++)
            //{
            //X_PRINT("0x%02X ",p_dat_1[i]);
            //}            
            //EinkDisPlay(0,p_dat_1,60);            
			break; /**<指令集合   */
        
		case CMDOTA     : 
            OTAProcess(cmdpack->param);      
            printf("CMDOTA..OTA\r\n");
            break; /**<OTA */
        
		case CMDACESS   : 
            AcessPorcess(cmdpack->param);
            printf("CMDACESS..r\n");
            break; /**<入网设置*/
        
 		case CMDSETMODE :
            SetBleWorkMode(cmdpack->param);
            printf("CMDSETMODE.设置工作模式.\r\n");
            break; /**<设置工作模式*/
        
		case CMDLEDCONTROL:
            CMDLedContral((cmdpack->param),CPARALENS(cmdpack->cmdLen)); 
            printf("CMDLEDCONTROL..r\n");
		    break;
        
	    default:
            printf("default..r\n");
            break;
	} 		
}




/*******************************************************************************
* @brief 指令包头部，指令校验确认
* @param [in]  onlyhead   true:只校验头部，false:校验头部和数据
*        [in]  cmdpack    指令包指针 
* @return  >0 校验成功 否则失败
*******************************************************************************/
int cmdPackCheck(UINT8 onlyhead,CMD_PACK_S  *cmdpack)
{
    UINT8 i;
	UINT32 checksum = 0;
	int dataLen = 0;  // cmdpack->cmdLen;

    //UINT8 head[3] ; /**<包头*/
    //UINT8 type    ; /**<数据的类型*/
    //UINT8 cmdLen  ; /**<指令长度  */
    //UINT8 checkSum; /**<校验和*/
    //UINT8 cmd     ; /**<指令码*/
    //UINT8 param[CPARAMLEN]; /**<指令包参数长度*/

    printf("%c%c%c\r\n",cmdpack->head[0],cmdpack->head[1],cmdpack->head[2]);    
    printf("type %02X \r\n",cmdpack->type);    
    printf("cmdLen %02X \r\n",cmdpack->cmdLen);    
    printf("checkSum %02X \r\n",cmdpack->checkSum);    
    printf("cmd %02X \r\n",cmdpack->cmd);        
	dataLen = get_packLength(cmdpack);
	dataLen -= CPHLEN;

    if(dataLen >0)
    {
        for(i = 0; i <dataLen; i++)
        {
         PRINT("%02X,",cmdpack->param[i]); 
        }
         PRINT("\r\n");         
    }

    
    if (memcmp(cmdpack->head,PACKHEAD,3) != 0)
    {
		X_PRINT("FUNC:%s,head erro\r\n",__func__);
		return -1;
	}
    
    //if (onlyhead == true)
    //{
    //    Print_I3("onlyhead\r\n");
    //	return 1;
    //}
    
    checksum  = dataCheckSum(cmdpack->param,dataLen);
    checksum += cmdpack->cmd;
	if (cmdpack->checkSum == (UINT8)checksum)
	{
        Print_I3("checksum OK\r\n");
	    return  1;
	}
	X_PRINT("FUNC:%s,check sum erro\r\n",__func__);
	return  0;
}

void packRecvInit(void)
{
	memset(bbitmap,0x00,sizeof(bbitmap)); /**<清除bbitmap*/
	gPackLen=0;  /**<包总长度*/
    gPackRev=0;  /**<已经接收的包的长度*/
	gFileSize=0; /**<文件的大小*/
	g_packdown = false;
	loopflag   = false;
}




void PackProcessCallBack_Save_To_Flash(uint8_t const *p_data,DATA_PACK_S *dataPack, unsigned char w_len)
{     
    UINT16 i,j,Dat,L;
	//UINT8 *data = NULL;
	//DATA_PACK_S *dataPack = NULL;
	UINT16 dataLen = 0;
	loopflag=true;

    printf("L=%d\r\n",w_len);

    //if (memcmp(dataPack->head,PACKHEAD,3) != 0)
    //{
    // 	return -1;
    //}

	//for (;;)
	//{
        //if (dataPack != NULL)
        //{
        //clearPackBuff((UINT8 *)dataPack);
        //}
        
       //dataPack = get_packBuff((UINT8 *)PACKHEAD,TYPESLAVEPACK,packbuff,TODATAPACK,PACKBCOU,false);
       //if (dataPack == NULL)
       //{
       //loopflag=false;
       //return ;
       //}


      if (memcmp(dataPack->head,PACKHEAD,3) == 0)
      {            
		if (dataPack->type == TYPEDATAPACK)
		{
            //printf("头部-3B-XTE %02X  %02X %02X\r\n",dataPack->head[0],dataPack->head[1],dataPack->head[2]);    
            //printf("类型 1B 数据时=0x02 %02X \r\n",dataPack->type);    
            //printf("数据长度 2B 整个数据长度-头部到参数最末端-%02X %02X\r\n",dataPack->dataLen[0],dataPack->dataLen[1]);        
            //printf("校验和-1B-指令到参数的最末端的累加和-取最低位 %02X \r\n",dataPack->checkSum);    
            //printf("总包数-1B-数据总包数 %02X \r\n",dataPack->totalPack);    
            //printf("包编号-1B-当前包数编号 %02X \r\n",dataPack->packNumber);        
            //printf("数 %02X %02X %02X %02X %02X\r\n",dataPack->data[0],dataPack->data[1],dataPack->data[2],dataPack->data[3]);

            printf("包 %02x-%02x----%x%x %d\r\n",dataPack->totalPack,dataPack->packNumber,dataPack->dataLen[1],dataPack->dataLen[0],j);
            PRINT(".%d  %d  %d\r\n",Bluetooth_Dat_counter,Flash_256byte_counter,Flash_image_counter);
            
            p_dat_1=p_data+9;L=w_len-9;// 这 9 个字节， 是包的头部，去了
            for(i = 0; i <L; i++)
            {              
              Flash_Buffer[Bluetooth_Dat_counter]=p_dat_1[i];
              Bluetooth_Dat_counter++;
              if(Bluetooth_Dat_counter >=256)
              {                
                 Total_data+=Bluetooth_Dat_counter;
                 TDX_SPI_FLASH_W_256Bytes_xt(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size);        
                 Flash_256byte_counter++;
                 Bluetooth_Dat_counter =0;
              }                
            }            
            PRINT("%d  %d  %d\r\n",Bluetooth_Dat_counter,Flash_256byte_counter,Flash_image_counter);

            
            if(File_head==0)
            {
               // p_dat_1=p_data+9;L=w_len-9;                
               EinkDisPlay(0,p_dat_1,L,1);
               File_head=1;                
            }
            //else{Total_data-=9;}
            return;


            //if (dataPackCheck(false,dataPack) <= 0)
            //{
            //    Print_I3("cmd pack check erro...\r\n");
            //    //continue;				
            //}	
            
			if(dataPack->packNumber == 0)
			{
			   g_totalPack = dataPack->totalPack;
               PRINT("g_totalPack=%d\r\n",g_totalPack); 
			}
			dataLen  = DDATALENS(get_packLength(dataPack));

           //printf("dataLen %d \r\n",dataLen);                                
 		   //X_PRINT("FUNC:%s,DATAPACK %d -%d,len:%d\r\n",__func__,dataPack->packNumber,dataPack->totalPack,dataLen);
           //if (gFileSize > FSAVERSIZE)
           //{
           //    Qflash_Write(FLSHDATASTART+(dataPack->packNumber*PACKDATALEN),dataPack->data,dataLen );
           //}
           //else
           //{	
           //	if(dataPack->packNumber*PACKDATALEN > FSAVERSIZE)
           //		continue;
           //    memcpy(arrFileSaveRAM+(dataPack->packNumber*PACKDATALEN),dataPack->data,dataLen );
           //}
		    setblock(dataPack->packNumber,bbitmap);			
		}
		else if (dataPack->type == TYPESLAVEPACK)
		{           
            PRINT("指令\r\n");    
            File_head =0;
            
            if (cmdPackCheck(false,(CMD_PACK_S *)dataPack) <= 0)
            {
            	X_PRINT("cmd pack check erro...\r\n");
                return;
               //continue;
            }	
            CMDProcess((CMD_PACK_S *)dataPack); /**<执行指令*/			
		}
      } 
      else
      {
          total_data = total_data+w_len;        

//            j=0; 
//            for(i=0;i<w_len;i++)  //w_len
//            {
//              PRINT("0x%02X,",p_data[i]); 
//              j++;
//              if(j>20) {j=0;PRINT("\r\n");}
//            }
//        PRINT("Dat \r\n");
            PRINT("Dat=%d %d %d\r\n",Bluetooth_Dat_counter,Flash_256byte_counter,Flash_image_counter);
            for(i = 0; i <w_len; i++)
            {
                  Flash_Buffer[Bluetooth_Dat_counter]=p_data[i];
                  Bluetooth_Dat_counter++;                  
                  if(Bluetooth_Dat_counter >=256)
                  {  
                     Total_data+=Bluetooth_Dat_counter;
                     TDX_SPI_FLASH_W_256Bytes_xt(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size);        
                     Flash_256byte_counter++;
                     Bluetooth_Dat_counter =0;
                  }                
            }
          PRINT("%d  %d  %d\r\n",Bluetooth_Dat_counter,Flash_256byte_counter,Flash_image_counter);          

      }
      
	//}
}


//void PackProcessCallBack(unsigned char *pack, unsigned char w_len)
void PackProcessCallBack(uint8_t const *p_data,DATA_PACK_S *dataPack, UINT16 w_len,UINT8 file_head)
{     
    UINT16 i,j,Dat,L;
	//UINT8 *data = NULL;
	//DATA_PACK_S *dataPack = NULL;
	UINT16 dataLen = 0;
	loopflag=true;

    //printf("L=%d\r\n",w_len);
//    j=0; 
//    for(i=0;i<w_len;i++)  //w_len
//    {
//      PRINT("0x%02X,",p_data[i]); 
//      j++;
//      if(j>20) {j=0;PRINT("\r\n");}
//    }
//    PRINT("--\r\n");

    

    //if (memcmp(dataPack->head,PACKHEAD,3) != 0)
    //{
    // 	return -1;
    //}

	//for (;;)
	//{
        //if (dataPack != NULL)
        //{
        //clearPackBuff((UINT8 *)dataPack);
        //}
        
       //dataPack = get_packBuff((UINT8 *)PACKHEAD,TYPESLAVEPACK,packbuff,TODATAPACK,PACKBCOU,false);
       //if (dataPack == NULL)
       //{
       //loopflag=false;
       //return ;
       //}


      if(file_head==1) //if (memcmp(dataPack->head,PACKHEAD,3) == 0)
      {            
		//if (dataPack->type == TYPEDATAPACK)
		{
            //printf("头部-3B-XTE %02X  %02X %02X\r\n",dataPack->head[0],dataPack->head[1],dataPack->head[2]);    
            //printf("类型 1B 数据时=0x02 %02X \r\n",dataPack->type);    
            //printf("数据长度 2B 整个数据长度-头部到参数最末端-%02X %02X\r\n",dataPack->dataLen[0],dataPack->dataLen[1]);        
            //printf("校验和-1B-指令到参数的最末端的累加和-取最低位 %02X \r\n",dataPack->checkSum);    
            //printf("总包数-1B-数据总包数 %02X \r\n",dataPack->totalPack);    
            //printf("包编号-1B-当前包数编号 %02X \r\n",dataPack->packNumber);        
            //printf("数 %02X %02X %02X %02X %02X\r\n",dataPack->data[0],dataPack->data[1],dataPack->data[2],dataPack->data[3]);

            //j=dataPack->dataLen[1];
            //j=(j<<8);
            //j=(j|dataPack->dataLen[0]);
            //printf("包%d-%d %X%X\r\n",dataPack->totalPack,dataPack->packNumber,dataPack->dataLen[1],dataPack->dataLen[0]);

            //if (dataPackCheck(false,dataPack) <= 0)
            //{
            //    Print_I3("cmd pack check erro...\r\n");
            //    //continue;				
            //}	
            
//			if(dataPack->packNumber == 0)
//			{
//			   g_totalPack = dataPack->totalPack;
//               PRINT("g_totalPack=%d\r\n",g_totalPack); 
//			}
//			dataLen  = DDATALENS(get_packLength(dataPack));

//            if(File_head==0)
//            {
//                j=0; 
//                for(i=0;i<w_len;i++)  //w_len
//                {
//                  PRINT("0x%02X,",p_data[i]); 
//                  j++;
//                  if(j>20) {j=0;PRINT("\r\n");}
//                }
//                PRINT("--\r\n");
//            }

#if 1
            p_dat_1=p_data;L=w_len;            
            //if(File_head==0)
            //{
                EinkDisPlay(0,p_dat_1,L,0);
                File_head=1;                
                //wrier_BW();
            //}
#else
            //p_dat_1=p_data+9;L=w_len-9;
            p_dat_1=p_data;L=w_len;            
            if(File_head==0)
            {
                EinkDisPlay(0,p_dat_1,L,0);
                File_head=1;                
                //wrier_BW();
            }
            else
            {
                //for(i = 0; i <(w_len-9); i++)
//                for(i = 0; i <10; i++)
//                {              
//                  PRINT("0x%02X,",p_dat_1[i]); 
//                }
//                PRINT("--\r\n");           
                //printf("RLE重复压缩算法\r\n");  
                

                total_data = L;
                if(remainder_no_Zero !=0)
                {
                    Dat=p_dat_1[0];
                    if(BW_BR == Bww_Data) {Dat=(~(p_dat_1[0]));}                    
                    X_PRINT("vm %02X-%02X \r\n",remainder_no_Zero,p_dat_1[0]);
                    //for(j=0;j<remainder_no_Zero;j++)
                    //{
                    //SPI0_MasterSendByte(Dat);
                    //}
                    image_size_bw_add +=remainder_no_Zero;
                    SPI0_MasterTrans_0x(Dat,remainder_no_Zero);
                    WWDG_SetCounter(0);//喂狗
                    
                    if(image_size_bw_add>=image_size_bw)
                    {
                        if(BW_BR == Bww_Data)
                        {
                           Print_I3("RED_Data..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                            
                           XT_3Color_800_480_RED();
                           BW_BR = RED_Data;
                           image_size_bw_add=0;
                           //wrier_red();
                        }
                        else if(BW_BR == RED_Data)
                        {
                            Print_I3("Ove_Data..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                                                        
                            XT_3Color_800_480_end();
                            BW_BR = Ove_Data;
                        }
                        else if(BW_BR == Ove_Data)
                        {                            
                            Print_I3("over xFu..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                                                        
                            return;
                        }             
                        else
                        {
                           BW_BR = Err_Data; 
                           Print_I3("ER..");
                           return;
                        }
                    }

                    
                    p_dat_1=p_dat_1+1; 
                    L=L-1;                    
                }

                remainder_no_Zero=0;
                if(L%2!=0)
                {
                  remainder_no_Zero=p_dat_1[L-1];
                  printf("xRLE L=%d re=%d\r\n",L,remainder_no_Zero);    
                }
                //else
                //{
                //  printf("xRLE L=%d no re\r\n",L);  
                //}

                
                for(i=0;i<L;i+=2)
                {            
                    //X_PRINT("(%02X-%02X)",p_dat_1[i],p_dat_1[i+1]);
                    if(p_dat_1[i]==0)
                    {
                      Print_I3("er\r\n");
                      X_PRINT("(%02X-%02X)",p_dat_1[i],p_dat_1[i+1]);
                    }
                    Dat=p_dat_1[i+1];
                    if(BW_BR == Bww_Data){Dat=(~(p_dat_1[i+1]));}
                    //for(j=0;j<p_dat_1[i];j++)
                    //{
                    //  SPI0_MasterSendByte(Dat);
                    //}
                    
                    image_size_bw_add +=p_dat_1[i];            
                    SPI0_MasterTrans_0x(Dat,p_dat_1[i]);
                    WWDG_SetCounter(0);//喂狗
                    
                    if(image_size_bw_add>=image_size_bw)
                    {
                        if(BW_BR == Bww_Data)
                        {
                           Print_I3("xFu..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                            
                           XT_3Color_800_480_RED();
                           BW_BR = RED_Data;
                           image_size_bw_add=0;
                           //wrier_red();
                        }
                        else if(BW_BR == RED_Data)
                        {
                            Print_I3("xFu..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                                                        
                            XT_3Color_800_480_end();
                            BW_BR = Ove_Data;
                        }
                        else if(BW_BR == Ove_Data)
                        {
                            Print_I3("over xFu..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                                                        
                            return;
                        }                        
                        else
                        {
                           BW_BR = Err_Data; 
                           Print_I3("ER..");
                           return;
                        }
                    }                   
                    
                }               
                printf("xS=%d %d..\r\n\r\n....\r\n\r\n",image_size_bw_add,total_data);                            
            }           
            
           //printf("dataLen %d \r\n",dataLen);                                
 		   //X_PRINT("FUNC:%s,DATAPACK %d -%d,len:%d\r\n",__func__,dataPack->packNumber,dataPack->totalPack,dataLen);
           //if (gFileSize > FSAVERSIZE)
           //{
           //    Qflash_Write(FLSHDATASTART+(dataPack->packNumber*PACKDATALEN),dataPack->data,dataLen );
           //}
           //else
           //{	
           //	if(dataPack->packNumber*PACKDATALEN > FSAVERSIZE)
           //		continue;
           //    memcpy(arrFileSaveRAM+(dataPack->packNumber*PACKDATALEN),dataPack->data,dataLen );
           //}
		    setblock(dataPack->packNumber,bbitmap);	
#endif

		}
       //else if (dataPack->type == TYPESLAVEPACK)
       //{
       //    PRINT("指令\r\n");    
       //    File_head =0;
       //    
       //    if (cmdPackCheck(false,(CMD_PACK_S *)dataPack) <= 0)
       //    {
       //    	X_PRINT("cmd pack check erro...\r\n");
       //        return;
       //       //continue;
       //    }	
       //    CMDProcess((CMD_PACK_S *)dataPack); /**<执行指令*/			
       //}
      } 
      else
      {
          total_data = total_data+w_len;        
          //PRINT("数据=%d\r\n",total_data);    
          
//          if(test_flag_1==0)
//            return;

//          p_dat_1 = p_data;  
//          test_flag_1=0;
//          if(remainder_no_Zero !=0)
//          {
//            j=1;
//            L=w_len-1;
//            PRINT("re\r\n");
//          }
//          else
//          {
//            j=0;
//            L=w_len;
//            PRINT("no re\r\n");
//          }
//          

//            j=0; 
//            for(i=0;i<w_len;i++)  //w_len
//            {
//              PRINT("0x%02X,",p_data[i]); 
//              j++;
//              if(j>20) {j=0;PRINT("\r\n");}
//            }
//            PRINT("--\r\n");
            
        
          p_dat_1 = p_data;
          if(remainder_no_Zero !=0)
          {
              Dat=p_dat_1[0];
              if(BW_BR == Bww_Data){Dat=(~(p_dat_1[0]));}                    
              X_PRINT("xRm %02X-%02X\r\n",remainder_no_Zero,p_dat_1[0]);
              //for(j=0;j<remainder_no_Zero;j++)
              //{
              //  SPI0_MasterSendByte(Dat);
              //}
              image_size_bw_add +=remainder_no_Zero; 
              SPI0_MasterTrans_0x(Dat,remainder_no_Zero);
              WWDG_SetCounter(0);//喂狗

              
              if(image_size_bw_add>=image_size_bw)
              {
                  if(BW_BR == Bww_Data)
                  {
                     Print_I3("RED_Data..%d %d %x",image_size_bw_add,image_size_bw,BW_BR);                                       
                     XT_3Color_800_480_RED();
                     BW_BR = RED_Data;
                     image_size_bw_add=0;
                  }
                  else if(BW_BR == RED_Data)
                  {
                      Print_I3("Ove_Data..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                                                
                      XT_3Color_800_480_end();
                      BW_BR = Ove_Data;
                  }
                  else if(BW_BR == Ove_Data)
                  {
                      Print_I3("over Fu..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                    
                      return;
                  }
                  else
                  {
                     BW_BR = Err_Data; 
                     Print_I3("ER..");
                     return;
                  }                  
              }              
              p_dat_1=p_data+1; 
              L=w_len-1;              
          }
          else
          {
             p_dat_1=p_data; 
             L=w_len;              
          }

          remainder_no_Zero=0;
          if(L%2!=0)
          {
            //remainder_no_Zero=p_dat_1[L-1];
            remainder_no_Zero=p_data[w_len-1];
            printf("tRLE L=%d re=%d\r\n",L,remainder_no_Zero);    
          }
          //else
          //{
          //  printf("tRLE L=%d no re\r\n",L);  
          //}


          for(i = 0; i <L; i+=2)
          {
            //X_PRINT("(%02X-%02X)\r\n",p_dat_1[i],p_dat_1[i+1]);
            
            if(p_dat_1[i]==0)
            {
                Print_I3("er\r\n");
                X_PRINT("%d %02X-%02X",i,p_dat_1[i],p_dat_1[i+1]);

                //j=0; 
                //for(i=0;i<w_len;i++)  //w_len
                //{
                //  PRINT("0x%02X,",p_data[i]); 
                //  j++;
                //  if(j>20) {j=0;PRINT("\r\n");}
                //}
                
                BW_BR = Ove_Data;
                PRINT("--\r\n");                
                return;                
            }
            Dat=p_dat_1[i+1];
            if(BW_BR == Bww_Data){Dat=(~(p_dat_1[i+1]));}
			//for(j=0;j<p_dat_1[i];j++)
            //{
			//  SPI0_MasterSendByte(Dat);
			//}
			image_size_bw_add +=p_dat_1[i];            
            SPI0_MasterTrans_0x(Dat,p_dat_1[i]);
            WWDG_SetCounter(0);//喂狗
           
           if(image_size_bw_add>=image_size_bw)
           {
               if(BW_BR == Bww_Data)
               {
                  Print_I3("Fu..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                                       
                  XT_3Color_800_480_RED();
                  BW_BR = RED_Data;
                  image_size_bw_add=0;
               }
               else if(BW_BR == RED_Data)
               {
                   Print_I3("over..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                                                
                   XT_3Color_800_480_end();
                   BW_BR = Ove_Data;
                   return;                    
               }
               else if(BW_BR == Ove_Data)
               {
                   // by_lgp  如果是 AB 两个屏，两个图片，则只会走到这里来
                   //AB_only();
                   Print_I3("over Fu..%d %d %x\r\n",image_size_bw_add,image_size_bw,BW_BR);                    
                   return;
               }
               else
               {
                  BW_BR = Err_Data; 
                  Print_I3("ER..");
                  return;
               }               
           }
          }
          //PRINT("%d  %d  %d\r\n",Bluetooth_Dat_counter,Flash_256byte_counter,Flash_image_counter);          
          printf("kS=%d %d .......\r\n\r\n",image_size_bw_add,total_data);                            
      }
      
	//}
}


/*********************************************************************
 * @fn      on_bleuartServiceEvt
 *
 * @brief   ble uart service callback handler
 *
 * @return  NULL
 */
void on_bleuartServiceEvt(UINT16 connection_handle, ble_uart_evt_t *p_evt)
{
    UINT16 i;
    
    switch(p_evt->type)
    {
        case BLE_UART_EVT_TX_NOTI_DISABLED:
            PRINT("%02x:bleuart_EVT_TX_NOTI_DISABLED\r\n", connection_handle);
            break;
        case BLE_UART_EVT_TX_NOTI_ENABLED:
            PRINT("%02x:bleuart_EVT_TX_NOTI_ENABLED\r\n", connection_handle);
            break;
        case BLE_UART_EVT_BLE_DATA_RECIEVED:
            
            //PRINT("BLE RX DATA len:%d\r\n", p_evt->data.length);
            
            //for(i = 0; i < p_evt->data.length; i++)
            //{
            //  PRINT("%02X ",p_evt->data.p_data[i]); 
            //}
            //PRINT("\r\n");

            PackProcessCallBack_Save_To_Flash(p_evt->data.p_data,p_evt->data.p_data,p_evt->data.length);

            //PackProcessCallBack(p_evt->data.p_data,p_evt->data.p_data,p_evt->data.length);

#if 0
            //for notify back test
            //to ble
            UINT16 to_write_length = p_evt->data.length;
            app_drv_fifo_write(&app_uart_rx_fifo, (UINT8 *)p_evt->data.p_data, &to_write_length);
            tmos_start_task(Peripheral_TaskID, UART_TO_BLE_SEND_EVT, 2);
            //end of nofify back test

            //ble to uart
            app_uart_tx_data((UINT8 *)p_evt->data.p_data, p_evt->data.length);
#endif            

            break;
        default:
            break;
    }
}


void AB_only(void)
{
       UINT16 i,L,j;    

       //File_2_addr = 0x4ADC;  // 4B06
       //File_2_addr = 0x4B06;  // 


       File_2_addr = File_2_addr - 17-4;  // 这样才可以对全了
       Total_data=0;
       UC7279_Chip_Num=UC7279_Num_b;
       XT_3Color_800_480_init();      
       Start_Flash_power();
       Set_Spi1_output_init();        
       SPI_FLASH_ReadManuID_DeviceID(0x000000);
       SPI_FLASH_ReadManuID_DeviceID(0x000000);
     
       BW_BR = Bww_Data;
       Flash_256byte_counter=0;
       Flash_image_counter=0;       
       Bluetooth_Dat_counter =0;  
       File_head=0;

       WWDG_SetCounter(0);//喂狗 
       TDX_SPI_FLASH_Read_244Bytes_By_addr(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size,File_2_addr);
       Flash_256byte_counter++;
  
      //if(Flash_Buffer[0] != 0x58)
      //{
      //   Print_I3("Data Er....");        
      //   j=0; 
      //   for(i=0;i<256;i++)  //w_len
      //   {
      //     PRINT("0x%02X,",Flash_Buffer[i]); 
      //     j++;if(j>10) {j=0;PRINT("\r\n");}
      //   }PRINT("--\r\n");
      //   return;
      //}    
      //PackProcessCallBack(Flash_Buffer,Flash_Buffer,244,1);
      
       WritePicToEink(0,Flash_Buffer+17+4,244-17-4,0);
       File_head=1;       
    
       
       for(i = 0; i <375; i++)   // 48000 x 2 / 256
       {              
            WWDG_SetCounter(0);//喂狗 
            TDX_SPI_FLASH_Read_244Bytes_By_addr(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size,File_2_addr);
            Flash_256byte_counter++;
            PackProcessCallBack(Flash_Buffer,Flash_Buffer,244,0);
            if((BW_BR == Ove_Data) || (BW_BR == Err_Data))
            {           
              Print_I3("over "); 
              break;
            }         
       }    
}

//包 10-00
//0x58,0x54,0x45,0x02,0x04,0xC4,0xA7,0x10,0x00,0x58,0x54,0x45,0x4B,0x00,0x0F,0x70,0x10,0x00,0x00,0x4B,0x02,
//0x01,0x00,0x00,0x00,0x11,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x20,0x00,0x00,0x01,0xE0,
//0x01,0x00,0x00,0x4A,0xDC,0x26,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,
//0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,
//0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,
//0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,
//0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,
//0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,
//0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,
//0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,
//0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,
//0x01,0x80,0x62,0x00,0x01,0x0F,0x01,0x80,0x62,0x00,0x01,0x0F,0x01,--


//HEAD:XTEK
//Picture Qutity:1,File Size:19202,CheckSum(L):16
//[WritePicToEink:379] L=218
//X=0 Y=0 W=800 H=480 Type=1 D-Size=19164
//image_size_bw=48000 0
// 19202 - 19164 = 38 = 17(文件头部) + 21(图片信息)

// Diaplay_Qutity  1..A Screen  2..B Screen
void Xin_Tai(UINT8 Diaplay_Qutity)
{
    UINT16 i,L,j;
#if 1
if(Diaplay_Qutity==1)
{
    Total_data=0;
    File_Qutity=1;


    UC7279_Chip_Num=UC7279_Num_a;
    XT_3Color_800_480_init();            
    Start_Flash_power();
    Set_Spi1_output_init();        
    SPI_FLASH_ReadManuID_DeviceID(0x000000);
    SPI_FLASH_ReadManuID_DeviceID(0x000000);

    BW_BR = Bww_Data;    
    Flash_256byte_counter=0;
    Flash_image_counter=0;       
    Bluetooth_Dat_counter =0;    
    File_head=0;

    WWDG_SetCounter(0);//喂狗 
    TDX_SPI_FLASH_Read_244Bytes(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size);
    Flash_256byte_counter++;
    
    //j=0; 
    //for(i=0;i<256;i++)  //w_len
    //{
    //  PRINT("0x%02X,",Flash_Buffer[i]); 
    //  j++;
    //  if(j>20) {j=0;PRINT("\r\n");}
    //}
    //PRINT("--\r\n");
    //return;

    if(Flash_Buffer[0] != 0x58)
    {
        Print_I3("Data Er....");
        return;
    }    
    PackProcessCallBack(Flash_Buffer,Flash_Buffer,244,1);

    
    for(i = 0; i <375; i++)   // 48000 x 2 / 256
    {              
         WWDG_SetCounter(0);//喂狗 
         TDX_SPI_FLASH_Read_244Bytes(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size);
         Flash_256byte_counter++;
         PackProcessCallBack(Flash_Buffer,Flash_Buffer,244,0);
         if((BW_BR == Ove_Data) || (BW_BR == Err_Data))
         {
           Print_I3("over "); 
           break;
         }         
    }    
 }  
#endif    
//================================================================    
#if 1
 if(Diaplay_Qutity==2)
 {
    Total_data=0;
    UC7279_Chip_Num=UC7279_Num_b;
    XT_3Color_800_480_init();      
    Start_Flash_power();
    Set_Spi1_output_init();        
    SPI_FLASH_ReadManuID_DeviceID(0x000000);
    SPI_FLASH_ReadManuID_DeviceID(0x000000);
    

    BW_BR = Bww_Data;
    Flash_256byte_counter=0;
    Flash_image_counter=0;       
    Bluetooth_Dat_counter =0;  
    File_head=0;

    WWDG_SetCounter(0);//喂狗 
    TDX_SPI_FLASH_Read_244Bytes(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size);
    Flash_256byte_counter++;

    if(Flash_Buffer[0] != 0x58)
    {
        Print_I3("Data Er....");        
        j=0; 
        for(i=0;i<256;i++)  //w_len
        {
          PRINT("0x%02X,",Flash_Buffer[i]); 
          j++;if(j>20) {j=0;PRINT("\r\n");}
        }PRINT("--\r\n");
        return;
    }    
    PackProcessCallBack(Flash_Buffer,Flash_Buffer,244,1);

    
    for(i = 0; i <375; i++)   // 48000 x 2 / 256
    {              
         WWDG_SetCounter(0);//喂狗 
         TDX_SPI_FLASH_Read_244Bytes(Flash_Buffer,Flash_image_counter,Flash_256byte_counter,Max_image_Size);
         Flash_256byte_counter++;
         PackProcessCallBack(Flash_Buffer,Flash_Buffer,244,0);
         if((BW_BR == Ove_Data) || (BW_BR == Err_Data))
         {           
           Print_I3("over "); 
           break;
         }         
    }    
 }  
#endif    

}

#endif


