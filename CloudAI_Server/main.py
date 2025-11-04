import os
import json
import time
from flask import Flask, jsonify
from flask_socketio import SocketIO
import paho.mqtt.client as mqtt
from datetime import datetime

# --- CONFIG ---
MQTT_BROKER = '172.19.0.83'
MQTT_PORT = 1883
MQTT_TOPIC_SENSOR = 'fire/sensor'

# --- SYSTEM STATE CACHE ---
system_state = {
    "smoke": 0,
    "temperature": 0,
    "humidity": 0,
    "alert": False,
    "last_update": None
}

# --- FLASK + SOCKET.IO INIT ---
app = Flask(__name__)
socketio = SocketIO(app, 
                    cors_allowed_origins="*", 
                    logger=True, 
                    engineio_logger=True,
                    async_mode="eventlet")
# --- MQTT CALLBACKS ---
def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("✅ MQTT broker connected successfully")
        client.subscribe(MQTT_TOPIC_SENSOR)
        print(f"📡 Subscribed to topic: {MQTT_TOPIC_SENSOR}")
    else:
        print(f"❌ MQTT failed with code {rc}")

def on_message(client, userdata, msg):
    print(f"📨 MQTT Message Received: {msg.topic}")
    
    try:
        payload_str = msg.payload.decode('utf-8')
        print(f"📄 Raw Payload: {payload_str}")

        data = json.loads(payload_str)  # MUST BE VALID JSON
        print(f"✅ JSON Parsed: {data}")

        system_state.update({
            "smoke": data.get("smoke"),
            "temperature": data.get("temperature"),
            "humidity": data.get("humidity"),
            "alert": data.get("alert", False),
            "last_update": datetime.now().strftime("%H:%M:%S")
        })

        socketio.emit('mqtt_update', system_state)
        analyze_and_act(system_state)

    except Exception as e:
        print(f"❌ JSON Decode Error: {e}")


def analyze_and_act(state):
    smoke = state["smoke"]
    temp = state["temperature"]
    humidity = state["humidity"]

    # ⚙️ Logic AI đơn giản
    if smoke > 20 or temp > 30:
        print("🚨 FIRE DETECTED! Activating sprinkler!")
        # Gửi lệnh MQTT điều khiển lại NodeMCU
        command = {"command": "ACTIVATE_SPRINKLER", "data": {"position": "ON"}}
        mqtt_client.publish("fire/command", json.dumps(command))
        system_state.update({
        "alert": True,
        "sprinkler_active": True,
        "last_update": datetime.now().strftime("%H:%M:%S")
    })

    # 🔁 Phát realtime tới WebSocket
        socketio.emit('mqtt_update', system_state)

    elif smoke < 15 or temp < 28:
        print("✅ Conditions normal, turning off sprinkler.")
        command = {"command": "RESET_SYSTEM"}
        mqtt_client.publish("fire/command", json.dumps(command))


mqtt_client = mqtt.Client()
mqtt_client.on_connect = on_connect
mqtt_client.on_message = on_message
mqtt_client.connect(MQTT_BROKER, MQTT_PORT, 60)
mqtt_client.loop_start()

# --- ENDPOINTS ---
@app.route('/status', methods=['GET'])
def get_status():
    return jsonify(system_state)

@app.route('/')
def monitor_page():
    return """
<!DOCTYPE html>
<html>
<head>
<title>Fire Detection Realtime Monitor</title>
<script src="https://cdn.socket.io/4.7.2/socket.io.min.js"></script>
<style>
    body { font-family: Arial; background: #111; color: #eee; padding: 20px; }
    .alert { color: red; font-weight: bold; }
    #log { max-height: 200px; overflow-y: auto; background: #222; padding: 10px; }
</style>
</head>
<body>
<h1>🔥 Fire Detection Realtime Monitor</h1>

<div id="state"></div>
<h3>MQTT Log:</h3>
<div id="log"></div>

<script>
const socket = io("http://172.19.0.83:5000", {
  transports: ["websocket"], // dùng WebSocket thuần
  reconnection: true,
  reconnectionAttempts: 5,
  reconnectionDelay: 1000
});


socket.on('connect', () => console.log("✅ WebSocket connected"));

socket.on('mqtt_update', (state) => {
    let alertText = state.alert ? "<span class='alert'>ALERT!</span>" : "Normal";
    document.getElementById('state').innerHTML =
        `<p>Smoke: ${state.smoke}</p>
         <p>Temp: ${state.temperature}°C</p>
         <p>Humidity: ${state.humidity}%</p>
         <p>Status: ${alertText}</p>
         <p>Last Update: ${state.last_update}</p>`;

    let logDiv = document.getElementById('log');
    logDiv.innerHTML += `[${state.last_update}] Smoke=${state.smoke}, Temp=${state.temperature}, Hum=${state.humidity}<br>`;
    logDiv.scrollTop = logDiv.scrollHeight;
});
</script>
</body>
</html>
"""


if __name__ == "__main__":
    print("🚀 Starting Fire Detection Monitoring Server...")
    socketio.run(app, host="0.0.0.0", port=5000)
