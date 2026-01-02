#pragma once
#include <Arduino.h>
#include <stdio.h>

#include "drivers/LcdDriver.h"
#include "core/SettingsService.h"
#include "util/Types.h"
#include "include/config_ui.h"

// Pantalla de configuración de iconos HUD (On/Off por cada icono).
class IconsScreenRenderer {
public:
    explicit IconsScreenRenderer(LcdDriver* lcdDriver) : lcd(lcdDriver) {}

    void render(uint8_t selectedIdx,
                const HudConfig& hud,
                Language lang) {
        if (!lcd) return;

        U8G2& u8g2 = lcd->getU8g2();
        u8g2.clearBuffer();

        const uint8_t* fontText = (lang == Language::EN) ? UI_FONT_TEXT_EN : UI_FONT_TEXT_ES;
        u8g2.setFont(fontText);

        const char* title = (lang == Language::EN) ? "Icons:" : "Iconos:";
        u8g2.drawStr(3, 9, title);

        // Paginado simple
        const uint8_t ITEMS_PER_PAGE = 4;
        uint8_t total = UI_HUD_MENU_COUNT;
        uint8_t page = selectedIdx / ITEMS_PER_PAGE;
        uint8_t totalPages = (total + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        uint8_t start = page * ITEMS_PER_PAGE;
        uint8_t end = start + ITEMS_PER_PAGE;
        if (end > total) end = total;

        // Indicador de página (círculos como el menú principal)
        const uint8_t radius = 2;
        const uint8_t spacing = 8;
        uint8_t totalWidth = (totalPages > 0) ? (uint8_t)((totalPages - 1) * spacing) : 0;
        int startX = (int)((128 - totalWidth) / 2);
        if (startX < 0) startX = 0;
        uint8_t centerY = 60;
        for (uint8_t p = 0; p < totalPages; ++p) {
            uint8_t cx = (uint8_t)(startX + p * spacing);
            if (p == page) {
                u8g2.drawDisc(cx, centerY, radius);
            } else {
                u8g2.drawCircle(cx, centerY, radius, U8G2_DRAW_ALL);
            }
        }

        uint8_t y = 20;
        const uint8_t lineStep = 12;
        const char* selMarker = ">";
        uint8_t xLeft = 7;
        uint16_t selW = u8g2.getUTF8Width(selMarker);
        const uint8_t suffixX = 70;

        for (uint8_t i = start; i < end; ++i) {
            const bool isBack = (i >= UI_HUD_ICON_COUNT);
            const char* label = isBack ? ((lang == Language::EN) ? "Back" : "Volver")
                                       : iconLabel(lang, i);
            const char* state = nullptr;
            if (!isBack) {
                bool enabled = iconEnabled(hud, i);
                state = (lang == Language::EN)
                            ? (enabled ? "On" : "Off")
                            : (enabled ? "On" : "Off");
            }

            if (i == selectedIdx) {
                uint8_t bgHeight = lineStep;
                uint8_t bgY = y - bgHeight + 2;
                uint8_t bgX = 0;
                uint8_t bgW = 128;
                u8g2.setDrawColor(1);
                u8g2.drawBox(bgX, bgY, bgW, bgHeight);
                // Esquinas derechas suavizadas
                u8g2.setDrawColor(0);
                u8g2.drawPixel(bgW - 1, bgY);
                u8g2.drawPixel(bgW - 2, bgY);
                u8g2.drawPixel(bgW - 1, bgY + 1);
                u8g2.drawPixel(bgW - 3, bgY);
                u8g2.drawPixel(bgW - 1, bgY + bgHeight - 1);
                u8g2.drawPixel(bgW - 2, bgY + bgHeight - 1);
                u8g2.drawPixel(bgW - 1, bgY + bgHeight - 2);
                u8g2.drawPixel(bgW - 3, bgY + bgHeight - 1);

                u8g2.setDrawColor(0);
                u8g2.drawStr(xLeft, y, selMarker);
                u8g2.drawStr(xLeft + selW + 6, y, label);
                if (state) {
                    u8g2.drawStr(suffixX, y, state);
                }
                u8g2.setDrawColor(1);
            } else {
                u8g2.drawStr(xLeft, y, label);
                if (state) {
                    u8g2.drawStr(suffixX, y, state);
                }
            }
            y += lineStep;
        }

        u8g2.sendBuffer();
    }

private:
    static const char* iconLabel(Language lang, uint8_t idx) {
        if (lang == Language::EN) {
            switch (idx) {
            case 0: return "Arrows";
            case 1: return "Time";
            case 2: return "Temp";
            case 3: return "Units";
            case 4: return "Border";
            case 5: return "Jumps";
            default: return "";
            }
        } else {
            switch (idx) {
            case 0: return "Flechas";
            case 1: return "Hora";
            case 2: return "Temp";
            case 3: return "Unidad";
            case 4: return "Borde";
            case 5: return "Saltos";
            default: return "";
            }
        }
    }

    static bool iconEnabled(const HudConfig& hud, uint8_t idx) {
        switch (idx) {
        case 0: return hud.showArrows;
        case 1: return hud.showTime;
        case 2: return hud.showTemp;
        case 3: return hud.showUnits;
        case 4: return hud.showBorder;
        case 5: return hud.showJumps;
        default: return true;
        }
    }

    LcdDriver* lcd = nullptr;
};
