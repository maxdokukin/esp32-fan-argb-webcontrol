#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h>
#include <driver/ledc.h>
#include <Preferences.h>

// ==== Wi-Fi Credentials ==== //
const char* ssid     = "The Grad Resident";
const char* password = "DecoratingLandsFace";

// ==== Pin & PWM Definitions ==== //
static constexpr int FAN1_PWM_PIN  = 0;
static constexpr int FAN2_PWM_PIN  = 1;
static constexpr int FAN1_TACH_PIN = 3;
static constexpr int FAN2_TACH_PIN = 4;
static constexpr int FAN1_LED_PIN  = 6;
static constexpr int FAN2_LED_PIN  = 7;

// ==== NeoPixel Settings ==== //
static constexpr int NUM_LEDS = 12;

// ==== PWM Settings ==== //
static constexpr int    PWM_FREQ     = 25000; 
static constexpr int    PWM_RES_BITS = 8;     
static constexpr uint8_t FAN_MIN_PWM = 50;    

// ==== Tachometer & RPM Settings ==== //
static constexpr unsigned long TACH_SAMPLE_TIME_MS = 1000;
static constexpr int           PULSES_PER_REV      = 2;

// ==== Globals ==== //
WebServer        server(80);
Adafruit_NeoPixel strip1(NUM_LEDS, FAN1_LED_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip2(NUM_LEDS, FAN2_LED_PIN, NEO_GRB + NEO_KHZ800);

volatile unsigned long tachCount1 = 0;
volatile unsigned long tachCount2 = 0;
unsigned long lastTachTime = 0;
uint16_t    rawRPM1       = 0;
uint16_t    rawRPM2       = 0;

// ==== NVS (Preferences) ==== //
Preferences prefs;
static constexpr char PREF_NAMESPACE[] = "fanled";
static constexpr char KEY_F1[] = "fan1_spd";
static constexpr char KEY_F2[] = "fan2_spd";
static constexpr char KEY_C1[] = "led1_col";
static constexpr char KEY_C2[] = "led2_col";

// ==== HTML Interface (with fetch('/initial') to pull in real values) ==== //
const char MAIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32 Dual Fan & LED Control</title>
<style>
  body{font-family:Arial;text-align:center;margin:20px}
  .control{margin:20px}
  .slider{width:80%}
  .colorpicker{width:80px;height:40px;border:none;cursor:pointer}
  #rpmChart{width:90%;max-width:800px;height:300px;margin:20px auto}
</style>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head><body>
<h1>ESP32 Dual Fan & LED Control</h1>

<div class="control"><h2>Fan 1</h2>
  <label>Speed: <span id="f1val">0</span>%</label><br>
  <input id="f1" class="slider" type="range" min="0" max="100" value="0"><br>
  <label>RPM: <span id="r1">0</span></label><br>
  <label>LED 1 Color:</label><br>
  <input id="c1" class="colorpicker" type="color" value="#ffffff">
</div>

<div class="control"><h2>Fan 2</h2>
  <label>Speed: <span id="f2val">0</span>%</label><br>
  <input id="f2" class="slider" type="range" min="0" max="100" value="0"><br>
  <label>RPM: <span id="r2">0</span></label><br>
  <label>LED 2 Color:</label><br>
  <input id="c2" class="colorpicker" type="color" value="#ffffff">
</div>

<canvas id="rpmChart"></canvas>

<script>
document.addEventListener('DOMContentLoaded',()=>{
  const f1 = document.getElementById('f1'),
        f2 = document.getElementById('f2'),
        c1 = document.getElementById('c1'),
        c2 = document.getElementById('c2');

  // **Fetch stored values and immediately apply them to the UI**
  fetch('/initial')
    .then(resp => resp.json())
    .then(cfg => {
      f1.value = cfg.fan1;
      f2.value = cfg.fan2;
      c1.value = '#' + cfg.col1.toString(16).padStart(6,'0');
      c2.value = '#' + cfg.col2.toString(16).padStart(6,'0');
      document.getElementById('f1val').innerText = f1.value;
      document.getElementById('f2val').innerText = f2.value;
    });

  // On-change handlers
  f1.oninput = ()=> {
    document.getElementById('f1val').innerText = f1.value;
    fetch('/fan1?value=' + f1.value);
  };
  f2.oninput = ()=> {
    document.getElementById('f2val').innerText = f2.value;
    fetch('/fan2?value=' + f2.value);
  };
  c1.onchange = ()=> fetch('/color1?value=' + c1.value.substring(1));
  c2.onchange = ()=> fetch('/color2?value=' + c2.value.substring(1));

  // RPM chart setup
  const ctx = document.getElementById('rpmChart').getContext('2d'),
        maxPts = 60;
  let labels = Array(maxPts).fill(''),
      data1  = Array(maxPts).fill(0),
      data2  = Array(maxPts).fill(0);

  const chart = new Chart(ctx, {
    type:'line',
    data:{
      labels,
      datasets:[
        {label:'Fan 1 RPM', data:data1, borderColor:'red', fill:false},
        {label:'Fan 2 RPM', data:data2, borderColor:'blue', fill:false},
      ]
    },
    options:{
      animation:false,
      scales:{ x:{display:false}, y:{beginAtZero:true} }
    }
  });

  setInterval(()=>{
    const now = new Date().toLocaleTimeString();
    Promise.all([
      fetch('/rpm1').then(r=>r.text()),
      fetch('/rpm2').then(r=>r.text())
    ]).then(vals => {
      const v1 = parseInt(vals[0]),
            v2 = parseInt(vals[1]);
      labels.push(now); data1.push(v1); data2.push(v2);
      if(labels.length>maxPts){
        labels.shift(); data1.shift(); data2.shift();
      }
      chart.update();
      document.getElementById('r1').innerText = v1;
      document.getElementById('r2').innerText = v2;
    });
  }, 1000);
});
</script>
</body></html>
)rawliteral";

// ==== ISRs ==== //
void IRAM_ATTR tachISR1() { tachCount1++; }
void IRAM_ATTR tachISR2() { tachCount2++; }

// ==== HTTP Handlers ==== //
void handleRoot() {
  server.send_P(200, "text/html", MAIN_HTML);
}

// Return JSON with stored fan speeds & colors
void handleInitial() {
  uint16_t f1 = prefs.getUInt(KEY_F1, 0);
  uint16_t f2 = prefs.getUInt(KEY_F2, 0);
  uint32_t c1 = prefs.getUInt(KEY_C1, 0xFFFFFF);
  uint32_t c2 = prefs.getUInt(KEY_C2, 0xFFFFFF);
  String js = "{";
    js += "\"fan1\":" + String(f1) + ",";
    js += "\"fan2\":" + String(f2) + ",";
    js += "\"col1\":" + String(c1) + ",";
    js += "\"col2\":" + String(c2);
  js += "}";
  server.send(200, "application/json", js);
}

void handleFan1() {
  int val = server.arg("value").toInt();
  uint8_t duty = val==0 ? 0 : map(val,1,100,FAN_MIN_PWM,(1<<PWM_RES_BITS)-1);
  ledcWrite(FAN1_PWM_PIN, duty);
  prefs.putUInt(KEY_F1, (uint16_t)val);
  server.send(200, "text/plain", "OK");
}

void handleFan2() {
  int val = server.arg("value").toInt();
  uint8_t duty = val==0 ? 0 : map(val,1,100,FAN_MIN_PWM,(1<<PWM_RES_BITS)-1);
  ledcWrite(FAN2_PWM_PIN, duty);
  prefs.putUInt(KEY_F2, (uint16_t)val);
  server.send(200, "text/plain", "OK");
}

void handleColor1() {
  uint32_t c = strtoul(server.arg("value").c_str(), nullptr, 16);
  for(int i=0;i<NUM_LEDS;i++) strip1.setPixelColor(i, c);
  strip1.show();
  prefs.putUInt(KEY_C1, c);
  server.send(200,"text/plain","OK");
}

void handleColor2() {
  uint32_t c = strtoul(server.arg("value").c_str(), nullptr, 16);
  for(int i=0;i<NUM_LEDS;i++) strip2.setPixelColor(i, c);
  strip2.show();
  prefs.putUInt(KEY_C2, c);
  server.send(200,"text/plain","OK");
}

void handleRPM1() {
  server.send(200, "text/plain", String(rawRPM1));
}

void handleRPM2() {
  server.send(200, "text/plain", String(rawRPM2));
}

// Apply NVS-stored settings on startup
void applyStoredSettings() {
  uint16_t f1 = prefs.getUInt(KEY_F1, 0);
  uint16_t f2 = prefs.getUInt(KEY_F2, 0);
  uint8_t  d1 = f1==0 ? 0 : map(f1,1,100,FAN_MIN_PWM,(1<<PWM_RES_BITS)-1);
  uint8_t  d2 = f2==0 ? 0 : map(f2,1,100,FAN_MIN_PWM,(1<<PWM_RES_BITS)-1);
  ledcWrite(FAN1_PWM_PIN, d1);
  ledcWrite(FAN2_PWM_PIN, d2);

  uint32_t c1 = prefs.getUInt(KEY_C1, 0xFFFFFF);
  uint32_t c2 = prefs.getUInt(KEY_C2, 0xFFFFFF);
  for(int i=0;i<NUM_LEDS;i++){
    strip1.setPixelColor(i, c1);
    strip2.setPixelColor(i, c2);
  }
  strip1.show();
  strip2.show();
}

void setup() {
  Serial.begin(115200);

  // NVS
  prefs.begin(PREF_NAMESPACE, false);

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED) delay(500);
  Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());

  // PWM channels
  ledcAttach(FAN1_PWM_PIN, PWM_FREQ, PWM_RES_BITS);
  ledcAttach(FAN2_PWM_PIN, PWM_FREQ, PWM_RES_BITS);

  // Tachometer ISRs
  pinMode(FAN1_TACH_PIN, INPUT_PULLUP);
  pinMode(FAN2_TACH_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(FAN1_TACH_PIN), tachISR1, FALLING);
  attachInterrupt(digitalPinToInterrupt(FAN2_TACH_PIN), tachISR2, FALLING);
  lastTachTime = millis();

  // LED strips
  strip1.begin(); strip1.show();
  strip2.begin(); strip2.show();

  // Apply any previously stored speeds/colors
  applyStoredSettings();

  // HTTP routes
  server.on("/",         handleRoot);
  server.on("/initial",  handleInitial);
  server.on("/fan1",     handleFan1);
  server.on("/fan2",     handleFan2);
  server.on("/color1",   handleColor1);
  server.on("/color2",   handleColor2);
  server.on("/rpm1",     handleRPM1);
  server.on("/rpm2",     handleRPM2);
  server.begin();
}

void loop() {
  unsigned long now = millis();
  if (now - lastTachTime >= TACH_SAMPLE_TIME_MS) {
    noInterrupts();
      unsigned long c1 = tachCount1;
      unsigned long c2 = tachCount2;
      tachCount1 = tachCount2 = 0;
    interrupts();

    rawRPM1 = (uint16_t)((c1 * 60000UL) / (TACH_SAMPLE_TIME_MS * PULSES_PER_REV));
    rawRPM2 = (uint16_t)((c2 * 60000UL) / (TACH_SAMPLE_TIME_MS * PULSES_PER_REV));
    lastTachTime = now;
  }

  server.handleClient();
}
