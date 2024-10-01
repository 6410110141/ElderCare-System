import cv2  # ใช้สำหรับการจัดการกล้องและภาพ
import mediapipe as mp  # ใช้สำหรับการประมวลผลภาพด้วย MediaPipe
import threading  # ใช้สำหรับสร้างเธรด
import time  # ใช้สำหรับการจัดการเวลา
from PIL import Image  # ใช้สำหรับจัดการภาพ
import pytesseract  # ใช้สำหรับการทำ OCR
import speech_recognition as sr  # ใช้สำหรับการรู้จำเสียงพูด
import serial  # ใช้สำหรับการสื่อสารผ่าน Serial

# สำหรับ TTS
from gtts import gTTS  # ใช้สำหรับการแปลงข้อความเป็นเสียงพูด
import os  # ใช้สำหรับการเข้าถึงฟังก์ชันระบบปฏิบัติการ

# โมดูลสำหรับการตรวจสอบประสิทธิภาพการทำงาน
import psutil  
import subprocess  # ใช้สำหรับรันคำสั่งในระบบปฏิบัติการ

# สำหรับการแจ้งเตือนใน LINE
import numpy as np  # ใช้สำหรับการจัดการอาร์เรย์
import requests, urllib.parse  # ใช้สำหรับการส่ง HTTP requests
import io  # ใช้สำหรับการจัดการ I/O

# Serial communication สำหรับ Arduino และ NodeMCU
arduino_serial = serial.Serial('/dev/ttyACM0', 9600, timeout = 1)
time.sleep(2)  # รอการเชื่อมต่อ

nodemcu_serial = serial.Serial('/dev/ttyUSB0', 9600, timeout = 1)
time.sleep(2)  # รอการเชื่อมต่อ

# ตั้งค่า Mediapipe สำหรับการตรวจจับท่าทาง
mp_pose = mp.solutions.pose
pose = mp_pose.Pose()
mp_drawing = mp.solutions.drawing_utils

# เปิดกล้อง
cap = cv2.VideoCapture(0)  # ใช้กล้องตัวที่ 0
cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1080)  # ตั้งค่าความกว้างของเฟรม
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)  # ตั้งค่าความสูงของเฟรม

# ข้อมูลที่ใช้ร่วมกันระหว่างเธรด
frame_lock = threading.Lock()  # ล็อกสำหรับการเข้าถึงเฟรม
frame = None  # ตัวแปรสำหรับเก็บเฟรม
stop_threads = False  # ตัวแปรสำหรับควบคุมการหยุดเธรด
mode = 1  # โหมดเริ่มต้น (1 = ตรวจจับการล้ม, 2 = OCR)
OCR = False  # ตัวแปรสำหรับการเปิด/ปิด OCR
text_received = "default text"  # ข้อความที่ได้รับ
notification = False  # ตัวแปรสำหรับการแจ้งเตือน

# ล็อกสำหรับความปลอดภัยของเธรดเมื่อเปลี่ยนโหมด
mode_lock = threading.Lock()

def sent_line_notify(massage, image_path):
    token = "qFcOGWcjUWRbxjMnCqTuakORly91d5i2dGU6mAZnt7H"  # Token สำหรับ LINE Notify
    url = 'https://notify-api.line.me/api/notify'  # URL สำหรับ LINE Notify API
    HEADERS = {'Authorization': 'Bearer ' + token}  # Header สำหรับการยืนยันตัวตน

    # ข้อความ
    msg = massage
    # ภาพ
    img = Image.open(image_path)  # เปิดภาพ
    img.load()  # โหลดภาพ
    myimg = np.array(img)  # แปลงภาพเป็น NumPy array
    f = io.BytesIO()  # สร้าง Buffer
    Image.fromarray(myimg).save(f, 'png')  # บันทึกภาพเป็น PNG ใน Buffer
    data = f.getvalue()  # รับค่าจาก Buffer

    # ส่งคำขอ
    response = requests.post(url, headers=HEADERS, params={"message": msg}, files={"imageFile": data})
    print(response)  # แสดงผลการตอบกลับ

def speak_thai_google(text):
    # สร้างเสียงพูดภาษาไทยในรูปแบบ MP3 แบบโมโน
    tts = gTTS(text=text, lang='th')
    tts.save("thai_speech_mono.mp3")  # บันทึกไฟล์เสียงโมโน

    # แปลงไฟล์ MP3 โมโนเป็นสเตอริโอโดยใช้ ffmpeg
    os.system("ffmpeg -i thai_speech_mono.mp3 -ac 2 thai_speech_stereo.mp3 -y")

    # เล่นไฟล์เสียงสเตอริโอโดยใช้ mpg321 หรือเครื่องเล่นอื่น
    os.system("mpg321 thai_speech_stereo.mp3")

# เรียกใช้ฟังก์ชันเพื่อเล่นเสียงต้อนรับ
speak_thai_google("สวัสดีค่ะนายท่านยินดีต้อนรับสู่ Elder Care System ตอนนี้กำลังเตรียมความพร้อมระบบ กรุณารอสักครู่")

def capture_frames():
    global frame, stop_threads  # กำหนดตัวแปร global ที่ใช้ในฟังก์ชัน
    while not stop_threads:  # ทำงานในขณะที่ stop_threads เป็น False
        ret, current_frame = cap.read()  # อ่านภาพจากกล้อง
        if ret:  # ถ้าอ่านภาพได้สำเร็จ
            with frame_lock:  # ใช้ lock เพื่อป้องกันการเข้าถึงพร้อมกัน
                frame = current_frame  # เก็บภาพปัจจุบันในตัวแปร global frame
        time.sleep(0.01)  # หยุดพัก 0.01 วินาทีเพื่อไม่ให้ใช้ CPU มากเกินไป

def process_frames():
    global frame, stop_threads, mode, OCR  # กำหนดตัวแปร global ที่ใช้ในฟังก์ชัน
    last_time = 0  # ตัวแปรสำหรับเก็บเวลาในการตรวจจับการล้ม

    def speak_thai_google(text):
        # สร้างเสียงพูดภาษาไทยในรูปแบบ MP3 mono
        tts = gTTS(text=text, lang='th')  # ใช้ gTTS เพื่อสร้างเสียงพูด
        tts.save("thai_speech_mono.mp3")  # บันทึกเสียงพูดเป็นไฟล์
        # แปลงไฟล์ MP3 mono เป็น stereo โดยใช้ ffmpeg
        os.system("ffmpeg -i thai_speech_mono.mp3 -ac 2 thai_speech_stereo.mp3 -y")
        # เล่นไฟล์เสียง stereo โดยใช้ mpg321 หรือโปรแกรมอื่น ๆ
        os.system("mpg321 thai_speech_stereo.mp3")

    while not stop_threads:  # ทำงานในขณะที่ stop_threads เป็น False
        start_time = time.time()  # บันทึกเวลาเริ่มต้น

        if frame is None:  # ถ้าไม่มีภาพ
            continue  # ข้ามไปยังรอบถัดไป

        with frame_lock:  # ใช้ lock เพื่อป้องกันการเข้าถึงพร้อมกัน
            frame_copy = frame.copy()  # สร้างสำเนาของภาพปัจจุบัน

        # Fall detection (ตรวจจับการล้ม)
        if mode == 1:  # ถ้าตั้งค่าโหมดเป็น 1 (ตรวจจับการล้ม)
            # แปลงภาพเป็น RGB เพื่อใช้กับ mediapipe
            image = cv2.cvtColor(frame_copy, cv2.COLOR_BGR2RGB)
            results = pose.process(image)  # ประมวลผลภาพเพื่อหาลักษณะของร่างกาย

            # แปลงกลับเป็น BGR เพื่อแสดงภาพ
            image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)

            if results.pose_landmarks:  # ถ้ามีการตรวจพบ landmarks ของร่างกาย
                mp_drawing.draw_landmarks(image, results.pose_landmarks, mp_pose.POSE_CONNECTIONS)  # วาด landmarks บนภาพ

                landmarks = results.pose_landmarks.landmark  # ดึงข้อมูล landmarks
                landmark_coords = [(int(landmark.x * frame_copy.shape[1]), int(landmark.y * frame_copy.shape[0])) for landmark in landmarks]

                x_values = [coord[0] for coord in landmark_coords]  # ค่า x ของ landmarks
                y_values = [coord[1] for coord in landmark_coords]  # ค่า y ของ landmarks

                x_min, x_max = min(x_values), max(x_values)  # คำนวณค่าต่ำสุดและสูงสุดของ x
                y_min, y_max = min(y_values), max(y_values)  # คำนวณค่าต่ำสุดและสูงสุดของ y

                label = "Person"  # ตั้งชื่อให้กับบุคคล
                color = (0, 255, 0)  # สีเขียว

                # ดึงค่าตำแหน่ง y ของบาง landmarks
                nose_y = landmarks[mp_pose.PoseLandmark.NOSE].y
                left_hip_y = landmarks[mp_pose.PoseLandmark.LEFT_HIP].y
                right_hip_y = landmarks[mp_pose.PoseLandmark.RIGHT_HIP].y
                left_ankle_y = landmarks[mp_pose.PoseLandmark.LEFT_ANKLE].y
                right_ankle_y = landmarks[mp_pose.PoseLandmark.RIGHT_ANKLE].y

                # ตรวจสอบการล้ม
                if (left_ankle_y - nose_y) < 0.2 and (right_ankle_y - nose_y) < 0.2:
                    label = "Fall Detected"  # เปลี่ยนชื่อเป็น "การล้มตรวจพบ"
                    color = (0, 0, 255)  # สีแดงสำหรับการล้ม
                    if time.time() - last_time > 60:  # ถ้าผ่านไปมากกว่า 60 วินาที
                        print("Detected a fall!")  # พิมพ์ข้อความตรวจพบการล้ม
                        cv2.imwrite('Fall_image.jpg', image)  # บันทึกภาพเมื่อเกิดการล้ม
                        sent_line_notify("เเจ้งเตือนการหกล้ม", "Fall_image.jpg")  # ส่งการแจ้งเตือนไปยัง LINE Notify
                        last_time = time.time()  # อัปเดตเวลา

                # แสดงชื่อและกรอบรอบบุคคล
                cv2.putText(image, label, (x_min, y_min - 10), cv2.FONT_HERSHEY_SIMPLEX, 1, color, 2, cv2.LINE_AA)
                cv2.rectangle(image, (x_min, y_min), (x_max, y_max), color, 2)

            frame_time = time.time() - start_time  # คำนวณเวลาในการประมวลผลภาพ
            time_text = f"Frame time: {frame_time:2f} sec"  # สร้างข้อความแสดงเวลา
            cv2.putText(image, time_text, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (56, 61, 150), 2, cv2.LINE_AA)  # แสดงเวลาในภาพ
            # แสดงผลภาพที่ได้
            cv2.imshow('Pose Detection with Bounding Box', image)

        if cv2.waitKey(10) & 0xFF == ord('q'):  # ถ้ากดปุ่ม 'q'
            stop_threads = True  # เปลี่ยนค่า stop_threads เป็น True
            break  # ออกจากลูป

        # OCR (การรู้จำตัวอักษร)
        if mode == 2:  # ถ้าตั้งค่าโหมดเป็น 2 (การรู้จำตัวอักษร)
            image = cv2.cvtColor(frame_copy, cv2.COLOR_BGR2RGB)  # แปลงภาพเป็น RGB
            image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)  # แปลงกลับเป็น BGR

            if cv2.waitKey(10) & 0xFF == ord('q'):  # ถ้ากดปุ่ม 'q'
                stop_threads = True  # เปลี่ยนค่า stop_threads เป็น True
                break
            
            if OCR == True:  # ถ้า OCR ถูกเปิดใช้งาน
                print("Processing . . .")  # พิมพ์ข้อความกำลังประมวลผล
                cv2.imwrite('captured_image.jpg', image)  # บันทึกภาพที่จับได้

                # โหลดภาพด้วย OpenCV
                image = cv2.imread('captured_image.jpg')

                # แปลงเป็นภาพขาวดำ
                gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

                # ใช้ thresholding เพื่อปรับปรุงการมองเห็นของข้อความ
                thresh = cv2.threshold(gray, 150, 255, cv2.THRESH_BINARY)[1]

                # แปลงกลับเป็น PIL Image สำหรับ pytesseract
                processed_image = Image.fromarray(thresh)
                                                                                                                                                                                                                        
                # ทำการ OCR โดยระบุการรู้จำภาษาไทยและอังกฤษ
                text = pytesseract.image_to_string(image, lang='tha+eng')  # รู้จำตัวอักษรในภาพ

                print(text)  # พิมพ์ข้อความที่รู้จำได้
                if text.strip():  # เรียก TTS ถ้าข้อความไม่ว่าง
                    speak_thai_google(text)  # แปลงข้อความเป็นเสียงพูด
                else:
                    print("Error: No text provided for TTS.")  # แจ้งเตือนถ้าไม่มีข้อความ

                OCR = False  # ตั้งค่า OCR เป็น False เพื่อหยุดการประมวลผล OCR

            frame_time = time.time() - start_time  # คำนวณเวลาในการประมวลผลภาพ
            time_text = f"Frame time: {frame_time:2f} sec"  # สร้างข้อความแสดงเวลา
            cv2.putText(image, time_text, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 1, (56, 61, 150), 2, cv2.LINE_AA)  # แสดงเวลาในภาพ
            # แสดงผลภาพที่ได้
            cv2.imshow('Pose Detection with Bounding Box', image)

# Thread for changing the mode based on terminal input 
def change_mode_serial():
    global mode, stop_threads, OCR, notification  # กำหนดตัวแปร global ที่ใช้ในฟังก์ชัน
    while not stop_threads:  # ทำงานในขณะที่ stop_threads เป็น False
        if arduino_serial.in_waiting > 0:  # เช็คว่ามีข้อมูลจาก Arduino หรือไม่
            arduino_data = arduino_serial.readline().decode('utf-8').strip()  # อ่านข้อมูลจาก Arduino
            #print(f"Arduino send: {arduino_data}\n")  # พิมพ์ข้อมูลที่อ่านได้ (สามารถเปิดใช้เพื่อดีบัก)
            
            # ตรวจสอบข้อมูลที่ได้รับจาก Arduino
            if("NOTIFICATION:" in arduino_data):  # ถ้ามีข้อความ "NOTIFICATION:"
                print("Send NOTIFICATION")  # พิมพ์ข้อความแจ้งเตือน
                notification = True  # ตั้งค่า notification เป็น True
                nodemcu_serial.write((arduino_data + "\n").encode('utf-8'))  # ส่งข้อมูลไปยัง NodeMCU
            if(arduino_data == "STOP_ALARM"):  # ถ้าข้อมูลเป็น "STOP_ALARM"
                notification = False  # ตั้งค่า notification เป็น False
            if(arduino_data == "MODE:1"):  # ถ้าข้อมูลเป็น "MODE:1"
                mode = 1  # เปลี่ยนโหมดเป็น 1 (ตรวจจับการล้ม)
                nodemcu_serial.write((arduino_data + "\n").encode('utf-8'))  # ส่งข้อมูลไปยัง NodeMCU
                print("Mode changed Fall detection.")  # พิมพ์ข้อความเปลี่ยนโหมด
            if(arduino_data == "MODE:2"):  # ถ้าข้อมูลเป็น "MODE:2"
                mode = 2  # เปลี่ยนโหมดเป็น 2 (OCR)
                nodemcu_serial.write((arduino_data + "\n").encode('utf-8'))  # ส่งข้อมูลไปยัง NodeMCU
                print("Mode changed to OCR.")  # พิมพ์ข้อความเปลี่ยนโหมด
            if(arduino_data == "Capture"):  # ถ้าข้อมูลเป็น "Capture"
                OCR = True  # ตั้งค่า OCR เป็น True
                print("Entered picture from webcam.")  # พิมพ์ข้อความระบุการจับภาพจากเว็บแคม
            if(arduino_data == "Stop"):  # ถ้าข้อมูลเป็น "Stop"
                stop_threads = True  # เปลี่ยนค่า stop_threads เป็น True
                print("Exiting...")  # พิมพ์ข้อความออกจากลูป
                break  # ออกจากลูป

        if nodemcu_serial.in_waiting > 0:  # เช็คว่ามีข้อมูลจาก NodeMCU หรือไม่
            nodemcu_data = nodemcu_serial.readline().decode('utf-8').strip()  # อ่านข้อมูลจาก NodeMCU
            #print(f"NodeMCU send: {nodemcu_data}\n")  # พิมพ์ข้อมูลที่อ่านได้ (สามารถเปิดใช้เพื่อดีบัก)
            
            # ตรวจสอบข้อมูลที่ได้รับจาก NodeMCU
            if(nodemcu_data == "WIFI"):  # ถ้าข้อมูลเป็น "WIFI"
                arduino_serial.write((nodemcu_data + "\n").encode('utf-8'))  # ส่งข้อมูลไปยัง Arduino
            if(nodemcu_data == "LOST"):  # ถ้าข้อมูลเป็น "LOST"
                arduino_serial.write((nodemcu_data + "\n").encode('utf-8'))  # ส่งข้อมูลไปยัง Arduino
            if(nodemcu_data == "MODE:1"):  # ถ้าข้อมูลเป็น "MODE:1"
                mode = 1  # เปลี่ยนโหมดเป็น 1 (ตรวจจับการล้ม)
                arduino_serial.write((nodemcu_data + "\n").encode('utf-8'))  # ส่งข้อมูลไปยัง Arduino
                print("Mode changed Fall detection.")  # พิมพ์ข้อความเปลี่ยนโหมด
            if(nodemcu_data == "MODE:2"):  # ถ้าข้อมูลเป็น "MODE:2"
                mode = 2  # เปลี่ยนโหมดเป็น 2 (OCR)
                arduino_serial.write((nodemcu_data + "\n").encode('utf-8'))  # ส่งข้อมูลไปยัง Arduino
                print("Mode changed to OCR.")  # พิมพ์ข้อความเปลี่ยนโหมด
            if(nodemcu_data == "Capture"):  # ถ้าข้อมูลเป็น "Capture"
                OCR = True  # ตั้งค่า OCR เป็น True
                print("Entered picture from webcam.")  # พิมพ์ข้อความระบุการจับภาพจากเว็บแคม
            if(nodemcu_data == "Stop"):  # ถ้าข้อมูลเป็น "Stop"
                stop_threads = True  # เปลี่ยนค่า stop_threads เป็น True
                print("Exiting...")  # พิมพ์ข้อความออกจากลูป
                break  # ออกจากลูป


def voice_mode():
    global mode, stop_threads, text_received, OCR, notification, frame  # กำหนดตัวแปร global ที่ใช้ในฟังก์ชัน
    recognizer = sr.Recognizer()  # สร้างตัวแปร Recognizer สำหรับการรู้จำเสียง

    while not stop_threads:  # ทำงานในขณะที่ stop_threads เป็น False
        try:
            with sr.Microphone() as source:  # ใช้ไมโครโฟนเป็นแหล่งเสียง
                #print("Adjusting for ambient noise...")  # ปรับการรับเสียงเพื่อกรองเสียงรบกวน
                recognizer.adjust_for_ambient_noise(source)  # ปรับตัวรับเสียงให้เข้ากับเสียงรบกวนรอบข้าง
                print("Say something (5 seconds limit)...")  # แสดงข้อความให้ผู้ใช้พูด
                audio = recognizer.listen(source, phrase_time_limit=5)  # รอรับเสียงจากไมโครโฟน

            text_received = recognizer.recognize_google(audio).lower()  # แปลเสียงที่รับเป็นข้อความ
            print("You said: " + text_received)  # แสดงข้อความที่ได้จากการรู้จำเสียง

            # ตรวจสอบคำสั่งเสียงที่ได้รับและทำการเปลี่ยนโหมด
            if "1" in text_received or "one" in text_received:  # ถ้าผู้ใช้พูด "1" หรือ "one"
                mode = 1  # เปลี่ยนโหมดเป็น 1 (ตรวจจับการล้ม)
                arduino_serial.write(("MODE:1\n").encode('utf-8'))  # ส่งคำสั่งไปยัง Arduino
                nodemcu_serial.write(("MODE:1\n").encode('utf-8'))  # ส่งคำสั่งไปยัง NodeMCU
                print("Mode changed to Fall detection.")  # พิมพ์ข้อความเปลี่ยนโหมด
            if "2" in text_received or "two" in text_received or "to" in text_received:  # ถ้าผู้ใช้พูด "2", "two", หรือ "to"
                mode = 2  # เปลี่ยนโหมดเป็น 2 (OCR)
                arduino_serial.write(("MODE:2\n").encode('utf-8'))  # ส่งคำสั่งไปยัง Arduino
                nodemcu_serial.write(("MODE:2\n").encode('utf-8'))  # ส่งคำสั่งไปยัง NodeMCU
                print("Mode changed to OCR.")  # พิมพ์ข้อความเปลี่ยนโหมด
            if "capture" in text_received:  # ถ้าผู้ใช้พูด "capture"
                OCR = True  # ตั้งค่า OCR เป็น True
                print("Entered picture from webcam.")  # พิมพ์ข้อความระบุการจับภาพจากเว็บแคม
            if "help" in text_received:  # ถ้าผู้ใช้พูด "help"
                cv2.imwrite('Help_image.jpg', frame)  # บันทึกภาพจากเฟรมปัจจุบัน
                sent_line_notify("ขอความช่วยเหลือ", "Help_image.jpg")  # ส่งการแจ้งเตือนขอความช่วยเหลือ
                print("Call for help.")  # พิมพ์ข้อความเรียกความช่วยเหลือ
            if "quit" in text_received:  # ถ้าผู้ใช้พูด "quit"
                stop_threads = True  # เปลี่ยนค่า stop_threads เป็น True
                print("Exiting via voice command.")  # พิมพ์ข้อความออกจากลูป
                break  # ออกจากลูป

        except sr.UnknownValueError:  # จัดการกรณีไม่สามารถรู้จำเสียงได้
            print("Sorry, I didn't understand that.")  # พิมพ์ข้อความแจ้งเตือน
        except sr.RequestError as e:  # จัดการกรณีเกิดข้อผิดพลาดกับ API ของ Google
            print(f"Error with Google Web Speech API: {e}")  # พิมพ์ข้อความแจ้งข้อผิดพลาด


def monitor_mode(): 
    global stop_threads  # กำหนดตัวแปร global เพื่อใช้ในฟังก์ชัน
    # ฟังก์ชันเพื่อรับค่า CPU usage
    def get_cpu_usage():
        return psutil.cpu_percent(interval=1)  # ส่งคืนเปอร์เซ็นต์การใช้งาน CPU

    # ฟังก์ชันเพื่อรับค่า RAM usage
    def get_memory_usage():
        memory = psutil.virtual_memory()  # รับข้อมูลการใช้งานหน่วยความจำ
        return memory.percent  # ส่งคืนเปอร์เซ็นต์การใช้งาน RAM

    # ฟังก์ชันเพื่อรับอุณหภูมิของระบบ
    def get_temperature():
        try:
            temps = psutil.sensors_temperatures()  # ตรวจสอบเซ็นเซอร์อุณหภูมิ
            if not temps:
                return "No temperature sensors found."  # ถ้าไม่มีเซ็นเซอร์ให้แจ้งเตือน

            # ถ้ามีเซ็นเซอร์ ให้ดึงข้อมูลเซ็นเซอร์ที่ใช้ได้ เช่น 'cpu-thermal' หรืออื่น ๆ
            for name, entries in temps.items():  
                for entry in entries:
                    return entry.current  # ส่งคืนอุณหภูมิปัจจุบันของเซ็นเซอร์
        except Exception as e:
            return "Error in temperature monitoring."  # แจ้งข้อผิดพลาดในการตรวจสอบอุณหภูมิ

    # ฟังก์ชันเพื่อส่งข้อมูลสถานะของระบบไปยัง Arduino และ NodeMCU
    def sent_system_stats():
        odroid_info = f"CPU:{get_cpu_usage()},RAM:{get_memory_usage()},TEMP:{get_temperature()}\n"  # สร้างข้อความที่ประกอบด้วย CPU, RAM, TEMP
        nodemcu_serial.write(odroid_info.encode('utf-8'))  # ส่งข้อมูลไปยัง NodeMCU
        arduino_serial.write(odroid_info.encode('utf-8'))  # ส่งข้อมูลไปยัง Arduino

    # ลูปเพื่อส่งข้อมูลสถานะของระบบอย่างต่อเนื่องในขณะที่ stop_threads เป็น False
    while not stop_threads:
        sent_system_stats()  # เรียกใช้ฟังก์ชันเพื่อส่งสถานะของระบบ

# Thread for changing the mode based on terminal input
def alarm_clock():
    global stop_threads, notification  # กำหนดตัวแปร global เพื่อใช้ในฟังก์ชัน
    # ลูปเพื่อเล่นเสียงเตือนในขณะที่ stop_threads เป็น False
    while not stop_threads:
        if notification == True:  # ถ้า notification เป็น True
            os.system("mpg321 screaming-monkeys.mp3")  # เล่นเสียง "screaming-monkeys.mp3"
        if notification == True:  # ถ้า notification เป็น True
            os.system("gorilla-tag-monkeys.mp3")  # เล่นเสียง "gorilla-tag-monkeys.mp3"
        if notification == True:  # ถ้า notification เป็น True
            os.system("mpg321 monkeys_HJucFGX.mp3")  # เล่นเสียง "monkeys_HJucFGX.mp3"

# เริ่มต้นเธรดสำหรับแต่ละฟังก์ชัน
capture_thread = threading.Thread(target=capture_frames)  # เธรดสำหรับจับภาพ
process_thread = threading.Thread(target=process_frames)  # เธรดสำหรับประมวลผลภาพ
voice_thread = threading.Thread(target=voice_mode)  # เธรดสำหรับการรับคำสั่งเสียง
serial_thread = threading.Thread(target=change_mode_serial)  # เธรดสำหรับการเปลี่ยนโหมด
monitor_thread = threading.Thread(target=monitor_mode)  # เธรดสำหรับตรวจสอบสถานะของระบบ
alarm_thread = threading.Thread(target=alarm_clock)  # เธรดสำหรับเสียงเตือน

# เริ่มต้นเธรดทั้งหมด
capture_thread.start()
process_thread.start()
voice_thread.start()
serial_thread.start()
monitor_thread.start()
alarm_thread.start()

# รอให้เธรดทั้งหมดเสร็จสิ้น
capture_thread.join()
process_thread.join()
voice_thread.join()
serial_thread.join()
monitor_thread.join()
alarm_thread.join()

cap.release()  # ปล่อยทรัพยากรจาก OpenCV
cv2.destroyAllWindows()  # ปิดหน้าต่าง OpenCV
