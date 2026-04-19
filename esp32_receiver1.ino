#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

int buzzer = 15;

// atch the sender structure
typedef struct test_struct {
  double f;
  bool gas;
  float bpm;
  bool fingerOn;
} test_struct;

// struct_message called myData
test_struct myData;

// Callback function that will be executed when data is received
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  memcpy(&myData, incomingData, sizeof(myData));
  Serial.print("Bytes received: ");
  Serial.println(len);
  Serial.print("Object Temp: ");
  Serial.print(myData.f);
  Serial.println("°F");
  Serial.print("Gas: ");
  if (myData.gas) {
    Serial.println("Present!!!");
  } else {
    Serial.println("No");
  }
  if (myData.fingerOn) {
    Serial.print("BPM: ");
    Serial.println(myData.bpm);
  } else {
    Serial.println("No finger on");
  }
  Serial.println();

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.print(myData.f);
  display.cp437(true);
  display.write(248);
  display.print("F");
  display.setCursor(0, 20);
  if (myData.gas) {
    display.print("Gas Leak!");
  } else {
    display.print("No Gas...");
  }
  display.setCursor(0, 40);
  if (myData.fingerOn) {
    display.print("BPM: ");
    display.print(myData.bpm);
  } else {
    display.print("No finger");
  }
  display.display();
}
 
void setup() {
  // Initialize Serial Monitor
  Serial.begin(115200);
  Wire.begin(21,22);
  pinMode(buzzer, OUTPUT);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Once ESPNow is successfully Init, we will register for recv CB to
  // get recv packer info
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
}

void loop() {
  // Turn on buzzer alarm when a gas is detected
  if (myData.gas) {
    digitalWrite(buzzer, HIGH);
    delay(1000);
    digitalWrite(buzzer, LOW);
    delay(1000);
  } else {
    digitalWrite(buzzer, LOW);
  }
}
