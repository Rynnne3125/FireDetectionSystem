#define BLYNK_TEMPLATE_ID "TMPL6HhxqRjpz"
#define BLYNK_TEMPLATE_NAME "esp8266controlstep"

#define BLYNK_PRINT Serial
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <AccelStepper.h>
#include <SimpleKalmanFilter.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

SimpleKalmanFilter simpleKalmanFilter(2, 2, 0.01);
int step_X = D5; int Dir_X = D3; int ena_X= D2;
int step_Y = D6; int Dir_Y = D7; int ena_Y= D8;
int sprinkler_pin = D1;  // Vòi phun nước
int alarm_pin = D4;      // Còi báo động

AccelStepper Step_X(1, step_X, Dir_X, ena_X);
AccelStepper Step_Y(1, step_Y, Dir_Y, ena_Y);

// System State
int current_X = 0;
int current_Y = 0;
int target_X = 0;
int target_Y = 0;
bool fire_detected = false;
bool sprinkler_active = false;
bool alarm_active = false;
unsigned long last_fire_time = 0;
unsigned long last_mqtt_check = 0;

char auth[] = "9sMCzS_1q6_ObxpbRouYhPi7ZQAhKxHc";//Enter your Auth token
char ssid[] = "Loanlun_5G";//Enter your WIFI name
char pass[] = "loanlunsociu299";//Enter your WIFI password

// MQTT Configuration
const char* mqtt_server = "192.168.2.61";
const int mqtt_port = 1883;
const char* mqtt_topic_command = "fire/command";
const char* mqtt_topic_status = "fire/status";

// MQTT Client
WiFiClient espClient;
PubSubClient mqtt_client(espClient);

void setup()
{
  Serial.begin(9600);
  Serial.println("=== Enhanced Fire Detection Controller ===");

  // Initialize pins
  pinMode(step_X,OUTPUT);
  pinMode(sprinkler_pin, OUTPUT);
  pinMode(alarm_pin, OUTPUT);
  
  // Initialize outputs
  digitalWrite(sprinkler_pin, LOW);
  digitalWrite(alarm_pin, LOW);

  // Configure stepper motors
  Step_X.setMaxSpeed(1000);
  Step_X.setAcceleration(1000);
  Step_Y.setMaxSpeed(1000);
  Step_Y.setAcceleration(1000);

  // Connect to WiFi and Blynk
  Blynk.begin(auth, ssid, pass, "blynk.cloud", 80);
  
  // Setup MQTT
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  
  Serial.println("System initialized successfully");
}
// MQTT Functions
void reconnectMQTT() {
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    if (mqtt_client.connect("FireController")) {
      Serial.println("connected");
      mqtt_client.subscribe(mqtt_topic_command);
      mqtt_client.subscribe(mqtt_topic_status);
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
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  String command = doc["command"];
  JsonObject data = doc["data"];

  if (command == "FIRE_DETECTED") {
    processFireCommand(data);
  } else if (command == "MOVE_CAMERA") {
    processCameraCommand(data);
  } else if (command == "ACTIVATE_SPRINKLER") {
    processSprinklerCommand(data);
  } else if (command == "RESET_SYSTEM") {
    fire_detected = false;
    activateSprinkler(false);
    activateAlarm(false);
    Serial.println("System reset");
  }
}

void processFireCommand(JsonObject& data) {
  fire_detected = true;
  last_fire_time = millis();
  
  // Activate alarm
  activateAlarm(true);
  
  // Get fire position and move camera
  if (data.containsKey("position")) {
    int fire_x = data["position"][0];
    int fire_y = data["position"][1];
    
    // Convert pixel coordinates to motor steps
    target_X = map(fire_x, 0, 640, -100, 100);  // Assuming 640x480 image
    target_Y = map(fire_y, 0, 480, -100, 100);
    
    moveToPosition(target_X, target_Y);
    
    Serial.printf("Fire detected at (%d, %d) - Moving to (%d, %d)\n", 
                  fire_x, fire_y, target_X, target_Y);
  }
  
  // Activate sprinkler after a short delay
  delay(2000);  // 2 second delay to position camera
  activateSprinkler(true);
}

void processCameraCommand(JsonObject& data) {
  if (data.containsKey("angles")) {
    float angle_x = data["angles"]["x"];
    float angle_y = data["angles"]["y"];
    
    // Convert angles to motor steps
    target_X = map(angle_x, -30, 30, -100, 100);  // Assuming ±30 degree range
    target_Y = map(angle_y, -30, 30, -100, 100);
    
    moveToPosition(target_X, target_Y);
    
    Serial.printf("Moving camera to angles: X=%.1f, Y=%.1f\n", angle_x, angle_y);
  }
}

void processSprinklerCommand(JsonObject& data) {
  bool activate = data.containsKey("position") || data.containsKey("angles");
  activateSprinkler(activate);
  
  if (activate) {
    Serial.println("Sprinkler activated");
  } else {
    Serial.println("Sprinkler deactivated");
  }
}

void moveToPosition(int x, int y) {
  // Apply Kalman filter for smooth movement
  int filtered_x = simpleKalmanFilter.updateEstimate(x);
  int filtered_y = simpleKalmanFilter.updateEstimate(y);
  
  // Move stepper motors
  Step_X.moveTo(filtered_x);
  Step_Y.moveTo(filtered_y);
  
  current_X = filtered_x;
  current_Y = filtered_y;
  
  Serial.printf("Moving to position: X=%d, Y=%d\n", filtered_x, filtered_y);
}

void activateSprinkler(bool activate) {
  sprinkler_active = activate;
  digitalWrite(sprinkler_pin, activate ? HIGH : LOW);
  
  // Update Blynk
  Blynk.virtualWrite(V4, activate ? 1 : 0);
}

void activateAlarm(bool activate) {
  alarm_active = activate;
  digitalWrite(alarm_pin, activate ? HIGH : LOW);
  
  // Update Blynk
  Blynk.virtualWrite(V5, activate ? 1 : 0);
}

void sendStatusUpdate() {
  if (!mqtt_client.connected()) {
    return;
  }
  
  DynamicJsonDocument doc(512);
  doc["timestamp"] = millis();
  doc["fire_detected"] = fire_detected;
  doc["sprinkler_active"] = sprinkler_active;
  doc["alarm_active"] = alarm_active;
  
  // Create position object properly
  JsonObject position = doc.createNestedObject("position");
  position["x"] = current_X;
  position["y"] = current_Y;
  
  String status_message;
  serializeJson(doc, status_message);
  
  mqtt_client.publish(mqtt_topic_status, status_message.c_str());
}

// Blynk Virtual Pin Handlers
BLYNK_WRITE(V2) {
  int value = param.asInt();
  int filtered_value = simpleKalmanFilter.updateEstimate(value);
  int mapped_value = map(filtered_value, 0, 200, 0, 360);
  int steps = map(mapped_value, 0, 360, 0, 200);

  if (mapped_value != current_Y) {
    Step_Y.moveTo(steps);
    current_Y = mapped_value;
  }
  Step_Y.run();
}

BLYNK_WRITE(V3) {
  int value = param.asInt();
  int filtered_value = simpleKalmanFilter.updateEstimate(value);
  int mapped_value = map(filtered_value, 100, 100, 0, 360);
  int steps = map(mapped_value, 0, 360, 0, 100);

  if (mapped_value != current_X) {
    Step_X.moveTo(steps);
    current_X = mapped_value;
  }
  Step_X.run();
}

BLYNK_WRITE(V4) {
  int value = param.asInt();
  activateSprinkler(value == 1);
}

BLYNK_WRITE(V5) {
  int value = param.asInt();
  activateAlarm(value == 1);
}

void loop() {
  Blynk.run();
  
  // Handle MQTT
  if (!mqtt_client.connected()) {
    reconnectMQTT();
  }
  mqtt_client.loop();
  
  // Run stepper motors
  Step_X.run();
  Step_Y.run();
  
  // Auto-deactivate sprinkler after 30 seconds
  if (sprinkler_active && millis() - last_fire_time > 30000) {
    activateSprinkler(false);
    Serial.println("Auto-deactivating sprinkler after 30 seconds");
  }
  
  // Auto-deactivate alarm after 60 seconds
  if (alarm_active && millis() - last_fire_time > 60000) {
    activateAlarm(false);
    Serial.println("Auto-deactivating alarm after 60 seconds");
  }
  
  // Send status update every 10 seconds
  if (millis() - last_mqtt_check > 10000) {
    sendStatusUpdate();
    last_mqtt_check = millis();
  }
  
  delay(10);
}