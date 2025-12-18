#pragma once
#include <Arduino.h>

// Pin definitions. 

// I2C pins shared between BMP390L and DS3231
constexpr uint8_t PIN_I2C_SDA = 3;
constexpr uint8_t PIN_I2C_SCL = 2;

// LCD ST7567A (using software SPI via u8g2)
constexpr uint8_t PIN_LCD_SCK  = 4; //scl
constexpr uint8_t PIN_LCD_MOSI = 5;
constexpr uint8_t PIN_LCD_CS   = 8;
constexpr uint8_t PIN_LCD_DC   = 6;
constexpr uint8_t PIN_LCD_RST  = 7;
constexpr uint8_t PIN_LCD_LED  = 9;

// Vibrator control
constexpr uint8_t PIN_VIBE = 15;

// Battery / charger sensing (ADC channels)
constexpr uint8_t PIN_BATT_VOLTAGE  = 11;   // connected to battery voltage divider
constexpr uint8_t PIN_CHARGER_SENSE = 1;  // connected to charger detection

// Buttons 3.3v
constexpr uint8_t PIN_BTN_UP   = 14;
constexpr uint8_t PIN_BTN_MID  = 13;
constexpr uint8_t PIN_BTN_DOWN = 12;