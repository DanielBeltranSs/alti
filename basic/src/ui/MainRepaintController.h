#pragma once
#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "ui/UiModels.h"

// Controla cuándo repintar la pantalla principal según las reglas de ahorro.
// Separa la lógica de “gating” del renderer para mantener el código legible.
class MainRepaintController {
public:
    // Fuerza un repintado en el próximo ciclo.
    void force() { forceMainRepaint = true; }

    // Resetea el estado acumulado (se usa al salir de ahorro).
    void reset() {
        state = {};
        state.first        = true;
        state.lastBattPct  = 255;
        state.lastMinute   = 0xFFFF;
        state.haveLastTime = false;
        state.haveLastTemp = false;
        memset(state.lastTimeText, 0, sizeof(state.lastTimeText));
        forceMainRepaint = true;
    }

    // Devuelve true si corresponde repintar en modo ahorro (MAIN + suelo + sin lock).
    bool shouldRepaint(const MainUiModel& model, uint32_t nowMs) {
        bool repaint = false;

        if (state.first) {
            repaint = true;
        }

        if (forceMainRepaint) {
            repaint = true;
        }

        float altShown = model.alt.altToShow;
        if (!state.haveLastAlt ||
            fabsf(altShown - state.lastAltShown) > 1e-3f) {
            repaint = true;
        }

        if (state.lastBattPct == 255 ||
            model.batteryPercent != state.lastBattPct) {
            repaint = true;
        }

        if (model.charging != state.lastCharging) {
            repaint = true;
        }

        if (model.showZzz != state.lastZzz) {
            repaint = true;
        }

        if (model.lockActive != state.lastLock) {
            repaint = true;
        }

        uint16_t minute = minuteTickFromNow(nowMs);
        if (state.lastMinute == 0xFFFF || minute != state.lastMinute) {
            repaint = true;
        }

        if (!state.haveLastTime ||
            strncmp(model.timeText, state.lastTimeText, sizeof(model.timeText)) != 0) {
            repaint = true;
        }

        bool tempFinite = isfinite(model.temperatureC);
        int16_t tempInt = tempFinite ? (int16_t)lroundf(model.temperatureC) : 0;
        if (tempFinite != state.haveLastTemp ||
            (tempFinite && tempInt != state.lastTempInt)) {
            repaint = true;
        }

        if (repaint) {
            state.first        = false;
            state.haveLastAlt  = true;
            state.lastAltShown = altShown;
            state.lastBattPct  = model.batteryPercent;
            state.lastCharging = model.charging;
            state.lastZzz      = model.showZzz;
            state.lastLock     = model.lockActive;
            state.lastMinute   = minute;

            state.haveLastTime = true;
            strncpy(state.lastTimeText, model.timeText, sizeof(state.lastTimeText));

            state.haveLastTemp = tempFinite;
            state.lastTempInt  = tempInt;

            forceMainRepaint = false;
        }

        return repaint;
    }

private:
    static uint16_t minuteTickFromNow(uint32_t nowMs) {
        return static_cast<uint16_t>(nowMs / 60000UL);
    }

    struct State {
        bool     first        = true;
        bool     haveLastAlt  = false;
        float    lastAltShown = 0.0f;
        uint8_t  lastBattPct  = 255;
        bool     lastCharging = false;
        bool     lastZzz      = false;
        bool     lastLock     = false;
        uint16_t lastMinute   = 0xFFFF;

        bool     haveLastTime = false;
        char     lastTimeText[6] = {0};

        bool     haveLastTemp = false;
        int16_t  lastTempInt  = 0;
    };

    State state{};
    bool  forceMainRepaint = true;
};
