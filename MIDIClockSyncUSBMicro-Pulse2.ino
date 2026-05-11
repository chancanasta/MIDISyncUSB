#include "MIDIUSB.h"

//#define DEBUG_OUTPUT

#define TRUE  1
#define FALSE 0
//available sync ouputs (PPQNs)
#define SYNC_24 0
#define SYNC_48 1
#define SYNC_96 2

//our guess at a temp (BPM) if no previous clock signals recieved
#define BPM_GUESS 160

//MIDI baud rate
#define USB_MIDI_BAUD 115200
//gap at end of full cycle (in ms) to allow next MIDI clock to trigger square
//rather than it auto triggering before next clock signal  
#define SQUARE_BUFFER 2


//number of MIDI clocks per quarter note
#define MIDI_CLOCKS 24

//number of ms to keep LED on for a "flash"
#define LED_FLASH_TIME  20


//bunch of globals for the clock
byte gClockSetting=0;
byte gClockRunning=FALSE;


//Variables for square wave and duty cycle
byte gPulseHigh=FALSE;
unsigned long gDutyCycle=0;
unsigned long gPulseStart=0;
unsigned long gFullCycle=0;
byte gSquareRunning=FALSE;
byte gTriggerSquare=FALSE;
unsigned long lastFlash=0;
unsigned long gGuessClock=0;
unsigned long gLastMIDIClock=0;
unsigned long gNow;

//number of pulses we'll send per MIDI clock
byte gPulsesPerMIDI=1;
byte gCurrentNoPulses=0;
//LED flash
byte gLEDOn=FALSE;
unsigned long gLEDOffTime=0;

//PIN for square wave output
#define CLOCK_PIN 7


#ifdef DEBUG_OUTPUT
//somewhere to format our messages
char gOutBuffer[40];
#endif

void setup() 
{
//set PPQ (24,48 or 96)
  gClockSetting=SYNC_48;

//LED setup
  pinMode(LED_BUILTIN,OUTPUT);   // declare the LED's pin as output
  pinMode(LED_BUILTIN_TX,INPUT); //turn off built in transmit LED on miro board
  pinMode(CLOCK_PIN,OUTPUT);
  digitalWrite(CLOCK_PIN,LOW);


//USB MIDI in setup
  Serial.begin(USB_MIDI_BAUD);  //start serial with usb midi baudrate 
  Serial.flush();
#ifdef DEBUG_OUTPUT
  Serial.print("Starting\r\n");
#endif
//create a guess square wave cycle time
//this will be used if no previous clocks have been recieved
  gGuessClock=(60000/BPM_GUESS)/24;

// set a 200ms second duty cycle as a test
//  gDutyCycle=200;
//  gFullCycle=gDutyCycle<<1;
//  gPulseHigh=FALSE;
 // gClockRunning=TRUE;
 // gTriggerSquare=TRUE;
  gLastMIDIClock=0;
  setPulse();
}

void loop()
{
//MIDI event packet
   midiEventPacket_t rx;

//get 'now'
  gNow=millis();

 // Read the incoming MIDI bytes and trap for start,stop,continue and clock signals  
  rx = MidiUSB.read();
  if (rx.header!=0)
  {
//check for clocks, starts and stops
    switch(rx.byte1)
    {
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
        gClockRunning=FALSE;
        gLastMIDIClock=0;
        break;
    }
  //ignore all other MIDI data
  }

//see if we're triggering the square wave
  if(gTriggerSquare)
  {
//we are, so set a few flags
    gTriggerSquare=FALSE;
    gSquareRunning=TRUE;
    gPulseStart=gNow;
    gPulseHigh=TRUE;
//set output to high
    setPulse();
    gCurrentNoPulses++;
  }
//is the pulse running?
  if(gSquareRunning)
  {
//it is, see if we've reached end of full cycle
    if(gNow>=(gPulseStart+gFullCycle))
    {
      gSquareRunning=FALSE;
//retrigger if the clock is running, but make sure we haven't issued too many pulses for this clock input
//this can happen on initial clock signal where we guess the BPM, if real BPM is significantly slower we would issues too many pulses
      if(gClockRunning&&(gCurrentNoPulses<gPulsesPerMIDI))
        gTriggerSquare=TRUE;
    }
    else
    {
//not end of cycle, see if half cycle
      if(gNow>=(gPulseStart+gDutyCycle))
      {
//end of duty cycle - set pulse low
        gPulseHigh=FALSE;
        setPulse();
      }
    }
  }
//LED on ?
  if(gLEDOn)
  {
//see if it's time to turn it off
    if(gNow>gLEDOffTime)
    {
//LED flash time complete, turn it off
      pinMode(LED_BUILTIN_TX,INPUT);
      gLEDOn=FALSE;
    }
  }
}

void setPulse()
{
   digitalWrite(CLOCK_PIN,gPulseHigh);
}

void gotClock()
{
   unsigned long diff;

//got a MIDI clock, this should always trigger a square, if we're running
  if(gClockRunning)
    gTriggerSquare=TRUE;
//do we have a previous clock?
  if(gLastMIDIClock==0)
//we don't - so this is the first MIDI clock message we've recieved, use our 'guess' bpm to start the clock going
//we'll auto correct on the second clock signal
    gLastMIDIClock=gNow-gGuessClock;
//get time between MIDI clocks
  diff=(gNow-gLastMIDIClock);
//reset the number of pulses
  gCurrentNoPulses=0;
//work out duty cycle of square wave based on PPQN conversion of MIDI clock
  switch(gClockSetting)
  {
    case SYNC_48:
      gFullCycle=diff>>1; //48ppq (Linn etc)
      gPulsesPerMIDI=2;
      break;
    case SYNC_96:
      gFullCycle=diff>>2; //96ppq (Oberheim etc)
      gPulsesPerMIDI=4;
      break;
    default:
      gPulsesPerMIDI=1;
      gFullCycle=diff; //24ppq (Roland, Korg etc, matches MIDI clock)
  }
//the square wave will be 1/2 whatever cycle we've determined
  gDutyCycle=gFullCycle>>1;
//record this as the last MIDI clock
  gLastMIDIClock=gNow;

  gFullCycle-=SQUARE_BUFFER;
}

//handle start
void gotStart()
{
#ifdef DEBUG_OUTPUT
  Serial.write("Start\r\n");
#endif
  gLastMIDIClock=0;
  gClockRunning=TRUE;
}

//flash LED
void flash()
{
//check if we're already 'flashing'
  if(!gLEDOn)
  {
//not currently 'flashing', so flash LED
    pinMode( LED_BUILTIN_TX, OUTPUT);
    digitalWrite(LED_BUILTIN_TX, LOW );
//set the LED var to on and initialse the counter
    gLEDOn=TRUE;
    gLEDOffTime=millis()+LED_FLASH_TIME;
  }
}

