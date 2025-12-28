#pragma once
#include <Arduino.h>
#include <stdio.h>

#include "include/config_ui.h"
#include "drivers/LcdDriver.h"
#include "core/SettingsService.h"
#include "ui/UiStrings.h"

// Renderiza el menú raíz (paginado) usando datos de Settings.
class MenuRenderer {
public:
    explicit MenuRenderer(LcdDriver* lcdDriver) : lcd(lcdDriver) {}

    void renderRoot(uint8_t selectedIndex,
                    const UtcDateTime& nowUtc,
                    const Settings& settings) {
        if (!lcd) return;

        U8G2& u8g2 = lcd->getU8g2();
        u8g2.clearBuffer();

        u8g2.setFont(UI_FONT_TEXT_SMALL);

        // Header: fecha (arriba)
        char dateBuf[16];
        snprintf(dateBuf, sizeof(dateBuf),
                 "%02u/%02u/%02u",
                 static_cast<unsigned>(nowUtc.day),
                 static_cast<unsigned>(nowUtc.month),
                 static_cast<unsigned>(nowUtc.year % 100));
        u8g2.drawStr(2, 8, dateBuf);

        // Paginación
        const uint8_t ITEMS_PER_PAGE = 4;
        uint8_t totalPages = (UI_MENU_ITEM_COUNT + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        uint8_t curPage    = selectedIndex / ITEMS_PER_PAGE;

        char pageBuf[8];
        snprintf(pageBuf, sizeof(pageBuf),
                 "%u/%u",
                 static_cast<unsigned>(curPage + 1),
                 static_cast<unsigned>(totalPages));

        uint16_t pageW = u8g2.getStrWidth(pageBuf);
        u8g2.drawStr((128 - pageW) / 2, 63, pageBuf);

        // Items visibles en esta página
        uint8_t startIndex = curPage * ITEMS_PER_PAGE;
        uint8_t endIndex   = startIndex + ITEMS_PER_PAGE;
        if (endIndex > UI_MENU_ITEM_COUNT) {
            endIndex = UI_MENU_ITEM_COUNT;
        }

        uint8_t y = 20;
        const uint8_t lineStep = 12;

        Language lang = settings.idioma;

        for (uint8_t i = startIndex; i < endIndex; ++i) {
            char lineBuf[32];
            const char* baseLabel = uiMenuLabel(lang, i);
            const char* suffix    = computeSuffix(i, settings, lang);

            snprintf(lineBuf, sizeof(lineBuf), "%s%s", baseLabel, suffix);

            if (i == selectedIndex) {
                u8g2.drawStr(2, y, ">");
                u8g2.drawStr(10, y, lineBuf);
            } else {
                u8g2.drawStr(10, y, lineBuf);
            }

            y += lineStep;
        }

        u8g2.sendBuffer();
    }

private:
    static const char* computeSuffix(uint8_t idx, const Settings& settings, Language lang) {
        switch (idx) {
        case 0: // Unidad
            return (settings.unidadMetros == UnitType::METERS) ? " m" : " ft";
        case 1: // Luz activada / desactivada
            if (lang == Language::EN) {
                return (settings.brilloPantalla == 0) ? " Off" : " On";
            } else {
                return (settings.brilloPantalla == 0) ? " Des" : " Act";
            }
        case 4: // Ahorro
            switch (settings.ahorroTimeoutOption) {
            default:
            case 0: return " 5m";
            case 1: return " 10m";
            case 2: return " 20m";
            case 3: return (lang == Language::EN) ? " Off" : " Off";
            }
        case 5: // Invertir
            return settings.inverPant ? " On" : " Off";
        case 8: // Idioma
            return (settings.idioma == Language::ES) ? " ES" : " EN";
        default:
            return "";
        }
    }

    LcdDriver* lcd = nullptr;
};
