#include <WiFi.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h>

// ================= CONFIGURATION =================
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
const int localPort = 3333;  // runs on port 3333

// ================= PIN DEFINITIONS =================
#define RELAY_PIN   14
#define SERVO_PIN   13
#define LED_PIN     33
#define BUZZER_PIN  27
#define BUZZER_CHANNEL 2
#define BUZZER_FREQ    2000   // Alarm tone
#define BUZZER_RES     8

Servo myservo;
WiFiUDP udp;
char packetBuffer[255];
bool isEmergency = false;

void setup() {
  Serial.begin(115200);
  
  pinMode(RELAY_PIN, OUTPUT);
  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, BUZZER_RES);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
  pinMode(LED_PIN, OUTPUT);
  
  myservo.attach(SERVO_PIN);
  
  // --- INITIAL SAFE STATE ---
  digitalWrite(RELAY_PIN, HIGH); // Motor ON (if NC wired)
  digitalWrite(LED_PIN, LOW);    // LED Off
  ledcWrite(BUZZER_CHANNEL, 0);   // silence
  
  // Servo starts at 110 degrees (Safe Position)
  myservo.write(110);             
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
  udp.begin(localPort);
}

void loop() {
  // If Emergency is triggered, we need to PULSE the buzzer
  if (isEmergency) {
    static unsigned long lastToggle = 0;
    static bool buzzerOn = false;

    if (millis() - lastToggle > 200) {
      lastToggle = millis();
      buzzerOn = !buzzerOn;

      if (buzzerOn) {
        ledcWrite(BUZZER_CHANNEL, 128);   // ON (50% duty)
      } else {
        ledcWrite(BUZZER_CHANNEL, 0);     // OFF
      }
    }
    return;
  }

  int packetSize = udp.parsePacket();
  if (packetSize) {
    int len = udp.read(packetBuffer, 255);
    if (len > 0) packetBuffer[len] = 0;
    String command = String(packetBuffer);
    
    if (command == "STOP") {
      triggerEmergency();
    }
  }
}

void triggerEmergency() {
  if (!isEmergency) {
    isEmergency = true;
    
    // 1. Cut Power
    digitalWrite(RELAY_PIN, LOW); 
    
    // 2. Push STOP (Move to 60 degrees)
    myservo.write(60); 
    
    // 3. Turn on Visual Alarm
    digitalWrite(LED_PIN, HIGH);
    
    // 4. Turn the buzzer on
    ledcWriteTone(BUZZER_CHANNEL, BUZZER_FREQ); // set frequency ONCE
  }
}
