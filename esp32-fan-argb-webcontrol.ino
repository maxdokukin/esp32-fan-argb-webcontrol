#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <driver/ledc.h>

// ==== User Configurable WiFi Credentials ==== //
const char* ssid     = "The Grad Resident";
const char* password = "DecoratingLandsFace";

// ==== Pin & PWM Definitions ==== //
static constexpr int FAN1_TACH_PIN   = 5;   // GPIO for Fan 1 tachometer input
static constexpr int FAN1_PWM_PIN    = 6;   // GPIO for Fan 1 PWM control
static constexpr int FAN2_TACH_PIN   = 7;   // GPIO for Fan 2 tachometer input
static constexpr int FAN2_PWM_PIN    = 8;   // GPIO for Fan 2 PWM control
static constexpr int FAN1_LED_PIN    = 9;   // GPIO for Fan 1 LED strip
static constexpr int FAN2_LED_PIN    = 10;  // GPIO for Fan 2 LED strip

// ==== Constants ==== //
static constexpr int FAN_MIN_PWM     = 20;      // 15% of 256 approx
static constexpr int NUM_LEDS        = 12;      // LEDs per strip
static constexpr int PWM_FREQ        = 25000;   // 25 kHz PWM frequency
static constexpr int PWM_RES_BITS    = 8;       // 8-bit PWM resolution
static constexpr unsigned long TACH_DEBOUNCE_US = 2000; // 2 ms debounce

WebServer server(80);
Adafruit_NeoPixel strip1(NUM_LEDS, FAN1_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(NUM_LEDS, FAN2_LED_PIN, NEO_GRB + NEO_KHZ800);

// ==== State ==== //
uint8_t fan1Speed = 0, fan2Speed = 0;
uint32_t ledColor1 = 0xFFFFFF, ledColor2 = 0xFFFFFF;

// ==== Tachometer Variables ==== //
volatile uint16_t tachCount1 = 0, tachCount2 = 0;
volatile unsigned long lastTach1 = 0, lastTach2 = 0;
uint16_t fan1RPM = 0, fan2RPM = 0;
unsigned long lastRPMTime = 0;

// ==== HTML Interface ==== //
const char MAIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Dual Fan & LED Control</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 30px; }
    .slider { width: 80%; }
    .colorpicker { width: 80px; height: 40px; }
  </style>
</head>
<body>
  <h1>ESP32 Dual Fan & LED Control</h1>
  <!-- Fan 1 controls -->
  <div>
    <h2>Fan 1</h2>
    <label>Speed: <span id="f1val">0</span>%</label><br>
    <input type="range" id="f1" class="slider" min="0" max="100" value="0" oninput="updateF1(this.value)"><br>
    <label>RPM: <span id="r1">0</span></label>
    <div style="margin-top:20px;">
      <label for="c1">LED 1 Color:</label><br>
      <input type="color" id="c1" class="colorpicker" value="#ffffff" onchange="updateColor1(this.value)">
    </div>
  </div>
  <!-- Fan 2 controls -->
  <div style="margin-top:30px;">
    <h2>Fan 2</h2>
    <label>Speed: <span id="f2val">0</span>%</label><br>
    <input type="range" id="f2" class="slider" min="0" max="100" value="0" oninput="updateF2(this.value)"><br>
    <label>RPM: <span id="r2">0</span></label>
    <div style="margin-top:20px;">
      <label for="c2">LED 2 Color:</label><br>
      <input type="color" id="c2" class="colorpicker" value="#ffffff" onchange="updateColor2(this.value)">
    </div>
  </div>
<script>
function updateF1(v) { document.getElementById('f1val').innerText = v; fetch(`/fan1?value=${v}`); }
function updateF2(v) { document.getElementById('f2val').innerText = v; fetch(`/fan2?value=${v}`); }
function updateColor1(c){ fetch(`/color1?value=${c.substring(1)}`); }
function updateColor2(c){ fetch(`/color2?value=${c.substring(1)}`); }
setInterval(() => {
  fetch('/rpm1').then(r => r.text()).then(t => document.getElementById('r1').innerText = t);
  fetch('/rpm2').then(r => r.text()).then(t => document.getElementById('r2').innerText = t);
}, 1000);
</script>
</body>
</html>
)rawliteral";

// ==== ISRs ==== //
void IRAM_ATTR onTach1() { unsigned long now = micros(); if (now - lastTach1 > TACH_DEBOUNCE_US) { tachCount1++; lastTach1 = now; } }
void IRAM_ATTR onTach2() { unsigned long now = micros(); if (now - lastTach2 > TACH_DEBOUNCE_US) { tachCount2++; lastTach2 = now; } }

// ==== HTTP Handlers ==== //
void handleRoot()   { server.send_P(200, "text/html", MAIN_HTML); }
void handleFan1()   { uint8_t pct = server.arg("value").toInt(); fan1Speed = map(pct, 0, 100, FAN_MIN_PWM, (1<<PWM_RES_BITS)-1); ledcWrite(FAN1_PWM_PIN, fan1Speed); server.send(200, "text/plain", "OK"); }
void handleFan2()   { uint8_t pct = server.arg("value").toInt(); fan2Speed = map(pct, 0, 100, FAN_MIN_PWM, (1<<PWM_RES_BITS)-1); ledcWrite(FAN2_PWM_PIN, fan2Speed); server.send(200, "text/plain", "OK"); }
void handleColor1() { uint32_t c = strtoul(server.arg("value").c_str(), nullptr, 16); ledColor1 = c; for (int i = 0; i < NUM_LEDS; i++) strip1.setPixelColor(i, ledColor1); strip1.show(); server.send(200, "text/plain", "OK"); }
void handleColor2() { uint32_t c = strtoul(server.arg("value").c_str(), nullptr, 16); ledColor2 = c; for (int i = 0; i < NUM_LEDS; i++) strip2.setPixelColor(i, ledColor2); strip2.show(); server.send(200, "text/plain", "OK"); }
void handleRPM1()   { server.send(200, "text/plain", String(fan1RPM)); }
void handleRPM2()   { server.send(200, "text/plain", String(fan2RPM)); }

// ==== Setup & Loop ==== //
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  // PWM outputs
  ledcAttach(FAN1_PWM_PIN, PWM_FREQ, PWM_RES_BITS);
  ledcAttach(FAN2_PWM_PIN, PWM_FREQ, PWM_RES_BITS);
  ledcWrite(FAN1_PWM_PIN, fan1Speed);
  ledcWrite(FAN2_PWM_PIN, fan2Speed);

  // Tach inputs
  pinMode(FAN1_TACH_PIN, INPUT_PULLUP);
  pinMode(FAN2_TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN1_TACH_PIN), onTach1, FALLING);
  attachInterrupt(digitalPinToInterrupt(FAN2_TACH_PIN), onTach2, FALLING);

  // LED strips
  strip1.begin(); strip1.show();
  strip2.begin(); strip2.show();

  // HTTP routes
  server.on("/",    handleRoot);
  server.on("/fan1",   handleFan1);
  server.on("/fan2",   handleFan2);
  server.on("/color1", handleColor1);
  server.on("/color2", handleColor2);
  server.on("/rpm1",   handleRPM1);
  server.on("/rpm2",   handleRPM2);
  server.begin();
}

void loop() {
  unsigned long m = millis();
  if (m - lastRPMTime >= 1000) {
    noInterrupts();
    uint16_t c1 = tachCount1, c2 = tachCount2;
    tachCount1 = tachCount2 = 0;
    interrupts();
    fan1RPM = c1 * 30;
    fan2RPM = c2 * 30;
    lastRPMTime = m;
  }
  server.handleClient();
}
