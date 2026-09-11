/*
  ╔══════════════════════════════════════════════════════════════╗
  ║           EcoFarm AI — Complete Master Sketch                ║
  ║           Arduino UNO Q (STM32 + Qualcomm MPU)                ║
  ║                                                                ║
  ║  WHAT THIS CODE DOES:                                         ║
  ║  1. Monitors 3-zone soil moisture -> auto pump control         ║
  ║  2. Rain sensor -> pump OFF when raining (water saving)        ║
  ║  3. LDR -> tracks sunlight level                                ║
  ║  4. PIR -> field security alert (unauthorized entry)            ║
  ║  5. BMP280 -> air temperature + pressure                        ║
  ║  6. DS18B20 -> soil temperature                                 ║
  ║  7. DHT11 -> air humidity                                       ║
  ║  8. LED Matrix -> growth animation + scrolling live data         ║
  ║  9. LoRa RA-02 -> TX-only field alert broadcast (every 30s + on ║
  ║     DRY/MOTION events)                                          ║
  ║ 10. Green/Red LED + Buzzer -> visual + audio alerts               ║
  ║ 11. Manual override (pump + LED) from the App Lab web dashboard  ║
  ║ 12. Bridge -> exposes everything to python/main.py (dashboard)   ║
  ║                                                                ║
  ║  PIN MAP (matches wiring doc exactly):                          ║
  ║  A0 = Soil Zone 1        A1 = Soil Zone 2                      ║
  ║  A2 = Soil Zone 3        A3 = Rain Sensor (analog, unused read) ║
  ║  A4 = LDR (analog, unused read)                                  ║
  ║  D2 = Rain Digital       D3 = LDR Digital                       ║
  ║  D4 = PIR Motion         D5 = Relay (Pump)                      ║
  ║  D6 = Buzzer             D7 = DS18B20                            ║
  ║  D8 = DHT11              D9 = Green LED                         ║
  ║  D10 = Red LED           D11/D12/D13 = LoRa hardware SPI          ║
  ║  SS (D10 on this board's SPI header) = LoRa NSS                  ║
  ║  LoRa RESET/DIO0 = not wired (TX-only demo, library uses -1)      ║
  ╚══════════════════════════════════════════════════════════════╝
*/

#include "Arduino_LED_Matrix.h"
#include "Arduino_RouterBridge.h"
#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <string.h>
#include <stdio.h>

// ==================== PIN DEFINITIONS ====================
#define S1           A0
#define S2           A1
#define S3           A2
#define DRY          1023
#define WET          377

#define RAIN_ANALOG  A3
#define RAIN_DIGITAL 2

#define LDR_ANALOG   A4
#define LDR_DIGITAL  3

#define PIR_PIN      4
#define RELAY_PIN    5
#define BUZZER       6
#define ONE_WIRE_BUS 7
#define DHTPIN       8
#define LED_G        9
#define LED_R        10

// LoRa hardware SPI (D11=MOSI, D12=MISO, D13=SCK — automatic on this board)
#define LORA_NSS     SS   // board's dedicated SPI CS pin
#define LORA_RST     -1   // TX-only demo — reset not wired
#define LORA_DIO0    -1   // TX-only demo — dio0 not wired
#define LORA_FREQ    433E6 // CHANGE to 868E6 / 915E6 if your module is that variant

#define DHTTYPE      DHT11

// ==================== OBJECTS ====================
ArduinoLEDMatrix matrix;
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
Adafruit_BMP280 bmp;
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature soilTempSensor(&oneWire);
DHT dht(DHTPIN, DHTTYPE);

const int ROWS = 8, COLS = 13;

// ==================== VARIABLES ====================
int   zone1 = 0, zone2 = 0, zone3 = 0;
String systemStatus = "Starting...";
bool  pumpOn = false;

float temperatureC = 0.0;
float pressureHPa  = 0.0;
float humidityPct  = -1.0;
float soilTempC    = -127.0;

int   rainAnalog     = 0;
bool  isRaining      = false;
int   lightPercent   = 0;
bool  motionDetected = false;

bool bmpReady  = false;
bool ds18Ready = false;
bool dhtReady  = false;
bool oledReady = false;
bool loraReady = false;

volatile bool g_alertMode = false;
int loraPacketCount = 0;

// --- Manual override state (used by the App Lab dashboard) ---
bool manualMode      = false; // pump manual override
bool manualPumpState = false;
bool ledManualMode    = false; // LED manual override
bool ledManualState   = false;
bool redLedOn         = false;

// ==================== LED MATRIX FRAMES ====================
uint8_t seed[ROWS][COLS] = {
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,0,0,1,1,1,0,0,0,0,0},
};
uint8_t sprout[ROWS][COLS] = {
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,0,1,1,1,1,1,0,0,0,0},
  {0,0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,0,0,1,1,1,0,0,0,0,0},
};
uint8_t midPlant[ROWS][COLS] = {
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,1,1,1,1,1,1,1,0,0,0},
  {0,0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,0,1,1,1,1,1,0,0,0,0},
  {0,0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,0,0,1,1,1,0,0,0,0,0},
};
uint8_t fullPlant[ROWS][COLS] = {
  {0,0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,1,1,1,1,1,1,1,0,0,0},
  {0,1,1,1,0,0,1,0,0,1,1,1,0},
  {0,0,0,1,1,1,1,1,1,1,0,0,0},
  {0,0,1,1,0,0,1,0,0,1,1,0,0},
  {0,0,0,0,1,1,1,1,1,0,0,0,0},
  {0,0,0,0,0,0,1,0,0,0,0,0,0},
  {0,0,0,0,1,1,1,1,1,0,0,0,0},
};
uint8_t alertIcon[ROWS][COLS] = {
  {0,0,0,0,0,1,0,0,0,0,0,0,0},
  {0,0,0,0,1,1,1,0,0,0,0,0,0},
  {0,0,0,1,1,0,1,1,0,0,0,0,0},
  {0,0,1,1,0,1,0,1,1,0,0,0,0},
  {0,1,1,0,0,1,0,0,1,1,0,0,0},
  {0,1,1,0,0,1,0,0,1,1,0,0,0},
  {0,1,1,0,0,0,0,0,1,1,0,0,0},
  {1,1,1,1,1,1,1,1,1,1,1,0,0},
};
uint8_t rainIcon[ROWS][COLS] = {
  {0,0,1,1,1,1,1,1,1,1,0,0,0},
  {0,1,1,1,1,1,1,1,1,1,1,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,1,0,0,1,0,0,1,0,0,1,0,0},
  {0,0,1,0,0,1,0,0,1,0,0,1,0},
  {0,0,0,1,0,0,1,0,0,1,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
};
uint8_t motionIcon[ROWS][COLS] = {
  {0,0,0,1,1,0,0,0,0,0,0,0,0},
  {0,0,0,1,1,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,1,1,1,0,0,0},
  {0,1,1,1,1,1,0,0,1,0,0,0,0},
  {0,0,0,1,0,0,0,1,1,1,0,0,0},
  {0,0,1,0,1,0,0,0,0,0,0,0,0},
  {0,1,0,0,0,1,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0,0},
};

// ==================== FORWARD DECLARATIONS ====================
void checkSensorsAndPump();
void updateOLED();
void sendLoRaAlert(String msg);
void scrollText(const char *msg, int speedMs = 70);  // default arg lives HERE only

// ==================== MATRIX ANIMATIONS ====================
void playGrowthAnimation() {
  matrix.renderBitmap(seed, ROWS, COLS);      checkSensorsAndPump(); delay(400);
  matrix.renderBitmap(sprout, ROWS, COLS);    checkSensorsAndPump(); delay(400);
  matrix.renderBitmap(midPlant, ROWS, COLS);  checkSensorsAndPump(); delay(400);
  matrix.renderBitmap(fullPlant, ROWS, COLS); checkSensorsAndPump(); delay(800);
}

void playWaterDroplets() {
  int dropCols[3] = {3, 6, 9};
  for (int cycle = 0; cycle < 2; cycle++) {
    for (int row = 0; row < ROWS; row++) {
      uint8_t frame[ROWS][COLS] = {0};
      for (int d = 0; d < 3; d++) {
        int r = (row + d * 3) % ROWS;
        frame[r][dropCols[d]] = 1;
        if (r > 0) frame[r-1][dropCols[d]] = 1;
      }
      matrix.renderBitmap(frame, ROWS, COLS);
      checkSensorsAndPump();
      delay(120);
    }
  }
}

void playRainAnimation() {
  for (int i = 0; i < 3; i++) {
    matrix.renderBitmap(rainIcon, ROWS, COLS); delay(500);
    uint8_t blank[ROWS][COLS] = {0};
    matrix.renderBitmap(blank, ROWS, COLS);    delay(300);
  }
}

void playMotionAlert() {
  for (int i = 0; i < 4; i++) {
    matrix.renderBitmap(motionIcon, ROWS, COLS);
    tone(BUZZER, 2000, 100); delay(200);
    uint8_t blank[ROWS][COLS] = {0};
    matrix.renderBitmap(blank, ROWS, COLS); delay(150);
  }
}

void playAlert() {
  for (int i = 0; i < 3; i++) {
    matrix.renderBitmap(alertIcon, ROWS, COLS); checkSensorsAndPump(); delay(250);
    uint8_t blank[ROWS][COLS] = {0};
    matrix.renderBitmap(blank, ROWS, COLS);     checkSensorsAndPump(); delay(150);
  }
  scrollText("DRY ZONE!");
}

// ==================== FONT + SCROLL ====================
uint8_t getGlyphRow(char c, int row) {
  switch (c) {
    case 'A': { static const uint8_t g[7]={0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
    case 'C': { static const uint8_t g[7]={0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}; return g[row]; }
    case 'D': { static const uint8_t g[7]={0x1E,0x11,0x11,0x11,0x11,0x11,0x1E}; return g[row]; }
    case 'E': { static const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}; return g[row]; }
    case 'F': { static const uint8_t g[7]={0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}; return g[row]; }
    case 'G': { static const uint8_t g[7]={0x0E,0x11,0x10,0x17,0x11,0x11,0x0E}; return g[row]; }
    case 'H': { static const uint8_t g[7]={0x11,0x11,0x11,0x1F,0x11,0x11,0x11}; return g[row]; }
    case 'I': { static const uint8_t g[7]={0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}; return g[row]; }
    case 'K': { static const uint8_t g[7]={0x11,0x12,0x14,0x18,0x14,0x12,0x11}; return g[row]; }
    case 'L': { static const uint8_t g[7]={0x10,0x10,0x10,0x10,0x10,0x10,0x1F}; return g[row]; }
    case 'M': { static const uint8_t g[7]={0x11,0x1B,0x15,0x15,0x11,0x11,0x11}; return g[row]; }
    case 'N': { static const uint8_t g[7]={0x11,0x19,0x15,0x13,0x11,0x11,0x11}; return g[row]; }
    case 'O': { static const uint8_t g[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'P': { static const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}; return g[row]; }
    case 'R': { static const uint8_t g[7]={0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}; return g[row]; }
    case 'S': { static const uint8_t g[7]={0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}; return g[row]; }
    case 'T': { static const uint8_t g[7]={0x1F,0x04,0x04,0x04,0x04,0x04,0x04}; return g[row]; }
    case 'U': { static const uint8_t g[7]={0x11,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case 'V': { static const uint8_t g[7]={0x11,0x11,0x11,0x11,0x0A,0x0A,0x04}; return g[row]; }
    case 'W': { static const uint8_t g[7]={0x11,0x11,0x11,0x15,0x15,0x15,0x0A}; return g[row]; }
    case 'Y': { static const uint8_t g[7]={0x11,0x11,0x0A,0x04,0x04,0x04,0x04}; return g[row]; }
    case 'Z': { static const uint8_t g[7]={0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}; return g[row]; }
    case '0': { static const uint8_t g[7]={0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}; return g[row]; }
    case '1': { static const uint8_t g[7]={0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}; return g[row]; }
    case '2': { static const uint8_t g[7]={0x0E,0x11,0x01,0x0E,0x10,0x10,0x1F}; return g[row]; }
    case '3': { static const uint8_t g[7]={0x1F,0x02,0x04,0x0E,0x01,0x11,0x0E}; return g[row]; }
    case '4': { static const uint8_t g[7]={0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}; return g[row]; }
    case '5': { static const uint8_t g[7]={0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}; return g[row]; }
    case '6': { static const uint8_t g[7]={0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}; return g[row]; }
    case '7': { static const uint8_t g[7]={0x1F,0x01,0x02,0x04,0x08,0x08,0x08}; return g[row]; }
    case '8': { static const uint8_t g[7]={0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}; return g[row]; }
    case '9': { static const uint8_t g[7]={0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}; return g[row]; }
    case '%': { static const uint8_t g[7]={0x19,0x1A,0x04,0x04,0x04,0x0B,0x13}; return g[row]; }
    case '!': { static const uint8_t g[7]={0x04,0x04,0x04,0x04,0x04,0x00,0x04}; return g[row]; }
    case ' ': default: { return 0x00; }
  }
}

void scrollText(const char *msg, int speedMs) {
  int len = strlen(msg);
  int glyphWidth = 5, spacing = 1;
  int totalWidth = len * (glyphWidth + spacing);
  for (int offset = COLS; offset >= -totalWidth; offset--) {
    uint8_t frame[ROWS][COLS] = {0};
    for (int col = 0; col < COLS; col++) {
      int sourceCol = col - offset;
      if (sourceCol < 0 || sourceCol >= totalWidth) continue;
      int charIndex = sourceCol / (glyphWidth + spacing);
      int colInChar = sourceCol % (glyphWidth + spacing);
      if (colInChar >= glyphWidth) continue;
      char c = msg[charIndex];
      for (int row = 0; row < 7 && row < ROWS; row++) {
        uint8_t rowBits = getGlyphRow(c, row);
        bool on = (rowBits >> (glyphWidth - 1 - colInChar)) & 0x01;
        if (on) frame[row][col] = 1;
      }
    }
    matrix.renderBitmap(frame, ROWS, COLS);
    checkSensorsAndPump();
    delay(speedMs);
  }
}

// ==================== BRIDGE FUNCTIONS ====================
void  show_stats(int w, int d)   { }
void  trigger_alert(bool a)      { g_alertMode = a; }
int   get_zone1()                { return zone1; }
int   get_zone2()                { return zone2; }
int   get_zone3()                { return zone3; }
bool  get_pump()                 { return pumpOn; }
bool  get_rain()                 { return isRaining; }
bool  get_motion()                { return motionDetected; }
int   get_light()                { return lightPercent; }
float get_temperature()          { return temperatureC; }
float get_pressure()             { return pressureHPa; }
float get_humidity()             { return humidityPct; }
float get_soil_temperature()     { return soilTempC; }
String get_status()              { return systemStatus; }
bool  get_lora_ready()            { return loraReady; }

// --- Manual pump control (called from dashboard buttons) ---
void set_pump_manual(bool state) { manualMode = true; manualPumpState = state; }
void set_auto_mode()             { manualMode = false; }
bool get_manual_mode()           { return manualMode; }

// --- Manual LED control ---
void set_led_manual(bool state)  { ledManualMode = true; ledManualState = state; }
void set_led_auto()              { ledManualMode = false; }
bool get_led_manual_mode()       { return ledManualMode; }
bool get_red_led()               { return redLedOn; }

// --- Buzzer test button ---
void test_buzzer()               { tone(BUZZER, 1000, 300); }

// ==================== LORA ====================
void sendLoRaAlert(String msg) {
  if (!loraReady) return;
  LoRa.beginPacket();
  LoRa.print("EcoFarm|");
  LoRa.print(msg);
  LoRa.print("|Z1:"); LoRa.print(zone1);
  LoRa.print("|Z2:"); LoRa.print(zone2);
  LoRa.print("|Z3:"); LoRa.print(zone3);
  LoRa.print("|T:");  LoRa.print((int)temperatureC);
  LoRa.print("|H:");  LoRa.print((int)humidityPct);
  LoRa.print("|R:");  LoRa.print(isRaining ? "1":"0");
  LoRa.endPacket();
  loraPacketCount++;
  Serial.print("LoRa TX #"); Serial.print(loraPacketCount);
  Serial.print(": "); Serial.println(msg);
}

// ==================== ENVIRONMENT ====================
void readEnvironmentSensors() {
  if (bmpReady) {
    temperatureC = bmp.readTemperature();
    pressureHPa  = bmp.readPressure() / 100.0F;
  }
  if (ds18Ready) {
    soilTempSensor.requestTemperatures();
    delay(100);
    float r = soilTempSensor.getTempCByIndex(0);
    if (r != DEVICE_DISCONNECTED_C && r > -100.0) soilTempC = r;
  }
  if (dhtReady) {
    float h = dht.readHumidity();
    if (!isnan(h)) humidityPct = h;
  }
  rainAnalog   = analogRead(RAIN_ANALOG);
  isRaining    = (digitalRead(RAIN_DIGITAL) == LOW);
  int rawLight = analogRead(LDR_ANALOG);
  lightPercent = map(rawLight, 0, 1023, 100, 0);
  motionDetected = (digitalRead(PIR_PIN) == HIGH);
}

// ==================== FLOAT SPLIT FOR OLED ====================
void splitFloat(float v, int &w, int &d) {
  bool neg = v < 0; if (neg) v = -v;
  int s = (int)(v * 10.0 + 0.5);
  w = s / 10; d = s % 10;
  if (neg) w = -w;
}

// ==================== OLED ====================
void updateOLED() {
  if (!oledReady) return;
  char line[32]; int w, d;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x14B_tr);
  u8g2.drawStr(0, 12, "EcoFarm AI");
  u8g2.setFont(u8g2_font_6x12_tr);
  snprintf(line, sizeof(line), "Z1:%d%% Z2:%d%% Z3:%d%%", zone1, zone2, zone3);
  u8g2.drawStr(0, 25, line);
  snprintf(line, sizeof(line), "%s%s%s",
    pumpOn ? "PUMP:ON " : "PUMP:OFF",
    isRaining ? " RAIN" : "",
    motionDetected ? " MOT!" : "");
  u8g2.drawStr(0, 37, line);
  if (bmpReady) {
    splitFloat(temperatureC, w, d);
    snprintf(line, sizeof(line), "Air:%d.%dC Sol:%dC", w, d, (int)(soilTempC+0.5));
  } else {
    snprintf(line, sizeof(line), "Air:N/A Sol:N/A");
  }
  u8g2.drawStr(0, 49, line);
  snprintf(line, sizeof(line), "H:%d%% L:%d%% P:%dhPa",
    (int)(humidityPct < 0 ? 0 : humidityPct),
    lightPercent,
    (int)(pressureHPa + 0.5));
  u8g2.drawStr(0, 61, line);
  u8g2.sendBuffer();
}

// ==================== SENSOR + PUMP LOGIC ====================
void checkSensorsAndPump() {
  int p1 = constrain(map(analogRead(S1), DRY, WET, 0, 100), 0, 100);
  int p2 = constrain(map(analogRead(S2), DRY, WET, 0, 100), 0, 100);
  int p3 = constrain(map(analogRead(S3), DRY, WET, 0, 100), 0, 100);
  zone1 = p1; zone2 = p2; zone3 = p3;

  isRaining      = (digitalRead(RAIN_DIGITAL) == LOW);
  motionDetected = (digitalRead(PIR_PIN) == HIGH);
  lightPercent   = map(analogRead(LDR_ANALOG), 0, 1023, 100, 0);

  // ---------- MANUAL PUMP OVERRIDE ----------
  if (manualMode) {
    digitalWrite(RELAY_PIN, manualPumpState ? LOW : HIGH);
    pumpOn = manualPumpState;
    systemStatus = manualPumpState ? "MANUAL - PUMP ON" : "MANUAL - PUMP OFF";
  } else {
    bool anyDry    = (p1 < 30 || p2 < 30 || p3 < 30);
    bool anyMedium = (p1 < 60 || p2 < 60 || p3 < 60);

    if (isRaining) {
      // RAIN → Pump always OFF (water saving)
      digitalWrite(RELAY_PIN, HIGH);
      systemStatus = "RAIN - Pump OFF";
      pumpOn = false;
    } else if (anyDry) {
      digitalWrite(RELAY_PIN, LOW);
      tone(BUZZER, 1000, 200);
      systemStatus = "DRY - PUMP ON!";
      pumpOn = true;
      g_alertMode = true;
      sendLoRaAlert("DRY ALERT");
    } else if (anyMedium) {
      digitalWrite(RELAY_PIN, HIGH);
      systemStatus = "MEDIUM - Pump OFF";
      pumpOn = false;
    } else {
      digitalWrite(RELAY_PIN, HIGH);
      systemStatus = "WET - All OK";
      pumpOn = false;
    }
  }

  // ---------- LED (manual or follows pump/alert automatically) ----------
  if (ledManualMode) {
    digitalWrite(LED_R, ledManualState ? HIGH : LOW);
    digitalWrite(LED_G, ledManualState ? LOW : HIGH);
    redLedOn = ledManualState;
  } else {
    bool alertNow = pumpOn || isRaining == false && (zone1 < 30 || zone2 < 30 || zone3 < 30);
    digitalWrite(LED_R, alertNow ? HIGH : LOW);
    digitalWrite(LED_G, alertNow ? LOW : HIGH);
    redLedOn = alertNow;
  }

  // ---------- MOTION ALERT ----------
  if (motionDetected) {
    tone(BUZZER, 2500, 150);
    sendLoRaAlert("MOTION!");
  }

  updateOLED();
}

// ==================== SERIAL STATUS ====================
void printSerialStatus() {
  Serial.println("=== EcoFarm AI ===");
  Serial.print("Z1:"); Serial.print(zone1);
  Serial.print("% Z2:"); Serial.print(zone2);
  Serial.print("% Z3:"); Serial.print(zone3); Serial.println("%");
  Serial.print("Status: "); Serial.println(systemStatus);
  Serial.print("Rain: "); Serial.println(isRaining ? "YES" : "No");
  Serial.print("Light: "); Serial.print(lightPercent); Serial.println("%");
  Serial.print("Motion: "); Serial.println(motionDetected ? "YES!" : "No");
  Serial.print("AirTemp: "); Serial.print(temperatureC); Serial.println("C");
  Serial.print("SoilTemp: "); Serial.print(soilTempC); Serial.println("C");
  Serial.print("Humidity: "); Serial.print(humidityPct); Serial.println("%");
  Serial.print("Pressure: "); Serial.print(pressureHPa); Serial.println("hPa");
  Serial.print("LoRa TX: "); Serial.println(loraPacketCount);
  Serial.println("==================");
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(9600);

  pinMode(LED_G,        OUTPUT);
  pinMode(LED_R,        OUTPUT);
  pinMode(BUZZER,       OUTPUT);
  pinMode(RELAY_PIN,    OUTPUT);
  pinMode(RAIN_DIGITAL, INPUT);
  pinMode(LDR_DIGITAL,  INPUT);
  pinMode(PIR_PIN,      INPUT);

  digitalWrite(RELAY_PIN, HIGH); // Pump OFF by default
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_R, LOW);

  matrix.begin();
  Wire.begin();

  Serial.println("PIR warming up (ignore motion for ~30-60s after power-on)");

  // BMP280
  if (bmp.begin(0x76)) { bmpReady = true;  Serial.println("BMP280: OK"); }
  else                  { Serial.println("BMP280: NOT FOUND"); }

  // DS18B20
  soilTempSensor.begin();
  if (soilTempSensor.getDeviceCount() > 0) { ds18Ready = true; Serial.println("DS18B20: OK"); }
  else { Serial.println("DS18B20: NOT FOUND"); }

  // DHT11
  dht.begin(); delay(2000);
  float dhtTest = dht.readHumidity();
  if (!isnan(dhtTest)) { dhtReady = true; humidityPct = dhtTest; Serial.println("DHT11: OK"); }
  else { Serial.println("DHT11: NOT FOUND"); }

  // OLED
  if (u8g2.begin()) { oledReady = true; Serial.println("OLED: OK"); }
  else { Serial.println("OLED: NOT FOUND"); }

  // LoRa (hardware SPI, TX only)
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (LoRa.begin(LORA_FREQ)) {
    loraReady = true;
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    Serial.println("LoRa: OK");
  } else {
    Serial.println("LoRa: NOT FOUND - check wiring (VCC=3.3V only!)");
  }

  // Bridge — exposes everything to python/main.py
  Bridge.begin();
  Bridge.provide("show_stats",           show_stats);
  Bridge.provide("trigger_alert",        trigger_alert);
  Bridge.provide("get_zone1",            get_zone1);
  Bridge.provide("get_zone2",            get_zone2);
  Bridge.provide("get_zone3",            get_zone3);
  Bridge.provide("get_status",           get_status);
  Bridge.provide("get_pump",             get_pump);
  Bridge.provide("get_rain",             get_rain);
  Bridge.provide("get_motion",           get_motion);
  Bridge.provide("get_light",            get_light);
  Bridge.provide("get_temperature",      get_temperature);
  Bridge.provide("get_pressure",         get_pressure);
  Bridge.provide("get_humidity",         get_humidity);
  Bridge.provide("get_soil_temperature", get_soil_temperature);
  Bridge.provide("get_lora_ready",       get_lora_ready);

  Bridge.provide("set_pump_manual",      set_pump_manual);
  Bridge.provide("set_auto_mode",        set_auto_mode);
  Bridge.provide("get_manual_mode",      get_manual_mode);

  Bridge.provide("set_led_manual",       set_led_manual);
  Bridge.provide("set_led_auto",         set_led_auto);
  Bridge.provide("get_led_manual_mode",  get_led_manual_mode);
  Bridge.provide("get_red_led",          get_red_led);

  Bridge.provide("test_buzzer",          test_buzzer);

  Serial.println("=== EcoFarm AI Ready ===");
  scrollText("ECOFARM AI");
  checkSensorsAndPump();
  readEnvironmentSensors();
}

// ==================== LOOP ====================
void loop() {
  // Environment every 5 sec
  static unsigned long lastEnv  = 0;
  if (millis() - lastEnv > 5000) {
    readEnvironmentSensors();
    lastEnv = millis();
  }

  // LoRa heartbeat every 30 sec
  static unsigned long lastLora = 0;
  if (millis() - lastLora > 30000) {
    sendLoRaAlert("HEARTBEAT");
    lastLora = millis();
  }

  printSerialStatus();
  updateOLED();

  if (g_alertMode) { playAlert(); g_alertMode = false; return; }
  if (isRaining)   { playRainAnimation(); scrollText("RAIN ON"); }
  if (motionDetected) { playMotionAlert(); scrollText("MOTION!"); }

  playGrowthAnimation();
  playWaterDroplets();
  scrollText("ECOFARM AI");

  char msg[40];
  snprintf(msg, sizeof(msg), "Z1 %d%% Z2 %d%% Z3 %d%%", zone1, zone2, zone3);
  scrollText(msg);
  scrollText(pumpOn ? "PUMP ON" : "PUMP OFF");

  if (bmpReady) {
    snprintf(msg, sizeof(msg), "TEMP %dC", (int)(temperatureC+0.5));
    scrollText(msg);
  }
  if (ds18Ready && soilTempC > -100.0) {
    snprintf(msg, sizeof(msg), "SOIL %dC", (int)(soilTempC+0.5));
    scrollText(msg);
  }
  if (dhtReady && humidityPct >= 0) {
    snprintf(msg, sizeof(msg), "HUM %d%%", (int)(humidityPct+0.5));
    scrollText(msg);
  }
  snprintf(msg, sizeof(msg), "LIGHT %d%%", lightPercent);
  scrollText(msg);
  scrollText(isRaining ? "RAIN ON" : "NO RAIN");

  checkSensorsAndPump();
}
