#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "core/LogbookService.h"
#include "drivers/LcdDriver.h"
#include "core/SettingsService.h"
#include "util/Types.h"
#include "drivers/ButtonsDriver.h"
#include "core/UiStateService.h"
#include "include/config_ui.h"
#include <time.h>

// UI para la bitácora: listado de saltos y borrado.
class LogbookUi {
public:
    LogbookUi(LogbookService* lb, LcdDriver* lcd)
        : logbook(lb), lcdDrv(lcd) {}

    void enter() {
        LogbookService::Stats st{};
        if (logbook && logbook->getStats(st)) {
            count = st.count;
        } else {
            count = 0;
        }
        idx = 0;
        erasePrompt = false;
        eraseRequireRelease = false;
        upHeld = false;
        downHeld = false;
        toastActive = false;
    }

    void handleEvent(const ButtonEvent& ev,
                     ButtonId logicalId,
                     UiStateService& uiState,
                     const Settings& settings)
    {
        if (!logbook) return;

        // Track de botones mantenidos (para combinaciones UP+DOWN).
        if (logicalId == ButtonId::UP) {
            if (ev.type == ButtonEventType::PRESS)   upHeld = true;
            if (ev.type == ButtonEventType::RELEASE) upHeld = false;
        } else if (logicalId == ButtonId::DOWN) {
            if (ev.type == ButtonEventType::PRESS)   downHeld = true;
            if (ev.type == ButtonEventType::RELEASE) downHeld = false;
        }

        // Si acabamos de entrar en prompt, exigimos soltar ambos antes de confirmar.
        if (erasePrompt && eraseRequireRelease && !upHeld && !downHeld) {
            eraseRequireRelease = false;
        }

        // Salir con MID press
        if (logicalId == ButtonId::MID && ev.type == ButtonEventType::PRESS) {
            erasePrompt = false;
            eraseRequireRelease = false;
            uiState.setScreen(UiScreen::MENU_ROOT);
            return;
        }

        // Navegación
        if (ev.type == ButtonEventType::PRESS || ev.type == ButtonEventType::REPEAT) {
            int step = (ev.type == ButtonEventType::PRESS) ? 1 : 5;
            if (logicalId == ButtonId::UP) {
                if (count > 0) {
                    idx = (idx + step) % count; // más antiguo
                }
            } else if (logicalId == ButtonId::DOWN) {
                if (count > 0) {
                    idx = (idx + count - (step % count)) % count; // más reciente
                }
            }
        }

        // Borrado (según spec): mantener UP+DOWN 3s → prompt. Soltar y repetir
        // UP+DOWN 3s → confirma borrado.
        if (ev.type == ButtonEventType::LONG_PRESS_3S &&
            (logicalId == ButtonId::UP || logicalId == ButtonId::DOWN) &&
            upHeld && downHeld) {
            if (!erasePrompt) {
                erasePrompt = true;
                eraseRequireRelease = true; // evita borrar en el mismo hold (llegan 2 eventos: UP y DOWN)
            } else if (!eraseRequireRelease) {
                if (logbook->reset()) {
                    count = 0;
                    idx = 0;
                    toastActive = true;
                    toastUntil  = millis() + 900;
                    strncpy(toastMsg, (settings.idioma == Language::ES) ? "Bit\u00e1cora borrada"
                                                                        : "Logbook erased",
                            sizeof(toastMsg)-1);
                    toastMsg[sizeof(toastMsg)-1] = '\0';
                }
                erasePrompt = false;
                eraseRequireRelease = false;
                upHeld = false;
                downHeld = false;
            }
        }
    }

    void render(const Settings& settings) {
        if (!lcdDrv) return;
        U8G2& u8g2 = lcdDrv->getU8g2();

        if (toastActive) {
            u8g2.clearBuffer();
            const uint8_t* fontText = chooseFont(settings.idioma);
            u8g2.setFont(fontText);
            int w = u8g2.getUTF8Width(toastMsg);
            int x = (128 - w) / 2; if (x < 0) x = 0;
            u8g2.drawUTF8(x, 36, toastMsg);
            u8g2.sendBuffer();
            if ((int32_t)(millis() - toastUntil) >= 0) {
                toastActive = false;
            }
            return;
        }

        if (erasePrompt) {
            drawErasePrompt(u8g2, settings.idioma);
            return;
        }

        if (count == 0) {
            drawEmpty(u8g2, settings.idioma);
            return;
        }

        LogbookService::Record rec{};
        if (!logbook->getByIndex((uint16_t)idx, rec)) {
            drawEmpty(u8g2, settings.idioma);
            return;
        }

        drawEntry(u8g2, rec, idx, count, settings);
    }

private:
    static void formatTime(uint32_t epoch, char* hhmm, size_t hhmmLen, char* dmy, size_t dmyLen) {
        time_t t = (time_t)epoch;
        struct tm *tmv = gmtime(&t);
        if (!tmv) {
            snprintf(hhmm, hhmmLen, "--:--");
            snprintf(dmy,  dmyLen,  "--/--/--");
            return;
        }
        snprintf(hhmm, hhmmLen, "%02d:%02d", tmv->tm_hour, tmv->tm_min);
        snprintf(dmy,  dmyLen,  "%02d/%02d/%02d", tmv->tm_mday, tmv->tm_mon+1, (tmv->tm_year+1900)%100);
    }

    static void drawEmpty(U8G2& u8g2, Language lang) {
        u8g2.clearBuffer();
        u8g2.setFont(chooseFont(lang));
        u8g2.drawUTF8(8, 28, (lang == Language::ES) ? "Sin registros" : "No entries");
        u8g2.drawUTF8(8, 46, (lang == Language::ES) ? "MID para salir" : "MID to exit");
        u8g2.sendBuffer();
    }

    static void drawErasePrompt(U8G2& u8g2, Language lang) {
        u8g2.clearBuffer();
        u8g2.setFont(chooseFont(lang));
        u8g2.drawUTF8(2, 16, (lang == Language::ES) ? "Borrar Bit\u00e1cora" : "Erase Logbook");
        u8g2.drawUTF8(2, 32, (lang == Language::ES) ? "Mantener UP+DOWN 3s" : "Hold UP+DOWN 3s");
        u8g2.drawUTF8(2, 48, (lang == Language::ES) ? "Soltar y repetir / MID" : "Release+repeat / MID");
        u8g2.sendBuffer();
    }

    static String fmtAlt(float altM, UnitType unit, int decimals) {
        float v = altM;
        const float M_TO_FT = 3.2808399f;
        const char* suffix = " m";
        if (unit == UnitType::FEET) {
            v *= M_TO_FT;
            suffix = " ft";
        }
        char buf[24];
        dtostrf(v, 0, decimals, buf);
        return String(buf) + suffix;
    }

    static String fmtFF(float secs) {
        if (secs >= 60.0f) {
            uint32_t total = (uint32_t)secs;
            uint32_t m = total / 60;
            uint32_t s = total % 60;
            char buf[16];
            snprintf(buf, sizeof(buf), "%lu:%02lu", (unsigned long)m, (unsigned long)s);
            return String(buf);
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f s", secs);
        return String(buf);
    }

    static String fmtVel(float mps) {
        float kmh = mps * 3.6f;
        char buf[24];
        dtostrf(kmh, 0, 1, buf);
        return String(buf) + " km/h";
    }

    static const uint8_t* chooseFont(Language lang) {
        return (lang == Language::EN) ? UI_FONT_TEXT_EN : UI_FONT_TEXT_ES;
    }

    static void drawEntry(U8G2& u8g2,
                          const LogbookService::Record& rec,
                          int idx, int total,
                          const Settings& settings) {
        u8g2.clearBuffer();

        // Marco
        u8g2.drawHLine(0, 0, 128);
        u8g2.drawHLine(0, 13, 128);
        u8g2.drawHLine(0, 63, 128);
        u8g2.drawVLine(0, 0, 64);
        u8g2.drawVLine(127, 0, 64);

        // Header Jump + hora
        u8g2.setFont(chooseFont(settings.idioma));
        char hhmm[8];
        char dmy[12];
        formatTime(rec.tsUtc, hhmm, sizeof(hhmm), dmy, sizeof(dmy));

        char hdr[48];
        snprintf(hdr, sizeof(hdr), "Jump: %lu %s %s",
                 (unsigned long)rec.id, hhmm, dmy);
        u8g2.drawUTF8(2, 10, hdr);

        String sExit   = fmtAlt(rec.exitAltM,   settings.unidadMetros, 0);
        String sDeploy = fmtAlt(rec.deployAltM, settings.unidadMetros, 0);
        String sFF     = fmtFF(rec.freefallTimeS);
        String sVff    = fmtVel(rec.vmaxFFmps);
        String sVcan   = fmtVel(rec.vmaxCanopymps);

        u8g2.drawUTF8(2, 22,  (settings.idioma == Language::ES) ? "Exit:" : "Exit:");
        u8g2.drawUTF8(34, 22, sExit.c_str());

        u8g2.drawUTF8(2, 32,  (settings.idioma == Language::ES) ? "Open:" : "Open:");
        u8g2.drawUTF8(34, 32, sDeploy.c_str());

        u8g2.drawUTF8(2, 42,  "FF:");
        u8g2.drawUTF8(34, 42, sFF.c_str());

        u8g2.drawUTF8(2, 52,  "V:");
        u8g2.drawUTF8(34, 52, sVff.c_str());

        u8g2.drawUTF8(2, 62,  "Vc:");
        u8g2.drawUTF8(34, 62, sVcan.c_str());

        // Footer: índice
        u8g2.setFont(UI_FONT_TEXT_SMALL);
        char idbuf[20];
        snprintf(idbuf, sizeof(idbuf), "<%d/%d>", idx+1, total);
        int idW = u8g2.getStrWidth(idbuf);
        int idX = 128 - idW - 2; if (idX < 0) idX = 0;
        u8g2.setCursor(idX, 62);
        u8g2.print(idbuf);

        u8g2.sendBuffer();
    }

    LogbookService* logbook   = nullptr;
    LcdDriver*      lcdDrv    = nullptr;
    uint16_t        count     = 0;
    int             idx       = 0;   // 0 = más reciente
    bool            erasePrompt = false;
    bool            eraseRequireRelease = false;
    bool            upHeld = false;
    bool            downHeld = false;

    bool     toastActive = false;
    uint32_t toastUntil  = 0;
    char     toastMsg[48]{};
};
