#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

#include "include/config_pins.h"

// Wrapper del LCD ST7567A usando u8g2.
class LcdDriver {
public:
    bool begin() {
        // Inicializar u8g2
        u8g2.begin();

        u8g2.setContrast(140);   // prueba: 140–200 suele ir bien en ST7567
        // Orientación por defecto (no invertida)
        u8g2.setDisplayRotation(rotationInverted ? U8G2_R2 : U8G2_R0);

        u8g2.clearBuffer();
        u8g2.sendBuffer();

        // Configurar backlight por PWM usando la API nueva ledcAttach(pin, freq, resolution)
        pinMode(PIN_LCD_LED, OUTPUT);
        // 5 kHz, resolución 8 bits
        ledcAttach(PIN_LCD_LED, 5000, 8);

        // Arrancamos con backlight apagado
        setBacklight(0);
        return true;
    }

    // nivel 0–255 (0 = apagado, 255 = máximo)
    void setBacklight(uint8_t level) {
        backlightLevel = level;
        // En esta API ledcWrite toma el PIN, no el canal
        ledcWrite(PIN_LCD_LED, backlightLevel);
    }

    uint8_t getBacklight() const {
        return backlightLevel;
    }

    // Aplica rotación 0° (false) o 180° (true)
    void setRotation(bool inverted) {
        rotationInverted = inverted;
        u8g2.setDisplayRotation(rotationInverted ? U8G2_R2 : U8G2_R0);
    }

    bool isRotationInverted() const {
        return rotationInverted;
    }

    // Acceso al objeto u8g2 para que UiRenderer dibuje
    U8G2& getU8g2() {
        return u8g2;
    }

    void prepareForDeepSleep() {
        // Apagar backlight
        setBacklight(0);

        // Poner el display en power save y limpiar cualquier contenido
        u8g2.setPowerSave(true);
        u8g2.clearBuffer();
        u8g2.sendBuffer();
    }

private:
    // SW SPI: SCK, MOSI, CS, DC, RST
    U8G2_ST7567_JLX12864_F_4W_SW_SPI u8g2{
        U8G2_R0,
        PIN_LCD_SCK,   // clock
        PIN_LCD_MOSI,  // data
        PIN_LCD_CS,    // cs
        PIN_LCD_DC,    // dc
        PIN_LCD_RST    // reset
    };

    uint8_t backlightLevel = 0;
    bool    rotationInverted = false;
};
