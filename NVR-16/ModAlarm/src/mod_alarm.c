#include "common_msgcmd.h"
#include "mod_alarm.h"
#include "mod_ptz.h"

#include "lib_md.h"
#include "lib_alarm.h"
#include "lib_misc.h"
#include "lib_video.h"

#include <unistd.h>

#include "diskmanage.h"

#include "public.h"

//typedef void* HDDHDR;
#define MAX_HDD_NUM	8

typedef enum
{
	EM_ALARM_PARA_GET = 0,
	EM_ALARM_PARA_SET,
} EMALARMPARAGETSET;

typedef struct
{
	EMALARMPARATYPE emMsg;
	EMALARMPARAGETSET emGetSet;
	u8 nChn;
	SAlarmPara *psAlaPara;
} SAlarmMsgHeader;

#define MAX_PARA_DATA_SIZE	512

typedef struct
{
	u8 nStatus;
	u8 nStatusLast;
	time_t nTime;
} SEvent;

typedef struct
{
	SAlarmPara sAlarmPara;
	SAlarmDispatch sAlarmDispatch;
	SAlarmSchedule sAlarmSchedule; 
	SEvent sEvent;
	u8 nSetChangeTimes;
	u8 nSetChangeTimesLast;
} SAlarmChn;

typedef struct
{
	u8 nChannels;
	SAlarmChn* psAlarmChn;
} SAlarmEvent;

typedef struct
{
	SAlarmPara sAlarmPara;
	SAlarmSchedule sAlarmSchedule;
	SEvent sEvent;
} SAlarmOutChn;

typedef struct
{
	u8 nChannels;
	SAlarmOutChn* psAlarmOutChn;
} SAlarmOut;

typedef struct
{
	SAlarmPara sAlarmPara;
	SEvent sEvent;
} SAlarmBuzzChn;

typedef struct
{
	u8 nChannels;
	SAlarmBuzzChn* psAlarmBuzzChn;
} SAlarmBuzz;

//跃天485报警板
typedef struct 
{
	int status;
	pthread_mutex_t lock;
}check_485ext_info;
static check_485ext_info ext485info;

typedef struct
{
	SAlarmEvent sSensors;
	//yaogang modify 20141010
	SAlarmEvent sIPCCover;
	SAlarmEvent sIPCExtSensors;
	SAlarmEvent s485ExtSensors;
	SAlarmEvent sHDDLost;	//硬盘丢失
	SAlarmEvent sHDDErr;	//硬盘读写出错
	SAlarmEvent sHDDNone;	//开机检测无硬盘
	SAlarmEvent sVMotions;
	SAlarmEvent sVBlinds;
	u32 nVBlindLuma;
	SAlarmEvent sVLosts;
	
	SAlarmOut sAlarmOut;
	SAlarmBuzz sAlarmBuzz;
	
	FNALARMCB pfnAlarmCb; //报警事件触发回调函数
	
	u8 nModWorking;
	u8* nPtzTourId;//触发过的某通道的巡航号，防止重复触发
	u8* nChnTouchPtzTour;//哪个通道触发了上面的巡航号,跟上面nPtzTourId配对使用
	
	SMsgCmdHdr sMsgCmdHdr;
	pthread_t thrdCmd;
//yaogang modify 20141202
	char *devpath;
	int fd_485;
	pthread_t thrd_485rd;
	
	//yaogang modify 20150324 
	//跃天: 1 nvr，2 轮巡解码器，3 切换解码器
	u8 nNVROrDecoder;
} SAlarmManager;
static pthread_mutex_t lock485;


s32 DoAlarmInit(SAlarmInitPara* psAlarmInitPara, PARAOUT SAlarmManager *psAlarmManager);
s32 CreateAlarmChn(SAlarmChn **psAlarmChn, u8 nNum);

void* AlarmCmdDealFxn(void *arg);
void* AlarmCheckEventFxn(void *arg);
void* yt485Fxn(void *arg);


void DoAlarmMsgCmd(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager);
void DealEventInPara(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager);
void DealAlarmOutPara(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager);
void DealAlarmBuzzPara(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager);
void DealEventInSchedule(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager);
void DealAlarmOutSchedule(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager);
void DealEventInDispatch(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager);

void CheckAlarmEvent(SAlarmManager *psAlarmManager);
void sendto_485ext_board(EMALARMEVENT emAlarmEvent, u8 chn, int fd485);

void CheckAlarm485(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void CheckAlarmIPCExtSensors(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void CheckAlarmIPCCover(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void CheckAlarmDiskNone(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void CheckAlarmDiskErr(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void CheckAlarmDiskLost(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void CheckAlarmSensors(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void CheckAlarmVlosts(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void CheckAlarmVBlinds(SAlarmEvent *psAlarmEvent, u32 lumaLevel, FNALARMCB pfnAlarmCb);
void CheckAlarmVMotion(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485);
void RefreshStatus(u8 nEnable, u8 nType, u8 nDelay, u8 nStatus, SAlarmChn *psAlarmChn);
void RefreshStatus_unSchedule(u8 nEnable, u8 nType, u8 nDelay, u8 nStatus, SAlarmChn *psAlarmChn);

void setmd(u8 chn, SAlarmVMotionPara *psAlarmVMotionPara, u8 *pnMdLevel);
u8 CalStatus(u8 type, u8 st);

void AlarmDispatch(SAlarmManager *psAlarmManager);

void BuzzDispatch(SAlarmManager *psAlarmManager);
u8 CheckEventDispatchBuzz(SAlarmEvent *psAlarmEvent);

void AlarmOutDispatch(SAlarmManager *psAlarmManager);
u8 CheckEventDispatchAlarmOut(SAlarmEvent *psAlarmEvent, u8 id);
void AlarmEventCallback(SAlarmChn *psAlarmChn, FNALARMCB pfnAlarmCb, EMALARMEVENT emAlarmEvent, u8 chn, int fd485);

void RecordDispatch(SAlarmManager *psAlarmManager);
u8 CheckEventDispatchRecord(SAlarmEvent *psAlarmEvent, u8 id);
void RecordDispatchCallback(u8 nStatus, FNALARMCB pfnAlarmCb, EMALARMEVENT emAlarmEvent, u8 chn);

void PtzDispatch(SAlarmManager *psAlarmManager);
void EventDispatchPtz(SAlarmManager *psAlarmManager, SAlarmEvent *psAlarmEvent,  FNALARMCB pfnAlarmCb, u8* pLastStatus);

void ZoomDispatch(SAlarmManager *psAlarmManager);
void EventDispatchZoom(SAlarmEvent *psAlarmEvent,  FNALARMCB pfnAlarmCb, u8* pLastStatus);

static u8 CheckHardDiskExist(void);

#define PRINT_DEBUG printf("File:%s, Func:%s, Line:%d\n", __FILE__,__FUNCTION__,__LINE__);

extern int OpenDev(char *Dev);
extern int set_speed(int fd, int speed);
extern int set_Parity(int fd, int databits, int stopbits, int parity);


s32 ModAlarmInit(SAlarmInitPara* psAlarmInitPara, PARAOUT AlaHdr* pAlaHdr)
{
	static u8 nInitFlag = 0;
	
	if(nInitFlag)
	{
		return -1;
	}
	
	nInitFlag = 1;//置初始化状态为1
	
	printf("%s start...\n", __FUNCTION__);
	
	if(NULL == pAlaHdr || NULL == psAlarmInitPara)//判断参数
	{
		nInitFlag = 0;
		return -1;
	}
	
	SAlarmManager *psAlarmManager = calloc(1, sizeof(SAlarmManager));
	if(NULL == psAlarmManager)
	{
		nInitFlag = 0;
		return -1;
	}
	memset(psAlarmManager,0,sizeof(SAlarmManager));//csp modify
	
	if(-1 == DoAlarmInit(psAlarmInitPara, psAlarmManager))
	{
		ModAlarmDeinit((AlaHdr)psAlarmManager);
		nInitFlag = 0;
		return -1;
	}
	//yaogang modify 20141202
	pthread_mutex_init(&ext485info.lock, NULL);
	ext485info.status = 0;

	psAlarmManager->fd_485 = -1;
	psAlarmManager->devpath =	"/dev/ttyAMA1";
#if 1
	pthread_mutex_init(&lock485, NULL);
	int fd = OpenDev(psAlarmManager->devpath);

	if(fd < 0)
	{
		printf("%s :OpenDev %s failed!\n", __FUNCTION__, psAlarmManager->devpath);
		return -1;
	}
	if(set_speed(fd, EM_PTZ_BAUDRATE_9600) == -1)
	{
		printf("Set speed Error! function:%s\n", __FUNCTION__);
		close(fd);
		ModAlarmDeinit((AlaHdr)psAlarmManager);
		return -1;
	}
	if(set_Parity(fd, EM_PTZ_DATABIT_8, EM_PTZ_STOPBIT_1, 'N') == -1)
	{
		printf("Set Parity Error! function:%s\n", __FUNCTION__);
		close(fd);
		ModAlarmDeinit((AlaHdr)psAlarmManager);
		return -1;
	}
	
	tl_rs485_ctl(0);//485RTSN  读

	psAlarmManager->fd_485 = fd;
	printf("ModAlarmInit fd485: %d\n", fd);
#endif	
	int ret;
	ret = pthread_create(&psAlarmManager->thrdCmd, NULL, AlarmCmdDealFxn, psAlarmManager);
	if(ret != 0)
	{
		ModAlarmDeinit((AlaHdr)psAlarmManager);
		printf("pthread create failed\n");
		nInitFlag = 0;
		return ret;
	}
	
	ret = pthread_create(&psAlarmManager->thrdCmd, NULL, AlarmCheckEventFxn, psAlarmManager);
	if(ret != 0)
	{
		ModAlarmDeinit((AlaHdr)psAlarmManager);
		printf("pthread create failed\n");
		nInitFlag = 0;
		return ret;
	}
	
    	*pAlaHdr = (AlaHdr)psAlarmManager;
//yaogang modify 20141202
	
	ret = pthread_create(&psAlarmManager->thrd_485rd, NULL, yt485Fxn, psAlarmManager);
	if(ret != 0)
	{
		ModAlarmDeinit((AlaHdr)psAlarmManager);
		printf("%s pthread create failed line: %d\n", __FUNCTION__, __LINE__);
		nInitFlag = 0;
		return ret;
	}
	
	
	return 0;
}

s32 DoAlarmInit(SAlarmInitPara* psAlarmInitPara, PARAOUT SAlarmManager* psAlarmManager)
{
	if(NULL == psAlarmInitPara || NULL == psAlarmManager)
	{
		return -1;
	}
	
	memset((void *)psAlarmManager, 0, sizeof(SAlarmManager));
	
	psAlarmManager->sSensors.nChannels = psAlarmInitPara->nAlarmSensorNum;
	//printf("%s sSensors.nChannels: %d\n", __func__, psAlarmManager->sSensors.nChannels);
	psAlarmManager->sIPCExtSensors.nChannels = psAlarmInitPara->nVideoChnNum;
	psAlarmManager->sIPCCover.nChannels = psAlarmInitPara->nVideoChnNum;
	psAlarmManager->s485ExtSensors.nChannels = psAlarmInitPara->nVideoChnNum;
	psAlarmManager->sHDDLost.nChannels = psAlarmInitPara->nDiskNum;
	psAlarmManager->sHDDErr.nChannels = psAlarmInitPara->nDiskNum;
	psAlarmManager->nNVROrDecoder= psAlarmInitPara->nNVROrDecoder;
	
	psAlarmManager->sHDDNone.nChannels = 1;

	//printf("yg DoAlarmInit psAlarmManager->nNVROrDecoder: %d\n", psAlarmManager->nNVROrDecoder);
	//printf("yg DoAlarmInit sHDDLost.nChannels: %d\n", psAlarmManager->sHDDLost.nChannels);
	
	
	psAlarmManager->sAlarmOut.nChannels = psAlarmInitPara->nAlarmOutNum;
	psAlarmManager->sAlarmBuzz.nChannels = psAlarmInitPara->nBuzzNum;
	psAlarmManager->sVMotions.nChannels = psAlarmInitPara->nVideoChnNum;
	psAlarmManager->sVBlinds.nChannels = 0;//psAlarmInitPara->nVideoChnNum;
	psAlarmManager->sVLosts.nChannels = psAlarmInitPara->nVideoChnNum;
	psAlarmManager->pfnAlarmCb = psAlarmInitPara->pfnAlarmCb;
	psAlarmManager->nVBlindLuma = psAlarmInitPara->nVBlindLuma;
	if(0 == psAlarmManager->nVBlindLuma)
	{
		psAlarmManager->nVBlindLuma = 0x00f00000;
	}
	
	psAlarmManager->sMsgCmdHdr = CreateMsgCmd(sizeof(SAlarmMsgHeader));
	if(NULL == psAlarmManager->sMsgCmdHdr)
	{
		return -1;
	}
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	
	//传感器
	SAlarmEvent *psAlarmEvent = &psAlarmManager->sSensors;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	//yaogang modify 20141010
	//IPCEXT传感器
	psAlarmEvent = &psAlarmManager->sIPCExtSensors;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//IPCCover
	psAlarmEvent = &psAlarmManager->sIPCCover;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//485EXT传感器
	psAlarmEvent = &psAlarmManager->s485ExtSensors;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//HDD Lost报警器
	psAlarmEvent = &psAlarmManager->sHDDLost;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//HDD IO Err报警器
	psAlarmEvent = &psAlarmManager->sHDDErr;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//HDD None报警器开机检测无硬盘
	psAlarmEvent = &psAlarmManager->sHDDNone;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//移动侦测
	psAlarmEvent = &psAlarmManager->sVMotions;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	
	//视频遮挡
	psAlarmEvent = &psAlarmManager->sVBlinds;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	
	//视频丢失
	psAlarmEvent = &psAlarmManager->sVLosts;
	if(-1 == CreateAlarmChn(&psAlarmEvent->psAlarmChn, psAlarmEvent->nChannels))
	{
		return -1;
	}
	//csp modify 20130326
	//#if defined(CHIP_HISI3531)// || defined(CHIP_HISI3521)
	//int ch = 0;
	//for(ch=0;ch<psAlarmEvent->nChannels;ch++)
	//{
	//	psAlarmEvent->psAlarmChn[ch].sEvent.nStatus = 2;
	//}
	//#endif
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	
	//报警输出
	SAlarmOut *psAlarmOut = &psAlarmManager->sAlarmOut;
	psAlarmOut->psAlarmOutChn = (SAlarmOutChn *)malloc(sizeof(SAlarmOutChn) * psAlarmOut->nChannels);
	if(NULL == psAlarmOut->psAlarmOutChn)
	{
		return -1;
	}
	memset(psAlarmOut->psAlarmOutChn, 0, sizeof(SAlarmOutChn) * psAlarmOut->nChannels);
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	
	//蜂鸣器输出
	SAlarmBuzz *psAlarmBuzz = &psAlarmManager->sAlarmBuzz;
	psAlarmBuzz->psAlarmBuzzChn = (SAlarmBuzzChn *)malloc(sizeof(SAlarmBuzzChn) * psAlarmBuzz->nChannels);
	if(NULL == psAlarmBuzz->psAlarmBuzzChn)
	{
		return -1;
	}
	memset(psAlarmBuzz->psAlarmBuzzChn, 0, sizeof(SAlarmBuzzChn) * psAlarmBuzz->nChannels);
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	
	psAlarmManager->nPtzTourId = malloc(sizeof(u8) * psAlarmInitPara->nVideoChnNum);
	memset(psAlarmManager->nPtzTourId, 0xff, sizeof(u8) * psAlarmInitPara->nVideoChnNum);
	psAlarmManager->nChnTouchPtzTour = malloc(sizeof(u8) * psAlarmInitPara->nVideoChnNum);
	memset(psAlarmManager->nChnTouchPtzTour, 0xff, sizeof(u8) * psAlarmInitPara->nVideoChnNum);
	
	return 0;
}

s32 CreateAlarmChn(SAlarmChn** psAlarmChn, u8 nNum)
{
	if(psAlarmChn)//if (psAlarmChn && nNum >= 0)//csp modify
	{
		if (nNum)
		{
			*psAlarmChn = (SAlarmChn *)malloc(sizeof(SAlarmChn) * nNum);
			if(NULL == *psAlarmChn)
			{
				return -1;
			}
			memset(*psAlarmChn, 0, sizeof(SAlarmChn) * nNum);
		}
		return 0;
	}
	else
	{
		return -1;
	}
}

s32 ModAlarmDeinit(AlaHdr AlaHdr)
{
	if(AlaHdr)
	{
		SAlarmManager* psAlarmManager = (SAlarmManager*)AlaHdr;
		if(psAlarmManager->sMsgCmdHdr)
		{
			DestroyMsgCmd(psAlarmManager->sMsgCmdHdr);
		}
		
		SAlarmEvent *psAlarmEvent = &psAlarmManager->sSensors;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}
		//yaogang modify 20141010
		psAlarmEvent = &psAlarmManager->sIPCExtSensors;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}
		
		psAlarmEvent = &psAlarmManager->sIPCCover;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}
		
		psAlarmEvent = &psAlarmManager->s485ExtSensors;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}
		psAlarmEvent = &psAlarmManager->sHDDLost;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}

		psAlarmEvent = &psAlarmManager->sHDDErr;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}

		psAlarmEvent = &psAlarmManager->sHDDNone;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}
		
		psAlarmEvent = &psAlarmManager->sVMotions;
		if (psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}
		
		psAlarmEvent = &psAlarmManager->sVBlinds;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}
		
		psAlarmEvent = &psAlarmManager->sVLosts;
		if(psAlarmEvent->psAlarmChn)
		{
			free(psAlarmEvent->psAlarmChn);
			psAlarmEvent->psAlarmChn = NULL;
		}
		
		if(psAlarmManager->sAlarmOut.psAlarmOutChn)
		{
			free(psAlarmManager->sAlarmOut.psAlarmOutChn);
			psAlarmManager->sAlarmOut.psAlarmOutChn = NULL;
		}
		
		if(psAlarmManager->sAlarmBuzz.psAlarmBuzzChn)
		{
			free(psAlarmManager->sAlarmBuzz.psAlarmBuzzChn);
			psAlarmManager->sAlarmBuzz.psAlarmBuzzChn = NULL;
		}
		
		free(AlaHdr);
		
		return 0;
	}
	
	return -1;
}

s32 ModAlarmSetParam(AlaHdr AlaHdr, EMALARMPARATYPE emAlarmParaType, u8 id, const SAlarmPara* psAlarmPara)
{
	SAlarmManager* psAlarmManager = (SAlarmManager*)AlaHdr;
	SAlarmMsgHeader sAlaMsgHd;
	
	if(NULL == AlaHdr)
	{
	    return -1;
	}
	
	sAlaMsgHd.emMsg = emAlarmParaType;
	sAlaMsgHd.emGetSet = EM_ALARM_PARA_SET;
	sAlaMsgHd.nChn = id;
	sAlaMsgHd.psAlaPara = (SAlarmPara*)psAlarmPara;
	
#if 1
	DoAlarmMsgCmd(&sAlaMsgHd, psAlarmManager);
#else
	if(WriteMsgCmd(psAlarmManager->sMsgCmdHdr, &sAlaMsgHd))
	{
	    return -1;
	}
#endif
	
	return 0;
}

s32 ModAlarmGetParam(AlaHdr AlaHdr, EMALARMPARATYPE emAlarmParaType, u8 id, SAlarmPara* psAlarmPara)
{
	SAlarmManager* psAlarmManager = (SAlarmManager*)AlaHdr;
	SAlarmMsgHeader sAlaMsgHd;
	
	if(NULL == AlaHdr)
	{
		return -1;
	}
	
	sAlaMsgHd.emMsg = emAlarmParaType;
	sAlaMsgHd.emGetSet = EM_ALARM_PARA_GET;
	sAlaMsgHd.nChn = id;
	sAlaMsgHd.psAlaPara = psAlarmPara;
	if(WriteMsgCmd(psAlarmManager->sMsgCmdHdr, &sAlaMsgHd))
	{
		return -1;
	}
	
	return 0;
}

s32 ModAlarmWorkingEnable(AlaHdr AlaHdr, u8 nEnable)
{
	if(NULL == AlaHdr)
	{
		return -1;
	}
	
	SAlarmManager* psAlarmManager = (SAlarmManager*)AlaHdr;
	psAlarmManager->nModWorking = nEnable ? 1 : 0;
	
	return 0;
}

void* AlarmCmdDealFxn(void *arg)
{
	//参数设置及模块管理相关
	SAlarmManager* psAlarmManager = NULL;
	SMsgCmdHdr pAlaMsgHdr = NULL;
	SAlarmMsgHeader sAlaMsgHead;
	u8 IsLoopNull = 0;
	
	psAlarmManager = (SAlarmManager*)arg;
	if(NULL == psAlarmManager)
	{
		return NULL;
	}
	
	pAlaMsgHdr = psAlarmManager->sMsgCmdHdr;
	if(NULL == pAlaMsgHdr)
	{
		return NULL;
	}
	
#if defined(CHIP_HISI3521)//#if defined(CHIP_HISI3531) || defined(CHIP_HISI3521)//csp modify
	//tl_set_alarm_out(0,0);
#endif
	
	//int x = tl_md_open(); //移动侦测驱动支持
	//printf("rz_md_open return %d\n", x);
	
	printf("$$$$$$$$$$$$$$$$$$AlarmCmdDealFxn id:%d\n",getpid());
	
	while(1)
	{
		IsLoopNull = 1;
		
		//读并处理新的参数命令
		if(0 == ReadMsgCmd(pAlaMsgHdr, &sAlaMsgHead))
		{
			printf("%s emMsg: %d\n", __func__, sAlaMsgHead.emMsg);
			//处理命令
			DoAlarmMsgCmd(&sAlaMsgHead, psAlarmManager);
			
			AckMsgCmd(pAlaMsgHdr); //确认命令处理完成
			
			IsLoopNull = 0;
			
			//printf("%s: %d\n", __FUNCTION__, __LINE__);
		}
		
		//printf("cw&*&*&*&**&*8%s: %d\n", __FUNCTION__, __LINE__);
		//报警事件侦听
		//CheckAlarmEvent(psAlarmManager);
		//printf("%s: %d\n", __FUNCTION__, __LINE__);
		
		//if(psAlarmManager->nModWorking)//csp modify
		{
			//报警触发处理
			AlarmDispatch(psAlarmManager);
			//printf("%s: %d\n", __FUNCTION__, __LINE__);
		}
		
		if(IsLoopNull)
		{
			usleep(500 * 1000);
		}
	}
	
	return NULL;
}
//yaogang modify 20141202
static int ReadOneFrame(int fd, u8 *pbuf, int len)
{
	int ret;
#if 0
	while (cnt != len)
	{
		ret = read(fd, pbuf+cnt, len-cnt);
		if (ret < 0)
		{
			printf("%s read failed cnt: %d\n", __FUNCTION__, cnt);
			return -1;
		}
		cnt += ret;
	}
#endif
	int i;
	for (i=0; i<len; i++)
	{
		ret = read(fd, pbuf+i, 1);
		if (ret < 0)
		{
			printf("%s read failed i: %d\n", __FUNCTION__, i);
			return -1;
		}
		printf("read 485board buf[%d] = 0x%x\n", i, pbuf[i]);
	}
	return len;
}
static int ReadByte(int fd, u8 *pbuf, int len)
{
	int ret;
	fd_set r;
	struct timeval t;
	
	t.tv_sec = 0;
	t.tv_usec = 10*1000;//10ms;
	
	FD_ZERO(&r);
	FD_SET(fd, &r);

	pthread_mutex_lock(&lock485);
	ret = select(fd + 1, &r, NULL, NULL, &t);
	if(ret == 0)
	{
		//printf("%s select() timeout\n", __FUNCTION__);
	}
	else if(ret < 0)
	{
		printf("%s select() failed\n", __FUNCTION__);
	}
	else
	{
		ret = read(fd, pbuf, len);
		if (ret < 0)
		{
			printf("%s read() failed\n", __FUNCTION__);
		}
		else
		{
			printf("%s reallen: %d, read(): 0x%x\n", __FUNCTION__, ret, pbuf[0]);
		}
	}
	pthread_mutex_unlock(&lock485);

	return ret;
}

#define TIMEOUT485	5	//收到0xBD后，5秒内要接收完一帧7字节
void* yt485Fxn(void *arg)
{
	//参数设置及模块管理相关
	SAlarmManager* psAlarmManager = NULL;
	psAlarmManager = (SAlarmManager*)arg;
	u8 template_buf[7] = {0xBD, 0xEE, 0xEE, 0xC, 0, 0xDD, 0xFF};//0xC -12 - 485网络上传报警号
	/*
	防区报警码 BD EE EE AA XX DD FF
	AA: 报警源的识别码
	XX：报警源的通道号
	其他5位固定不变
	*/
	u8 buf[7];
	u8 alarm_chn;

	printf("yt485Fxn run1\n");
	
	if(NULL == psAlarmManager)
	{
		return NULL;
	}

	int fd = psAlarmManager->fd_485;
	if (-1 == fd)
	{
		printf("%s 485fd invalid\n", __FUNCTION__);
		return NULL;
	}
	printf("yt485Fxn run2\n");

	u8 status = 0;
	u8 cnt = 0;
	int ret;
	time_t start = time(NULL);

	while (1)
	{
		//printf("%s status: %d, cnt: %d\n", __FUNCTION__, status, cnt);
		switch (status)
		{
			case 0://等待开始
			{
				cnt = 0;
				ret = ReadByte(fd, buf, sizeof(buf));
				if (ret > 0)
				{
					if (buf[0] == 0xBD)
					{
						status = 1;//读一帧数据
						cnt += ret;
						start = time(NULL);
					}
					else
					{
						printf("%s first byte != 0xBD\n", __FUNCTION__);
					}
				}
				sleep(1);
			} break;
			case 1://读一帧
			{
				if (cnt >= sizeof(buf))//recv over
				{
					status = 2;
				}
				else
				{
					ret = ReadByte(fd, buf+cnt, sizeof(buf)-cnt);
					if (ret > 0)
					{
						cnt += ret;
					}
					if(  (time(NULL) - start) >= TIMEOUT485)
					{
						printf("%s recv 1 frame timeout\n", __FUNCTION__);
						status = 0;
						cnt = 0;
					}
				}
				usleep(1000);//1ms
			} break;
			case 2://处理
			{
				template_buf[4] = buf[4];
				if (memcmp(template_buf, buf, sizeof(buf)) != 0)
				{
					printf("%s : recv frame invalid\n", __FUNCTION__);
					int i;
					for (i=0; i<sizeof(buf); i++)
					{
						printf("\t buf[%d]: 0x%x\n", i, buf[i]);
					}
					
				}
				else
				{
					alarm_chn = buf[4];
					if (alarm_chn > psAlarmManager->s485ExtSensors.nChannels)
					{
						printf("%s : chn = %d invalid\n", __FUNCTION__, alarm_chn);
						
					}
					else
					{
						printf("%s : 485extboard chn%d alarm\n", __FUNCTION__, alarm_chn);
						pthread_mutex_lock(&ext485info.lock);
						ext485info.status |= 1<< alarm_chn;
						pthread_mutex_unlock(&ext485info.lock);
					}
				}
				status = 0;
				cnt = 0;
			} break;
			default:
				printf("%s status invalid\n", __FUNCTION__);
		}
		

#if 0	
		ret = ReadOneFrame(fd, buf, sizeof(buf));
		if (ret != sizeof(buf))
		{
			printf("%s ReadOneFrame failed ret: %d\n", __FUNCTION__, ret);
			return NULL;
		}
		
		//template_buf cmp
		template_buf[4] = buf[4];
		if (memcmp(template_buf, buf, sizeof(buf)) != 0)
		{
			printf("%s : recv frame invalid\n", __FUNCTION__);
			int i;
			for (i=0; i<ret; i++)
			{
				printf("\t buf[%d]: 0x%x\n", i, buf[i]);
			}
			continue;
		}
		
		alarm_chn = buf[4];
		if (alarm_chn > psAlarmManager->s485ExtSensors.nChannels)
		{
			printf("%s : chn %d invalid\n", __FUNCTION__, alarm_chn);
			continue;
		}
		
		printf("%s : 485extboard chn %d alarm\n", __FUNCTION__, alarm_chn);
		pthread_mutex_lock(&ext485info.lock);
		ext485info.status |= 1<< alarm_chn;
		pthread_mutex_unlock(&ext485info.lock);
#endif
	}
}

void* AlarmCheckEventFxn(void *arg)
{
	printf("$$$$$$$$$$$$$$$$$$AlarmCheckEventFxn id:%d\n",getpid());
	
    //参数设置及模块管理相关
    SAlarmManager* psAlarmManager = NULL;
    psAlarmManager = (SAlarmManager*)arg;
    if(NULL == psAlarmManager)
    {
        return NULL;
    }
	
	//usleep(10* 1000* 1000);
	
#if defined(CHIP_HISI3531) || defined(CHIP_HISI3521)
	//tl_VI_SetUserPic("./lost.yuv", 704, 576, 704);
	//////tl_VI_SetUserPic("./novideo.yuv", 704, 576, 704);//csp modify 20140525
#endif
	
	//csp modify
	//usleep(5* 1000* 1000);
	
	int x = tl_md_open();//移动侦测驱动支持
	//csp modify
	//printf("tl_md_open return %d\n", x);
	printf("rz_md_open result:%d\n", x);
	
	//csp modify 20130326
#if defined(CHIP_HISI3531)// || defined(CHIP_HISI3521)
	if(psAlarmManager->pfnAlarmCb)
	{
		u32 nEventSatus = tl_video_connection_status();
		SAlarmCbData sAlarmCbData;
		sAlarmCbData.emAlarmEvent = EM_ALARM_EVENT_CTRL_CHN_LED;
		sAlarmCbData.nChn = 0;
		sAlarmCbData.nData = 0;
		sAlarmCbData.nTime = time(NULL);
		sAlarmCbData.reserved[0] = nEventSatus;
		psAlarmManager->pfnAlarmCb(&sAlarmCbData);
	}
#endif
	
    while(1)
    {
		#if 1//csp modify
		usleep(150 * 1000);
		#else
		usleep(100 * 1000);
		#endif
		
		if(!psAlarmManager->nModWorking)
		{
			continue;
		}
		
		//printf("CheckAlarmEvent at:%u\n",SystemGetMSCount());
		
		//报警事件侦听
        CheckAlarmEvent(psAlarmManager);
        //printf("%s: %d\n", __FUNCTION__, __LINE__);
    }
	
    return NULL;
}

void DoAlarmMsgCmd(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager)
{
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	//dealcmd
	switch(psMsgHead->emMsg)
	{
	case EM_ALARM_PARA_SENSOR: //报警事件参数设�
	case EM_ALARM_PARA_IPCCOVER:
	case EM_ALARM_PARA_IPCEXTSENSOR://yaogang modify 20141010
	case EM_ALARM_PARA_485EXTSENSOR:
	case EM_ALARM_PARA_HDD:
	case EM_ALARM_PARA_VMOTION:
	case EM_ALARM_PARA_VBLIND:
	case EM_ALARM_PARA_VLOST:
		DealEventInPara(psMsgHead, psAlarmManager);
		break;
	case EM_ALARM_PARA_ALARMOUT: //报警输出参数设置
		DealAlarmOutPara(psMsgHead, psAlarmManager);
		break;
	case EM_ALARM_PARA_BUZZ: //蜂鸣器参数设置
		DealAlarmBuzzPara(psMsgHead, psAlarmManager);
		break;
	case EM_ALARM_SCHEDULE_SENSOR: //报警事件布防
	case EM_ALARM_SCHEDULE_IPCEXTSENSOR:
	case EM_ALARM_SCHEDULE_IPCCOVER:
	case EM_ALARM_SCHEDULE_VMOTION:
	case EM_ALARM_SCHEDULE_VBLIND:
	case EM_ALARM_SCHEDULE_VLOST:
		DealEventInSchedule(psMsgHead, psAlarmManager);
		break;                 
	case EM_ALARM_SCHEDULE_ALARMOUT: //报警输出布防
		DealAlarmOutSchedule(psMsgHead, psAlarmManager);
		break;
	case EM_ALARM_SCHEDULE_BUZZ: //蜂鸣器布防；暂不支持
		break;
	case EM_ALARM_DISPATCH_SENSOR: //报警事件触发处理
	case EM_ALARM_DISPATCH_IPCEXTSENSOR://yaogang modify 20141010
	case EM_ALARM_DISPATCH_IPCCOVER:
	case EM_ALARM_DISPATCH_485EXTSENSOR:
	case EM_ALARM_DISPATCH_HDD:
	case EM_ALARM_DISPATCH_VMOTION:
	case EM_ALARM_DISPATCH_VBLIND:
	case EM_ALARM_DISPATCH_VLOST:
		DealEventInDispatch(psMsgHead, psAlarmManager);
		break;
	default:
		{
			printf("%s:%d\n", __FUNCTION__, __LINE__);
			break;
		}
	}
}

//设置报警事件参数
void DealEventInPara(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager)
{
	SAlarmEvent *psAlarmEvent = NULL;
	
	switch(psMsgHead->emMsg)
	{
		case EM_ALARM_PARA_SENSOR:
			psAlarmEvent = &psAlarmManager->sSensors;
		//yaogang modify 20141010
		case EM_ALARM_PARA_IPCCOVER:
			if(NULL == psAlarmEvent)
			{
				psAlarmEvent = &psAlarmManager->sIPCCover;
			}
		case EM_ALARM_PARA_IPCEXTSENSOR:
			if(NULL == psAlarmEvent)
			{
				psAlarmEvent = &psAlarmManager->sIPCExtSensors;
			}
		case EM_ALARM_PARA_485EXTSENSOR:
			if(NULL == psAlarmEvent)
			{
				psAlarmEvent = &psAlarmManager->s485ExtSensors;
			}
		case EM_ALARM_PARA_HDD:
			if(NULL == psAlarmEvent)
			{
				psAlarmEvent = &psAlarmManager->sHDDLost;
				printf("yg psMsgHead->nChn: %d\n", psMsgHead->nChn);
			}
		case EM_ALARM_PARA_VMOTION:
			if(NULL == psAlarmEvent)
			{
				psAlarmEvent = &psAlarmManager->sVMotions;
				if(EM_ALMARM_VMOTION_AREA_SELECTALL == psMsgHead->psAlaPara->sAlaVMotionPara.emSetType)
				{
					memset((void *)psMsgHead->psAlaPara->sAlaVMotionPara.nBlockStatus, 0xff
						, sizeof(u64) * sizeof(psMsgHead->psAlaPara->sAlaVMotionPara.nBlockStatus));
				}
				else if(EM_ALMARM_VMOTION_AREA_CLEAR == psMsgHead->psAlaPara->sAlaVMotionPara.emSetType)
				{
					memset((void *)psMsgHead->psAlaPara->sAlaVMotionPara.nBlockStatus, 0x0
						, sizeof(u64) * sizeof(psMsgHead->psAlaPara->sAlaVMotionPara.nBlockStatus));
				}
			}
		case EM_ALARM_PARA_VBLIND:
			if(NULL == psAlarmEvent)
			{
				psAlarmEvent = &psAlarmManager->sVBlinds;
			}
		case EM_ALARM_PARA_VLOST:
			if(NULL == psAlarmEvent)
			{
				psAlarmEvent = &psAlarmManager->sVLosts;
			}
			
			//
			if(psMsgHead->nChn < psAlarmEvent->nChannels)
			{
				if(EM_ALARM_PARA_SET == psMsgHead->emGetSet)
				{
					
					psAlarmEvent->psAlarmChn[psMsgHead->nChn].nSetChangeTimes++;
					memcpy(&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmPara,
						psMsgHead->psAlaPara, sizeof(SAlarmPara));
					//硬盘参数共用，做单独处理
					if (EM_ALARM_PARA_HDD == psMsgHead->emMsg)
					{
						psAlarmEvent = &psAlarmManager->sHDDErr;
						psAlarmEvent->psAlarmChn[psMsgHead->nChn].nSetChangeTimes++;
						memcpy(&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmPara,
							psMsgHead->psAlaPara, sizeof(SAlarmPara));

						if (psMsgHead->nChn == 0)
						{
							psAlarmEvent = &psAlarmManager->sHDDNone;
							psAlarmEvent->psAlarmChn[psMsgHead->nChn].nSetChangeTimes++;
							memcpy(&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmPara,
								psMsgHead->psAlaPara, sizeof(SAlarmPara));
						}
					}
					
				}
				else if(EM_ALARM_PARA_GET == psMsgHead->emGetSet)
				{
					memcpy(psMsgHead->psAlaPara,
						&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmPara, sizeof(SAlarmPara));
					//硬盘参数共用，做单独处理
					if (EM_ALARM_PARA_HDD == psMsgHead->emMsg)
					{
						psAlarmEvent = &psAlarmManager->sHDDErr;
						memcpy(psMsgHead->psAlaPara,
						&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmPara, sizeof(SAlarmPara));

						if (psMsgHead->nChn == 0)
						{
							psAlarmEvent = &psAlarmManager->sHDDNone;
							memcpy(psMsgHead->psAlaPara,
						&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmPara, sizeof(SAlarmPara));
						}
					}
				}
			}
			//printf("%s:%d\n", __FUNCTION__, __LINE__);
			break;
		default:
			break;
	}
}

//设置报警输出参数
void DealAlarmOutPara(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager)
{
    SAlarmOut *psAlarmOut = &psAlarmManager->sAlarmOut;
    if (psMsgHead->nChn < psAlarmOut->nChannels)
    {
        if(EM_ALARM_PARA_SET == psMsgHead->emGetSet)
        {
            memcpy(&psAlarmOut->psAlarmOutChn[psMsgHead->nChn].sAlarmPara,
                    psMsgHead->psAlaPara, sizeof(SAlarmPara));
        }
        else if(EM_ALARM_PARA_GET == psMsgHead->emGetSet)
        {
            memcpy(psMsgHead->psAlaPara, &psAlarmOut->psAlarmOutChn[psMsgHead->nChn].sAlarmPara,
                     sizeof(SAlarmPara));
        }
    }
}

//设置蜂鸣器参数
void DealAlarmBuzzPara(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager)
{
    SAlarmBuzz *psAlarmBuzz = &psAlarmManager->sAlarmBuzz;
    if (psMsgHead->nChn < psAlarmBuzz->nChannels)
    {
        if(EM_ALARM_PARA_SET == psMsgHead->emGetSet)
        {
            memcpy(&psAlarmBuzz->psAlarmBuzzChn[psMsgHead->nChn].sAlarmPara,
                    psMsgHead->psAlaPara, sizeof(SAlarmPara));
        }
        else if(EM_ALARM_PARA_GET == psMsgHead->emGetSet)
        {
            memcpy(psMsgHead->psAlaPara, &psAlarmBuzz->psAlarmBuzzChn[psMsgHead->nChn].sAlarmPara,
                     sizeof(SAlarmPara));
        }
    }
}

//设置报警事件布防参数
void DealEventInSchedule(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager)
{
    SAlarmEvent *psAlarmEvent = NULL;

    switch(psMsgHead->emMsg)
    {
        case EM_ALARM_SCHEDULE_SENSOR:	
            psAlarmEvent = &psAlarmManager->sSensors;
	case EM_ALARM_SCHEDULE_IPCEXTSENSOR:
            if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sIPCExtSensors;
            }
	case EM_ALARM_SCHEDULE_IPCCOVER:
            if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sIPCCover;
            }
        case EM_ALARM_SCHEDULE_VMOTION:
            if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sVMotions;
            }
        case EM_ALARM_SCHEDULE_VBLIND:	
            if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sVBlinds;
            }
        case EM_ALARM_SCHEDULE_VLOST:
            if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sVLosts;
            }
            if (psMsgHead->nChn < psAlarmEvent->nChannels)
            {
                if(EM_ALARM_PARA_SET == psMsgHead->emGetSet)
                {
                    psAlarmEvent->psAlarmChn[psMsgHead->nChn].nSetChangeTimes++;
                    memcpy(&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmSchedule,
                        &psMsgHead->psAlaPara->sAlaSchedule, sizeof(SAlarmSchedule));
                }
                else if(EM_ALARM_PARA_GET == psMsgHead->emGetSet)
                {
                    memcpy(&psMsgHead->psAlaPara->sAlaSchedule,
                          &psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmSchedule, sizeof(SAlarmSchedule));
                }
            }
            break;
        default:
            break;
    }
}

//设置报警输出布防参数
void DealAlarmOutSchedule(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager)
{
    SAlarmOut *psAlarmOut = &psAlarmManager->sAlarmOut;
    if (psMsgHead->nChn < psAlarmOut->nChannels)
    {
        if(EM_ALARM_PARA_SET == psMsgHead->emGetSet)
        {
            memcpy(&psAlarmOut->psAlarmOutChn[psMsgHead->nChn].sAlarmSchedule,
                    &psMsgHead->psAlaPara->sAlaSchedule, sizeof(SAlarmSchedule));
        }
        else if(EM_ALARM_PARA_GET == psMsgHead->emGetSet)
        {
            memcpy(&psMsgHead->psAlaPara->sAlaSchedule,
                    &psAlarmOut->psAlarmOutChn[psMsgHead->nChn].sAlarmSchedule, sizeof(SAlarmSchedule));
        }
    }
}

//设置报警事件触发参数
void DealEventInDispatch(SAlarmMsgHeader* psMsgHead, PARAOUT SAlarmManager *psAlarmManager)
{
    SAlarmEvent *psAlarmEvent = NULL;
    
    switch(psMsgHead->emMsg)
    {
        case EM_ALARM_DISPATCH_SENSOR:
            psAlarmEvent = &psAlarmManager->sSensors;
	//yaogang modify 20141010
	case EM_ALARM_DISPATCH_IPCEXTSENSOR:
	    if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sIPCExtSensors;
            }
	case EM_ALARM_DISPATCH_IPCCOVER:
	    if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sIPCCover;
            }
	case EM_ALARM_DISPATCH_485EXTSENSOR:
	    if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->s485ExtSensors;
            }
	case EM_ALARM_DISPATCH_HDD:
	    if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sHDDLost;
            }
		
        case EM_ALARM_DISPATCH_VMOTION:
            if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sVMotions;
            }
        case EM_ALARM_DISPATCH_VBLIND:
            if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sVBlinds;
            }
        case EM_ALARM_DISPATCH_VLOST:
            if (NULL == psAlarmEvent)
            {
                psAlarmEvent = &psAlarmManager->sVLosts;
            }
            
            if (psMsgHead->nChn < psAlarmEvent->nChannels)
            {
                if(EM_ALARM_PARA_SET == psMsgHead->emGetSet)
                {
                    memcpy(&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmDispatch,
                        &psMsgHead->psAlaPara->sAlaDispatch, sizeof(SAlarmDispatch));

			//硬盘参数共用，做单独处理
			if (EM_ALARM_DISPATCH_HDD == psMsgHead->emMsg)
			{
				psAlarmEvent = &psAlarmManager->sHDDErr;
				memcpy(&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmDispatch,
                       			 &psMsgHead->psAlaPara->sAlaDispatch, sizeof(SAlarmDispatch));

				if (psMsgHead->nChn == 0)
				{
					psAlarmEvent = &psAlarmManager->sHDDNone;
					memcpy(&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmDispatch,
                        			&psMsgHead->psAlaPara->sAlaDispatch, sizeof(SAlarmDispatch));
				}
			}
			
                }
                else if(EM_ALARM_PARA_GET == psMsgHead->emGetSet)
                {
                    memcpy(&psMsgHead->psAlaPara->sAlaDispatch,
                        &psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmDispatch, sizeof(SAlarmDispatch));

			//硬盘参数共用，做单独处理
			if (EM_ALARM_DISPATCH_HDD == psMsgHead->emMsg)
			{
				psAlarmEvent = &psAlarmManager->sHDDErr;
				memcpy(&psMsgHead->psAlaPara->sAlaDispatch,
                       			&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmDispatch, sizeof(SAlarmDispatch));

				if (psMsgHead->nChn == 0)
				{
					psAlarmEvent = &psAlarmManager->sHDDNone;
					memcpy(&psMsgHead->psAlaPara->sAlaDispatch,
                        			&psAlarmEvent->psAlarmChn[psMsgHead->nChn].sAlarmDispatch, sizeof(SAlarmDispatch));
				}
			}
                }
            }
            break;
        default:
            break;
    }
}

void CheckAlarmEvent(SAlarmManager *psAlarmManager)
{
	SAlarmEvent *psAlarmEvent;
	
	//传感器报警检测
	psAlarmEvent = &psAlarmManager->sSensors;
	CheckAlarmSensors(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
	
	//IPC 传感器报警检测
	psAlarmEvent = &psAlarmManager->sIPCExtSensors;
	CheckAlarmIPCExtSensors(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
	
	//IPC 遮盖报警检测
	psAlarmEvent = &psAlarmManager->sIPCCover;
	CheckAlarmIPCCover(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
	
	//视频丢失检测
	#if 1//csp modify 20121222
	static unsigned char count = 0;
	if(++count == 3)
	{
		psAlarmEvent = &psAlarmManager->sVLosts;
		CheckAlarmVlosts(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
	}
	//开机检测无硬盘，有效检测其实只有一次，各种初始化 后
	if(count == 6)
	{
		psAlarmEvent = &psAlarmManager->sHDDNone;
		//yaogang modify 20150324 
		if (psAlarmManager->nNVROrDecoder == 1) //nvr
		{
			CheckAlarmDiskNone(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
		}
	}
	//485扩展板
	if(count == 7)
	{
		psAlarmEvent = &psAlarmManager->s485ExtSensors;
		CheckAlarm485(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
	}
	//检测硬盘丢失
	if(count == 9)
	{
		count = 0;
		
		psAlarmEvent = &psAlarmManager->sHDDLost;
		//yaogang modify 20150324 
		if (psAlarmManager->nNVROrDecoder == 1) //nvr
		{
			CheckAlarmDiskLost(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
		}
	}
	#endif
	//检测硬盘读写错误
	psAlarmEvent = &psAlarmManager->sHDDErr;
	//yaogang modify 20150324 
	if (psAlarmManager->nNVROrDecoder == 1) //nvr
	{
		CheckAlarmDiskErr(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
	}
	
	//视频遮挡检测
	//psAlarmEvent = &psAlarmManager->sVBlinds;
	//CheckAlarmVBlinds(psAlarmEvent, psAlarmManager->nVBlindLuma, psAlarmManager->pfnAlarmCb);
	
	//视频移动侦测检测
	psAlarmEvent = &psAlarmManager->sVMotions;
	CheckAlarmVMotion(psAlarmEvent, psAlarmManager->pfnAlarmCb, psAlarmManager->fd_485);
}

void AlarmEventCallback(SAlarmChn *psAlarmChn, FNALARMCB pfnAlarmCb, EMALARMEVENT emAlarmEvent, u8 chn, int fd485)
{
	//printf("%s:%d nStatusLast:%d nStatus:%d\n", __FUNCTION__, __LINE__, psAlarmChn->sEvent.nStatusLast, psAlarmChn->sEvent.nStatus);
	if(pfnAlarmCb && psAlarmChn->sEvent.nStatusLast != psAlarmChn->sEvent.nStatus)
	{
		SAlarmCbData sAlarmCbData;
		sAlarmCbData.emAlarmEvent = emAlarmEvent;
		sAlarmCbData.nChn = chn;
		sAlarmCbData.nData = psAlarmChn->sEvent.nStatus;
		sAlarmCbData.nTime = psAlarmChn->sEvent.nTime;
		pfnAlarmCb(&sAlarmCbData);
		
		//if(emAlarmEvent == EM_ALARM_EVENT_VMOTION && chn == 0)
		//{
			//printf("######chn%d motion notify status[%d]######\n",chn,psAlarmChn->sEvent.nStatus);
		//}
	}
	//yaogang modify 20141203 to YueTian 485Ext Board
	if ( (psAlarmChn->sEvent.nStatusLast != psAlarmChn->sEvent.nStatus) 
		&& psAlarmChn->sEvent.nStatus)//只在报警触发时发送
	{
		sendto_485ext_board(emAlarmEvent, chn, fd485);
	}

	#if 0
	//yaogang debug
	if ((EM_ALARM_EVENT_SENSOR == emAlarmEvent)
		&& (psAlarmChn->sEvent.nStatusLast != psAlarmChn->sEvent.nStatus) )
	{
		printf("%s sensor%d trigger\n", __func__, chn);
	}
	#endif
}

//yaogang modify 20141203 to YueTian 485Ext Board
//485扩展板
static u32 tl_get_alarm_485()
{
	u32 ret = 0;
	
	pthread_mutex_lock(&ext485info.lock);
	ret = ext485info.status;
	ext485info.status = 0;
	pthread_mutex_unlock(&ext485info.lock);

	return ret;
}
const u8 template_buf[7] = {0xBD, 0xEE, 0xEE, 0, 0, 0xDD, 0xFF};
void sendto_485ext_board(EMALARMEVENT emAlarmEvent, u8 chn, int fd485)
{
/*
0		//硬盘未格式化报警
1		//硬盘丢失报警
2		//硬盘读写出错报警
3		//硬盘满报警
4		//开机检测无硬盘
5		//信号量报警
6		//视频丢失报警
7		//移动侦测报警
8		//IPC遮盖报警
9		//制式不匹配报警
10		//非法访问报警
11		//IPC外部报警
12		//485扩展报警
*/
/*
防区报警码 BD EE EE AA XX DD FF
AA: 报警源的识别码
XX：报警源的通道号
其他5位固定不变
*/
#if 1

	return ;

#else
	u8 type = 255;
	u8 buf[7];

	memcpy(buf, template_buf, sizeof(buf));

	switch (emAlarmEvent)
	{
		case EM_ALARM_EVENT_SENSOR:	 //传感器报警事件
		{
			type = 5;
		} break;
		case EM_ALARM_EVENT_VLOST:		 //视频丢失报警事件
		{
			type =6 ;
		} break;
		case EM_ALARM_EVENT_VMOTION:		//移动侦测报警事件
		{
			type = 7;
		} break;
		case EM_ALARM_EVENT_IPCEXT:		//IPC外部传感器报警事件
		{
			type = 11;
		} break;
		case EM_ALARM_EVENT_IPCCOVER:		//IPC 遮盖
		{
			type = 8;
		} break;
		case EM_ALARM_EVENT_DISK_LOST:	//硬盘丢失
		{
			type = 1;
		} break;
		case EM_ALARM_EVENT_DISK_ERR:		//硬盘读写错误
		{
			type = 2;
		} break;
		case EM_ALARM_EVENT_DISK_NONE:	//开机检测无硬盘
		{
			type = 4;
		} break;
		case EM_ALARM_EVENT_485EXT:
		{
			//485扩展板的报警就不用回传给485扩展板了
		} break;
		default:
			printf("%s AlarmEvent invalid\n", __FUNCTION__);
	}
	
	if (type != 255)
	{
		buf[3] = type;
		buf[4] = chn;

		pthread_mutex_lock(&lock485);
		printf("485 fd: %d, send type: 0x%x, chn: 0x%x\n", fd485, type, chn);
		tl_rs485_ctl(1);//485RTSN  写
		usleep(20*1000);
		
		if (write(fd485, buf, sizeof(buf)) != sizeof(buf))
		{
			perror("sendto_485ext_board : ");
		}
		
			
		tl_rs485_ctl(0);//还原485RTSN  读
		usleep(20*1000);
		pthread_mutex_unlock(&lock485);
	}
#endif
}

void CheckAlarm485(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
	int i;
	u32 nEventSatus = 0;
	u8 nEnable, nDelay;

	nEventSatus = tl_get_alarm_485();
	if (nEventSatus)
		printf("Ext485 alarmin = 0x%04x\n", nEventSatus);
	//printf("%s:%d nChannels:%d\n", __FUNCTION__, __LINE__, psAlarmEvent->nChannels);
	for (i = 0;i < psAlarmEvent->nChannels; i++)
	{
		SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		nEnable = psAlarmChn->sAlarmPara.sAla485ExtSensorPara.nEnable;
		//EMALARMSENSORTYPE emType = psAlarmChn->sAlarmPara.sAlaSensorPara.emType;
		nDelay = psAlarmChn->sAlarmPara.sAla485ExtSensorPara.nDelay;
		psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
		//printf("chn[%d], emType = %d\n", i, emType);
		if ((nEventSatus >> i) & 1)
		{
			printf("yg 485Ext alarmin chn%d nEnable: %d, nDelay: %d\n", i, nEnable, nDelay);
		}
		RefreshStatus_unSchedule(nEnable, 1, nDelay, (nEventSatus >> i) & 1, psAlarmChn);
	    
	    	AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_485EXT, i, fd485);
	}
}

//#define SENSOR_DEMO
//IPC外部报警检测
void CheckAlarmIPCExtSensors(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
    int i;
    u32 nEventSatus = 0;
    u8 nEnable, nDelay;
	
    nEventSatus = tl_get_alarm_IPCExt();
	if (nEventSatus)
		printf("IPCExt alarmin = 0x%04x\n", nEventSatus);
    //printf("%s:%d nChannels:%d\n", __FUNCTION__, __LINE__, psAlarmEvent->nChannels);
    for (i = 0;i < psAlarmEvent->nChannels; i++)
    {
		SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		nEnable = psAlarmChn->sAlarmPara.sAlaIPCExtSensorPara.nEnable;
		//EMALARMSENSORTYPE emType = psAlarmChn->sAlarmPara.sAlaSensorPara.emType;
		nDelay = psAlarmChn->sAlarmPara.sAlaIPCExtSensorPara.nDelay;
		psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
		//printf("chn[%d], emType = %d\n", i, emType);
		if ((nEventSatus >> i) & 1)
		{
			printf("yg IPCExt alarmin chn%d nEnable: %d, nDelay: %d\n", i, nEnable, nDelay);
		}
		RefreshStatus(nEnable, 1, nDelay, (nEventSatus >> i) & 1, psAlarmChn);
        
        	AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_IPCEXT, i, fd485);
    }
    //printf("%s:%d\n", __FUNCTION__, __LINE__);
}

//yaogang modify 20141118
//以硬盘逻辑编号按位排列
//disk logic index	 8765 4321
//eg disk_status = 0b0000 0100  表示逻辑SATA口3上的硬盘丢失
extern u32 get_disk_err_from_public();
void CheckAlarmDiskErr(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
	int i;
	u32 nEventSatus = 0;
	u8 nEnable, nDelay;

	nEventSatus = get_disk_err_from_public();
	//if (nEventSatus)
	//	printf("yg CheckAlarmDiskErr DiskErr = 0x%04x\n", nEventSatus);
	
	for (i = 0;i < psAlarmEvent->nChannels; i++)//MAX_HDD_NUM
	{
		SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		nEnable = psAlarmChn->sAlarmPara.sAlaHDDPara.nEnable;
		nDelay = 10;//psAlarmChn->sAlarmPara.sAlaIPCCoverPara.nDelay;
		psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
		//printf("chn[%d], emType = %d\n", i, emType);
		//if ((nEventSatus >> i) & 1)
		//{
		//	printf("yg DISK_Err alarmin chn%d nEnable: %d, nDelay: %d\n", i, nEnable, nDelay);
		//}
		
		//yaogang modify for bad disk
		//RefreshStatus_unSchedule(nEnable, 1, nDelay, (nEventSatus >> i) & 1, psAlarmChn);
		RefreshStatus_unSchedule(1, 1, nDelay, (nEventSatus >> i) & 1, psAlarmChn);
	    
	    AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_DISK_ERR, i, fd485);
	}
}


extern int get_disk_lost_statue();

void CheckAlarmDiskLost(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
	int i;
	u32 nEventSatus = 0;
	u8 nEnable, nDelay;

	nEventSatus = get_disk_lost_statue();
	if (nEventSatus)
		printf("yg DiskLost = 0x%04x\n", nEventSatus);
	
	for (i = 0;i < psAlarmEvent->nChannels; i++)//MAX_HDD_NUM
	{
		SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		nEnable = psAlarmChn->sAlarmPara.sAlaHDDPara.nEnable;
		nDelay = 10;//psAlarmChn->sAlarmPara.sAlaIPCCoverPara.nDelay;
		psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
		//printf("chn[%d], emType = %d\n", i, emType);
		if ((nEventSatus >> i) & 1)
		{
			printf("yg DISK_LOST alarmin chn%d nEnable: %d, nDelay: %d\n", i, nEnable, nDelay);
		}
		RefreshStatus_unSchedule(nEnable, 1, nDelay, (nEventSatus >> i) & 1, psAlarmChn);
	    
	    AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_DISK_LOST, i, fd485);
	}
}

extern int get_disk_exist_statue();

void CheckAlarmDiskNone(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
	u32 nEventSatus = 0;
	u8 nEnable, nDelay;
	//状态机	0: 等待，对方模块还没有完成硬盘开机自检
	//				对方完成开机自检后我方第一次读取
	//			1: 不管上一步有没有报警，在这里消除报警
	//			2: 本模块工作已经完成，直接返回
	static int statue = 0;
	SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[0];

	//printf("yg CheckAlarmDiskNone statue: %d\n", statue);
	switch (statue)
	{
		case 0:
		{
			nEventSatus = get_disk_exist_statue();
			if (nEventSatus & (1<<1))//对方完成开机自检
			{
				printf("CheckAlarmDiskNone: nEventSatus: 0x%x\n", nEventSatus);

				nEnable = psAlarmChn->sAlarmPara.sAlaHDDPara.nEnable;
				nDelay = 10;//psAlarmChn->sAlarmPara.sAlaIPCCoverPara.nDelay;
				psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
				printf("yg DISK_NONE alarmin nEnable: %d, nDelay: %d\n", nEnable, nDelay);
				
				if (nEventSatus & 1)//无硬盘，报警
				{
					RefreshStatus_unSchedule(nEnable, 1, nDelay, 1, psAlarmChn);
			    
			    		AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_DISK_NONE, 0, fd485);
					statue = 1;	
				}
				else
				{
					statue = 2;
				}
			}
		}break;
		case 1:
		{
			nEnable = psAlarmChn->sAlarmPara.sAlaHDDPara.nEnable;
			nDelay = 10;//psAlarmChn->sAlarmPara.sAlaIPCCoverPara.nDelay;
			psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
			if(time(NULL) - psAlarmChn->sEvent.nTime > nDelay) //报警延时有10秒，过后清除报警
			{
				RefreshStatus_unSchedule(nEnable, 1, nDelay, 0, psAlarmChn); 
				AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_DISK_NONE, 0, fd485);

				statue = 2;
			}	
		}break;
		case 2:
		{
			return;
		}break;
		default:
			printf("CheckAlarmDiskNone: invalid param\n");
	}
	
}

//IPC遮盖报警检测
void CheckAlarmIPCCover(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
    int i;
    u32 nEventSatus = 0;
    u8 nEnable, nDelay;

    nEventSatus = tl_get_alarm_IPCCover();
	if (nEventSatus)
		printf("IPCCover alarmin = 0x%04x\n", nEventSatus);
    //printf("%s:%d nChannels:%d\n", __FUNCTION__, __LINE__, psAlarmEvent->nChannels);
    for (i = 0;i < psAlarmEvent->nChannels; i++)
    {
		SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		nEnable = psAlarmChn->sAlarmPara.sAlaIPCCoverPara.nEnable;
		//EMALARMSENSORTYPE emType = psAlarmChn->sAlarmPara.sAlaSensorPara.emType;
		nDelay = psAlarmChn->sAlarmPara.sAlaIPCCoverPara.nDelay;
		psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
		//printf("chn[%d], emType = %d\n", i, emType);
		if ((nEventSatus >> i) & 1)
		{
			printf("yg IPCCover alarmin chn%d nEnable: %d, nDelay: %d\n", i, nEnable, nDelay);
		}
		RefreshStatus_unSchedule(nEnable, 1, nDelay, (nEventSatus >> i) & 1, psAlarmChn);
        
        	AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_IPCCOVER, i, fd485);
    }
    //printf("%s:%d\n", __FUNCTION__, __LINE__);
}

//传感器报警检测
void CheckAlarmSensors(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
    int i;
    u32 nEventSatus = 0;
    u8 nEnable, nDelay;

	//得到当前状态
    nEventSatus = tl_get_alarm_input();
	
	//printf("alarmin = 0x%04x\n", nEventSatus);
    //printf("%s:%d nChannels:%d\n", __FUNCTION__, __LINE__, psAlarmEvent->nChannels);
    for (i = 0;i < psAlarmEvent->nChannels; i++)
    {
		SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		nEnable = psAlarmChn->sAlarmPara.sAlaSensorPara.nEnable;
		EMALARMSENSORTYPE emType = psAlarmChn->sAlarmPara.sAlaSensorPara.emType;
		nDelay = psAlarmChn->sAlarmPara.sAlaSensorPara.nDelay;
		psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
		//printf("chn[%d], emType = %d\n", i, emType);
		RefreshStatus(nEnable, (EM_ALARM_SENSOR_HIGH == emType) ? 1 : 0, nDelay, (nEventSatus >> i) & 1, psAlarmChn);
		
        #if 0//def SENSOR_DEMO
        static int count = 0;
        count++;
        if (count < 100)
        {
            psAlarmChn->sEvent.nStatusLast = psAlarmChn->sEvent.nStatus;
            psAlarmChn->sEvent.nStatus = !psAlarmChn->sEvent.nStatus;
         }
        else
        {
            psAlarmChn->sEvent.nStatusLast = 1;
            psAlarmChn->sEvent.nStatus = 0;
        }
        #endif
        
        AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_SENSOR, i, fd485);
    }
    //printf("%s:%d\n", __FUNCTION__, __LINE__);
}

//视频丢失检测
void CheckAlarmVlosts(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
	int i;
	u32 nEventSatus = 0;
	u8 nEnable, nDelay;
	
	nEventSatus = tl_video_connection_status();
	//printf("%s nEventSatus: 0x%08x, nChannels = %d\n", __func__, nEventSatus, psAlarmEvent->nChannels);
	
	for(i = 0;i < psAlarmEvent->nChannels; i++)
	{
		SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		nEnable = psAlarmChn->sAlarmPara.sAlaVLostPara.nEnable;
		nDelay = psAlarmChn->sAlarmPara.sAlaVLostPara.nDelay;
		psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;

		RefreshStatus(nEnable, 0, nDelay, (nEventSatus >> i) & 1, psAlarmChn);

		AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VLOST, i, fd485);
	}
}
#if 0
//视频遮挡检测
void CheckAlarmVBlinds(SAlarmEvent *psAlarmEvent, u32 lumaLevel, FNALARMCB pfnAlarmCb)
{
	int i;
	u8 nEnable, nDelay;
	unsigned int luma;
	
	for(i = 0; i < psAlarmEvent->nChannels; i++)
	{
		SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		nEnable = psAlarmChn->sAlarmPara.sAlaVBlindPara.nEnable;
		nDelay = psAlarmChn->sAlarmPara.sAlaVBlindPara.nDelay;
		psAlarmChn->nSetChangeTimesLast = psAlarmChn->nSetChangeTimes;
		if(nEnable)
		{
			if(0 == tl_video_get_luma(i, &luma))
			{
				
			}
			else
			{
				printf("chn=%d,luma=0x%08x get error\n", i, luma);
				continue;
			}
		}
		
		if(luma > lumaLevel)
		{
			RefreshStatus(nEnable, 0, nDelay, 1, psAlarmChn);
		}
		else
		{
			RefreshStatus(nEnable, 0, nDelay, 0, psAlarmChn);
		}
		
		AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VBLIND, i);
	}
}
#endif
//移动侦测检测
#if 1
void CheckAlarmVMotion(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb, int fd485)
{
	int i;
	md_result_t mdr;
	u8 nFlag = 0, nEnable, nDelay, nSetChangeTimes;
	
	static u32* pnMdFoundTimes = NULL;
	static u8* pnMdLevel = NULL;
	static u8* pnEnableLast = NULL;
	
	if(NULL == pnMdLevel || NULL == pnMdFoundTimes || NULL == pnEnableLast)
	{
		pnMdFoundTimes = malloc(sizeof(u32) * psAlarmEvent->nChannels);
		if(NULL == pnMdFoundTimes)
		{
			return;
		}
		
		pnMdLevel = malloc(sizeof(u8) * psAlarmEvent->nChannels);
		if(NULL == pnMdLevel)
		{
			free(pnMdFoundTimes);
			pnMdFoundTimes = NULL;
			
			return;
		}
		
		pnEnableLast = malloc(sizeof(u8) * psAlarmEvent->nChannels);
		if(NULL == pnEnableLast)
		{
			free(pnMdFoundTimes);
			pnMdFoundTimes = NULL;
			
			free(pnMdLevel);
			pnMdLevel = NULL;
			
			//free(pnEnableLast);
			//pnEnableLast = NULL;
			
			return;
		}
		
		for(i = 0; i< psAlarmEvent->nChannels; i++)//初始化参数
		{
			pnMdFoundTimes[i] = 0;
			pnMdLevel[i] = 0;
			pnEnableLast[i] = 0;
		}
	}
	
    // 移动侦测检查 使能状态更新
    for(i = 0; i < psAlarmEvent->nChannels; i++)
    {
        SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
        nEnable = psAlarmChn->sAlarmPara.sAlaVMotionPara.nEnable;
        nDelay = psAlarmChn->sAlarmPara.sAlaVMotionPara.nDelay;
		
		//printf("%s:%d\n", __FUNCTION__, __LINE__);
        nSetChangeTimes = psAlarmChn->nSetChangeTimes;
        if(nSetChangeTimes != psAlarmChn->nSetChangeTimesLast)
        {
        	//printf("%s:%d, chn:%d, nEnable = %d\n", __FUNCTION__, __LINE__,i,nEnable);
            psAlarmChn->nSetChangeTimesLast = nSetChangeTimes;
            if(nEnable)
            {
            	//enable
                if(pnEnableLast[i] != nEnable)
                {
                    tl_md_enable(i);
					printf("rz_md_enable chn:%d\n", i);
                }
                setmd(i, &psAlarmChn->sAlarmPara.sAlaVMotionPara, &pnMdLevel[i]);
            }
            else
            {
                //disable
                if(pnEnableLast[i] != nEnable)
                {
                    tl_md_disable(i);
					//printf("tl_md_disable chn:%d\n", i);
                }
                pnMdFoundTimes[i] = 0;
            }
            pnEnableLast[i] = nEnable;
			
			//csp modify
			RefreshStatus(nEnable, 1, nDelay, 0, psAlarmChn);
			AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VMOTION, i, fd485);
			
			//if(i == 0) printf("chn%d monotion resume for config changed\n",i);
        }
        
        if(nEnable)
        {
            nFlag += 1;//nFlag = 1;//yzw
        }
        else
        {
            pnMdFoundTimes[i] = 0;
			
			//csp modify
			RefreshStatus(nEnable, 1, nDelay, 0, psAlarmChn);
			AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VMOTION, i, fd485);
        }
		
		//csp modify
		//RefreshStatus(nEnable, 1, nDelay, 0, psAlarmChn);
		//AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VMOTION, i);
		
		//if(i == 0) printf("chn%d monotion status:(%d,%d)\n",i,psAlarmChn->sEvent.nStatus,psAlarmChn->sEvent.nStatusLast);
    }
	
	while(nFlag--)//if (nFlag) //yzw
	{
		int ret = tl_md_read_result(&mdr, 200*1000);
		//printf("tl_md_read_result : ret = %d, venc_idx = %d\n", ret, mdr.venc_idx);
		if(1 == ret)
		{
			pnMdFoundTimes[mdr.venc_idx]++;
			
			//csp modify 20140506
			//if(pnMdFoundTimes[mdr.venc_idx] > pnMdLevel[mdr.venc_idx])
			if(1)
			{
				SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[mdr.venc_idx];
				nEnable = psAlarmChn->sAlarmPara.sAlaVMotionPara.nEnable;
				nDelay = psAlarmChn->sAlarmPara.sAlaVMotionPara.nDelay;
				
				RefreshStatus(nEnable, 1, nDelay, 1, psAlarmChn);
				AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VMOTION, mdr.venc_idx, fd485);
				
				//if(mdr.venc_idx == 0) printf("chn%d monotion detected...\n",mdr.venc_idx);
			}
		}
		else if(2 == ret)
		{
			pnMdFoundTimes[mdr.venc_idx] = 0;
			
			//csp modify
			SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[mdr.venc_idx];
			nEnable = psAlarmChn->sAlarmPara.sAlaVMotionPara.nEnable;
			nDelay = psAlarmChn->sAlarmPara.sAlaVMotionPara.nDelay;
			//printf("%s:%d nStatusLast:%d nStatus:%d nDelay:%d\n", __FUNCTION__, __LINE__, psAlarmChn->sEvent.nStatusLast, psAlarmChn->sEvent.nStatus, nDelay);
			RefreshStatus(nEnable, 1, nDelay, 0, psAlarmChn);
			//printf("%s:%d nStatusLast:%d nStatus:%d nDelay:%d\n", __FUNCTION__, __LINE__, psAlarmChn->sEvent.nStatusLast, psAlarmChn->sEvent.nStatus, nDelay);
			AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VMOTION, mdr.venc_idx, fd485);
			
			//if(mdr.venc_idx == 0) printf("chn%d monotion resume...\n",mdr.venc_idx);
		}
	}
}
#else
void CheckAlarmVMotion(SAlarmEvent *psAlarmEvent, FNALARMCB pfnAlarmCb)
{
    int i;
    md_result_t mdr;
    u8 nFlag = 0, nEnable, nDelay, nSetChangeTimes;
    static u32* pnMdFoundTimes = NULL;
    static u8* pnMdLevel = NULL;
    static u8* pnEnableLast = NULL;

    if (NULL == pnMdLevel  ||  NULL == pnMdFoundTimes || NULL == pnEnableLast)
    {
        pnMdFoundTimes = malloc(sizeof(u32) * psAlarmEvent->nChannels);
        if (NULL == pnMdFoundTimes)
        {
            return;
        }
        
        pnMdLevel = malloc(sizeof(u8) * psAlarmEvent->nChannels);
        if (NULL == pnMdLevel)
        {
            free(pnMdFoundTimes);
            pnMdFoundTimes = NULL;
            
            return;
        }
		
        pnEnableLast = malloc(sizeof(u8) * psAlarmEvent->nChannels);
        if (NULL == pnMdLevel)
        {
            free(pnMdFoundTimes);
            pnMdFoundTimes = NULL;
            free(pnEnableLast);
            pnEnableLast = NULL;
            
            return;
        }
        
        for (i = 0; i< psAlarmEvent->nChannels; i++)//初始化参数
        {
            pnMdFoundTimes[i] = 0;
            pnMdLevel[i] = 0;
            pnEnableLast[i] = 0;
        }
    }
	
    // 移动侦测检查 使能状态更新
    for (i = 0; i < psAlarmEvent->nChannels; i++)
    {
        SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[i];
        nEnable = psAlarmChn->sAlarmPara.sAlaVMotionPara.nEnable;
        nDelay = psAlarmChn->sAlarmPara.sAlaVMotionPara.nDelay;

        nSetChangeTimes = psAlarmChn->nSetChangeTimes;
        //printf("%s:%d\n", __FUNCTION__, __LINE__);
        if (nSetChangeTimes != psAlarmChn->nSetChangeTimesLast)
        { 
            psAlarmChn->nSetChangeTimesLast = nSetChangeTimes; 
            if (nEnable)
            {
                //enable
                if (pnEnableLast[i] != nEnable)
                {
                    tl_md_enable(i);
                }
                setmd(i, &psAlarmChn->sAlarmPara.sAlaVMotionPara, &pnMdLevel[i]);
            }
            else
            {
                //disable
                if (pnEnableLast[i] != nEnable)
                {
                    tl_md_disable(i);
                }
                pnMdFoundTimes[i] = 0;
            }
            pnEnableLast[i] = nEnable;
        }
        
        if (nEnable)
        {
            nFlag = 1;
        }
        else
        {
            pnMdFoundTimes[i] = 0;
        }

        RefreshStatus(nEnable, 1, nDelay, 0, psAlarmChn);
        
        AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VMOTION, i);
    }

    if (nFlag)
    {
        int ret = tl_md_read_result(&mdr, 200*1000);
        if (1 == ret)
        {
            pnMdFoundTimes[mdr.venc_idx]++;

            if (pnMdFoundTimes[mdr.venc_idx] > pnMdLevel[mdr.venc_idx])
            {
                SAlarmChn *psAlarmChn = &psAlarmEvent->psAlarmChn[mdr.venc_idx];
                nEnable = psAlarmChn->sAlarmPara.sAlaVMotionPara.nEnable;
                nDelay = psAlarmChn->sAlarmPara.sAlaVMotionPara.nDelay;

                RefreshStatus(nEnable, 1, nDelay, 1, psAlarmChn);

                AlarmEventCallback(psAlarmChn, pfnAlarmCb, EM_ALARM_EVENT_VMOTION, mdr.venc_idx);
            }
        }
        else if (2 == ret)
        {
            pnMdFoundTimes[mdr.venc_idx] = 0;
        }
    }
}
#endif

u8 IsInSchedule(SAlarmSchedule* psSchedule)
{
	int i, day;
	struct tm *ptm;
	time_t cur = time(NULL);
	
	//csp modify 20131213
	cur += ModAlarmGetTimeZoneOffset();
	
	//csp modify
	//ptm = localtime(&cur);
	struct tm tm0;
	ptm = &tm0;
	localtime_r(&cur, ptm);
	
    switch (psSchedule->nSchType)
    {
        case EM_ALARM_SCH_WEEK_DAY:
            day = ptm->tm_wday;
			break;
        default:
            return 0;
    }
    
	for (i = 0; i < MAX_ALARM_TIME_SEGMENTS; i++)
	{		
		if((ptm->tm_hour*60 + ptm->tm_min >= psSchedule->nSchTime[day][i].nStartTime/60) 
			&& (ptm->tm_hour*60 + ptm->tm_min <= psSchedule->nSchTime[day][i].nStopTime/60))
		{
			return 1;
		}
	}
	
    return 0;
}

static u8 alarm_checkstatue = 0;

u8 GetAlarmCheckStaue()
{
	return alarm_checkstatue;
}

void RefreshStatus_unSchedule(u8 nEnable, u8 nType, u8 nDelay, u8 nLevel, SAlarmChn *psAlarmChn)
{
	//SAlarmSchedule sSchedule = psAlarmChn->sAlarmSchedule;
	u8 nStatus = 0;

	//配置
	if(nEnable)
	{
		//判断 有无报警 
		
		nStatus = CalStatus(nType, nLevel);
		//printf("yg CalStatus %d\n", nStatus);
		//printf("yg IsInSchedule %d\n", IsInSchedule(&sSchedule));
		/*
		if(nStatus)
		{
			if(!IsInSchedule(&sSchedule))
			{
				nStatus = 0;
			}
		}
		*/
	}
	else
	{
		nStatus = 0;
	}
	
	psAlarmChn->sEvent.nStatusLast = psAlarmChn->sEvent.nStatus;
	//printf("yg nStatus %d\n", psAlarmChn->sEvent.nStatus);
	//有的话 保存时间
	if(nStatus)
	{
		psAlarmChn->sEvent.nStatus = nStatus;
		psAlarmChn->sEvent.nTime = time(NULL);
	}
	else
	{
		//延时处理
		if(time(NULL) - psAlarmChn->sEvent.nTime >= nDelay)
		{
			psAlarmChn->sEvent.nStatus = 0;
		}
	}
	
	alarm_checkstatue = nStatus;
	//if (nStatus == 1)
		//printf("yg 2 nStatus %d\n", nStatus);
}

void RefreshStatus(u8 nEnable, u8 nType, u8 nDelay, u8 nLevel, SAlarmChn *psAlarmChn)
{
	SAlarmSchedule sSchedule = psAlarmChn->sAlarmSchedule;
	u8 nStatus = 0;

	//配置
	if(nEnable)
	{
		//判断 有无报警 
		
		nStatus = CalStatus(nType, nLevel);
		
		if(nStatus)
		{
			//printf("yg CalStatus %d\n", nStatus);
			//printf("yg IsInSchedule %d\n", IsInSchedule(&sSchedule));
			if(!IsInSchedule(&sSchedule))
			{
				nStatus = 0;
			}
		}
	}
	else
	{
		nStatus = 0;
	}
	
	psAlarmChn->sEvent.nStatusLast = psAlarmChn->sEvent.nStatus;
	//printf("yg nStatus %d\n", psAlarmChn->sEvent.nStatus);
	//有的话 保存时间
	if(nStatus)
	{
		psAlarmChn->sEvent.nStatus = nStatus;
		psAlarmChn->sEvent.nTime = time(NULL);
	}
	else
	{
		//延时处理
		if(time(NULL) - psAlarmChn->sEvent.nTime >= nDelay)
		{
			psAlarmChn->sEvent.nStatus = 0;
		}
	}
	
	alarm_checkstatue = nStatus;
	//printf("yg 2 nStatus %d\n", nStatus);
}

void getMDnum(u8 flag_motion,unsigned short *sad,unsigned short *sadnum,u8 *framenum)
{
	switch(flag_motion)
	{
		case 1:
			*sad  = 30;
			*sadnum  = 10;
			*framenum  = 6;
			break;
		case 2:
			*sad  = 25;
			*sadnum  = 8;
			*framenum  = 5;
			break;
		case 3:
			*sad  = 17;
			*sadnum  = 6;
			*framenum  = 4;
			break;
		case 4:
			*sad  = 10;
			*sadnum  = 4;
			*framenum  = 3;
			break;
		case 5:
			*sad  = 4;
			*sadnum  = 1;
			*framenum  = 2;
			break;
		default:
			*sad  = 7;
			*sadnum  = 7;
			*framenum  = 7;
			printf("getMDnum error\n");
			break;
	}
	
	//printf("sad=%d,sadnum=%d,framenum=%d\n", *sad, *sadnum, *framenum);
}

void setmd(u8 chn, SAlarmVMotionPara *psAlarmVMotionPara, u8 *pnMdLevel)
{
	int k;
	int j;
	unsigned long long tmp = 0;
	
	md_atr_t mda;
	memset(&mda, 0, sizeof(mda));//cw_md
	
	#if 0
	printf("setmd: chn = %d, emResol = %d, area:( ", chn, psAlarmVMotionPara->emResol);
	for(j=0;j<18;++j)
	{
		printf("%llx ", psAlarmVMotionPara->nBlockStatus[j]);
	}
	printf(")\n");
	#endif
	
	if(EM_RESOL_CIF == psAlarmVMotionPara->emResol)
	{
		tmp = 1;
		for(j=0;j<18;++j)
		{
			for(k=0;k<22;++k)
			{
				//if(ma.flag_block[j*22+k] == '1') 
				if(psAlarmVMotionPara->nBlockStatus[j] & (1 << k)) 
				{
					//csp modify 20130416
					//mda.area[j] |= (tmp<<(22-k));
					mda.area[j] |= (tmp<<(21-k));
					//printf("#");
				}
				else
				{
					//printf("^");
				}
			}
			// printf("\n");
			//if(img.flag_motion) printf("md:0x%016llx\n",mda.area[j]);
		}
		getMDnum(psAlarmVMotionPara->nSensitivity, &(mda.sad), &(mda.sad_num), pnMdLevel);
	}
	else//d1
	{
		tmp = 1;
		for(j=0;j<18;++j)
		{
			for(k=0;k<22;++k)
			{
				if(psAlarmVMotionPara->nBlockStatus[j] & (1 << k))
				{
					//csp modify 20130416
					//mda.area[j*2] |= (tmp<<((22-k)*2));
					//mda.area[j*2+1] |= (tmp<<((22-k)*2));
					//mda.area[j*2] |= (tmp<<((22-k)*2-1));
					//mda.area[j*2+1] |= (tmp<<((22-k)*2-1));
					mda.area[j*2] |= (tmp<<((21-k)*2));
					mda.area[j*2+1] |= (tmp<<((21-k)*2));
					mda.area[j*2] |= (tmp<<((21-k)*2+1));
					mda.area[j*2+1] |= (tmp<<((21-k)*2+1));
					//printf("#");
				}
				else
				{
					//printf("^");
				}
			}
			// printf("\n");
			//if(img.flag_motion) printf("%016llx\n%016llx\n",mda.area[j*2],mda.area[j*2+1]);
		}
		getMDnum(psAlarmVMotionPara->nSensitivity, &(mda.sad), &(mda.sad_num), pnMdLevel);
		mda.sad_num *= 4;
	}
	
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
	k = tl_md_set_atr(chn, &mda);
	
	//printf("k = %d\n",k);
	//printf("%s:%d\n", __FUNCTION__, __LINE__);
}

u8 CalStatus(u8 type, u8 st)
{
	return (type == st);
}

void AlarmDispatch(SAlarmManager *psAlarmManager)
{
	//触发蜂鸣器
	BuzzDispatch(psAlarmManager);
	
	//触发报警输出
	AlarmOutDispatch(psAlarmManager);
	
	//触发录像
	//yaogang modify 20150324 
	//跃天: 1 nvr，2 轮巡解码器，3 切换解码器
	if (psAlarmManager->nNVROrDecoder == 1)
	{
		RecordDispatch(psAlarmManager);
	}
	
	//触发云台联动
	PtzDispatch(psAlarmManager);
	
	//触发单通道放大
	ZoomDispatch(psAlarmManager);
}

//蜂鸣器触发
void BuzzDispatch(SAlarmManager *psAlarmManager)
{
    SAlarmEvent *psAlarmEvent;
    u8 nStatus = 0, nStatusLast = 0;
    u8 nEnable; //是否启用
    u16 nDelay; //延时	
    u8 nDuration;//蜂鸣时间时长非零将是间歇式蜂鸣(单位s)
    u8 nInterval; //蜂鸣时间间隔非零将是间歇式蜂鸣(单位s)
    
    static time_t nLastTime = 0; //状态为1的时间
    static time_t nLastBuzzTime = 0; //上次蜂鸣时间
    static u8 nBuzzLast = 1;
	
	SAlarmBuzz* psAlarmBuzz = &psAlarmManager->sAlarmBuzz;
	if(0 == psAlarmBuzz->nChannels)
	{
		return;
	}

	//printf("sensor alarm\n");
    psAlarmEvent = &psAlarmManager->sSensors;
    nStatus = CheckEventDispatchBuzz(psAlarmEvent);
	
	
	//if(nStatus) printf("buzz event-1\n");
     if(0 == nStatus)
    {
	//printf("IPCEXT sensor alarm\n");
        psAlarmEvent = &psAlarmManager->sIPCExtSensors;
        nStatus = CheckEventDispatchBuzz(psAlarmEvent);
		
		//if(nStatus) printf("buzz event-2\n");
    }
	 //printf("IPCCover alarm\n");
	if(0 == nStatus)
	{
		//printf("yg psAlarmManager->sIPCCover\n");
	    psAlarmEvent = &psAlarmManager->sIPCCover;
	    nStatus = CheckEventDispatchBuzz(psAlarmEvent);
		
		//if(nStatus) printf("buzz event-2\n");
	}
	 
    if(0 == nStatus)
    {
        psAlarmEvent = &psAlarmManager->sVMotions;
        nStatus = CheckEventDispatchBuzz(psAlarmEvent);
		
		//if(nStatus) printf("buzz event-2\n");
    }
	
    if(0 == nStatus)
    {
        psAlarmEvent = &psAlarmManager->sVBlinds;
        nStatus = CheckEventDispatchBuzz(psAlarmEvent);
		
		//if(nStatus) printf("buzz event-3\n");
    }
	
    if(0 == nStatus)
    {
        psAlarmEvent = &psAlarmManager->sVLosts;
        nStatus = CheckEventDispatchBuzz(psAlarmEvent);
		
		//if(nStatus) printf("buzz event-4\n");
    }
    if(0 == nStatus)
    {
        psAlarmEvent = &psAlarmManager->sHDDLost;
	//printf("buzz DISK lost\n");
        nStatus = CheckEventDispatchBuzz(psAlarmEvent);
		
		//if(nStatus) printf("buzz DISK lost\n");
    }
	if(0 == nStatus)
	{
		psAlarmEvent = &psAlarmManager->sHDDErr;
		//printf("buzz DISK Err\n");
		nStatus = CheckEventDispatchBuzz(psAlarmEvent);

		//if(nStatus) printf("buzz DISK Err\n");
	}
	if(0 == nStatus)
	{
		psAlarmEvent = &psAlarmManager->sHDDNone;
		//printf("buzz DISK None\n");
		nStatus = CheckEventDispatchBuzz(psAlarmEvent);

		//if(nStatus) printf("buzz DISK None\n");
	}
	if(0 == nStatus)
	{
		//printf("485EXT sensor alarm\n");
		psAlarmEvent = &psAlarmManager->s485ExtSensors;
		nStatus = CheckEventDispatchBuzz(psAlarmEvent);

		//if(nStatus) printf("buzz 485EXT\n");
	}
	
	nEnable = psAlarmBuzz->psAlarmBuzzChn[0].sAlarmPara.sAlaBuzzPara.nEnable;
	nDelay = psAlarmBuzz->psAlarmBuzzChn[0].sAlarmPara.sAlaBuzzPara.nDelay;
	nInterval = psAlarmBuzz->psAlarmBuzzChn[0].sAlarmPara.sAlaBuzzPara.nInterval;
	nDuration = psAlarmBuzz->psAlarmBuzzChn[0].sAlarmPara.sAlaBuzzPara.nDuration;
	nStatusLast = psAlarmBuzz->psAlarmBuzzChn[0].sEvent.nStatus;
	//nLastTime = psAlarmBuzz->psAlarmBuzzChn[0].sEvent.nTime;
	
	if(0 == nEnable)
	{
		//if(nStatus) printf("buzz event-disable\n");
		
		nStatus = 0;
	}
	
    if(nStatus)
    {
        if(0 == nStatusLast)
        {
            psAlarmBuzz->psAlarmBuzzChn[0].sEvent.nTime = time(NULL);
			//printf("buzz start time : %d\n", psAlarmBuzz->psAlarmBuzzChn[0].sEvent.nTime);
        }     
    }
    else
    {
    	//防止修改时间为过去时间时会报警
    	if(nLastTime > time(NULL))
    	{
			psAlarmBuzz->psAlarmBuzzChn[0].sEvent.nTime = time(NULL) - nDelay - 1;
			//nLastTime = psAlarmBuzz->psAlarmBuzzChn[0].sEvent.nTime;
			nLastTime = 0;
    	}

		if(nStatusLast && (0 == nLastTime))
		{
			nLastTime = time(NULL);
		}

        if(time(NULL) - nLastTime < nDelay)
        {
            nStatus = 1;
        }     	
    }
	
    psAlarmBuzz->psAlarmBuzzChn[0].sEvent.nStatus = nStatus;
	
    if(nStatus)
    {
        if(0 == nStatusLast)
        {
            nBuzzLast = 1;
            nLastBuzzTime = time(NULL);
            tl_buzzer_ctl(1);
        }
        else if(nInterval && nDuration)
        {
            if(nBuzzLast)
            {
                if(time(NULL) - nLastBuzzTime >=  nDuration)
                {
                    nBuzzLast = 0;
                    nLastBuzzTime = time(NULL);
					tl_buzzer_ctl(0);
                }
            }
            else
            {
                if(time(NULL) - nLastBuzzTime >=  nInterval)
                {
                    nBuzzLast = 1;
                    nLastBuzzTime = time(NULL);
					tl_buzzer_ctl(1);
                }
            }
        }
    }
    else
    {
        if(nBuzzLast)
        {
            nBuzzLast = 0;
			nLastTime = 0;
            tl_buzzer_ctl(0);
        }
    }
	
    nStatusLast = nStatus;
}

u8 CheckEventDispatchBuzz(SAlarmEvent *psAlarmEvent)
{
    int i;
    SAlarmChn* psAlarmChn;
    
    for(i =0; i <  psAlarmEvent->nChannels; i++)
    {
        psAlarmChn = &psAlarmEvent->psAlarmChn[i];

        if (1 == psAlarmChn->sEvent.nStatus && psAlarmChn->sAlarmDispatch.nFlagBuzz)
        {
			//if (i == 0)
			//	printf("yg CheckEventDispatchBuzz nstatus: %d, nFlagBuzz: %d\n", \
			//		psAlarmChn->sEvent.nStatus, psAlarmChn->sAlarmDispatch.nFlagBuzz);

			return 1;
        }       
    }

    return 0;
}

void AlarmOutDispatch(SAlarmManager *psAlarmManager)
{
    SAlarmEvent *psAlarmEvent;
    int i;
    u8 nStatus = 0, nStatusLast;
    u8 nEnable; //是否启用
    u16 nDelay; //延时	
    time_t nLastTime; //状态为1的时间
    
    SAlarmOut* psAlarmOut = &psAlarmManager->sAlarmOut;
    if (0 == psAlarmOut->nChannels)
    {
        return;
    }
	
	u8 typechanged = 0;
	
	static u8* pLastType = NULL;
	static u8* pResumed = NULL;
	static int done = 0;
	
	if(0 == done)
	{
		pLastType = (u8*)malloc(psAlarmOut->nChannels);
		//csp modify 20140525
		//memset(pLastType, 0, psAlarmOut->nChannels);
		memset(pLastType, 0xfe, psAlarmOut->nChannels);
		
		pResumed = (u8*)malloc(psAlarmOut->nChannels);
		memset(pResumed, 1, psAlarmOut->nChannels);
		
		done = 1;
	}
	
    for (i = 0; i < psAlarmOut->nChannels; i++)
    {
        SAlarmOutChn *psAlarmOutChn = &psAlarmOut->psAlarmOutChn[i];
		
		if(pLastType[i] != psAlarmOutChn->sAlarmPara.sAlaOutPara.emType)
		{
			tl_set_alarm_out(i, 2 - psAlarmOutChn->sAlarmPara.sAlaOutPara.emType);
			pLastType[i] = psAlarmOutChn->sAlarmPara.sAlaOutPara.emType;
			typechanged = 1;
		}
		
        psAlarmEvent = &psAlarmManager->sSensors;
        nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
        if(0 == nStatus)
        {
            psAlarmEvent = &psAlarmManager->sVMotions;
            nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
	if(nStatus) printf("buzz event-1\n");
        }

	if(0 == nStatus)
    {
        psAlarmEvent = &psAlarmManager->sIPCExtSensors;
        nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
		
	if(nStatus) printf("buzz event-2\n");
    }
	if(0 == nStatus)
    {
		//printf("yg psAlarmManager->sIPCCover\n");
        psAlarmEvent = &psAlarmManager->sIPCCover;
        nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
		
	if(nStatus) printf("buzz event-3\n");
    }

        if(0 == nStatus)
        {
            psAlarmEvent = &psAlarmManager->sVBlinds;
            nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
			if(nStatus) printf("buzz event-4\n");
        }

        if(0 == nStatus)
        {
            psAlarmEvent = &psAlarmManager->sVLosts;
            nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
			if(nStatus) printf("buzz event-5\n");
        }
	if(0 == nStatus)
        {
		psAlarmEvent = &psAlarmManager->sHDDLost;
		nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
		if(nStatus) printf("AlarmOut DISK lost\n");
	
        }
	if(0 == nStatus)
        {
		psAlarmEvent = &psAlarmManager->sHDDErr;
		nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
		if(nStatus) printf("AlarmOut DISK Err\n");
	
        }
	if(0 == nStatus)
        {
		psAlarmEvent = &psAlarmManager->sHDDNone;
		nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);
		if(nStatus) printf("AlarmOut DISK None\n");
	
        }
	if(0 == nStatus)
	{
		psAlarmEvent = &psAlarmManager->s485ExtSensors;
		nStatus = CheckEventDispatchAlarmOut(psAlarmEvent, i);

		if(nStatus) printf("AlarmOut 485Ext\n");
	}

       nEnable = psAlarmOutChn->sAlarmPara.sAlaOutPara.nEnable;
       nDelay = psAlarmOutChn->sAlarmPara.sAlaOutPara.nDelay;
       nStatusLast = psAlarmOutChn->sEvent.nStatus;
       nLastTime = psAlarmOutChn->sEvent.nTime;

       if (nStatus && (0 == nEnable || 0 == IsInSchedule(&psAlarmOutChn->sAlarmSchedule)))
       {
            nStatus = 0;
       }

        if(nStatus)
        {
            psAlarmOutChn->sEvent.nTime = time(NULL);
            if (0 == nStatusLast)
            {
            	//PRINT_DEBUG;
                psAlarmOutChn->sEvent.nStatus = 1;
                tl_set_alarm_out(i, psAlarmOutChn->sAlarmPara.sAlaOutPara.emType - 1);
				pResumed[i] = 0;
            }
			if(typechanged != 0)
			{
				tl_set_alarm_out(i, psAlarmOutChn->sAlarmPara.sAlaOutPara.emType - 1);
			}
        }
        else
        {
            if(((time(NULL) - nLastTime > nDelay) || (time(NULL) < nLastTime)) && (pResumed[i] == 0))
            {
            	//PRINT_DEBUG;
                pResumed[i] = 1;
            	psAlarmOutChn->sEvent.nStatus = 0;
                tl_set_alarm_out(i, 2 - psAlarmOut->psAlarmOutChn[i].sAlarmPara.sAlaOutPara.emType);
            }
			else if((time(NULL) - nLastTime < nDelay) && (pResumed[i] == 0) && (typechanged))
            {
            	tl_set_alarm_out(i, psAlarmOutChn->sAlarmPara.sAlaOutPara.emType - 1);
            }
        }
		typechanged = 0;
    }
}

u8 CheckEventDispatchAlarmOut(SAlarmEvent *psAlarmEvent, u8 id)
{
    int i, j;
    SAlarmChn* psAlarmChn;
    
    for(i =0; i <  psAlarmEvent->nChannels; i++)
    {
        psAlarmChn = &psAlarmEvent->psAlarmChn[i];
        if (1 == psAlarmChn->sEvent.nStatus)
        {
            for (j = 0; j < sizeof(psAlarmChn->sAlarmDispatch.nAlarmOut) / sizeof(u8); j++)
            {
                if (psAlarmChn->sAlarmDispatch.nAlarmOut[j] == id)
                {
                    return 1;
                }
            }
        }
    }
	
    return 0;
}

void RecordDispatch(SAlarmManager *psAlarmManager)
{
	if (0 == CheckHardDiskExist())
	{
		return;
	}
	
    SAlarmEvent *psAlarmEvent;
    int i;
    u8 nChannels, nStatus;
    
    nChannels = psAlarmManager->sVMotions.nChannels;
    if (0 == nChannels)
    {
        return;
    }

	static u8* pnStatusSensorRec = NULL;
	static u8* pnStatusIPCExtSensorRec = NULL;
	static u8* pnStatusIPCCoverRec = NULL;
	static u8* pnStatusVMotionRec = NULL;
	static u8* pnStatusVLostRec = NULL;
	static u8* pnStatusVBlindRec = NULL;
	static u8 flag = 0;

    if(flag == 0)
	{
		//printf("malloc   \n");
		pnStatusSensorRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusSensorRec)
	    {
	        return;
	    }
	    memset(pnStatusSensorRec, 0, sizeof(u8) * nChannels);
	    
	    pnStatusVMotionRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusVMotionRec)
	    {
	        free(pnStatusSensorRec);
	        return;
	    }
	    memset(pnStatusVMotionRec, 0, sizeof(u8) * nChannels);
	    
	    pnStatusVLostRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusVLostRec)
	    {
	        free(pnStatusSensorRec);
	        free(pnStatusVMotionRec);
	        return;
	    }
	    memset(pnStatusVLostRec, 0, sizeof(u8) * nChannels);
	    
	    pnStatusVBlindRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusVBlindRec)
	    {
	        free(pnStatusSensorRec);
	        free(pnStatusVMotionRec);
	        free(pnStatusVLostRec);
	        return;
	    }
	    memset(pnStatusVBlindRec, 0, sizeof(u8) * nChannels);

		pnStatusIPCExtSensorRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusIPCExtSensorRec)
	    {
		free(pnStatusSensorRec);
	        free(pnStatusVMotionRec);
	        free(pnStatusVLostRec);
		free(pnStatusVBlindRec);
	        return;
	    }
	    memset(pnStatusIPCExtSensorRec, 0, sizeof(u8) * nChannels);

		pnStatusIPCCoverRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusIPCCoverRec)
	    {
		free(pnStatusSensorRec);
	        free(pnStatusVMotionRec);
	        free(pnStatusVLostRec);
		free(pnStatusVBlindRec);
		free(pnStatusIPCExtSensorRec);
	        return;
	    }
	    memset(pnStatusIPCCoverRec, 0, sizeof(u8) * nChannels);

		flag = 1;
    }
	
    for (i = 0; i < nChannels; i++)
    {
        psAlarmEvent = &psAlarmManager->sSensors;
        nStatus = CheckEventDispatchRecord(psAlarmEvent, i);
        if (nStatus != pnStatusSensorRec[i])
        {
            //传感器触发录像回调
            RecordDispatchCallback(nStatus, psAlarmManager->pfnAlarmCb, EM_ALARM_EVENT_DISPATCH_REC_SENSOR, i);
            pnStatusSensorRec[i] = nStatus;
        }

	psAlarmEvent = &psAlarmManager->sIPCExtSensors;
        nStatus = CheckEventDispatchRecord(psAlarmEvent, i);
        if (nStatus != pnStatusIPCExtSensorRec[i])
        {
            //IPC 外部传感器触发录像回调
            RecordDispatchCallback(nStatus, psAlarmManager->pfnAlarmCb, EM_ALARM_EVENT_DISPATCH_REC_IPCEXTSENSOR, i);
            pnStatusIPCExtSensorRec[i] = nStatus;
        }

	psAlarmEvent = &psAlarmManager->sIPCCover;
        nStatus = CheckEventDispatchRecord(psAlarmEvent, i);
        if (nStatus != pnStatusIPCCoverRec[i])
        {
            //IPCCover触发录像回调
            RecordDispatchCallback(nStatus, psAlarmManager->pfnAlarmCb, EM_ALARM_EVENT_DISPATCH_REC_IPCCOVER, i);
            pnStatusIPCCoverRec[i] = nStatus;
        }
		
        psAlarmEvent = &psAlarmManager->sVMotions;
        nStatus = CheckEventDispatchRecord(psAlarmEvent, i);
        if (nStatus != pnStatusVMotionRec[i])
        {
            //移动侦测触发录像回调
            RecordDispatchCallback(nStatus, psAlarmManager->pfnAlarmCb, EM_ALARM_EVENT_DISPATCH_REC_VMOTION, i);
            pnStatusVMotionRec[i] = nStatus;
        }

        psAlarmEvent = &psAlarmManager->sVBlinds;
        nStatus = CheckEventDispatchRecord(psAlarmEvent, i);
        if (nStatus != pnStatusVBlindRec[i])
        {
            //视频遮挡触发录像回调
            RecordDispatchCallback(nStatus, psAlarmManager->pfnAlarmCb, EM_ALARM_EVENT_DISPATCH_REC_VBLIND, i);
            pnStatusVBlindRec[i] = nStatus;
        }

        psAlarmEvent = &psAlarmManager->sVLosts;
        nStatus = CheckEventDispatchRecord(psAlarmEvent, i);
        if (nStatus != pnStatusVLostRec[i])
        {
            //视频丢失触发录像回调
            RecordDispatchCallback(nStatus, psAlarmManager->pfnAlarmCb, EM_ALARM_EVENT_DISPATCH_REC_VLOST, i);
            pnStatusVLostRec[i] = nStatus;
        }
    }
}

u8 CheckEventDispatchRecord(SAlarmEvent *psAlarmEvent, u8 id)
{
    int i, j;
    SAlarmChn* psAlarmChn;
    
    for(i =0; i <  psAlarmEvent->nChannels; i++)
    {
        psAlarmChn = &psAlarmEvent->psAlarmChn[i];
        if (1 == psAlarmChn->sEvent.nStatus)
        {
            for (j = 0; j < sizeof(psAlarmChn->sAlarmDispatch.nRecordChn) / sizeof(u8); j++)
            {
                if (psAlarmChn->sAlarmDispatch.nRecordChn[j] == id)
                {
                    return 1;
                }
            }
        }
    }

    return 0;
}

void RecordDispatchCallback(u8 nStatus, FNALARMCB pfnAlarmCb, EMALARMEVENT emAlarmEvent, u8 chn)
{
    if (pfnAlarmCb)
    {
        SAlarmCbData sAlarmCbData;
        sAlarmCbData.emAlarmEvent = emAlarmEvent;
        sAlarmCbData.nChn = chn;
        sAlarmCbData.nData = nStatus;
        sAlarmCbData.nTime = time(NULL);
        pfnAlarmCb(&sAlarmCbData);
    }
}

void PtzDispatch(SAlarmManager *psAlarmManager)
{
    SAlarmEvent *psAlarmEvent;
    u8 nChannels;


    nChannels = psAlarmManager->sVMotions.nChannels;
    if (0 == nChannels)
    {
        return;
    }

	static u8* pnStatusSensorRec = NULL;
	static u8* pnStatusVMotionRec = NULL;
	static u8* pnStatusVLostRec = NULL;
	static u8* pnStatusVBlindRec = NULL;
	static u8* pnStatusIPCExtSensorRec = NULL;
	static u8* pnStatusIPCCoverRec = NULL;
	static u8 flag = 0;

    if(flag == 0)
	{
		//printf("malloc   \n");
		pnStatusSensorRec = malloc(sizeof(u8) * psAlarmManager->sSensors.nChannels);
	    if (NULL == pnStatusSensorRec)
	    {
	        return;
	    }
	    memset(pnStatusSensorRec, 0, sizeof(u8) * psAlarmManager->sSensors.nChannels);
	    
	    pnStatusVMotionRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusVMotionRec)
	    {
	        free(pnStatusSensorRec);
	        return;
	    }
	    memset(pnStatusVMotionRec, 0, sizeof(u8) * nChannels);
	    
	    pnStatusVLostRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusVLostRec)
	    {
	        free(pnStatusSensorRec);
	        free(pnStatusVMotionRec);
	        return;
	    }
	    memset(pnStatusVLostRec, 0, sizeof(u8) * nChannels);
	    
	    pnStatusVBlindRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusVBlindRec)
	    {
	        free(pnStatusSensorRec);
	        free(pnStatusVMotionRec);
	        free(pnStatusVLostRec);
	        return;
	    }
	    memset(pnStatusVBlindRec, 0, sizeof(u8) * nChannels);

		pnStatusIPCExtSensorRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusIPCExtSensorRec)
	    {
	        free(pnStatusSensorRec);
	        free(pnStatusVMotionRec);
	        free(pnStatusVLostRec);
		free(pnStatusVBlindRec);
	        return;
	    }
	    memset(pnStatusIPCExtSensorRec, 0, sizeof(u8) * nChannels);

		pnStatusIPCCoverRec = malloc(sizeof(u8) * nChannels);
	    if (NULL == pnStatusIPCCoverRec)
	    {
	        free(pnStatusSensorRec);
	        free(pnStatusVMotionRec);
	        free(pnStatusVLostRec);
		free(pnStatusVBlindRec);
		free(pnStatusIPCExtSensorRec);
	        return;
	    }
	    memset(pnStatusIPCCoverRec, 0, sizeof(u8) * nChannels);

		flag = 1;
    }

    //传感器触发云台回调
    psAlarmEvent = &psAlarmManager->sSensors;
    EventDispatchPtz(psAlarmManager, psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusSensorRec);

	//IPCEXT 传感器触发云台回调
    psAlarmEvent = &psAlarmManager->sIPCExtSensors;
    EventDispatchPtz(psAlarmManager, psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusIPCExtSensorRec);

    //移动侦测触发云台回调
    psAlarmEvent = &psAlarmManager->sVMotions;
    EventDispatchPtz(psAlarmManager, psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusVMotionRec);

    //视频遮挡触发云台回调
    psAlarmEvent = &psAlarmManager->sVBlinds;
    EventDispatchPtz(psAlarmManager, psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusVBlindRec);
	
	psAlarmEvent = &psAlarmManager->sIPCCover;
    EventDispatchPtz(psAlarmManager, psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusIPCCoverRec);

    //视频丢失触发云台回调
    psAlarmEvent = &psAlarmManager->sVLosts;
    EventDispatchPtz(psAlarmManager, psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusVLostRec);
}

void EventDispatchPtz(SAlarmManager *psAlarmManager, SAlarmEvent *psAlarmEvent,  FNALARMCB pfnAlarmCb, u8* pLastStatus)
{
    int i, j;
    SAlarmChn* psAlarmChn;
    EMALARMEVENT emAlarmEvent;
	static EMALARMEVENT emLastAlarmEvent;
	SAlarmCbData sAlarmCbData;
	static time_t past = 0;
	static int presetFlag[sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)][sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)];// = {0};

	//用来记录上次开启时的编号
	static u8 nLastData[sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)][sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)];// = {0};
	static u8 nLastType[sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)][sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)];// = {0};
	static u8 nTmpType[sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)][sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)];// = {0};
	time_t now = 0;
    u8 nData;

    //外for循环对每个通道进行检查
    for(i =0; i <  psAlarmEvent->nChannels; i++)
    {
        psAlarmChn = &psAlarmEvent->psAlarmChn[i];
        
        //当状态改变时
        if (pLastStatus[i] != psAlarmChn->sEvent.nStatus)//(psAlarmChn->sEvent.nStatusLast != psAlarmChn->sEvent.nStatus)
        {
			memset(nTmpType[i], 0, sizeof(nTmpType) / (sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz)/sizeof(SAlarmPtz)));
			
			//内for循环会对报警通道支持有N路通道联动做状态检查
            for (j = 0; j < sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz) / sizeof(SAlarmPtz); j++)
            {
            	//联动通道的类型
                switch (psAlarmChn->sAlarmDispatch.sAlarmPtz[j].emALaPtzType)
                {
                    case EM_ALARM_PTZ_PRESET:
						{
                        	emAlarmEvent = EM_ALARM_EVENT_DISPATCH_PTZ_PRESET;
                        	
							//标记对应通道联动的云台置点,每个通道有128个置点
							presetFlag[i][j] = 1;
                        } break;
                    case EM_ALARM_PTZ_PATROL:
						{
                        	emAlarmEvent = EM_ALARM_EVENT_DISPATCH_PTZ_PATROL;
							presetFlag[i][j] = 0;
						} break;
                    case EM_ALARM_PTZ_LOCUS:
						{
                        	emAlarmEvent = EM_ALARM_EVENT_DISPATCH_PTZ_LOCUS;
							presetFlag[i][j] = 0;
                    	}  break;
                    case EM_ALARM_PTZ_NULL:
                    default:
						nTmpType[i][j] = 1;
                        continue;                                        
                }
				
				//nChn = psAlarmChn->sAlarmDispatch.sAlarmPtz[j].nChn;
				//联动通道的编号
                nData = psAlarmChn->sAlarmDispatch.sAlarmPtz[j].nId;                

                //联动通道的通道号
                //sAlarmCbData.emAlarmEvent = emAlarmEvent;
                sAlarmCbData.nChn = j;//nChn;
                
				//常开/常闭时
				if ((1 == psAlarmChn->sEvent.nStatus)) 
				{
					//填充结构体
					sAlarmCbData.nData = nData;
					sAlarmCbData.emAlarmEvent = emAlarmEvent;
					sAlarmCbData.nTime = time(NULL);
					
					emLastAlarmEvent = emAlarmEvent; //记录此次命令,停止时能正确执行相应的命令
						
					//记录此次的联动编号
					nLastData[i][j] = nData;

					//对置点联动屏蔽掉,后边有5秒进行一次置点联动,防止第一次同时向云台发两次命令
					if (emAlarmEvent != EM_ALARM_EVENT_DISPATCH_PTZ_PRESET) 
					{
						//巡航联动
						if(emAlarmEvent == EM_ALARM_EVENT_DISPATCH_PTZ_PATROL)
						{
							if(psAlarmManager->nPtzTourId[sAlarmCbData.nChn] == 0xff)
							{
								psAlarmManager->nPtzTourId[sAlarmCbData.nChn] = sAlarmCbData.nData;
								psAlarmManager->nChnTouchPtzTour[sAlarmCbData.nChn] = i;
								pfnAlarmCb(&sAlarmCbData);
							}
						}
						else
						{
                					pfnAlarmCb(&sAlarmCbData);
						}
					}
				} 
				else //常闭
				{
					sAlarmCbData.nData = 0xff;//发停止的标志
					sAlarmCbData.emAlarmEvent = emLastAlarmEvent;
					sAlarmCbData.nTime = time(NULL);
					
					presetFlag[i][j] = 0;
					
					//巡航联动
                	if(emAlarmEvent == EM_ALARM_EVENT_DISPATCH_PTZ_PATROL)
					{
						if(psAlarmManager->nChnTouchPtzTour[sAlarmCbData.nChn] == i)
						{
							psAlarmManager->nPtzTourId[sAlarmCbData.nChn] = 0xff;
							psAlarmManager->nChnTouchPtzTour[sAlarmCbData.nChn] = 0xff;
							pfnAlarmCb(&sAlarmCbData);
						}
					}
					else
					{
						pfnAlarmCb(&sAlarmCbData);
					}
				}
            }

			//记录本次事件状态
			pLastStatus[i] = psAlarmChn->sEvent.nStatus;
			
			if (1 == psAlarmChn->sEvent.nStatus) 
			{
				//记录本次各通道报警状态
				for (j = 0; j < sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz) / sizeof(SAlarmPtz); j++)
	            {
					nLastType[i][j] = psAlarmChn->sAlarmDispatch.sAlarmPtz[j].emALaPtzType;
				}
			} 
			else
			{
				for (j = 0; j < sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz) / sizeof(SAlarmPtz); j++)
	            {
					//各通道联动事件状态比较,并且本次通道本次报警状态为关时,则执行关操作
					//防止有一路无报警时把所有的联动都关闭情况
					if ((nLastType[i][j] != psAlarmChn->sAlarmDispatch.sAlarmPtz[j].emALaPtzType)
						&& nTmpType[i][j])
					{
						sAlarmCbData.nChn = j;
						sAlarmCbData.nData = 0xff;
						presetFlag[i][j] = 0;
						sAlarmCbData.emAlarmEvent = emLastAlarmEvent;
						sAlarmCbData.nTime = time(NULL);
	                	pfnAlarmCb(&sAlarmCbData);
					}
				}
			}
        }	

		//状态为报警时,云台联动置点会5秒再次执行下置点控制
		//必须放在前边if条件状态的判断后边来执行此if判断,这边才能保证在开启状态时还会一直保持置点运转
		if (psAlarmChn->sEvent.nStatus
			&& (emLastAlarmEvent == EM_ALARM_EVENT_DISPATCH_PTZ_PRESET))
		{
			now = time(NULL);
			//5秒自动执行下置点联动
			if (now - past > 5)
			{
				for (j = 0; j < sizeof(psAlarmChn->sAlarmDispatch.sAlarmPtz) / sizeof(SAlarmPtz); j++)
	            {
					//判断各通道的联动事件置点是否为1
					if (presetFlag[i][j] && (EM_ALARM_PTZ_PRESET == nLastType[i][j])) 
					{
		                nData = psAlarmChn->sAlarmDispatch.sAlarmPtz[j].nId;		                
		                sAlarmCbData.emAlarmEvent = emLastAlarmEvent;
		                sAlarmCbData.nChn = j;
						sAlarmCbData.nData = nLastData[i][j];
		                sAlarmCbData.nTime = time(NULL);
		                pfnAlarmCb(&sAlarmCbData);
		                past = now;						
		            }
				}
			}
		}		
    }
}

void ZoomDispatch(SAlarmManager *psAlarmManager)
{
	SAlarmEvent *psAlarmEvent;
	u8 nChannels = psAlarmManager->sVMotions.nChannels;
	if(0 == nChannels)
	{
		return;
	}
	
	static u8* pnStatusSensorZoom = NULL;
	static u8* pnStatusIPCExtSensorZoom = NULL;
	static u8* pnStatus485ExtSensorZoom = NULL;
	static u8* pnStatusIPCCoverZoom = NULL;
	static u8* pnStatusVMotionZoom = NULL;
	static u8* pnStatusVLostZoom = NULL;
	static u8* pnStatusVBlindZoom = NULL;
	static u8 flag = 0;
	
	if(flag == 0)
	{
		pnStatusSensorZoom = malloc(sizeof(u8) * psAlarmManager->sSensors.nChannels);
		if(NULL == pnStatusSensorZoom)
		{
			return;
		}
		memset(pnStatusSensorZoom, 0, sizeof(u8) * psAlarmManager->sSensors.nChannels);
		
		pnStatusVMotionZoom = malloc(sizeof(u8) * nChannels);
		if(NULL == pnStatusVMotionZoom)
		{
			free(pnStatusSensorZoom);
			return;
		}
		memset(pnStatusVMotionZoom, 0, sizeof(u8) * nChannels);
		
		pnStatusVLostZoom = malloc(sizeof(u8) * nChannels);
		if(NULL == pnStatusVLostZoom)
		{
			free(pnStatusSensorZoom);
			free(pnStatusVMotionZoom);
			return;
		}
		memset(pnStatusVLostZoom, 0, sizeof(u8) * nChannels);
		
		pnStatusVBlindZoom = malloc(sizeof(u8) * nChannels);
		if(NULL == pnStatusVBlindZoom)
		{
			free(pnStatusSensorZoom);
			free(pnStatusVMotionZoom);
			free(pnStatusVLostZoom);
			return;
		}
		memset(pnStatusVBlindZoom, 0, sizeof(u8) * nChannels);

		pnStatusIPCExtSensorZoom = malloc(sizeof(u8) * nChannels);
		if(NULL == pnStatusIPCExtSensorZoom)
		{
			free(pnStatusSensorZoom);
			free(pnStatusVMotionZoom);
			free(pnStatusVLostZoom);
			free(pnStatusVBlindZoom);
			return;
		}
		memset(pnStatusIPCExtSensorZoom, 0, sizeof(u8) * nChannels);

		pnStatusIPCCoverZoom = malloc(sizeof(u8) * nChannels);
		if(NULL == pnStatusIPCCoverZoom)
		{
			free(pnStatusSensorZoom);
			free(pnStatusVMotionZoom);
			free(pnStatusVLostZoom);
			free(pnStatusVBlindZoom);
			free(pnStatusIPCExtSensorZoom);
			return;
		}
		memset(pnStatusIPCCoverZoom, 0, sizeof(u8) * nChannels);

		pnStatus485ExtSensorZoom = malloc(sizeof(u8) * nChannels);
		if(NULL == pnStatus485ExtSensorZoom)
		{
			free(pnStatusSensorZoom);
			free(pnStatusVMotionZoom);
			free(pnStatusVLostZoom);
			free(pnStatusVBlindZoom);
			free(pnStatusIPCExtSensorZoom);
			free(pnStatusIPCCoverZoom);
			return;
		}
		memset(pnStatus485ExtSensorZoom, 0, sizeof(u8) * psAlarmManager->s485ExtSensors.nChannels);
		
		flag = 1;
	}
	
	#if 1
	psAlarmEvent = &psAlarmManager->sVMotions;
	EventDispatchZoom(psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusVMotionZoom);
	
	psAlarmEvent = &psAlarmManager->sSensors;
	EventDispatchZoom(psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusSensorZoom);

	psAlarmEvent = &psAlarmManager->sIPCExtSensors;
	EventDispatchZoom(psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusIPCExtSensorZoom);

	psAlarmEvent = &psAlarmManager->sIPCCover;
	EventDispatchZoom(psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusIPCCoverZoom);
	
	psAlarmEvent = &psAlarmManager->sVBlinds;
	EventDispatchZoom(psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusVBlindZoom);

	psAlarmEvent = &psAlarmManager->s485ExtSensors;
	EventDispatchZoom(psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatus485ExtSensorZoom);
	#endif
	
	psAlarmEvent = &psAlarmManager->sVLosts;
	EventDispatchZoom(psAlarmEvent, psAlarmManager->pfnAlarmCb, pnStatusVLostZoom);
}

void EventDispatchZoom(SAlarmEvent *psAlarmEvent,  FNALARMCB pfnAlarmCb, u8* pLastStatus)
{
	int i;
	SAlarmChn* psAlarmChn;
	static time_t past = 0;
	time_t now = 0;
	
	for(i = 0; i <  psAlarmEvent->nChannels; i++)
	{
		psAlarmChn = &psAlarmEvent->psAlarmChn[i];
		
		SAlarmCbData sAlarmCbData;
		sAlarmCbData.emAlarmEvent = EM_ALARM_EVENT_DISPATCH_ZOOMCHN;
		sAlarmCbData.nChn = i;
		
		if((pLastStatus[i] != psAlarmChn->sEvent.nStatus)//(psAlarmChn->sEvent.nStatusLast != psAlarmChn->sEvent.nStatus)
			//&& (1 == psAlarmChn->sEvent.nStatus)
			//&& (0xff != psAlarmChn->sAlarmDispatch.nZoomChn)
			)
		{
			if(1 == psAlarmChn->sEvent.nStatus)
			{
				if(0xff != psAlarmChn->sAlarmDispatch.nZoomChn)
				{
					sAlarmCbData.nData = psAlarmChn->sAlarmDispatch.nZoomChn;
					sAlarmCbData.nTime = time(NULL);
					//printf("chn%d zoom chn%d\n",i,psAlarmChn->sAlarmDispatch.nZoomChn);
					pfnAlarmCb(&sAlarmCbData);
					
					past = time(NULL);//csp modify
				}
			}
			else
			{
				if(0xff != psAlarmChn->sAlarmDispatch.nZoomChn)//csp modify
				{
					sAlarmCbData.nData = 0xff;
					sAlarmCbData.nTime = time(NULL);
					//printf("chn%d disable zoom\n",i);
					pfnAlarmCb(&sAlarmCbData);
					
					past = time(NULL);//csp modify
				}
			}
			
			pLastStatus[i] = psAlarmChn->sEvent.nStatus;
			
			continue;//csp modify
		}
		
		if(1 == psAlarmChn->sEvent.nStatus)
		{
			now = time(NULL);
			//printf("chn%d nStatus:%d,now=%ld,past=%ld,span=%ld\n",i,psAlarmChn->sEvent.nStatus,now,past,now-past);
			if(now - past > 5)
			{
				if(0xff != psAlarmChn->sAlarmDispatch.nZoomChn)
				{
					sAlarmCbData.nData = psAlarmChn->sAlarmDispatch.nZoomChn;
					sAlarmCbData.nTime = time(NULL);
					//printf("[chn%d zoom chn%d]\n",i,psAlarmChn->sAlarmDispatch.nZoomChn);
					pfnAlarmCb(&sAlarmCbData);
					
					past = now;//csp modify
				}
				
				//past = now;//csp modify
			}
		}
	}
}

u8 CheckHardDiskExist(void)
{
    HDDHDR hddHdr = PublicGetHddManage();
	
	if(hddHdr)
	{
		int i = 0;
		for (i = 0; i < MAX_HDD_NUM; ++i) 
		{
			if ((((disk_manager *)hddHdr)->hinfo[i].is_disk_exist) 
				&& (((disk_manager *)hddHdr)->hinfo[i].storage_type != 'u')) 
	        {
				return 1;
			}
		}
	}
	return 0;
}

static int g_tz_offset = 8*3600;

s32 ModAlarmSetTimeZoneOffset(int nOffset)
{
	g_tz_offset = nOffset;
	return g_tz_offset;
}

s32 ModAlarmGetTimeZoneOffset()
{
	return g_tz_offset;
}

