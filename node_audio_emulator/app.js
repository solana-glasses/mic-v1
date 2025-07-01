#!/usr/bin/env node

/**
 * Solana Glasses Audio Emulator
 * 
 * Node.js application that emulates INMP441 microphone for ESP32-CAM development.
 * Records audio from MacBook microphone, converts to 16kHz 16-bit mono WAV,
 * and sends to ESP32-CAM via HTTP POST.
 */

const fs = require('fs');
const path = require('path');
const record = require('node-record-lpcm16');
const wav = require('wav');
const axios = require('axios');
const FormData = require('form-data');
const chalk = require('chalk');
const inquirer = require('inquirer');
const cliProgress = require('cli-progress');
const keypress = require('keypress');
const notifier = require('node-notifier');
const ip = require('ip');

// Configuration
const CONFIG = {
  audio: {
    sampleRate: 16000,
    channels: 1,
    bitDepth: 16,
    encoding: 'signed-integer'
  },
  recording: {
    standardDuration: 5000,    // Fixed 5-second recording
    maxDuration: 10000,
    minDuration: 500,
    silenceThreshold: 0.01,
    recordingTimeout: 30000
  },
  network: {
    esp32Port: 80,
    uploadEndpoint: '/upload-audio',
    statusEndpoint: '/status',
    timeout: 30000,
    retryAttempts: 3
  },
  files: {
    saveRecordings: true,
    recordingsDir: './recordings',
    tempDir: './temp',
    maxFiles: 50
  },
  ui: {
    updateInterval: 100,
    levelBarWidth: 30,
    debugMode: false
  }
};

// Global state
let state = {
  esp32IP: null,
  isConnected: false,
  isRecording: false,
  audioLevel: 0,
  recordingData: [],
  recordingStartTime: 0,
  statusInterval: null,
  currentRecorder: null
};

async function main() {
  console.clear();
  printBanner();
  setupDirectories();
  setupKeyboard();
  await findESP32();
  await startMainInterface();
}

function printBanner() {
  console.log(chalk.cyan('╔══════════════════════════════════════════════════════════════╗'));
  console.log(chalk.cyan('║') + chalk.white.bold('                    SOLANA GLASSES                           ') + chalk.cyan('║'));
  console.log(chalk.cyan('║') + chalk.white('                  Audio Emulator v1.0                       ') + chalk.cyan('║'));
  console.log(chalk.cyan('║') + chalk.gray('              ESP32-CAM Development Tool                     ') + chalk.cyan('║'));
  console.log(chalk.cyan('╚══════════════════════════════════════════════════════════════╝'));
  console.log();
  console.log(chalk.green('🎤 MacBook Microphone → ESP32-CAM Voice Assistant'));
  console.log(chalk.gray('Records audio and sends via HTTP to ESP32-CAM for processing\n'));
}

function setupDirectories() {
  const dirs = [CONFIG.files.recordingsDir, CONFIG.files.tempDir];
  dirs.forEach(dir => {
    if (!fs.existsSync(dir)) {
      fs.mkdirSync(dir, { recursive: true });
    }
  });
}

function setupKeyboard() {
  keypress(process.stdin);
  process.stdin.on('keypress', handleKeypress);
  process.stdin.setRawMode(true);
  process.stdin.resume();
  
  // Handle process cleanup
  process.on('SIGINT', () => {
    if (state.isRecording) {
      stopRecording();
    }
    exitApplication();
  });
}

async function findESP32() {
  console.log(chalk.yellow('🔍 Searching for ESP32-CAM device...\n'));
  
  const discoveredIP = await autoDiscoverESP32();
  
  if (discoveredIP) {
    state.esp32IP = discoveredIP;
    console.log(chalk.green(`✅ Found ESP32-CAM at: ${discoveredIP}\n`));
  } else {
    console.log(chalk.yellow('❌ Auto-discovery failed. Please enter IP manually.\n'));
    await manualIPEntry();
  }
  
  const connected = await testESP32Connection();
  if (connected) {
    state.isConnected = true;
    console.log(chalk.green('✅ ESP32-CAM connection successful!\n'));
  } else {
    console.log(chalk.red('❌ Failed to connect to ESP32-CAM'));
    process.exit(1);
  }
}

async function autoDiscoverESP32() {
  const localIP = ip.address();
  const subnet = localIP.substring(0, localIP.lastIndexOf('.'));
  
  console.log(chalk.gray(`Scanning subnet: ${subnet}.x`));
  
  const promises = [];
  for (let i = 1; i <= 254; i++) {
    const targetIP = `${subnet}.${i}`;
    promises.push(checkESP32AtIP(targetIP));
  }
  
  try {
    const results = await Promise.allSettled(promises);
    const foundIPs = results
      .filter(result => result.status === 'fulfilled' && result.value)
      .map(result => result.value);
    
    if (foundIPs.length > 0) {
      return foundIPs[0];
    }
  } catch (error) {
    if (CONFIG.ui.debugMode) {
      console.log(chalk.red(`Discovery error: ${error.message}`));
    }
  }
  
  return null;
}

async function checkESP32AtIP(ip) {
  try {
    const response = await axios.get(`http://${ip}:${CONFIG.network.esp32Port}${CONFIG.network.statusEndpoint}`, {
      timeout: 2000
    });
    
    if (response.data && (response.data.wifi_connected !== undefined)) {
      return ip;
    }
  } catch (error) {
    // Ignore connection errors during discovery
  }
  
  return null;
}

async function manualIPEntry() {
  const answer = await inquirer.prompt([{
    type: 'input',
    name: 'ip',
    message: 'Enter ESP32-CAM IP address:',
    validate: (input) => {
      const ipRegex = /^(\d{1,3}\.){3}\d{1,3}$/;
      return ipRegex.test(input) || 'Please enter a valid IP address';
    }
  }]);
  
  state.esp32IP = answer.ip;
}

async function testESP32Connection() {
  try {
    const response = await axios.get(
      `http://${state.esp32IP}:${CONFIG.network.esp32Port}${CONFIG.network.statusEndpoint}`,
      { timeout: CONFIG.network.timeout }
    );
    
    return response.status === 200;
  } catch (error) {
    console.log(chalk.red(`Connection test failed: ${error.message}`));
    return false;
  }
}

async function startMainInterface() {
  console.log(chalk.blue('🎛️  CONTROLS:'));
  console.log(chalk.white('   SPACEBAR    ') + chalk.gray('Record 5 seconds of audio'));
  console.log(chalk.white('   R           ') + chalk.gray('Manual record toggle'));
  console.log(chalk.white('   S           ') + chalk.gray('Show ESP32-CAM status'));
  console.log(chalk.white('   T           ') + chalk.gray('Test connection'));
  console.log(chalk.white('   D           ') + chalk.gray('Toggle debug mode'));
  console.log(chalk.white('   C           ') + chalk.gray('Clear screen'));
  console.log(chalk.white('   Q           ') + chalk.gray('Quit application'));
  console.log();
  
  console.log(chalk.green('🎤 Ready to record! Press SPACEBAR for 5-second recording...\n'));
}

function handleKeypress(ch, key) {
  if (!key) return;
  
  switch (key.name) {
    case 'space':
      handleSpacebarPress();
      break;
      
    case 'r':
      toggleRecording();
      break;
      
    case 's':
      showESP32Status();
      break;
      
    case 't':
      testConnection();
      break;
      
    case 'd':
      toggleDebugMode();
      break;
      
    case 'c':
      console.clear();
      printBanner();
      console.log(chalk.green('🎤 Ready to record! Press SPACEBAR for 5-second recording...\n'));
      break;
      
    case 'q':
      exitApplication();
      break;
  }
}

function handleSpacebarPress() {
  if (state.isRecording) {
    console.log(chalk.yellow('⚠️  Already recording! Please wait...'));
    return;
  }
  
  console.log(chalk.cyan('\n🎤 Recording for 5 seconds... Please speak now!'));
  startStandardRecording();
}

function startStandardRecording() {
  startRecording();
  
  // Automatically stop after 5 seconds
  setTimeout(() => {
    if (state.isRecording) {
      console.log(chalk.blue('\n⏱️  5 seconds completed - stopping recording...'));
      stopRecording();
    }
  }, CONFIG.recording.standardDuration);
}

async function startRecording() {
  if (state.isRecording) {
    console.log(chalk.yellow('⚠️  Already recording!'));
    return;
  }
  
  console.log(chalk.green('\n🎤 Starting recording...'));
  state.isRecording = true;
  state.recordingData = [];
  state.recordingStartTime = Date.now();
  
  const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
  const tempFile = path.join(CONFIG.files.tempDir, `recording_${timestamp}.wav`);
  
  const writer = new wav.FileWriter(tempFile, {
    channels: CONFIG.audio.channels,
    sampleRate: CONFIG.audio.sampleRate,
    bitDepth: CONFIG.audio.bitDepth
  });
  
  state.currentRecorder = record.record({
    sampleRateHertz: CONFIG.audio.sampleRate,
    threshold: 0,
    verbose: CONFIG.ui.debugMode,
    recordProgram: 'sox',
    silence: '1.0',
    device: null
  });
  
  state.currentRecorder.stream()
    .on('data', (chunk) => {
      state.recordingData.push(chunk);
      state.audioLevel = calculateAudioLevel(chunk);
    })
    .on('error', (error) => {
      console.log(chalk.red(`\nRecording error: ${error.message}`));
      stopRecording();
    })
    .pipe(writer);
  
  // Note: Recording duration is now controlled by startStandardRecording()
  // Keep max duration as fallback safety
  setTimeout(() => {
    if (state.isRecording) {
      console.log(chalk.yellow('\n⏱️  Max recording duration reached (safety timeout)'));
      stopRecording();
    }
  }, CONFIG.recording.maxDuration);
}

function stopRecording() {
  if (!state.isRecording) return;
  
  console.log(chalk.blue('\n🛑 Stopping recording...'));
  state.isRecording = false;
  
  if (state.currentRecorder) {
    state.currentRecorder.stop();
    state.currentRecorder = null;
  }
  
  const duration = Date.now() - state.recordingStartTime;
  
  if (duration < CONFIG.recording.minDuration) {
    console.log(chalk.yellow(`⚠️  Recording too short (${duration}ms). Minimum: ${CONFIG.recording.minDuration}ms`));
    return;
  }
  
  setTimeout(() => {
    processRecording(duration);
  }, 500);
}

async function processRecording(duration) {
  console.log(chalk.blue('🔄 Processing recording...'));
  
  const tempFiles = fs.readdirSync(CONFIG.files.tempDir)
    .filter(file => file.startsWith('recording_') && file.endsWith('.wav'))
    .map(file => ({
      name: file,
      path: path.join(CONFIG.files.tempDir, file),
      mtime: fs.statSync(path.join(CONFIG.files.tempDir, file)).mtime
    }))
    .sort((a, b) => b.mtime - a.mtime);
  
  if (tempFiles.length === 0) {
    console.log(chalk.red('❌ No recorded file found'));
    return;
  }
  
  const recordedFile = tempFiles[0].path;
  
  if (!fs.existsSync(recordedFile)) {
    console.log(chalk.red('❌ Recorded file not found'));
    return;
  }
  
  const fileSize = fs.statSync(recordedFile).size;
  console.log(chalk.gray(`📁 File size: ${fileSize} bytes (${(duration/1000).toFixed(1)}s)`));
  
  if (fileSize < 1000) {
    console.log(chalk.yellow('⚠️  Recording file too small, skipping upload'));
    return;
  }
  
  if (CONFIG.files.saveRecordings) {
    const savedFile = path.join(CONFIG.files.recordingsDir, tempFiles[0].name);
    fs.copyFileSync(recordedFile, savedFile);
    console.log(chalk.gray(`💾 Saved: ${savedFile}`));
  }
  
  await uploadToESP32(recordedFile);
  
  fs.unlinkSync(recordedFile);
  
  console.log(chalk.green('\n✅ Recording processed! Press SPACEBAR for next 5-second recording...\n'));
}

function calculateAudioLevel(buffer) {
  if (buffer.length === 0) return 0;
  
  let sum = 0;
  for (let i = 0; i < buffer.length; i += 2) {
    const sample = buffer.readInt16LE(i);
    sum += sample * sample;
  }
  
  const rms = Math.sqrt(sum / (buffer.length / 2));
  return Math.min(rms / 32768, 1.0);
}

async function uploadToESP32(filePath) {
  if (!state.isConnected) {
    console.log(chalk.red('❌ ESP32-CAM not connected'));
    return;
  }
  
  console.log(chalk.blue('📤 Uploading to ESP32-CAM...'));
  
  try {
    const fileStream = fs.createReadStream(filePath);
    const formData = new FormData();
    formData.append('audio', fileStream, {
      filename: 'audio.wav',
      contentType: 'audio/wav'
    });
    
    const response = await axios.post(
      `http://${state.esp32IP}:${CONFIG.network.esp32Port}${CONFIG.network.uploadEndpoint}`,
      formData,
      {
        headers: formData.getHeaders(),
        timeout: CONFIG.network.timeout,
        maxContentLength: Infinity,
        maxBodyLength: Infinity
      }
    );
    
    if (response.status === 200) {
      console.log(chalk.green('✅ Upload successful!'));
      console.log(chalk.gray('🤖 ESP32-CAM is processing your voice...'));
    } else {
      console.log(chalk.yellow(`⚠️  Upload completed with status: ${response.status}`));
    }
    
  } catch (error) {
    console.log(chalk.red(`❌ Upload failed: ${error.message}`));
    
    if (error.code === 'ECONNREFUSED') {
      console.log(chalk.yellow('💡 Check if ESP32-CAM is powered on and connected to WiFi'));
      state.isConnected = false;
    }
  }
}

function toggleRecording() {
  if (state.isRecording) {
    stopRecording();
  } else {
    startRecording();
  }
}

async function showESP32Status() {
  if (!state.esp32IP) {
    console.log(chalk.red('❌ No ESP32-CAM IP configured'));
    return;
  }
  
  console.log(chalk.blue('\n📊 Fetching ESP32-CAM status...'));
  
  try {
    const response = await axios.get(
      `http://${state.esp32IP}:${CONFIG.network.esp32Port}${CONFIG.network.statusEndpoint}`,
      { timeout: CONFIG.network.timeout }
    );
    
    const status = response.data;
    
    console.log(chalk.white('\n📋 ESP32-CAM STATUS:'));
    console.log(chalk.white('═══════════════════'));
    console.log(chalk.white('WiFi:         ') + (status.wifi_connected ? chalk.green('✅ Connected') : chalk.red('❌ Disconnected')));
    console.log(chalk.white('SD Card:      ') + (status.sd_initialized ? chalk.green('✅ Ready') : chalk.red('❌ Error')));
    console.log(chalk.white('Recording:    ') + (status.recording_active ? chalk.yellow('🔴 Active') : chalk.gray('⚪ Idle')));
    console.log(chalk.white('IP Address:   ') + chalk.cyan(status.ip_address));
    console.log(chalk.white('Free Heap:    ') + chalk.yellow(status.free_heap + ' bytes'));
    console.log(chalk.white('Uptime:       ') + chalk.gray((status.uptime / 1000).toFixed(1) + ' seconds'));
    console.log(chalk.white('Conversations:') + chalk.cyan(status.conversation_history_count));
    console.log();
    
  } catch (error) {
    console.log(chalk.red(`❌ Failed to get status: ${error.message}`));
    state.isConnected = false;
  }
}

async function testConnection() {
  console.log(chalk.blue('\n🔍 Testing ESP32-CAM connection...'));
  
  const connected = await testESP32Connection();
  state.isConnected = connected;
  
  if (connected) {
    console.log(chalk.green('✅ Connection successful!'));
  } else {
    console.log(chalk.red('❌ Connection failed!'));
    console.log(chalk.yellow('💡 Try checking the IP address or ESP32-CAM power'));
  }
  console.log();
}

function toggleDebugMode() {
  CONFIG.ui.debugMode = !CONFIG.ui.debugMode;
  console.log(chalk.blue(`\n🔧 Debug mode: ${CONFIG.ui.debugMode ? 'ON' : 'OFF'}`));
  console.log();
}

function exitApplication() {
  console.log(chalk.blue('\n👋 Shutting down Solana Glasses Audio Emulator...'));
  
  if (state.isRecording) {
    stopRecording();
  }
  
  if (state.statusInterval) {
    clearInterval(state.statusInterval);
  }
  
  console.log(chalk.green('✅ Goodbye!'));
  process.exit(0);
}

process.on('SIGINT', () => {
  console.log(chalk.yellow('\n⚠️  Received SIGINT (Ctrl+C)'));
  exitApplication();
});

if (require.main === module) {
  main().catch(error => {
    console.log(chalk.red(`\n❌ Application Error: ${error.message}`));
    process.exit(1);
  });
}
