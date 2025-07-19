#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <driver/ledc.h>

// ==== User Configurable WiFi Credentials ==== //
const char* ssid     = "The Grad Resident";
const char* password = "DecoratingLandsFace";

// ==== Pin & PWM Definitions ==== //
static constexpr int FAN_PWM_PIN    = 6;    // GPIO 6 for PWM control
static constexpr int FAN_TACH_PIN   = 5;    // GPIO 5 for tachometer input
static constexpr int LED_STRIP_PIN  = 9;    // GPIO 9 for LED strip
static constexpr int NUM_LEDS       = 10;
static constexpr int PWM_FREQ       = 25000; // 25 kHz PWM frequency
static constexpr int PWM_RES_BITS   = 8;     // 8-bit PWM resolution

// ==== PWM Mapping (0%–100% slider maps to 15%–100% duty) ==== //
static constexpr uint8_t FAN_MIN_PERCENT = 15;  // hard minimum duty cycle
static constexpr uint8_t FAN_MIN_PWM     = (FAN_MIN_PERCENT * ((1 << PWM_RES_BITS) - 1)) / 100;
static constexpr uint8_t FAN_MAX_PWM     = (1 << PWM_RES_BITS) - 1;  // maximum duty cycle

WebServer server(80);
Adafruit_NeoPixel strip(NUM_LEDS, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// ==== State ==== //
uint8_t fanSpeed = 0;            // current PWM value (0–255)
uint32_t ledColor = 0xFFFFFF;    // current LED color

// ==== Tachometer Variables ==== //
volatile uint16_t tachPulseCount = 0;      // counts valid pulses in ISR
volatile unsigned long lastTachTime = 0;   // last pulse timestamp (µs)
static constexpr unsigned long TACH_DEBOUNCE_US = 2000;  // debounce threshold
uint16_t fanRPM = 0;                        // last calculated RPM
unsigned long lastRPMTime = 0;              // last RPM update timestamp (ms)

// ==== Main Control Page ==== //
const char MAIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32-C3 Fan & LED Control</title>
  <style>
    body { font-family: Arial, sans-serif; text-align:center; margin-top:50px; }
    .slider { width:80%; }
    .colorpicker { width:80px; height:40px; }
  </style>
</head>
<body>
  <h1>ESP32-C3 Fan & LED Control</h1>
  <div>
    <label for="fanRange">Fan Speed (%): <span id="fanspeedval">0</span>%</label><br>
    <input type="range" id="fanRange" class="slider" min="0" max="100" value="0"
           oninput="updateFan(this.value)">
  </div>
  <div style="margin-top:20px;">
    <label>Fan RPM: <span id="rpmval">0</span></label>
  </div>
  <div style="margin-top:30px;">
    <label for="colorPicker">LED Color:</label><br>
    <input type="color" id="colorPicker" class="colorpicker" value="#ffffff"
           onchange="updateColor(this.value)">
  </div>
<script>
function updateFan(val) {
  document.getElementById('fanspeedval').innerText = val;
  fetch(`/fan?value=${val}`);
}
function updateColor(col) {
  const hex = col.substring(1);
  fetch(`/color?value=${hex}`);
}
// periodically fetch the actual RPM
setInterval(() => {
  fetch('/rpm')
    .then(res => res.text())
    .then(val => document.getElementById('rpmval').innerText = val);
}, 1000);
</script>
</body>
</html>
)rawliteral";

// ==== HTTP Handlers ==== //
void handleRoot() {
  server.send_P(200, "text/html", MAIN_HTML);
}

void handleFan() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  int pct = server.arg("value").toInt();
  // map 0–100% into 15–100% duty cycle
  fanSpeed = map(pct, 0, 100, FAN_MIN_PWM, FAN_MAX_PWM);
  ledcWrite(FAN_PWM_PIN, fanSpeed);
  server.send(200, "text/plain", "OK");
}

void handleColor() {
  if (!server.hasArg("value")) {
    server.send(400, "text/plain", "Missing value");
    return;
  }
  String h = server.arg("value");
  uint32_t c = strtoul(h.c_str(), nullptr, 16);
  ledColor = c;
  for (int i = 0; i < NUM_LEDS; i++) strip.setPixelColor(i, ledColor);
  strip.show();
  server.send(200, "text/plain", "OK");
}

void handleRPM() {
  server.send(200, "text/plain", String(fanRPM));
}

// Tachometer ISR with debounce
void IRAM_ATTR onTachPulse() {
  unsigned long now = micros();
  if (now - lastTachTime > TACH_DEBOUNCE_US) {
    tachPulseCount++;
    lastTachTime = now;
  }
}

// ==== Setup & Loop ==== //
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(500);

  // configure PWM for fan
  ledcAttach(FAN_PWM_PIN, PWM_FREQ, PWM_RES_BITS);
  ledcWrite(FAN_PWM_PIN, fanSpeed);

  // configure tachometer input
  pinMode(FAN_TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), onTachPulse, FALLING);

  // initialize WS2812 strip
  strip.begin(); strip.show();

  // setup routes
  server.on("/",      handleRoot);
  server.on("/fan",   handleFan);
  server.on("/color", handleColor);
  server.on("/rpm",   handleRPM);
  server.begin();
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastRPMTime >= 1000) {
    noInterrupts();
    uint16_t pulses = tachPulseCount;
    tachPulseCount = 0;
    interrupts();
    // two pulses per rev -> RPM = pulses/sec * 60 / 2
    fanRPM = pulses * 30;
    lastRPMTime = currentMillis;
  }
  server.handleClient();
}
