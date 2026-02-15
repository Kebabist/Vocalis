# Vocalis
Vocalis is a voice command recognition system powered by Edge Impulse, designed for ESP32 (Arduino) and available as a standalone C++ library. It enables real-time voice command detection and device control, with a web interface for audio upload and analysis.

## Features

- **ESP32 Arduino Version**:
  - Real-time voice command inference using Edge Impulse models
  - Web server for uploading and analyzing audio files
  - Visual feedback via RGB LEDs and onboard flashlight
  - WiFi connectivity for remote access
  - Supports HTTP POST and file upload for audio data

- **C++ Library Version**:
  - Portable C++ library for integrating voice command recognition into custom applications
  - Edge Impulse model inference support

## Edge Impulse Voice Recognition Model

Vocalis uses a voice recognition model trained and optimized with Edge Impulse. Edge Impulse provides a powerful platform for collecting audio data, designing features (such as MFCCs), and training classifiers to recognize spoken commands. The resulting model is exported for embedded deployment, enabling real-time, offline inference directly on the ESP32 or in C++ applications. This approach ensures robust performance, low latency, and adaptability for custom voice commands.

## Model Optimization: EON™ Compiler & Quantized Inference

Vocalis leverages the Edge Impulse EON™ Compiler for deployment, which provides the same model accuracy while significantly reducing memory and storage requirements—up to 40% less RAM and 46% less ROM compared to standard inference engines. This enables efficient, real-time voice command recognition on resource-constrained devices like the ESP32.

For optimal performance, Vocalis uses quantized (int8) models instead of unoptimized float32 models. Quantized inference reduces RAM usage and latency, allowing the model to run faster and with lower power consumption, while maintaining high accuracy. For example, on Espressif ESP-EYE (ESP32 240MHz):

- **Quantized (int8):**
  - Latency: 365 ms (MFCC + classifier)
  - RAM: 15.4K
  - Flash: 32.0K
  - Accuracy: 76.70%

- **Unoptimized (float32):**
  - Latency: 404 ms
  - RAM: 15.4K (MFCC) + 7.0K (classifier)
  - Flash: 32.2K
  - Accuracy: 76.95%

By deploying with EON™ Compiler and quantized models, Vocalis achieves efficient, offline voice command recognition with minimal latency and power consumption.

## Project Structure

- `Vocalis.ino`: ESP32 Arduino sketch for voice command recognition and web interface
- `mic_web_interface.py`: Python Flask web app for recording, processing, and forwarding audio to ESP32
- `dependencies`

## Getting Started

### ESP32 Arduino Version

1. **Hardware Requirements**:
   - ESP32 (tested with ESP32-CAM)
   - RGB LEDs (PWM-capable GPIOs)
   - Optional: Microphone (I2S interface)

2. **Setup**:
   - Flash `Vocalis.ino` to your ESP32 using Arduino IDE (ESP32 core v2.0.4 recommended)
   - Connect RGB LEDs and flashlight to specified GPIO pins
   - Update WiFi credentials in the sketch

3. **Dependencies**:
   - Edge Impulse inferencing library
   - ESP32 Arduino core
   - FreeRTOS, I2S driver, WiFi, WebServer libraries
   - Download `dependencies` Based on your Device: VoiceREC_Arduino_lib.rar is an Avr C code optimized for ESP32 devices , voicerec-cpp-lib.zip is a CPP library of the same trained Model

4. **Usage**:
   - Start the ESP32; it will connect to WiFi and launch a web server
   - Access the web interface via the ESP32's IP address
   - Upload raw audio files or send audio via HTTP POST for inference
   - Visual feedback is provided via LEDs and flashlight

### Python Web App

- Run `mic_web_interface.py` to start a local web server for recording and sending audio to ESP32
- Requirements: Python 3.x, Flask, requests, numpy, (optional) webrtcvad, noisereduce
- Access the web app at `http://localhost:5000`
- Record audio, apply VAD/denoise, and forward to ESP32 for inference

### C++ Library Version

- Integrate the C++ library into your application for offline or embedded voice command recognition
- See the library documentation for API usage and model integration

## Dependencies

- All required libraries will be provided in the `dependencies/` directory.
- For Python, install requirements via:
  ```
  pip install flask requests numpy webrtcvad noisereduce
  ```

## Supported Commands

- "on", "off", "yes", "no", "up", "down", "left", "right"
- Custom commands can be added by retraining the Edge Impulse model

## License

This project is licensed under the MIT License. See the source files for details.

## Credits

- Edge Impulse for model and inferencing libraries
- ESP32 Arduino Core
- Python Flask
