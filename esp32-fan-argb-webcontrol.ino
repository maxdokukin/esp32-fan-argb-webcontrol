#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <driver/ledc.h>

// ==== User Configurable WiFi Credentials ==== //
const char* ssid     = "The Grad Resident";
const char* password = "DecoratingLandsFace";

// ==== Pin & PWM Definitions ==== //
static constexpr int FAN_PWM_PIN    =  6;    // GPIO 3 for PWM control
static constexpr int FAN_TACH_PIN   =  5;    // GPIO 4 for tachometer input
static constexpr int LED_STRIP_PIN  =  9;    // GPIO 2 for LED strip
static constexpr int NUM_LEDS       =  10;
static constexpr int PWM_FREQ       = 25000; // 25 kHz PWM frequency
static constexpr int PWM_RES_BITS   =     8; // 8-bit PWM resolution

WebServer        server(80);
Adafruit_NeoPixel strip(NUM_LEDS, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// ==== Calibration & State ==== //
uint8_t fanMinPWM   =   0;   // calibrated minimum
uint8_t fanMaxPWM   = 255;   // calibrated maximum
uint8_t rawFanPWM   =   0;   // for raw PWM slider
uint8_t fanSpeed    =   0;   // last mapped speed (0–255)
uint32_t ledColor   = 0xFFFFFF;

// ==== Tachometer Variables ==== //
volatile uint16_t tachPulseCount = 0;  // counts pulses in ISR
uint16_t fanRPM                = 0;  // last calculated RPM
unsigned long lastRPMTime      = 0;  // last time RPM was updated

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
    .link { margin-top:20px; }
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
  <div class="link">
    <a href="/calibrate">Calibrate Fan</a>
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
    .then(val => {
      document.getElementById('rpmval').innerText = val;
    });
}, 1000);
</script>
</body>
</html>
)rawliteral";

// ==== Calibration Page ==== //
const char CAL_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Fan Calibration</title>
  <style>
    body { font-family: Arial, sans-serif; text-align:center; margin-top:50px; }
    .slider { width:80%; }
    .back { margin-top:20px; }
  </style>
</head>
<body>
  <h1>Fan Calibration</h1>
  <p>
    <label>Min PWM: <span id="minVal">%MIN%</span></label><br>
    <input type="range" id="minSlider" class="slider" min="0" max="255" value="%MIN%"
           oninput="updateMin(this.value)">
  </p>
  <p>
    <label>Max PWM: <span id="maxVal">%MAX%</span></label><br>
    <input type="range" id="maxSlider" class="slider" min="0" max="255" value="%MAX%"
           oninput="updateMax(this.value)">
  </p>
  <hr>
  <p>
    <label>Raw PWM: <span id="rawVal">%RAW%</span></label><br>
    <input type="range" id="rawSlider" class="slider" min="0" max="255" value="%RAW%"
           oninput="updateRaw(this.value)">
  </p>
  <div class="back">
    <a href="/">Back to Control</a>
  </div>
<script>
function updateMin(v) {
  document.getElementById('minVal').innerText = v;
  fetch(`/setMin?value=${v}`);
}
function updateMax(v) {
  document.getElementById('maxVal').innerText = v;
  fetch(`/setMax?value=${v}`);
}
function updateRaw(v) {
  document.getElementById('rawVal').innerText = v;
  fetch(`/setRaw?value=${v}`);
}
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
  fanSpeed = map(pct, 0, 100, fanMinPWM, fanMaxPWM);
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
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, ledColor);
  }
  strip.show();
  server.send(200, "text/plain", "OK");
}
void handleCalibrate() {
  String page = FPSTR(CAL_HTML);
  page.replace("%MIN%", String(fanMinPWM));
  page.replace("%MAX%", String(fanMaxPWM));
  page.replace("%RAW%", String(rawFanPWM));
  server.send(200, "text/html", page);
}
void handleSetMin() {
  if (server.hasArg("value")) {
    fanMinPWM = constrain(server.arg("value").toInt(), 0, 255);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing value");
  }
}
void handleSetMax() {
  if (server.hasArg("value")) {
    fanMaxPWM = constrain(server.arg("value").toInt(), 0, 255);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing value");
  }
}
void handleSetRaw() {
  if (server.hasArg("value")) {
    rawFanPWM = constrain(server.arg("value").toInt(), 0, 255);
    ledcWrite(FAN_PWM_PIN, rawFanPWM);
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "Missing value");
  }
}

// Tachometer ISR
void IRAM_ATTR onTachPulse() {
  tachPulseCount++;
}

// Handler for RPM
void handleRPM() {
  server.send(200, "text/plain", String(fanRPM));
}

// ==== Setup & Loop ==== //
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // configure PWM for fan
  ledcAttach(FAN_PWM_PIN, PWM_FREQ, PWM_RES_BITS);
  ledcWrite(FAN_PWM_PIN, fanSpeed);

  // configure tachometer input
  pinMode(FAN_TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN_TACH_PIN), onTachPulse, FALLING);

  // initialize WS2812 strip
  strip.begin();
  strip.show();

  // setup routes
  server.on("/",         handleRoot);
  server.on("/fan",      handleFan);
  server.on("/color",    handleColor);
  server.on("/calibrate",handleCalibrate);
  server.on("/setMin",   handleSetMin);
  server.on("/setMax",   handleSetMax);
  server.on("/setRaw",   handleSetRaw);
  server.on("/rpm",      handleRPM);
  server.begin();
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastRPMTime >= 1000) {
    noInterrupts();
    uint16_t pulses = tachPulseCount;
    tachPulseCount = 0;
    interrupts();
    fanRPM = pulses * 30; // 2 pulses per revolution
    lastRPMTime = currentMillis;
  }
  server.handleClient();
}
