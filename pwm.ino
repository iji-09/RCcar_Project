#include <Servo.h>
#include "PinChangeInterrupt.h"

// 수신기 입력
#define CH1_PIN A0    // 조향
#define CH2_PIN A1    // 속도
// 아두이노 출력
#define SERVO_PIN 6   // 조향 서보
#define ESC_PIN   7   // ESC 제어

Servo steeringServo;
Servo esc;

volatile unsigned long ch1Start = 0, ch2Start = 0;
volatile int ch1Width = 1500, ch2Width = 1500;
volatile bool newCh1 = false, newCh2 = false;

void setup() {
  Serial.begin(9600);

  pinMode(CH1_PIN, INPUT_PULLUP);
  pinMode(CH2_PIN, INPUT_PULLUP);
  attachPCINT(digitalPinToPCINT(CH1_PIN), ch1ISR, CHANGE);
  attachPCINT(digitalPinToPCINT(CH2_PIN), ch2ISR, CHANGE);

  steeringServo.attach(SERVO_PIN); // 조향 서보
  esc.attach(ESC_PIN);             // 속도 제어 ESC
}

void ch1ISR() {
  if (digitalRead(CH1_PIN) == HIGH)
    ch1Start = micros();
  else if (ch1Start) {
    ch1Width = micros() - ch1Start;
    newCh1 = true;
    ch1Start = 0;
  }
}

void ch2ISR() {
  if (digitalRead(CH2_PIN) == HIGH)
    ch2Start = micros();
  else if (ch2Start) {
    ch2Width = micros() - ch2Start;
    newCh2 = true;
    ch2Start = 0;
  }
}

void loop() {
  // 조향 제어
  if (newCh1) {
    newCh1 = false;
    int angle = map(ch1Width, 1000, 2000, 0, 180);
    steeringServo.write(constrain(angle, 0, 180));
    Serial.print("Steering: ");
    Serial.println(angle);
  }

  // 속도 제어 (10% 출력 제한)
  if (newCh2) {
  newCh2 = false;

  int limitedPWM;

  if (ch2Width < 1500) {
    // 후진: 1000~1500 → 1450~1500
    limitedPWM = map(ch2Width, 1000, 1500, 1410, 1500);
  } else if (ch2Width > 1500) {
    // 전진: 1500~2000 → 1500~1550
    limitedPWM = map(ch2Width, 1500, 2000, 1500, 1590);
  } else {
    limitedPWM = 1500; // 중립
  }

  esc.writeMicroseconds(constrain(limitedPWM, 1410, 1590));
  Serial.print("Limited Throttle PWM: ");
  Serial.println(limitedPWM);
  }
}

