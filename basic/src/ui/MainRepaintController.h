#pragma once
#include <Arduino.h>
#include <math.h>
#include <string.h>

#include "ui/UiModels.h"
#include "core/SettingsService.h"

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
        state.lastHudMask  = 0xFF;
        state.haveLastTime = false;
        state.haveLastTemp = false;
        memset(state.lastTimeText, 0, sizeof(state.lastTimeText));
        forceMainRepaint = true;
    }

    // Devuelve true si corresponde repintar en modo ahorro (MAIN + suelo + sin lock).
    bool shouldRepaint(const MainUiModel& model,
                       const HudConfig& hudCfg,
                       uint32_t nowMs) {
        bool repaint = false;

        uint8_t hudMask = hudCfg.toMask();

        bool lockVisible     = model.lockActive; // no removible
        bool climbVisible    = hudCfg.showArrows && model.climbing;
        bool ffVisible       = hudCfg.showArrows && model.freefall;
        bool zzzVisible      = model.showZzz;    // no removible
        bool chargingVisible = model.charging;   // no removible
        bool timeVisible     = hudCfg.showTime;
        bool tempVisible     = hudCfg.showTemp;

        if (state.first) {
            repaint = true;
        }

        if (forceMainRepaint) {
            repaint = true;
        }

        if (state.lastHudMask == 0xFF || state.lastHudMask != hudMask) {
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

        if (chargingVisible != state.lastCharging) {
            repaint = true;
        }

        if (zzzVisible != state.lastZzz) {
            repaint = true;
        }

        if (lockVisible != state.lastLock) {
            repaint = true;
        }

        if (climbVisible != state.lastClimb) {
            repaint = true;
        }

        if (ffVisible != state.lastFreefall) {
            repaint = true;
        }

        uint16_t minute = minuteTickFromNow(nowMs);
        if (timeVisible) {
            if (state.lastMinute == 0xFFFF || minute != state.lastMinute) {
                repaint = true;
            }

            if (!state.haveLastTime ||
                strncmp(model.timeText, state.lastTimeText, sizeof(model.timeText)) != 0) {
                repaint = true;
            }
        }

        bool tempFinite = isfinite(model.temperatureC);
        int16_t tempInt = tempFinite ? (int16_t)lroundf(model.temperatureC) : 0;
        if (tempVisible) {
            if (tempFinite != state.haveLastTemp ||
                (tempFinite && tempInt != state.lastTempInt)) {
                repaint = true;
            }
        }

        if (repaint) {
            state.first        = false;
            state.haveLastAlt  = true;
            state.lastAltShown = altShown;
            state.lastBattPct  = model.batteryPercent;
            state.lastCharging = chargingVisible;
            state.lastZzz      = zzzVisible;
            state.lastLock     = lockVisible;
            state.lastClimb    = climbVisible;
            state.lastFreefall = ffVisible;
            state.lastMinute   = timeVisible ? minute : state.lastMinute;
            state.lastHudMask  = hudMask;

            if (timeVisible) {
                state.haveLastTime = true;
                strncpy(state.lastTimeText, model.timeText, sizeof(state.lastTimeText));
            }

            if (tempVisible) {
                state.haveLastTemp = tempFinite;
                state.lastTempInt  = tempInt;
            }

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
        bool     lastClimb    = false;
        bool     lastFreefall = false;
        uint16_t lastMinute   = 0xFFFF;
        uint8_t  lastHudMask  = 0xFF;

        bool     haveLastTime = false;
        char     lastTimeText[6] = {0};

        bool     haveLastTemp = false;
        int16_t  lastTempInt  = 0;
    };

    State state{};
    bool  forceMainRepaint = true;
};
