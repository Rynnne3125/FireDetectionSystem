
#define BLYNK_PRINT Serial
#include <ESP8266WiFi.h>
#define BLYNK_TEMPLATE_ID "TMPL6Nn3f5huh"
#define BLYNK_TEMPLATE_NAME "Quickstart Template"

#include <BlynkSimpleEsp8266.h>
#include <DHT.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

char auth[] = "qGubvkBdm5Kr4ThlKh690-6VOFj-bDlI";
char ssid[] = "Phong";//Enter your WIFI name
char pass[] = "rintran3125";//Enter your WIFI password

// MQTT Configuration
const char* mqtt_server = "172.19.0.83";
const int mqtt_port = 1883;
const char* mqtt_topic_sensor = "fire/sensor";
const char* mqtt_topic_command = "fire/command";

DHT dht(D5, DHT11); //(sensor pin,sensor type)
BlynkTimer timer;

// Define component pins
#define voinuoc D7
#define canhbao D1
#define AD A0

// System State
int smoke_level = 0;
float temperature = 0.0;
float humidity = 0.0;
bool alert_active = false;
bool sprinkler_active = false;
unsigned long last_sensor_send = 0;
unsigned long last_alert_time = 0;

// Thresholds
const int SMOKE_THRESHOLD_LOW = 300;
const int SMOKE_THRESHOLD_HIGH = 500;
const float TEMP_THRESHOLD_LOW = 35.0;
const float TEMP_THRESHOLD_HIGH = 40.0;
const float HUMIDITY_THRESHOLD = 80.0;

// MQTT Client
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

void setup() {
  Serial.begin(115200);  // tốc độ cao hơn để debug
  Serial.println("=== Enhanced Fire Detection Sensor System ===");
 
  pinMode(canhbao, OUTPUT);
  pinMode(voinuoc, OUTPUT);
  digitalWrite(voinuoc, LOW);
  digitalWrite(canhbao, LOW);
  
  dht.begin();
  delay(2000);
  Blynk.begin(auth, ssid, pass, "blynk.cloud", 80);
  
  // Setup MQTT
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);

  // Setup timers
  timer.setInterval(1000L, datsensor);        // Read sensors every 1 second
  timer.setInterval(1000L, DHT11sensor);     // đọc DHT 3 giây/lần
  timer.setInterval(5000L, sendMQTTSensorData); // Send to MQTT every 5 seconds
  timer.setInterval(100L, checkAlertConditions); // Check alerts every 100ms

  Serial.println("System initialized successfully");
}

//Get the MQ2 sensor values
void datsensor() {
  smoke_level = analogRead(AD);
  Serial.printf("Smoke: %d\n", smoke_level);
  Blynk.virtualWrite(V0, smoke_level);
}

//Get the DHT11 sensor values
void DHT11sensor() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (isnan(h) || isnan(t)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  
  temperature = t;
  humidity = h;
  
  Blynk.virtualWrite(V1, t);
  Blynk.virtualWrite(V2, h);
  Serial.printf("Temp: %.1f°C, Humidity: %.1f%%\n", temperature, humidity);
}

// MQTT Functions
void reconnectMQTT() {
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    if (mqtt_client.connect("FireSensor")) {
      Serial.println("connected");
      mqtt_client.subscribe(mqtt_topic_command);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  
  String message = "";
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // Parse JSON message
  DynamicJsonDocument doc(512);
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  String command = doc["command"];

  if (command == "ACTIVATE_SPRINKLER") {
    bool activate = doc["data"].containsKey("position") || doc["data"].containsKey("angles");
    activateSprinkler(activate);
  } else if (command == "RESET_SYSTEM") {
    activateAlert(false);
    activateSprinkler(false);
    Serial.println("System reset via MQTT");
  }
}

void sendMQTTSensorData() {
  if (!mqtt_client.connected()) {
    Serial.println("MQTT not connected, attempting to reconnect...");
    reconnectMQTT();
    return;
  }
  
  DynamicJsonDocument doc(512);
  doc["timestamp"] = millis();
  doc["device_id"] = "ESP8266_Sensor";
  doc["smoke"] = smoke_level;
  doc["temperature"] = temperature;
  doc["humidity"] = humidity;
  doc["alert_active"] = alert_active;
  doc["sprinkler_active"] = sprinkler_active;
  doc["alert_level"] = getCurrentAlertLevel();
  
  String sensor_message;
  serializeJson(doc, sensor_message);
  
  bool success = mqtt_client.publish(mqtt_topic_sensor, sensor_message.c_str());
  
  if (success) {
    Serial.println("✅ Sensor data sent via MQTT");
    Serial.printf("📊 Data: Smoke=%d, Temp=%.1f°C, Humidity=%.1f%%, Alert=%s\n", 
                  smoke_level, temperature, humidity, alert_active ? "YES" : "NO");
  } else {
    Serial.println("❌ Failed to send sensor data via MQTT");
  }
}

int getCurrentAlertLevel() {
  if (smoke_level > SMOKE_THRESHOLD_HIGH || temperature > TEMP_THRESHOLD_HIGH) {
    return 2; // Critical
  } else if (smoke_level > SMOKE_THRESHOLD_LOW || temperature > TEMP_THRESHOLD_LOW) {
    return 1; // Warning
  }
  return 0; // Normal
}

void checkAlertConditions() {
  bool should_alert = false;
  int alert_level = 0;
  
  // Check smoke levels
  if (smoke_level > SMOKE_THRESHOLD_HIGH) {
    should_alert = true;
    alert_level = 2; // Critical
  } else if (smoke_level > SMOKE_THRESHOLD_LOW) {
    should_alert = true;
    alert_level = 1; // Warning
  }
  
  // Check temperature
  if (temperature > TEMP_THRESHOLD_HIGH) {
    should_alert = true;
    alert_level = max(alert_level, 2); // Critical
  } else if (temperature > TEMP_THRESHOLD_LOW) {
    should_alert = true;
    alert_level = max(alert_level, 1); // Warning
  }
  
  // Check humidity (high humidity can indicate fire)
  if (humidity > HUMIDITY_THRESHOLD) {
    should_alert = true;
    alert_level = max(alert_level, 1); // Warning
  }
  
  // Activate/deactivate alert
  if (should_alert && !alert_active) {
    activateAlert(true);
    last_alert_time = millis();
    Serial.printf("ALERT ACTIVATED! Level: %d\n", alert_level);
  } else if (!should_alert && alert_active) {
    // Deactivate alert after 30 seconds of normal readings
    if (millis() - last_alert_time > 30000) {
      activateAlert(false);
      Serial.println("Alert deactivated - conditions normalized");
    }
  }
}

void activateAlert(bool activate) {
  alert_active = activate;
  digitalWrite(canhbao, activate ? HIGH : LOW);
  
  if (activate) {
    Serial.println("ALERT: Fire detection sensors triggered!");
  } else {
    Serial.println("Alert cleared");
  }
}

void activateSprinkler(bool activate) {
  sprinkler_active = activate;
  digitalWrite(voinuoc, activate ? HIGH : LOW);
  
  if (activate) {
    Serial.println("Sprinkler activated");
  } else {
    Serial.println("Sprinkler deactivated");
  }
}


BLYNK_WRITE(V3){ 
  int p = param.asInt();
  activateSprinkler(p == 1);
}

BLYNK_WRITE(V4) {
  int p = param.asInt();
  activateAlert(p == 1);
}

BLYNK_WRITE(V5) {
  // Manual system reset
  activateAlert(false);
  activateSprinkler(false);
  Serial.println("Manual system reset via Blynk");
}

void loop() {
  Blynk.run();//Run the Blynk library
  timer.run();//Run the Blynk timer
  
  // Handle MQTT
  if (!mqtt_client.connected()) {
    reconnectMQTT();
  }
  mqtt_client.loop();
  
  // Auto-deactivate sprinkler after 60 seconds
  if (sprinkler_active && millis() - last_alert_time > 60000) {
    activateSprinkler(false);
    Serial.println("Auto-deactivating sprinkler after 60 seconds");
  }
  
  delay(10);
}
