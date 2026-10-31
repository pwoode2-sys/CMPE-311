#include <stdint.h>
#include <avr/interrupt.h>
// Own register constants (EC.1.0.2) - unchanged from Project 3
#define MY_SREG    (*(volatile uint8_t*)0x5F)
#define MY_TIMSK2  (*(volatile uint8_t*)0x70)
#define MY_TCCR2A  (*(volatile uint8_t*)0xB0)
#define MY_TCCR2B  (*(volatile uint8_t*)0xB1)
#define MY_TCNT2   (*(volatile uint8_t*)0xB2)
#define MY_OCR2A   (*(volatile uint8_t*)0xB3)
#define MY_WGM21   1
#define MY_CS22    2
#define MY_OCIE2A  1

// mymilli(): timer, ISR, and reader - unchanged from Project 3
volatile uint32_t myTicks = 0;           // incremented every 1.000 ms

ISR(TIMER2_COMPA_vect) {
  myTicks++;
}

void myMilliInit() {
  MY_TCCR2A = (1 << MY_WGM21);           // CTC mode
  MY_TCCR2B = (1 << MY_CS22);            // prescaler 64 -> 250 kHz
  MY_OCR2A  = 249;                       // 250 counts = 1.000 ms
  MY_TCNT2  = 0;
  MY_TIMSK2 = (1 << MY_OCIE2A);          // compare-match A interrupt on
  __asm__ __volatile__("sei");
}

uint32_t mymilli() {
  uint8_t saved = MY_SREG;               // save interrupt state
  __asm__ __volatile__("cli");
  uint32_t copy = myTicks;               // atomic 32-bit read
  MY_SREG = saved;                       // restore interrupt state
  return copy;
}

// Cyclic executive (Project 2)
typedef void (*Task)(void);

//LED and serial dialog (unchanged from Project 3)
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

// Fan and button (NEW, 10/26/2026)
const uint8_t buttonPin = 4;             // pressed = HIGH (10k pull-down)
const uint8_t motorPin  = 9;             // Timer1 PWM output (OC1A)

const uint32_t DEBOUNCE_MS = 30;

// Order the button walks through: OFF LOW MED HIGH MED LOW (then repeat)
const uint8_t SEQ_LEN = 6;
const uint8_t speedSeq[SEQ_LEN] = { 0, 1, 2, 3, 2, 1 };

// PWM duty (0-255) for level 0=OFF, 1=LOW, 2=MEDIUM, 3=HIGH
//   LOW  = 102/255 = 40.0 %
//   MED  = 166/255 = 65.1 %
//   HIGH = 255/255 = 100 %
const uint8_t dutyTable[4] = { 0, 102, 166, 255 };

uint8_t  seqIndex     = 0;               // start in OFF
bool     lastReading  = LOW;             // raw pin value last time
bool     stableState  = LOW;             // debounced button state
uint32_t lastChange   = 0;               // mymilli() of last raw change

void setMotorSpeed(uint8_t level) {
  analogWrite(motorPin, dutyTable[level]);
}

// print the status line after a button press
void printFanStatus(uint8_t level) {
  Serial.print(F("Button pressed -> Fan: "));
  switch (level) {
    case 0:  Serial.print(F("OFF"));    break;
    case 1:  Serial.print(F("LOW"));    break;
    case 2:  Serial.print(F("MEDIUM")); break;
    default: Serial.print(F("HIGH"));   break;
  }
  Serial.print(F(" (compare value "));
  Serial.print(dutyTable[level]);
  Serial.println(F(")"));
}

// Non-blocking line reader
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

// Tasks
void taskSerial() {
  if (!prompted) {
    Serial.println(waitingForLed ? F("What LED? (1 or 2)")
                                 : F("What interval (in msec)?"));
    prompted = true;
  }
  if (!readLine()) return;

  long v = atol(inBuf);
  if (waitingForLed) {
    if (v == 1 || v == 2) {
      chosenLed = v;
      waitingForLed = false;
    } else {
      Serial.println(F("Enter 1 or 2."));   
    }
  } else {
    uint32_t half = (v > 1) ? v / 2 : 0;
    if (chosenLed == 1) { interval1 = half; lastToggle1 = mymilli(); }
    else                { interval2 = half; lastToggle2 = mymilli(); }
    waitingForLed = true;
  }
  prompted = false;                          // re-prompt on the next pass
}

void taskLed1() { blink(ledPin1, interval1, lastToggle1, state1); }
void taskLed2() { blink(ledPin2, interval2, lastToggle2, state2); }

// Debounce the button; one press = one step through speedSeq[].
void taskButton() {
  bool reading = digitalRead(buttonPin);

  if (reading != lastReading) {          // raw pin changed: restart timer
    lastReading = reading;
    lastChange  = mymilli();
  }

  if ((mymilli() - lastChange >= DEBOUNCE_MS) && (reading != stableState)) {
    stableState = reading;               // change survived the debounce time
    if (stableState == HIGH) {           // act on the press, not the release
      seqIndex = (seqIndex + 1) % SEQ_LEN;
      setMotorSpeed(speedSeq[seqIndex]);
      printFanStatus(speedSeq[seqIndex]);   
    }
  }
}

Task taskTable[] = { taskSerial, taskLed1, taskLed2, taskButton };
const uint8_t NUM_TASKS = sizeof(taskTable) / sizeof(taskTable[0]);

void setup() {
  Serial.begin(9600);
  pinMode(ledPin1, OUTPUT);
  pinMode(ledPin2, OUTPUT);
  pinMode(buttonPin, INPUT);             // external 10k pull-down
  pinMode(motorPin, OUTPUT);
  setMotorSpeed(0);                      // fan OFF at power-up
  myMilliInit();
}

void loop() {
  for (uint8_t i = 0; i < NUM_TASKS; i++) taskTable[i]();
}
