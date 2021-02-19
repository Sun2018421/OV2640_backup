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
 ALIENTEK ������STM32F7������ ʵ��46
 �����ʵ��-HAL�⺯����
 ����֧�֣�www.openedv.com
 �Ա����̣�http://eboard.taobao.com 
 ��ע΢�Ź���ƽ̨΢�źţ�"����ԭ��"����ѻ�ȡSTM32���ϡ�
 ������������ӿƼ����޹�˾  
 ���ߣ�����ԭ�� @ALIENTEK
************************************************/

vu8 bmp_request=0;						//bmp��������:0,��bmp��������;1,��bmp��������,��Ҫ��֡�ж�����,�ر�DCMI�ӿ�.
u8 ovx_mode=0;							//bit0:0,RGB565ģʽ;1,JPEGģʽ 
u16 curline=0;							//����ͷ�������,��ǰ�б��
u16 yoffset=0;							//y�����ƫ����

#define jpeg_buf_size   4*1024*1024		//����JPEG���ݻ���jpeg_buf�Ĵ�С(4M�ֽ�)
#define jpeg_line_size	2*1024			//����DMA��������ʱ,һ�����ݵ����ֵ
#define output_width 32
#define output_height 40

u32 *dcmi_line_buf[2];					//RGB��ʱ,����ͷ����һ��һ�ж�ȡ,�����л���  
u32 *jpeg_data_buf;						//JPEG���ݻ���buf 

volatile u32 jpeg_data_len=0; 			//buf�е�JPEG��Ч���ݳ��� 
volatile u8 jpeg_data_ok=0;				//JPEG���ݲɼ���ɱ�־ 
volatile u8 currentline = 0;
volatile u8 one_shot_ok = 0;


//RGB�����ݽ��ջص�����
void rgblcd_dcmi_rx_callback(void)
{  
	u16 *pbuf;
	if(DMADMCI_Handler.Instance->CR&(1<<19))//DMAʹ��buf1,��ȡbuf0
	{ 
		pbuf=(u16*)dcmi_line_buf[0]; 
	}else 						//DMAʹ��buf0,��ȡbuf1
	{
		pbuf=(u16*)dcmi_line_buf[1]; 
	} 	
	LTDC_Color_Fill(0,curline,lcddev.width-1,curline,pbuf);//DM2D��� 
	if(curline<lcddev.height)curline++;
	if(bmp_request==1&&curline==(lcddev.height-1))//��bmp��������,�ر�DCMI
	{
		DCMI_Stop();	//ֹͣDCMI
		bmp_request=0;	//������������.
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

//�л�ΪOV2640ģʽ
void sw_ov2640_mode(void)
{  
  GPIO_InitTypeDef GPIO_Initure;
 	OV2640_PWDN_Set(0); //OV2640 Power Up 
	//GPIOC8/9/11�л�Ϊ DCMI�ӿ�
  GPIO_Initure.Pin=GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_11;  
  GPIO_Initure.Mode=GPIO_MODE_AF_PP;          //���츴��
  GPIO_Initure.Pull=GPIO_PULLUP;              //����
  GPIO_Initure.Speed=GPIO_SPEED_HIGH;         //����
  GPIO_Initure.Alternate=GPIO_AF13_DCMI;      //����ΪDCMI   
  HAL_GPIO_Init(GPIOC,&GPIO_Initure);         //��ʼ�� 

} 
//�л�ΪSD��ģʽ
void sw_sdcard_mode(void)
{
  GPIO_InitTypeDef GPIO_Initure;
	OV2640_PWDN_Set(1); //OV2640 Power Down  
 	//GPIOC8/9/11�л�Ϊ SDIO�ӿ�
 	//GPIOC8/9/11�л�Ϊ SDIO�ӿ�
  GPIO_Initure.Pin=GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_11;  
  GPIO_Initure.Mode=GPIO_MODE_AF_PP;          //���츴��
  GPIO_Initure.Pull=GPIO_PULLUP;              //����
  GPIO_Initure.Speed=GPIO_SPEED_HIGH;         //����
  GPIO_Initure.Alternate=GPIO_AF12_SDMMC1;    //����ΪSDIO  
  HAL_GPIO_Init(GPIOC,&GPIO_Initure);   
}
//�ļ������������⸲�ǣ�
//mode:0,����.bmp�ļ�;1,����.jpg�ļ�.
//bmp��ϳ�:����"0:PHOTO/PIC13141.bmp"���ļ���
//jpg��ϳ�:����"0:PHOTO/PIC13141.jpg"���ļ���
void camera_new_pathname(u8 *pname,u8 mode)
{	 
	u8 res;					 
	u16 index=0;
	while(index<0XFFFF)
	{
		if(mode==0)sprintf((char*)pname,"0:PHOTO/PIC%05d.bmp",index);
		else sprintf((char*)pname,"0:PHOTO/PIC%05d.jpg",index);
		res=f_open(ftemp,(const TCHAR*)pname,FA_READ);//���Դ�����ļ�
		if(res==FR_NO_FILE)break;		//���ļ���������=����������Ҫ��.
		index++;
	}
}  


int main(void)
{
	u8 res;							 
	u8 *pname;					//��·�����ļ��� 	   
	u8 i , j;						 
	u16 *ouputpointer;
  Write_Through();                //����ǿ��͸д��
  Cache_Enable();                 //��L1-Cache
  MPU_Memory_Protection();        //������ش洢����
  HAL_Init();				        //��ʼ��HAL��
  Stm32_Clock_Init(432,25,2,9);   //����ʱ��,216Mhz 
  delay_init(216);                //��ʱ��ʼ��
  uart_init(115200);		        //���ڳ�ʼ��
	printf("hello world\r\n");
	
  usmart_dev.init(108); 		    //��ʼ��USMART
  LED_Init();                     //��ʼ��LED 
  KEY_Init();                     //��ʼ������
  SDRAM_Init();                   //SDRAM��ʼ��
  LCD_Init();                     //LCD��ʼ��
  OV2640_Init();				    //��ʼ��OV2640
  sw_sdcard_mode();			    //�����л�ΪSD��ģʽ
  PCF8574_Init();                 //��ʼ��PCF8574
  W25QXX_Init();                  //��ʼ��W25Q256
  my_mem_init(SRAMIN);            //��ʼ���ڲ��ڴ��
  my_mem_init(SRAMEX);            //��ʼ���ⲿSDRAM�ڴ��
  my_mem_init(SRAMDTCM);           //��ʼ���ڲ�CCM�ڴ��
  POINT_COLOR=RED;  
  exfuns_init();		            //Ϊfatfs��ر��������ڴ�  
  f_mount(fs[0],"0:",1);          //����SD�� 
 	f_mount(fs[1],"1:",1);          //����SPI FLASH. 

	while(font_init()) 		        //����ֿ�
	{	    
		LCD_ShowString(30,50,200,16,16,"Font Error!");
		delay_ms(200);				  
		LCD_Fill(30,50,240,66,WHITE);//�����ʾ	     
		delay_ms(200);				  
	}  	 
 	Show_Str(30,50,200,16,"������STM32F4/F7������",16,0);	 			    	 
	Show_Str(30,70,200,16,"OV2640�����ʵ��",16,0);				    	 
	Show_Str(30,90,200,16,"KEY0:����(bmp��ʽ)",16,0);			    	 
	Show_Str(30,110,200,16,"KEY1:����(jpg��ʽ)",16,0);			    	 
	Show_Str(30,130,200,16,"KEY2:�Զ��Խ�",16,0);					    	 
	Show_Str(30,150,200,16,"WK_UP:FullSize/Scale",16,0);				    	 
	Show_Str(30,170,200,16,"2016��9��31��",16,0);
    res=f_mkdir("0:/PHOTO");		//����PHOTO�ļ���
	if(res!=FR_EXIST&&res!=FR_OK) 	//�����˴���
	{		
		res=f_mkdir("0:/PHOTO");		//����PHOTO�ļ���		
		Show_Str(30,190,240,16,"SD������!",16,0);
		delay_ms(200);				  
		Show_Str(30,190,240,16,"���չ��ܽ�������!",16,0);
		delay_ms(200);				 	
	}
  dcmi_line_buf[0]=mymalloc(SRAMIN,jpeg_line_size*4);	//Ϊjpeg dma���������ڴ�	
	dcmi_line_buf[1]=mymalloc(SRAMIN,jpeg_line_size*4);	//Ϊjpeg dma���������ڴ�	
	jpeg_data_buf=mymalloc(SRAMEX,jpeg_buf_size);		//Ϊjpeg�ļ������ڴ�(���4MB)
 	pname=mymalloc(SRAMIN,30);//Ϊ��·�����ļ�������30���ֽڵ��ڴ�	 
 	while(pname==NULL||!dcmi_line_buf[0]||!dcmi_line_buf[1]||!jpeg_data_buf)	//�ڴ�������
 	{	    
		Show_Str(30,190,240,16,"�ڴ����ʧ��!",16,0);
		delay_ms(200);				  
		LCD_Fill(30,190,240,146,WHITE);//�����ʾ	     
		delay_ms(200);				  
	}   
	while(OV2640_Init())//��ʼ��OV2640
	{
		Show_Str(30,190,240,16,"OV2640 ����!",16,0);
		delay_ms(200);
	    LCD_Fill(30,190,239,206,WHITE);
		delay_ms(200);
	}
    Show_Str(30,210,230,16,"OV2640 ����",16,0); 
	//�Զ��Խ���ʼ��
	OV2640_RGB565_Mode();	//RGB565ģʽ 
	OV2640_Light_Mode(0);	//�Զ�ģʽ
	OV2640_Color_Saturation(3);//ɫ�ʱ��Ͷ�0
	OV2640_Brightness(4);	//����0
	OV2640_Contrast(3);		//�Աȶ�0
	OV2640_Special_Effects(2); //���óɺڰ�
	DCMI_Init();			//DCMI����

	dcmi_rx_callback=out_dcmi_rx_callback;
	DCMI_DMA_Init((u32)dcmi_line_buf[0],(u32)dcmi_line_buf[1],output_width/2,DMA_MDATAALIGN_HALFWORD,DMA_MINC_ENABLE);//DCMI DMA����  
	
 
	currentline = 0 ;
	one_shot_ok = 0;
	OV2640_ImageWin_Set(0,0,1600,1184); // 1600 = 40 * 40 , 1184 = 32 *37
	printf("ov2640_outsize_set_ret:%d \r\n",OV2640_OutSize_Set(output_width,output_height));	//����������ʾ
	LCD_Clear(BLACK);
	DCMI_Start(); 			//�������� 
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
