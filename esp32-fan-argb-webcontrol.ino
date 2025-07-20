#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <driver/ledc.h>

// ==== User Configurable WiFi Credentials ==== //
const char* ssid     = "The Grad Resident";
const char* password = "DecoratingLandsFace";

// ==== Pin & PWM Definitions ==== //
static constexpr int FAN1_PWM_PIN    = 0;  // GPIO for Fan 1 PWM control
static constexpr int FAN2_PWM_PIN    = 1;  // GPIO for Fan 2 PWM control
static constexpr int FAN1_TACH_PIN   = 3;  // GPIO for Fan 1 tachometer input
static constexpr int FAN2_TACH_PIN   = 4;  // GPIO for Fan 2 tachometer input
static constexpr int FAN1_LED_PIN    = 6;  // GPIO for Fan 1 NeoPixel strip
static constexpr int FAN2_LED_PIN    = 7;  // GPIO for Fan 2 NeoPixel strip

// ==== Constants ==== //
static constexpr int    FAN_MIN_PWM           = 20;       // 15% duty
static constexpr int    NUM_LEDS              = 12;       // LEDs per strip
static constexpr int    PWM_FREQ              = 25000;    // PWM frequency
static constexpr int    PWM_RES_BITS          = 8;        // PWM resolution
static constexpr unsigned long TACH_DEBOUNCE_US       = 2000; // 2 ms debounce

// ==== Global Objects ==== //
WebServer        server(80);
Adafruit_NeoPixel strip1(NUM_LEDS, FAN1_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(NUM_LEDS, FAN2_LED_PIN, NEO_GRB + NEO_KHZ800);

// ==== State Variables ==== //
uint8_t  fan1Speed = 0, fan2Speed = 0;
uint32_t ledColor1 = 0xFFFFFF, ledColor2 = 0xFFFFFF;

// ==== Tachometer & RPM ==== //
volatile unsigned long lastTach1 = 0, lastTach2 = 0;
volatile unsigned int  tachCount1 = 0, tachCount2 = 0;
uint16_t rawRPM1 = 0, rawRPM2 = 0;

// ==== HTML Interface ==== //
const char MAIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Dual Fan & LED Control</title>
  <style>
    body { font-family: Arial; text-align: center; margin: 20px; }
    .control { margin: 20px; }
    .slider { width: 80%; }
    .colorpicker { width: 80px; height: 40px; }
    #rpmChart { width: 90%; max-width: 800px; height: 300px; margin: 20px auto; }
  </style>
  <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
  <h1>ESP32 Dual Fan & LED Control</h1>
  <div class="control">
    <h2>Fan 1</h2>
    <label>Speed: <span id="f1val">0</span>%</label><br>
    <input type="range" id="f1" class="slider" min="0" max="100" value="0"><br>
    <label>RPM: <span id="r1">0</span></label><br>
    <label>LED 1 Color:</label><br>
    <input type="color" id="c1" class="colorpicker" value="#ffffff">
  </div>
  <div class="control">
    <h2>Fan 2</h2>
    <label>Speed: <span id="f2val">0</span>%</label><br>
    <input type="range" id="f2" class="slider" min="0" max="100" value="0"><br>
    <label>RPM: <span id="r2">0</span></label><br>
    <label>LED 2 Color:</label><br>
    <input type="color" id="c2" class="colorpicker" value="#ffffff">
  </div>
  <canvas id="rpmChart"></canvas>
  <script>
    document.addEventListener('DOMContentLoaded', () => {
      document.getElementById('f1').oninput = function() {
        document.getElementById('f1val').innerText = this.value;
        fetch('/fan1?value=' + this.value);
      };
      document.getElementById('f2').oninput = function() {
        document.getElementById('f2val').innerText = this.value;
        fetch('/fan2?value=' + this.value);
      };
      document.getElementById('c1').onchange = function() {
        fetch('/color1?value=' + this.value.substring(1));
      };
      document.getElementById('c2').onchange = function() {
        fetch('/color2?value=' + this.value.substring(1));
      };

      const ctx = document.getElementById('rpmChart').getContext('2d');
      const maxPoints = 60;
      let labels = Array(maxPoints).fill('');
      let data1   = Array(maxPoints).fill(0);
      let data2   = Array(maxPoints).fill(0);

      const chart = new Chart(ctx, {
        type: 'line',
        data: {
          labels: labels,
          datasets: [
            { label: 'Fan 1 RPM', data: data1, borderColor: 'red',  fill: false },
            { label: 'Fan 2 RPM', data: data2, borderColor: 'blue', fill: false }
          ]
        },
        options: {
          animation: false,
          scales: {
            x: { display: false },
            y: { beginAtZero: true }
          }
        }
      });

      setInterval(() => {
        const now = new Date().toLocaleTimeString();
        Promise.all([
          fetch('/rpm1').then(r => r.text()),
          fetch('/rpm2').then(r => r.text())
        ]).then(vals => {
          const v1 = parseInt(vals[0]), v2 = parseInt(vals[1]);
          labels.push(now);
          data1.push(v1);
          data2.push(v2);
          if (labels.length > maxPoints) {
            labels.shift();
            data1.shift();
            data2.shift();
          }
          chart.update();
          document.getElementById('r1').innerText = v1;
          document.getElementById('r2').innerText = v2;
        });
      }, 1000);
    });
  </script>
</body>
</html>
)rawliteral";

// ==== ISRs: Count Pulses ==== //
void IRAM_ATTR onTach1() {
  unsigned long now = micros();
  if (now - lastTach1 > TACH_DEBOUNCE_US) {
    lastTach1 = now;
    tachCount1++;
  }
}

void IRAM_ATTR onTach2() {
  unsigned long now = micros();
  if (now - lastTach2 > TACH_DEBOUNCE_US) {
    lastTach2 = now;
    tachCount2++;
  }
}

// ==== HTTP Handlers ==== //
void handleRoot() {
  server.send_P(200, "text/html", MAIN_HTML);
}

void handleFan1() {
  int val = server.arg("value").toInt();  // 0–100
  uint8_t duty;
  if (val == 0) {
    duty = 0;
  } else {
    duty = map(val, 1, 100, FAN_MIN_PWM, (1 << PWM_RES_BITS) - 1);
  }
  ledcWrite(FAN1_PWM_PIN, duty);
  server.send(200, "text/plain", "OK");
}

void handleFan2() {
  int val = server.arg("value").toInt();  // 0–100
  uint8_t duty;
  if (val == 0) {
    duty = 0;
  } else {
    duty = map(val, 1, 100, FAN_MIN_PWM, (1 << PWM_RES_BITS) - 1);
  }
  ledcWrite(FAN2_PWM_PIN, duty);
  server.send(200, "text/plain", "OK");
}

void handleColor1() {
  uint32_t c = strtoul(server.arg("value").c_str(), nullptr, 16);
  ledColor1 = c;
  for (int i = 0; i < NUM_LEDS; i++) {
    strip1.setPixelColor(i, ledColor1);
  }
  strip1.show();
  server.send(200, "text/plain", "OK");
}

void handleColor2() {
  uint32_t c = strtoul(server.arg("value").c_str(), nullptr, 16);
  ledColor2 = c;
  for (int i = 0; i < NUM_LEDS; i++) {
    strip2.setPixelColor(i, ledColor2);
  }
  strip2.show();
  server.send(200, "text/plain", "OK");
}

void handleRPM1() {
  noInterrupts();
  uint16_t count = tachCount1;
  tachCount1 = 0;
  interrupts();
  rawRPM1 = count * 30;
  server.send(200, "text/plain", String(rawRPM1));
}

void handleRPM2() {
  noInterrupts();
  uint16_t count = tachCount2;
  tachCount2 = 0;
  interrupts();
  rawRPM2 = count * 30;
  server.send(200, "text/plain", String(rawRPM2));
}

// ==== Setup & Loop ==== //
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  Serial.printf("Connected! IP address: %s\n", WiFi.localIP().toString().c_str());

  // PWM setup
  ledcAttach(FAN1_PWM_PIN, PWM_FREQ, PWM_RES_BITS);
  ledcAttach(FAN2_PWM_PIN, PWM_FREQ, PWM_RES_BITS);

  // Tach inputs
  pinMode(FAN1_TACH_PIN, INPUT_PULLUP);
  pinMode(FAN2_TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN1_TACH_PIN), onTach1, FALLING);
  attachInterrupt(digitalPinToInterrupt(FAN2_TACH_PIN), onTach2, FALLING);

  // LEDs
  strip1.begin();
  strip1.show();
  strip2.begin();
  strip2.show();

  // HTTP routes
  server.on("/",      handleRoot);
  server.on("/fan1",  handleFan1);
  server.on("/fan2",  handleFan2);
  server.on("/color1", handleColor1);
  server.on("/color2", handleColor2);
  server.on("/rpm1",  handleRPM1);
  server.on("/rpm2",  handleRPM2);
  server.begin();
}

void loop() {
  server.handleClient();
}
