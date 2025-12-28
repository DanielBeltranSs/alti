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

        u8g2.setFont(UI_FONT_TEXT_SMALL);

        const char* title = (lang == Language::EN) ? "Icons" : "Iconos";
        u8g2.drawStr(2, 10, title);

        // Paginado simple
        const uint8_t ITEMS_PER_PAGE = 4;
        uint8_t total = UI_HUD_MENU_COUNT;
        uint8_t page = selectedIdx / ITEMS_PER_PAGE;
        uint8_t totalPages = (total + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        uint8_t start = page * ITEMS_PER_PAGE;
        uint8_t end = start + ITEMS_PER_PAGE;
        if (end > total) end = total;

        // Indicador de página
        char pageBuf[8];
        snprintf(pageBuf, sizeof(pageBuf), "%u/%u",
                 static_cast<unsigned>(page + 1),
                 static_cast<unsigned>(totalPages));
        uint16_t pageW = u8g2.getStrWidth(pageBuf);
        u8g2.drawStr(128 - pageW - 2, 10, pageBuf);

        uint8_t y = 22;
        const uint8_t lineStep = 11;

        for (uint8_t i = start; i < end; ++i) {
            if (i < UI_HUD_ICON_COUNT) {
                bool enabled = iconEnabled(hud, i);
                const char* label = iconLabel(lang, i);
                const char* state = (lang == Language::EN)
                                        ? (enabled ? "On" : "Off")
                                        : (enabled ? "On" : "Off");

                char line[32];
                snprintf(line, sizeof(line), "%s %s", label, state);

                if (i == selectedIdx) {
                    u8g2.drawStr(2, y, ">");
                    u8g2.drawStr(10, y, line);
                } else {
                    u8g2.drawStr(10, y, line);
                }
            } else {
                // Item "Volver"
                const char* backLabel = (lang == Language::EN) ? "Back" : "Volver";
                if (i == selectedIdx) {
                    u8g2.drawStr(2, y, ">");
                    u8g2.drawStr(10, y, backLabel);
                } else {
                    u8g2.drawStr(10, y, backLabel);
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
            default: return "";
            }
        } else {
            switch (idx) {
            case 0: return "Flechas";
            case 1: return "Hora";
            case 2: return "Temp";
            default: return "";
            }
        }
    }

    static bool iconEnabled(const HudConfig& hud, uint8_t idx) {
        switch (idx) {
        case 0: return hud.showArrows;
        case 1: return hud.showTime;
        case 2: return hud.showTemp;
        default: return true;
        }
    }

    LcdDriver* lcd = nullptr;
};
