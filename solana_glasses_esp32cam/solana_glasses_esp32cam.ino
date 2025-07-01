/*
  Solana Glasses - ESP32-CAM Voice Assistant
  
  Smart glasses voice assistant demo using ESP32-CAM with:
  - HTTP server to receive audio from Node.js app
  - Deepgram Speech-to-Text API integration
  - OpenAI Chat Completions API integration
  - SD card audio file handling
  - Conversation history management
  
  Hardware: ESP32-CAM with built-in SD card reader
  Audio: Received via HTTP from Node.js emulator (no INMP441 needed)
  
  GPIO Configuration:
  - GPIO 4: Onboard LED (status indicator)
  - GPIO 0: Boot button (manual trigger)
  - SD Card: Built-in pins (GPIO 2, 4, 12, 13, 14, 15)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <FS.h>
#include <SPI.h>

// ================================
// CONFIGURATION SECTION
// ================================

// WiFi Credentials
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
#define SD_CS_PIN 2         // SD card chip select (built-in)

// Audio Configuration
#define SAMPLE_RATE 16000   // 16kHz
#define AUDIO_FORMAT "wav"
#define MAX_AUDIO_SIZE 1024000  // 1MB max audio file

// System Configuration
#define DEBUG_MODE true     // Set to false for production
#define MAX_HISTORY 10      // Maximum conversation history entries
#define HTTP_TIMEOUT 30000  // 30 seconds timeout

// ================================
// GLOBAL VARIABLES
// ================================

WebServer server(80);
WiFiClientSecure client;

// Conversation History
DynamicJsonDocument conversation_history(8192);
int history_count = 0;

// System Status
bool wifi_connected = false;
bool sd_initialized = false;
bool recording_active = false;
unsigned long last_activity = 0;

// LED Status
enum LEDStatus {
  LED_OFF,
  LED_WIFI_CONNECTING,
  LED_READY,
  LED_RECORDING,
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
  Serial.println("Solana Glasses ESP32-CAM Starting");
  Serial.println("=================================");
  
  // Initialize hardware
  setupPins();
  setupSD();
  setupWiFi();
  setupWebServer();
  
  // Initialize conversation
  initializeConversation();
  
  Serial.println("System ready! Waiting for audio...");
  Serial.println("Send audio via HTTP POST to /upload-audio");
  Serial.println("=================================\n");
  
  setLEDStatus(LED_READY);
}

// ================================
// MAIN LOOP
// ================================

void loop() {
  server.handleClient();
  
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
  
  // Periodic status update
  if (millis() - last_activity > 60000) { // Every minute
    printSystemStatus();
    last_activity = millis();
  }
  
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

void setupSD() {
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("[ERROR] SD Card initialization failed!");
    setLEDStatus(LED_ERROR);
    sd_initialized = false;
    return;
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[ERROR] No SD card attached");
    setLEDStatus(LED_ERROR);
    sd_initialized = false;
    return;
  }
  
  sd_initialized = true;
  Serial.println("[SETUP] SD Card initialized successfully");
  
  // Print card info
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("[INFO] SD Card Size: %lluMB\n", cardSize);
}

void setupWiFi() {
  setLEDStatus(LED_WIFI_CONNECTING);
  
  WiFi.begin(ssid, password);
  Serial.print("[SETUP] Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    Serial.println("\n[SETUP] WiFi connected successfully!");
    Serial.print("[INFO] IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[INFO] MAC address: ");
    Serial.println(WiFi.macAddress());
  } else {
    wifi_connected = false;
    Serial.println("\n[ERROR] WiFi connection failed!");
    setLEDStatus(LED_ERROR);
  }
}

void setupWebServer() {
  if (!wifi_connected) return;
  
  // Handle audio upload
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
    <head><title>Solana Glasses ESP32-CAM</title></head>
    <body>
      <h1>Solana Glasses Voice Assistant</h1>
      <h2>System Status</h2>
      <p>WiFi: Connected</p>
      <p>SD Card: )" + String(sd_initialized ? "Ready" : "Error") + R"(</p>
      <p>IP Address: )" + WiFi.localIP().toString() + R"(</p>
      <p>Free Heap: )" + String(ESP.getFreeHeap()) + R"( bytes</p>
      <h2>Usage</h2>
      <p>Send audio files via POST to <code>/upload-audio</code></p>
      <p>Check status via GET to <code>/status</code></p>
    </body>
    </html>
  )";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  DynamicJsonDocument status(1024);
  
  status["wifi_connected"] = wifi_connected;
  status["sd_initialized"] = sd_initialized;
  status["recording_active"] = recording_active;
  status["free_heap"] = ESP.getFreeHeap();
  status["uptime"] = millis();
  status["ip_address"] = WiFi.localIP().toString();
  status["conversation_history_count"] = history_count;
  
  String response;
  serializeJson(status, response);
  
  server.send(200, "application/json", response);
}

void handleAudioUpload() {
  if (!sd_initialized) {
    server.send(500, "application/json", "{\"error\":\"SD card not initialized\"}");
    return;
  }
  
  server.send(200, "application/json", "{\"status\":\"received\"}");
}

void handleAudioUploadFile() {
  setLEDStatus(LED_RECORDING);
  
  HTTPUpload& upload = server.upload();
  static File audioFile;
  static String filename;
  
  if (upload.status == UPLOAD_FILE_START) {
    // Generate unique filename
    filename = "/audio_" + String(millis()) + ".wav";
    audioFile = SD.open(filename, FILE_WRITE);
    
    if (!audioFile) {
      Serial.println("[ERROR] Failed to create audio file");
      setLEDStatus(LED_ERROR);
      return;
    }
    
    Serial.println("[INFO] Started receiving audio: " + filename);
    recording_active = true;
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (audioFile) {
      audioFile.write(upload.buf, upload.currentSize);
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    if (audioFile) {
      audioFile.close();
      recording_active = false;
      
      Serial.printf("[INFO] Audio saved: %s (%d bytes)\n", filename.c_str(), upload.totalSize);
      
      // Process the audio file
      processAudioFile(filename);
    }
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
// AUDIO PROCESSING FUNCTIONS
// ================================

void processAudioFile(String filename) {
  setLEDStatus(LED_PROCESSING);
  
  Serial.println("[PROCESS] Starting audio processing pipeline...");
  
  // Step 1: Validate audio file
  if (!validateAudioFile(filename)) {
    Serial.println("[ERROR] Audio file validation failed");
    setLEDStatus(LED_ERROR);
    return;
  }
  
  // Step 2: Convert speech to text
  String transcription = speechToText(filename);
  if (transcription.length() == 0) {
    Serial.println("[ERROR] Speech-to-text failed");
    setLEDStatus(LED_ERROR);
    return;
  }
  
  Serial.println("[STT] Transcription: " + transcription);
  
  // Step 3: Get AI response
  String ai_response = getAIResponse(transcription);
  if (ai_response.length() == 0) {
    Serial.println("[ERROR] AI response failed");
    setLEDStatus(LED_ERROR);
    return;
  }
  
  // Step 4: Display response
  displayAIResponse(ai_response);
  
  // Step 5: Update conversation history
  updateConversationHistory(transcription, ai_response);
  
  // Clean up audio file
  SD.remove(filename);
  
  setLEDStatus(LED_READY);
  Serial.println("[PROCESS] Audio processing complete\n");
}

bool validateAudioFile(String filename) {
  File audioFile = SD.open(filename, FILE_READ);
  if (!audioFile) {
    return false;
  }
  
  size_t fileSize = audioFile.size();
  audioFile.close();
  
  if (fileSize < 44) { // Minimum WAV header size
    Serial.println("[ERROR] Audio file too small (no WAV header)");
    return false;
  }
  
  if (fileSize > MAX_AUDIO_SIZE) {
    Serial.println("[ERROR] Audio file too large");
    return false;
  }
  
  if (DEBUG_MODE) {
    Serial.printf("[DEBUG] Audio file validated: %d bytes\n", fileSize);
  }
  
  return true;
}

// ================================
// DEEPGRAM STT INTEGRATION
// ================================

String speechToText(String filename) {
  if (!wifi_connected) {
    Serial.println("[ERROR] WiFi not connected for STT");
    return "";
  }
  
  Serial.println("[STT] Connecting to Deepgram API...");
  
  client.setInsecure(); // For demo purposes - use proper certificates in production
  
  if (!client.connect(deepgram_host, 443)) {
    Serial.println("[ERROR] Failed to connect to Deepgram");
    return "";
  }
  
  // Read audio file
  File audioFile = SD.open(filename, FILE_READ);
  if (!audioFile) {
    Serial.println("[ERROR] Cannot open audio file for STT");
    client.stop();
    return "";
  }
  
  size_t contentLength = audioFile.size();
  
  // Prepare HTTP request
  String request = "POST /v1/listen?model=nova-2&smart_format=true HTTP/1.1\r\n";
  request += "Host: " + String(deepgram_host) + "\r\n";
  request += "Authorization: Token " + String(deepgram_api_key) + "\r\n";
  request += "Content-Type: audio/wav\r\n";
  request += "Content-Length: " + String(contentLength) + "\r\n";
  request += "Connection: close\r\n\r\n";
  
  client.print(request);
  
  // Send audio data
  uint8_t buffer[1024];
  while (audioFile.available()) {
    size_t bytesRead = audioFile.read(buffer, sizeof(buffer));
    client.write(buffer, bytesRead);
  }
  audioFile.close();
  
  // Read response
  String response = "";
  unsigned long timeout = millis() + HTTP_TIMEOUT;
  
  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      response += client.readString();
      break;
    }
    delay(10);
  }
  
  client.stop();
  
  if (response.length() == 0) {
    Serial.println("[ERROR] Empty response from Deepgram");
    return "";
  }
  
  // Parse JSON response
  int jsonStart = response.indexOf("\r\n\r\n") + 4;
  if (jsonStart < 4) {
    Serial.println("[ERROR] Invalid HTTP response format");
    return "";
  }
  
  String jsonResponse = response.substring(jsonStart);
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, jsonResponse);
  
  if (error) {
    Serial.println("[ERROR] Failed to parse Deepgram JSON response");
    if (DEBUG_MODE) Serial.println("Response: " + jsonResponse);
    return "";
  }
  
  // Extract transcription
  if (doc["results"]["channels"][0]["alternatives"].size() > 0) {
    String transcript = doc["results"]["channels"][0]["alternatives"][0]["transcript"];
    return transcript;
  }
  
  Serial.println("[ERROR] No transcript found in Deepgram response");
  return "";
}

// ================================
// OPENAI CHAT INTEGRATION
// ================================

String getAIResponse(String user_message) {
  if (!wifi_connected) {
    Serial.println("[ERROR] WiFi not connected for AI");
    return "";
  }
  
  Serial.println("[AI] Connecting to OpenAI API...");
  
  client.setInsecure(); // For demo purposes - use proper certificates in production
  
  if (!client.connect(openai_host, 443)) {
    Serial.println("[ERROR] Failed to connect to OpenAI");
    return "";
  }
  
  // Prepare chat messages
  DynamicJsonDocument request_doc(4096);
  request_doc["model"] = "gpt-3.5-turbo";
  request_doc["max_tokens"] = 150;
  request_doc["temperature"] = 0.7;
  
  JsonArray messages = request_doc.createNestedArray("messages");
  
  // System message
  JsonObject system_msg = messages.createNestedObject();
  system_msg["role"] = "system";
  system_msg["content"] = "You are a helpful voice assistant for smart glasses called Solana Glasses. Keep responses concise and conversational, suitable for voice output. Limit responses to 2-3 sentences.";
  
  // Add conversation history
  JsonArray history = conversation_history["messages"];
  for (JsonVariant msg : history) {
    messages.add(msg);
  }
  
  // Add current user message
  JsonObject user_msg = messages.createNestedObject();
  user_msg["role"] = "user";
  user_msg["content"] = user_message;
  
  String request_body;
  serializeJson(request_doc, request_body);
  
  // Prepare HTTP request
  String request = "POST /v1/chat/completions HTTP/1.1\r\n";
  request += "Host: " + String(openai_host) + "\r\n";
  request += "Authorization: Bearer " + String(openai_api_key) + "\r\n";
  request += "Content-Type: application/json\r\n";
  request += "Content-Length: " + String(request_body.length()) + "\r\n";
  request += "Connection: close\r\n\r\n";
  request += request_body;
  
  client.print(request);
  
  // Read response
  String response = "";
  unsigned long timeout = millis() + HTTP_TIMEOUT;
  
  while (client.connected() && millis() < timeout) {
    if (client.available()) {
      response += client.readString();
      break;
    }
    delay(10);
  }
  
  client.stop();
  
  if (response.length() == 0) {
    Serial.println("[ERROR] Empty response from OpenAI");
    return "";
  }
  
  // Parse JSON response
  int jsonStart = response.indexOf("\r\n\r\n") + 4;
  if (jsonStart < 4) {
    Serial.println("[ERROR] Invalid HTTP response format");
    return "";
  }
  
  String jsonResponse = response.substring(jsonStart);
  
  DynamicJsonDocument doc(4096);
  DeserializationError error = deserializeJson(doc, jsonResponse);
  
  if (error) {
    Serial.println("[ERROR] Failed to parse OpenAI JSON response");
    if (DEBUG_MODE) Serial.println("Response: " + jsonResponse);
    return "";
  }
  
  // Extract AI response
  if (doc["choices"].size() > 0) {
    String ai_response = doc["choices"][0]["message"]["content"];
    return ai_response;
  }
  
  Serial.println("[ERROR] No response found in OpenAI response");
  return "";
}

// ================================
// CONVERSATION MANAGEMENT
// ================================

void initializeConversation() {
  conversation_history["messages"] = conversation_history.createNestedArray("messages");
  history_count = 0;
  
  if (DEBUG_MODE) Serial.println("[SETUP] Conversation history initialized");
}

void updateConversationHistory(String user_message, String ai_response) {
  JsonArray messages = conversation_history["messages"];
  
  // Add user message
  JsonObject user_msg = messages.createNestedObject();
  user_msg["role"] = "user";
  user_msg["content"] = user_message;
  
  // Add assistant response
  JsonObject assistant_msg = messages.createNestedObject();
  assistant_msg["role"] = "assistant";
  assistant_msg["content"] = ai_response;
  
  history_count += 2;
  
  // Limit history size
  while (history_count > MAX_HISTORY * 2) {
    messages.remove(0);
    messages.remove(0);
    history_count -= 2;
  }
  
  if (DEBUG_MODE) {
    Serial.printf("[DEBUG] Conversation history updated (%d messages)\n", history_count);
  }
}

// ================================
// OUTPUT AND STATUS FUNCTIONS
// ================================

void displayAIResponse(String response) {
  Serial.println("\n" + String("=").substring(0, 50));
  Serial.println("🤖 SOLANA GLASSES RESPONSE:");
  Serial.println("=================================");
  Serial.println(response);
  Serial.println("=================================\n");
}

void printSystemStatus() {
  Serial.println("\n📊 SYSTEM STATUS:");
  Serial.println("==================");
  Serial.printf("WiFi: %s\n", wifi_connected ? "✅ Connected" : "❌ Disconnected");
  Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("SD Card: %s\n", sd_initialized ? "✅ Ready" : "❌ Error");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Uptime: %lu ms\n", millis());
  Serial.printf("Conversation History: %d messages\n", history_count);
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
      
    case LED_RECORDING:
      // Fast blink during recording
      digitalWrite(LED_PIN, (now / 100) % 2);
      break;
      
    case LED_PROCESSING:
      // Medium blink during processing
      digitalWrite(LED_PIN, (now / 200) % 2);
      break;
      
    case LED_ERROR:
      // Very fast blink for errors
      digitalWrite(LED_PIN, (now / 50) % 2);
      break;
  }
} 