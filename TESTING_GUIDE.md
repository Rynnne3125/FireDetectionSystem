# 🔥 Hướng dẫn Test Hệ thống Báo cháy Thông minh

## 📋 Tổng quan luồng hoạt động đã sửa

### ✅ Luồng hoạt động mới:
```
ESP8266 Sensor → MQTT → Cloud AI Server → MQTT → ESP8266 Controller
ESP32-CAM → HTTP → Cloud AI Server → MQTT → ESP8266 Controller
```

## 🚀 Cách khởi động hệ thống

### 1. **Khởi động MQTT Broker**
```bash
# Windows (nếu đã cài Mosquitto)
mosquitto -v

# Hoặc dùng Docker
docker run -it -p 1883:1883 -p 9001:9001 eclipse-mosquitto
```

### 2. **Khởi động Cloud AI Server**
```bash
cd CloudAI_Server
pip install -r requirements.txt
python main.py
```

**Kết quả mong đợi:**
```
✅ MQTT broker connected successfully
📡 Subscribed to topic: fire/sensor
Starting Enhanced Fire Detection AI Server...
 * Running on all addresses (0.0.0.0)
 * Running on http://127.0.0.1:5000
 * Running on http://10.180.248.83:5000
```

### 3. **Nạp code cho các thiết bị**
- **ESP8266 Sensor**: Nạp `canhbaogass/canhbaogass.ino`
- **ESP32-CAM**: Nạp `CameraWebServer/CameraWebServer.ino`
- **ESP8266 Controller**: Nạp `FireDetection_Controller/FireDetection_Controller.ino`

## 📊 Cách xem dữ liệu từ các thiết bị

### 🌐 **1. Web Monitor Dashboard (KHUYẾN NGHỊ)**
Truy cập: `http://localhost:5000/monitor`

**Tính năng:**
- ✅ Xem dữ liệu real-time từ tất cả thiết bị
- ✅ Trạng thái hệ thống (Normal/Warning/Critical)
- ✅ Dữ liệu cảm biến (Smoke, Temperature, Humidity)
- ✅ Trạng thái Camera và Controller
- ✅ Auto-refresh mỗi 5 giây

### 📡 **2. MQTT Command Line (CHO DEBUG)**

#### Xem dữ liệu từ ESP8266 Sensor:
```bash
mosquitto_sub -h 10.180.248.83 -t "fire/sensor"
```

**Kết quả mong đợi:**
```json
{
  "timestamp": 1234567890,
  "device_id": "ESP8266_Sensor",
  "smoke": 250,
  "temperature": 25.5,
  "humidity": 60.2,
  "alert_active": false,
  "sprinkler_active": false,
  "alert_level": 0
}
```

#### Xem lệnh điều khiển:
```bash
mosquitto_sub -h 10.180.248.83 -t "fire/command"
```

#### Xem trạng thái hệ thống:
```bash
mosquitto_sub -h 10.180.248.83 -t "fire/status"
```

### 📹 **3. Kiểm tra ESP32-CAM**

#### Xem camera stream:
Truy cập: `http://[ESP32-CAM_IP]/`

#### Xem Serial Monitor ESP32-CAM:
**Kết quả mong đợi:**
```
📸 Sending image to cloud server...
📊 Image size: 15432 bytes
📊 Body size: 15456 bytes
✅ [HTTP] POST... code: 200
📄 Response: {"fire_detected":false,"confidence":0.15,"timestamp":"2024-01-01T12:00:00"}
🤖 Processing AI response: {"fire_detected":false,"confidence":0.15,"timestamp":"2024-01-01T12:00:00"}
✅ No fire detected. Confidence: 0.15
```

### 📊 **4. Kiểm tra ESP8266 Sensor**

#### Xem Serial Monitor ESP8266 Sensor:
**Kết quả mong đợi:**
```
=== Enhanced Fire Detection Sensor System ===
System initialized successfully
Smoke: 250
Temp: 25.5°C, Humidity: 60.2%
✅ Sensor data sent via MQTT
📊 Data: Smoke=250, Temp=25.5°C, Humidity=60.2%, Alert=NO
```

### 🎮 **5. Kiểm tra ESP8266 Controller**

#### Xem Serial Monitor ESP8266 Controller:
**Kết quả mong đợi:**
```
=== Enhanced Fire Detection Controller ===
System initialized successfully
Attempting MQTT connection...connected
```

## 🧪 Cách test hệ thống

### **Test 1: Kiểm tra kết nối cơ bản**

1. **Kiểm tra MQTT:**
```bash
# Terminal 1: Subscribe
mosquitto_sub -h 10.180.248.83 -t "fire/sensor"

# Terminal 2: Publish test
mosquitto_pub -h 10.180.248.83 -t "fire/sensor" -m '{"test":"data"}'
```

2. **Kiểm tra Web Server:**
```bash
curl http://localhost:5000/status
```

### **Test 2: Test cảm biến**

1. **Thổi khói vào cảm biến MQ2**
2. **Quan sát Serial Monitor ESP8266 Sensor:**
```
Smoke: 650
⚠️ ALERT ACTIVATED! Level: 2
✅ Sensor data sent via MQTT
📊 Data: Smoke=650, Temp=25.5°C, Humidity=60.2%, Alert=YES
```

3. **Quan sát Cloud AI Server:**
```
📨 Received MQTT message on topic: fire/sensor
🔍 Processing sensor data: {'smoke': 650, 'temperature': 25.5, 'humidity': 60.2}
🚨 Alert level updated to: 2
```

4. **Quan sát Web Monitor:**
- Status chuyển từ "🟢 System Normal" → "🚨 Critical Alert"
- Smoke Level hiển thị giá trị cao
- Alert Active chuyển thành "✅ Yes"

### **Test 3: Test Camera AI**

1. **Đưa ảnh có lửa vào camera**
2. **Quan sát Serial Monitor ESP32-CAM:**
```
📸 Sending image to cloud server...
📊 Image size: 15432 bytes
✅ [HTTP] POST... code: 200
📄 Response: {"fire_detected":true,"confidence":0.85,"fire_position":[320,240],"camera_angles":{"x":5.2,"y":-3.1}}
🔥 FIRE DETECTED! Confidence: 0.85
📍 Fire position: (320, 240)
📐 Camera angles: X=5.2°, Y=-3.1°
```

3. **Quan sát Cloud AI Server:**
```
FIRE DETECTED! Position: (320, 240), Confidence: 0.85
Sent command: FIRE_DETECTED with data: {'position': [320, 240], 'angles': {'x': 5.2, 'y': -3.1}, 'confidence': 0.85}
Sent command: ACTIVATE_SPRINKLER with data: {'position': [320, 240], 'angles': {'x': 5.2, 'y': -3.1}}
```

4. **Quan sát ESP8266 Controller:**
```
📨 MQTT Message arrived [fire/command] {"command":"FIRE_DETECTED","data":{"position":[320,240],"angles":{"x":5.2,"y":-3.1}}}
🎮 Received command: FIRE_DETECTED
Fire detected at (320, 240) - Moving to (50, -25)
Moving to position: X=50, Y=-25
Sprinkler activated
```

### **Test 4: Test điều khiển thủ công**

1. **Qua Web Monitor:**
```bash
curl -X POST http://localhost:5000/control \
  -H "Content-Type: application/json" \
  -d '{"command":"MANUAL_SPRINKLER","position":{"x":0,"y":0}}'
```

2. **Qua MQTT:**
```bash
mosquitto_pub -h 10.180.248.83 -t "fire/command" \
  -m '{"command":"ACTIVATE_SPRINKLER","data":{"position":{"x":0,"y":0}}}'
```

## 🔧 Troubleshooting

### **Vấn đề 1: MQTT không kết nối**
```bash
# Kiểm tra MQTT broker
mosquitto_sub -h localhost -t "test" -v

# Kiểm tra firewall
telnet 10.180.248.83 1883
```

### **Vấn đề 2: ESP32-CAM không gửi ảnh**
- Kiểm tra WiFi connection
- Kiểm tra địa chỉ cloud_server trong code
- Xem Serial Monitor để debug

### **Vấn đề 3: Cloud AI Server không nhận MQTT**
- Kiểm tra MQTT broker đang chạy
- Kiểm tra IP address trong code
- Xem log server để debug

### **Vấn đề 4: Web Monitor không hiển thị dữ liệu**
- Kiểm tra `/status` endpoint: `curl http://localhost:5000/status`
- Kiểm tra browser console (F12)
- Kiểm tra network connection

## 📱 Test với Blynk App

1. **Mở Blynk App trên điện thoại**
2. **Kết nối với project "canhbaochay"**
3. **Quan sát dữ liệu real-time:**
   - V0: Smoke Level
   - V1: Temperature
   - V2: Humidity
   - V3: Manual Sprinkler Control
   - V4: Manual Alarm Control
   - V5: System Reset

## 🎯 Kết quả mong đợi

### **Khi hệ thống hoạt động bình thường:**
- ✅ Web Monitor hiển thị "🟢 System Normal"
- ✅ ESP8266 Sensor gửi dữ liệu mỗi 5 giây
- ✅ ESP32-CAM gửi ảnh mỗi 5 giây
- ✅ Cloud AI Server nhận và xử lý dữ liệu
- ✅ Blynk App hiển thị dữ liệu real-time

### **Khi có cảnh báo:**
- ⚠️ Web Monitor chuyển sang "⚠️ Warning Level"
- 🔥 ESP8266 Sensor kích hoạt còi báo
- 📊 Cloud AI Server gửi lệnh điều khiển

### **Khi phát hiện cháy:**
- 🚨 Web Monitor chuyển sang "🚨 Critical Alert"
- 🔥 ESP32-CAM phát hiện lửa và gửi vị trí
- 💧 ESP8266 Controller kích hoạt vòi phun
- 🔊 Còi báo kích hoạt
- 🎮 Motor xoay đến vị trí cháy

---

**🎉 Chúc bạn test thành công! Nếu có vấn đề gì, hãy kiểm tra Serial Monitor của các thiết bị để debug.**
