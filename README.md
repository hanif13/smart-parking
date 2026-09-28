# Smart Parking System

ระบบต้นแบบลานจอดรถอัจฉริยะ (Smart Parking) ใช้ Arduino 2 บอร์ดร่วมกับ Raspberry
Pi 3B เพื่อตรวจสอบช่องจอด ควบคุมไม้กั้น แสดงผลผ่าน LCD จัดเก็บข้อมูล SQLite
และแสดงผลผ่าน Web Dashboard

## 1. ภาพรวมระบบ

``` text
Arduino 1
  ├─ Ultrasonic S1-S4
  ├─ LED เขียว/แดง 8 ดวง
  └─ SoftwareSerial
        │
        ▼
Arduino 2
  ├─ IR Sensor
  ├─ Servo Gate
  ├─ LCD 16x2 I2C
  ├─ External Interrupt
  ├─ Timer2
  ├─ Watchdog
  └─ EEPROM
        │ USB Serial
        ▼
Raspberry Pi 3B
  ├─ Python
  ├─ Flask
  ├─ SQLite
  ├─ Parking Analytics
  └─ Web Dashboard
```

## 2. โครงสร้างโปรเจกต์

``` text
smart-parking/
├── app/
│   ├── app.py
│   ├── database.py
│   ├── serial_manager.py
│   ├── parking_analytics.py
│   ├── templates/
│   │   └── dashboard.html
│   └── static/
│       ├── dashboard.js
│       └── style.css
├── data/
│   └── parking.db
└── requirements.txt
```

> ไม่ควรนำ `venv/` จาก Raspberry Pi ไปใช้บนเครื่องอื่น ให้สร้าง Virtual
> Environment ใหม่แทน

## 3. Arduino 1 --- การต่อขา

### Ultrasonic

  ช่อง     TRIG   ECHO
  ----- ------ ------
  S1        D2     D3
  S2        D4     D5
  S3        D6     D7
  S4        D8     D9

Threshold ปัจจุบัน: **10 cm**

-   ตรวจพบวัตถุภายใน Threshold → `FULL`
-   ไม่พบวัตถุภายใน Threshold → `FREE`

### LED

  ช่อง     LED เขียว   LED แดง
  ----- ---------- ---------
  S1           D10       D11
  S2           D12       D13
  S3            A0        A1
  S4            A2        A3

`FREE` = เขียว ON / แดง OFF\
`FULL` = เขียว OFF / แดง ON

### Arduino 1 ↔ Arduino 2

  Arduino 1   Arduino 2   หน้าที่
  ----------- ----------- ------------
  A4 TX       D10 RX      ส่งข้อมูล
  A5 RX       D11 TX      รับข้อมูล
  GND         GND         Ground ร่วม

Baud Rate: `9600`

## 4. Arduino 2 --- การต่อขา

### IR Sensor

  IR      Arduino 2
  ----- -----------
  OUT            D2
  VCC            5V
  GND           GND

D2 ใช้เป็น External Interrupt:

``` cpp
volatile bool irInterruptFlag = false;

void irISR() {
    irInterruptFlag = true;
}

attachInterrupt(digitalPinToInterrupt(IR_PIN), irISR, CHANGE);
```

### Servo

  Servo                 Arduino 2
  -------- ----------------------
  Signal                       A0
  VCC        5V / แหล่งจ่ายที่เหมาะสม
  GND                         GND

`0°` = ปิดไม้กั้น\
`90°` = เปิดไม้กั้น

หากใช้แหล่งจ่าย Servo แยก ต้องต่อ GND ร่วมกับ Arduino

### LCD 16x2 I2C

Address: `0x27`

  LCD   Arduino 2
  ----- -----------
  VCC   5V
  GND   GND
  SDA   SDA
  SCL   SCL

### SoftwareSerial

  Arduino 2   หน้าที่
  ----------- ------
  D10         RX
  D11         TX
  Baud        9600

## 5. Embedded Software

ระบบใช้คุณสมบัติของ Arduino/AVR ดังนี้

### External Interrupt

ใช้ IR ที่ D2 เพื่อรับเหตุการณ์โดยไม่ต้อง polling ตลอดเวลา:

``` cpp
volatile bool irInterruptFlag = false;

void irISR() {
    irInterruptFlag = true;
}
```

ISR ควรทำงานสั้นและให้ `loop()` ประมวลผลต่อ

### Timer2

ใช้ Timer2 สำหรับงานจับเวลาระดับมิลลิวินาที:

``` cpp
ISR(TIMER2_COMPA_vect) {
    // อัปเดตตัวนับเวลา
}
```

### Watchdog

ใช้ตรวจสอบกรณีโปรแกรมค้าง:

``` cpp
#include <avr/wdt.h>

void setup() {
    wdt_enable(WDTO_2S);
}

void loop() {
    // งานหลัก
    wdt_reset();
}
```

### EEPROM

ใช้เก็บค่าที่ต้องการคงอยู่หลัง Restart:

``` cpp
EEPROM.update(address, value);
```

## 6. รูปแบบข้อมูล Serial

Arduino 1 ตัวอย่าง:

``` text
A1,STATUS,S1=0,S2=1,S3=0,S4=0,A=3,CHG=...
```

Arduino 2 ตัวอย่าง:

``` text
A2,STATUS,IR=0,GATE=CLOSED,A=4,...
```

Event:

``` text
A2,EVENT,GATE_OPEN
A2,EVENT,GATE_CLOSE
```

## 7. Raspberry Pi

ตรวจสอบ Python:

``` bash
python3 --version
```

ติดตั้งเครื่องมือ:

``` bash
sudo apt update
sudo apt install -y python3 python3-pip python3-venv git
```

เข้าโปรเจกต์:

``` bash
cd ~/Desktop/smart-parking
```

สร้าง Virtual Environment:

``` bash
python3 -m venv venv
source venv/bin/activate
```

ติดตั้ง Dependencies:

``` bash
pip install --upgrade pip
pip install -r requirements.txt
```

## 8. USB Serial และสิทธิ์

ตรวจสอบ Arduino:

``` bash
ls /dev/ttyACM*
```

ตัวอย่างที่ใช้ในการพัฒนา:

``` text
/dev/ttyACM0 → Arduino 1
/dev/ttyACM1 → Arduino 2
```

ชื่อ Device อาจสลับกันได้หลังถอด/เสียบ USB ใหม่

หาก Python เปิด Serial ไม่ได้:

``` bash
groups
```

เพิ่มผู้ใช้เข้า `dialout`:

``` bash
sudo usermod -aG dialout $USER
```

จากนั้น Logout/Login ใหม่ หรือ:

``` bash
sudo reboot
```

## 9. อัปโหลดโปรแกรม Arduino

สำหรับ Arduino ทั้งสองบอร์ด:

1.  เปิด Arduino IDE
2.  เลือก Board: Arduino UNO
3.  เลือก Port ของบอร์ด
4.  Compile
5.  Upload
6.  ตรวจสอบ Serial Monitor ที่ `9600 baud`

จากนั้นเชื่อมต่อ Arduino ทั้งสองเข้ากับ Raspberry Pi ผ่าน USB

## 10. รันระบบ

``` bash
cd ~/Desktop/smart-parking
source venv/bin/activate
python -m app.app
```

ระบบจะเปิด Flask ที่ Port `5000`

ตรวจสอบ IP:

``` bash
hostname -I
```

จากเครื่องใน Network เดียวกัน:

``` text
http://<RASPBERRY_PI_IP>:5000
```

ตัวอย่าง:

``` text
http://10.144.249.228:5000
```

## 11. API สำคัญ

สถานะระบบ:

``` text
GET /api/status
```

ช่องจอด:

``` text
GET /api/parking
```

ไม้กั้น:

``` text
GET /api/gate
```

Events:

``` text
GET /api/events?limit=10
```

ฐานข้อมูล:

``` text
GET /api/database
```

Health:

``` text
GET /api/health
```

ตัวอย่าง:

``` bash
curl http://localhost:5000/api/health
curl http://localhost:5000/api/status
curl http://localhost:5000/api/parking
```

## 12. Parking Analytics

ระบบสร้าง Parking Session จากการเปลี่ยนสถานะช่องจอด:

``` text
FREE → FULL
     = เริ่ม Parking Session

FULL → FREE
     = จบ Parking Session
```

สถิติที่ระบบรองรับ เช่น:

-   รถ/Session ที่เริ่มจอดวันนี้
-   Session ที่สิ้นสุดวันนี้
-   จำนวนที่กำลังจอด
-   เวลาเฉลี่ยที่จอด
-   เวลาที่จอดนานที่สุด
-   Session ล่าสุด
-   สรุปรายวัน

> `cars_entered` ในระบบนี้หมายถึงจำนวนครั้งที่เริ่ม Parking Session จาก
> `FREE → FULL` ไม่ใช่จำนวนรถที่ผ่านประตูโดยตรง เนื่องจากต้นแบบยังไม่มี Vehicle ID
> และระบบระบุทิศทางเข้า/ออกที่สมบูรณ์

## 13. Database

ไฟล์ฐานข้อมูล:

``` text
data/parking.db
```

ตารางหลัก:

``` text
events
gate_status
parking_sessions
parking_slot_state
parking_status
```

อย่าลบ `parking.db` หากต้องการเก็บข้อมูลเดิม

## 14. Troubleshooting

ตรวจสอบ USB:

``` bash
ls /dev/ttyACM*
```

ตรวจสอบ Flask:

``` bash
ps aux | grep "app.app"
```

ตรวจสอบ Port:

``` bash
ss -ltnp | grep 5000
```

ตรวจสอบ API:

``` bash
curl http://localhost:5000/api/health
```

ดู Log โดยรัน:

``` bash
python -m app.app
```

หยุดระบบ:

``` text
Ctrl + C
```

ออกจาก venv:

``` bash
deactivate
```

## 15. Git

`.gitignore` ที่แนะนำ:

``` gitignore
venv/
__pycache__/
*.pyc
*.pyo
.DS_Store
.vscode/
.idea/
```

Commit:

``` bash
git add .
git commit -m "Update smart parking system"
git push origin main
```

ไม่ควร Commit `venv/`, `__pycache__/`, `*.pyc` และไฟล์ที่ไม่จำเป็น

## 16. ข้อจำกัด

1.  รองรับช่องจอด 4 ช่อง
2.  Ultrasonic มีข้อจำกัดจากตำแหน่งและมุมติดตั้ง
3.  IR Sensor เพียงตัวเดียวไม่สามารถระบุทิศทางเข้า/ออกได้อย่างแน่นอน
4.  ยังไม่มี Vehicle ID หรือ License Plate Recognition
5.  Analytics อาศัยการเปลี่ยนสถานะของช่องจอด
6.  `cars_entered`/`cars_exited` เป็นข้อมูล Parking Session
7.  Raspberry Pi 3B มีทรัพยากรจำกัด

## 17. สรุป

Smart Parking System แบ่งการทำงานเป็น Arduino 1 สำหรับช่องจอด, Arduino 2
สำหรับไม้กั้นและอุปกรณ์ทางเข้า และ Raspberry Pi 3B สำหรับ Backend, Database,
Analytics และ Web Dashboard

ระบบครอบคลุมตั้งแต่ Hardware, Embedded Software, Serial Communication,
Python/Flask, SQLite, Parking Analytics และ Web Dashboard
เพื่อเป็นต้นแบบระบบ Smart Parking แบบครบวงจร
