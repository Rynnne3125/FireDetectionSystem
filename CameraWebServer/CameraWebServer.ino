
#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>


// ===================
// Select camera model
// ===================
//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
//#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
//#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD
//#define CAMERA_MODEL_DFRobot_FireBeetle2_ESP32S3 // Has PSRAM
//#define CAMERA_MODEL_DFRobot_Romeo_ESP32S3 // Has PSRAM
#include "camera_pins.h"

// ===========================
// Enter your WiFi credentials
// ===========================
const char* ssid = "Phong";
const char* password = "rintran3125";
void startCameraServer();
void setupLedFlash(int pin);

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  //config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;
  
  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if(config.pixel_format == PIXFORMAT_JPEG){
    if(psramFound()){
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); // flip it back
    s->set_brightness(s, 1); // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if(config.pixel_format == PIXFORMAT_JPEG){
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash(LED_GPIO_NUM);
#endif

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

// --- Thông tin server cloud ---
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

const char* cloud_server = "http://172.19.0.83/upload"; // Địa chỉ server cloud thực tế
const char* status_server = "http://172.19.0.83/status";

// --- System State ---
int alertLevel = 0;  // 0: Normal, 1: Warning, 2: Critical
bool fireDetected = false;
unsigned long lastFireTime = 0;

void sendImageToCloud() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("❌ Camera capture failed");
    return;
  }
  
  if ((WiFi.status() == WL_CONNECTED)) {
    HTTPClient http;
    http.begin(cloud_server);
    http.addHeader("Content-Type", "multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW");
    
    // Create multipart form data with proper image data
    String body = "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";
    body += "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n";
    body += "Content-Type: image/jpeg\r\n\r\n";
    
    // Add image data
    String imageData = "";
    for (size_t i = 0; i < fb->len; i++) {
      imageData += (char)fb->buf[i];
    }
    body += imageData;
    body += "\r\n";
    
    // Add sensor data if available (simulated for now)
    if (alertLevel > 0) {
      body += "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\n";
      body += "Content-Disposition: form-data; name=\"sensor_data\"\r\n\r\n";
      body += "{\"smoke\":500,\"temperature\":45,\"humidity\":60}\r\n";
    }
    
    body += "\r\n------WebKitFormBoundary7MA4YWxkTrZu0gW--\r\n";
    
    Serial.printf("📸 Sending image to cloud server...\n");
    Serial.printf("📊 Image size: %d bytes\n", fb->len);
    Serial.printf("📊 Body size: %d bytes\n", body.length());
    
    // Send request
    int httpCode = http.POST(body);
    
    if (httpCode > 0) {
      String response = http.getString();
      Serial.printf("✅ [HTTP] POST... code: %d\n", httpCode);
      Serial.printf("📄 Response: %s\n", response.c_str());
      processAIResponse(response);
    } else {
      Serial.printf("❌ [HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
  } else {
    Serial.println("❌ WiFi not connected");
  }
  
  esp_camera_fb_return(fb);
}

void processAIResponse(String response) {
  Serial.printf("🤖 Processing AI response: %s\n", response.c_str());
  
  // Parse JSON response
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, response);
  
  if (error) {
    Serial.print("❌ JSON parsing failed: ");
    Serial.println(error.c_str());
    return;
  }
  
  bool fireDetected = doc["fire_detected"];
  float confidence = doc["confidence"];
  
  if (fireDetected) {
    ::fireDetected = true;
    lastFireTime = millis();
    alertLevel = 2; // Critical
    
    Serial.printf("🔥 FIRE DETECTED! Confidence: %.2f\n", confidence);
    
    // Flash LED to indicate fire
    setLedFlash(true);
    delay(100);
    setLedFlash(false);
    
    // Get fire position if available
    if (doc.containsKey("fire_position")) {
      int x = doc["fire_position"][0];
      int y = doc["fire_position"][1];
      Serial.printf("📍 Fire position: (%d, %d)\n", x, y);
    }
    
    // Get camera angles if available
    if (doc.containsKey("camera_angles")) {
      float angleX = doc["camera_angles"]["x"];
      float angleY = doc["camera_angles"]["y"];
      Serial.printf("📐 Camera angles: X=%.1f°, Y=%.1f°\n", angleX, angleY);
    }
  } else {
    Serial.printf("✅ No fire detected. Confidence: %.2f\n", confidence);
    
    // Gradually reduce alert level
    if (alertLevel > 0 && millis() - lastFireTime > 30000) { // 30 seconds
      alertLevel = max(0, alertLevel - 1);
      ::fireDetected = false;
      Serial.println("🔄 Alert level reduced");
    }
  }
}

unsigned long lastSend = 0;
const unsigned long sendInterval = 5000; // gửi ảnh mỗi 5 giây

// Định nghĩa đơn giản: chỉ khởi tạo pin LED flash như OUTPUT và tắt (không dùng LEDC/PWM)
void setupLedFlash(int pin) {
  if (pin >= 0) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW); // ban đầu tắt đèn
  }
}

// Hàm tiện ích để bật/tắt LED flash sau này (sử dụng digitalWrite)
void setLedFlash(bool on) {
#if defined(LED_GPIO_NUM)
  digitalWrite(LED_GPIO_NUM, on ? HIGH : LOW);
#endif
}

void loop() {
  if (millis() - lastSend > sendInterval) {
    sendImageToCloud();
    lastSend = millis();
  }
  delay(100);
}
