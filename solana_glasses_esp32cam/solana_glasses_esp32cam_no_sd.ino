/*
  Solana Glasses - ESP32-CAM Voice Assistant (NO SD CARD VERSION)
  
  This is a temporary version that bypasses SD card functionality
  for initial testing and WiFi troubleshooting.
  
  Use this version to:
  1. Test WiFi connectivity
  2. Test HTTP server functionality
  3. Verify API connections work
  
  Once SD card issues are resolved, switch back to the main version.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// ================================
// CONFIGURATION SECTION
// ================================

// WiFi Credentials - UPDATE THESE!
const char* ssid = "YOUR_WIFI_SSID";           // Replace with your WiFi SSID
const char* password = "YOUR_WIFI_PASSWORD";   // Replace with your WiFi password

// API Keys
const char* deepgram_api_key = "YOUR_DEEPGRAM_API_KEY";  // Get from https://console.deepgram.com/
const char* openai_api_key = "YOUR_OPENAI_API_KEY";     // Get from https://platform.openai.com/

// API Endpoints
const char* deepgram_host = "api.deepgram.com";
const char* openai_host = "api.openai.com";

// ESP32-CAM Pin Definitions
#define LED_PIN 4           // Onboard LED
#define BUTTON_PIN 0        // Boot button

// System Configuration
#define DEBUG_MODE true     // Set to false for production
#define HTTP_TIMEOUT 30000  // 30 seconds timeout

// ================================
// GLOBAL VARIABLES
// ================================

WebServer server(80);
WiFiClientSecure client;

// System Status
bool wifi_connected = false;
bool sd_initialized = false;  // Always false in this version

// LED Status
enum LEDStatus {
  LED_OFF,
  LED_WIFI_CONNECTING,
  LED_READY,
  LED_PROCESSING,
  LED_ERROR
};

LEDStatus current_led_status = LED_OFF;
unsigned long led_last_update = 0;

// ================================
// SETUP FUNCTION
// ================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=================================");
  Serial.println("Solana Glasses ESP32-CAM (NO SD)");
  Serial.println("=================================");
  Serial.println("SD Card functionality disabled for testing");
  
  // Initialize hardware
  setupPins();
  setupWiFi();
  
  if (wifi_connected) {
    setupWebServer();
    Serial.println("System ready! (SD card disabled)");
    Serial.println("Test with Node.js audio emulator");
    Serial.println("=================================\n");
    setLEDStatus(LED_READY);
  } else {
    Serial.println("SETUP FAILED - Check WiFi credentials");
    setLEDStatus(LED_ERROR);
  }
}

// ================================
// MAIN LOOP
// ================================

void loop() {
  if (wifi_connected) {
    server.handleClient();
  }
  
  // Handle button press
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(50); // Debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      handleButtonPress();
      while (digitalRead(BUTTON_PIN) == LOW) delay(10);
    }
  }
  
  // Update LED status
  updateLED();
  
  delay(10);
}

// ================================
// HARDWARE SETUP FUNCTIONS
// ================================

void setupPins() {
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  digitalWrite(LED_PIN, LOW);
  
  if (DEBUG_MODE) Serial.println("[SETUP] GPIO pins initialized");
}

void setupWiFi() {
  setLEDStatus(LED_WIFI_CONNECTING);
  
  // Print WiFi debugging info
  Serial.println("[DEBUG] WiFi Connection Attempt");
  Serial.printf("[DEBUG] SSID: '%s'\n", ssid);
  Serial.printf("[DEBUG] Password length: %d characters\n", strlen(password));
  
  WiFi.begin(ssid, password);
  Serial.print("[SETUP] Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    Serial.printf(" [%d]", WiFi.status()); // Print status codes
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    Serial.println("\n[SETUP] WiFi connected successfully!");
    Serial.print("[INFO] IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[INFO] MAC address: ");
    Serial.println(WiFi.macAddress());
    Serial.printf("[INFO] Signal strength: %d dBm\n", WiFi.RSSI());
  } else {
    wifi_connected = false;
    Serial.println("\n[ERROR] WiFi connection failed!");
    Serial.printf("[ERROR] Final status: %d\n", WiFi.status());
    printWiFiDiagnostics();
    setLEDStatus(LED_ERROR);
  }
}

void printWiFiDiagnostics() {
  Serial.println("\n[DIAGNOSTICS] WiFi Troubleshooting:");
  Serial.println("================================");
  
  // Check WiFi status codes
  int status = WiFi.status();
  Serial.printf("Status Code: %d - ", status);
  
  switch(status) {
    case WL_NO_SHIELD: Serial.println("NO_SHIELD (WiFi not available)"); break;
    case WL_IDLE_STATUS: Serial.println("IDLE_STATUS (WiFi ready but not connected)"); break;
    case WL_NO_SSID_AVAIL: Serial.println("NO_SSID_AVAIL (Network not found)"); break;
    case WL_SCAN_COMPLETED: Serial.println("SCAN_COMPLETED"); break;
    case WL_CONNECTED: Serial.println("CONNECTED (Should not see this in error)"); break;
    case WL_CONNECT_FAILED: Serial.println("CONNECT_FAILED (Wrong password or auth issue)"); break;
    case WL_CONNECTION_LOST: Serial.println("CONNECTION_LOST"); break;
    case WL_DISCONNECTED: Serial.println("DISCONNECTED"); break;
    default: Serial.println("UNKNOWN STATUS"); break;
  }
  
  // Scan for available networks
  Serial.println("\n[SCAN] Available WiFi Networks:");
  int n = WiFi.scanNetworks();
  if (n == 0) {
    Serial.println("No networks found");
  } else {
    for (int i = 0; i < n; ++i) {
      Serial.printf("%d: %s (%d dBm) %s\n", 
                    i + 1, 
                    WiFi.SSID(i).c_str(), 
                    WiFi.RSSI(i),
                    (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Encrypted");
    }
  }
  
  Serial.println("\n[SOLUTIONS] Try these fixes:");
  Serial.println("1. Check SSID spelling (case-sensitive)");
  Serial.println("2. Check password (case-sensitive)");
  Serial.println("3. Ensure 2.4GHz network (ESP32 doesn't support 5GHz)");
  Serial.println("4. Move closer to router");
  Serial.println("5. Try a mobile hotspot for testing");
  Serial.println("================================\n");
}

void setupWebServer() {
  if (!wifi_connected) return;
  
  // Handle audio upload (simplified - no SD card)
  server.on("/upload-audio", HTTP_POST, handleAudioUpload, handleAudioUploadFile);
  
  // Handle status requests
  server.on("/status", HTTP_GET, handleStatus);
  
  // Handle root page
  server.on("/", HTTP_GET, handleRoot);
  
  // Handle not found
  server.onNotFound(handleNotFound);
  
  server.begin();
  Serial.println("[SETUP] HTTP server started on port 80");
}

// ================================
// WEB SERVER HANDLERS
// ================================

void handleRoot() {
  String html = R"(
    <html>
    <head><title>Solana Glasses ESP32-CAM (No SD)</title></head>
    <body>
      <h1>Solana Glasses Voice Assistant</h1>
      <h2>TESTING MODE - SD Card Disabled</h2>
      <h2>System Status</h2>
      <p>WiFi: Connected ✅</p>
      <p>SD Card: DISABLED for testing ⚠️</p>
      <p>IP Address: )" + WiFi.localIP().toString() + R"(</p>
      <p>Signal: )" + String(WiFi.RSSI()) + R"( dBm</p>
      <p>Free Heap: )" + String(ESP.getFreeHeap()) + R"( bytes</p>
      <h2>Usage</h2>
      <p>This is a test version without SD card functionality</p>
      <p>Use this to verify WiFi and HTTP server work</p>
      <p>Once working, fix SD card and use the full version</p>
    </body>
    </html>
  )";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  DynamicJsonDocument status(1024);
  
  status["wifi_connected"] = wifi_connected;
  status["sd_initialized"] = false;  // Always false
  status["recording_active"] = false;
  status["free_heap"] = ESP.getFreeHeap();
  status["uptime"] = millis();
  status["ip_address"] = WiFi.localIP().toString();
  status["signal_strength"] = WiFi.RSSI();
  status["test_mode"] = true;
  status["message"] = "SD card disabled for testing";
  
  String response;
  serializeJson(status, response);
  
  server.send(200, "application/json", response);
}

void handleAudioUpload() {
  Serial.println("[TEST] Audio upload endpoint called");
  server.send(200, "application/json", "{\"status\":\"received\",\"message\":\"SD card disabled - testing only\"}");
}

void handleAudioUploadFile() {
  setLEDStatus(LED_PROCESSING);
  
  HTTPUpload& upload = server.upload();
  static size_t totalSize = 0;
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("[TEST] Started receiving audio data");
    totalSize = 0;
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    totalSize += upload.currentSize;
    
  } else if (upload.status == UPLOAD_FILE_END) {
    Serial.printf("[TEST] Audio received: %d bytes\n", totalSize);
    Serial.println("[TEST] In full version, this would be saved to SD card");
    Serial.println("[TEST] APIs would process the audio and return response");
    
    // Simulate successful processing
    Serial.println("\n" + String("=").substring(0, 50));
    Serial.println("🤖 SOLANA GLASSES TEST RESPONSE:");
    Serial.println("=================================");
    Serial.println("SD card disabled - this is a test version.");
    Serial.println("Fix SD card issues and use the full version.");
    Serial.println("WiFi and HTTP server are working correctly!");
    Serial.println("=================================\n");
    
    setLEDStatus(LED_READY);
  }
}

void handleNotFound() {
  server.send(404, "text/plain", "Endpoint not found. Try / or /status");
}

void handleButtonPress() {
  Serial.println("[USER] Button pressed - System status:");
  printSystemStatus();
}

// ================================
// STATUS FUNCTIONS
// ================================

void printSystemStatus() {
  Serial.println("\n📊 SYSTEM STATUS (TEST MODE):");
  Serial.println("==================");
  Serial.printf("WiFi: %s\n", wifi_connected ? "✅ Connected" : "❌ Disconnected");
  Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
  Serial.printf("SD Card: ❌ DISABLED for testing\n");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Uptime: %lu ms\n", millis());
  Serial.println("==================\n");
}

// ================================
// LED STATUS FUNCTIONS
// ================================

void setLEDStatus(LEDStatus status) {
  current_led_status = status;
  led_last_update = millis();
}

void updateLED() {
  unsigned long now = millis();
  
  switch (current_led_status) {
    case LED_OFF:
      digitalWrite(LED_PIN, LOW);
      break;
      
    case LED_WIFI_CONNECTING:
      // Slow blink while connecting
      digitalWrite(LED_PIN, (now / 500) % 2);
      break;
      
    case LED_READY:
      // Steady on when ready
      digitalWrite(LED_PIN, HIGH);
      break;
      
    case LED_PROCESSING:
      // Fast blink during processing
      digitalWrite(LED_PIN, (now / 200) % 2);
      break;
      
    case LED_ERROR:
      // Very fast blink for errors
      digitalWrite(LED_PIN, (now / 100) % 2);
      break;
  }
} 