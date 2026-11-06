// Rev E bring-up test: exercises everything except the display and Wi-Fi.
//
// Prints a status line twice a second plus immediate button events over
// native USB serial (115200). Display pins are left untouched (high-Z) and
// Wi-Fi/BT stay off so the board stays well inside the USB100 input budget.
//
// Pin map: DESIGN.md "GPIO map". Procedures: BRINGUP.md steps 5 and 8.

#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>
#include <esp_system.h>

// ---- pins (DESIGN.md) ----
constexpr int PIN_VBAT_SENSE    = 1;   // ADC1_CH0, 1M/1M divider -> x2
constexpr int PIN_USB_PRESENT_N = 2;   // low = VBUS present, external pull-up R8
constexpr int PIN_VOL_SENSE     = 4;   // ADC1_CH3, wheel wiper
constexpr int PIN_VOL_EN        = 5;   // powers the wheel only while reading
constexpr int PIN_CHG_EN1       = 6;   // charger mode bits, 100k pull-downs
constexpr int PIN_CHG_EN2       = 7;

struct Button { const char *name; int pin; bool down; uint32_t changedMs; uint32_t presses; };
Button buttons[] = {
  {"MENU", 9, false, 0, 0}, {"PLAY", 10, false, 0, 0}, {"PREV", 11, false, 0, 0},
  {"NEXT", 12, false, 0, 0}, {"SEL", 13, false, 0, 0}, {"BOOT", 0, false, 0, 0},
};
constexpr uint32_t DEBOUNCE_MS = 25;

int wheelMin = 100000, wheelMax = -1;

// Average several calibrated ADC readings (millivolts at the pin).
int readMilliVolts(int pin, int samples = 16) {
  long sum = 0;
  for (int i = 0; i < samples; i++) sum += analogReadMilliVolts(pin);
  return sum / samples;
}

int readBatteryMilliVolts() { return readMilliVolts(PIN_VBAT_SENSE) * 2; }

int readWheelMilliVolts() {
  digitalWrite(PIN_VOL_EN, HIGH);
  delay(1);                        // 2k + 10 nF settles in well under 1 ms
  int mv = readMilliVolts(PIN_VOL_SENSE, 8);
  digitalWrite(PIN_VOL_EN, LOW);
  return mv;
}

const char *chargerMode() {
  bool en1 = digitalRead(PIN_CHG_EN1), en2 = digitalRead(PIN_CHG_EN2);
  if (!en1 && !en2) return "USB100";
  if (en1 && !en2)  return "USB500";
  if (!en1 && en2)  return "ILIM";
  return "SUSPEND";
}

const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_EXT:      return "external pin";
    case ESP_RST_SW:       return "software";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_USB:      return "usb";
    default:               return "other";
  }
}

void printHeader() {
  esp_chip_info_t info;
  esp_chip_info(&info);
  uint32_t flashBytes = 0;
  esp_flash_get_size(NULL, &flashBytes);

  Serial.println();
  Serial.println("=== eink-pod rev E bring-up test ===");
  Serial.printf("chip: %s rev %d.%d, %d cores, flash %lu MB\n", ESP.getChipModel(),
                info.revision / 100, info.revision % 100, info.cores,
                (unsigned long)(flashBytes / (1024 * 1024)));
  Serial.printf("reset reason: %s\n", resetReasonName(esp_reset_reason()));
  Serial.printf("internal temp: %.1f C\n", temperatureRead());
  Serial.printf("charger mode pins: %s (expect USB100: firmware leaves them to the pull-downs)\n",
                chargerMode());

  // Any button reading pressed at boot is stuck or shorted to GND.
  for (auto &b : buttons) {
    if (b.pin == 0) continue;   // BOOT may legitimately be held during reset
    if (digitalRead(b.pin) == LOW)
      Serial.printf("WARNING: %s (GPIO%d) reads LOW at boot: stuck or shorted?\n", b.name, b.pin);
  }
  Serial.println("Press each button and roll the wheel end to end.");
  Serial.println("columns: vbat_mV  usb  chg_mode  wheel_mV (min..max)  presses M/P/<</>>/S/B  temp_C");
}

void setup() {
  // Leave charger mode pins as inputs: the 100k pull-downs hold USB100.
  pinMode(PIN_CHG_EN1, INPUT);
  pinMode(PIN_CHG_EN2, INPUT);

  pinMode(PIN_USB_PRESENT_N, INPUT);          // no internal pull; R8 is the pull-up
  pinMode(PIN_VOL_EN, OUTPUT);
  digitalWrite(PIN_VOL_EN, LOW);
  for (auto &b : buttons) pinMode(b.pin, INPUT_PULLUP);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT_SENSE, ADC_11db);
  analogSetPinAttenuation(PIN_VOL_SENSE, ADC_11db);

  Serial.begin(115200);
  uint32_t start = millis();
  while (!Serial && millis() - start < 3000) delay(10);   // give the host time to open the port
  delay(250);                                             // VBAT divider/filter settling
  printHeader();
}

void pollButtons() {
  uint32_t now = millis();
  for (auto &b : buttons) {
    bool down = digitalRead(b.pin) == LOW;
    if (down != b.down && now - b.changedMs > DEBOUNCE_MS) {
      b.down = down;
      b.changedMs = now;
      if (down) b.presses++;
      Serial.printf("button %-4s %s\n", b.name, down ? "DOWN" : "up");
    }
  }
}

void loop() {
  pollButtons();

  static uint32_t lastReport = 0;
  if (millis() - lastReport < 500) { delay(2); return; }
  lastReport = millis();

  int vbat = readBatteryMilliVolts();
  int wheel = readWheelMilliVolts();
  wheelMin = min(wheelMin, wheel);
  wheelMax = max(wheelMax, wheel);
  bool usb = digitalRead(PIN_USB_PRESENT_N) == LOW;

  Serial.printf("vbat %4d  usb %-3s  %-7s  wheel %4d (%4d..%4d)  presses %lu/%lu/%lu/%lu/%lu/%lu  %.1f\n",
                vbat, usb ? "yes" : "no", chargerMode(), wheel, wheelMin, wheelMax,
                (unsigned long)buttons[0].presses, (unsigned long)buttons[1].presses,
                (unsigned long)buttons[2].presses, (unsigned long)buttons[3].presses,
                (unsigned long)buttons[4].presses, (unsigned long)buttons[5].presses,
                temperatureRead());
}
