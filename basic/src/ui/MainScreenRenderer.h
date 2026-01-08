#pragma once
#include <Arduino.h>
#include <math.h>

#include "include/config_ui.h"
#include "drivers/LcdDriver.h"
#include "util/AltFormat.h"
#include "ui/UiModels.h"
#include "core/SettingsService.h"

// Renderiza la pantalla principal (altitud, hora, iconos).
class MainScreenRenderer {
public:
    MainScreenRenderer(LcdDriver* lcdDriver) : lcd(lcdDriver) {}

    void render(const MainUiModel& model,
                const HudConfig& hudCfg,
                uint32_t repaintCounter) {
        if (!lcd) return;

        U8G2& u8g2 = lcd->getU8g2();

        u8g2.clearBuffer();

        // Si modo minimal para CLIMB/FF está activo, dibuja sólo la altura
        // (ajusta fuente/posición en config_ui.h para probar centrado).
        if (model.minimalFlight) {
            String altStr = formatAltitudeString(model.alt.altToShow, model.freefall);
            u8g2.setFont(UI_FONT_ALT_CLEAR);
            uint16_t altWidth = u8g2.getStrWidth(altStr.c_str());
            uint16_t altX     = ((128 - altWidth) / 2);
            uint16_t altY     = UI_CLEAR_ALT_Y; // baseline configurable
            u8g2.drawStr(altX, altY, altStr.c_str());
            u8g2.sendBuffer();
            return;
        }

        // 1) Línea superior: unidad + temperatura + hora + (icono de carga) + batería
        u8g2.setFont(UI_FONT_TEXT_SMALL);

        uint8_t yTop = 9;

        char battBuf[8];
        snprintf(battBuf, sizeof(battBuf), "%u%%", model.batteryPercent);
        uint16_t battW = u8g2.getStrWidth(battBuf);
        uint16_t battX = 128 - battW - 2;

        // Bloque izquierdo: unidad, temperatura
        uint8_t xLeft = 2;
        if (hudCfg.showUnits) {
            const char* unitText = (model.unit == UnitType::METERS) ? "M" : "FT";
            u8g2.drawStr(xLeft, yTop, unitText);
            xLeft += u8g2.getStrWidth(unitText) + 4;
        }

        if (hudCfg.showTime && model.timeText[0] != '\0') {
            int w = u8g2.getStrWidth(model.timeText);
            int x = (128 - w) / 2;
            // Evitar solape con bloque izquierdo y batería
            int minX = (int)xLeft + 2;
            if (x < minX) x = minX;
            int maxX = (int)battX - w - 2;
            if (x > maxX) x = maxX;
            if (x < 0) x = 0;
            u8g2.setCursor(x, yTop);
            u8g2.print(model.timeText);
        }

        // Icono de carga alineado al texto de batería (a la izquierda del %)
        uint16_t rightBound = battX;
        if (model.charging) {
            u8g2.setFont(u8g2_font_open_iconic_other_1x_t);
            uint8_t iconW = u8g2.getMaxCharWidth();
            int iconX = (int)battX - iconW - 2;
            if (iconX < 0) iconX = 0;
            u8g2.drawGlyph(iconX, yTop, 64);
            u8g2.setFont(UI_FONT_TEXT_SMALL);
            rightBound = (uint16_t)iconX;
        }

        // Redibuja hora si fue limitada por icono de carga
        if (hudCfg.showTime && model.timeText[0] != '\0') {
            int w = u8g2.getStrWidth(model.timeText);
            int x = (128 - w) / 2;
            int minX = (int)xLeft + 2;
            if (x < minX) x = minX;
            int maxX = (int)rightBound - w - 2;
            if (x > maxX) x = maxX;
            if (x < 0) x = 0;
            u8g2.setCursor(x, yTop);
            u8g2.print(model.timeText);
        }

        u8g2.drawStr(battX, yTop, battBuf);

        // 2) Altura grande centrada (Logisoso)
        String altStr = formatAltitudeString(model.alt.altToShow, model.freefall);

        u8g2.setFont(UI_FONT_ALT_MAIN);
        uint16_t altWidth = u8g2.getStrWidth(altStr.c_str());
        uint16_t altX     = ((128 - altWidth) / 2);
        uint16_t altY     = 48; // baseline altura principal; ajusta si cambias de fuente

        u8g2.drawStr(altX, altY, altStr.c_str());

        // 3) Iconos de estado (lock, flechas, Zzz)
        u8g2.setFont(UI_FONT_TEXT_SMALL);
        uint8_t yStatus = 20;

        if (model.lockActive) {
            u8g2.setFont(u8g2_font_open_iconic_thing_1x_t);
            u8g2.drawGlyph(26, 63, 79);
            u8g2.setFont(UI_FONT_TEXT_SMALL);
        }

        if (model.climbing && hudCfg.showArrows) {
            u8g2.drawTriangle(20, yStatus+8,  26, yStatus-2,  32, yStatus+8);
        }

        if (model.freefall && hudCfg.showArrows) {
            uint8_t x0 = 96;
            u8g2.drawTriangle(x0, yStatus-2, x0+6, yStatus+8, x0+12, yStatus-2);
        }

        if (model.canopy && hudCfg.showArrows) {
            // Cuadrado simple para indicar canopy en la misma banda de iconos
            uint8_t x0 = 92;
            uint8_t size = 12;
            u8g2.drawBox(x0, yStatus-8, size, size);
        }

        if (model.showZzz) {
            uint8_t iconX = (uint8_t)(altX / 2);
            uint8_t iconY = 32; // mitad de la altura de pantalla
            u8g2.setFont(u8g2_font_open_iconic_weather_2x_t);
            u8g2.drawGlyph(iconX, iconY, 66);
            u8g2.setFont(UI_FONT_TEXT_SMALL);
        }

        // Contador de saltos (jump id) en esquina inferior derecha
        if (hudCfg.showJumps) {
            char jumpBuf[12];
            snprintf(jumpBuf, sizeof(jumpBuf), "%lu",
                     static_cast<unsigned long>(model.totalJumps));
            uint16_t w = u8g2.getStrWidth(jumpBuf);
            uint8_t x = (w < 128) ? static_cast<uint8_t>(128 - w - 1) : 1;
            uint8_t y = 62; // 1 px sobre el borde inferior
            u8g2.drawStr(x-3, y, jumpBuf);
        }

        // Temperatura en esquina inferior izquierda
        if (hudCfg.showTemp && isfinite(model.temperatureC)) {
            u8g2.setFont(UI_FONT_TEXT_SMALL);
            char tempBuf[12];
            int tInt = (int)lroundf(model.temperatureC);
            snprintf(tempBuf, sizeof(tempBuf), "%d%cC", tInt, 176);
            u8g2.drawStr(2, 62, tempBuf);
        }

        // Marco opcional alrededor de la pantalla
        if (hudCfg.showBorder) {
            u8g2.setFont(UI_FONT_TEXT_SMALL);
            u8g2.drawFrame(0, 0, 128, 64);
            // Línea separadora bajo la banda superior
            u8g2.drawHLine(0, yTop + 1, 128);
            // Línea para la sección inferior de iconos (con 1px de margen)
            u8g2.drawHLine(1, 64 - 11, 126);
        }

        u8g2.sendBuffer();
    }

private:
    LcdDriver* lcd = nullptr;
};
