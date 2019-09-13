#include "Audio.h"
#include "stm32f4xx_conf.h"
#include "stm32f4xx.h"
#include "stm32f4xx_dac.h"

#include <stdlib.h>

static void WriteRegister(uint8_t address, uint8_t value);
static void StartAudioDMAAndRequestBuffers();
static void StopAudioDMA();

static AudioCallbackFunction *CallbackFunction;
static void *CallbackContext;
static int16_t * volatile NextBufferSamples;
static volatile int NextBufferLength;
static volatile int BufferNumber;
static volatile bool DMARunning;

	uint32_t *ptr;
	uint32_t *ptrSR;
	uint32_t *ptrDOR;
	uint32_t *ptrTIM;

void InitializeAudio(int plln, int pllr, int i2sdiv, int i2sodd) {


	GPIO_InitTypeDef  GPIO_InitStructure;
	DAC_InitTypeDef 	DAC_InitStruct;
	TIM_TimeBaseInitTypeDef TIM_InitStruct;
	I2C_InitTypeDef I2C_InitStruct;

	// Initialize state.
	CallbackFunction = NULL;
	CallbackContext = NULL;
	NextBufferSamples = NULL;
	NextBufferLength = 0;
	BufferNumber = 0;
	DMARunning = false;
    volatile int i;

	// Turn on peripherals.
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI3, ENABLE);	// Needed for Audio chip
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);


	// We need to have a timer set to 'ping' at 44 kHz
	// At every ping the DAC will output a single sample.
	TIM_TimeBaseStructInit(&TIM_InitStruct);
	TIM_InitStruct.TIM_Period	= 90;	// Approximately 44khz
	TIM_InitStruct.TIM_Prescaler = 20;
	TIM_TimeBaseInit(TIM6, &TIM_InitStruct);

	// TIM6 TRGO selection
	TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);

	// Enable GPIO port A4 as an analog output
	GPIO_StructInit( &GPIO_InitStructure );
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);


	// Configure C7,C10,C12 from I2S (SPI3)
	GPIO_StructInit( &GPIO_InitStructure );
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_10 | GPIO_Pin_12;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOC, &GPIO_InitStructure);
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource7, GPIO_AF_SPI3);
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource10, GPIO_AF_SPI3);
	GPIO_PinAFConfig(GPIOC, GPIO_PinSource12, GPIO_AF_SPI3);

	// Disable I2S.
	SPI3 ->I2SCFGR = 0;

	// I2S clock configuration
	// Disable PLLI2S and wait until is off
	RCC->CR &= ~RCC_CR_PLLI2SON;
	while ( RCC -> CR & RCC_CR_PLLI2SON );

	RCC ->CFGR &= ~RCC_CFGR_I2SSRC; // PLLI2S clock used as I2S clock source.
	RCC ->PLLI2SCFGR = (pllr << 28) | (plln << 6);

	// Configure I2S.
	SPI3 ->I2SPR = i2sdiv | (i2sodd << 8) | SPI_I2SPR_MCKOE;


	// Configure I2S as Master receiver, Phillips mode, 16-bit values, clock polarity low, enable.
	SPI3 ->I2SCFGR = SPI_I2SCFGR_I2SMOD | SPI_I2SCFGR_I2SCFG_1 | SPI_I2SCFGR_I2SCFG_0
			| SPI_I2SCFGR_I2SE;

	// WARNING: In MASTER TRANSMITTER MODE, the STM32F407 will only generate the master clock signal when data is being
	// actively transmitted.  At all other times, the Master Clock (MCLK) signal will be turned off.
	// If you are using the CS43L22 chip, which uses the MCLK to run a charge pump for its analog components during
	// ANALOG PASSTHRU mode this might have unintended consequences.

	// Enable PLLI2S and wait until it is ready.
	RCC ->CR |= RCC_CR_PLLI2SON;
	while (!(RCC ->CR & RCC_CR_PLLI2SRDY ));

	//
	// Configure DAC channel 1
	//
	DAC_StructInit( &DAC_InitStruct );
	DAC_InitStruct.DAC_WaveGeneration = DAC_WaveGeneration_None;
	DAC_InitStruct.DAC_Trigger = DAC_Trigger_T6_TRGO;
	DAC_InitStruct.DAC_OutputBuffer = DAC_OutputBuffer_Enable ;
	DAC_Init(DAC_Channel_1, &DAC_InitStruct );

	//
	// Enable DAC channel 1
	//
	NVIC_InitTypeDef      NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 4;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 4;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	// Initialize the CS43L22 system for audio passthrough
	// Reset the CS43L22 chip.
	GPIO_ResetBits(GPIOD, GPIO_Pin_4);
	for( int counter=0; counter < 32000; counter++ );
	GPIO_SetBits(GPIOD,GPIO_Pin_4);

	// Initialization sequence from 4.11 of CS43L22 manual
	// Configure chip for audio passthrough with amplification to headphone jack.
	WriteRegister(0x00, 0x99);
	WriteRegister(0x47, 0x80);
	WriteRegister(0x32, 0x80);
	WriteRegister(0x32, 0x00);
	WriteRegister(0x00, 0x00);

	WriteRegister(0x02, 0x01);
	WriteRegister(0x04, 0xaf);			// HP always on, Speakers always off
	WriteRegister(0x08, 0x01);			// Select AINA1 as input
	WriteRegister(0x09, 0x01);			// Select AINB1 as input
	WriteRegister(0x0e, 0xc0);			// Analog passthrough enabled.
	WriteRegister(0x14, 0xe0);
	WriteRegister(0x15, 0xe0);
	WriteRegister(0x02, 0x9e);			// Activate the chip

}

void AudioOn() {
/*	WriteRegister(0x02, 0x9e);
	SPI3 ->I2SCFGR = SPI_I2SCFGR_I2SMOD | SPI_I2SCFGR_I2SCFG_1
			| SPI_I2SCFGR_I2SE; // Master transmitter, Phillips mode, 16 bit values, clock polarity low, enable.*/
}

void AudioOff() {
/*	WriteRegister(0x02, 0x01);
	SPI3 ->I2SCFGR = 0;*/
}

void SetAudioVolume(int volume) {
	/*
	WriteRegister(0x20, (volume + 0x19) & 0xff);
	WriteRegister(0x21, (volume + 0x19) & 0xff);*/
}

void OutputAudioSample(int16_t sample) {
	//while (!(SPI3 ->SR & SPI_SR_TXE ))
	//	;
	//SPI3 ->DR = sample;
}

void OutputAudioSampleWithoutBlocking(int16_t sample) {
	//SPI3 ->DR = sample;
}

void PlayAudioWithCallback(AudioCallbackFunction *callback, void *context) {
	StopAudioDMA();

	CallbackFunction = callback;
	CallbackContext = context;
	BufferNumber = 0;

	if (CallbackFunction)
		CallbackFunction(CallbackContext, BufferNumber);
}

void StopAudio() {

	StopAudioDMA();

	CallbackFunction = NULL;
}

void ProvideAudioBuffer(void *samples, int numsamples) {
	while (!ProvideAudioBufferWithoutBlocking(samples, numsamples))
			__asm__ volatile ("wfi");


}

bool ProvideAudioBufferWithoutBlocking(void *samples, int numsamples) {
	int loop=0;

	if (NextBufferSamples)
		return false;

	NextBufferSamples = samples;
	NextBufferLength  = numsamples;

	 	 ptr = &DAC->DHR12R1;
	 	 ptrSR = &DAC->SR;	//chck for dma underrrun
	     ptrDOR = &DAC->DOR1;

	if (!DMARunning)
		StartAudioDMAAndRequestBuffers();

	return true;
}

static void StartAudioDMAAndRequestBuffers() {


	// Configure DMA stream.
	DMA1_Stream5 ->CR = /*(0 * DMA_SxCR_CHSEL_0 )*/0x0E000000  | // Channel 7
			(1 * DMA_SxCR_PL_0 ) | // Priority 1
			(1 * DMA_SxCR_PSIZE_0 ) | // PSIZE = 16 bit
			(1 * DMA_SxCR_MSIZE ) | // MSIZE = 16 bit
			DMA_SxCR_MINC | // Increase memory address
			(1 * DMA_SxCR_DIR_0 ) | // Memory to peripheral
			DMA_SxCR_TCIE; // Transfer complete interrupt
	DMA1_Stream5 ->NDTR = NextBufferLength >> 1;
	DMA1_Stream5 ->PAR = (uint32_t) &DAC->DHR12R1;
	DMA1_Stream5 ->M0AR = (uint32_t) NextBufferSamples;
	DMA1_Stream5 ->FCR = DMA_SxFCR_DMDIS;
	DMA1_Stream5 ->CR |= DMA_SxCR_EN;


	//TIM_Cmd(TIM6, DISABLE);

	DAC_Cmd(DAC_Channel_1, ENABLE);
	DAC_DMACmd(DAC_Channel_1, ENABLE); //DMAEN1

	  TIM_Cmd(TIM6, ENABLE);

	// Update state.
	NextBufferSamples = NULL;
	BufferNumber ^= 1;
	DMARunning = true;

	// Invoke callback if it exists to queue up another buffer.
	if (CallbackFunction)
		CallbackFunction(CallbackContext, BufferNumber);
}

static void StopAudioDMA() {

	DMA1_Stream5 ->CR &= ~DMA_SxCR_EN; // Disable DMA stream.
		while (DMA1_Stream5 ->CR & DMA_SxCR_EN )
			; // Wait for DMA stream to stop.*/

	DMARunning = false;
}


void DMA1_Stream5_IRQHandler()
{
	DMA1 ->HIFCR |= DMA_HIFCR_CTCIF5; // Clear interrupt flag.

	if (NextBufferSamples) {
			StartAudioDMAAndRequestBuffers();
		}
	else
	{
			DMARunning = false;
		}

}

/* I2C Functions.
 * These functions are used to talk to the CS43L22 chip.
 * This chip will act as an audio amplifier for the headphone jack.
 * This is useful for debugging purposes.
 */

/* This function issues a start condition and
 * transmits the slave address + R/W bit
 *
 * Parameters:
 * 		I2Cx --> the I2C peripheral e.g. I2C1
 * 		address --> the 7 bit slave address
 * 		direction --> the tranmission direction can be:
 * 						I2C_Direction_Tranmitter for Master transmitter mode
 * 						I2C_Direction_Receiver for Master receiver
 */
void I2C_start(I2C_TypeDef* I2Cx, uint8_t address, uint8_t direction){
	// wait until I2C1 is not busy anymore
	while(I2C_GetFlagStatus(I2Cx, I2C_FLAG_BUSY));

	// Send I2C1 START condition
	I2C_GenerateSTART(I2Cx, ENABLE);

	// wait for I2C1 EV5 --> Slave has acknowledged start condition
	while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_MODE_SELECT));

	// Send slave Address for write
	I2C_Send7bitAddress(I2Cx, address, direction);

	/* wait for I2C1 EV6, check if
	 * either Slave has acknowledged Master transmitter or
	 * Master receiver mode, depending on the transmission
	 * direction
	 */
	if(direction == I2C_Direction_Transmitter){
		while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED));
	}
	else if(direction == I2C_Direction_Receiver){
		while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED));
	}
}

/* This function transmits one byte to the slave device
 * Parameters:
 *		I2Cx --> the I2C peripheral e.g. I2C1
 *		data --> the data byte to be transmitted
 */
void I2C_write(I2C_TypeDef* I2Cx, uint8_t data)
{
	I2C_SendData(I2Cx, data);
	// wait for I2C1 EV8_2 --> byte has been transmitted
	while(!I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_TRANSMITTED));
}

/* This function reads one byte from the slave device
 * and acknowledges the byte (requests another byte)
 */
uint8_t I2C_read_ack(I2C_TypeDef* I2Cx){
	// enable acknowledge of received data
	I2C_AcknowledgeConfig(I2Cx, ENABLE);
	// wait until one byte has been received
	while( !I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_RECEIVED) );
	// read data from I2C data register and return data byte
	uint8_t data = I2C_ReceiveData(I2Cx);
	return data;
}

/* This function reads one byte from the slave device
 * and doesn't acknowledge the received data
 */
uint8_t I2C_read_nack(I2C_TypeDef* I2Cx){
	// disable acknowledge of received data
	// nack also generates stop condition after last byte received
	// see reference manual for more info
	I2C_AcknowledgeConfig(I2Cx, DISABLE);
	I2C_GenerateSTOP(I2Cx, ENABLE);
	// wait until one byte has been received
	while( !I2C_CheckEvent(I2Cx, I2C_EVENT_MASTER_BYTE_RECEIVED) );
	// read data from I2C data register and return data byte
	uint8_t data = I2C_ReceiveData(I2Cx);
	return data;
}

/* This function issues a stop condition and therefore
 * releases the bus
 */
void I2C_stop(I2C_TypeDef* I2Cx){
	// Send I2C1 STOP Condition
	I2C_GenerateSTOP(I2Cx, ENABLE);
}

static uint8_t ReadRegister(uint8_t address) {

	uint8_t return_value;

	I2C_start(I2C1, 0x94, I2C_Direction_Transmitter);
	I2C_write(I2C1, address);
	I2C_stop(I2C1); // Stop and releases the bus.

	I2C_start(I2C1, 0x95, I2C_Direction_Receiver);
	return_value = I2C_read_nack(I2C1);
	I2C_stop(I2C1);

	return(return_value);
}

static void WriteRegister(uint8_t address, uint8_t value) {

	I2C_start(I2C1, 0x94, I2C_Direction_Transmitter); // start a transmission in Master transmitter mode
	I2C_write(I2C1, address); // write one byte to the slave
	I2C_write(I2C1, value); // write another byte to the slave
	I2C_stop(I2C1); // stop the transmission
}
