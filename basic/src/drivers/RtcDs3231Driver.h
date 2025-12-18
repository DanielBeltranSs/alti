// src/drivers/RtcDs3231Driver.h
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "include/config_pins.h"
#include "util/Types.h"

class RtcDs3231Driver {
public:
    static constexpr uint8_t DS3231_ADDR = 0x68;

    void begin() {
        // Usa el mismo bus I2C (pines personalizados) que el BMP390
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

        // Validación simple: si no responde, sólo avisamos por serial
        Wire.beginTransmission(DS3231_ADDR);
        if (Wire.endTransmission() != 0) {
            Serial.println(F("[RTC] DS3231 no respondió en I2C"));
        }
    }

    UtcDateTime nowUtc() {
        UtcDateTime dt{2025,1,1,0,0,0};

        Wire.beginTransmission(DS3231_ADDR);
        Wire.write(0x00); // registro de segundos
        Wire.endTransmission(false);

        Wire.requestFrom(DS3231_ADDR, (uint8_t)7);
        if (Wire.available() < 7) {
            return dt; // devuelve algo por defecto si falla
        }

        uint8_t sec  = Wire.read();
        uint8_t min  = Wire.read();
        uint8_t hour = Wire.read();
        uint8_t dayOfWeek = Wire.read(); (void)dayOfWeek;
        uint8_t day  = Wire.read();
        uint8_t month= Wire.read();
        uint8_t year = Wire.read();

        dt.second = bcdToDec(sec & 0x7F);
        dt.minute = bcdToDec(min & 0x7F);
        dt.hour   = bcdToDec(hour & 0x3F);
        dt.day    = bcdToDec(day & 0x3F);
        dt.month  = bcdToDec(month & 0x1F);
        dt.year   = 2000 + bcdToDec(year);

        return dt;
    }

    void setUtc(const UtcDateTime& dt) {
        Wire.beginTransmission(DS3231_ADDR);
        Wire.write(0x00); // empezamos en registro de segundos

        Wire.write(decToBcd(dt.second));
        Wire.write(decToBcd(dt.minute));
        Wire.write(decToBcd(dt.hour)); // 24h

        // DOW lo dejamos en 1 (no lo usas)
        Wire.write(0x01);

        Wire.write(decToBcd(dt.day));
        Wire.write(decToBcd(dt.month));
        Wire.write(decToBcd((uint8_t)(dt.year - 2000)));

        Wire.endTransmission();
    }

private:
    uint8_t bcdToDec(uint8_t val) {
        return (val / 16 * 10) + (val % 16);
    }

    uint8_t decToBcd(uint8_t val) {
        return ((val / 10) * 16) + (val % 10);
    }
};
