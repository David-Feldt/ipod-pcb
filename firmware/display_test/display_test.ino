// Rev E display bring-up: GDEY0213B74 (SSD1680) on J4, BRINGUP.md step 7.
//
// Does nothing to the panel until you press SEL (centre button) or send 'g'
// over serial, so it is safe to flash before the panel is fitted.
// Sequence: white, black, white, checkerboard, orientation screen, then hibernate.
// Each refresh is timed via BUSY; battery voltage is logged before/after.
//
// Requires GxEPD2 (tested 1.6.9) and Adafruit GFX.

#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_BW.h>

// ---- pins (DESIGN.md) ----
constexpr int PIN_VBAT_SENSE = 1;
constexpr int PIN_VOL_EN     = 5;
constexpr int PIN_CHG_EN1    = 6;
constexpr int PIN_CHG_EN2    = 7;
constexpr int PIN_BTN_SEL    = 13;
constexpr int PIN_EPD_SCK    = 16;
constexpr int PIN_EPD_MOSI   = 17;
constexpr int PIN_EPD_CS     = 18;
constexpr int PIN_EPD_DC     = 21;
constexpr int PIN_EPD_RST    = 47;
constexpr int PIN_EPD_BUSY   = 48;

constexpr uint32_t SPI_HZ = 4000000;     // conservative for first contact
constexpr int MIN_START_MV = 3500;       // refuse to start on a weak battery

GxEPD2_BW<GxEPD2_213_GDEY0213B74, GxEPD2_213_GDEY0213B74::HEIGHT>
    display(GxEPD2_213_GDEY0213B74(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

int readBatteryMilliVolts() {
  long sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_VBAT_SENSE);
  return sum / 16 * 2;
}

// Run one full-screen draw + refresh, time it, and judge the BUSY behaviour.
bool timedRefresh(const char *name, void (*draw)()) {
  int before = readBatteryMilliVolts();
  uint32_t t0 = millis();
  display.setFullWindow();
  display.firstPage();
  do { draw(); } while (display.nextPage());
  uint32_t ms = millis() - t0;
  int after = readBatteryMilliVolts();

  const char *verdict = "OK";
  bool ok = true;
  if (ms < 500)        { verdict = "TOO FAST: BUSY never asserted (panel not responding?)"; ok = false; }
  else if (ms > 9000)  { verdict = "TOO SLOW: BUSY stuck or timed out (boost fault?)"; ok = false; }
  Serial.printf("[%-12s] %5lu ms  vbat %d -> %d mV  %s\n", name, (unsigned long)ms, before, after, verdict);
  return ok;
}

void drawWhite() { display.fillScreen(GxEPD_WHITE); }
void drawBlack() { display.fillScreen(GxEPD_BLACK); }

void drawChecker() {
  display.fillScreen(GxEPD_WHITE);
  const int s = 8;
  for (int y = 0; y < display.height(); y += s)
    for (int x = 0; x < display.width(); x += s)
      if (((x / s) + (y / s)) & 1) display.fillRect(x, y, s, s, GxEPD_BLACK);
}

// Orientation screen: border, filled top-left corner, arrow toward the FPC tail.
void drawOrientation() {
  int w = display.width(), h = display.height();
  display.fillScreen(GxEPD_WHITE);
  display.drawRect(0, 0, w, h, GxEPD_BLACK);
  display.drawRect(2, 2, w - 4, h - 4, GxEPD_BLACK);
  display.fillRect(0, 0, 16, 16, GxEPD_BLACK);              // top-left marker
  display.fillTriangle(w / 2, 8, w / 2 - 12, 28, w / 2 + 12, 28, GxEPD_BLACK);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(1);
  display.setCursor(8, 40);  display.print("TOP (FPC tail)");
  display.setTextSize(2);
  display.setCursor(8, 80);  display.print("eink-pod");
  display.setCursor(8, 104); display.print("rev E");
  display.setTextSize(1);
  display.setCursor(8, 140); display.printf("vbat %d mV", readBatteryMilliVolts());
  display.setCursor(8, 156); display.printf("%dx%d", w, h);
  display.setCursor(8, 172); display.print("square = top-left");
  display.setCursor(8, h - 16); display.print("BOTTOM");
}

void runSequence() {
  int vbat = readBatteryMilliVolts();
  Serial.printf("\nstarting display test, vbat %d mV, BUSY idle level %d\n", vbat, digitalRead(PIN_EPD_BUSY));
  if (vbat < MIN_START_MV) {
    Serial.printf("ABORT: battery below %d mV; charge it first\n", MIN_START_MV);
    return;
  }

  display.init(115200, true, 10, false);   // prints GxEPD2's own BUSY timing too
  display.setRotation(0);                  // portrait, 122 x 250
  Serial.printf("BUSY after reset/init: %d (expect 0 = idle)\n", digitalRead(PIN_EPD_BUSY));

  int failures = 0;
  failures += !timedRefresh("white",   drawWhite);
  failures += !timedRefresh("black",   drawBlack);
  failures += !timedRefresh("white",   drawWhite);
  failures += !timedRefresh("checker", drawChecker);
  failures += !timedRefresh("orient",  drawOrientation);

  display.hibernate();
  Serial.printf("done: %d refresh(es) flagged. Panel hibernated. Press SEL or send 'g' to repeat.\n", failures);
}

void setup() {
  pinMode(PIN_CHG_EN1, INPUT);             // leave USB100 to the pull-downs
  pinMode(PIN_CHG_EN2, INPUT);
  pinMode(PIN_VOL_EN, OUTPUT);
  digitalWrite(PIN_VOL_EN, LOW);
  pinMode(PIN_BTN_SEL, INPUT_PULLUP);
  pinMode(PIN_EPD_BUSY, INPUT);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);

  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);
  delay(250);

  SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));

  Serial.println("\n=== eink-pod rev E display test ===");
  Serial.printf("vbat %d mV. Press SEL (centre) or send 'g' to start.\n", readBatteryMilliVolts());
}

void loop() {
  static bool lastSel = false;
  bool sel = digitalRead(PIN_BTN_SEL) == LOW;
  bool go = sel && !lastSel;
  lastSel = sel;
  while (Serial.available()) if (Serial.read() == 'g') go = true;
  if (go) runSequence();
  delay(20);
}
