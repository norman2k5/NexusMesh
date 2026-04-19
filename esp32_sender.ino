#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_MLX90614.h>
#include "MAX30105.h"
#include "heartRate.h"

Adafruit_MLX90614 mlx = Adafruit_MLX90614();

MAX30105 particleSensor;
long lastBeat = 0;
float beatsPerMinute = 0;

// MAC addresses of the receivers
uint8_t receiver1MAC[] = {0x98, 0xA3, 0x16, 0x9D, 0x5C, 0xD4};
uint8_t receiver2MAC[] = {0x10, 0x51, 0xDB, 0x0A, 0xAE, 0xE0};

// Pin for gas sensor
#define DO_PIN 22

// Data structure to send
typedef struct test_struct {
    double f;       // Fahrenheit
    bool gas;       // Gas or no gas
    float bpm;      // Beats per min
    bool fingerOn;  // Check if finger touches pulse sensor
} test_struct;

test_struct test;

// Peer info structures
esp_now_peer_info_t peer1;
esp_now_peer_info_t peer2;

// Send callback for ESP32-C6 after sending data
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    Serial.print("Send status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

void setup() {
    Serial.begin(115200);
    Wire.begin(4, 5); // SDA, SCL of MLX

    // Initialize MLX sensor
    if (!mlx.begin()) {
        Serial.println("Error connecting to MLX sensor. Check wiring.");
        while (1);
    }

    // MAX30102 on pins 6 & 7
    Wire1.begin(6, 7); // SDA, SCL of pulse
    if (!particleSensor.begin(Wire1, I2C_SPEED_FAST)) {
        Serial.println("MAX30102 was not found. Please check wiring/power.");
        while (1);
    }
    Serial.println("Place index finger on the sensor with steady pressure.");
    particleSensor.setup();
    particleSensor.setPulseAmplitudeRed(0x0A);  // Enable red/IR LED
    particleSensor.setPulseAmplitudeGreen(0);

    //Gas sensor
    pinMode(DO_PIN, INPUT);
    Serial.println("Warming up the MQ2 sensor");
    delay(20000);

    WiFi.mode(WIFI_STA);    // Set wifi to station mode

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // Register send callback
    esp_now_register_send_cb(OnDataSent);

    // Add first peer
    memset(&peer1, 0, sizeof(peer1));
    memcpy(peer1.peer_addr, receiver1MAC, 6);
    peer1.channel = 0;
    peer1.encrypt = false;
    if (esp_now_add_peer(&peer1) != ESP_OK) {
        Serial.println("Failed to add receiver1 peer");
    }

    // Add second peer
    memset(&peer2, 0, sizeof(peer2));
    memcpy(peer2.peer_addr, receiver2MAC, 6);
    peer2.channel = 0;
    peer2.encrypt = false;
    if (esp_now_add_peer(&peer2) != ESP_OK) {
        Serial.println("Failed to add receiver2 peer");
    }
}

void loop() {
    // Read IR value from pulse sensor
    long irValue = particleSensor.getIR();
    test.fingerOn = (irValue >= 50000);

    // Detect hearbeat & calc BPM
    if (checkForBeat(irValue) == true) {
        long delta = millis() - lastBeat;
        lastBeat = millis();
        beatsPerMinute = 60 / (delta / 1000.0);
    }

    // Store BPM
    test.bpm = beatsPerMinute;
    // Non-blocking send every 1 second
    static unsigned long lastSend = 0;
    if (millis() - lastSend >= 1000) {
        lastSend = millis();
    }
    
    // Read temperature
    test.f = mlx.readObjectTempF();
    //Initialize gas state
    test.gas = (digitalRead(DO_PIN) == LOW);

    Serial.println("---------------------------------");
    Serial.print("Object temperature: ");
    Serial.print(test.f);
    Serial.println(" °F");
    Serial.print("Gas: ");
        if (test.gas) {
            Serial.println("Present!");
        } else {
            Serial.println("No");
        }
    Serial.print("BPM: ");
    Serial.println(test.bpm);
    if (!test.fingerOn) {
        Serial.println("No finger detected");
    }
    Serial.println("---------------------------------");

    // Send to receiver1
    esp_err_t result1 = esp_now_send(receiver1MAC, (uint8_t *)&test, sizeof(test_struct));
    if (result1 == ESP_OK) Serial.println("Sent to receiver1 successfully");
    else Serial.println("Error sending to receiver1");

    // Send to receiver2
    esp_err_t result2 = esp_now_send(receiver2MAC, (uint8_t *)&test, sizeof(test_struct));
    if (result2 == ESP_OK) Serial.println("Sent to receiver2 successfully");
    else Serial.println("Error sending to receiver2");
    Serial.println("");
}