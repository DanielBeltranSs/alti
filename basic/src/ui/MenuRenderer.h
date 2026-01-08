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

        const uint8_t* fontText = (settings.idioma == Language::EN)
                                    ? UI_FONT_TEXT_EN
                                    : UI_FONT_TEXT_ES;

        // Header: título + fecha con fuente global de texto
        u8g2.setFont(fontText);
        const char* title = (settings.idioma == Language::EN) ? "Menu:" : "Men\u00fa:";
        u8g2.drawUTF8(2, 10, title);

        // Fecha en esquina superior derecha
        char dateBuf[16];
        snprintf(dateBuf, sizeof(dateBuf),
                 "%02u/%02u/%02u",
                 static_cast<unsigned>(nowUtc.day),
                 static_cast<unsigned>(nowUtc.month),
                 static_cast<unsigned>(nowUtc.year % 100));
        uint16_t dateW = u8g2.getUTF8Width(dateBuf);
        u8g2.drawUTF8(128 - dateW - 2, 10, dateBuf);

        const uint8_t ITEMS_PER_PAGE = 4;
        uint8_t totalPages = (UI_MENU_ITEM_COUNT + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        uint8_t curPage    = selectedIndex / ITEMS_PER_PAGE;

        char pageBuf[8];
        snprintf(pageBuf, sizeof(pageBuf),
                 "%u/%u",
                 static_cast<unsigned>(curPage + 1),
                 static_cast<unsigned>(totalPages));

        u8g2.setFont(fontText);
        // Paginación como círculos centrados (relleno = página actual)
        const uint8_t radius = 2;
        const uint8_t spacing = 8;
        uint8_t totalWidth = (totalPages > 0) ? (uint8_t)((totalPages - 1) * spacing) : 0;
        int startX = (int)((128 - totalWidth) / 2);
        if (startX < 0) startX = 0;
        uint8_t centerY = 60;
        for (uint8_t p = 0; p < totalPages; ++p) {
            uint8_t cx = (uint8_t)(startX + p * spacing);
            if (p == curPage) {
                u8g2.drawDisc(cx, centerY, radius);
            } else {
                u8g2.drawCircle(cx, centerY, radius, U8G2_DRAW_ALL);
            }
        }

        // Items visibles en esta página
        uint8_t startIndex = curPage * ITEMS_PER_PAGE;
        uint8_t endIndex   = startIndex + ITEMS_PER_PAGE;
        if (endIndex > UI_MENU_ITEM_COUNT) {
            endIndex = UI_MENU_ITEM_COUNT;
        }

        u8g2.setFont(fontText);

        uint8_t y = 20;
        const uint8_t lineStep = 12;

        Language lang = settings.idioma;
        const char* selMarker = ">";
        uint8_t xLeft = 7; // desplazamos todo 5 px a la derecha
        uint16_t selW = u8g2.getUTF8Width(selMarker);
        const uint8_t suffixX = 70; // columna fija para sufijos/toggles

        for (uint8_t i = startIndex; i < endIndex; ++i) {
            const char* baseLabel = uiMenuLabel(lang, i);
            const char* suffix    = computeSuffix(i, settings, lang);

            if (i == selectedIndex) {
                // fondo invertido con esquinas redondeadas al final de la fila
                uint8_t bgHeight = lineStep;
                uint8_t bgY = y - bgHeight + 2;
                uint8_t bgX = 0;
                uint8_t bgW = 128;
                u8g2.setDrawColor(1);
                u8g2.drawBox(bgX, bgY, bgW, bgHeight);
                // recortar esquinas derechas redondeadas manuales
                u8g2.setDrawColor(0);
                // esquina superior derecha
                u8g2.drawPixel(bgW - 1, bgY);
                u8g2.drawPixel(bgW - 2, bgY);
                u8g2.drawPixel(bgW - 1, bgY + 1);
                u8g2.drawPixel(bgW - 3, bgY);
                // esquina inferior derecha
                u8g2.drawPixel(bgW - 1, bgY + bgHeight - 1);
                u8g2.drawPixel(bgW - 2, bgY + bgHeight - 1);
                u8g2.drawPixel(bgW - 1, bgY + bgHeight - 2);
                u8g2.drawPixel(bgW - 3, bgY + bgHeight - 1);

                u8g2.setDrawColor(0);
                u8g2.drawUTF8(xLeft, y, selMarker);
                u8g2.drawUTF8(xLeft + selW + 6, y, baseLabel);
                if (suffix && suffix[0] != '\0') {
                    u8g2.drawUTF8(suffixX, y, suffix);
                }
                u8g2.setDrawColor(1);
            } else {
                // texto alineado a la izquierda
                u8g2.drawUTF8(xLeft, y, baseLabel);
                if (suffix && suffix[0] != '\0') {
                    u8g2.drawUTF8(suffixX, y, suffix);
                }
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
        case 2: // Ahorro
            switch (settings.ahorroTimeoutOption) {
            default:
            case 0: return " 5m";
            case 1: return " 10m";
            case 2: return " 20m";
            case 3: return (lang == Language::EN) ? " Off" : " Off";
            }
        case 3: // Invertir
            return settings.inverPant ? " On" : " Off";
        case 4: // Idioma
            return (settings.idioma == Language::ES) ? " ES" : " EN";
        case 6: // Pantalla limpia
            return settings.hudMinimalFlight ? " On" : " Off";
        default:
            return "";
        }
    }

    LcdDriver* lcd = nullptr;
};
