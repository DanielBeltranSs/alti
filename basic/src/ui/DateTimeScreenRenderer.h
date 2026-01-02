#pragma once
#include <Arduino.h>
#include <stdio.h>

#include "drivers/LcdDriver.h"
#include "core/UiStateService.h"
#include "util/Types.h"
#include "include/config_ui.h"

// Pantalla de edición de fecha y hora (RTC).
class DateTimeScreenRenderer {
public:
    explicit DateTimeScreenRenderer(LcdDriver* lcdDriver) : lcd(lcdDriver) {}

    void render(const UiStateService::DateTimeEditState& st, Language lang) {
        if (!lcd) return;
        U8G2& u8g2 = lcd->getU8g2();
        u8g2.clearBuffer();

        const uint8_t* fontText = (lang == Language::EN) ? UI_FONT_TEXT_EN : UI_FONT_TEXT_ES;
        u8g2.setFont(fontText);
        const char* title = (lang == Language::EN) ? "Date/Time" : "Fecha/Hora";
        u8g2.drawStr(2, 10, title);

        // Fecha (DD/MM/AAAA)
        uint8_t x = 16;
        uint8_t yDate = 26;

        char dbuf[4];  snprintf(dbuf, sizeof(dbuf),  "%02u", static_cast<unsigned>(st.value.day));
        char mbuf[4];  snprintf(mbuf, sizeof(mbuf),  "%02u", static_cast<unsigned>(st.value.month));
        char ybuf[8];  snprintf(ybuf, sizeof(ybuf),  "%04u", static_cast<unsigned>(st.value.year));

        x = drawField(u8g2, x, yDate, dbuf, st.cursor == 0);
        x = drawSep(u8g2, x, yDate, "/");
        x = drawField(u8g2, x, yDate, mbuf, st.cursor == 1);
        x = drawSep(u8g2, x, yDate, "/");
        x = drawField(u8g2, x, yDate, ybuf, st.cursor == 2);

        // Hora
        uint8_t yTime = yDate + 16;
        x = 28;

        char hbuf[4];  snprintf(hbuf, sizeof(hbuf),  "%02u", static_cast<unsigned>(st.value.hour));
        char minbuf[4];snprintf(minbuf, sizeof(minbuf),"%02u", static_cast<unsigned>(st.value.minute));

        x = drawField(u8g2, x, yTime, hbuf, st.cursor == 3);
        x = drawSep(u8g2, x, yTime, ":");
        x = drawField(u8g2, x, yTime, minbuf, st.cursor == 4);

        // Ayuda inferior
        const char* hint = (lang == Language::EN)
            ? "UP/DN adjust  MID next  MID3s save  MID6s cancel"
            : "UP/DN ajusta  MID cambia  MID3s guarda  MID6s cancela";
        uint16_t hintW = u8g2.getStrWidth(hint);
        int16_t hx = (int16_t)((128 - hintW) / 2);
        if (hx < 0) hx = 0;
        u8g2.drawStr(hx, 62, hint);

        u8g2.sendBuffer();
    }

private:
    static uint8_t drawField(U8G2& u8g2,
                             uint8_t x,
                             uint8_t y,
                             const char* txt,
                             bool selected) {
        uint8_t w = u8g2.getStrWidth(txt);
        if (selected) {
            u8g2.drawFrame(x - 2, y - 10, w + 4, 12);
        }
        u8g2.drawStr(x, y, txt);
        return x + w;
    }

    static uint8_t drawSep(U8G2& u8g2,
                           uint8_t x,
                           uint8_t y,
                           const char* sep) {
        uint8_t w = u8g2.getStrWidth(sep);
        u8g2.drawStr(x + 2, y, sep);
        return x + w + 4;
    }

    LcdDriver* lcd = nullptr;
};
