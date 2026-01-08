#pragma once
#include <Arduino.h>
#include "util/Types.h"
#include "include/config_ui.h"

// UiStateService manages the current UI screen, lock state, menu selection index,
// and tracks the last user interaction time.  Other components query this to
// decide which screen to render and when to sleep.
class UiStateService {
public:
    void begin() {
        screen                  = UiScreen::MAIN;
        lastInteractionMs       = millis();
        locked                  = false;
        lockGroundStableStartMs = 0;
        lockStartMs             = 0;
        leftGroundSinceLock     = false;
        menuIndex               = 0;
        offsetEditValue         = 0.0f;
        suspendRequested        = false;
        iconMenuIndex           = 0;
    }

    // Notify that the user interacted (e.g., pressed a button).
    void notifyInteraction(uint32_t nowMs) {
        lastInteractionMs = nowMs;
    }

    uint32_t getLastInteractionMs() const { return lastInteractionMs; }

    UiScreen getScreen() const { return screen; }
    void setScreen(UiScreen s) { screen = s; }

    bool isLocked() const { return locked; }
    void setLocked(bool v) {
        if (v) {
            locked                 = true;
            lockStartMs            = millis();
            lockGroundStableStartMs= 0;
            leftGroundSinceLock    = false;
        } else {
            locked                 = false;
            lockGroundStableStartMs= 0;
            leftGroundSinceLock    = false;
            lockStartMs            = 0;
        }
    }

    // Índice de selección del menú raíz (y futuros submenús).
    uint8_t getMenuIndex() const { return menuIndex; }
    void setMenuIndex(uint8_t idx) { menuIndex = idx; }

    // Índice de selección para la pantalla de iconos HUD.
    uint8_t getIconMenuIndex() const { return iconMenuIndex; }
    void setIconMenuIndex(uint8_t idx) {
        if (idx >= UI_HUD_MENU_COUNT) idx = 0;
        iconMenuIndex = idx;
    }

    // Auto-release del lock cuando llevamos cierto tiempo en suelo estable.
    //
    // onGround      = (phase == GROUND)
    // groundStable  = AltitudeData.isGroundStable (ya incluye la lógica de 30s de AltimetryService)
    void updateLockAutoRelease(bool onGround, bool groundStable, uint32_t nowMs) {
        // Si no está locked, no hay nada que hacer
        if (!locked) {
            lockGroundStableStartMs = 0;
            return;
        }

        // Marcar si salimos del suelo mientras estaba el lock activo
        if (!onGround) {
            leftGroundSinceLock = true;
            lockGroundStableStartMs = 0;
        }

        // Sólo contamos estabilidad en suelo
        if (!onGround || !groundStable) {
            lockGroundStableStartMs = 0;
            return;
        }

        // Primera vez que entramos en condición de suelo estable con lock
        if (lockGroundStableStartMs == 0) {
            lockGroundStableStartMs = nowMs;
            return;
        }

        uint32_t elapsedStable = nowMs - lockGroundStableStartMs;
        uint32_t elapsedSinceLock = (lockStartMs != 0) ? (nowMs - lockStartMs) : 0;

        // Reglas:
        // 1) Post-salto: si salimos de GROUND alguna vez, auto-unlock tras 60s de suelo estable.
        // 2) Embarque: si nunca salimos de suelo, auto-unlock sólo si pasa una espera larga (p.ej. vuelo cancelado).
        constexpr uint32_t LOCK_RELEASE_POST_FLIGHT_MS = 60UL * 1000UL;
        constexpr uint32_t LOCK_RELEASE_BOARDING_MS    = 10UL * 60UL * 2000UL; // 10 minutos de espera

        bool canReleasePostFlight =
            leftGroundSinceLock &&
            (elapsedStable >= LOCK_RELEASE_POST_FLIGHT_MS);

        bool canReleaseBoarding =
            !leftGroundSinceLock &&
            (elapsedSinceLock >= LOCK_RELEASE_BOARDING_MS) &&
            (elapsedStable   >= LOCK_RELEASE_POST_FLIGHT_MS); // también pedimos 1 min estable

        if (canReleasePostFlight || canReleaseBoarding) {
            locked = false;
            lockGroundStableStartMs = 0;
            leftGroundSinceLock = false;
            lockStartMs = 0;
        }
    }

    // ----- Offset editor -----
    void startOffsetEdit(float currentValue) {
        offsetEditValue = clampOffset(currentValue);
    }

    float getOffsetEditValue() const { return offsetEditValue; }

    void adjustOffsetEdit(float delta) {
        offsetEditValue = clampOffset(offsetEditValue + delta);
    }

    void setOffsetEditValue(float v) {
        offsetEditValue = clampOffset(v);
    }

    // ----- Date/time editor -----
    struct DateTimeEditState {
        UtcDateTime value{};
        uint8_t     cursor = 0; // 0=Y,1=M,2=D,3=H,4=Min
    };

    void startDateTimeEdit(const UtcDateTime& current) {
        dtEdit.value  = current;
        dtEdit.cursor = 0;
    }

    const DateTimeEditState& getDateTimeEdit() const { return dtEdit; }

    void advanceDateTimeCursor() {
        dtEdit.cursor = (dtEdit.cursor + 1) % 5;
    }

    void adjustDateTimeField(int delta) {
        switch (dtEdit.cursor) {
        case 0: // Día (1..días del mes)
            {
                int maxDay = daysInMonth(dtEdit.value.year, dtEdit.value.month);
                int d = static_cast<int>(dtEdit.value.day) + delta;
                while (d < 1)     d += maxDay;
                while (d > maxDay) d -= maxDay;
                dtEdit.value.day = static_cast<uint8_t>(d);
            }
            break;
        case 1: // Mes (1..12)
            {
                int m = static_cast<int>(dtEdit.value.month) + delta;
                while (m < 1)  m += 12;
                while (m > 12) m -= 12;
                dtEdit.value.month = static_cast<uint8_t>(m);
                clampDay();
            }
            break;
        case 2: // Año (2000..2099)
            {
                int y = static_cast<int>(dtEdit.value.year) + delta;
                if (y < 2000) y = 2099;
                if (y > 2099) y = 2000;
                dtEdit.value.year = static_cast<uint16_t>(y);
                clampDay();
            }
            break;
        case 3: // Hour (0..23)
            {
                int h = static_cast<int>(dtEdit.value.hour) + delta;
                while (h < 0)  h += 24;
                while (h > 23) h -= 24;
                dtEdit.value.hour = static_cast<uint8_t>(h);
            }
            break;
        case 4: // Minute (0..59)
            {
                int m = static_cast<int>(dtEdit.value.minute) + delta;
                while (m < 0)  m += 60;
                while (m > 59) m -= 60;
                dtEdit.value.minute = static_cast<uint8_t>(m);
            }
            break;
        default:
            break;
        }
    }

    // ----- Suspensión manual (deep sleep) -----
    void requestSuspend() { suspendRequested = true; }

    bool hasSuspendRequest() const { return suspendRequested; }

    bool consumeSuspendRequest() {
        bool req = suspendRequested;
        suspendRequested = false;
        return req;
    }

private:
    static float clampOffset(float v) {
        constexpr float MIN_OFF = -9999.0f;
        constexpr float MAX_OFF =  9999.0f;
        if (v < MIN_OFF) return MIN_OFF;
        if (v > MAX_OFF) return MAX_OFF;
        return v;
    }

    static bool isLeap(uint16_t year) {
        if (year % 400 == 0) return true;
        if (year % 100 == 0) return false;
        return (year % 4) == 0;
    }

    static int daysInMonth(uint16_t year, uint8_t month) {
        static const uint8_t kDays[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
        if (month < 1 || month > 12) return 31;
        if (month == 2 && isLeap(year)) return 29;
        return kDays[month-1];
    }

    void clampDay() {
        int maxDay = daysInMonth(dtEdit.value.year, dtEdit.value.month);
        if (dtEdit.value.day < 1) dtEdit.value.day = 1;
        if (dtEdit.value.day > maxDay) dtEdit.value.day = static_cast<uint8_t>(maxDay);
    }

    UiScreen  screen            = UiScreen::MAIN;
    uint32_t  lastInteractionMs = 0;
    bool      locked            = false;

    // Desde cuándo llevamos suelo estable con el lock activo
    uint32_t  lockGroundStableStartMs = 0;
    uint32_t  lockStartMs             = 0;
    bool      leftGroundSinceLock     = false;

    // Índice de selección de menú (root o el que toque)
    uint8_t   menuIndex         = 0;

    // Valor temporal para edición de offset
    float     offsetEditValue   = 0.0f;

    DateTimeEditState dtEdit{};

    bool suspendRequested = false;

    uint8_t iconMenuIndex = 0;
};
