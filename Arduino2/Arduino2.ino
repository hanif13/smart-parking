#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

// =====================================================
// ARDUINO 2
// SMART PARKING GATE CONTROLLER
//
// หน้าที่
// 1. รับจำนวนช่องว่างจาก Arduino 1
// 2. ตรวจจับรถด้วย IR
// 3. ควบคุม Servo เปิด/ปิดไม้กั้น
// 4. แสดงจำนวนช่องว่างบน LCD
// 5. ส่งสถานะไป Raspberry Pi
//
// เนื้อหาวิชา
// - Interrupt
// - Timer / Counter
// - Watchdog Timer
// - EEPROM
// - UART / USART
// - SoftwareSerial
// =====================================================


// =====================================================
// PIN CONFIGURATION
// =====================================================

// ---------- IR Sensor ----------
// ย้ายจาก D5 -> D2
// เพราะ D5 ใช้ร่วมกับ SoftwareSerial / Pin Change Interrupt
const byte IR_PIN = 2;


// ---------- Servo ----------
const byte SERVO_PIN = A0;


// ---------- SoftwareSerial ----------
// Arduino 1 TX (A4) -> Arduino 2 RX (D10)
// Arduino 2 TX (D11) -> Arduino 1 RX (A5)
const byte SOFT_RX = 10;
const byte SOFT_TX = 11;


// =====================================================
// OBJECTS
// =====================================================

LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo gateServo;

SoftwareSerial arduinoSerial(SOFT_RX, SOFT_TX);


// =====================================================
// SYSTEM VARIABLES
// =====================================================

// จำนวนช่องว่าง
int availableSlots = 0;


// สถานะรถที่ IR
volatile bool irInterruptFlag = false;

bool carAtIR = false;


// สถานะไม้กั้น
bool gateOpen = false;


// เวลาที่เริ่มตรวจว่ารถพ้น IR แล้ว
unsigned long clearStart = 0;


// เวลาปิดไม้กั้นหลังรถพ้น
const unsigned long CLOSE_DELAY = 1500;


// =====================================================
// TIMER VARIABLES
// =====================================================

// Timer2 ทำหน้าที่สร้าง time base 1 ms
volatile unsigned long systemMillis = 0;


// =====================================================
// DEBUG / STATUS TIMER
// =====================================================

unsigned long lastPiStatus = 0;
unsigned long lastDebug = 0;


// =====================================================
// EEPROM DATA
// =====================================================

struct EEPROMData {

  unsigned long magic;

  unsigned long bootCount;

  unsigned long irDetectionCount;

  unsigned long gateOpenCount;

  unsigned long gateCloseCount;
};


// Magic number
const unsigned long EEPROM_MAGIC = 0x12345678;


// EEPROM address
const int EEPROM_ADDRESS = 0;


// ข้อมูล EEPROM
EEPROMData eepromData;


// =====================================================
// FUNCTION DECLARATIONS
// =====================================================

void irISR();

void setupTimer2();

void loadEEPROM();

void saveEEPROM();

void receiveArduino1();

void processIRInterrupt();

void updateGate();

void openGate();

void closeGate();

void updateLCD();

void sendPiStatus();

void printDebug();


// =====================================================
// TIMER2 INTERRUPT
// =====================================================
//
// Timer2 ใช้สร้าง interrupt ทุกประมาณ 1 ms
//
// Arduino UNO 16 MHz
// Prescaler = 64
// OCR2A = 249
//
// 16,000,000 / 64 = 250,000
// 250,000 / 250 = 1,000 Hz
// = 1 ms
// =====================================================

ISR(TIMER2_COMPA_vect) {

  systemMillis++;
}


// =====================================================
// IR INTERRUPT
// =====================================================
//
// ใช้ External Interrupt INT0
// Arduino UNO
// D2 = INT0
//
// เมื่อสัญญาณ IR เปลี่ยน
// จะเข้าฟังก์ชันนี้
// =====================================================

void irISR() {

  irInterruptFlag = true;
}


// =====================================================
// SETUP TIMER2
// =====================================================

void setupTimer2() {

  cli();

  // Normal mode
  TCCR2A = 0;

  // CTC mode
  TCCR2A |= (1 << WGM21);

  // Prescaler 64
  TCCR2B = 0;
  TCCR2B |= (1 << CS22);

  // 1 ms
  OCR2A = 249;

  // Enable Compare Match A interrupt
  TIMSK2 |= (1 << OCIE2A);

  sei();
}


// =====================================================
// EEPROM LOAD
// =====================================================

void loadEEPROM() {

  EEPROM.get(EEPROM_ADDRESS, eepromData);


  // ถ้า EEPROM ยังไม่มีข้อมูล
  if (eepromData.magic != EEPROM_MAGIC) {

    eepromData.magic = EEPROM_MAGIC;

    eepromData.bootCount = 0;

    eepromData.irDetectionCount = 0;

    eepromData.gateOpenCount = 0;

    eepromData.gateCloseCount = 0;

    EEPROM.put(EEPROM_ADDRESS, eepromData);
  }


  // เพิ่มจำนวนครั้งที่เปิดเครื่อง
  eepromData.bootCount++;

  EEPROM.put(EEPROM_ADDRESS, eepromData);
}


// =====================================================
// EEPROM SAVE
// =====================================================

void saveEEPROM() {

  EEPROM.put(EEPROM_ADDRESS, eepromData);
}


// =====================================================
// SETUP
// =====================================================

void setup() {

  // ---------------------------------------------------
  // Watchdog Timer
  // ---------------------------------------------------

  // ถ้าโปรแกรมค้างเกิน 2 วินาที
  // Arduino จะ reset ตัวเอง
  wdt_enable(WDTO_2S);


  // ---------------------------------------------------
  // Serial USB
  // ---------------------------------------------------

  Serial.begin(9600);


  // ---------------------------------------------------
  // SoftwareSerial
  // ---------------------------------------------------

  arduinoSerial.begin(9600);


  // ---------------------------------------------------
  // IR
  // ---------------------------------------------------

  pinMode(IR_PIN, INPUT);


  // External Interrupt
  //
  // D2 = INT0
  //
  // CHANGE = ตรวจทั้ง HIGH -> LOW
  // และ LOW -> HIGH

  attachInterrupt(
    digitalPinToInterrupt(IR_PIN),
    irISR,
    CHANGE
  );


  // ---------------------------------------------------
  // Servo
  // ---------------------------------------------------

  gateServo.attach(SERVO_PIN);

  // เริ่มต้นปิดไม้กั้น
  gateServo.write(0);

  gateOpen = false;


  // ---------------------------------------------------
  // LCD
  // ---------------------------------------------------

  lcd.init();

  lcd.backlight();


  // แสดงครั้งแรก
  //
  // ไม่มี lcd.clear()
  // เพื่อป้องกันการกระพริบ

  lcd.setCursor(0, 0);
  lcd.print("SMART PARKING");

  lcd.setCursor(0, 1);
  lcd.print("Available: 0/4");


  // ---------------------------------------------------
  // EEPROM
  // ---------------------------------------------------

  loadEEPROM();


  // ---------------------------------------------------
  // Timer2
  // ---------------------------------------------------

  setupTimer2();


  // ---------------------------------------------------
  // Debug
  // ---------------------------------------------------

  Serial.println();
  Serial.println("==============================");
  Serial.println("ARDUINO 2 START");
  Serial.println("==============================");

  Serial.println("IR Pin      : D2");
  Serial.println("Servo Pin   : A0");
  Serial.println("Soft RX     : D10");
  Serial.println("Soft TX     : D11");
  Serial.println("LCD         : I2C 0x27");

  Serial.println("==============================");


  // อัปเดต LCD
  updateLCD();
}


// =====================================================
// LOOP
// =====================================================

void loop() {

  // ---------------------------------------------------
  // RESET WATCHDOG
  // ---------------------------------------------------

  wdt_reset();


  // ---------------------------------------------------
  // รับข้อมูลจาก Arduino 1
  // ---------------------------------------------------

  receiveArduino1();


  // ---------------------------------------------------
  // ประมวลผล Interrupt จาก IR
  // ---------------------------------------------------

  processIRInterrupt();


  // ---------------------------------------------------
  // ควบคุมไม้กั้น
  // ---------------------------------------------------

  updateGate();


  // ---------------------------------------------------
  // ส่งสถานะไป Raspberry Pi
  // ---------------------------------------------------

  if (millis() - lastPiStatus >= 500) {

    lastPiStatus = millis();

    sendPiStatus();
  }


  // ---------------------------------------------------
  // Debug
  // ---------------------------------------------------

  if (millis() - lastDebug >= 1000) {

    lastDebug = millis();

    printDebug();
  }
}


// =====================================================
// RECEIVE DATA FROM ARDUINO 1
// =====================================================
//
// Arduino 1 ส่ง:
//
// A=3
//
// หมายถึงมีช่องว่าง 3 ช่อง
// =====================================================

void receiveArduino1() {

  if (!arduinoSerial.available()) {
    return;
  }


  String data = arduinoSerial.readStringUntil('\n');

  data.trim();


  // ตรวจสอบ A=
  if (data.startsWith("A=")) {

    int newAvailable = data.substring(2).toInt();


    // ป้องกันค่าผิดปกติ
    if (newAvailable < 0) {
      newAvailable = 0;
    }

    if (newAvailable > 4) {
      newAvailable = 4;
    }


    // -------------------------------------------------
    // สำคัญ
    // -------------------------------------------------
    //
    // ถ้าค่าเปลี่ยนเท่านั้น
    // จึงอัปเดต LCD
    //
    // ทำให้ LCD ไม่ต้องเขียนซ้ำทุก 500 ms

    if (newAvailable != availableSlots) {

      availableSlots = newAvailable;

      updateLCD();
    }
  }
}


// =====================================================
// PROCESS IR INTERRUPT
// =====================================================

void processIRInterrupt() {

  if (!irInterruptFlag) {
    return;
  }


  // ปิด flag
  irInterruptFlag = false;


  // อ่านค่าปัจจุบัน
  bool currentCarState = (digitalRead(IR_PIN) == LOW);


  // ---------------------------------------------------
  // ตรวจจับรถ
  // ---------------------------------------------------

  if (currentCarState && !carAtIR) {

    carAtIR = true;


    // เพิ่ม counter
    eepromData.irDetectionCount++;

    saveEEPROM();


    Serial.println("IR: CAR DETECTED");
  }


  // ---------------------------------------------------
  // รถออกจาก IR
  // ---------------------------------------------------

  else if (!currentCarState && carAtIR) {

    carAtIR = false;

    clearStart = millis();


    Serial.println("IR: CAR CLEARED");
  }
}


// =====================================================
// UPDATE GATE
// =====================================================

void updateGate() {

  // ---------------------------------------------------
  // มีรถที่ IR
  // ---------------------------------------------------

  if (carAtIR) {

    // ถ้าไม้กั้นยังปิด
    if (!gateOpen) {

      openGate();
    }


    // reset timer
    clearStart = 0;
  }


  // ---------------------------------------------------
  // ไม่มีรถที่ IR
  // ---------------------------------------------------

  else {

    // ถ้าไม้กั้นเปิดอยู่
    if (gateOpen) {

      // เริ่มจับเวลาหลังรถพ้น
      if (clearStart == 0) {

        clearStart = millis();
      }


      // รถพ้นเกินเวลาที่กำหนด
      if (millis() - clearStart >= CLOSE_DELAY) {

        closeGate();
      }
    }
  }
}


// =====================================================
// OPEN GATE
// =====================================================

void openGate() {

  if (gateOpen) {
    return;
  }


  gateServo.write(90);

  gateOpen = true;


  // EEPROM counter
  eepromData.gateOpenCount++;

  saveEEPROM();


  Serial.println("GATE: OPEN");


  // ส่ง event ไป Raspberry Pi
  Serial.println("A2,EVENT,GATE_OPEN");
}


// =====================================================
// CLOSE GATE
// =====================================================

void closeGate() {

  if (!gateOpen) {
    return;
  }


  gateServo.write(0);

  gateOpen = false;

  clearStart = 0;


  // EEPROM counter
  eepromData.gateCloseCount++;

  saveEEPROM();


  Serial.println("GATE: CLOSE");


  // ส่ง event ไป Raspberry Pi
  Serial.println("A2,EVENT,GATE_CLOSE");
}


// =====================================================
// UPDATE LCD
// =====================================================
//
// จุดสำคัญของการแก้ครั้งนี้
//
// ❌ ไม่มี lcd.clear()
// ❌ ไม่เขียนซ้ำทุก 500 ms
//
// Arduino จะเรียกฟังก์ชันนี้
// เฉพาะตอนที่จำนวนช่องว่างเปลี่ยน
// =====================================================

void updateLCD() {

  static int lastAvailableSlots = -1;

  static bool firstDisplay = true;


  // ถ้าไม่ใช่ครั้งแรก
  // และค่าจำนวนช่องว่างไม่เปลี่ยน
  // ไม่ต้องทำอะไร

  if (
    !firstDisplay &&
    lastAvailableSlots == availableSlots
  ) {

    return;
  }


  firstDisplay = false;

  lastAvailableSlots = availableSlots;


  // ---------------------------------------------------
  // บรรทัดที่ 1
  // ---------------------------------------------------

  lcd.setCursor(0, 0);

  lcd.print("SMART PARKING   ");


  // ---------------------------------------------------
  // บรรทัดที่ 2
  // ---------------------------------------------------

  lcd.setCursor(0, 1);

  lcd.print("Available: ");

  lcd.print(availableSlots);

  lcd.print("/4   ");
}


// =====================================================
// SEND STATUS TO RASPBERRY PI
// =====================================================
//
// Format:
//
// A2,STATUS,IR=0,GATE=CLOSED,A=3,IRC=5,OPEN=2,CLOSE=2
//
// IR
// 0 = ไม่มีรถ
// 1 = มีรถ
//
// GATE
// OPEN / CLOSED
//
// A
// จำนวนช่องว่าง
//
// IRC
// จำนวนครั้งตรวจจับ IR
//
// OPEN
// จำนวนครั้งเปิดไม้กั้น
//
// CLOSE
// จำนวนครั้งปิดไม้กั้น
// =====================================================

void sendPiStatus() {

  Serial.print("A2,STATUS");

  Serial.print(",IR=");

  Serial.print(carAtIR ? 1 : 0);

  Serial.print(",GATE=");

  if (gateOpen) {
    Serial.print("OPEN");
  }
  else {
    Serial.print("CLOSED");
  }

  Serial.print(",A=");

  Serial.print(availableSlots);

  Serial.print(",IRC=");

  Serial.print(eepromData.irDetectionCount);

  Serial.print(",OPEN=");

  Serial.print(eepromData.gateOpenCount);

  Serial.print(",CLOSE=");

  Serial.println(eepromData.gateCloseCount);
}


// =====================================================
// DEBUG
// =====================================================

void printDebug() {

  Serial.print("IR=");

  Serial.print(carAtIR ? "CAR" : "CLEAR");


  Serial.print(" | GATE=");

  Serial.print(gateOpen ? "OPEN" : "CLOSED");


  Serial.print(" | AVAILABLE=");

  Serial.print(availableSlots);


  Serial.print(" | IRC=");

  Serial.print(eepromData.irDetectionCount);


  Serial.print(" | OPEN=");

  Serial.print(eepromData.gateOpenCount);


  Serial.print(" | CLOSE=");

  Serial.print(eepromData.gateCloseCount);


  Serial.print(" | BOOT=");

  Serial.println(eepromData.bootCount);
}