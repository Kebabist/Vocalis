/* Edge Impulse Arduino examples
 * Copyright (c) 2022 EdgeImpulse Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

// These sketches are tested with 2.0.4 ESP32 Arduino Core
// https://github.com/espressif/arduino-esp32/releases/tag/2.0.4

// If your target is limited in memory remove this macro to save 10K RAM
#define EIDSP_QUANTIZE_FILTERBANK   0

/*
 ** NOTE: If you run into TFLite arena allocation issue.
 **
 ** This may be due to may dynamic memory fragmentation.
 ** Try defining "-DEI_CLASSIFIER_ALLOCATION_STATIC" in boards.local.txt (create
 ** if it doesn't exist) and copy this file to
 ** `<ARDUINO_CORE_INSTALL_PATH>/arduino/hardware/<mbed_core>/<core_version>/`.
 **
 ** See
 ** (https://support.arduino.cc/hc/en-us/articles/360012076960-Where-are-the-installed-cores-located-)
 ** to find where Arduino installs cores on your machine.
 **
 ** If the problem persists then there's not enough memory for this model and application.
 */

/* Includes ---------------------------------------------------------------- */
#include <VoiceREC_inferencing.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2s.h"

/* Add WiFi + upload proxy includes */
#include <WiFi.h>
#include <WebServer.h>

/* WiFi credentials - update as needed */
const char* ssid = "SSID";
const char* password = "Wifi_Pass";

/* RGB LED Pins (PWM-capable GPIOs) */
#define RED_PIN     12
#define GREEN_PIN   13
#define BLUE_PIN    14

/* ESP32-CAM Onboard Flashlight */
#define FLASHLIGHT_PIN  4

/* Web server on port 80 */
WebServer server(80);


/** Audio buffers, pointers and selectors */
typedef struct {
    int16_t *buffer;
    uint8_t buf_ready;
    uint32_t buf_count;
    uint32_t n_samples;
} inference_t;

static inference_t inference;
static const uint32_t sample_buffer_size = 2048;
static signed short sampleBuffer[sample_buffer_size];
static bool debug_nn = false; // Set this to true to see e.g. features generated from the raw signal
static bool record_status = true;

/* Flag set when audio arrived via HTTP upload/post */
static volatile bool audio_received = false;

/* Wrapper declarations that reuse existing microphone functions/buffer */
static bool audio_inference_start(uint32_t n_samples) {
    return microphone_inference_start(n_samples);
}

static bool audio_inference_wait(void) {
    return microphone_inference_record();
}

static int audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    return microphone_audio_signal_get_data(offset, length, out_ptr);
}

static void audio_inference_end(void) {
    microphone_inference_end();
}

/* HTTP handlers - file upload / POST proxy */
void handleRoot() {
    String html = "<html><head><title>Voice Command Receiver</title></head><body>";
    html += "<h1>ESP32 Voice Command Receiver</h1>";
    html += "<p>Upload a raw audio file (16-bit signed PCM, mono, " + String(EI_CLASSIFIER_FREQUENCY) + " Hz)</p>";
    html += "<p>Expected samples: " + String(EI_CLASSIFIER_RAW_SAMPLE_COUNT) + "</p>";
    html += "<form method='POST' action='/upload' enctype='multipart/form-data'>";
    html += "<input type='file' name='audio' accept='.raw,.bin'>";
    html += "<input type='submit' value='Upload and Analyze'>";
    html += "</form>";
    html += "<hr><h2>Or send via POST request:</h2>";
    html += "<p>POST /audio with raw 16-bit PCM data in body</p>";
    html += "</body></html>";
    server.send(200, "text/html", html);
}

void setLEDColor(const char* label) {
    Serial.printf("Setting LED for label: %s\n", label);
    analogWrite(RED_PIN, 0);
    analogWrite(GREEN_PIN, 0);
    analogWrite(BLUE_PIN, 0);

    if (strcmp(label, "on") == 0) {
        digitalWrite(FLASHLIGHT_PIN, HIGH);
        Serial.println("Flashlight ON");
        return; // Keep flashlight on, don't turn off after delay
    } else if (strcmp(label, "off") == 0) {
        digitalWrite(FLASHLIGHT_PIN, LOW);
        Serial.println("Flashlight OFF");
        return; // Flashlight off, no delay needed
    } else if (strcmp(label, "yes") == 0) {
        analogWrite(GREEN_PIN, 255);
    } else if (strcmp(label, "no") == 0) {
        analogWrite(RED_PIN, 255);
    } else if (strcmp(label, "up") == 0) {
        analogWrite(BLUE_PIN, 255);
    } else if (strcmp(label, "down") == 0) {
        analogWrite(RED_PIN, 255);
        analogWrite(GREEN_PIN, 255);
    } else if (strcmp(label, "left") == 0) {
        analogWrite(GREEN_PIN, 255);
        analogWrite(BLUE_PIN, 255);
    } else if (strcmp(label, "right") == 0) {
        analogWrite(RED_PIN, 255);
        analogWrite(BLUE_PIN, 255);
    } else {
        analogWrite(RED_PIN, 255);
        analogWrite(GREEN_PIN, 255);
        analogWrite(BLUE_PIN, 255);
    }

    delay(2000);
    analogWrite(RED_PIN, 0);
    analogWrite(GREEN_PIN, 0);
    analogWrite(BLUE_PIN, 0);
}

void handleAudioPost() {
    size_t content_length = 0;
    if (server.hasHeader("Content-Length")) {
        content_length = server.header("Content-Length").toInt();
    }
    if (content_length == 0 || content_length > inference.n_samples * sizeof(int16_t)) {
        server.send(400, "text/plain", "Invalid content length. Expected up to " +
            String(inference.n_samples * sizeof(int16_t)) + " bytes, got " +
            String(content_length));
        return;
    }
    WiFiClient client = server.client();
    size_t bytes_read = 0;
    uint8_t* audio_ptr = (uint8_t*)inference.buffer;
    unsigned long timeout = millis() + 5000;
    while (bytes_read < content_length && millis() < timeout) {
        if (client.available()) {
            size_t to_read = min((size_t)512, content_length - bytes_read);
            size_t read_now = client.read(audio_ptr + bytes_read, to_read);
            bytes_read += read_now;
            timeout = millis() + 5000;
        }
        delay(1);
    }
    inference.buf_count = bytes_read / sizeof(int16_t);
    Serial.printf("Received %d bytes (%d samples)\n", bytes_read, inference.buf_count);
    if (inference.buf_count == 0) {
        server.send(400, "text/plain", "No audio data received");
        return;
    }
    audio_received = true;
    inference.buf_ready = 1;
    server.send(200, "text/plain", "Audio received successfully");
}

void handleAudioUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("Upload Start: %s\n", upload.filename.c_str());
        inference.buf_count = 0;
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        size_t samples_in_chunk = upload.currentSize / sizeof(int16_t);
        size_t samples_to_copy = min(samples_in_chunk,
                                     (size_t)(inference.n_samples - inference.buf_count));
        if (samples_to_copy > 0) {
            memcpy(&inference.buffer[inference.buf_count], upload.buf, samples_to_copy * sizeof(int16_t));
            inference.buf_count += samples_to_copy;
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        Serial.printf("Upload End: %s, %u bytes\n", upload.filename.c_str(), upload.totalSize);
        audio_received = true;
        inference.buf_ready = 1;
    }
}

void handleUploadComplete() {
    if (!audio_received) {
        server.send(400, "text/plain", "No audio data received");
        return;
    }
    // Run inference on the uploaded buffer
    signal_t signal;
    signal.total_length = inference.buf_count;
    signal.get_data = &audio_signal_get_data;
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        server.send(500, "text/plain", String("Failed to run classifier: ") + String((int)r));
        audio_received = false;
        return;
    }

    // Build JSON response
    String response = "{\n";
    response += "  \"timing\": {\n";
    response += "    \"dsp_ms\": " + String(result.timing.dsp) + ",\n";
    response += "    \"classification_ms\": " + String(result.timing.classification) + ",\n";
    response += "    \"anomaly_ms\": " + String(result.timing.anomaly) + "\n";
    response += "  },\n";
    response += "  \"predictions\": [\n";

    float max_value = 0;
    const char* best_label = "";
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        response += "    {\"label\": \"" + String(result.classification[ix].label) +
                   "\", \"confidence\": " + String(result.classification[ix].value, 5) + "}";
        if (ix < EI_CLASSIFIER_LABEL_COUNT - 1) response += ",";
        response += "\n";

        if (result.classification[ix].value > max_value) {
            max_value = result.classification[ix].value;
            best_label = result.classification[ix].label;
        }
    }
    response += "  ],\n";
    response += "  \"best_match\": \"" + String(best_label) + "\",\n";
    response += "  \"confidence\": " + String(max_value, 5) + ",\n";
#if EI_CLASSIFIER_HAS_ANOMALY == 1
    response += "  \"anomaly_score\": " + String(result.anomaly, 5) + ",\n";
#endif
    response += "  \"samples_received\": " + String(inference.buf_count) + "\n";
    response += "}";

    server.send(200, "application/json", response);

    // Also print to serial
    ei_printf("Predictions (DSP: %d ms., Classification: %d ms., Anomaly: %d ms.):\n",
        result.timing.dsp, result.timing.classification, result.timing.anomaly);
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("    %s: ", result.classification[ix].label);
        ei_printf_float(result.classification[ix].value);
        ei_printf("\n");
    }
    ei_printf("Best match: %s (%.2f%%)\n", best_label, max_value * 100);

    setLEDColor(best_label);

    audio_received = false;
}

/* Helper to start the upload server - call from setup() if you want the HTTP API */
void startUploadServer() {
    WiFi.begin(ssid, password);
    int wifi_timeout = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_timeout < 30) {
        delay(1000);
        ei_printf(".");
        wifi_timeout++;
    }
    if (WiFi.status() != WL_CONNECTED) {
        ei_printf("\nFailed to connect to WiFi!\n");
        return;
    }
    ei_printf("\nWiFi connected!\n");
    ei_printf("IP Address: ");
    Serial.println(WiFi.localIP());

    // Allocate buffer for uploaded audio without starting microphone/I2S
    inference.buffer = (int16_t *)malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));
    if (inference.buffer == NULL) {
        ei_printf("ERR: Could not allocate audio buffer (size %d)\r\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT);
        return;
    }
    inference.buf_count = 0;
    inference.n_samples = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    inference.buf_ready = 0;

    server.on("/", HTTP_GET, handleRoot);
    server.on("/audio", HTTP_POST, handleAudioPost);
    server.on("/upload", HTTP_POST, handleUploadComplete, handleAudioUpload);
    const char* headerKeys[] = {"Content-Length", "Content-Type"};
    server.collectHeaders(headerKeys, 2);
    server.begin();
    ei_printf("HTTP server started on port 80\n");
}

/**
 * @brief      Arduino setup function
 */
void setup()
{
    // put your setup code here, to run once:
    Serial.begin(115200);
    // comment out the below line to cancel the wait for USB connection (needed for native USB)
    while (!Serial);
    Serial.println("Edge Impulse Inferencing Demo");

    // Initialize flashlight pin
    pinMode(FLASHLIGHT_PIN, OUTPUT);
    digitalWrite(FLASHLIGHT_PIN, LOW); // Start with flashlight off

    // summary of inferencing settings (from model_metadata.h)
    ei_printf("Inferencing settings:\n");
    ei_printf("\tInterval: ");
    ei_printf_float((float)EI_CLASSIFIER_INTERVAL_MS);
    ei_printf(" ms.\n");
    ei_printf("\tFrame size: %d\n", EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE);
    ei_printf("\tSample length: %d ms.\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT / 16);
    ei_printf("\tNo. of classes: %d\n", sizeof(ei_classifier_inferencing_categories) / sizeof(ei_classifier_inferencing_categories[0]));

    ei_printf("\nStarting continious inference in 2 seconds...\n");
    ei_sleep(2000);

    /* Microphone disabled for now (no mic attached)
    if (microphone_inference_start(EI_CLASSIFIER_RAW_SAMPLE_COUNT) == false) {
        ei_printf("ERR: Could not allocate audio buffer (size %d), this could be due to the window length of your model\r\n", EI_CLASSIFIER_RAW_SAMPLE_COUNT);
        return;
    }

    ei_printf("Recording...\n");
    */
    // start the HTTP upload server so we can POST/upload audio files
    startUploadServer();
}

/**
 * @brief      Arduino main function. Runs the inferencing loop.
 */
void loop()
{
    // service HTTP requests (uploads)
    server.handleClient();

    /* Microphone-based continuous inference disabled (no mic attached)
    bool m = microphone_inference_record();
    if (!m) {
        ei_printf("ERR: Failed to record audio...\n");
        return;
    }

    signal_t signal;
    signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    signal.get_data = &microphone_audio_signal_get_data;
    ei_impulse_result_t result = { 0 };

    EI_IMPULSE_ERROR r = run_classifier(&signal, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
        ei_printf("ERR: Failed to run classifier (%d)\n", r);
        return;
    }

    // print the predictions
    ei_printf("Predictions ");
    ei_printf("(DSP: %d ms., Classification: %d ms., Anomaly: %d ms.)",
        result.timing.dsp, result.timing.classification, result.timing.anomaly);
    ei_printf(": \n");
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        ei_printf("    %s: ", result.classification[ix].label);
        ei_printf_float(result.classification[ix].value);
        ei_printf("\n");
    }
#if EI_CLASSIFIER_HAS_ANOMALY == 1
    ei_printf("    anomaly score: ");
    ei_printf_float(result.anomaly);
    ei_printf("\n");
#endif
    */
}

static void audio_inference_callback(uint32_t n_bytes)
{
    for(int i = 0; i < n_bytes>>1; i++) {
        inference.buffer[inference.buf_count++] = sampleBuffer[i];

        if(inference.buf_count >= inference.n_samples) {
          inference.buf_count = 0;
          inference.buf_ready = 1;
        }
    }
}

static void capture_samples(void* arg) {

  const int32_t i2s_bytes_to_read = (uint32_t)arg;
  size_t bytes_read = i2s_bytes_to_read;

  while (record_status) {

    /* read data at once from i2s */
    i2s_read((i2s_port_t)1, (void*)sampleBuffer, i2s_bytes_to_read, &bytes_read, 100);

    if (bytes_read <= 0) {
      ei_printf("Error in I2S read : %d", bytes_read);
    }
    else {
        if (bytes_read < i2s_bytes_to_read) {
        ei_printf("Partial I2S read");
        }

        // scale the data (otherwise the sound is too quiet)
        for (int x = 0; x < i2s_bytes_to_read/2; x++) {
            sampleBuffer[x] = (int16_t)(sampleBuffer[x]) * 8;
        }

        if (record_status) {
            audio_inference_callback(i2s_bytes_to_read);
        }
        else {
            break;
        }
    }
  }
  vTaskDelete(NULL);
}

/**
 * @brief      Init inferencing struct and setup/start PDM
 *
 * @param[in]  n_samples  The n samples
 *
 * @return     { description_of_the_return_value }
 */
static bool microphone_inference_start(uint32_t n_samples)
{
    inference.buffer = (int16_t *)malloc(n_samples * sizeof(int16_t));

    if(inference.buffer == NULL) {
        return false;
    }

    inference.buf_count  = 0;
    inference.n_samples  = n_samples;
    inference.buf_ready  = 0;

    if (i2s_init(EI_CLASSIFIER_FREQUENCY)) {
        ei_printf("Failed to start I2S!");
    }

    ei_sleep(100);

    record_status = true;

    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, (void*)sample_buffer_size, 10, NULL);

    return true;
}

/**
 * @brief      Wait on new data
 *
 * @return     True when finished
 */
static bool microphone_inference_record(void)
{
    bool ret = true;

    while (inference.buf_ready == 0) {
        delay(10);
    }

    inference.buf_ready = 0;
    return ret;
}

/**
 * Get raw audio signal data
 */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr)
{
    numpy::int16_to_float(&inference.buffer[offset], out_ptr, length);

    return 0;
}

/**
 * @brief      Stop PDM and release buffers
 */
static void microphone_inference_end(void)
{
    i2s_deinit();
    ei_free(inference.buffer);
}


static int i2s_init(uint32_t sampling_rate) {
  // Start listening for audio: MONO @ 8/16KHz
  i2s_config_t i2s_config = {
      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_TX),
      .sample_rate = sampling_rate,
      .bits_per_sample = (i2s_bits_per_sample_t)16,
      .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
      .communication_format = I2S_COMM_FORMAT_I2S,
      .intr_alloc_flags = 0,
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = false,
      .tx_desc_auto_clear = false,
      .fixed_mclk = -1,
  };
  i2s_pin_config_t pin_config = {
      .bck_io_num = 26,    // IIS_SCLK
      .ws_io_num = 32,     // IIS_LCLK
      .data_out_num = -1,  // IIS_DSIN
      .data_in_num = 33,   // IIS_DOUT
  };
  esp_err_t ret = 0;

  ret = i2s_driver_install((i2s_port_t)1, &i2s_config, 0, NULL);
  if (ret != ESP_OK) {
    ei_printf("Error in i2s_driver_install");
  }

  ret = i2s_set_pin((i2s_port_t)1, &pin_config);
  if (ret != ESP_OK) {
    ei_printf("Error in i2s_set_pin");
  }

  ret = i2s_zero_dma_buffer((i2s_port_t)1);
  if (ret != ESP_OK) {
    ei_printf("Error in initializing dma buffer with 0");
  }

  return int(ret);
}

static int i2s_deinit(void) {
    i2s_driver_uninstall((i2s_port_t)1); //stop & destroy i2s driver
    return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif

