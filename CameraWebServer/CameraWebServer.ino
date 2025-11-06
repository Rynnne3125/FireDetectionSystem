#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <base64.h>
#include "esp_camera.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ===== WIFI & MQTT CONFIG =====
const char* ssid = "Phong";
const char* password = "rintran3125";
const char* mqtt_server = "10.120.93.83";
const int mqtt_port = 1883;
const char* cloud_server = "http://10.120.93.83:5000/upload";

// MQTT Topics
const char* topic_camera = "fire/camera";
const char* topic_command = "fire/command";

WiFiClient espClient;
PubSubClient mqtt(espClient);
HTTPClient http;

// ===== CAMERA PINS (AI-THINKER ESP32-CAM) =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ===== SYSTEM STATE =====
bool alertMode = false;
unsigned long lastCapture = 0;
unsigned long captureInterval = 5000;  // 5 giây bình thường
const unsigned long alertCaptureInterval = 1000;  // 1 giây khi có cảnh báo

// ===== FUNCTION PROTOTYPES =====
void setupCamera();
void setupWiFi();
void setupMQTT();
void reconnectMQTT();
void captureAndSend();
void sendImageHTTP(camera_fb_t* fb);
void sendImageMQTT(camera_fb_t* fb);
void mqttCallback(char* topic, byte* payload, unsigned int length);

void setup() {
  Serial.begin(115200);
  Serial.println("\n🔥 ESP32-CAM Fire Detection System");
  
  // Tắt brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  // Setup camera
  setupCamera();
  
  // Setup WiFi
  setupWiFi();
  
  // Setup MQTT
  setupMQTT();
  
  Serial.println("✅ System ready!");
}

void loop() {
  // Duy trì kết nối MQTT
  if (!mqtt.connected()) {
    reconnectMQTT();
  }
  mqtt.loop();
  
  // Chụp và gửi ảnh theo interval
  unsigned long currentMillis = millis();
  unsigned long interval = alertMode ? alertCaptureInterval : captureInterval;
  
  if (currentMillis - lastCapture >= interval) {
    lastCapture = currentMillis;
    captureAndSend();
  }
  
  delay(10);
}

// ===== SETUP CAMERA =====
void setupCamera() {
  Serial.println("📷 Configuring camera...");
  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Cấu hình chất lượng ảnh
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 10;  // 0-63, thấp hơn = chất lượng cao hơn
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Khởi tạo camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ Camera init failed: 0x%x\n", err);
    delay(1000);
    ESP.restart();
  }
  
  // Điều chỉnh sensor
  sensor_t* s = esp_camera_sensor_get();
  s->set_brightness(s, 0);     // -2 to 2
  s->set_contrast(s, 0);       // -2 to 2
  s->set_saturation(s, 0);     // -2 to 2
  s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect)
  s->set_whitebal(s, 1);       // 0 = disable , 1 = enable
  s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
  s->set_wb_mode(s, 0);        // 0 to 4
  s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
  s->set_aec2(s, 0);           // 0 = disable , 1 = enable
  s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
  s->set_agc_gain(s, 0);       // 0 to 30
  s->set_gainceiling(s, (gainceiling_t)0);  // 0 to 6
  s->set_bpc(s, 0);            // 0 = disable , 1 = enable
  s->set_wpc(s, 1);            // 0 = disable , 1 = enable
  s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
  s->set_lenc(s, 1);           // 0 = disable , 1 = enable
  s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
  s->set_vflip(s, 0);          // 0 = disable , 1 = enable
  
  Serial.println("✅ Camera configured successfully");
}

// ===== SETUP WIFI =====
void setupWiFi() {
  Serial.print("📡 Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WiFi connection failed!");
    delay(3000);
    ESP.restart();
  }
}

// ===== SETUP MQTT =====
void setupMQTT() {
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(30000);  // Tăng buffer cho ảnh lớn
}

// ===== RECONNECT MQTT =====
void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("🔄 Connecting to MQTT...");
    
    String clientId = "ESP32CAM-" + String(random(0xffff), HEX);
    
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("✅ MQTT connected!");
      mqtt.subscribe(topic_command);
      Serial.printf("📡 Subscribed to: %s\n", topic_command);
    } else {
      Serial.printf("❌ Failed, rc=%d. Retry in 5s\n", mqtt.state());
      delay(5000);
    }
  }
}

// ===== MQTT CALLBACK =====
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.printf("📨 Message from: %s\n", topic);
  
  // Parse JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    Serial.println("❌ JSON parsing failed");
    return;
  }
  
  const char* command = doc["command"];
  
  if (strcmp(command, "INCREASE_CAPTURE_RATE") == 0) {
    Serial.println("⚠️ Alert mode activated - increasing capture rate");
    alertMode = true;
  } 
  else if (strcmp(command, "RESET_SYSTEM") == 0) {
    Serial.println("🔄 Reset command received");
    alertMode = false;
    captureInterval = 5000;
  }
}

// ===== CAPTURE AND SEND IMAGE =====
void captureAndSend() {
  Serial.println("📸 Capturing image...");
  
  // Chụp ảnh
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Camera capture failed");
    return;
  }
  
  Serial.printf("✅ Image captured: %d bytes\n", fb->len);
  
  // Gửi qua HTTP (ưu tiên vì ổn định hơn với ảnh lớn)
  sendImageHTTP(fb);
  
  // Hoặc gửi qua MQTT (dùng cho ảnh nhỏ)
  // sendImageMQTT(fb);
  
  // Giải phóng buffer
  esp_camera_fb_return(fb);
}

// ===== SEND IMAGE VIA HTTP =====
void sendImageHTTP(camera_fb_t* fb) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected");
    return;
  }
  
  // Encode base64
  String base64Image = base64::encode(fb->buf, fb->len);
  
  // Tạo JSON
  StaticJsonDocument<50000> doc;
  doc["image"] = base64Image;
  doc["timestamp"] = millis();
  doc["source"] = "ESP32-CAM";
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Gửi HTTP POST
  http.begin(cloud_server);
  http.addHeader("Content-Type", "application/json");
  
  Serial.println("📤 Sending image to cloud...");
  int httpCode = http.POST(jsonString);
  
  if (httpCode > 0) {
    Serial.printf("✅ HTTP Response: %d\n", httpCode);
    
    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      Serial.println("Response: " + response);
      
      // Parse response để biết có lửa không
      StaticJsonDocument<256> responseDoc;
      deserializeJson(responseDoc, response);
      bool fireDetected = responseDoc["fire_detected"];
      
      if (fireDetected) {
        Serial.println("🔥 FIRE DETECTED BY AI!");
        alertMode = true;
      }
    }
  } else {
    Serial.printf("❌ HTTP Error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
}

// ===== SEND IMAGE VIA MQTT (Alternative) =====
void sendImageMQTT(camera_fb_t* fb) {
  // Encode base64
  String base64Image = base64::encode(fb->buf, fb->len);
  
  // Tạo JSON
  StaticJsonDocument<50000> doc;
  doc["image"] = base64Image;
  doc["timestamp"] = millis();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Gửi MQTT
  Serial.println("📤 Sending image via MQTT...");
  bool sent = mqtt.publish(topic_camera, jsonString.c_str(), false);
  
  if (sent) {
    Serial.println("✅ Image sent successfully");
  } else {
    Serial.println("❌ MQTT publish failed");
  }
}