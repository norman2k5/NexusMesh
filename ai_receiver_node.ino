// ESP32-C6 Receiver with AI Anomaly Detection, Multi-User Support, and MQ-2 Gas Sensor
#include <esp_now.h>
#include <WiFi.h>

// Structure to receive data - MUST MATCH THE SENDER
typedef struct test_struct {
    double f;      // Fahrenheit temperature
    bool gas;      // Gas detected (true/false from MQ-2 DO pin)
} test_struct;

test_struct myData;

// User Profile Structure
struct UserProfile {
  uint8_t id;
  String name;
  float history[50];           // Store last 50 temperature readings (°C)
  int historyIndex;
  int readingsCount;
  float personalBaseline;
  float baselineStdDev;
  bool baselineReady;
  float feverStartTemp;
  unsigned long feverStartTime;
  bool feverDetected;
  unsigned long lastSeen;
  
  // Gas exposure tracking
  int gasExposureCount;
  unsigned long lastGasAlert;
  float avgTempDuringGas;
};

// Maximum number of users
#define MAX_USERS 10

class MultiUserTempAI {
  private:
    UserProfile users[MAX_USERS];
    int activeUserCount;
    
  public:
    MultiUserTempAI() {
      activeUserCount = 0;
      // Initialize all users
      for (int i = 0; i < MAX_USERS; i++) {
        users[i].id = 255; // Invalid ID
        users[i].historyIndex = 0;
        users[i].readingsCount = 0;
        users[i].personalBaseline = 36.6;
        users[i].baselineStdDev = 0.3;
        users[i].baselineReady = false;
        users[i].feverDetected = false;
        users[i].lastSeen = 0;
        users[i].name = "Unknown";
        users[i].gasExposureCount = 0;
        users[i].lastGasAlert = 0;
        users[i].avgTempDuringGas = 0;
      }
    }
    
    // Register a new user
    int registerUser(uint8_t userId, String userName = "") {
      // Check if user already exists
      for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].id == userId) {
          if (userName != "") users[i].name = userName;
          return i;
        }
      }
      
      // Find empty slot
      for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].id == 255) {
          users[i].id = userId;
          users[i].name = (userName != "") ? userName : "User" + String(userId);
          users[i].historyIndex = 0;
          users[i].readingsCount = 0;
          users[i].personalBaseline = 36.6;
          users[i].baselineStdDev = 0.3;
          users[i].baselineReady = false;
          users[i].feverDetected = false;
          users[i].lastSeen = millis();
          users[i].gasExposureCount = 0;
          users[i].lastGasAlert = 0;
          activeUserCount++;
          Serial.print("✅ New user registered: ");
          Serial.print(users[i].name);
          Serial.print(" (ID: ");
          Serial.print(userId);
          Serial.println(")");
          return i;
        }
      }
      Serial.println("❌ Max users reached!");
      return -1;
    }
    
    // Get user index by ID
    int getUserIndex(uint8_t userId) {
      for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].id == userId) {
          return i;
        }
      }
      return -1;
    }
    
    // Convert Fahrenheit to Celsius
    float fahrenheitToCelsius(float f) {
      return (f - 32.0) * 5.0 / 9.0;
    }
    
    // Add temperature for specific user
    void addTemperature(uint8_t userId, float tempF) {
      float tempC = fahrenheitToCelsius(tempF);
      
      int userIndex = getUserIndex(userId);
      if (userIndex == -1) {
        userIndex = registerUser(userId);
        if (userIndex == -1) return;
      }
      
      UserProfile* user = &users[userIndex];
      user->lastSeen = millis();
      
      // Store in history
      user->history[user->historyIndex] = tempC;
      user->historyIndex = (user->historyIndex + 1) % 50;
      if (user->readingsCount < 50) user->readingsCount++;
      
      // Update personal baseline every 20 readings
      if (user->readingsCount >= 20 && user->readingsCount % 10 == 0) {
        updateBaseline(user);
      }
    }
    
    // Record gas exposure for user
    void recordGasExposure(uint8_t userId, float currentTempF) {
      float currentTempC = fahrenheitToCelsius(currentTempF);
      
      int userIndex = getUserIndex(userId);
      if (userIndex == -1) {
        userIndex = registerUser(userId);
        if (userIndex == -1) return;
      }
      
      UserProfile* user = &users[userIndex];
      user->gasExposureCount++;
      user->lastGasAlert = millis();
      user->avgTempDuringGas = (user->avgTempDuringGas + currentTempC) / 2;
    }
    
    // Calculate personal normal temperature range
    void updateBaseline(UserProfile* user) {
      float sum = 0;
      int validCount = 0;
      
      // Use readings between 36-37.5°C (normal range)
      for (int i = 0; i < user->readingsCount; i++) {
        if (user->history[i] >= 36.0 && user->history[i] <= 37.5) {
          sum += user->history[i];
          validCount++;
        }
      }
      
      if (validCount >= 10) {
        user->personalBaseline = sum / validCount;
        
        // Calculate standard deviation
        float varianceSum = 0;
        for (int i = 0; i < user->readingsCount; i++) {
          if (user->history[i] >= 36.0 && user->history[i] <= 37.5) {
            float diff = user->history[i] - user->personalBaseline;
            varianceSum += diff * diff;
          }
        }
        user->baselineStdDev = sqrt(varianceSum / validCount);
        user->baselineReady = true;
        
        Serial.print("📊 ");
        Serial.print(user->name);
        Serial.print(" baseline: ");
        Serial.print(user->personalBaseline);
        Serial.print("°C (±");
        Serial.print(user->baselineStdDev);
        Serial.println(")");
      }
    }
    
    // Detect anomalies using personal baseline
    struct AnomalyResult {
      String status;
      String recommendation;
      String icon;
      bool isAlert;
      float tempC;
      float tempF;
      uint8_t userId;
      bool isGasAlert;
    };
    
    AnomalyResult detectAnomaly(uint8_t userId, float currentTempF, bool gasDetected) {
      float currentTempC = fahrenheitToCelsius(currentTempF);
      
      AnomalyResult result;
      result.isAlert = false;
      result.tempC = currentTempC;
      result.tempF = currentTempF;
      result.userId = userId;
      result.isGasAlert = gasDetected;
      
      int userIndex = getUserIndex(userId);
      if (userIndex == -1) {
        result.status = "UNKNOWN USER";
        result.recommendation = "Register user first";
        result.icon = "❓";
        return result;
      }
      
      UserProfile* user = &users[userIndex];
      
      // PRIORITY 1: GAS DETECTION (Highest Priority)
      if (gasDetected) {
        result.isAlert = true;
        recordGasExposure(userId, currentTempF);
        
        result.status = "⚠️ GAS LEAK DETECTED! ⚠️";
        result.recommendation = "EVACUATE IMMEDIATELY! Turn off gas source! Ventilate area!";
        result.icon = "💨🔥🚨";
        
        // Add gas exposure warning
        if (user->gasExposureCount >= 3) {
          result.recommendation += " Multiple gas exposures! Seek medical attention!";
        }
        return result;
      }
      
      // PRIORITY 2: TEMPERATURE ANOMALIES
      // 1. ABSOLUTE THRESHOLDS (Clinical definitions)
      if (currentTempC >= 38.0) {
        result.isAlert = true;
        if (currentTempC >= 39.5) {
          result.status = "CRITICAL FEVER!";
          result.recommendation = "EMERGENCY! Seek medical help NOW!";
          result.icon = "🚨";
        } else if (currentTempC >= 38.5) {
          result.status = "HIGH FEVER";
          result.recommendation = "Contact Supervisor NOW!";
          result.icon = "⚠️";
        } else {
          result.status = "FEVER";
          result.recommendation = "Rest & hydrate. Monitor temp.";
          result.icon = "⚠️";
        }
        return result;
      }
      
      if (currentTempC <= 35.0) {
        result.isAlert = true;
        result.status = "HYPOTHERMIA";
        result.recommendation = "Get warm! Contact Supervisor NOW!.";
        result.icon = "❄️";
        return result;
      }
      
      // 2. PERSONAL BASELINE DEVIATION
      if (user->baselineReady && currentTempC >= 36.0 && currentTempC <= 38.0) {
        float deviation = abs(currentTempC - user->personalBaseline);
        
        if (deviation > 1.0) {
          result.isAlert = true;
          if (currentTempC > user->personalBaseline) {
            result.status = "ELEVATED";
            result.recommendation = "Above YOUR normal. Early sign?";
          } else {
            result.status = "LOWERED";
            result.recommendation = "Below YOUR normal. Check environment.";
          }
          result.icon = "⚠️";
          return result;
        }
        
        // Statistical anomaly (2 standard deviations)
        if (deviation > (user->baselineStdDev * 2)) {
          result.isAlert = true;
          result.status = "UNUSUAL";
          result.recommendation = "Unusual pattern. Monitor closely.";
          result.icon = "⚠️";
          return result;
        }
      }
      
      // 3. RAPID CHANGE DETECTION
      if (user->readingsCount >= 5) {
        float recentAvg = 0;
        int recentCount = 0;
        for (int i = 0; i < 5 && i < user->readingsCount; i++) {
          int idx = (user->historyIndex - 1 - i + 50) % 50;
          recentAvg += user->history[idx];
          recentCount++;
        }
        recentAvg /= recentCount;
        
        float change = abs(currentTempC - recentAvg);
        if (change > 0.8) {
          result.isAlert = true;
          result.status = "RAPID CHANGE";
          result.recommendation = "Temp changing fast! Verify reading.";
          result.icon = "⚠️";
          return result;
        }
      }
      
      // 4. Fever progression tracking
      if (currentTempC >= 37.5) {
        if (!user->feverDetected) {
          user->feverDetected = true;
          user->feverStartTemp = currentTempC;
          user->feverStartTime = millis();
          result.status = "FEVER STARTING";
          result.recommendation = "Monitor temperature closely";
          result.icon = "⚠️";
          result.isAlert = true;
          return result;
        } else {
          unsigned long feverDuration = (millis() - user->feverStartTime) / 60000;
          float tempIncrease = currentTempC - user->feverStartTemp;
          
          if (tempIncrease > 1.0 && feverDuration < 30) {
            result.isAlert = true;
            result.status = "RAPID FEVER ↑";
            result.recommendation = "Temp rising fast! Contact Supervisor!";
            result.icon = "🚨";
            return result;
          }
        }
      } else {
        user->feverDetected = false;
      }
      
      // NORMAL
      result.status = "NORMAL";
      result.recommendation = "Temperature is normal";
      result.icon = "✅";
      return result;
    }
    
    String getTrend(uint8_t userId) {
      int userIndex = getUserIndex(userId);
      if (userIndex == -1) return "Unknown User";
      
      UserProfile* user = &users[userIndex];
      if (user->readingsCount < 10) return "Learning...";
      
      float sum = 0;
      int n = min(10, user->readingsCount);
      for (int i = 0; i < n; i++) {
        int idx = (user->historyIndex - n + i + 50) % 50;
        sum += user->history[idx];
      }
      float avg = sum / n;
      
      if (user->readingsCount >= 10) {
        float recent = user->history[(user->historyIndex - 1 + 50) % 50];
        if (recent > avg + 0.3) return "RISING ↑";
        if (recent < avg - 0.3) return "FALLING ↓";
      }
      return "STABLE →";
    }
    
    float getPersonalBaseline(uint8_t userId) {
      int userIndex = getUserIndex(userId);
      if (userIndex == -1) return 36.6;
      return users[userIndex].personalBaseline;
    }
    
    bool isBaselineReady(uint8_t userId) {
      int userIndex = getUserIndex(userId);
      if (userIndex == -1) return false;
      return users[userIndex].baselineReady;
    }
    
    int getGasExposureCount(uint8_t userId) {
      int userIndex = getUserIndex(userId);
      if (userIndex == -1) return 0;
      return users[userIndex].gasExposureCount;
    }
    
    void listAllUsers() {
      Serial.println("\n📋 Registered Users:");
      Serial.println("===================");
      for (int i = 0; i < MAX_USERS; i++) {
        if (users[i].id != 255) {
          Serial.print("ID: ");
          Serial.print(users[i].id);
          Serial.print(" | Name: ");
          Serial.print(users[i].name);
          Serial.print(" | Readings: ");
          Serial.print(users[i].readingsCount);
          if (users[i].baselineReady) {
            Serial.print(" | Baseline: ");
            Serial.print(users[i].personalBaseline);
            Serial.print("°C");
          }
          if (users[i].gasExposureCount > 0) {
            Serial.print(" | Gas Exposures: ");
            Serial.print(users[i].gasExposureCount);
          }
          Serial.print(" | Last seen: ");
          Serial.print((millis() - users[i].lastSeen) / 1000);
          Serial.println("s ago");
        }
      }
      Serial.println("===================\n");
    }
};

MultiUserTempAI ai;

// Variable for tracking multiple senders
uint8_t lastSenderMAC[6];
unsigned long lastDataTime = 0;

// Callback function for ESP32-C6
void OnDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  
  // Get sender MAC address (used as unique user ID)
  uint8_t* senderMAC = (uint8_t*)recv_info->src_addr;
  
  // Create a simple user ID from MAC address (use last 2 bytes)
  uint8_t userId = senderMAC[4] ^ senderMAC[5]; // XOR last two bytes
  
  // Store last sender info
  memcpy(lastSenderMAC, senderMAC, 6);
  lastDataTime = millis();
  
  // Get data from structure
  float tempF = (float)myData.f;
  bool gasDetected = myData.gas;
  
  // Convert to Celsius for display
  float tempC = (tempF - 32.0) * 5.0 / 9.0;
  
  // Add temperature to AI for analysis
  ai.addTemperature(userId, tempF);
  
  // Detect anomalies (including gas)
  auto result = ai.detectAnomaly(userId, tempF, gasDetected);
  
  // Print to Serial Monitor with comprehensive information
  Serial.println("\n╔══════════════════════════════════════════════════════════════════╗");
  
  // MAC Address (Device ID)
  Serial.print("║ 📱 Device MAC: ");
  Serial.print(senderMAC[0], HEX);
  Serial.print(":"); Serial.print(senderMAC[1], HEX);
  Serial.print(":"); Serial.print(senderMAC[2], HEX);
  Serial.print(":"); Serial.print(senderMAC[3], HEX);
  Serial.print(":"); Serial.print(senderMAC[4], HEX);
  Serial.print(":"); Serial.print(senderMAC[5], HEX);
  int len_mac = 64 - (String("║ 📱 Device MAC: ").length() + 17);
  for(int i = 0; i < len_mac; i++) Serial.print(" ");
  Serial.println("║");
  
  // User ID
  Serial.print("║ 👤 User ID: ");
  Serial.print(userId);
  int len_user = 64 - (String("║ 👤 User ID: ").length() + String(userId).length());
  for(int i = 0; i < len_user; i++) Serial.print(" ");
  Serial.println("║");
  
  Serial.println("║                                                              ║");
  
  // Temperature Info
  Serial.print("║ 🌡️  Temperature: ");
  Serial.print(tempF, 1);
  Serial.print("°F / ");
  Serial.print(tempC, 1);
  Serial.print("°C");
  int len_temp = 64 - (String("║ 🌡️  Temperature: ").length() + String(tempF, 1).length() + 6 + String(tempC, 1).length() + 2);
  for(int i = 0; i < len_temp; i++) Serial.print(" ");
  Serial.println("║");
  
  // Gas Detection
  if (gasDetected) {
    Serial.println("║                                                              ║");
    Serial.println("║ 💨💨💨 GAS DETECTED! 💨💨💨                                   ║");
    Serial.println("║ 🚨 IMMEDIATE ACTION REQUIRED! 🚨                             ║");
    Serial.println("║                                                              ║");
    Serial.println("║ ⚠️  WARNING: Flammable gas detected!                         ║");
    Serial.println("║                                                              ║");
    Serial.println("║ 📋 ACTION ITEMS:                                             ║");
    Serial.println("║    1. EVACUATE the area immediately                         ║");
    Serial.println("║    2. Do NOT use any electrical switches                    ║");
    Serial.println("║    3. Do NOT create sparks or flames                        ║");
    Serial.println("║    4. Open windows if safe to do so                         ║");
    Serial.println("║    5. Call emergency services                               ║");
    Serial.println("║    6. Seek medical attention if symptoms present            ║");
    Serial.println("║                                                              ║");
  }
  
  // Emergency Flag from Gas
  if (gasDetected) {
    Serial.println("║ 🚨 EMERGENCY MODE ACTIVE! 🚨                                  ║");
    Serial.println("║                                                              ║");
  }
  
  // AI Status
  Serial.print("║ 🤖 Status: ");
  Serial.print(result.icon);
  Serial.print(" ");
  Serial.print(result.status);
  int len_status = 64 - (String("║ 🤖 Status: ").length() + result.icon.length() + 1 + result.status.length());
  for(int i = 0; i < len_status; i++) Serial.print(" ");
  Serial.println("║");
  
  // AI Recommendation
  Serial.print("║ 💡 Advice: ");
  
  // Handle long recommendations by wrapping
  String advice = result.recommendation;
  if (advice.length() > 50) {
    // Print first part
    String part1 = advice.substring(0, 50);
    Serial.print(part1);
    int len_adv1 = 64 - (String("║ 💡 Advice: ").length() + part1.length());
    for(int i = 0; i < len_adv1; i++) Serial.print(" ");
    Serial.println("║");
    
    // Print second part
    Serial.print("║            ");
    String part2 = advice.substring(50);
    Serial.print(part2);
    int len_adv2 = 64 - (String("║            ").length() + part2.length());
    for(int i = 0; i < len_adv2; i++) Serial.print(" ");
    Serial.println("║");
  } else {
    Serial.print(advice);
    int len_adv = 64 - (String("║ 💡 Advice: ").length() + advice.length());
    for(int i = 0; i < len_adv; i++) Serial.print(" ");
    Serial.println("║");
  }
  
  // Personal Baseline (if available)
  if (ai.isBaselineReady(userId)) {
    Serial.print("║ 🎯 Personal Baseline: ");
    Serial.print(ai.getPersonalBaseline(userId), 1);
    Serial.print("°C (Trend: ");
    Serial.print(ai.getTrend(userId));
    Serial.print(")");
    int len_base = 64 - (String("║ 🎯 Personal Baseline: ").length() + String(ai.getPersonalBaseline(userId), 1).length() + 6 + ai.getTrend(userId).length() + 1);
    for(int i = 0; i < len_base; i++) Serial.print(" ");
    Serial.println("║");
  } else {
    Serial.print("║ 🎯 Personal Baseline: Learning... (need ");
    Serial.print(20 - ai.getUserIndex(userId) >= 0 ? (20 - ai.getUserIndex(userId) >= 0 ? 20 - ai.getUserIndex(userId) : 0) : 0);
    Serial.print(" more readings)");
    int len_base = 64 - (String("║ 🎯 Personal Baseline: Learning... (need ").length() + 3 + 16);
    for(int i = 0; i < len_base; i++) Serial.print(" ");
    Serial.println("║");
  }
  
  // Gas Exposure History
  int exposureCount = ai.getGasExposureCount(userId);
  if (exposureCount > 0) {
    Serial.print("║ ⚠️  Gas exposure history: ");
    Serial.print(exposureCount);
    Serial.print(" event(s)");
    if (exposureCount >= 3) Serial.print(" - HEALTH RISK!");
    int len_gas = 64 - (String("║ ⚠️  Gas exposure history: ").length() + String(exposureCount).length() + 9 + (exposureCount >= 3 ? 15 : 0));
    for(int i = 0; i < len_gas; i++) Serial.print(" ");
    Serial.println("║");
  }
  
  Serial.println("╚══════════════════════════════════════════════════════════════════╝");
  
  // Visual alert (blink built-in LED for anomalies)
  if (result.isAlert) {
    int blinkCount = gasDetected ? 20 : 3; // Much more blinks for gas detection
    int blinkDelay = gasDetected ? 100 : 150;
    for (int i = 0; i < blinkCount; i++) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(blinkDelay);
      digitalWrite(LED_BUILTIN, LOW);
      delay(blinkDelay);
    }
    // Extra long blink pattern for gas
    if (gasDetected) {
      delay(500);
      for (int i = 0; i < 10; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(50);
        digitalWrite(LED_BUILTIN, LOW);
        delay(50);
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  Serial.println("\n\n╔══════════════════════════════════════════════════════════════════╗");
  Serial.println("║     AI Multi-User Temperature & MQ-2 Gas Monitor                ║");
  Serial.println("║                      ESP32-C6 Receiver                          ║");
  Serial.println("╚══════════════════════════════════════════════════════════════════╝\n");
  
  // Set device as Wi-Fi Station
  WiFi.mode(WIFI_STA);
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Error initializing ESP-NOW");
    return;
  }
  
  // Register receive callback
  esp_now_register_recv_cb(OnDataRecv);
  
  pinMode(LED_BUILTIN, OUTPUT);
  
  Serial.println("✅ System Ready!");
  Serial.println("📡 Monitoring for:");
  Serial.println("   • Temperature anomalies (MLX90614 sensor)");
  Serial.println("   • MQ-2 Gas leaks (flammable gas detection)");
  Serial.println("   • Multi-device tracking (by MAC address)");
  Serial.println("   • Personal baseline learning\n");
  
  Serial.println("📖 About MQ-2 Gas Sensor:");
  Serial.println("   • Detects: LPG, Propane, Hydrogen, Methane, Smoke");
  Serial.println("   • Digital output: LOW when gas detected");
  Serial.println("   • Preheating time required: 20 seconds\n");
  
  Serial.println("📖 Available Commands:");
  Serial.println("   • 'list'  - Show all registered devices/users");
  Serial.println("   • 'clear' - Clear serial monitor");
  Serial.println("   • 'stats' - Show system information");
  Serial.println("   • 'help'  - Show this help\n");
  Serial.println("⏳ Waiting for data from senders...\n");
}

void loop() {
  // Check for data timeout (no data for 30 seconds)
  if (millis() - lastDataTime > 30000 && lastDataTime != 0) {
    Serial.println("⚠️ Warning: No data received for 30 seconds!");
    lastDataTime = millis(); // Reset to avoid spamming
  }
  
  // Handle serial commands
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    command.toLowerCase();
    
    if (command == "list") {
      ai.listAllUsers();
    } 
    else if (command == "clear") {
      Serial.print("\033[2J\033[1;1H"); // Clear screen (ANSI)
      Serial.println("Screen cleared\n");
      Serial.println("📡 Monitoring for data...\n");
    }
    else if (command == "help") {
      Serial.println("\n📖 Available Commands:");
      Serial.println("  list  - Show all registered devices/users");
      Serial.println("  clear - Clear serial monitor");
      Serial.println("  stats - Show system statistics");
      Serial.println("  help  - Show this help\n");
    }
    else if (command == "stats") {
      Serial.println("\n📊 System Statistics:");
      Serial.print("  Free heap: ");
      Serial.print(ESP.getFreeHeap());
      Serial.println(" bytes");
      Serial.print("  CPU frequency: ");
      Serial.print(ESP.getCpuFreqMHz());
      Serial.println(" MHz");
      Serial.print("  Uptime: ");
      Serial.print(millis() / 1000);
      Serial.println(" seconds");
      Serial.print("  Last data received: ");
      if (lastDataTime > 0) {
        Serial.print((millis() - lastDataTime) / 1000);
        Serial.println(" seconds ago");
      } else {
        Serial.println("Never");
      }
      Serial.println();
    }
  }
  
  delay(100);
}

