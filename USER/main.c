#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "led.h"
#include "key.h"
#include "lcd.h"
#include "ltdc.h"
#include "sdram.h"
#include "w25qxx.h"
#include "nand.h"  
#include "mpu.h"
#include "ov2640.h"
#include "pcf8574.h"
#include "dcmi.h"
#include "sdmmc_sdcard.h"
#include "usmart.h"
#include "malloc.h"
#include "ff.h"
#include "exfuns.h"
#include "fontupd.h"
#include "text.h"
#include "piclib.h"	
#include "string.h"		
#include "math.h"
#include "app_x-cube-ai.h"
/************************************************
 ALIENTEK 阿波罗STM32F7开发板 实验46
 照相机实验-HAL库函数版
 技术支持：www.openedv.com
 淘宝店铺：http://eboard.taobao.com 
 关注微信公众平台微信号："正点原子"，免费获取STM32资料。
 广州市星翼电子科技有限公司  
 作者：正点原子 @ALIENTEK
************************************************/

vu8 bmp_request=0;						//bmp拍照请求:0,无bmp拍照请求;1,有bmp拍照请求,需要在帧中断里面,关闭DCMI接口.
u8 ovx_mode=0;							//bit0:0,RGB565模式;1,JPEG模式 
u16 curline=0;							//摄像头输出数据,当前行编号
u16 yoffset=0;							//y方向的偏移量

#define jpeg_buf_size   4*1024*1024		//定义JPEG数据缓存jpeg_buf的大小(4M字节)
#define jpeg_line_size	2*1024			//定义DMA接收数据时,一行数据的最大值
#define output_width 32
#define output_height 40

u32 *dcmi_line_buf[2];					//RGB屏时,摄像头采用一行一行读取,定义行缓存  
u32 *jpeg_data_buf;						//JPEG数据缓存buf 

volatile u32 jpeg_data_len=0; 			//buf中的JPEG有效数据长度 
volatile u8 jpeg_data_ok=0;				//JPEG数据采集完成标志 
volatile u8 currentline = 0;
volatile u8 one_shot_ok = 0;


//RGB屏数据接收回调函数
void rgblcd_dcmi_rx_callback(void)
{  
	u16 *pbuf;
	if(DMADMCI_Handler.Instance->CR&(1<<19))//DMA使用buf1,读取buf0
	{ 
		pbuf=(u16*)dcmi_line_buf[0]; 
	}else 						//DMA使用buf0,读取buf1
	{
		pbuf=(u16*)dcmi_line_buf[1]; 
	} 	
	LTDC_Color_Fill(0,curline,lcddev.width-1,curline,pbuf);//DM2D填充 
	if(curline<lcddev.height)curline++;
	if(bmp_request==1&&curline==(lcddev.height-1))//有bmp拍照请求,关闭DCMI
	{
		DCMI_Stop();	//停止DCMI
		bmp_request=0;	//标记请求处理完成.
	}
}

void out_dcmi_rx_callback(void){
	u16 *pbuf;
	u16 * databuf =(u16*)jpeg_data_buf;
	databuf = databuf+currentline*output_width;
	u8 i ;
	
	if(DMADMCI_Handler.Instance->CR&(1<<19))
	{
		pbuf = (u16*)dcmi_line_buf[0];
	}
	else {
		pbuf = (u16*)dcmi_line_buf[1];
	}
	
	for(i=0;i<output_width; i++){
		databuf[i] = pbuf[i];
	}
	LED0_Toggle;
	currentline++;
	
}

//切换为OV2640模式
void sw_ov2640_mode(void)
{  
  GPIO_InitTypeDef GPIO_Initure;
 	OV2640_PWDN_Set(0); //OV2640 Power Up 
	//GPIOC8/9/11切换为 DCMI接口
  GPIO_Initure.Pin=GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_11;  
  GPIO_Initure.Mode=GPIO_MODE_AF_PP;          //推挽复用
  GPIO_Initure.Pull=GPIO_PULLUP;              //上拉
  GPIO_Initure.Speed=GPIO_SPEED_HIGH;         //高速
  GPIO_Initure.Alternate=GPIO_AF13_DCMI;      //复用为DCMI   
  HAL_GPIO_Init(GPIOC,&GPIO_Initure);         //初始化 

} 
//切换为SD卡模式
void sw_sdcard_mode(void)
{
  GPIO_InitTypeDef GPIO_Initure;
	OV2640_PWDN_Set(1); //OV2640 Power Down  
 	//GPIOC8/9/11切换为 SDIO接口
 	//GPIOC8/9/11切换为 SDIO接口
  GPIO_Initure.Pin=GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_11;  
  GPIO_Initure.Mode=GPIO_MODE_AF_PP;          //推挽复用
  GPIO_Initure.Pull=GPIO_PULLUP;              //上拉
  GPIO_Initure.Speed=GPIO_SPEED_HIGH;         //高速
  GPIO_Initure.Alternate=GPIO_AF12_SDMMC1;    //复用为SDIO  
  HAL_GPIO_Init(GPIOC,&GPIO_Initure);   
}
//文件名自增（避免覆盖）
//mode:0,创建.bmp文件;1,创建.jpg文件.
//bmp组合成:形如"0:PHOTO/PIC13141.bmp"的文件名
//jpg组合成:形如"0:PHOTO/PIC13141.jpg"的文件名
void camera_new_pathname(u8 *pname,u8 mode)
{	 
	u8 res;					 
	u16 index=0;
	while(index<0XFFFF)
	{
		if(mode==0)sprintf((char*)pname,"0:PHOTO/PIC%05d.bmp",index);
		else sprintf((char*)pname,"0:PHOTO/PIC%05d.jpg",index);
		res=f_open(ftemp,(const TCHAR*)pname,FA_READ);//尝试打开这个文件
		if(res==FR_NO_FILE)break;		//该文件名不存在=正是我们需要的.
		index++;
	}
}  


int main(void)
{
	u8 res;							 
	u8 *pname;					//带路径的文件名 	   
	u8 i , j;						 
	u16 *ouputpointer;
  Write_Through();                //开启强制透写！
  Cache_Enable();                 //打开L1-Cache
  MPU_Memory_Protection();        //保护相关存储区域
  HAL_Init();				        //初始化HAL库
  Stm32_Clock_Init(432,25,2,9);   //设置时钟,216Mhz 
  delay_init(216);                //延时初始化
  uart_init(115200);		        //串口初始化
	printf("hello world\r\n");
	
  usmart_dev.init(108); 		    //初始化USMART
  LED_Init();                     //初始化LED 
  KEY_Init();                     //初始化按键
  SDRAM_Init();                   //SDRAM初始化
  LCD_Init();                     //LCD初始化
  OV2640_Init();				    //初始化OV2640
  sw_sdcard_mode();			    //首先切换为SD卡模式
  PCF8574_Init();                 //初始化PCF8574
  W25QXX_Init();                  //初始化W25Q256
  my_mem_init(SRAMIN);            //初始化内部内存池
  my_mem_init(SRAMEX);            //初始化外部SDRAM内存池
  my_mem_init(SRAMDTCM);           //初始化内部CCM内存池
  POINT_COLOR=RED;  
  exfuns_init();		            //为fatfs相关变量申请内存  
  f_mount(fs[0],"0:",1);          //挂载SD卡 
 	f_mount(fs[1],"1:",1);          //挂载SPI FLASH. 

	while(font_init()) 		        //检查字库
	{	    
		LCD_ShowString(30,50,200,16,16,"Font Error!");
		delay_ms(200);				  
		LCD_Fill(30,50,240,66,WHITE);//清除显示	     
		delay_ms(200);				  
	}  	 
 	Show_Str(30,50,200,16,"阿波罗STM32F4/F7开发板",16,0);	 			    	 
	Show_Str(30,70,200,16,"OV2640照相机实验",16,0);				    	 
	Show_Str(30,90,200,16,"KEY0:拍照(bmp格式)",16,0);			    	 
	Show_Str(30,110,200,16,"KEY1:拍照(jpg格式)",16,0);			    	 
	Show_Str(30,130,200,16,"KEY2:自动对焦",16,0);					    	 
	Show_Str(30,150,200,16,"WK_UP:FullSize/Scale",16,0);				    	 
	Show_Str(30,170,200,16,"2016年9月31日",16,0);
    res=f_mkdir("0:/PHOTO");		//创建PHOTO文件夹
	if(res!=FR_EXIST&&res!=FR_OK) 	//发生了错误
	{		
		res=f_mkdir("0:/PHOTO");		//创建PHOTO文件夹		
		Show_Str(30,190,240,16,"SD卡错误!",16,0);
		delay_ms(200);				  
		Show_Str(30,190,240,16,"拍照功能将不可用!",16,0);
		delay_ms(200);				 	
	}
  dcmi_line_buf[0]=mymalloc(SRAMIN,jpeg_line_size*4);	//为jpeg dma接收申请内存	
	dcmi_line_buf[1]=mymalloc(SRAMIN,jpeg_line_size*4);	//为jpeg dma接收申请内存	
	jpeg_data_buf=mymalloc(SRAMEX,jpeg_buf_size);		//为jpeg文件申请内存(最大4MB)
 	pname=mymalloc(SRAMIN,30);//为带路径的文件名分配30个字节的内存	 
 	while(pname==NULL||!dcmi_line_buf[0]||!dcmi_line_buf[1]||!jpeg_data_buf)	//内存分配出错
 	{	    
		Show_Str(30,190,240,16,"内存分配失败!",16,0);
		delay_ms(200);				  
		LCD_Fill(30,190,240,146,WHITE);//清除显示	     
		delay_ms(200);				  
	}   
	while(OV2640_Init())//初始化OV2640
	{
		Show_Str(30,190,240,16,"OV2640 错误!",16,0);
		delay_ms(200);
	    LCD_Fill(30,190,239,206,WHITE);
		delay_ms(200);
	}
    Show_Str(30,210,230,16,"OV2640 正常",16,0); 
	//自动对焦初始化
	OV2640_RGB565_Mode();	//RGB565模式 
	OV2640_Light_Mode(0);	//自动模式
	OV2640_Color_Saturation(3);//色彩饱和度0
	OV2640_Brightness(4);	//亮度0
	OV2640_Contrast(3);		//对比度0
	OV2640_Special_Effects(2); //设置成黑白
	DCMI_Init();			//DCMI配置

	dcmi_rx_callback=out_dcmi_rx_callback;
	DCMI_DMA_Init((u32)dcmi_line_buf[0],(u32)dcmi_line_buf[1],output_width/2,DMA_MDATAALIGN_HALFWORD,DMA_MINC_ENABLE);//DCMI DMA配置  
	
 
	currentline = 0 ;
	one_shot_ok = 0;
	OV2640_ImageWin_Set(0,0,1600,1184); // 1600 = 40 * 40 , 1184 = 32 *37
	printf("ov2640_outsize_set_ret:%d \r\n",OV2640_OutSize_Set(output_width,output_height));	//满屏缩放显示
	LCD_Clear(BLACK);
	DCMI_Start(); 			//启动传输 
  while(one_shot_ok!=1)
	{
				delay_ms(10);
	}	
	ouputpointer = (u16*)jpeg_data_buf;
	for(i=0;i < output_height ; i++){
			for(j=0 ; j < output_width ; j++){
				printf("%x ",ouputpointer[i*output_width+j]);
			}
	printf("\r\n");
			
	}
	printf("over");
	
}
