#include <stdint.h>
#include <avr/interrupt.h> 

//Own register constants 
#define MY_SREG    (*(volatile uint8_t*)0x5F)
#define MY_TIMSK2  (*(volatile uint8_t*)0x70)
#define MY_TCCR2A  (*(volatile uint8_t*)0xB0)
#define MY_TCCR2B  (*(volatile uint8_t*)0xB1)
#define MY_TCNT2   (*(volatile uint8_t*)0xB2)
#define MY_OCR2A   (*(volatile uint8_t*)0xB3)

#define MY_WGM21   1   
#define MY_CS22    2   
#define MY_OCIE2A  1   

// mymilli(): timer, ISR, and reader
volatile uint32_t myTicks = 0;           

// fires every 1.000 ms
ISR(TIMER2_COMPA_vect) {                 
  myTicks++;
}

void myMilliInit() {
  MY_TCCR2A = (1 << MY_WGM21);           
  MY_TCCR2B = (1 << MY_CS22);            
  MY_OCR2A  = 249;                       
  MY_TCNT2  = 0;
  MY_TIMSK2 = (1 << MY_OCIE2A);          
  __asm__ __volatile__("sei");           
}

uint32_t mymilli() {
  uint8_t saved = MY_SREG;               
  __asm__ __volatile__("cli");           
  uint32_t copy = myTicks;
  MY_SREG = saved;                       
  return copy;
}

// Cyclic executive application from PROJECT2
typedef void (*Task)(void);

const int ledPin1 = 2;
const int ledPin2 = 3;

uint32_t interval1 = 0;                  
uint32_t interval2 = 0;
uint32_t lastToggle1 = 0;
uint32_t lastToggle2 = 0;
bool state1 = LOW;
bool state2 = LOW;

bool waitingForLed = true;               
int  chosenLed = 0;
bool prompted = false;

char inBuf[12];
uint8_t inLen = 0;

// Non-blocking collects bytes already in the buffer, true when a line is complete
bool readLine() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (inLen == 0) continue;          
      inBuf[inLen] = '\0';
      inLen = 0;
      return true;
    }
    if (inLen < sizeof(inBuf) - 1) inBuf[inLen++] = c;
  }
  return false;
}

void blink(int pin, uint32_t interval, uint32_t &lastToggle, bool &state) {
  if (interval == 0) {
    if (state) { state = LOW; digitalWrite(pin, LOW); }  
    return;
  }
  if (mymilli() - lastToggle >= interval) {
    state = !state;
    digitalWrite(pin, state);
    lastToggle += interval;              
  }
}

void taskSerial() {
  if (!prompted) {
    Serial.println(waitingForLed ? F("What LED? (1 or 2)")
                                 : F("What interval (in msec)?"));
    prompted = true;
  }
  if (!readLine()) return;

  long v = atol(inBuf);
  if (waitingForLed) {
    if (v == 1 || v == 2) { chosenLed = v; waitingForLed = false; }
  } else {
    uint32_t half = (v > 1) ? v / 2 : 0;
    if (chosenLed == 1) { interval1 = half; lastToggle1 = mymilli(); }
    else                { interval2 = half; lastToggle2 = mymilli(); }
    waitingForLed = true;
  }
  prompted = false;
}

void taskLed1() { blink(ledPin1, interval1, lastToggle1, state1); }
void taskLed2() { blink(ledPin2, interval2, lastToggle2, state2); }

Task taskTable[] = { taskSerial, taskLed1, taskLed2 };
const uint8_t NUM_TASKS = sizeof(taskTable) / sizeof(taskTable[0]);

void setup() {
  Serial.begin(9600);
  pinMode(ledPin1, OUTPUT);
  pinMode(ledPin2, OUTPUT);
  myMilliInit();
}

void loop() {
  for (uint8_t i = 0; i < NUM_TASKS; i++) taskTable[i]();
}
