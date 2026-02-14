#include "MIDIUSB.h"

//#define DEBUG_OUTPUT

#define TRUE  1
#define FALSE 0

#define SYNC_24 0
#define SYNC_48 1
#define SYNC_96 2

//this is the min gap in ms between a 24ppq clock we'll support
//whoch will allow us to work up to ~250 bpm
#define DEFAULT_GAP 15
//MIDI baud rate
#define USB_MIDI_BAUD 115200

//number of MIDI clocks per quarter note
#define MIDI_CLOCKS 24



//number of loops to keep LED on for a "flash"
#define LED_COUNT  500
#define DEBUG_FLASH_COUNT 8000

//bunch of globals for the clock
byte gClockSetting=0;
byte gClockMultiplier=1;
byte gTickSetting=0;
byte gClockCount=0;
byte gClockRunning=FALSE;
byte gSendDebugCount=0;
byte gRunningClock=FALSE;

//a few more
unsigned long gLastMilli=0;
unsigned long gMilliGap=0;
unsigned long gLastOutTick=0;
unsigned long gOutTickGap=0;
byte gGotInTick=0;
byte gOutTickCount;
//LED flash
byte gFlashCnt=0;
byte gLEDOn=FALSE;
unsigned long gLEDCount=0;
unsigned long gWaitLoop=0; 

//pulse
byte gPulseOn=FALSE;
#define PULSE_LENGTH  2
unsigned long gPulseEnd=0;
#define CLOCK_PIN 7



#ifdef DEBUG_OUTPUT
//somewhere to format our messages
char gOutBuffer[40];
#endif

void setup() 
{
//set PPQ (24,48 or 96)
  gClockSetting=SYNC_48;

//set the multiplier for the output clock
  switch(gClockSetting)
  {
    case SYNC_48:
      gClockMultiplier=2; //48ppq (Linn etc)
      break;
    case SYNC_96:
      gClockMultiplier=4; //96ppq (Oberheim etc)
      break;
    case SYNC_24:
    default:
      gClockMultiplier=1; //24ppq (Roland etc)

  }
  gTickSetting=gClockMultiplier*MIDI_CLOCKS;

//LED setup
  pinMode(LED_BUILTIN,OUTPUT);   // declare the LED's pin as output
  pinMode(LED_BUILTIN_TX,INPUT); //turn off built in transmit LED on miro board
  pinMode(CLOCK_PIN,OUTPUT);
  digitalWrite(CLOCK_PIN,LOW);


//MIDI in setup
  Serial.begin(USB_MIDI_BAUD);  //start serial with usb midi baudrate 
  Serial.flush();
#ifdef DEBUG_OUTPUT
  Serial.print("Starting\r\n");
#endif
}

void loop() 
{
  midiEventPacket_t rx;

//debug - just flash the LED and put out a square wave to show we're alive
#ifdef DEBUG_OUTPUT
  gWaitLoop++;
  if( gWaitLoop>DEBUG_FLASH_COUNT)
  {
    gWaitLoop=0;
    flash();
    //Pulse
    sendPulse();
  }
#endif
//are we sending square wave?
  if(gPulseOn)
  {
    if(gPulseEnd==0)
    {
      gPulseEnd=(millis()+PULSE_LENGTH);
      digitalWrite(CLOCK_PIN,HIGH);
    }
    else
    {
//turn off output if we've hit end of pulse
      if(millis()>gPulseEnd)
      {
        gPulseOn=FALSE;
        gPulseEnd=0;
        digitalWrite(CLOCK_PIN,LOW);
      }
    }
  }
//are we flashing the LED?
  if(gLEDOn && gLEDCount==LED_COUNT)
  {
//    digitalWrite(LED_BUILTIN, HIGH );
    pinMode( LED_BUILTIN_TX, OUTPUT);
    digitalWrite(LED_BUILTIN_TX, LOW );
  }
 
// Read the incoming MIDI bytes and trap for start,stop,continue and clock signals  
  rx = MidiUSB.read();
  if (rx.header!=0)
  {
  
//check for clocks, starts and stops
    switch(rx.byte1)
    {
//-------------------  
//Clock
      case 0xF8:           
        gotClock();
        break;
//Start
      case 0xFA:
        gotStart();
        break;
//Continue
      case 0xFB:
#ifdef DEBUG_OUTPUT
        Serial.write("Continue\r\n");
#endif
        gClockRunning=TRUE;
        break;
//Stop
      case 0xFC:
#ifdef DEBUG_OUTPUT
        Serial.write("Stop\r\n");
#endif
        gLastMilli=0;
        gClockRunning=FALSE;
        gRunningClock=FALSE;
        break;
    }
  //ignore all other MIDI data
  }
 
//LED on ?
  if(gLEDOn)
  {
//if it's on, decrease the counter
    gLEDCount--;
    if(!gLEDCount)
    {
//hit zero, turn off the LED
//      digitalWrite(LED_BUILTIN, LOW );
      pinMode(LED_BUILTIN_TX,INPUT);
      gLEDOn=FALSE;
    }

  }
//Check if we need to send a sync pulse
  checkSync();
}

//handle start
void gotStart()
{
#ifdef DEBUG_OUTPUT
  Serial.write("Start\r\n");
#endif
  gClockRunning=TRUE;
  gClockCount=0;
  gFlashCnt=0;
//did we already have a running clock? (i.e. clock is running prior to start)
  if(!gRunningClock)
  {
//no running clock, which means we won't have be able to work out time between ticks
//and any multiplier out ticks (i.e. larger than 24) will be off
//so... send the missing ticks prior to this, meaning the last "real tick" will trigger the compelte quarternote on drum machine
//i.e. 24 ppqn in, 48 ppqn out, so we send 1 tick now, then an additional tick per clock pulse, this means the last "real" 24 ppqn tick will trigger
//the 48 ppqn signal and will remain in time
    switch (gClockMultiplier)
    {
      case 1:
        break; //24 ppqn - no need for extra pulse
      case 2:
        sendSync(); //48 ppqn - need to send additional pulse
        break;
      case 3: // 96 ppqn - should send 2 pulses, leaving for moment as can't test with real DM
        break;
    }
  }

}
//start sending a square wave / pulse
void sendPulse()
{
  gPulseOn=TRUE;
  gPulseEnd=0;
}

//handles incoming midi clock signals
void gotClock()
{
//get the current time
  unsigned long time=millis();
//indicate a running clock
  gRunningClock=TRUE;
//flag we got a tick
  gGotInTick=TRUE;
#ifdef DEBUG_OUTPUT
  if(gOutTickCount!=0)
  {
    snprintf(gOutBuffer,"ERROR - got clock in but out tick not zero (%)\r\n",gOutTickCount);
    Serial.write(gOutBuffer);
  }
#endif

//
  gClockCount++;
//set the number of out ticks from this "in tick"  
  gOutTickCount+=gClockMultiplier;
//get gap since last tick
 // if(gLastMilli!=0)
 // {
  //  gMilliGap=time-gLastMilli;
#ifdef DEBUG_OUTPUT
  //  snprintf(gOutBuffer, sizeof(gOutBuffer), "Last Milli gap set to  %d \r\n", gMilliGap);
  //  Serial.write(gOutBuffer);
#endif
//  }
 // else
//  {
//this is to handle when the clock is only running after a start message
//In that case - the fist set of additional pulses after the first midi clock will be missing
//(as there has been no steady clocks before "START" to work out the gap between pulses / the tempo)
//and things will slowly go out of time
//so instead we put in a default gap at around 165bpm - this will generate the first additional pulse(s)
//and the tempo will be corrected by the second clock pulse   
    gMilliGap=DEFAULT_GAP;
//note that if clock signals ARE being generated prior to the start signal - this will not cause issues
//as it will be corrected automatically after 2nd clock pulse anyway
  // }
 //get the gap between our "out ticks"
  gOutTickGap=gMilliGap/gClockMultiplier;
  gLastMilli=time;
  
//check if we've hit the 24ppq MIDI clocks
   if(gClockCount==MIDI_CLOCKS)
   {
      gClockCount=0;
#ifdef BLINK_ON_INPUT
//see if we're running or not
      if(gClockRunning)
        flash();
#endif
   }
}

//handles sending sync signal
void checkSync()
{
//if we got an in tick, just prior to this - send
  if(gGotInTick)
  {
    gGotInTick=FALSE;
    if(gOutTickCount)
      sendSync();
  }
  else
  {
  //haven't recieved an in tick, see if we have pending out ticks
      if(gOutTickCount)
      {
//check time between now and last send
        unsigned long now=millis();
        unsigned long timeDiff=now-gLastOutTick;
        if(timeDiff>=gOutTickGap)
            sendSync();
      }
  }

}

void sendSync()
{
  gFlashCnt++;
//record the time we sent this
  gLastOutTick=millis();
//decrease the number of out ticks
  if( gOutTickCount>0)
    gOutTickCount--;

//if we're running, send a square wave to the output pin
  if(gClockRunning)
    sendPulse();
//if we've hit PPQ, flash
  if(gFlashCnt==MIDI_CLOCKS*gClockMultiplier)
  {
    if(gClockRunning)
    {
        flash();
#ifdef DEBUG_OUTPUT
        Serial.write("x\r\n");
#endif
    }
    gFlashCnt=0;
  }
}

void flash()
{
//check if we're already 'flashing'
  if(!gLEDOn)
  {
//we aren't, so set the LED var to on and initialse the counter
    gLEDOn=TRUE;
    gLEDCount=LED_COUNT;
  }
}
//------------------------------------------------------------------------------





