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

## Project Structure

- `Vocalis.ino`: ESP32 Arduino sketch for voice command recognition and web interface
- `mic_web_interface.py`: Python Flask web app for recording, processing, and forwarding audio to ESP32
- `dependencies/`: Directory containing required libraries (to be uploaded)

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
   - (See `dependencies/` for all required libraries)

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

---

Feel free to modify or expand this README as needed for your GitHub repository.
