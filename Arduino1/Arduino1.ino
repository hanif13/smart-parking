#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>

// =====================================================
// ARDUINO 1: ระบบที่จอดรถอัจฉริยะ (SMART PARKING SYSTEM)
// หน้าที่:
// 1. ตรวจสอบช่องจอด 4 ช่องด้วย Ultrasonic
// 2. ส่งจำนวนช่องว่างไป Arduino 2
// 3. ส่งสถานะโครงสร้างข้อมูลไป Raspberry Pi
// =====================================================

// การกำหนดขาพิน (Pin Configuration)
const byte SLOT1_TRIG = 2;
const byte SLOT1_ECHO = 3;
const byte SLOT2_TRIG = 4;
const byte SLOT2_ECHO = 5;
const byte SLOT3_TRIG = 6;
const byte SLOT3_ECHO = 7;
const byte SLOT4_TRIG = 8;
const byte SLOT4_ECHO = 9;

// พินการสื่อสาร SoftwareSerial (A1 A4=TX, A1 A5=RX)
const byte COMM_RX = A5;
const byte COMM_TX = A4;

SoftwareSerial parkingSerial(COMM_RX, COMM_TX);

// การตั้งค่าและค่าคงที่
const int PARKING_THRESHOLD = 10;     // ระยะที่ถือว่ามีรถจอด (ซม.)
const uint16_t EEPROM_MAGIC = 0xA151; // ค่าตรวจสอบความถูกต้องของ EEPROM
const int EEPROM_ADDRESS = 0;         // ตำแหน่งเริ่มต้นบันทึกใน EEPROM

// ตัวแปรสถานะช่องจอดปัจจุบัน
bool slot1Full = false;
bool slot2Full = false;
bool slot3Full = false;
bool slot4Full = false;

// ตัวแปรเก็บสถานะก่อนหน้า
bool oldSlot1Full = false;
bool oldSlot2Full = false;
bool oldSlot3Full = false;
bool oldSlot4Full = false;

// ตัวนับเวลา Millis สำหรับ Timer1
volatile unsigned long timer1Millis = 0;

// โครงสร้างข้อมูล EEPROM
struct EepromData {
  uint16_t magic;
  uint32_t bootCount;
  uint32_t parkingChangeCount;
};

EepromData eepromData;

// Interrupt Service Routine สำหรับ Hardware Timer1 Compare Match A
ISR(TIMER1_COMPA_vect) {
  timer1Millis++;
}

// อ่านค่าเวลา Millis จาก Timer1 แบบปลอดภัย (ป้องกันข้อมูลขัดแย้งขณะเกิด Interrupt)
unsigned long getTimerMillis() {
  unsigned long value;
  noInterrupts();
  value = timer1Millis;
  interrupts();
  return value;
}

// ตั้งค่า Hardware Timer1 (สร้างจังหวะนับเวลาทุกๆ 1 ms)
void setupTimer1() {
  noInterrupts();
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 249;                         // 1 ms ที่ความถี่ 16MHz และ Prescaler 64
  TCCR1B |= (1 << WGM12);              // โหมด CTC (Clear Timer on Compare Match)
  TCCR1B |= (1 << CS11) | (1 << CS10);  // ตั้งค่า Prescaler เป็น 64
  TIMSK1 |= (1 << OCIE1A);             // เปิดใช้งาน Interrupt Compare Match A
  interrupts();
}

// โหลดข้อมูลจาก EEPROM
void loadEEPROM() {
  EEPROM.get(EEPROM_ADDRESS, eepromData);
  // หากยังไม่เคยตั้งค่า EEPROM มาก่อน ให้ทำการกำหนดค่าเริ่มต้น
  if (eepromData.magic != EEPROM_MAGIC) {
    eepromData.magic = EEPROM_MAGIC;
    eepromData.bootCount = 0;
    eepromData.parkingChangeCount = 0;
    EEPROM.put(EEPROM_ADDRESS, eepromData);
  }
}

// บันทึกจำนวนครั้งที่เครื่องเริ่มทำงาน (Boot Count)
void saveBootCount() {
  eepromData.bootCount++;
  EEPROM.put(EEPROM_ADDRESS, eepromData);
}

// บันทึกจำนวนครั้งที่มีการเปลี่ยนแปลงสถานะที่จอดรถ
void saveParkingChange() {
  eepromData.parkingChangeCount++;
  EEPROM.put(EEPROM_ADDRESS, eepromData);
}

// อ่านระยะทางจากเซนเซอร์ Ultrasonic (หน่วยเป็น ซม.)
float readDistance(byte trigPin, byte echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(5);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  unsigned long duration = pulseIn(echoPin, HIGH, 25000UL);
  if (duration == 0) return 999; // คืนค่า 999 หากเกิด Timeout หรือวัดค่าไม่ได้
  return duration * 0.0343 / 2.0;
}

// ตรวจสอบช่องจอด (คืนค่า true หากมีรถจอดอยู่)
bool checkParkingSlot(byte trigPin, byte echoPin) {
  float distance = readDistance(trigPin, echoPin);
  Serial.print("Distance = ");
  Serial.print(distance);
  Serial.println(" cm");
  return (distance < PARKING_THRESHOLD);
}

// คำนวณจำนวนช่องจอดที่ยังว่างอยู่
int getAvailableSlots() {
  int available = 4;
  if (slot1Full) available--;
  if (slot2Full) available--;
  if (slot3Full) available--;
  if (slot4Full) available--;
  return available;
}

// ตรวจสอบว่ามีการเปลี่ยนแปลงสถานะการจอดหรือไม่
void checkParkingStatusChange() {
  bool changed = (slot1Full != oldSlot1Full) || 
                 (slot2Full != oldSlot2Full) || 
                 (slot3Full != oldSlot3Full) || 
                 (slot4Full != oldSlot4Full);

  if (changed) {
    eepromData.parkingChangeCount++;
    EEPROM.put(EEPROM_ADDRESS, eepromData);

    Serial.print("PARKING CHANGE #");
    Serial.println(eepromData.parkingChangeCount);

    // อัปเดตสถานะก่อนหน้า
    oldSlot1Full = slot1Full;
    oldSlot2Full = slot2Full;
    oldSlot3Full = slot3Full;
    oldSlot4Full = slot4Full;
  }
}

// ส่งสถานะจำนวนช่องว่างไปที่ Arduino 2 (เช่น A=3)
void sendParkingStatus() {
  parkingSerial.print("A=");
  parkingSerial.println(getAvailableSlots());
}

// ส่งสถานะแบบโครงสร้างข้อมูลไปที่ Raspberry Pi (เช่น A1,STATUS,S1=0,S2=1,S3=0,S4=0,A=3,CHG=12)
void sendPiStatus() {
  Serial.print("A1,STATUS,");
  Serial.print("S1="); Serial.print(slot1Full ? 1 : 0);
  Serial.print(",S2="); Serial.print(slot2Full ? 1 : 0);
  Serial.print(",S3="); Serial.print(slot3Full ? 1 : 0);
  Serial.print(",S4="); Serial.print(slot4Full ? 1 : 0);
  Serial.print(",A="); Serial.print(getAvailableSlots());
  Serial.print(",CHG="); Serial.println(eepromData.parkingChangeCount);
}

// แสดงข้อมูลเพื่อตรวจสอบสถานะการทำงาน (Debug) ผ่าน Serial Monitor
void printStatus() {
  Serial.println();
  Serial.println("================================");
  Serial.println("       PARKING SYSTEM");
  Serial.println("================================");
  Serial.print("Slot 1 : "); Serial.println(slot1Full ? "FULL" : "FREE");
  Serial.print("Slot 2 : "); Serial.println(slot2Full ? "FULL" : "FREE");
  Serial.print("Slot 3 : "); Serial.println(slot3Full ? "FULL" : "FREE");
  Serial.print("Slot 4 : "); Serial.println(slot4Full ? "FULL" : "FREE");
  Serial.print("Available : "); Serial.print(getAvailableSlots()); Serial.println("/4");
  Serial.print("Boot Count : "); Serial.println(eepromData.bootCount);
  Serial.print("Parking Change : "); Serial.println(eepromData.parkingChangeCount);
  Serial.print("Timer1 : "); Serial.print(getTimerMillis()); Serial.println(" ms");
  Serial.println("================================");
}

void setup() {
  // เปิดใช้งาน Watchdog Timer (รีเซ็ตตัวเองหากโปรแกรมค้างเกิน 2 วินาที)
  wdt_enable(WDTO_2S);

  // การตั้งค่าการสื่อสาร Serial
  Serial.begin(9600);          // สื่อสารกับ Raspberry Pi ผ่าน USB
  parkingSerial.begin(9600);   // สื่อสารกับ Arduino 2 ผ่าน SoftwareSerial

  // กำหนดโหมดพินสำหรับเซนเซอร์ Ultrasonic
  pinMode(SLOT1_TRIG, OUTPUT); pinMode(SLOT1_ECHO, INPUT);
  pinMode(SLOT2_TRIG, OUTPUT); pinMode(SLOT2_ECHO, INPUT);
  pinMode(SLOT3_TRIG, OUTPUT); pinMode(SLOT3_ECHO, INPUT);
  pinMode(SLOT4_TRIG, OUTPUT); pinMode(SLOT4_ECHO, INPUT);

  // โหลดและอัปเดตข้อมูลใน EEPROM
  loadEEPROM();
  saveBootCount();

  // ตั้งค่า Hardware Timer1
  setupTimer1();

  // กำหนดค่าเริ่มต้นให้กับสถานะช่องจอด
  slot1Full = slot2Full = slot3Full = slot4Full = false;
  oldSlot1Full = oldSlot2Full = oldSlot3Full = oldSlot4Full = false;

  // แสดงข้อความแจ้งพร้อมใช้งาน
  Serial.println("================================");
  Serial.println("ARDUINO 1 READY");
  Serial.println("SMART PARKING SYSTEM");
  Serial.println("4 ULTRASONIC");
  Serial.println("TIMER1 = 1ms");
  Serial.println("WATCHDOG = 2s");
  Serial.println("EEPROM = ENABLED");
  Serial.println("UART = 9600");
  Serial.print("THRESHOLD = "); Serial.print(PARKING_THRESHOLD); Serial.println(" CM");
  Serial.println("================================");
}

void loop() {
  // เคลียร์ Watchdog Timer (ป้อนอาหารหมาเฝ้าบ้าน ป้องกันการรีเซ็ต)
  wdt_reset();

  // ตรวจสอบสถานะช่องจอดทั้ง 4 ช่อง
  slot1Full = checkParkingSlot(SLOT1_TRIG, SLOT1_ECHO); delay(50);
  slot2Full = checkParkingSlot(SLOT2_TRIG, SLOT2_ECHO); delay(50);
  slot3Full = checkParkingSlot(SLOT3_TRIG, SLOT3_ECHO); delay(50);
  slot4Full = checkParkingSlot(SLOT4_TRIG, SLOT4_ECHO);

  // ตรวจสอบการเปลี่ยนแปลงสถานะ
  checkParkingStatusChange();

  unsigned long now = getTimerMillis();

  // ส่งข้อมูลให้ Arduino 2 ทุกๆ 500 ms
  static unsigned long lastSend = 0;
  if (now - lastSend >= 500) {
    lastSend = now;
    sendParkingStatus();
  }

  // ส่งข้อมูลให้ Raspberry Pi ทุกๆ 500 ms
  static unsigned long lastPiSend = 0;
  if (now - lastPiSend >= 500) {
    lastPiSend = now;
    sendPiStatus();
  }

  // แสดงผล Debug ผ่าน Serial ทุกๆ 1 วินาที
  static unsigned long lastPrint = 0;
  if (now - lastPrint >= 1000) {
    lastPrint = now;
    printStatus();
  }
}
