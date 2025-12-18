#pragma once
#include <Arduino.h>
#include <math.h>

#include "include/config_ui.h"
#include "drivers/LcdDriver.h"
#include "util/AltFormat.h"
#include "ui/UiModels.h"

// Renderiza la pantalla principal (altitud, hora, iconos).
class MainScreenRenderer {
public:
    MainScreenRenderer(LcdDriver* lcdDriver) : lcd(lcdDriver) {}

    void render(const MainUiModel& model, uint32_t repaintCounter) {
        if (!lcd) return;

        U8G2& u8g2 = lcd->getU8g2();

        u8g2.clearBuffer();

        // 1) Línea superior: icono de carga + hora + batería
        u8g2.setFont(UI_FONT_TEXT_SMALL);

        uint8_t xTop = 2;
        uint8_t yTop = 8;

        char battBuf[8];
        snprintf(battBuf, sizeof(battBuf), "%u%%", model.batteryPercent);
        uint16_t battW = u8g2.getStrWidth(battBuf);
        uint16_t battX = 128 - battW - 2;

        if (model.charging) {
            u8g2.drawStr(xTop, yTop, "+"); // placeholder tipo "cargando"
            xTop += u8g2.getStrWidth("+") + 2;
        }

        uint16_t timeW = 0;
        if (model.timeText[0] != '\0') {
            u8g2.drawStr(xTop, yTop, model.timeText);
            timeW = u8g2.getStrWidth(model.timeText);
        }

        // Temperatura (C) si hay espacio entre hora y batería
        if (isfinite(model.temperatureC)) {
            char tempBuf[12];
            int tInt = (int)lroundf(model.temperatureC);
            snprintf(tempBuf, sizeof(tempBuf), "%dC", tInt);
            uint16_t tempW = u8g2.getStrWidth(tempBuf);
            uint16_t tempX = xTop + timeW + 6;
            if (tempX + tempW + 2 < battX) {
                u8g2.drawStr(tempX, yTop, tempBuf);
            }
        }

        u8g2.drawStr(battX, yTop, battBuf);

        // 2) Altura grande centrada (Logisoso)
        String altStr = formatAltitudeString(model.alt.altToShow, model.freefall);

        u8g2.setFont(UI_FONT_ALT_MAIN);
        uint16_t altWidth = u8g2.getStrWidth(altStr.c_str());
        uint16_t altX     = (128 - altWidth) / 2;
        uint16_t altY     = 46;

        u8g2.drawStr(altX, altY, altStr.c_str());

        // 3) Iconos de estado (lock, flechas, Zzz)
        u8g2.setFont(UI_FONT_TEXT_SMALL);
        uint8_t yStatus = 20;

        if (model.lockActive) {
            u8g2.setFont(u8g2_font_open_iconic_thing_1x_t);
            u8g2.drawGlyph(26, 63, 79);
            u8g2.setFont(UI_FONT_TEXT_SMALL);
        }

        if (model.climbing) {
            u8g2.drawTriangle(20, yStatus+8,  26, yStatus-2,  32, yStatus+8);
        }

        if (model.freefall) {
            uint8_t x0 = 96;
            u8g2.drawTriangle(x0, yStatus-2, x0+6, yStatus+8, x0+12, yStatus-2);
        }

        if (model.showZzz) {
            u8g2.drawStr(96, 60, "Zzz");
        }

        // Contador de repaints (debug)
        u8g2.setFont(u8g2_font_micro_tr);
        char rbuf[16];
        snprintf(rbuf, sizeof(rbuf), "R:%lu",
                 static_cast<unsigned long>(repaintCounter));
        u8g2.drawStr(1, 63, rbuf);

        u8g2.sendBuffer();
    }

private:
    LcdDriver* lcd = nullptr;
};
