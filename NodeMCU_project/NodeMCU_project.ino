#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <WiFiClient.h>
#include <TridentTD_LineNotify.h>
#include <WiFiManager.h>

#define BLYNK_TEMPLATE_ID "TMPL6bD5_WOag"
#define BLYNK_TEMPLATE_NAME "Eldercare Project"
#define BLYNK_DEVICE_NAME "Select Mode"
#define BLYNK_AUTH_TOKEN "muRV7J4h8fzmf_f2sBIMs4lcFnWfZKOs"

// Token สำหรับ LINE Notify
String LINE_TOKEN = "qFcOGWcjUWRbxjMnCqTuakORly91d5i2dGU6mAZnt7H";
unsigned long lastNotificationTime = 0, lastSend = 0, lastTime = 0;
const unsigned long notificationInterval = 60000;

//ThingSpeak
const char* server = "api.thingspeak.com";
const char* api_key = "E0EJPXHG4KLTO3TX";
WiFiClient client;

enum Mode {
  detection,
  readText
};

Mode deviceMode = detection;

// ฟังก์ชันที่ทำงานเมื่อกดปุ่ม Virtual Pin V0 (สำหรับโหมด 1)
BLYNK_WRITE(V0) {
  int pinValue = param.asInt();  // รับค่าจากปุ่ม
  if (pinValue == 1) {
    deviceMode = detection;
    Serial.println("MODE:1");
    LINE.notify("โหมดตรวจจับการล้ม");
  }
}

// ฟังก์ชันที่ทำงานเมื่อกดปุ่ม Virtual Pin V2 (สำหรับโหมด 2)
BLYNK_WRITE(V1) {
  int pinValue = param.asInt();  // รับค่าจากปุ่ม
  if (pinValue == 1) {
    deviceMode = readText;
    Serial.println("MODE:2");
    LINE.notify("โหมดอ่านฉลากยา");
  }
}

void sendDataToThingSpeak(String data) {
  if (client.connect(server, 80)) {
    String url = "/update?api_key=" + String(api_key);

    // ค้นหาตำแหน่งของข้อมูล CPU, RAM และ TEMP
    int cpu_index = data.indexOf("CPU:") + 4;    
    int ram_index = data.indexOf("RAM:") + 4;    
    int temp_index = data.indexOf("TEMP:") + 5;  

    // ดึงข้อมูลจากข้อความที่ได้รับ
    String cpu_usage = data.substring(cpu_index, data.indexOf(",", cpu_index));  
    String ram_usage = data.substring(ram_index, data.indexOf(",", ram_index));  
    String cpu_temp = data.substring(temp_index);                                

    // สร้าง URL ที่ส่งข้อมูลไปยัง ThingSpeak
    url += "&field1=" + cpu_usage + "&field2=" + ram_usage + "&field3=" + cpu_temp;


    // ส่งคำสั่ง HTTP GET request ไปยัง ThingSpeak
    client.print(String("GET ") + url + " HTTP/1.1\r\n" + "Host: " + server + "\r\n" + "Connection: close\r\n\r\n");

    client.stop();  // ปิดการเชื่อมต่อหลังส่งข้อมูล
  }
}

void processData(String data) {
  // กำหนดตัวแปร currentTime ให้เก็บค่าเวลาในหน่วยมิลลิวินาทีปัจจุบัน
  unsigned long currentTime = millis();
  
  // ตรวจสอบว่าข้อมูลที่ได้รับขึ้นต้นด้วย "NOTIFICATION:" หรือไม่
  if (data.startsWith("NOTIFICATION:")) {
    // ดึงข้อมูลที่อยู่หลัง "NOTIFICATION:" โดยเริ่มต้นจากตำแหน่งที่ 13 ของสตริง
    String action = data.substring(13);

    // ตรวจสอบค่าของ action และส่งการแจ้งเตือนผ่าน LINE ตามเงื่อนไข
    if (action == "Morning") {
      LINE.notify("แจ้งเตือนการรับประทานยาตอนเช้า");
    } else if (action == "Noon") {
      LINE.notify("แจ้งเตือนการรับประทานยาตอนเที่ยง");
    } else if (action == "Evening") {
      LINE.notify("แจ้งเตือนการรับประทานยาตอนเย็น");
    } else if (action == "Beforebed") {
      LINE.notify("แจ้งเตือนการรับประทานยาก่อนนอน");
    } else {
      // หาก action ไม่ตรงกับเงื่อนไขข้างต้น ให้ส่งการแจ้งเตือนพร้อมกับข้อมูล action นั้น
      LINE.notify("แจ้งเตือนการรับประทานยาเวลา " + action);
    }
  
  // ตรวจสอบว่าข้อมูลที่ได้รับเป็น "MODE:1" หรือไม่
  } else if (data == "MODE:1") {
    // ตั้งค่าโหมดเป็น detection (โหมดตรวจจับการล้ม) และส่งการแจ้งเตือนผ่าน LINE
    deviceMode = detection;
    LINE.notify("โหมดตรวจจับการล้ม");

  // ตรวจสอบว่าข้อมูลที่ได้รับเป็น "MODE:2" หรือไม่
  } else if (data == "MODE:2") {
    // ตั้งค่าโหมดเป็น readText (โหมดอ่านฉลากยา) และส่งการแจ้งเตือนผ่าน LINE
    deviceMode = readText;
    LINE.notify("โหมดอ่านฉลากยา");

  // ตรวจสอบว่าข้อมูลที่ได้รับขึ้นต้นด้วย "CPU:" หรือไม่
  } else if (data.startsWith("CPU:")) {
    // หากเวลาที่ผ่านไปมากกว่า 20 วินาทีตั้งแต่ครั้งล่าสุดที่ส่งข้อมูลไปยัง ThingSpeak
    if (millis() - lastSend > 20000) {
      // ส่งข้อมูลไปยัง ThingSpeak
      sendDataToThingSpeak(data);

      // บันทึกเวลาปัจจุบันเป็นค่าของ lastSend เพื่อใช้สำหรับการตรวจสอบครั้งต่อไป
      lastSend = millis();
    }
  }
}


void setup() {

  // เริ่มต้นการสื่อสาร Serial
  Serial.begin(9600);

  // สร้างตัวจัดการ Wi-Fi
  WiFiManager wifiManager;
  // ตั้งค่าให้สร้าง Access Point ชื่อ "ElderCare System"
  wifiManager.autoConnect("ElderCare System");

  // หลังจากเชื่อมต่อ WiFi แล้ว เริ่มการทำงานของ Blynk โดยไม่ระบุ SSID และ Password
  Blynk.config(BLYNK_AUTH_TOKEN);
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.connect();  
  }

  // กำหนด Line Token
  LINE.setToken(LINE_TOKEN);
  // ส่งข้อความแจ้งเตือนผ่าน LINE เมื่อเชื่อมต่อสำเร็จ
  LINE.notify("Device Connected to WiFi and Blynk");
}

void loop() {
  Blynk.run();
  //ตรวจสอบว่ามีข้อมูลที่รับเข้ามาทาง Serial
  if (Serial.available()) {
    //อ่านข้อมูลที่รับเข้ามา
    String data = Serial.readStringUntil('\n');
    //ส่งข้อมูลไปให้ฟังก์ชัน processData 
    processData(data);
  }

  //ส่งสถานะการเชื่อมต่ออินเตอร์เน็ตทุก 1 นาที
  if (millis() - lastTime > 60000) {
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WIFI");
    } else {
      Serial.println("LOST");
    }
    lastTime = millis();
  }
}