//This program uses the RTC to display time on a 4 digit 7 segment display
//When the alarm triggers, it plays mp3 files through a USB connected on the
//micro USB port

#include "stm32f4xx_rtc.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_pwr.h"
#include "stm32f4xx_dac.h"
#include "stm32f4xx_tim.h"
#include "misc.h"
#include "stm32f4xx_exti.h"
#include "audioMP3.h"
#include "timeKeeping.h"
#include "main.h"

//structures
RCC_ClocksTypeDef RCC_Clocks;
GPIO_InitTypeDef	GPIOInitStruct;
TIM_TimeBaseInitTypeDef TIM_InitStruct;
NVIC_InitTypeDef NVIC_InitStructure;
EXTI_InitTypeDef EXTI_InitStructure;
I2C_InitTypeDef I2C_InitStruct;


//function prototypes
void configuration(void);
void display7Seg(void);
void setTime(void);
void setAlarm(void);
void snooze(void);
void getCurrentTime(void);
void set24Hour(void);
void buttonControls(void);


//global variables
int time[4];
int counter=0;
int interruptOccurred = 0;
int AlarmPmFlag = 0;
int TimePmFlag = 0;
int hour24Flag = 0;
unsigned int debouncing = 0;
int buttonState = 0;
int buttonFlag = 0;

/*
extern volatile int exitMp3 = 0;
extern volatile int mp3PlayingFlag = 0;
extern volatile int snoozeMemory = 0;
*/

/*for testing
uint32_t *ptr;
uint32_t *ptr2;*/

int main(void)
{

  configuration();

  // This flag should be set to zero AFTER you have debugged your MP3 player code
  // In your final code, this will be set to 1 when the alarm time is reached in the
  // RTC_ALARM_IRQHandler function.
  // It is set to one here for testing purposes only.
  interruptOccurred = 1;

  //continuously loops checking for the alarm interrupt
  while(1)
  {
	//	 mp3PlayingFlag = 1;
	//	 audioToMp3();

	  //checks for alarm A interrupt and calls the mp3 function
	  if(1 == interruptOccurred && mp3PlayingFlag == 0)
	  {

		 interruptOccurred = 0;
		 mp3PlayingFlag = 1;
		 audioToMp3();
	  }

	  //ensures the alarm is not already playing
	  else if(1 == interruptOccurred && mp3PlayingFlag == 1)
	  {
		 interruptOccurred = 0;
	  }

  }

}

//timer interrupt handler that is called at a rate of 500Hz
//this function gets the time and displays it on the 7 segment display
//it also checks for button presses, debounces, and handles each case
void TIM5_IRQHandler(void)
{
	int previousState = 0;

	//double checks that interrupt has occurred
	if( TIM_GetITStatus( TIM5, TIM_IT_Update ) != RESET )
	{

		getCurrentTime();

		//displays as either 24 or 12 hour
		if(1 == hour24Flag)
		{
			set24Hour();
		}

		display7Seg();

		buttonControls();

		//toggles 12 and 24 hour modes
		if((buttonFlag == 1) && (buttonState == MODEBUTTON))
		{
			  if(0 == hour24Flag)
			  {
				  //sets a flag for the display function to look for
				  hour24Flag = 1;
			  }
			  else
			  {
				  hour24Flag = 0;
			  }

			  //stops the statement from running continuously
			  buttonFlag = 2;
		}


		//if snooze button is pressed cancels the mp3 and sets alarm 10 minutes later
		if((buttonFlag == 1) && (buttonState == SNOOZEBUTTON))
		{
			if(mp3PlayingFlag == 1)
			{
			  exitMp3 = 1;
			  snooze();
			}
			buttonFlag = 2;
		}

		//if the reset button is pressed without mp3 playing, it turns off and on the alarm
		//otherwise it cancels the mp3
		if((buttonFlag == 1) && (buttonState == BUTTON))
		{
			if(mp3PlayingFlag == 0)  //if not playing
			{

				//toggle alarm
				if(0 != (0x100 & RTC->CR))  //if alarm is on
				{
					RTC_AlarmCmd(RTC_Alarm_A,DISABLE);

				}

				//if alarm is off
				else
				{
					RTC_AlarmCmd(RTC_Alarm_A,ENABLE);
					RTC_ClearFlag(RTC_FLAG_ALRAF);
				}


			 }

			 else
			 {
				 exitMp3 = 1;
			 }

			 //checks if snooze has been used before, resets the alarm to original time
			 if(1 == snoozeMemory)
			 {
				 AlarmStruct.RTC_AlarmTime = alarmMemory.RTC_AlarmTime;
				 RTC_SetAlarm(RTC_Format_BCD,RTC_Alarm_A,&AlarmStruct);
				 snoozeMemory = 0;
			 }
			 buttonFlag = 2;
		 }

		 //used to set the time
		 //while holding down the set time button, pressing the either the increment minute or increment hour
		 //button will add 1 min or hour to the current time respectively.
		 if((buttonFlag == 1) && (buttonState == TIMEHOURBUTTON || buttonState == TIMEMINBUTTON))
		 {
			 setTime();
			 buttonFlag = 2;
		 }


		 //set alarm time
		 //works similar to the process of setting the time, but works when pushing the set alarm button
		 if((buttonFlag == 1) && (buttonState == ALARMHOURBUTTON || buttonState == ALARMMINBUTTON))
		 {

			 //checks to see if the alarm was on or off previously
			 if(0 != (0x100 & RTC->CR))
			 {
				  previousState = 1;
			 }

			 setAlarm();

			 //if the alarm was on, it is re-enabled
			 if(1==previousState)
			 {
				 RTC_AlarmCmd(RTC_Alarm_A,ENABLE);
				 RTC_ClearFlag(RTC_FLAG_ALRAF);
				 previousState = 0;
		     }

			 buttonFlag = 2;
		  }

	     //clears interrupt flag
	     TIM5->SR = (uint16_t)~TIM_IT_Update;

    }
}

//alarm A interrupt handler
//when alarm occurs, clear all the interrupt bits and flags
//then set the flag to play mp3
void RTC_Alarm_IRQHandler(void)
{

	//resets alarm flags and sets flag to play mp3
	  if(RTC_GetITStatus(RTC_IT_ALRA) != RESET)
	  {
    	RTC_ClearFlag(RTC_FLAG_ALRAF);
	    RTC_ClearITPendingBit(RTC_IT_ALRA);
	    EXTI_ClearITPendingBit(EXTI_Line17);
		interruptOccurred = 1;

	  }


}

//displays the current clock or alarm time
void display7Seg(void)
{
	// Note that writing to D4 will affect communication with CS43L22 chip.
	// Writing to D15,D14,D13 will affect the indicator LEDs on the STM32F4 board.
	//clears display
	GPIOD->BSRRH = 0x1FEF;
    GPIOE->BSRRH = 0xFFFF;

}

//used to increment time values
void setTime(void)
{
	// You can set the time with calls to the following function:
	// RTC_SetTime(RTC_Format_BCD, &myclockTimeStruct);
	// Read manuals for format and entries.
}

//used to increment alarm values
void setAlarm(void)
{
	// Set alarm here.
	// Note that changing the alarm time when the alarm is active will cause
	// the real time clock to enter an inconsistent state and the alarm may fail.
	// The proper procedure is:
	// 1. RTC_AlarmCmd(RTC_AlarmA,DISABLE);
	// 2. RTC_SetAlarm(RTC_Format_BCD, RTC_Alarm_A, &AlarmStruct)
	//     where AlarmStruct hold your new alarm time.
	// 3. RTC_AlarmCmd(RTC_AlarmA,ENABLE);
	//     if you wish to reactivate the alarm.
	// 4. RTC_ClearFlag(RTC_FLAG_ALRAF);
	//     to clear alarm flag if needed.
}

//adds 10 minutes to the current time and checks to make sure that time is
//acceptable for a 12 hour clock.  It also stores the time when the button is
//first pressed so when the alarm is cancelled the alarm time goes back to
//the original
void snooze(void)
{


}

//called by the timer interrupt to separate the BCD time values
void getCurrentTime(void)
{

	// Obtain current time.
	RTC_GetTime(RTC_Format_BCD, &myclockTimeStruct);

}


//converts the 12 hour time to 24 hour when displaying the time
//or alarm values
void set24Hour(void)
{

}

//called every timer interrupt.  checks for changes on the
//input data register and then debounces and sets which button is pressed
void buttonControls(void)
{
	static uint16_t oldState = 0;
	static uint16_t newState = 0;

	//gets the current register value
	newState = GPIOC->IDR & NOBUTTON;

	//compares it to the previous register value
	//if they are different, reset the debug count
	if(oldState != newState)
	{
		debouncing = 0;
	}

	//otherwise increment the count
	else
	{
		debouncing++;
	}

	// Do your debouncing processing here.
}


//configures the clocks, gpio, alarm, interrupts etc.
void configuration(void)
{

	//lets the system clocks be viewed
	RCC_GetClocksFreq(&RCC_Clocks);

	//enable peripheral clocks
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM5, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);	// Needed for Audio chip

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM8, ENABLE);

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

	//enable the RTC
	PWR_BackupAccessCmd(DISABLE);
	PWR_BackupAccessCmd(ENABLE);
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);
	RCC_RTCCLKCmd(ENABLE);
	RTC_AlarmCmd(RTC_Alarm_A,DISABLE);

	//Enable the LSI OSC
	RCC_LSICmd(ENABLE);

	//Wait till LSI is ready
	while(RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET);

	//enable the external interrupt for the RTC to use the Alarm
	// EXTI configuration
	EXTI_ClearITPendingBit(EXTI_Line17);
	EXTI_InitStructure.EXTI_Line = EXTI_Line17;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);


	//FOR TESTING
	/*
	GPIO_StructInit( &GPIOInitStruct );
	GPIOInitStruct.GPIO_Pin = GPIO_Pin_13;
	GPIOInitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIOInitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIOInitStruct);
	*/

	//set timer 5 to interrupt at a rate of 500Hz
	TIM_TimeBaseStructInit(&TIM_InitStruct);
	TIM_InitStruct.TIM_Period	=  8000;	// 500Hz
	TIM_InitStruct.TIM_Prescaler = 20;
	TIM_TimeBaseInit(TIM5, &TIM_InitStruct);

	// Enable the TIM5 global Interrupt
	NVIC_Init( &NVIC_InitStructure );
	NVIC_InitStructure.NVIC_IRQChannel = TIM5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init( &NVIC_InitStructure );

	//setup the RTC for 12 hour format
	myclockInitTypeStruct.RTC_HourFormat = RTC_HourFormat_12;
	myclockInitTypeStruct.RTC_AsynchPrediv = 127;
	myclockInitTypeStruct.RTC_SynchPrediv = 0x00FF;
	RTC_Init(&myclockInitTypeStruct);

	//set the time displayed on power up to 12PM
	myclockTimeStruct.RTC_H12 = RTC_H12_PM;
	myclockTimeStruct.RTC_Hours = 0x012;
	myclockTimeStruct.RTC_Minutes = 0x00;
	myclockTimeStruct.RTC_Seconds = 0x00;
	RTC_SetTime(RTC_Format_BCD, &myclockTimeStruct);


	//sets alarmA for 12:00AM, date doesn't matter
	AlarmStruct.RTC_AlarmTime.RTC_H12 = RTC_H12_AM;
	AlarmStruct.RTC_AlarmTime.RTC_Hours = 0x12;
	AlarmStruct.RTC_AlarmTime.RTC_Minutes = 0x00;
	AlarmStruct.RTC_AlarmTime.RTC_Seconds = 0x00;
	AlarmStruct.RTC_AlarmMask = RTC_AlarmMask_DateWeekDay;
	RTC_SetAlarm(RTC_Format_BCD,RTC_Alarm_A,&AlarmStruct);

	// Enable the Alarm global Interrupt
	NVIC_Init( &NVIC_InitStructure );
	NVIC_InitStructure.NVIC_IRQChannel = RTC_Alarm_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init( &NVIC_InitStructure );

	// Pins B6 and B9 are used for I2C communication, configure as alternate function.
	// We use them to communicate with the CS43L22 chip via I2C
	GPIO_StructInit( &GPIOInitStruct );
	GPIOInitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_9; // we are going to use PB6 and PB9
	GPIOInitStruct.GPIO_Mode = GPIO_Mode_AF;			// set pins to alternate function
	GPIOInitStruct.GPIO_Speed = GPIO_Speed_50MHz;		// set GPIO speed
	GPIOInitStruct.GPIO_OType = GPIO_OType_OD;			// set output to open drain --> the line has to be only pulled low, not driven high
	GPIOInitStruct.GPIO_PuPd = GPIO_PuPd_UP;			// enable pull up resistors
	GPIO_Init(GPIOB, &GPIOInitStruct);

	// The I2C1_SCL and I2C1_SDA pins are now connected to their AF
	// so that the I2C1 can take over control of the
	//  pins
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_I2C1);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource9, GPIO_AF_I2C1);

	// Set the I2C structure parameters
	// I2C is used to configure CS43L22 chip for analog processing on the headphone jack.
	I2C_InitStruct.I2C_ClockSpeed = 100000; 			// 100kHz
	I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;			// I2C mode
	I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;	// 50% duty cycle --> standard
	I2C_InitStruct.I2C_OwnAddress1 = 0x00;			// own address, not relevant in master mode
	I2C_InitStruct.I2C_Ack = I2C_Ack_Disable;			// disable acknowledge when reading (can be changed later on)
	I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit; // set address length to 7 bit addresses

	// Initialize the I2C peripheral w/ selected parameters
	I2C_Init(I2C1,&I2C_InitStruct);

	// enable I2C1
	I2C_Cmd(I2C1, ENABLE);

	//IO for push buttons using internal pull-up resistors
	GPIO_StructInit( &GPIOInitStruct );
	GPIOInitStruct.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_11;
	GPIOInitStruct.GPIO_Speed = GPIO_Speed_2MHz;
	GPIOInitStruct.GPIO_Mode = GPIO_Mode_IN;
	GPIOInitStruct.GPIO_OType = GPIO_OType_PP;
	GPIOInitStruct.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOC, &GPIOInitStruct);

	//configure GPIO for digits
	GPIO_StructInit( &GPIOInitStruct );
	GPIOInitStruct.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12 | GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIOInitStruct.GPIO_Speed = GPIO_Speed_2MHz;
	GPIOInitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIOInitStruct.GPIO_OType = GPIO_OType_PP;
	GPIOInitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOE, &GPIOInitStruct);

	//configure GPIO for multiplexing
	// Note: Pin D4 is used to reset the CS43L22 chip
	//       Pins D7, D8, D9, D10, D11 are recommended for multiplexing the LED display.
	GPIO_StructInit( &GPIOInitStruct );
	GPIOInitStruct.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_7 | GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11;
	GPIOInitStruct.GPIO_Speed = GPIO_Speed_2MHz;
	GPIOInitStruct.GPIO_Mode = GPIO_Mode_OUT;
	GPIOInitStruct.GPIO_OType = GPIO_OType_PP;
	GPIOInitStruct.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIOInitStruct);

	//enables RTC alarm A interrupt
	RTC_ITConfig(RTC_IT_ALRA, ENABLE);

	//enables timer interrupt
	TIM5->DIER |= TIM_IT_Update;

	//enables timer
	TIM5->CR1 |= TIM_CR1_CEN;

}
