#pragma once
#include <Arduino.h>

#include "drivers/LcdDriver.h"
#include "include/config_ui.h"
#include "include/config_ble.h"

// Pantalla de estado BLE: muestra On/Off, PIN y estado de conexión.
class BleScreenRenderer {
public:
    explicit BleScreenRenderer(LcdDriver* lcdDriver) : lcd(lcdDriver) {}

    void render(bool featureEnabled,
                bool bleEnabled,
                const char* deviceName,
                const char* pin,
                bool connected,
                Language lang) {
        if (!lcd) return;
        U8G2& u8g2 = lcd->getU8g2();
        u8g2.clearBuffer();

        const uint8_t* fontText = (lang == Language::EN) ? UI_FONT_TEXT_EN : UI_FONT_TEXT_ES;
        u8g2.setFont(fontText);

        const char* title = (lang == Language::EN) ? "Bluetooth" : "Bluetooth";
        u8g2.drawUTF8(2, 10, title);

        uint8_t y = 26;
        const uint8_t step = 12;

        if (!featureEnabled) {
            const char* msg = (lang == Language::EN) ? "Not available" : "No disponible";
            u8g2.drawUTF8(2, y, msg);
        } else {
            const char* onOff = bleEnabled ? ((lang == Language::EN) ? "On" : "On")
                                           : ((lang == Language::EN) ? "Off" : "Off");
            const char* lblState = (lang == Language::EN) ? "State:" : "Estado:";
            u8g2.drawUTF8(2, y, lblState);
            u8g2.drawUTF8(60, y, onOff);
            y += step;

            const char* lblName = (lang == Language::EN) ? "Name:" : "Nombre:";
            u8g2.drawUTF8(2, y, lblName);
            u8g2.drawUTF8(60, y, deviceName);
            y += step;

            const char* lblPin = "PIN:";
            u8g2.drawUTF8(2, y, lblPin);
            u8g2.drawUTF8(60, y, pin);
            y += step;

            const char* lblConn = (lang == Language::EN) ? "Link:" : "Enlace:";
            const char* connTxt = connected
                                    ? ((lang == Language::EN) ? "Connected" : "Conectado")
                                    : ((lang == Language::EN) ? "Not connected" : "No conectado");
            u8g2.drawUTF8(2, y, lblConn);
            u8g2.drawUTF8(60, y, connTxt);
            y += step;

            const char* hint = (lang == Language::EN)
                                   ? "UP/DN toggle  MID back"
                                   : "UP/DN cambia  MID vuelve";
            int w = u8g2.getUTF8Width(hint);
            int x = (128 - w) / 2;
            if (x < 0) x = 0;
            u8g2.drawUTF8(x, 62, hint);
        }

        u8g2.sendBuffer();
    }

private:
    LcdDriver* lcd = nullptr;
};
