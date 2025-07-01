/*
  Solana Glasses - ESP32-CAM Voice Assistant v2.0
  
  Simplified version with PSRAM audio buffering (NO SD CARD REQUIRED)
  
  Features:
  - WiFi HTTP server for audio upload
  - PSRAM audio buffering (faster, more reliable)
  - Deepgram Speech-to-Text API integration
  - OpenAI Chat API integration
  - Conversation history management
  - LED status indicators
  
  Hardware: ESP32-CAM (no SD card needed)
  Audio: Received via HTTP from Node.js emulator
*/

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

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

// Audio Configuration
#define MAX_AUDIO_SIZE 500000   // 500KB max audio (about 15 seconds at 16kHz)
#define MIN_AUDIO_SIZE 5000     // 5KB minimum

// System Configuration
#define DEBUG_MODE true         // Set to false for production
#define MAX_HISTORY 8           // Maximum conversation history entries
#define HTTP_TIMEOUT 30000      // 30 seconds timeout

// ================================
// GLOBAL VARIABLES
// ================================

WebServer server(80);
WiFiClientSecure client;

// Audio Buffer (stored in PSRAM)
uint8_t* audioBuffer = NULL;
size_t audioBufferSize = 0;

// Conversation History  
DynamicJsonDocument conversation_history(4096);  // Optimized for memory efficiency
int history_count = 0;

// System Status
bool wifi_connected = false;
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
  Serial.println("Solana Glasses ESP32-CAM v2.0");
  Serial.println("=================================");
  Serial.println("🚀 Simplified PSRAM-only version");
  Serial.println("📦 No SD card required!");
  
  // Initialize hardware
  setupPins();
  setupPSRAM();
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

void setupPSRAM() {
  if (psramFound()) {
    size_t psramSize = ESP.getPsramSize();
    Serial.printf("[SETUP] PSRAM found: %d bytes\n", psramSize);
    
    // Allocate audio buffer in PSRAM
    audioBuffer = (uint8_t*)ps_malloc(MAX_AUDIO_SIZE);
    if (audioBuffer) {
      Serial.printf("[SETUP] Audio buffer allocated: %d bytes in PSRAM\n", MAX_AUDIO_SIZE);
    } else {
      Serial.println("[ERROR] Failed to allocate PSRAM buffer");
      setLEDStatus(LED_ERROR);
    }
  } else {
    Serial.println("[WARNING] PSRAM not found - using regular RAM");
    audioBuffer = (uint8_t*)malloc(MAX_AUDIO_SIZE);
    if (!audioBuffer) {
      Serial.println("[ERROR] Failed to allocate RAM buffer");
      setLEDStatus(LED_ERROR);
    }
  }
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
    Serial.printf("[INFO] Signal strength: %d dBm\n", WiFi.RSSI());
  } else {
    wifi_connected = false;
    Serial.println("\n[ERROR] WiFi connection failed!");
    printWiFiHelp();
    setLEDStatus(LED_ERROR);
  }
}

void printWiFiHelp() {
  Serial.println("\n[HELP] WiFi Troubleshooting:");
  Serial.println("1. Check SSID and password in code");
  Serial.println("2. Ensure 2.4GHz network (ESP32 doesn't support 5GHz)");
  Serial.println("3. Try mobile hotspot for testing");
  Serial.println("4. Move ESP32-CAM closer to router\n");
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
    <head><title>Solana Glasses ESP32-CAM v2.0</title></head>
    <body>
      <h1>🎤 Solana Glasses Voice Assistant</h1>
      <h2>v2.0 - PSRAM Edition</h2>
      <h3>System Status</h3>
      <p>WiFi: ✅ Connected</p>
      <p>Storage: 🧠 PSRAM Buffer (no SD card needed)</p>
      <p>IP Address: )" + WiFi.localIP().toString() + R"(</p>
      <p>Signal: )" + String(WiFi.RSSI()) + R"( dBm</p>
      <p>Free Heap: )" + String(ESP.getFreeHeap()) + R"( bytes</p>
      <p>Free PSRAM: )" + String(ESP.getFreePsram()) + R"( bytes</p>
      <h3>Features</h3>
      <ul>
        <li>✅ Fast PSRAM audio buffering</li>
        <li>✅ Direct API processing</li>
        <li>✅ No SD card required</li>
        <li>✅ Conversation history</li>
      </ul>
    </body>
    </html>
  )";
  
  server.send(200, "text/html", html);
}

void handleStatus() {
  DynamicJsonDocument status(1024);
  
  status["wifi_connected"] = wifi_connected;
  status["storage_type"] = "PSRAM";
  status["recording_active"] = recording_active;
  status["free_heap"] = ESP.getFreeHeap();
  status["free_psram"] = ESP.getFreePsram();
  status["uptime"] = millis();
  status["ip_address"] = WiFi.localIP().toString();
  status["signal_strength"] = WiFi.RSSI();
  status["conversation_history_count"] = history_count;
  status["version"] = "2.0";
  
  String response;
  serializeJson(status, response);
  
  server.send(200, "application/json", response);
}

void handleAudioUpload() {
  server.send(200, "application/json", "{\"status\":\"ready\",\"storage\":\"psram\"}");
}

void handleAudioUploadFile() {
  setLEDStatus(LED_RECORDING);
  
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    Serial.println("[INFO] Started receiving audio data");
    audioBufferSize = 0;
    recording_active = true;
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // Copy audio data directly to PSRAM buffer
    if (audioBufferSize + upload.currentSize <= MAX_AUDIO_SIZE) {
      memcpy(audioBuffer + audioBufferSize, upload.buf, upload.currentSize);
      audioBufferSize += upload.currentSize;
    } else {
      Serial.println("[WARNING] Audio buffer overflow - truncating");
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    recording_active = false;
    
    Serial.printf("[INFO] Audio received: %d bytes in PSRAM\n", audioBufferSize);
    
    // Process the audio data
    processAudioBuffer();
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

void processAudioBuffer() {
  setLEDStatus(LED_PROCESSING);
  
  Serial.println("[PROCESS] Starting audio processing pipeline...");
  
  // Step 1: Validate audio buffer
  if (audioBufferSize < MIN_AUDIO_SIZE) {
    Serial.printf("[ERROR] Audio too small: %d bytes (minimum %d)\n", audioBufferSize, MIN_AUDIO_SIZE);
    setLEDStatus(LED_ERROR);
    return;
  }
  
  if (audioBufferSize > MAX_AUDIO_SIZE) {
    Serial.printf("[ERROR] Audio too large: %d bytes (maximum %d)\n", audioBufferSize, MAX_AUDIO_SIZE);
    setLEDStatus(LED_ERROR);
    return;
  }
  
  Serial.printf("[DEBUG] Processing %d bytes of audio data\n", audioBufferSize);
  
  // Step 2: Convert speech to text
  String transcription = speechToText();
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
  
  setLEDStatus(LED_READY);
  Serial.println("[PROCESS] Audio processing complete\n");
}

// ================================
// DEEPGRAM STT INTEGRATION
// ================================

String speechToText() {
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
  
  // Prepare HTTP request (minimal response for ESP32-CAM memory efficiency)
  String request = "POST /v1/listen?model=nova-2 HTTP/1.1\r\n";
  request += "Host: " + String(deepgram_host) + "\r\n";
  request += "Authorization: Token " + String(deepgram_api_key) + "\r\n";
  request += "Content-Type: audio/wav\r\n";
  request += "Content-Length: " + String(audioBufferSize) + "\r\n";
  request += "Connection: close\r\n\r\n";
  
  client.print(request);
  
  // Send audio data from PSRAM buffer
  size_t sent = 0;
  while (sent < audioBufferSize) {
    size_t chunk_size = min((size_t)1024, audioBufferSize - sent);
    client.write(audioBuffer + sent, chunk_size);
    sent += chunk_size;
    delay(1); // Small delay to prevent overwhelming
  }
  
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
  
  // Optimized buffer size for minimal Deepgram response (no smart_format)
  DynamicJsonDocument doc(2048);  // Reduced from 8192 - minimal response is much smaller
  DeserializationError error = deserializeJson(doc, jsonResponse);
  
  if (error) {
    Serial.println("[ERROR] Failed to parse Deepgram JSON response");
    Serial.printf("[DEBUG] JSON Error: %s\n", error.c_str());
    Serial.printf("[DEBUG] Response length: %d bytes\n", jsonResponse.length());
    if (DEBUG_MODE) {
      Serial.println("Response preview (first 500 chars):");
      Serial.println(jsonResponse.substring(0, 500));
    }
    return "";
  }
  
  // Extract transcription with better error checking
  if (doc.containsKey("results") && 
      doc["results"].containsKey("channels") && 
      doc["results"]["channels"].size() > 0 &&
      doc["results"]["channels"][0].containsKey("alternatives") &&
      doc["results"]["channels"][0]["alternatives"].size() > 0) {
    
    String transcript = doc["results"]["channels"][0]["alternatives"][0]["transcript"];
    
    if (transcript.length() > 0) {
      return transcript;
    } else {
      Serial.println("[ERROR] Empty transcript in response");
    }
  } else {
    Serial.println("[ERROR] Invalid Deepgram response structure");
    if (DEBUG_MODE) {
      Serial.println("Available keys:");
      for (JsonPair kv : doc.as<JsonObject>()) {
        Serial.printf("  %s\n", kv.key().c_str());
      }
    }
  }
  
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
  DynamicJsonDocument request_doc(3072);  // Smaller for memory efficiency
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
  
  // Increase buffer size for OpenAI's response with usage statistics
  DynamicJsonDocument doc(4096);  // Increased from 3072 to 4096 bytes
  DeserializationError error = deserializeJson(doc, jsonResponse);
  
  if (error) {
    Serial.println("[ERROR] Failed to parse OpenAI JSON response");
    Serial.printf("[DEBUG] JSON Error: %s\n", error.c_str());
    Serial.printf("[DEBUG] Response length: %d bytes\n", jsonResponse.length());
    if (DEBUG_MODE) {
      Serial.println("Response preview (first 500 chars):");
      Serial.println(jsonResponse.substring(0, 500));
    }
    return "";
  }
  
  // Extract AI response with better error checking
  if (doc.containsKey("choices") && 
      doc["choices"].size() > 0 &&
      doc["choices"][0].containsKey("message") &&
      doc["choices"][0]["message"].containsKey("content")) {
    
    String ai_response = doc["choices"][0]["message"]["content"];
    
    if (ai_response.length() > 0) {
      return ai_response;
    } else {
      Serial.println("[ERROR] Empty content in OpenAI response");
    }
  } else {
    Serial.println("[ERROR] Invalid OpenAI response structure");
    if (DEBUG_MODE) {
      Serial.println("Available keys:");
      for (JsonPair kv : doc.as<JsonObject>()) {
        Serial.printf("  %s\n", kv.key().c_str());
      }
    }
  }
  
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
  
  // Limit history size (smaller for memory efficiency)
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
  Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
  Serial.printf("Storage: 🧠 PSRAM Buffer\n");
  Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
  Serial.printf("Uptime: %lu ms\n", millis());
  Serial.printf("Conversations: %d messages\n", history_count);
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