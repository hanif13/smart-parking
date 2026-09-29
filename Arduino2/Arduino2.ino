#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

// =====================================================
// ARDUINO 2: ระบบควบคุมไม้กั้นที่จอดรถ (SMART PARKING GATE CONTROLLER)
// หน้าที่:
// 1. รับจำนวนช่องว่างจาก Arduino 1
// 2. ตรวจจับรถด้วยเซนเซอร์ IR
// 3. ควบคุม Servo เปิด/ปิดไม้กั้น
// 4. แสดงจำนวนช่องว่างบนหน้าจอ LCD
// 5. ส่งสถานะไปยัง Raspberry Pi
// =====================================================

// กำหนดขาพิน (Pin Configuration)
const byte IR_PIN = 2;       // IR Sensor (ใช้ INT0)
const byte SERVO_PIN = A0;   // Servo Motor
const byte SOFT_RX = 10;     // SoftwareSerial RX (รับจาก Arduino 1 TX)
const byte SOFT_TX = 11;     // SoftwareSerial TX (ส่งไป Arduino 1 RX)

// อุปกรณ์และไลบรารี
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo gateServo;
SoftwareSerial arduinoSerial(SOFT_RX, SOFT_TX);

// ตัวแปรระบบ
int availableSlots = 0;                 // จำนวนช่องจอดว่าง
volatile bool irInterruptFlag = false;    // Flag แจ้งเตือนเมื่อเกิด Interrupt จาก IR
bool carAtIR = false;                   // สถานะว่ามีรถตรง IR หรือไม่
bool gateOpen = false;                  // สถานะไม้กั้น (true = เปิด)
unsigned long clearStart = 0;           // เวลาที่เริ่มตรวจว่ารถพ้นเซนเซอร์ IR แล้ว
const unsigned long CLOSE_DELAY = 1500; // เวลาหน่วงก่อนปิดไม้กั้นหลังรถพ้น (1.5 วินาที)

// ตัวแปรเวลาและสถานะ
volatile unsigned long systemMillis = 0;
unsigned long lastPiStatus = 0;
unsigned long lastDebug = 0;

// โครงสร้างข้อมูล EEPROM
struct EEPROMData {
  unsigned long magic;
  unsigned long bootCount;
  unsigned long irDetectionCount;
  unsigned long gateOpenCount;
  unsigned long gateCloseCount;
};

const unsigned long EEPROM_MAGIC = 0x12345678;
const int EEPROM_ADDRESS = 0;
EEPROMData eepromData;

// ประกาศฟังก์ชัน (Function Prototypes)
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

// Timer2 Interrupt (ทำงานทุกๆ 1 ms)
ISR(TIMER2_COMPA_vect) {
  systemMillis++;
}

// External Interrupt จากเซนเซอร์ IR (พิน D2)
void irISR() {
  irInterruptFlag = true;
}

// ตั้งค่า Hardware Timer2 (สร้างฐานเวลา 1 ms)
void setupTimer2() {
  cli();
  TCCR2A = 0;
  TCCR2A |= (1 << WGM21);             // โหมด CTC
  TCCR2B = 0;
  TCCR2B |= (1 << CS22);              // Prescaler 64
  OCR2A = 249;                        // 1 ms ที่ความถี่ 16MHz
  TIMSK2 |= (1 << OCIE2A);            // เปิดใช้งาน Interrupt Compare Match A
  sei();
}

// โหลดข้อมูลจาก EEPROM
void loadEEPROM() {
  EEPROM.get(EEPROM_ADDRESS, eepromData);
  if (eepromData.magic != EEPROM_MAGIC) {
    eepromData.magic = EEPROM_MAGIC;
    eepromData.bootCount = 0;
    eepromData.irDetectionCount = 0;
    eepromData.gateOpenCount = 0;
    eepromData.gateCloseCount = 0;
    EEPROM.put(EEPROM_ADDRESS, eepromData);
  }
  eepromData.bootCount++;
  EEPROM.put(EEPROM_ADDRESS, eepromData);
}

// บันทึกข้อมูลลง EEPROM
void saveEEPROM() {
  EEPROM.put(EEPROM_ADDRESS, eepromData);
}

void setup() {
  // เปิดใช้งาน Watchdog Timer (รีเซ็ตตัวเองหากค้างเกิน 2 วินาที)
  wdt_enable(WDTO_2S);

  Serial.begin(9600);
  arduinoSerial.begin(9600);

  pinMode(IR_PIN, INPUT);
  // ตั้งค่า Interrupt ตรวจจับทั้งขอบขึ้นและขอบลง (CHANGE)
  attachInterrupt(digitalPinToInterrupt(IR_PIN), irISR, CHANGE);

  gateServo.attach(SERVO_PIN);
  gateServo.write(0); // เริ่มต้นปิดไม้กั้น
  gateOpen = false;

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("SMART PARKING");
  lcd.setCursor(0, 1); lcd.print("Available: 0/4");

  loadEEPROM();
  setupTimer2();

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

  updateLCD();
}

void loop() {
  wdt_reset(); // เคลียร์ Watchdog Timer

  receiveArduino1();      // รับข้อมูลจำนวนช่องว่างจาก Arduino 1
  processIRInterrupt();  // ประมวลผล Interrupt จาก IR
  updateGate();          // ควบคุมการเปิด/ปิดไม้กั้น

  // ส่งสถานะไป Raspberry Pi ทุกๆ 500 ms
  if (millis() - lastPiStatus >= 500) {
    lastPiStatus = millis();
    sendPiStatus();
  }

  // แสดงผล Debug ทุกๆ 1 วินาที
  if (millis() - lastDebug >= 1000) {
    lastDebug = millis();
    printDebug();
  }
}

// รับข้อมูลจาก Arduino 1 (รูปแบบ เช่น "A=3")
void receiveArduino1() {
  if (!arduinoSerial.available()) return;

  String data = arduinoSerial.readStringUntil('\n');
  data.trim();

  if (data.startsWith("A=")) {
    int newAvailable = data.substring(2).toInt();
    if (newAvailable < 0) newAvailable = 0;
    if (newAvailable > 4) newAvailable = 4;

    // อัปเดต LCD เฉพาะเมื่อจำนวนช่องจอดเปลี่ยนเท่านั้น
    if (newAvailable != availableSlots) {
      availableSlots = newAvailable;
      updateLCD();
    }
  }
}

// ประมวลผลเหตุการณ์จากเซนเซอร์ IR
void processIRInterrupt() {
  if (!irInterruptFlag) return;
  irInterruptFlag = false;

  bool currentCarState = (digitalRead(IR_PIN) == LOW); // LOW = มีวัตถุ/รถบัง

  // กรณีรถเพิ่งมาถึง IR
  if (currentCarState && !carAtIR) {
    carAtIR = true;
    eepromData.irDetectionCount++;
    saveEEPROM();
    Serial.println("IR: CAR DETECTED");
  } 
  // กรณีกำลังขับผ่านพ้น IR ไปแล้ว
  else if (!currentCarState && carAtIR) {
    carAtIR = false;
    clearStart = millis();
    Serial.println("IR: CAR CLEARED");
  }
}

// ควบคุมตรรกะไม้กั้น
void updateGate() {
  if (carAtIR) {
    if (!gateOpen) openGate();
    clearStart = 0;
  } else {
    if (gateOpen) {
      if (clearStart == 0) clearStart = millis();
      // เมื่อรถพ้นเซนเซอร์ครบตามเวลาหน่วง ให้ปิดไม้กั้น
      if (millis() - clearStart >= CLOSE_DELAY) {
        closeGate();
      }
    }
  }
}

// คำสั่งเปิดไม้กั้น
void openGate() {
  if (gateOpen) return;

  gateServo.write(90);
  gateOpen = true;

  eepromData.gateOpenCount++;
  saveEEPROM();

  Serial.println("GATE: OPEN");
  Serial.println("A2,EVENT,GATE_OPEN");
}

// คำสั่งปิดไม้กั้น
void closeGate() {
  if (!gateOpen) return;

  gateServo.write(0);
  gateOpen = false;
  clearStart = 0;

  eepromData.gateCloseCount++;
  saveEEPROM();

  Serial.println("GATE: CLOSE");
  Serial.println("A2,EVENT,GATE_CLOSE");
}

// อัปเดตการแสดงผลบนหน้าจอ LCD (อัปเดตเฉพาะเมื่อมีการเปลี่ยนแปลง)
void updateLCD() {
  static int lastAvailableSlots = -1;
  static bool firstDisplay = true;

  if (!firstDisplay && lastAvailableSlots == availableSlots) return;

  firstDisplay = false;
  lastAvailableSlots = availableSlots;

  lcd.setCursor(0, 0);
  lcd.print("SMART PARKING   ");
  lcd.setCursor(0, 1);
  lcd.print("Available: ");
  lcd.print(availableSlots);
  lcd.print("/4   ");
}

// ส่งสถานะแบบโครงสร้างข้อมูลไป Raspberry Pi
void sendPiStatus() {
  Serial.print("A2,STATUS");
  Serial.print(",IR="); Serial.print(carAtIR ? 1 : 0);
  Serial.print(",GATE="); Serial.print(gateOpen ? "OPEN" : "CLOSED");
  Serial.print(",A="); Serial.print(availableSlots);
  Serial.print(",IRC="); Serial.print(eepromData.irDetectionCount);
  Serial.print(",OPEN="); Serial.print(eepromData.gateOpenCount);
  Serial.print(",CLOSE="); Serial.println(eepromData.gateCloseCount);
}

// แสดงข้อมูลเพื่อการตรวจสอบ (Debug)
void printDebug() {
  Serial.print("IR="); Serial.print(carAtIR ? "CAR" : "CLEAR");
  Serial.print(" | GATE="); Serial.print(gateOpen ? "OPEN" : "CLOSED");
  Serial.print(" | AVAILABLE="); Serial.print(availableSlots);
  Serial.print(" | IRC="); Serial.print(eepromData.irDetectionCount);
  Serial.print(" | OPEN="); Serial.print(eepromData.gateOpenCount);
  Serial.print(" | CLOSE="); Serial.print(eepromData.gateCloseCount);
  Serial.print(" | BOOT="); Serial.println(eepromData.bootCount);
}
