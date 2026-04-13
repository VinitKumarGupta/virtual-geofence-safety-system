#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>

// =================== 1. SETTINGS ===================
const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
const char* pc_ip = "10.114.170.XX"; // <--- REPLACE WITH YOUR LAPTOP IP
const int pc_port = 1234;

WiFiUDP udp;

// =================== 2. PIN DEFINITIONS (AI THINKER) ===================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
  Serial.begin(115200);

  // Mapping camera data pins to ESP32 pins
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;        // Camera Clock Speed. Controls capture speed (Higher clock => faster capture)
  config.pixel_format = PIXFORMAT_JPEG;  // Image is compressed directly as JPEG.

  /*
  Without compression:
  320x240 raw image ≈ 76 KB
  JPEG compressed ≈ 5-15 KB
  
  Huge speed improvement.
  */
  
  // LOWER RES FOR SPEED (UDP packet size limit is ~1400 bytes usually)
  config.frame_size = FRAMESIZE_QVGA; // 320x240 (Small image = fast transmission)
  config.jpeg_quality = 12; // JPEG compression quality (Lower number = Higher quality)
  config.fb_count = 2;  // Number of image buffers stored in memory

  /*
  2 buffers allow:
  capture next frame while previous is being processed.
  Improves FPS.
  */

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera Init Failed");
    return;
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
}

void loop() {
  // A frame buffer is simply a block of memory that stores the captured image.
  camera_fb_t * fb = esp_camera_fb_get(); // capture one image from the camera

  /*
  - fb stores the address of image data in memory
  - Microcontrollers avoid copying big arrays because memory is limited.
  */

  // If capture fails, we skip this loop iteration
  if (!fb) return;

  // PROTOCOL: [Frame ID (1 byte)] [Chunk Index (1 byte)] [Total Chunks (1 byte)] [Data...]
  // We use a static byte that rolls over 0-255 automatically
  static uint8_t frameId = 0; 
  frameId++; 

  const int max_payload = 1400;   // Since UDP safe packets size is nearly 1400 bytes, larger packets may get dropped
  int total_len = fb->len;  // tells how big the JPEG image is
  int num_chunks = (total_len + max_payload - 1) / max_payload;  // Formula for splitting data
  
  // 3 bytes Header
  uint8_t header[3]; 
  
  for (int i = 0; i < num_chunks; i++) {
    int start = i * max_payload;
    int len = (start + max_payload > total_len) ? (total_len - start) : max_payload;  // ensures last chunk doesnt exceed image size
    
    header[0] = frameId;        // Identifies which frame this chunk belongs to
    header[1] = i;              // Chunk Index
    header[2] = num_chunks;     // Total Chunks
    
    udp.beginPacket(pc_ip, pc_port);
    udp.write(header, 3);
    udp.write(fb->buf + start, len);
    udp.endPacket();
    
    // Tiny delay to breathe
    delayMicroseconds(3000);   // Prevents network overload
  }

  esp_camera_fb_return(fb);    // Prevents memory leak by returning memory back to the system
  
  delay(100); // controls frame rate
}
