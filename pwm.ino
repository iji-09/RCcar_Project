#include <Servo.h>
#include "PinChangeInterrupt.h"

// ===================================
// 핀 정의
// ===================================
#define CH9_PIN        2   // D2 핀 사용, 모드(PWM) 입력
#define CH1_PIN        A0  // 조향 PWM 입력
#define CH2_PIN        A1  // 속도 PWM 입력
#define SERVO_PIN      6   // 조향 서보
#define ESC_PIN        7   // ESC 제어

// LED 핀 (필요에 맞춰 변경)
#define LEFT_LED_PIN   11  // 왼쪽 방향 표시 LED
#define RIGHT_LED_PIN  12  // 오른쪽 방향 표시 LED

#define AUTO_MODE       0
#define MANUAL_MODE     1

Servo steeringServo;
Servo esc;

// ===================================
// 수동모드 PWM 측정용 변수 (인터럽트용)
// ===================================
volatile unsigned long ch1Start = 0, ch2Start = 0, ch9Start = 0;
volatile int ch1Width = 1500, ch2Width = 1500, ch9Width = 1500;
volatile bool newCh1 = false, newCh2 = false, newCh9 = false;

// ===================================
// 자동모드 시리얼 버퍼
// ===================================
String inputString = "";

// ===================================
// 전역 변수
// ===================================
int prevMode = -1;  // 직전 모드 저장

// ===================================
// 함수 프로토타입
// ===================================
void runManualMode();
void runAutoMode();
void processSerialInput(const String &command);
void ch1ISR();
void ch2ISR();
void ch9ISR();

// ===================================
// setup()
// ===================================
void setup() {
  Serial.begin(9600);

  // 모드 스위치(CH9) 입력 설정
  pinMode(CH9_PIN, INPUT_PULLUP);
  attachPCINT(digitalPinToPCINT(CH9_PIN), ch9ISR, CHANGE);

  // 수동모드용 PWM 입력 설정 (CH1, CH2)
  pinMode(CH1_PIN, INPUT_PULLUP);
  pinMode(CH2_PIN, INPUT_PULLUP);
  attachPCINT(digitalPinToPCINT(CH1_PIN), ch1ISR, CHANGE);
  attachPCINT(digitalPinToPCINT(CH2_PIN), ch2ISR, CHANGE);

  // 서보/ESC 초기화
  steeringServo.attach(SERVO_PIN);
  esc.attach(ESC_PIN);
  esc.writeMicroseconds(1500);  // ESC 중립(정지) 상태

  // LED 출력 핀 설정
  pinMode(LEFT_LED_PIN, OUTPUT);
  pinMode(RIGHT_LED_PIN, OUTPUT);
  digitalWrite(LEFT_LED_PIN, LOW);
  digitalWrite(RIGHT_LED_PIN, LOW);
}

// ===================================
// loop()
// ===================================
void loop() {
  // 1) CH9 PWM 폭에 따라 현재 모드 판단
  int currentMode = (ch9Width < 1500) ? AUTO_MODE : MANUAL_MODE;

  // 2) 모드 변경 시 로그 출력
  if (currentMode != prevMode) {
    //   if (currentMode == AUTO_MODE) {
    //   Serial.println("모드 전환됨 → 자동모드" );
    // } else {
    //   Serial.println("모드 전환됨 → 수동모드" );
    // }
    prevMode = currentMode;
  }

  // 3) 모드에 따라 분기
  if (currentMode == AUTO_MODE) {
    runAutoMode();
  } else {
    runManualMode();
  }
}

// ===================================
// 수동모드 동작
// ===================================
void runManualMode() {
  // 1) CH1 값 변환에 따라 조향
  int angle = 90;  // 기본값(가운데)
  if (newCh1) {
    newCh1 = false;
    angle = map(ch1Width, 1000, 2000, 0, 180);
    angle = constrain(angle, 0, 180);
    steeringServo.write(angle);

    // Serial.print("수동 조향: ");
    // Serial.println(angle);
  }

  // 2) CH2 값 변환에 따라 속도(ESC) 제어
  int limitedPWM = 1500;
  bool isReverse = false;
  if (newCh2) {
    newCh2 = false;
    if (ch2Width < 1500) {
      // 후진 (1000~1500 → 1420~1500)
      limitedPWM = map(ch2Width, 1000, 1500, 1420, 1500);
      isReverse = true;  // 후진 플래그
    } else if (ch2Width > 1500) {
      // 전진 (1500~2000 → 1500~1560)
      limitedPWM = map(ch2Width, 1500, 2000, 1500, 1560);
      isReverse = false;
    } else {
      // 중립
      limitedPWM = 1500;
      isReverse = false;
    }
    limitedPWM = constrain(limitedPWM, 1420, 1560);
    esc.writeMicroseconds(limitedPWM);
  }

  // 3) LED 점등 로직
  //    - 후진: 두 LED 모두 ON
  //    - 전/중립 상태(역방향 아님)에서 조향 각도로 판단
  //       · 직진(각도 약 90° 전후): 모두 OFF
  //       · 왼쪽(각도 < 85°): 왼쪽 LED ON, 오른쪽 LED OFF
  //       · 오른쪽(각도 > 95°): 오른쪽 LED ON, 왼쪽 LED OFF
  //    (각도 중앙 90° 기준으로 ±5° 정도를 deadzone으로 설정)
  if (isReverse) {
    // 후진 시
    digitalWrite(LEFT_LED_PIN, HIGH);
    digitalWrite(RIGHT_LED_PIN, HIGH);
  } else {
    // 전/중립 상태일 때
    if (angle < 85) {
      // 왼쪽으로 많이 틀었을 때
      digitalWrite(LEFT_LED_PIN, HIGH);
      digitalWrite(RIGHT_LED_PIN, LOW);
    } else if (angle > 95) {
      // 오른쪽으로 많이 틀었을 때
      digitalWrite(LEFT_LED_PIN, LOW);
      digitalWrite(RIGHT_LED_PIN, HIGH);
    } else {
      // 거의 직진(90° 전후) 또는 중립일 때
      digitalWrite(LEFT_LED_PIN, LOW);
      digitalWrite(RIGHT_LED_PIN, LOW);
    }
  }
}

// ===================================
// 자동모드 동작 (Pi → 아두이노 통신)
// ===================================
void runAutoMode() {
  int avail = Serial.available();

  if (avail > 0) {
  
    inputString = Serial.readStringUntil('\n');
    inputString.trim();
    processSerialInput(inputString);
  }
}

// ===================================
// 시리얼로 들어온 커맨드 처리
//   - "N/A" → 후진 → 중립
//   - 숫자(0~180) → 조향 서보, 전진 속도
// ===================================
void processSerialInput(const String &command) {
  int angle;
  
  Serial.println(command);
  
  if (command == "N/A") {
    esc.writeMicroseconds(1440);
    delay(800);                  // 후진 짧게 수행
    esc.writeMicroseconds(1500); // 중립
  } else {
    angle = command.toInt();
    angle = constrain(angle, 0, 180);

    // 자동 조향
    steeringServo.write(angle);
    // 자동 전진(고정 속도 예시: 1550μs)
    esc.writeMicroseconds(1550);
  }


  int limitedPWM = 1500;
  bool isReverse = false;
  if (isReverse) {
      // 후진 시
      digitalWrite(LEFT_LED_PIN, HIGH);
      digitalWrite(RIGHT_LED_PIN, HIGH);
    } else {
      // 전/중립 상태일 때
      if (angle < 85) {
        // 왼쪽으로 많이 틀었을 때
        digitalWrite(LEFT_LED_PIN, HIGH);
        digitalWrite(RIGHT_LED_PIN, LOW);
      } else if (angle > 95) {
        // 오른쪽으로 많이 틀었을 때
        digitalWrite(LEFT_LED_PIN, LOW);
        digitalWrite(RIGHT_LED_PIN, HIGH);
      } else {
        // 거의 직진(90° 전후) 또는 중립일 때
        digitalWrite(LEFT_LED_PIN, LOW);
        digitalWrite(RIGHT_LED_PIN, LOW);
      }
    }
    
}

// ===================================
// 인터럽트 핸들러: CH1 (조향 PWM) 측정
// ===================================
void ch1ISR() {
  if (digitalRead(CH1_PIN) == HIGH) {
    ch1Start = micros();
  } else if (ch1Start) {
    ch1Width = micros() - ch1Start;
    newCh1   = true;
    ch1Start = 0;
  }
}

// ===================================
// 인터럽트 핸들러: CH2 (속도 PWM) 측정
// ===================================
void ch2ISR() {
  if (digitalRead(CH2_PIN) == HIGH) {
    ch2Start = micros();
  } else if (ch2Start) {
    ch2Width = micros() - ch2Start;
    newCh2   = true;
    ch2Start = 0;
  }
}

// ===================================
// 인터럽트 핸들러: CH9 (모드 PWM) 측정
// ===================================
void ch9ISR() {
  if (digitalRead(CH9_PIN) == HIGH) {
    ch9Start = micros();
  } else if (ch9Start) {
    ch9Width = micros() - ch9Start;
    newCh9   = true;
    ch9Start = 0;
  }
}
