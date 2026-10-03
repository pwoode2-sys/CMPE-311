typedef void (*Task)(void);

const int ledPin1 = 2;
const int ledPin2 = 3;

unsigned long interval1 = 0;   
unsigned long interval2 = 0;
unsigned long lastToggle1 = 0;
unsigned long lastToggle2 = 0;
bool state1 = LOW;
bool state2 = LOW;

bool waitingForLed = true;     
int  chosenLed = 0;
bool prompted = false;

char inBuf[12];
uint8_t inLen = 0;

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

void blink(int pin, unsigned long interval, unsigned long &lastToggle, bool &state) {
  if (interval == 0) {
    if (state) { state = LOW; digitalWrite(pin, LOW); }   
    return;
  }
  if (millis() - lastToggle >= interval) {
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
    // else: invalid, just re-prompt
  } else {
    unsigned long half = (v > 1) ? v / 2 : 0;
    if (chosenLed == 1) { interval1 = half; lastToggle1 = millis(); }
    else                { interval2 = half; lastToggle2 = millis(); }
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
}

void loop() {
  for (uint8_t i = 0; i < NUM_TASKS; i++) taskTable[i]();
}
