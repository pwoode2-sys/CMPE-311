#include <Arduino.h>
int ledPin1 = 2;
int ledPin2 = 3;

unsigned long interval1 = 0; // 0 = off
unsigned long interval2 = 0;

unsigned long lastToggle1 = 0;
unsigned long lastToggle2 = 0;

bool state1 = LOW;
bool state2 = LOW;

int waitingForLed = 1; // 1 = waiting for LED number, 0 = waiting for interval
int chosenLed = 0;

void setup() {
  Serial.begin(9600);
  pinMode(ledPin1, OUTPUT);
  pinMode(ledPin2, OUTPUT);
  Serial.println("What LED? (1 or 2)");
}

void loop() {
  checkSerial();
  blink(ledPin1, interval1, lastToggle1, state1);
  blink(ledPin2, interval2, lastToggle2, state2);
}

// One shared blink function for both LEDs
void blink(int pin, unsigned long interval, unsigned long &lastToggle, bool &state) {
  if (interval == 0) return; // off
  if (millis() - lastToggle >= interval) {
    state = !state;
    digitalWrite(pin, state);
    lastToggle = millis();
  }
}

void checkSerial() {
  if (!Serial.available()) return;

  if (waitingForLed == 1) {
    int led = Serial.parseInt();
    while (Serial.available()) Serial.read(); // clear leftover newline

    if (led == 1 || led == 2) {
      chosenLed = led;
      Serial.println("What interval (in msec)?");
      waitingForLed = 0;
    } else {
      Serial.println("Retry");
    }

  } else {
    long msec = Serial.parseInt();
    while (Serial.available()) Serial.read(); // clear leftover newline

    if (chosenLed == 1) interval1 = msec / 2;
    else interval2 = msec / 2;

    Serial.println("What LED? (1 or 2)");
    waitingForLed = 1;
  }
}
