#include <Arduino_FreeRTOS.h>   // must be included first
#include <task.h>
#include <queue.h>
#include <stdint.h>
#include <avr/interrupt.h>

// Set to 1 only for test T.46 (prints stack/heap use every 10 s).
#define RTOS_DEBUG 0

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

// Pins and fan tables (unchanged from Project 4)
const int     ledPin1   = 2;
const int     ledPin2   = 3;
const uint8_t buttonPin = 4;             // pressed = HIGH (10k pull-down)
const uint8_t motorPin  = 9;             // Timer1 PWM output (OC1A)

const uint32_t DEBOUNCE_MS = 30;

const uint8_t SEQ_LEN = 6;
const uint8_t speedSeq[SEQ_LEN] = { 0, 1, 2, 3, 2, 1 };   // OFF LOW MED HIGH MED LOW
const uint8_t dutyTable[4]      = { 0, 102, 166, 255 };   // OFF LOW MED HIGH

// RTOS objects (NEW, 11/24/2026)
// Tasks share no variables. They talk only through queues:
//   serial task -> LED task    : new half-interval in msec (length 1,
//                                xQueueOverwrite so the newest wins)
//   button task -> serial task : new fan level, so that only the
//                                serial task ever uses Serial

struct LedCtx {
  int           pin;
  QueueHandle_t cmdQ;        // uint32_t half-interval in msec
  uint32_t      interval;    // 0 = off
  uint32_t      lastToggle;
  bool          state;
};

static LedCtx led1 = { ledPin1, NULL, 0, 0, LOW };
static LedCtx led2 = { ledPin2, NULL, 0, 0, LOW };
static QueueHandle_t fanEventQ = NULL;   // uint8_t fan level

// Task priorities (higher number = higher priority)
#define PRIO_SERIAL 1
#define PRIO_LED    2
#define PRIO_BUTTON 2

// AVR stack depth is in bytes. Check with T.46 and adjust if needed.
#define STACK_SERIAL 176
#define STACK_LED    110
#define STACK_BUTTON 110

// Fan helpers
void setMotorSpeed(uint8_t level) {
  analogWrite(motorPin, dutyTable[level]);
}

// Only called from taskSerial, so prints never interleave.
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

// Serial dialog state (used only by taskSerial)
static bool    waitingForLed = true;
static int     chosenLed = 0;
static bool    prompted = false;
static char    inBuf[12];
static uint8_t inLen = 0;

// Non-blocking line reader
static bool readLine() {
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

// Tasks (each is a FreeRTOS task with its own endless loop)
void taskSerial(void *pv) {
  (void)pv;
  for (;;) {
    // 1. Report any fan change made by the button task.
    uint8_t level;
    while (xQueueReceive(fanEventQ, &level, 0) == pdPASS) {
      printFanStatus(level);
    }

    // 2. Serial dialog (same behaviour as Project 4).
    if (!prompted) {
      Serial.println(waitingForLed ? F("What LED? (1 or 2)")
                                   : F("What interval (in msec)?"));
      prompted = true;
    }
    if (readLine()) {
      long v = atol(inBuf);
      if (waitingForLed) {
        if (v == 1 || v == 2) {
          chosenLed = v;
          waitingForLed = false;
        } else {
          Serial.println(F("Enter 1 or 2."));
        }
      } else {
        uint32_t half = (v > 1) ? (uint32_t)v / 2 : 0;
        LedCtx *c = (chosenLed == 1) ? &led1 : &led2;
        xQueueOverwrite(c->cmdQ, &half);        // hand the value to the LED task
        waitingForLed = true;
      }
      prompted = false;                         // re-prompt on the next pass
    }

    vTaskDelay(1);                              // block one RTOS tick (15 ms)
  }
}

void taskLed(void *pv) {
  LedCtx *c = (LedCtx *)pv;
  for (;;) {
    uint32_t half;
    if (xQueueReceive(c->cmdQ, &half, 0) == pdPASS) {
      c->interval   = half;
      c->lastToggle = mymilli();
    }

    if (c->interval == 0) {
      if (c->state) { c->state = LOW; digitalWrite(c->pin, LOW); }
    } else if (mymilli() - c->lastToggle >= c->interval) {
      c->state = !c->state;
      digitalWrite(c->pin, c->state);
      c->lastToggle += c->interval;
    }

    vTaskDelay(1);
  }
}

// Debounce the button; one press = one step through speedSeq[].
void taskButton(void *pv) {
  (void)pv;
  uint8_t  seqIndex    = 0;                // start in OFF
  bool     lastReading = LOW;
  bool     stableState = LOW;
  uint32_t lastChange  = 0;

  for (;;) {
    bool reading = digitalRead(buttonPin);

    if (reading != lastReading) {          // raw pin changed: restart timer
      lastReading = reading;
      lastChange  = mymilli();
    }

    if ((mymilli() - lastChange >= DEBOUNCE_MS) && (reading != stableState)) {
      stableState = reading;               // change survived the debounce time
      if (stableState == HIGH) {           // act on the press, not the release
        seqIndex = (seqIndex + 1) % SEQ_LEN;
        uint8_t level = speedSeq[seqIndex];
        setMotorSpeed(level);
        xQueueSend(fanEventQ, &level, 0);  // serial task prints it
      }
    }

    vTaskDelay(1);
  }
}

#if RTOS_DEBUG
// Test T.46 only: prints the lowest free stack seen by each task.
static TaskHandle_t hSerial, hLed1, hLed2, hButton;
void taskDebug(void *pv) {
  (void)pv;
  for (;;) {
    vTaskDelay(667);                        // about 10 s at 15 ms per tick
    Serial.print(F("[dbg] min free stack S/L1/L2/B: "));
    Serial.print(uxTaskGetStackHighWaterMark(hSerial));  Serial.print('/');
    Serial.print(uxTaskGetStackHighWaterMark(hLed1));    Serial.print('/');
    Serial.print(uxTaskGetStackHighWaterMark(hLed2));    Serial.print('/');
    Serial.print(uxTaskGetStackHighWaterMark(hButton));
    Serial.print(F("  free heap: "));
    Serial.println(xPortGetFreeHeapSize());
  }
}
#endif

// setup(): create everything. The library starts the scheduler
// automatically when setup() returns, so loop() stays empty.
void setup() {
  Serial.begin(9600);
  pinMode(ledPin1, OUTPUT);
  pinMode(ledPin2, OUTPUT);
  pinMode(buttonPin, INPUT);             // external 10k pull-down
  pinMode(motorPin, OUTPUT);
  setMotorSpeed(0);                      // fan OFF at power-up
  myMilliInit();                         // Timer2 millisecond clock

  led1.cmdQ = xQueueCreate(1, sizeof(uint32_t));
  led2.cmdQ = xQueueCreate(1, sizeof(uint32_t));
  fanEventQ = xQueueCreate(4, sizeof(uint8_t));

  bool ok = led1.cmdQ && led2.cmdQ && fanEventQ;
#if RTOS_DEBUG
  ok = ok && xTaskCreate(taskSerial, "ser", STACK_SERIAL, NULL,  PRIO_SERIAL, &hSerial)  == pdPASS
          && xTaskCreate(taskLed,    "led1", STACK_LED,   &led1, PRIO_LED,    &hLed1)    == pdPASS
          && xTaskCreate(taskLed,    "led2", STACK_LED,   &led2, PRIO_LED,    &hLed2)    == pdPASS
          && xTaskCreate(taskButton, "btn", STACK_BUTTON, NULL,  PRIO_BUTTON, &hButton)  == pdPASS
          && xTaskCreate(taskDebug,  "dbg", STACK_SERIAL, NULL,  PRIO_SERIAL, NULL)      == pdPASS;
#else
  ok = ok && xTaskCreate(taskSerial, "ser", STACK_SERIAL, NULL,  PRIO_SERIAL, NULL) == pdPASS
          && xTaskCreate(taskLed,    "led1", STACK_LED,   &led1, PRIO_LED,    NULL) == pdPASS
          && xTaskCreate(taskLed,    "led2", STACK_LED,   &led2, PRIO_LED,    NULL) == pdPASS
          && xTaskCreate(taskButton, "btn", STACK_BUTTON, NULL,  PRIO_BUTTON, NULL) == pdPASS;
#endif

  if (!ok) {                             // out of RTOS heap: say so and stop
    Serial.println(F("RTOS out of memory"));
    for (;;) { }
  }
}

void loop() {
  // Empty. All work is done in the RTOS tasks.
}
