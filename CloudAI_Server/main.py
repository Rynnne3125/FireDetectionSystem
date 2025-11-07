import requests
import os
import json
import time
import cv2
import numpy as np
from flask import Flask, jsonify, request
from flask_socketio import SocketIO
import paho.mqtt.client as mqtt
from datetime import datetime
from ultralytics import YOLO
import base64
from io import BytesIO
from PIL import Image
import torch
# --- CONFIG ---
MQTT_BROKER = '10.120.93.83'
MQTT_PORT = 1883
MQTT_TOPIC_SENSOR = 'fire/sensor'
MQTT_TOPIC_CAMERA = 'fire/camera'
MQTT_TOPIC_COMMAND = 'fire/command'
# --- BLYNK CLOUD CONFIG ---
BLYNK_TOKEN = "pkELg0f-LZJk9_9wtgC0bt4SzmJpznyw"  # 🔹 Thay token của bạn
BLYNK_BASE_URL = f"https://blynk.cloud/external/api"


# Load YOLO Model
print("🔥 Loading YOLO Fire Detection Model...")
# FIX: Sử dụng model pretrained hoặc đường dẫn model của bạn
try:
    # Load YOLO once, in evaluation mode
    yolo_model = YOLO('runs/detect/fire_detection_fast/weights/best.pt')
    yolo_model.to('cuda' if torch.cuda.is_available() else 'cpu')
    print("✅ YOLO custom fire detection model loaded successfully")

except Exception as e:
    print(f"⚠️ Could not load custom model, downloading pretrained model...")
    yolo_model = YOLO('yolov8n.pt')
    print("✅ YOLO Model loaded successfully")

# --- SYSTEM STATE CACHE ---
system_state = {
    "smoke": 0,
    "temperature": 0,
    "humidity": 0,
    "alert": False,
    "camera_alert": False,
    "fire_detected": False,
    "fire_confidence": 0,
    "fire_location": None,
    "last_update": None,
    "last_image": None,
    "detection_count": 0
}

# --- FLASK + SOCKET.IO INIT ---
app = Flask(__name__)

# FIX: Thay eventlet bằng threading (hoặc gevent)
socketio = SocketIO(app, 
                    cors_allowed_origins="*", 
                    logger=True, 
                    engineio_logger=True,
                    async_mode="threading",  # CHANGED FROM "eventlet"
                    max_http_buffer_size=10*1024*1024)  # 10MB cho ảnh

# --- YOLO FIRE DETECTION ---
def detect_fire_yolo(image_data):
    """
    Phát hiện lửa bằng YOLO
    Returns: (has_fire, confidence, bbox, annotated_image)
    """
    try:
        # Decode base64 image
        if isinstance(image_data, str):
            if 'base64,' in image_data:
                image_data = image_data.split('base64,')[1]
            image_bytes = base64.b64decode(image_data)
            image = Image.open(BytesIO(image_bytes))
            frame = cv2.cvtColor(np.array(image), cv2.COLOR_RGB2BGR)
        else:
            frame = image_data
        
        # YOLO inference
        results = yolo_model(frame, conf=0.5, verbose=False)[0]
        
        fire_detected = False
        max_confidence = 0
        fire_bbox = None
        fire_center = None
        
        # Vẽ bounding box và tính toán vị trí
        for box in results.boxes:
            cls = int(box.cls[0])
            conf = float(box.conf[0])
            x1, y1, x2, y2 = map(int, box.xyxy[0])
            
            # NOTE: YOLO pretrained không có class "fire"
            # Bạn cần train model riêng HOẶC dùng class gần giống (person=0, fire truck=?, etc)
            # Để test, tạm thời phát hiện bất kỳ object nào
            if conf > 0.3:  # Phát hiện bất kỳ object nào với confidence > 0.3
                fire_detected = True
                if conf > max_confidence:
                    max_confidence = conf
                    fire_bbox = (x1, y1, x2, y2)
                    
                    # Tính tâm đốm lửa (để điều khiển vòi phun)
                    center_x = (x1 + x2) // 2
                    center_y = (y1 + y2) // 2
                    fire_center = (center_x, center_y)
                
                # Vẽ bounding box
                color = (0, 0, 255)
                label = f'Fire {conf:.2f}'
                cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
                cv2.putText(frame, label, (x1, y1-10), 
                           cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)
        
        # Thêm timestamp và status
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        status_text = "FIRE DETECTED!" if fire_detected else "Normal"
        status_color = (0, 0, 255) if fire_detected else (0, 255, 0)
        
        cv2.putText(frame, timestamp, (10, 30), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 255), 2)
        cv2.putText(frame, status_text, (10, 60), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.8, status_color, 2)
        
        # Convert về base64 để gửi qua WebSocket
        _, buffer = cv2.imencode('.jpg', frame)
        img_base64 = base64.b64encode(buffer).decode('utf-8')
        
        return fire_detected, max_confidence, fire_center, img_base64
        
    except Exception as e:
        print(f"❌ YOLO Detection Error: {e}")
        import traceback
        traceback.print_exc()
        return False, 0, None, None

# --- CALCULATE MOTOR ANGLES ---
def calculate_motor_angles(fire_center, image_width=640, image_height=480):
    """
    Tính góc motor cần xoay dựa trên vị trí lửa trong ảnh
    """
    if not fire_center:
        return None
    
    center_x, center_y = fire_center
    
    # Tính góc theo tỷ lệ
    # Camera FOV thường là 60-70 độ
    fov_horizontal = 60
    fov_vertical = 45
    
    # Tính độ lệch so với tâm ảnh
    offset_x = (center_x - image_width/2) / (image_width/2)
    offset_y = (center_y - image_height/2) / (image_height/2)
    
    # Chuyển sang góc motor
    angle_x = offset_x * (fov_horizontal / 2)
    angle_y = offset_y * (fov_vertical / 2)
    
    return {
        "angle_x": round(angle_x, 2),
        "angle_y": round(angle_y, 2),
        "offset_x": round(offset_x, 2),
        "offset_y": round(offset_y, 2)
    }

# --- MQTT CALLBACKS ---
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ MQTT broker connected successfully")
        client.subscribe(MQTT_TOPIC_SENSOR)
        client.subscribe(MQTT_TOPIC_CAMERA)
        print(f"📡 Subscribed to: {MQTT_TOPIC_SENSOR}, {MQTT_TOPIC_CAMERA}")
    else:
        print(f"❌ MQTT failed with code {rc}")

def on_message(client, userdata, msg):
    topic = msg.topic
    print(f"📨 MQTT Message from: {topic}")
    
    try:
        # 🧭 Dữ liệu cảm biến ESP8266
        if topic == MQTT_TOPIC_SENSOR:
            payload_str = msg.payload.decode('utf-8')
            data = json.loads(payload_str)
            smoke = data.get("smoke")
            temp = data.get("temperature")
            hum = data.get("humidity")

            print(f"🌡️ Sensor Data Received -> Smoke={smoke} ppm, Temp={temp}°C, Humidity={hum}%")
            
            # Lưu trạng thái cũ để so sánh (debounce / chỉ emit khi có thay đổi)
            prev_alert = system_state.get("alert", False)
            prev_smoke = system_state.get("smoke")
            prev_temp = system_state.get("temperature")
            prev_hum = system_state.get("humidity")

            # Cập nhật trạng thái hệ thống (LƯU Ý: không lấy 'alert' từ device)
            system_state.update({
                "smoke": smoke,
                "temperature": temp,
                "humidity": hum,
                "last_update": datetime.now().strftime("%H:%M:%S")
            })

            # Phân tích và cập nhật flag alert trong system_state
            analyze_sensor_data(system_state)

            # Emit chỉ khi có thay đổi quan trọng (hoặc luôn emit nếu bạn muốn)
            changed = (
                system_state.get("alert") != prev_alert or
                system_state.get("smoke") != prev_smoke or
                system_state.get("temperature") != prev_temp or
                system_state.get("humidity") != prev_hum
            )

            if changed:
                print(f"🔔 Emitting sensor_update (alert={system_state.get('alert')})")
                socketio.emit('sensor_update', system_state)
            else:
                # Nếu muốn vẫn log cho debug
                print("ℹ️ No significant change, skipping emit.")

        # 📷 Ảnh từ ESP32-CAM
        elif topic == MQTT_TOPIC_CAMERA:
            payload_str = msg.payload.decode('utf-8')
            data = json.loads(payload_str)

            image_data = data.get("image")
            if image_data:
                print("📸 Processing image with YOLO...")
                
                # Phát hiện lửa bằng YOLO
                fire_detected, confidence, fire_center, annotated_img = detect_fire_yolo(image_data)

                # Đếm số khung hình phát hiện liên tiếp
                if fire_detected:
                    system_state["detection_count"] += 1
                else:
                    system_state["detection_count"] = max(0, system_state["detection_count"] - 1)

                # Cập nhật trạng thái hệ thống
                system_state.update({
                    "camera_alert": fire_detected,
                    "fire_detected": system_state["detection_count"] >= 3,  # cần >=3 frame
                    "fire_confidence": confidence,
                    "fire_location": fire_center,
                    "last_image": annotated_img,
                    "last_update": datetime.now().strftime("%H:%M:%S")
                })

                # Gửi ảnh và thông tin realtime lên Web Dashboard
                socketio.emit('camera_update', {
                    "image": annotated_img,
                    "fire_detected": fire_detected,
                    "confidence": confidence,
                    "fire_location": fire_center
                })

                # Nếu xác nhận cháy → kích hoạt phản ứng
                if system_state["fire_detected"]:
                    activate_fire_response(fire_center)

    except Exception as e:
        print(f"❌ Message Processing Error: {e}")
        import traceback
        traceback.print_exc()


# --- ANALYZE SENSOR DATA ---
def analyze_sensor_data(state):
    smoke = state.get("smoke", 0) or 0
    temp = state.get("temperature", 0) or 0
    hum = state.get("humidity", 0) or 0

    # Thresholds (tùy bạn điều chỉnh)
    SMOKE_LOW = 300   # nếu sensor khác, map lại scale
    SMOKE_HIGH = 500
    TEMP_LOW = 35.0
    TEMP_HIGH = 40.0
    HUM_THRESHOLD = 95.0  # optional

    # logic kết hợp: ưu tiên khói + nhiệt
    should_alert = False

    # Giả sử smoke sensor trả về giá trị scale lớn (ví dụ 0-1023).
    # Nếu bạn đang dùng giá trị nhỏ (ví dụ 18,20), map hoặc dùng thresholds phù hợp.
    if (smoke and temp):
        if (smoke > SMOKE_LOW and temp > TEMP_LOW) or (smoke > SMOKE_HIGH) or (temp > TEMP_HIGH):
            should_alert = True
    else:
        # fallback: chỉ dựa vào nhiệt
        if temp > TEMP_HIGH:
            should_alert = True

    # (Tuỳ chọn) ignore high humidity alone to avoid false positive
    # if hum > HUM_THRESHOLD and not should_alert:
    #     should_alert = False

    if should_alert and not state.get("alert"):
        print("🚨 SENSOR ALERT! High smoke/temperature detected!")
        state["alert"] = True
        # tăng tần suất chụp nếu cần
        if not state.get("fire_detected"):
            command = {"command": "INCREASE_CAPTURE_RATE"}
            mqtt_client.publish(MQTT_TOPIC_COMMAND, json.dumps(command))

    elif not should_alert and state.get("alert"):
        # reset khi bình thường
        print("✅ Sensor readings normalized.")
        state["alert"] = False

 
# --- ACTIVATE FIRE RESPONSE ---
def activate_fire_response(fire_center):
    """
    Kích hoạt phản ứng chữa cháy khi phát hiện lửa (YOLO xác nhận)
    """
    print("🚨🔥 FIRE CONFIRMED! Activating fire suppression system...")

    # Tính góc motor hướng đến vị trí đám cháy
    angles = calculate_motor_angles(fire_center)

    # Gửi lệnh điều khiển cho ESP qua MQTT
    command = {
        "command": "ACTIVATE_SPRINKLER",
        "data": {
            "position": "ON",
            "angles": angles,
            "confidence": system_state["fire_confidence"],
            "timestamp": datetime.now().isoformat()
        }
    }
    mqtt_client.publish(MQTT_TOPIC_COMMAND, json.dumps(command))
    print("📡 MQTT: Fire command sent to ESP")

    # 🔄 Gửi cảnh báo lên Blynk Cloud
    try:
        # V3 = trạng thái cháy, V4 = độ tin cậy
        requests.get(f"{BLYNK_BASE_URL}/update?token={BLYNK_TOKEN}&V3=1")
        requests.get(f"{BLYNK_BASE_URL}/update?token={BLYNK_TOKEN}&V4={system_state['fire_confidence']:.2f}")

        # V5/V6 = tọa độ đám cháy
        if fire_center:
            requests.get(f"{BLYNK_BASE_URL}/update?token={BLYNK_TOKEN}&V5={fire_center[0]}")
            requests.get(f"{BLYNK_BASE_URL}/update?token={BLYNK_TOKEN}&V6={fire_center[1]}")
        print("✅ Fire alert sent to Blynk Cloud")
    except Exception as e:
        print("⚠️ Could not update Blynk Cloud:", e)

    # Cập nhật trạng thái nội bộ và Dashboard
    system_state.update({
        "alert": True,
        "sprinkler_active": True,
        "motor_angles": angles
    })

    socketio.emit('fire_alert', system_state)


# --- MQTT CLIENT INIT ---
mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message

try:
    mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
    mqtt_client.loop_start()
    print(f"✅ MQTT Client started: {MQTT_BROKER}:{MQTT_PORT}")
except Exception as e:
    print(f"⚠️ Could not connect to MQTT broker: {e}")

# --- REST API ENDPOINTS ---
@app.route('/status', methods=['GET'])
def get_status():
    """Lấy trạng thái hệ thống"""
    return jsonify(system_state)

@app.route('/upload', methods=['POST'])
def upload_image():
    """
    Endpoint để ESP32-CAM gửi ảnh qua HTTP (alternative to MQTT)
    """
    try:
        data = request.json
        image_data = data.get('image')
        
        if not image_data:
            return jsonify({"error": "No image data"}), 400
        
        # Phát hiện lửa
        fire_detected, confidence, fire_center, annotated_img = detect_fire_yolo(image_data)
        
        # Cập nhật state
        system_state.update({
            "camera_alert": fire_detected,
            "fire_confidence": confidence,
            "fire_location": fire_center,
            "last_image": annotated_img,
            "last_update": datetime.now().strftime("%H:%M:%S")
        })
        
        # Phát WebSocket
        socketio.emit('camera_update', {
            "image": annotated_img,
            "fire_detected": fire_detected,
            "confidence": confidence
        })
        
        return jsonify({
            "fire_detected": fire_detected,
            "confidence": confidence,
            "location": fire_center,
            "timestamp": system_state["last_update"]
        })
        
    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route('/control', methods=['POST'])
def manual_control():
    """Điều khiển thủ công"""
    try:
        data = request.json
        command = data.get('command')
        
        if command == "RESET":
            system_state.update({
                "alert": False,
                "fire_detected": False,
                "detection_count": 0,
                "sprinkler_active": False
            })
            mqtt_client.publish(MQTT_TOPIC_COMMAND, json.dumps({"command": "RESET_SYSTEM"}))
        
        return jsonify({"status": "success", "command": command})
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# --- WEB DASHBOARD ---
@app.route('/')
def monitor_page():
    return """
<!DOCTYPE html>
<html>
<head>
<title>🔥 Fire Detection AI Dashboard</title>
<script src="https://cdn.socket.io/4.7.2/socket.io.min.js"></script>
<style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { 
        font-family: 'Segoe UI', Arial; 
        background: linear-gradient(135deg, #1e3c72 0%, #2a5298 100%);
        color: #fff; 
        padding: 20px;
    }
    .container { max-width: 1400px; margin: 0 auto; }
    h1 { text-align: center; margin-bottom: 30px; text-shadow: 2px 2px 4px rgba(0,0,0,0.3); }
    .dashboard { display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
    .panel { 
        background: rgba(255,255,255,0.1); 
        backdrop-filter: blur(10px);
        padding: 20px; 
        border-radius: 15px;
        box-shadow: 0 8px 32px rgba(0,0,0,0.2);
    }
    .panel h2 { margin-bottom: 15px; border-bottom: 2px solid rgba(255,255,255,0.3); padding-bottom: 10px; }
    .alert { 
        color: #ff4444; 
        font-weight: bold; 
        animation: blink 1s infinite;
        font-size: 1.2em;
    }
    @keyframes blink { 50% { opacity: 0.3; } }
    .sensor-data { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-top: 15px; }
    .sensor-item {
        background: rgba(255,255,255,0.1);
        padding: 15px;
        border-radius: 10px;
        text-align: center;
    }
    .sensor-value { font-size: 2em; font-weight: bold; margin: 10px 0; }
    .camera-view { text-align: center; }
    #camera-image { 
        max-width: 100%; 
        border-radius: 10px; 
        margin-top: 15px;
        box-shadow: 0 4px 15px rgba(0,0,0,0.3);
    }
    #log { 
        max-height: 200px; 
        overflow-y: auto; 
        background: rgba(0,0,0,0.3); 
        padding: 15px; 
        border-radius: 10px;
        margin-top: 15px;
        font-family: 'Courier New', monospace;
        font-size: 0.9em;
    }
    .status-indicator {
        display: inline-block;
        width: 15px;
        height: 15px;
        border-radius: 50%;
        margin-right: 8px;
    }
    .status-normal { background: #4CAF50; }
    .status-alert { background: #ff4444; }
    .control-buttons {
        display: flex;
        gap: 10px;
        margin-top: 15px;
    }
    .btn {
        flex: 1;
        padding: 12px;
        border: none;
        border-radius: 8px;
        font-size: 1em;
        cursor: pointer;
        transition: all 0.3s;
    }
    .btn-danger { background: #ff4444; color: white; }
    .btn-success { background: #4CAF50; color: white; }
    .btn:hover { transform: translateY(-2px); box-shadow: 0 4px 12px rgba(0,0,0,0.3); }
</style>
</head>
<body>
<div class="container">
    <h1>🔥 Fire Detection AI Dashboard - YOLO + Sensors</h1>
    
    <div class="dashboard">
        <!-- Sensor Panel -->
        <div class="panel">
            <h2>📊 Sensor Data (ESP8266)</h2>
            <div class="sensor-data">
                <div class="sensor-item">
                    <div>💨 Smoke Level</div>
                    <div class="sensor-value" id="smoke">-</div>
                    <div>ppm</div>
                </div>
                <div class="sensor-item">
                    <div>🌡️ Temperature</div>
                    <div class="sensor-value" id="temp">-</div>
                    <div>°C</div>
                </div>
                <div class="sensor-item">
                    <div>💧 Humidity</div>
                    <div class="sensor-value" id="humidity">-</div>
                    <div>%</div>
                </div>
                <div class="sensor-item">
                    <div>🚨 Status</div>
                    <div id="sensor-status">Normal</div>
                </div>
            </div>
        </div>
        
        <!-- Camera Panel -->
        <div class="panel camera-view">
            <h2>📷 AI Camera (ESP32-CAM + YOLO)</h2>
            <div id="ai-status">Waiting for camera...</div>
            <img id="camera-image" src="" alt="Camera feed" style="display:none;">
            <div style="margin-top: 15px;">
                <div>🔥 Fire Confidence: <span id="confidence">0%</span></div>
                <div>📍 Location: <span id="location">-</span></div>
            </div>
        </div>
        
        <!-- System Status Panel -->
        <div class="panel">
            <h2>⚙️ System Status</h2>
            <div style="margin-top: 15px; font-size: 1.1em;">
                <div style="margin: 10px 0;">
                    <span class="status-indicator" id="mqtt-indicator"></span>
                    MQTT Connection: <span id="mqtt-status">Connecting...</span>
                </div>
                <div style="margin: 10px 0;">
                    <span class="status-indicator" id="fire-indicator"></span>
                    Fire Detection: <span id="fire-status">Normal</span>
                </div>
                <div style="margin: 10px 0;">
                    🎯 Motor Angles: X=<span id="angle-x">0</span>° Y=<span id="angle-y">0</span>°
                </div>
                <div style="margin: 10px 0;">
                    💦 Sprinkler: <span id="sprinkler-status">OFF</span>
                </div>
            </div>
            
            <div class="control-buttons">
                <button class="btn btn-danger" onclick="resetSystem()">🔄 Reset System</button>
                <button class="btn btn-success" onclick="testAlert()">🧪 Test Alert</button>
            </div>
        </div>
        
        <!-- Log Panel -->
        <div class="panel">
            <h2>📝 Activity Log</h2>
            <div id="log"></div>
        </div>
    </div>
</div>

<script>
const socket = io("http://10.120.93.83:5000", {
    transports: ["websocket"],
    reconnection: true,
    reconnectionAttempts: 5,
    reconnectionDelay: 1000 
});

function addLog(message) {
    const logDiv = document.getElementById('log');
    const time = new Date().toLocaleTimeString();
    logDiv.innerHTML += `<div>[${time}] ${message}</div>`;
    logDiv.scrollTop = logDiv.scrollHeight;
}

socket.on('connect', () => {
    console.log("✅ WebSocket connected");
    document.getElementById('mqtt-status').textContent = 'Connected';
    document.getElementById('mqtt-indicator').className = 'status-indicator status-normal';
    addLog('✅ Connected to server');
});

socket.on('disconnect', () => {
    document.getElementById('mqtt-status').textContent = 'Disconnected';
    document.getElementById('mqtt-indicator').className = 'status-indicator status-alert';
    addLog('❌ Disconnected from server');
});

// Cập nhật dữ liệu cảm biến
socket.on('sensor_update', (state) => {
    document.getElementById('smoke').textContent = state.smoke || '-';
    document.getElementById('temp').textContent = state.temperature || '-';
    document.getElementById('humidity').textContent = state.humidity || '-';
    
    const sensorStatus = document.getElementById('sensor-status');
    if (state.alert) {
        sensorStatus.innerHTML = '<span class="alert">⚠️ ALERT</span>';
    } else {
        sensorStatus.textContent = '✅ Normal';
    }
    
    addLog(`📊 Sensor: Smoke=${state.smoke}, Temp=${state.temperature}°C`);
});

// Cập nhật ảnh camera
socket.on('camera_update', (data) => {
    const img = document.getElementById('camera-image');
    img.src = 'data:image/jpeg;base64,' + data.image;
    img.style.display = 'block';
    
    document.getElementById('confidence').textContent = 
        data.fire_detected ? (data.confidence * 100).toFixed(1) + '%' : '0%';
    
    document.getElementById('location').textContent = 
        data.fire_location ? `X:${data.fire_location[0]}, Y:${data.fire_location[1]}` : '-';
    
    const aiStatus = document.getElementById('ai-status');
    if (data.fire_detected) {
        aiStatus.innerHTML = '<span class="alert">🔥 FIRE DETECTED!</span>';
        addLog(`🔥 FIRE detected with ${(data.confidence*100).toFixed(1)}% confidence!`);
    } else {
        aiStatus.innerHTML = '✅ No fire detected';
    }
});

// Cảnh báo hỏa hoạn
socket.on('fire_alert', (state) => {
    document.getElementById('fire-status').innerHTML = '<span class="alert">🔥 FIRE!</span>';
    document.getElementById('fire-indicator').className = 'status-indicator status-alert';
    document.getElementById('sprinkler-status').innerHTML = '<span class="alert">ON 💦</span>';
    
    if (state.motor_angles) {
        document.getElementById('angle-x').textContent = state.motor_angles.angle_x;
        document.getElementById('angle-y').textContent = state.motor_angles.angle_y;
    }
    
    addLog('🚨🔥 FIRE ALERT ACTIVATED! Sprinkler system engaged!');
});

function resetSystem() {
    fetch('/control', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({command: 'RESET'})
    }).then(() => {
        addLog('🔄 System reset requested');
        document.getElementById('fire-status').textContent = 'Normal';
        document.getElementById('fire-indicator').className = 'status-indicator status-normal';
        document.getElementById('sprinkler-status').textContent = 'OFF';
    });
}

function testAlert() {
    addLog('🧪 Test alert triggered');
}
</script>
</body>
</html>
"""

if __name__ == "__main__":
    print("🚀 Starting Fire Detection AI Server with YOLO...")
    print("📡 MQTT Broker:", MQTT_BROKER)
    print("🔥 YOLO Model loaded and ready")
    print("\n⚠️ NOTE: Using pretrained YOLO model. For actual fire detection,")
    print("   you need to train a custom model with fire dataset.\n")
    socketio.run(app, host="0.0.0.0", port=5000, debug=True)