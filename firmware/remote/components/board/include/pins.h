// GPIO map for the rev E board. Source of truth: DESIGN.md "GPIO map";
// matches firmware/bringup and firmware/display_test.
#pragma once

#define PIN_VBAT_SENSE     1   // ADC1_CH0, 1M/1M divider -> multiply by 2
#define PIN_USB_PRESENT_N  2   // LOW = VBUS present; external pull-up R8, no internal pulls
#define PIN_VOL_SENSE      4   // ADC1_CH3, wheel wiper
#define PIN_VOL_EN         5   // powers the wheel pot only while reading
#define PIN_CHG_EN1        6   // BQ24074 EN1 (100k pull-down)
#define PIN_CHG_EN2        7   // BQ24074 EN2 (100k pull-down)

#define PIN_BTN_MENU       9   // buttons: active LOW, firmware pull-ups
#define PIN_BTN_PLAY       10
#define PIN_BTN_PREV       11
#define PIN_BTN_NEXT       12
#define PIN_BTN_SEL        13
#define PIN_BTN_BOOT       0

#define PIN_EPD_SCK        16
#define PIN_EPD_MOSI       17
#define PIN_EPD_CS         18
#define PIN_EPD_DC         21
#define PIN_EPD_RST        47
#define PIN_EPD_BUSY       48  // HIGH = busy
