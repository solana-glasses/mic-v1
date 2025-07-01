# Required Libraries for Solana Glasses ESP32-CAM

This document lists all Arduino libraries needed for the Solana Glasses voice assistant project.

## ESP32 Board Package Installation

### Step 1: Add ESP32 Board Manager URL
1. Open Arduino IDE
2. Go to **File** → **Preferences**
3. In "Additional Board Manager URLs" field, add:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
4. Click **OK**

### Step 2: Install ESP32 Board Package
1. Go to **Tools** → **Board** → **Boards Manager**
2. Search for "ESP32"
3. Install **ESP32 by Espressif Systems** (Version 2.0.11 or later)
   - ⚠️ **Important**: Use ESP32 core 2.x.x (based on ESP-IDF 4.4.x) for maximum compatibility
   - Version 3.x.x (ESP-IDF 5.3.x) may have compatibility issues with some libraries
4. Wait for installation to complete

### Step 3: Select ESP32-CAM Board
1. Go to **Tools** → **Board** → **ESP32 Arduino**
2. Select **AI Thinker ESP32-CAM**
3. Set the following board settings:
   - **CPU Frequency**: 240MHz (WiFi/BT)
   - **Flash Mode**: QIO
   - **Flash Frequency**: 80MHz
   - **Flash Size**: 4MB (32Mb)
   - **Partition Scheme**: Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)
   - **Core Debug Level**: None
   - **PSRAM**: Enabled

## Required Libraries

### 1. ArduinoJson (REQUIRED)
- **Purpose**: JSON parsing for API requests/responses (Deepgram, OpenAI)
- **Installation**: Arduino Library Manager
- **Search Term**: "ArduinoJson"
- **Author**: Benoit Blanchon
- **Recommended Version**: 6.21.3 or later
- **Installation Steps**:
  1. Go to **Tools** → **Manage Libraries**
  2. Search for "ArduinoJson"
  3. Install "ArduinoJson by Benoit Blanchon"

### 2. Built-in ESP32 Libraries (Included with ESP32 Core)
These libraries are automatically available after installing the ESP32 board package:

#### WiFi.h
- **Purpose**: WiFi connection management
- **Status**: Built-in with ESP32 core
- **No installation required**

#### WebServer.h  
- **Purpose**: HTTP server for receiving audio files
- **Status**: Built-in with ESP32 core
- **No installation required**

#### WiFiClientSecure.h
- **Purpose**: HTTPS connections to APIs (Deepgram, OpenAI)
- **Status**: Built-in with ESP32 core
- **No installation required**

#### SD.h
- **Purpose**: SD card file operations
- **Status**: Built-in with ESP32 core
- **No installation required**

#### FS.h
- **Purpose**: File system operations
- **Status**: Built-in with ESP32 core
- **No installation required**

#### SPI.h
- **Purpose**: SPI communication for SD card
- **Status**: Built-in with ESP32 core
- **No installation required**

## Library Verification

### Check Installed Libraries
1. Go to **Tools** → **Manage Libraries**
2. Search for each library name
3. Verify "INSTALLED" status

### Test Compilation
1. Open the `solana_glasses_esp32cam.ino` sketch
2. Select the correct board: **AI Thinker ESP32-CAM**
3. Click **Verify** (checkmark icon)
4. If compilation succeeds, all libraries are properly installed

## Troubleshooting

### Common Issues and Solutions

#### 1. ESP32 Board Not Found
- **Symptom**: ESP32 boards don't appear in Tools → Board menu
- **Solution**: 
  - Double-check Board Manager URL is correct
  - Restart Arduino IDE
  - Re-install ESP32 board package

#### 2. Compilation Errors for Built-in Libraries
- **Symptom**: "WiFi.h not found" or similar errors
- **Solution**:
  - Verify ESP32 board is selected (not Arduino Uno/Nano)
  - Update ESP32 core to latest stable version
  - Clean and rebuild project

#### 3. ArduinoJson Compilation Errors
- **Symptom**: JSON-related compilation errors
- **Solution**:
  - Update to ArduinoJson version 6.21.3 or later
  - Check for conflicting JSON libraries
  - Remove old ArduinoJson versions

#### 4. Memory Issues During Compilation
- **Symptom**: "Out of memory" errors
- **Solution**:
  - Select correct partition scheme (Default 4MB with spiffs)
  - Enable PSRAM in board settings
  - Close unnecessary programs during compilation

#### 5. Upload Issues
- **Symptom**: Upload fails or board not detected
- **Solution**:
  - Install CP2102 or CH340 USB drivers
  - Put ESP32-CAM in programming mode (connect GPIO 0 to GND)
  - Check COM port selection
  - Try different USB cable

## Version Compatibility Matrix

| Component | Recommended Version | Minimum Version | Notes |
|-----------|-------------------|-----------------|-------|
| Arduino IDE | 2.2.1 | 1.8.19 | IDE 2.x recommended |
| ESP32 Core | 2.0.11 | 2.0.0 | Avoid 3.x.x for compatibility |
| ArduinoJson | 6.21.3 | 6.19.0 | Version 7.x not tested |

## Memory Requirements

### Flash Memory Usage
- **Sketch Size**: ~800KB (with all features)
- **Available Flash**: 4MB total
- **Partition**: 1.2MB for application code
- **SPIFFS**: 1.5MB for file system (SD card operations)

### RAM Usage
- **Static RAM**: ~50KB
- **Dynamic RAM**: ~100KB (conversation history, JSON buffers)
- **Available RAM**: 320KB + 4MB PSRAM
- **Recommendation**: Enable PSRAM for large JSON operations

## Alternative Libraries (If Issues Occur)

### If ArduinoJson Issues
1. **AsyncJson**: For async operations
2. **JSONModel**: Simpler but less feature-rich

### If WiFi Issues
1. **ESPAsyncWebServer**: Alternative to WebServer.h
2. **WiFiManager**: For easier WiFi configuration

### If SD Card Issues
1. **SdFat**: More robust SD card library
2. **LITTLEFS**: Alternative file system

## Production Recommendations

### For Deployment
1. Set `DEBUG_MODE false` in sketch
2. Use proper SSL certificates instead of `setInsecure()`
3. Implement watchdog timers
4. Add OTA update capability
5. Use stable library versions (avoid beta/RC)

### Security Considerations
1. Store API keys in EEPROM or secure storage
2. Implement request rate limiting
3. Add input validation for HTTP requests
4. Use encrypted communication only

## Getting Help

### Official Documentation
- [ESP32 Arduino Core Documentation](https://docs.espressif.com/projects/arduino-esp32/)
- [ArduinoJson Documentation](https://arduinojson.org/)

### Community Support
- [ESP32 Arduino Forum](https://esp32.com/)
- [Arduino Community Forum](https://forum.arduino.cc/)
- [GitHub Issues](https://github.com/espressif/arduino-esp32/issues)

### Common Search Terms for Troubleshooting
- "ESP32-CAM Arduino setup"
- "ESP32 WiFiClientSecure examples"
- "ArduinoJson ESP32 memory"
- "ESP32-CAM SD card problems" 