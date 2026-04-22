# NexusMesh 🏆
## Best Hardware Hack — HackKU 2026
A distributed, AI-powered eco-physiological sensing platform that protects workers and
communities in real time — no cloud, no delay, no privacy concerns.
## What It Does
NexusMesh is a three-node wireless sensor network built on ESP32 microcontrollers. A central
sender node reads body temperature, heart rate, and gas presence simultaneously, then broadcasts
all data over ESP-NOW (Wi-Fi peer-to-peer, no router required) to two receivers:
• A handheld display node shows live readings on a 0.96" OLED — portable enough for
a field worker to carry.
• An AI analysis node connected to a computer runs a multi-user anomaly detection
engine that learns each individual's personal temperature baseline and triggers escalating
alerts when deviations, rapid changes, or gas events are detected.
All inference runs locally on the ESP32. Zero cloud dependency.
Architecture
┌─────────────────────────────────┐ │ SENDER NODE │
│ ESP32-C6 │
│ ├── MLX90614 (IR temperature) │
│ ├── MAX30102 (heart rate/SpO2)│
│ └── MQ-135 (hazardous gas) │
│ │
│ Broadcasts via ESP-NOW @ 1Hz │ └────────────┬────────────────────┘ │ ESP-NOW (802.11, no router)
┌───────┴────────┐ │ │
▼ ▼
┌──────────────┐ ┌────────────────────────────────┐
│ HANDHELD │ │ AI RECEIVER NODE │
│ NODE │ │ ESP32-C6 │
│ ESP32 │ │ │
│ 0.96" OLED │ │ MultiUserTempAI engine: │
│ │ │ • Personal baseline learning │
│ Displays: │ │ • 4-tier anomaly detection │
│ • Temp °F │ │ • Rapid change detection │
│ • BPM │ │ • Fever progression tracking │
│ • Gas alert │ │ • Gas exposure history │
│ • Finger │ │ • Up to 10 concurrent users │
│ detected │ │ • MAC-based user identity │
└──────────────┘ └────────────────────────────────┘
Hardware
Component Purpose Interface
ESP32-C6 (×3) Main MCU for all three nodes —
MLX90614 Non-contact IR body temperature I2C (SDA: GPIO4, SCL: GPIO5)
MAX30102 Heart rate & SpO2 I2C (SDA: GPIO6, SCL: GPIO7)
MQ-135 Hazardous gas detection Digital (GPIO22)
SSD1306 0.96" OLED Live data display (handheld node) I2C
Note: The sender uses two separate I2C buses — Wire for the MLX90614 and Wire1 for the
MAX30102 — to avoid address conflicts.
Files
File Node Description
esp32_sender.ino Sender Reads all sensors, broadcasts struct via ESP-NOW to
two receivers every ~1s
esp32_handheld_node.ino Handheld Receives data, displays temp/BPM/gas status on OLED
ai_receiver_node.ino AI Node Full anomaly detection engine, outputs to Serial Monitor
AI Anomaly Detection
The AI node (ai_receiver_node.ino) implements a MultiUserTempAI class that supports up
to 10 concurrent users, identified by their sender's MAC address.
How the baseline learning works
Each user's last 50 temperature readings are stored in a circular buffer. Once 20 readings are
collected, the system computes a personal normal range (mean ± std dev) using only clinically
normal readings (36–37.5°C). The baseline updates every 10 readings thereafter.
Alert tiers (in priority order)
Priority Trigger Alert Level
1 Gas detected (MQ-135 LOW) 🚨 EVACUATE — immediate action
2 Temp ≥ 39.5°C 🚨 CRITICAL FEVER — emergency
3 Temp ≥ 38.5°C ⚠ HIGH FEVER — contact supervisor
4 Temp ≥ 38°C ⚠ FEVER — rest and monitor
5 Temp ≤ 35°C ❄ HYPOTHERMIA — contact supervisor
6 > 1°C from personal baseline ⚠ ELEVATED / LOWERED
7 > 2× std dev from baseline ⚠ UNUSUAL pattern
8 > 0.8°C rapid change (5-reading window) ⚠ RAPID CHANGE
9 Rising trend above 37.5°C ⚠ FEVER STARTING
— All clear ✅ NORMAL
Gas exposures are tracked per user — three or more exposures triggers an additional medical
attention recommendation.
Serial commands (AI node)
list → Show all registered users and their baselines
stats → Heap, CPU freq, uptime, last data timestamp
clear → Clear terminal (ANSI)
help → Show command list
Setup & Flashing
Requirements
• Arduino IDE 2.x or PlatformIO
• ESP32 board package installed
• Libraries:
o Adafruit MLX90614
o Adafruit SSD1306 + Adafruit GFX
o SparkFun MAX3010x
Steps
1. 2. 3. Flash the AI receiver node first, open Serial Monitor at 115200 baud, and note its MAC
address from the startup output.
Flash the handheld node, note its MAC address.
Update esp32_sender.ino with both receiver MAC addresses:
4. uint8_t receiver1MAC[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX}; //
Handhelduint8_t receiver2MAC[] = {0xXX, 0xXX, 0xXX, 0xXX, 0xXX, 0xXX};
// AI node
5. Flash the sender node.
6. 7. Allow 20 seconds for the MQ-135 to warm up before readings are valid.
Open Serial Monitor on the AI node — data will start streaming immediately.
Dependencies (included as submodules)
• Adafruit_BusIO
• Adafruit_GFX_Library
• Adafruit_MLX90614_Library
• Adafruit_SSD1306
• Adafruit_Unified_Sensor
• SparkFun_MAX3010x_Pulse_and_Proximity_Sensor_Library
Use Cases
• Industrial safety — factory floors, chemical handling, confined space entry
• Healthcare screening — fever detection without physical contact
• Field worker monitoring — portable handheld node for remote environments
• Research — personal physiological baseline tracking over time
Built At
HackKU 2026 — University of Kansas
🏆 Winner: Best Hardware Hack
License
MIT — free to use, modify, and build on.
