#include <SoftwareSerial.h>
#include <EEPROM.h>
#include <avr/wdt.h>
#include <avr/interrupt.h>


// =====================================================
// ARDUINO 1
// SMART PARKING SYSTEM
//
// หน้าที่
// 1. ตรวจช่องจอด 4 ช่องด้วย Ultrasonic
// 2. ส่ง Available ไป Arduino 2
// 3. ส่ง Structured Status ไป Raspberry Pi
//
// เพิ่มหัวข้อจากวิชา
// 1. Interrupt
// 2. Hardware Timer / Counter
// 3. Watchdog Timer
// 4. EEPROM
// 5. UART / USART
// 6. SoftwareSerial
//
// Sleep Mode : ยังไม่ใช้
// =====================================================


// =====================================================
// 1. PIN ULTRASONIC
// =====================================================

const byte SLOT1_TRIG = 2;
const byte SLOT1_ECHO = 3;

const byte SLOT2_TRIG = 4;
const byte SLOT2_ECHO = 5;

const byte SLOT3_TRIG = 6;
const byte SLOT3_ECHO = 7;

const byte SLOT4_TRIG = 8;
const byte SLOT4_ECHO = 9;


// =====================================================
// 2. COMMUNICATION
//
// Arduino 1 -> Arduino 2
//
// A1 A4 = TX
// A1 A5 = RX
// =====================================================

const byte COMM_RX = A5;
const byte COMM_TX = A4;

SoftwareSerial parkingSerial(
  COMM_RX,
  COMM_TX
);


// =====================================================
// 3. PARKING SETTING
// =====================================================

// ระยะที่ถือว่ามีรถ
const int PARKING_THRESHOLD = 10;


// =====================================================
// 4. PARKING STATUS
// =====================================================

bool slot1Full = false;
bool slot2Full = false;
bool slot3Full = false;
bool slot4Full = false;


// เก็บสถานะก่อนหน้า
bool oldSlot1Full = false;
bool oldSlot2Full = false;
bool oldSlot3Full = false;
bool oldSlot4Full = false;


// =====================================================
// 5. HARDWARE TIMER
//
// Timer1
//
// 16 MHz
// Prescaler = 64
//
// 16,000,000 / 64 = 250,000 Hz
//
// OCR1A = 249
//
// 250 ticks = 1 ms
// =====================================================

volatile unsigned long timer1Millis = 0;


// Timer1 Compare Match A Interrupt

ISR(TIMER1_COMPA_vect)
{
  timer1Millis++;
}


// =====================================================
// อ่านค่า Timer แบบปลอดภัย
// =====================================================

unsigned long getTimerMillis()
{
  unsigned long value;

  noInterrupts();

  value = timer1Millis;

  interrupts();

  return value;
}


// =====================================================
// ตั้งค่า Timer1
// =====================================================

void setupTimer1()
{
  noInterrupts();

  // Reset Timer1 registers

  TCCR1A = 0;
  TCCR1B = 0;

  TCNT1 = 0;


  // Compare value
  // 1 ms

  OCR1A = 249;


  // CTC Mode

  TCCR1B |= (1 << WGM12);


  // Prescaler 64

  TCCR1B |=
    (1 << CS11) |
    (1 << CS10);


  // Enable Compare Match A interrupt

  TIMSK1 |= (1 << OCIE1A);


  interrupts();
}


// =====================================================
// 6. EEPROM DATA
// =====================================================

struct EepromData
{
  uint16_t magic;

  uint32_t bootCount;

  uint32_t parkingChangeCount;
};


EepromData eepromData;


// Magic number
const uint16_t EEPROM_MAGIC = 0xA151;


// EEPROM address

const int EEPROM_ADDRESS = 0;


// =====================================================
// โหลดข้อมูลจาก EEPROM
// =====================================================

void loadEEPROM()
{
  EEPROM.get(
    EEPROM_ADDRESS,
    eepromData
  );


  // ถ้า EEPROM ยังไม่เคย initialize

  if (
    eepromData.magic != EEPROM_MAGIC
  )
  {
    eepromData.magic =
      EEPROM_MAGIC;

    eepromData.bootCount = 0;

    eepromData.parkingChangeCount = 0;


    EEPROM.put(
      EEPROM_ADDRESS,
      eepromData
    );
  }
}


// =====================================================
// บันทึก Boot Count
// =====================================================

void saveBootCount()
{
  eepromData.bootCount++;

  EEPROM.put(
    EEPROM_ADDRESS,
    eepromData
  );
}


// =====================================================
// บันทึก Parking Change
// =====================================================

void saveParkingChange()
{
  eepromData.parkingChangeCount++;

  EEPROM.put(
    EEPROM_ADDRESS,
    eepromData
  );
}


// =====================================================
// 7. อ่าน Ultrasonic
// =====================================================

float readDistance(
  byte trigPin,
  byte echoPin
)
{
  digitalWrite(
    trigPin,
    LOW
  );

  delayMicroseconds(5);


  digitalWrite(
    trigPin,
    HIGH
  );

  delayMicroseconds(10);


  digitalWrite(
    trigPin,
    LOW
  );


  unsigned long duration =
    pulseIn(
      echoPin,
      HIGH,
      25000UL
    );


  if (duration == 0)
  {
    return 999;
  }


  float distance =
    duration *
    0.0343 /
    2.0;


  return distance;
}


// =====================================================
// 8. ตรวจช่องจอด
// =====================================================

bool checkParkingSlot(
  byte trigPin,
  byte echoPin
)
{
  float distance =
    readDistance(
      trigPin,
      echoPin
    );


  Serial.print(
    "Distance = "
  );

  Serial.print(
    distance
  );

  Serial.println(
    " cm"
  );


  if (
    distance <
    PARKING_THRESHOLD
  )
  {
    return true;
  }


  return false;
}


// =====================================================
// 9. จำนวนช่องว่าง
// =====================================================

int getAvailableSlots()
{
  int available = 4;


  if (slot1Full)
  {
    available--;
  }


  if (slot2Full)
  {
    available--;
  }


  if (slot3Full)
  {
    available--;
  }


  if (slot4Full)
  {
    available--;
  }


  return available;
}


// =====================================================
// 10. ตรวจว่ามีการเปลี่ยนสถานะหรือไม่
// =====================================================

void checkParkingStatusChange()
{
  bool changed = false;


  if (
    slot1Full !=
    oldSlot1Full
  )
  {
    changed = true;
  }


  if (
    slot2Full !=
    oldSlot2Full
  )
  {
    changed = true;
  }


  if (
    slot3Full !=
    oldSlot3Full
  )
  {
    changed = true;
  }


  if (
    slot4Full !=
    oldSlot4Full
  )
  {
    changed = true;
  }


  if (changed)
  {
    eepromData.parkingChangeCount++;

    EEPROM.put(
      EEPROM_ADDRESS,
      eepromData
    );


    Serial.print(
      "PARKING CHANGE #"
    );

    Serial.println(
      eepromData.parkingChangeCount
    );


    oldSlot1Full =
      slot1Full;

    oldSlot2Full =
      slot2Full;

    oldSlot3Full =
      slot3Full;

    oldSlot4Full =
      slot4Full;
  }
}


// =====================================================
// 11. ส่ง Available ไป Arduino 2
//
// รูปแบบเดิม:
//
// A=4
// A=3
// A=2
// A=1
// A=0
// =====================================================

void sendParkingStatus()
{
  parkingSerial.print(
    "A="
  );

  parkingSerial.println(
    getAvailableSlots()
  );
}


// =====================================================
// 12. ส่ง Structured Status ไป Raspberry Pi
//
// ตัวอย่าง:
//
// A1,STATUS,S1=0,S2=1,S3=0,S4=0,A=3,CHG=12
//
// เพิ่ม CHG เพื่อส่ง Counter
// =====================================================

void sendPiStatus()
{
  Serial.print(
    "A1,STATUS,"
  );


  Serial.print(
    "S1="
  );

  Serial.print(
    slot1Full ? 1 : 0
  );


  Serial.print(
    ",S2="
  );

  Serial.print(
    slot2Full ? 1 : 0
  );


  Serial.print(
    ",S3="
  );

  Serial.print(
    slot3Full ? 1 : 0
  );


  Serial.print(
    ",S4="
  );

  Serial.print(
    slot4Full ? 1 : 0
  );


  Serial.print(
    ",A="
  );

  Serial.print(
    getAvailableSlots()
  );


  // Counter จาก EEPROM

  Serial.print(
    ",CHG="
  );

  Serial.println(
    eepromData.parkingChangeCount
  );
}


// =====================================================
// 13. Debug Status
// =====================================================

void printStatus()
{
  Serial.println();

  Serial.println(
    "================================"
  );

  Serial.println(
    "       PARKING SYSTEM"
  );

  Serial.println(
    "================================"
  );


  Serial.print(
    "Slot 1 : "
  );

  Serial.println(
    slot1Full
      ? "FULL"
      : "FREE"
  );


  Serial.print(
    "Slot 2 : "
  );

  Serial.println(
    slot2Full
      ? "FULL"
      : "FREE"
  );


  Serial.print(
    "Slot 3 : "
  );

  Serial.println(
    slot3Full
      ? "FULL"
      : "FREE"
  );


  Serial.print(
    "Slot 4 : "
  );

  Serial.println(
    slot4Full
      ? "FULL"
      : "FREE"
  );


  Serial.print(
    "Available : "
  );

  Serial.print(
    getAvailableSlots()
  );

  Serial.println(
    "/4"
  );


  Serial.print(
    "Boot Count : "
  );

  Serial.println(
    eepromData.bootCount
  );


  Serial.print(
    "Parking Change : "
  );

  Serial.println(
    eepromData.parkingChangeCount
  );


  Serial.print(
    "Timer1 : "
  );

  Serial.print(
    getTimerMillis()
  );

  Serial.println(
    " ms"
  );


  Serial.println(
    "================================"
  );
}


// =====================================================
// 14. SETUP
// =====================================================

void setup()
{
  // ---------------------------------------------------
  // Watchdog
  // ---------------------------------------------------

  // ถ้า Arduino ค้างเกินประมาณ 2 วินาที
  // จะ reset ตัวเอง

  wdt_enable(
    WDTO_2S
  );


  // ---------------------------------------------------
  // UART / USART
  // ---------------------------------------------------

  // USB Serial -> Raspberry Pi

  Serial.begin(
    9600
  );


  // ---------------------------------------------------
  // SoftwareSerial
  // ---------------------------------------------------

  parkingSerial.begin(
    9600
  );


  // ---------------------------------------------------
  // Ultrasonic
  // ---------------------------------------------------

  pinMode(
    SLOT1_TRIG,
    OUTPUT
  );

  pinMode(
    SLOT1_ECHO,
    INPUT
  );


  pinMode(
    SLOT2_TRIG,
    OUTPUT
  );

  pinMode(
    SLOT2_ECHO,
    INPUT
  );


  pinMode(
    SLOT3_TRIG,
    OUTPUT
  );

  pinMode(
    SLOT3_ECHO,
    INPUT
  );


  pinMode(
    SLOT4_TRIG,
    OUTPUT
  );

  pinMode(
    SLOT4_ECHO,
    INPUT
  );


  // ---------------------------------------------------
  // EEPROM
  // ---------------------------------------------------

  loadEEPROM();

  saveBootCount();


  // ---------------------------------------------------
  // Hardware Timer1
  // ---------------------------------------------------

  setupTimer1();


  // ---------------------------------------------------
  // Initial status
  // ---------------------------------------------------

  slot1Full = false;
  slot2Full = false;
  slot3Full = false;
  slot4Full = false;


  oldSlot1Full =
    slot1Full;

  oldSlot2Full =
    slot2Full;

  oldSlot3Full =
    slot3Full;

  oldSlot4Full =
    slot4Full;


  Serial.println(
    "================================"
  );

  Serial.println(
    "ARDUINO 1 READY"
  );

  Serial.println(
    "SMART PARKING SYSTEM"
  );

  Serial.println(
    "4 ULTRASONIC"
  );

  Serial.println(
    "TIMER1 = 1ms"
  );

  Serial.println(
    "WATCHDOG = 2s"
  );

  Serial.println(
    "EEPROM = ENABLED"
  );

  Serial.println(
    "UART = 9600"
  );

  Serial.print(
    "THRESHOLD = "
  );

  Serial.print(
    PARKING_THRESHOLD
  );

  Serial.println(
    " CM"
  );

  Serial.println(
    "================================"
  );
}


// =====================================================
// 15. LOOP
// =====================================================

void loop()
{
  // ---------------------------------------------------
  // Feed Watchdog
  // ---------------------------------------------------

  wdt_reset();


  // ---------------------------------------------------
  // Slot 1
  // ---------------------------------------------------

  slot1Full =
    checkParkingSlot(
      SLOT1_TRIG,
      SLOT1_ECHO
    );

  delay(50);


  // ---------------------------------------------------
  // Slot 2
  // ---------------------------------------------------

  slot2Full =
    checkParkingSlot(
      SLOT2_TRIG,
      SLOT2_ECHO
    );

  delay(50);


  // ---------------------------------------------------
  // Slot 3
  // ---------------------------------------------------

  slot3Full =
    checkParkingSlot(
      SLOT3_TRIG,
      SLOT3_ECHO
    );

  delay(50);


  // ---------------------------------------------------
  // Slot 4
  // ---------------------------------------------------

  slot4Full =
    checkParkingSlot(
      SLOT4_TRIG,
      SLOT4_ECHO
    );


  // ---------------------------------------------------
  // ตรวจการเปลี่ยนสถานะ
  // ---------------------------------------------------

  checkParkingStatusChange();


  // ---------------------------------------------------
  // ใช้ Hardware Timer1
  // ---------------------------------------------------

  unsigned long now =
    getTimerMillis();


  // ---------------------------------------------------
  // ส่ง Arduino 2 ทุก 500 ms
  // ---------------------------------------------------

  static unsigned long lastSend = 0;


  if (
    now - lastSend >= 500
  )
  {
    lastSend = now;

    sendParkingStatus();
  }


  // ---------------------------------------------------
  // ส่ง Raspberry Pi ทุก 500 ms
  // ---------------------------------------------------

  static unsigned long lastPiSend = 0;


  if (
    now - lastPiSend >= 500
  )
  {
    lastPiSend = now;

    sendPiStatus();
  }


  // ---------------------------------------------------
  // Debug ทุก 1 วินาที
  // ---------------------------------------------------

  static unsigned long lastPrint = 0;


  if (
    now - lastPrint >= 1000
  )
  {
    lastPrint = now;

    printStatus();
  }
}