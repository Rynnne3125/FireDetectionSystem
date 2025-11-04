# Hệ thống báo cháy thông minh với AI Hybrid

## 🚨 Tổng quan

Hệ thống báo cháy thông minh sử dụng kiến trúc hybrid kết hợp **Computer Vision AI** và **cảm biến truyền thống** để phát hiện và xử lý cháy tự động.

### ✨ Tính năng chính

- 🔥 **Phát hiện cháy thông minh**: Kết hợp AI computer vision và cảm biến khói/nhiệt độ
- 📹 **Camera tự động**: ESP32-CAM chụp ảnh và gửi lên AI server phân tích
- 🎯 **Định vị chính xác**: AI xác định vị trí cháy và điều khiển camera/vòi phun
- 💧 **Chữa cháy tự động**: Vòi phun nước tự động kích hoạt và xoay đến vị trí cháy
- 📱 **Giám sát từ xa**: Blynk IoT app để theo dõi và điều khiển
- 🔄 **Hệ thống đa cấp**: Cảnh báo sớm → Xác nhận AI → Chữa cháy tự động

## 🏗️ Kiến trúc hệ thống

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   ESP32-CAM     │    │   ESP8266       │    │   ESP8266       │
│   (Camera AI)   │    │   (Sensors)     │    │   (Controller)  │
├─────────────────┤    ├─────────────────┤    ├─────────────────┤
│ • Chụp ảnh      │    │ • MQ2 (khói)    │    │ • Motor X/Y     │
│ • Gửi lên AI    │    │ • DHT11 (nhiệt) │    │ • Vòi phun      │
│ • Nhận lệnh     │    │ • Gửi dữ liệu   │    │ • Còi báo       │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         └───────────────────────┼───────────────────────┘
                                 │
                    ┌─────────────────┐
                    │  Cloud AI Server │
                    │   (Python Flask) │
                    ├─────────────────┤
                    │ • Nhận ảnh      │
                    │ • AI Detection  │
                    │ • Fusion Logic  │
                    │ • Điều khiển    │
                    └─────────────────┘
```

## 📁 Cấu trúc dự án

```
FireDetectionSystem/
├── CameraWebServer/           # ESP32-CAM code
│   ├── CameraWebServer.ino   # Main camera code
│   ├── app_httpd.cpp         # HTTP server
│   └── camera_pins.h         # Pin configuration
├── canhbaogass/              # ESP8266 Sensor code
│   └── canhbaogass.ino       # Sensor reading & MQTT
├── FireDetection_Controller/ # ESP8266 Controller code
│   └── FireDetection_Controller.ino # Motor control & MQTT
├── CloudAI_Server/           # Python AI Server
│   ├── main.py              # Flask server & AI logic
│   ├── requirements.txt     # Python dependencies
│   └── HDSD_AI_Cloud.txt    # Setup guide
└── libraries/               # Arduino libraries
```

## 🚀 Cài đặt nhanh

### 1. Cloud AI Server
```bash
cd CloudAI_Server
pip install -r requirements.txt
python main.py
```

### 2. MQTT Broker
```bash
# Ubuntu/Debian
sudo apt install mosquitto mosquitto-clients

# Windows: Tải từ https://mosquitto.org/download/
```

### 3. Arduino Libraries
Cài đặt các thư viện sau trong Arduino IDE:
- `ArduinoJson`
- `PubSubClient`
- `AccelStepper`
- `SimpleKalmanFilter`

### 4. Nạp code
1. **ESP32-CAM**: Nạp `CameraWebServer/CameraWebServer.ino`
2. **ESP8266 Sensor**: Nạp `canhbaogass/canhbaogass.ino`
3. **ESP8266 Controller**: Nạp `FireDetection_Controller/FireDetection_Controller.ino`

## ⚙️ Cấu hình

### WiFi & Network
Cập nhật thông tin WiFi và IP addresses trong các file:
- `ssid` và `password` trong tất cả file .ino
- `cloud_server` IP trong CameraWebServer.ino
- `mqtt_server` IP trong các file ESP8266

### Blynk IoT
1. Tạo project mới với template ID tương ứng
2. Cấu hình Virtual Pins theo hướng dẫn
3. Cập nhật Auth token trong code

### Ngưỡng cảnh báo
Điều chỉnh các ngưỡng trong `CloudAI_Server/main.py`:
```python
SMOKE_THRESHOLD = 500
TEMP_THRESHOLD = 40.0
HUMIDITY_THRESHOLD = 80.0
```

## 🔄 Luồng hoạt động

### Bình thường
1. Cảm biến MQ2, DHT11 đọc dữ liệu liên tục
2. ESP32-CAM chụp ảnh mỗi 5 giây
3. AI phân tích ảnh với độ tin cậy thấp

### Khi có cảnh báo
1. Cảm biến phát hiện khói/nhiệt độ cao
2. Tăng tần suất chụp ảnh lên 1 giây
3. AI tăng độ nhạy phân tích
4. Nếu xác nhận cháy → Kích hoạt toàn bộ hệ thống

### Khi phát hiện cháy
1. AI xác định vị trí cháy trong ảnh
2. Tính toán góc xoay camera/vòi phun
3. Gửi lệnh MQTT điều khiển motor
4. Kích hoạt vòi phun và còi báo
5. Tự động tắt sau 30-60 giây

## 🛠️ Troubleshooting

### Camera không hoạt động
- ✅ Kiểm tra kết nối WiFi
- ✅ Xác nhận địa chỉ server
- ✅ Kiểm tra PSRAM configuration

### MQTT không kết nối
- ✅ Kiểm tra broker address và port
- ✅ Xác nhận firewall không chặn port 1883
- ✅ Kiểm tra WiFi connection

### Cảm biến không đọc được
- ✅ Kiểm tra kết nối pin
- ✅ Calibrate ngưỡng cảnh báo
- ✅ Kiểm tra nguồn điện

## 📊 API Endpoints

### Cloud AI Server
- `POST /upload` - Gửi ảnh để phân tích
- `POST /sensor_data` - Gửi dữ liệu cảm biến
- `GET /status` - Lấy trạng thái hệ thống
- `POST /control` - Điều khiển thủ công

### MQTT Topics
- `fire/command` - Lệnh điều khiển
- `fire/sensor` - Dữ liệu cảm biến
- `fire/status` - Trạng thái hệ thống

## 🔮 Mở rộng

### Tính năng bổ sung
- [ ] Machine Learning model training
- [ ] Database logging
- [ ] Mobile app notification
- [ ] Multi-camera support
- [ ] Weather integration

### Tích hợp
- [ ] Home Assistant
- [ ] Google Assistant
- [ ] Telegram bot
- [ ] Email alerts

## 📝 License

MIT License - Xem file LICENSE để biết thêm chi tiết.

## 🤝 Đóng góp

Mọi đóng góp đều được chào đón! Vui lòng tạo issue hoặc pull request.

## 📞 Hỗ trợ

Nếu gặp vấn đề, vui lòng:
1. Đọc file `CloudAI_Server/HDSD_AI_Cloud.txt` để biết hướng dẫn chi tiết
2. Kiểm tra Serial Monitor của các thiết bị
3. Tạo issue với thông tin lỗi chi tiết

---

**⚠️ Lưu ý**: Hệ thống này chỉ dành cho mục đích nghiên cứu và học tập. Để sử dụng trong thực tế, cần kiểm tra và chứng nhận an toàn từ các cơ quan có thẩm quyền.
