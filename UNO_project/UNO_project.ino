#include <LiquidCrystal_I2C.h>
#include <DS1307RTC.h>
#include <Keypad_I2C.h>
#include <Keypad.h>
#include <Wire.h>
#include <TimeLib.h>
#include <avr/wdt.h>

#define I2CADDR 0x20
#define DS1307_ADDRESS 0x68

// ที่อยู่ใน RAM ของ DS1307 สำหรับการจัดเก็บเวลาและสถานะต่าง ๆ
#define HOUR_ADDRESS 0x08
#define MINUTE_ADDRESS 0x09
#define MORNING_ADDRESS 0x0A
#define NOON_ADDRESS 0x0B
#define EVENING_ADDRESS 0x0C
#define BEFORE_BED_ADDRESS 0x0D
#define ALARM_ADDRESS 0x0E
#define FAN_MODE_ADDRESS 0x0F

// การตั้งค่าค่าคงที่ของเวลาในการแจ้งเตือนยา
#define MORNING_TIME 8
#define NOON_TIME 12
#define EVENING_TIME 16
#define BEFOREBED_TIME 20


// ตัวแปรสำหรับโหมดการทำงาน
enum Mode {
  detection,  // โหมดตรวจจับการทำงาน
  readText    // โหมดอ่านข้อความจากภาพ
};

Mode deviceMode = detection;  // ตั้งโหมดเริ่มต้นเป็นโหมดตรวจจับ

// ค่าของปุ่มบนคีย์แพด
const byte ROWS = 4;
const byte COLS = 4;
char hexaKeys[ROWS][COLS] = {
  { '1', '2', '3', 'A' },
  { '4', '5', '6', 'B' },
  { '7', '8', '9', 'C' },
  { '*', '0', '#', 'D' }
};

// การกำหนดขาของคีย์แพด
byte rowPins[ROWS] = { 7, 6, 5, 4 };
byte colPins[COLS] = { 3, 2, 1, 0 };

// ตัวแปรของจอ LCD และคีย์แพด I2C
Keypad_I2C keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS, I2CADDR);
LiquidCrystal_I2C lcd(0x3F, 16, 2);

// ตัวแปรสำหรับตัวอักษรแบบกำหนดเอง
byte customChars[7][8] = {
  { B00000, B00000, B00001, B00010, B00000, B00000, B00000, B00000 },  // wifi_char_1
  { B00000, B01110, B10001, B00000, B01110, B10001, B00000, B00100 },  // wifi_char_2
  { B00000, B00000, B10000, B01000, B00000, B00000, B00000, B00000 },  // wifi_char_3
  { B00000, B00000, B00110, B00100, B00100, B00100, B01100, B00000 },  // fan_char_1
  { B00000, B00000, B10000, B11000, B00100, B00011, B00001, B00000 },  // fan_char_2
  { B00000, B00000, B00000, B00001, B11111, B10000, B00000, B00000 },  // fan_char_3
  { B00000, B00000, B00011, B00010, B00100, B01000, B11000, B00000 }   // fan_char_4
};

// ตัวแปรจัดเก็บสถานะเวลาและการแจ้งเตือน
tmElements_t tm;
bool morning, noon, evening, beforebed, alarm, alarmStopped;
int select = 1, position = 0, frame = 0;
bool edit = false, blink_tx = false, flag = false, wifi_status = false, fan_status = false;
int Hour, Minute;
unsigned long last_time = 0, connection_time = 0, refresh_time = 0;
int MotorPin2 = 3, fan_mode;
// ตัวแปรสำหรับเก็บอุณหภูมิ
String Temperature = "--.-";

// ฟังก์ชันเขียนข้อมูลลงใน RTC
void writeToDS1307RAM(byte address, byte data) {
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(address);
  Wire.write(data);
  Wire.endTransmission();
}

// ฟังก์ชันอ่านข้อมูลจาก RTC
byte readFromDS1307RAM(int address) {
  Wire.beginTransmission(DS1307_ADDRESS);
  Wire.write(address);
  Wire.endTransmission();
  Wire.requestFrom(DS1307_ADDRESS, 1);
  return Wire.read();
}

// ฟังก์ชันแสดงตัวเลขบนจอ LCD (รูปแบบ 2 หลัก)
void print2digits(int number) {
  if (number >= 0 && number < 10) {
    lcd.print('0');
  }
  lcd.print(number);
}

// ฟังก์ชันกระพริบหน้าจอ LCD ทุก 500 ms
void Blink() {
  if ((millis() - last_time) > 500) {
    blink_tx = !blink_tx;
    last_time = millis();
  }
}

//Interrupt เมื่อกดปุ่ม
void pressButton() {

  switch (deviceMode) {
    //ถ้าอยู่ในโหมดตรวจจับการล้ม ปุ่มนี้ทำหน้าที่หยุดการแจ้งเตือน
    case (detection):
      alarmStopped = true;
      Serial.println("STOP_ALARM");
      break;
    //ถ้าอยู่ในโหมดอ่านฉลาก ปุ่มนี้ทำหน้าที่บอกให้ Odroid บันทึกรูป
    case (readText):
      Serial.println("Capture");
      break;
  }
}

// ฟังก์ชันแสดงการแจ้งเตือนเมื่อถึงเวลารับประทานยา
void alarmAlert(int H, int M) {
  // ถ้า alarmStopped เป็น true (การเตือนถูกหยุด) ให้แสดงวันที่และเวลาในปัจจุบันบนหน้าจอ LCD
  if (alarmStopped) {
    lcd.setCursor(0, 0);
    lcd.print("Date: ");
    print2digits(tm.Day);
    lcd.print("/");
    print2digits(tm.Month);
    lcd.print("/");
    lcd.print(tmYearToCalendar(tm.Year));
    lcd.setCursor(0, 1);
    lcd.print("Time: ");
    print2digits(tm.Hour);
    lcd.print(":");
    print2digits(tm.Minute);
    lcd.print(" ");
  } else {
    // ถ้า alarmStopped เป็น false ให้แสดงข้อความเตือนการรับประทานยา
    Blink();
    if (blink_tx)
      lcd.clear();
    else {
      lcd.setCursor(1, 0);
      lcd.print("Time to taking");
      lcd.setCursor(4, 1);
      lcd.print("Medicine");
    }

    // ถ้า flag ยังเป็น false (ยังไม่ได้ส่งการแจ้งเตือน)
    if (!flag) {
      // ตรวจสอบเวลาที่ตรงกับเวลาที่ตั้งไว้ และส่งข้อความแจ้งเตือนไปยัง Serial
      if (H == MORNING_TIME && M == 0)
        Serial.println("NOTIFICATION:Morning");  // แจ้งเตือนตอนเช้า
      else if (H == NOON_TIME && M == 0)
        Serial.println("NOTIFICATION:Noon");  // แจ้งเตือนตอนเที่ยง
      else if (H == EVENING_TIME && M == 0)
        Serial.println("NOTIFICATION:Evening");  // แจ้งเตือนตอนเย็น
      else if (H == BEFOREBED_TIME && M == 0)
        Serial.println("NOTIFICATION:Beforebed");  // แจ้งเตือนก่อนนอน
      else {
        // ถ้าไม่ตรงกับเวลาใด ๆ ให้แจ้งเตือนโดยส่งเวลา Hour:Minute ไปยัง Serial
        String h = (Hour < 10) ? "0" + String(Hour) : String(Hour);
        String m = (Minute < 10) ? "0" + String(Minute) : String(Minute);
        Serial.println("NOTIFICATION:" + h + ":" + m);
      }
      flag = true;  // ตั้งค่า flag เป็น true เพื่อป้องกันการส่งซ้ำ
    }
  }
}
// ฟังก์ชันเปิดใช้งาน Watchdog Timer
void setupWatchdog() {
  wdt_enable(WDTO_8S);
  wdt_reset();
}

// ฟังก์ชันสำหรับประมวลผลข้อมูลที่รับเข้ามาผ่าน Serial
void processData(String data) {
  // ตรวจสอบว่าข้อมูลที่ได้รับเป็น "MODE:1" หรือไม่
  if (data == "MODE:1") {
    lcd.clear();             // clear LCD
    deviceMode = detection;  // เปลี่ยนโหมดการทำงานเป็น detection (ตรวจจับ)

    // ตรวจสอบว่าข้อมูลที่ได้รับเป็น "MODE:2" หรือไม่
  } else if (data == "MODE:2") {
    lcd.clear();            // clear LCD
    deviceMode = readText;  // เปลี่ยนโหมดการทำงานเป็น readText (อ่านข้อความ)

    // ตรวจสอบว่าข้อมูลที่ได้รับเป็น "WIFI" หรือไม่
  } else if (data == "WIFI") {
    if (!wifi_status) {    // ถ้าสถานะ Wi-Fi เป็น false (ไม่ได้เชื่อมต่อ)
      wifi_status = true;  // เปลี่ยนสถานะ Wi-Fi เป็น true (เชื่อมต่อแล้ว)
    }
    connection_time = millis();  // บันทึกเวลาที่ Wi-Fi เชื่อมต่อ

    // ตรวจสอบว่าข้อมูลที่ได้รับเป็น "LOST" และ Wi-Fi ยังเชื่อมต่ออยู่หรือไม่
  } else if (data == "LOST" && wifi_status) {
    wifi_status = false;  // เปลี่ยนสถานะ Wi-Fi เป็น false (การเชื่อมต่อหลุด)

    // ตรวจสอบว่าข้อมูลที่ได้รับขึ้นต้นด้วย "CPU:" หรือไม่
  } else if (data.startsWith("CPU:")) {
    int temp_index = data.indexOf("TEMP:") + 5;  // ค้นหาตำแหน่งของข้อมูลอุณหภูมิ "TEMP:"
    Temperature = data.substring(temp_index);    // ดึงค่าข้อมูลอุณหภูมิจากตำแหน่งที่เจอมาเก็บในตัวแปร Temperature
  }

  // ตรวจสอบเวลาผ่านไป 60 วินาทีตั้งแต่ Wi-Fi เชื่อมต่อ
  if (millis() - connection_time > 60000) {
    wifi_status = false;  // ถ้าผ่านไป 60 วินาทีแล้ว Wi-Fi ยังไม่เชื่อมต่อ ให้ตั้งสถานะ Wi-Fi เป็น false
  }
}

//ฟังก์ชันควบคุมการทำงานของพัดลม
void setFanSpeed() {
  // เลือกโหมดการทำงานของพัดลมจากค่า fan_mode
  switch (fan_mode) {

    // กรณี fan_mode = 0: โหมดควบคุมอัตโนมัติตามอุณหภูมิ
    case 0:
      if (Temperature != "--.-") {                // ถ้ามีการอ่านค่าอุณหภูมิได้
        if (Temperature.toFloat() > 60) {         // ถ้าอุณหภูมิสูงกว่า 60 องศา
          analogWrite(MotorPin2, 0);              // หยุดพัดลม (หมุนเร็วสุดคือ 0 สำหรับพัดลมนี้)
          fan_status = true;                      // สถานะพัดลมทำงาน
        } else if (Temperature.toFloat() < 50) {  // ถ้าอุณหภูมิต่ำกว่า 50 องศา
          analogWrite(MotorPin2, 255);            // ปิดพัดลม (255 สำหรับพัดลมนี้คือหยุดหมุน)
          fan_status = false;                     // สถานะพัดลมหยุด
        }
      } else {
        // ถ้าไม่มีข้อมูลอุณหภูมิ (ยังไม่อ่านได้)
        analogWrite(MotorPin2, 0);  // ตั้งพัดลมให้ทำงาน
        fan_status = true;          // สถานะพัดลมทำงาน
      }
      break;

    // กรณี fan_mode = 1: โหมดปิดพัดลม
    case 1:
      analogWrite(MotorPin2, 255);  // ปิดพัดลม
      fan_status = false;           // สถานะพัดลมหยุด
      break;

    // กรณี fan_mode = 2: โหมดเปิดพัดลมตลอดเวลา
    case 2:
      analogWrite(MotorPin2, 0);  // ตั้งพัดลมให้ทำงาน
      fan_status = true;          // สถานะพัดลมทำงาน
      break;
  }
}

//ฟังก์ชันทำหน้าที่อ่านข้อมูลจากหน่วยความจำ RAM ของ RTC DS1307
void setupDataFromDS1307RAM() {
  // อ่านค่าเวลา (ชั่วโมง) จาก RAM ของ DS1307 และเก็บในตัวแปร Hour
  Hour = readFromDS1307RAM(HOUR_ADDRESS);
  // อ่านค่านาทีจาก RAM ของ DS1307 และเก็บในตัวแปร Minute
  Minute = readFromDS1307RAM(MINUTE_ADDRESS);
  // ตรวจสอบว่าค่าชั่วโมงที่อ่านมาเกินช่วง 0-23 หรือไม่ ถ้าเกินให้ตั้งค่าเป็น 0
  if (Hour >= 24 || Hour < 0)
    Hour = 0;
  // ตรวจสอบว่าค่านาทีที่อ่านมาเกินช่วง 0-59 หรือไม่ ถ้าเกินให้ตั้งค่าเป็น 0
  if (Minute >= 60 || Minute < 0)
    Minute = 0;

  // อ่านสถานะการเตือนช่วงเช้า (Morning) จาก RAM ของ DS1307
  morning = readFromDS1307RAM(MORNING_ADDRESS);
  // อ่านสถานะการเตือนช่วงเที่ยง (Noon) จาก RAM ของ DS1307
  noon = readFromDS1307RAM(NOON_ADDRESS);
  // อ่านสถานะการเตือนช่วงเย็น (Evening) จาก RAM ของ DS1307
  evening = readFromDS1307RAM(EVENING_ADDRESS);
  // อ่านสถานะการเตือนก่อนเข้านอน (Before Bed) จาก RAM ของ DS1307
  beforebed = readFromDS1307RAM(BEFORE_BED_ADDRESS);
  // อ่านสถานะการเปิด-ปิดการเตือน (Alarm) จาก RAM ของ DS1307
  alarm = readFromDS1307RAM(ALARM_ADDRESS);
  // อ่านโหมดการทำงานของพัดลมจาก RAM ของ DS1307
  fan_mode = readFromDS1307RAM(FAN_MODE_ADDRESS);

  // ถ้าค่า fan_mode ที่อ่านมาไม่ถูกต้อง (นอกช่วง 0-2) ให้ตั้งค่าเป็น 0 (โหมดอัตโนมัติ)
  if (fan_mode >= 3 || fan_mode < 0) {
    fan_mode = 0;
    // เขียนค่า fan_mode กลับไปที่ RAM ของ DS1307
    writeToDS1307RAM(FAN_MODE_ADDRESS, fan_mode);
  }
}

void setup() {
  sei();
  pinMode(2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(2), pressButton, FALLING);
  Wire.begin();
  keypad.begin();
  Serial.begin(9600);
  while (!Serial) {}
  lcd.init();
  lcd.home();
  setupWatchdog();
  setupDataFromDS1307RAM();
  pinMode(MotorPin2, OUTPUT);

  for (int i = 0; i < 7; i++) {
    lcd.createChar(i, customChars[i]);
  }
}

void loop() {
  // ตรวจสอบว่ามีข้อมูลเข้ามาทาง Serial หรือไม่
  if (Serial.available()) {
    // อ่านข้อมูลจาก Serial จนถึง newline และส่งไปประมวลผลในฟังก์ชัน processData()
    String data = Serial.readStringUntil('\n');
    processData(data);
  }

  // ตรวจสอบว่ามีการกดปุ่มบน keypad หรือไม่
  char key = keypad.getKey();
  if (key != NO_KEY) {
    // รีเซ็ต Watchdog Timer เมื่อมีการกดปุ่ม
    wdt_reset();
    
    // ตรวจสอบว่าปุ่ม 'A' ถูกกดหรือไม่
    if (key == 'A') {
      // เพิ่มค่า select และหมุนค่ากลับไปที่ 1 หากเกิน 7
      select++;
      if (select > 7)
        select = 1;
      lcd.clear();
    } 
    // ตรวจสอบว่าปุ่ม 'C' ถูกกดหรือไม่
    else if (key == 'C') {
      // ล้างหน้าจอและเปลี่ยนโหมดเป็น detection พร้อมส่งข้อมูล "MODE:1" ไปยัง Serial
      lcd.clear();
      deviceMode = detection;
      Serial.println("MODE:1");
    } 
    // ตรวจสอบว่าปุ่ม 'D' ถูกกดหรือไม่
    else if (key == 'D') {
      // ล้างหน้าจอและเปลี่ยนโหมดเป็น readText พร้อมส่งข้อมูล "MODE:2" ไปยัง Serial
      lcd.clear();
      Serial.println("MODE:2");
      deviceMode = readText;
    }
  }

  // ตรวจสอบโหมดการทำงานของอุปกรณ์
  switch (deviceMode) {
    // หากอยู่ในโหมด detection
    case (detection):
      // ตรวจสอบการเลือกฟังก์ชันการทำงาน
      switch (select) {
        // กรณีเลือก 1: อ่านข้อมูลเวลาจาก RTC
        case 1:
          if (RTC.read(tm)) {
            // ตรวจสอบเวลาปัจจุบันและเรียกใช้งาน alarmAlert() เมื่อถึงเวลา
            if (tm.Hour == Hour && tm.Minute == Minute && alarm) {
              alarmAlert(Hour, Minute);
            } else if (tm.Hour == MORNING_TIME && tm.Minute == 0 && morning) {
              alarmAlert(Hour, Minute);
            } else if (tm.Hour == NOON_TIME && tm.Minute == 0 && noon) {
              alarmAlert(Hour, Minute);
            } else if (tm.Hour == EVENING_TIME && tm.Minute == 0 && evening) {
              alarmAlert(Hour, Minute);
            } else if (tm.Hour == BEFOREBED_TIME && tm.Minute == 0 && beforebed) {
              alarmAlert(Hour, Minute);
            } else {
              // หากไม่มีการแจ้งเตือน แสดงวันที่และเวลา พร้อมกับแสดงสถานะ Wi-Fi และพัดลม
              if (flag) {
                Serial.println("STOP_ALARM");
              }
              flag = false;
              lcd.setCursor(0, 0);
              lcd.print("Date: ");
              print2digits(tm.Day);
              lcd.print("/");
              print2digits(tm.Month);
              lcd.print("/");
              lcd.print(tmYearToCalendar(tm.Year));
              lcd.setCursor(0, 1);
              lcd.print("Time: ");
              print2digits(tm.Hour);
              lcd.print(":");
              print2digits(tm.Minute);
              lcd.print(" ");
              if (wifi_status) {
                lcd.write(byte(0));
                lcd.write(byte(1));
                lcd.write(byte(2));
              } else {
                Blink();
                if (blink_tx)
                  lcd.print("   ");
                else {
                  lcd.write(byte(0));
                  lcd.write(byte(1));
                  lcd.write(byte(2));
                }
              }
              // แสดงสถานะพัดลม
              if (fan_status) {
                if (millis() - refresh_time > 100) {
                  if (frame == 0) {
                    lcd.write(byte(3));
                    frame = (frame + 1) % 4;
                  } else if (frame == 1) {
                    lcd.write(byte(4));
                    frame = (frame + 1) % 4;
                  } else if (frame == 2) {
                    lcd.write(byte(5));
                    frame = (frame + 1) % 4;
                  } else if (frame == 3) {
                    lcd.write(byte(6));
                    frame = (frame + 1) % 4;
                  }
                  refresh_time = millis();
                }
              } else {
                lcd.write(byte(6));
              }
              alarmStopped = false;
            }
          } else {
            // หากไม่สามารถอ่านเวลาได้ จะแสดงข้อความข้อผิดพลาดบน LCD
            lcd.setCursor(0, 0);
            lcd.print("Can't Read Time");
          }
          // รีเซ็ต Watchdog Timer
          wdt_reset();
          break;
        
        // กรณีเลือก 2: ตั้งค่าและแสดงสถานะของ Alarm Morning
        case 2:
          lcd.setCursor(0, 0);
          lcd.print("Alarm Morning");
          lcd.setCursor(0, 1);
          if (key == 'B') {
            morning = !morning;
            writeToDS1307RAM(MORNING_ADDRESS, morning);
            lcd.clear();
            lcd.setCursor(0, 1);
          }
          if (morning) {
            lcd.print("Status: ON");
          } else {
            lcd.print("Status: OFF");
          }
          break;
        
        // กรณีเลือก 3: ตั้งค่าและแสดงสถานะของ Alarm Noon
        case 3:
          lcd.setCursor(0, 0);
          lcd.print("Alarm Noon");
          lcd.setCursor(0, 1);
          if (key == 'B') {
            noon = !noon;
            writeToDS1307RAM(NOON_ADDRESS, noon);
            lcd.clear();
            lcd.setCursor(0, 1);
          }
          if (noon) {
            lcd.print("Status: ON");
          } else {
            lcd.print("Status: OFF");
          }
          break;

        // กรณีเลือก 4: ตั้งค่าและแสดงสถานะของ Alarm Evening
        case 4:
          lcd.setCursor(0, 0);
          lcd.print("Alarm Evening");
          lcd.setCursor(0, 1);
          if (key == 'B') {
            evening = !evening;
            writeToDS1307RAM(EVENING_ADDRESS, evening);
            lcd.clear();
            lcd.setCursor(0, 1);
          }
          if (evening) {
            lcd.print("Status: ON");
          } else {
            lcd.print("Status: OFF");
          }
          break;

        // กรณีเลือก 5: ตั้งค่าและแสดงสถานะของ Alarm BeforeBed
        case 5:
          lcd.setCursor(0, 0);
          lcd.print("Alarm BeforeBed");
          lcd.setCursor(0, 1);
          if (key == 'B') {
            beforebed = !beforebed;
            writeToDS1307RAM(BEFORE_BED_ADDRESS, beforebed);
            lcd.clear();
            lcd.setCursor(0, 1);
          }
          if (beforebed) {
            lcd.print("Status: ON");
          } else {
            lcd.print("Status: OFF");
          }
          break;

        // กรณีเลือก 6: การตั้งค่าเวลาของการแจ้งเตือน
        case 6:
          lcd.setCursor(0, 0);
          lcd.print("Set Alarm");
          if (key == '*') {
            edit = true;
          } else if (key == '#') {
            writeToDS1307RAM(HOUR_ADDRESS, Hour);
            writeToDS1307RAM(MINUTE_ADDRESS, Minute);
            edit = false;
          } else if (key == 'B') {
            alarm = !alarm;
            writeToDS1307RAM(ALARM_ADDRESS, alarm);
          }
          // แก้ไขเวลาการแจ้งเตือน
          if (edit) {
            if (key != NO_KEY && key >= '0' && key <= '9') {
              int num = key - '0';
              if (position == 0) {
                if (num <= 2) {
                  Hour = num * 10;
                  position++;
                }
              } else if (position == 1) {
                if (Hour < 20) {
                  Hour += num;
                  position++;
                } else {
                  if (num <= 3) {
                    Hour += num;
                    position++;
                  }
                }
              } else if (position == 2) {
                if (num <= 5) {
                  Minute = num * 10;
                  position++;
                }
              } else if (position == 3) {
                Minute += num;
                position = 0;
              }
            }
          }
          lcd.setCursor(0, 1);
          lcd.print("Time: ");
          if (edit) {
            Blink();
            if (position == 0) {
              if (blink_tx)
                lcd.print(" ");
              else
                lcd.print(Hour / 10);
              lcd.print(Hour % 10);
              lcd.print(":");
              print2digits(Minute);
            } else if (position == 1) {
              lcd.print(Hour / 10);
              if (blink_tx)
                lcd.print(" ");
              else
                lcd.print(Hour % 10);
              lcd.print(":");
              print2digits(Minute);
            } else if (position == 2) {
              print2digits(Hour);
              lcd.print(":");
              if (blink_tx)
                lcd.print(" ");
              else
                lcd.print(Minute / 10);
              lcd.print(Minute % 10);
            } else if (position == 3) {
              print2digits(Hour);
              lcd.print(":");
              lcd.print(Minute / 10);
              if (blink_tx)
                lcd.print(" ");
              else
                lcd.print(Minute % 10);
            }
          } else {
            print2digits(Hour);
            lcd.print(":");
            print2digits(Minute);
          }
          if (alarm) {
            lcd.print("   ON");
          } else {
            lcd.print("  OFF");
          }
          break;

        // กรณีเลือก 7: การตั้งค่าโหมดพัดลม
        case 7:
          if (key == 'B') {
            fan_mode = (fan_mode + 1) % 3;
            writeToDS1307RAM(FAN_MODE_ADDRESS, fan_mode);
            lcd.clear();
          }
          lcd.setCursor(0, 0);
          lcd.print("Set Fan");
          lcd.print("   ");
          lcd.print(Temperature);
          lcd.print((char)223);
          lcd.print("C");
          lcd.print(" --.-");
          lcd.print((char)223);
          lcd.print("C");
          lcd.setCursor(0, 1);
          lcd.print("Status: ");
          if (fan_mode == 0) {
            lcd.print("AUTO");
          } else if (fan_mode == 1) {
            lcd.print("OFF");
          } else if (fan_mode == 2) {
            lcd.print("ON");
          }
          break;
      }
      break;

    // หากอยู่ในโหมด readText จะแสดงข้อความบนหน้าจอ
    case (readText):
      wdt_reset();
      lcd.setCursor(0, 0);
      lcd.print("   Read  Text   ");
      lcd.setCursor(0, 1);
      lcd.print("  Press switch  ");
      break;
  }

  // เรียกใช้งานฟังก์ชันควบคุมการทำงานของพัดลม
  setFanSpeed();
}
