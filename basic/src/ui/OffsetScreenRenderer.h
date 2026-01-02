#pragma once
#include <Arduino.h>
#include <stdio.h>

#include "drivers/LcdDriver.h"
#include "util/Types.h"
#include "include/config_ui.h"

// Pantalla de edición de offset (valor y unidad).
class OffsetScreenRenderer {
public:
    explicit OffsetScreenRenderer(LcdDriver* lcdDriver) : lcd(lcdDriver) {}

    void render(float offsetValue, UnitType unit, Language lang) {
        if (!lcd) return;

        U8G2& u8g2 = lcd->getU8g2();
        u8g2.clearBuffer();

        const uint8_t* fontText = (lang == Language::EN) ? UI_FONT_TEXT_EN : UI_FONT_TEXT_ES;
        u8g2.setFont(fontText);

        const char* title = (lang == Language::EN) ? "Offset" : "Offset";
        u8g2.drawStr(2, 10, title);

        // Valor grande centrado
        u8g2.setFont(UI_FONT_ALT_MAIN);
        char valBuf[16];
        snprintf(valBuf, sizeof(valBuf), "%.0f", offsetValue);

        uint16_t valW = u8g2.getStrWidth(valBuf);
        uint16_t valX = (128 - valW) / 2;
        uint16_t valY = 44;
        u8g2.drawStr(valX, valY, valBuf);

        // Unidad al lado derecho del valor
        u8g2.setFont(UI_FONT_TEXT_SMALL);
        const char* unitStr = (unit == UnitType::METERS) ? "m" : "ft";
        u8g2.drawStr(valX + valW + 4, valY - 10, unitStr);

        // Instrucciones
        const char* hint = (lang == Language::EN)
                               ? "UP +1  DN -1  MID save"
                               : "UP +1  DOWN -1  MID guarda";
        uint16_t hintW = u8g2.getStrWidth(hint);
        u8g2.drawStr((128 - hintW) / 2, 62, hint);

        u8g2.sendBuffer();
    }

private:
    LcdDriver* lcd = nullptr;
};
