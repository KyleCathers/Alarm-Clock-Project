# Alarm Clock Project
2nd Year Alarm Clock Project

<p align="center">
  <img width="749" height="459" src="https://github.com/KyleCathers/Alarm-Clock-Project/blob/master/Alarm-Clock.jpg">
</p>

# Project Desciption

Our alarm clock project has various components that all come together in one complete system. 
Firstly, we have our manufactured PCB board that contains our audio amplifier circuit and our LED circuit. 
In the LED circuit, we have integrated a potentiometer (between the Vcc and the base of the PNP 
transistors) that allows the brightness of the LED display to be adjusted. This allows for the 
alarm clock user to set the brightness of the clock according to the brightness of the room or 
personal preference. Our physical design of an alarm clock case prototype was designed and modeled 
on SolidWorks, and has a user friendly interface with space for each of our buttons, volume and 
brightness control and LED and speaker display.

## Circuit Design
The PCB we designed has 2 different circuits in place, an audio amplifier circuit and a LED display 
control circuit. The audio amplifier circuit, has a voltage gain of 16.04 mV, and a power gain of 
40.74 mW, and uses the 5 V power supply off of the STM board along with pin A4 for the audio signal. 
The LED display circuit design utilizes different active low signals to determine which LED segments 
will light up, and 2 major active high signals to determine the LED digit as well. The power supply 
used in this case is 3V and all LEDs can handle a maximum of 10 mA continuous current. We are using 
the STM32F4 Discovery board pins PD7- D11 to control the digits as well as the pins E7-E14 to control 
the segments.  Our buttons were assigned to Pins PC4, PC5, PC7,PC8, PC9,PC11, and PC12. 

## Software Design
The coding for this project was generally supplied to us in the wiki, however the main.c functions
required implementation according to the desired behavior of the clock with the buttons. As well 
timekeeping.c required implementation of its functions in order to keep the alarmtime and time 
struct values in range of our 12 hour clock (rather than going into non BCD hex values). Main.c 
consists of functions (act together according to the finite state machine below) described in the 
comments of the code posted below, the functions that we implemented were TIM5_IRQ_Handler, 
display7Seg, displayNumber, setAlarm, snooze, setTime, and buttonControls. The setTime and setAlarm 
functions made use of helper functions written in timekeeping.c, these helper functions were 
timeHourCheck, timeMinuteCheck, alarmMinuteCheck, and alarmHourCheck. The hour check functions 
kept hours from 1 to 12, while the minute check functions kept the minutes between 0 and 59.
