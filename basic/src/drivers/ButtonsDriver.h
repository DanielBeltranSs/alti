#pragma once
#include <Arduino.h>
#include "include/config_pins.h"

// Identifiers for the physical buttons on the device.
enum class ButtonId { UP, MID, DOWN };

// Types of button events that can be reported.
enum class ButtonEventType { PRESS, REPEAT, LONG_PRESS_3S, LONG_PRESS_6S, RELEASE };

// Event structure returned by poll().
struct ButtonEvent {
    ButtonId id;
    ButtonEventType type;
    uint32_t timestampMs;
};

// ButtonsDriver detecta pulsaciones y long presses.
// Reacciona inmediato al flanco (como el firmware viejo) pero con
// un pequeño intervalo mínimo entre eventos para evitar rebotes.
class ButtonsDriver {
public:
    void begin() {
        // Botones conectados a 3.3V, activos en HIGH, con PULLDOWN interno
        pinMode(PIN_BTN_UP,   INPUT_PULLDOWN);
        pinMode(PIN_BTN_MID,  INPUT_PULLDOWN);
        pinMode(PIN_BTN_DOWN, INPUT_PULLDOWN);

        uint32_t now = millis();

        for (int i = 0; i < 3; ++i) {
            int pin;
            switch (i) {
                case 0: pin = PIN_BTN_UP;   break;
                case 1: pin = PIN_BTN_MID;  break;
                case 2: pin = PIN_BTN_DOWN; break;
                default: pin = PIN_BTN_UP;  break;
            }

            int r = digitalRead(pin);  // debería ser LOW en reposo

            lastState[i]    = r;
            pressStartMs[i] = 0;
            long3Reported[i] = false;
            long6Reported[i] = false;
            lastEventMs[i]   = now;
            repeating[i]     = false;
            lastRepeatMs[i]  = now;
        }
    }

    // Poll the buttons to see if a new event occurred. Returns true
    // if an event was generated and fills 'ev' with the event data.
    bool poll(ButtonEvent& ev) {
        uint32_t now = millis();

        // Tiempo mínimo entre eventos aceptados por botón.
        // Esto NO obliga a mantener apretado; solo evita dos eventos
        // seguidos demasiado rápidos (rebote).
        constexpr uint32_t MIN_EVENT_INTERVAL_MS = 30;
        constexpr uint32_t REPEAT_DELAY_MS       = 500;
        constexpr uint32_t REPEAT_INTERVAL_MS    = 120;

        // Iterate over the 3 buttons.
        for (int i = 0; i < 3; ++i) {
            uint8_t pin;
            switch (i) {
                case 0: pin = PIN_BTN_UP;   break;
                case 1: pin = PIN_BTN_MID;  break;
                case 2: pin = PIN_BTN_DOWN; break;
                default: pin = PIN_BTN_UP;  break;
            }

            int reading = digitalRead(pin);  // HIGH = pulsado, LOW = suelto

            // 1) Detectar flanco (cambio de estado)
            if (reading != lastState[i]) {
                uint32_t elapsed = now - lastEventMs[i];

                // Si el cambio ocurre demasiado rápido después del último evento,
                // lo tomamos como rebote y lo ignoramos.
                if (elapsed < MIN_EVENT_INTERVAL_MS) {
                    // Actualizamos el estado interno para no quedarnos colgados,
                    // pero no generamos evento.
                    lastState[i] = reading;
                    continue;
                }

                lastState[i]  = reading;
                lastEventMs[i] = now;

                ev.id          = static_cast<ButtonId>(i);
                ev.timestampMs = now;

                if (reading == HIGH) {
                    // Flanco de subida → PRESS inmediato
                    pressStartMs[i]  = now;
                    long3Reported[i] = false;
                    long6Reported[i] = false;
                    repeating[i]     = false;
                    lastRepeatMs[i]  = now;
                    ev.type = ButtonEventType::PRESS;
                } else {
                    // Flanco de bajada → RELEASE
                    ev.type = ButtonEventType::RELEASE;
                    repeating[i] = false;
                }
                return true;
            }

            // 2) Si está estable en HIGH, mirar long-press
            if (lastState[i] == HIGH && pressStartMs[i] != 0) {
                uint32_t held = now - pressStartMs[i];

                // LONG_PRESS_3S: entre 3s y 6s, solo una vez
                if (!long3Reported[i] && held >= 3000 && held < 6000) {
                    long3Reported[i] = true;
                    ev.id            = static_cast<ButtonId>(i);
                    ev.timestampMs   = now;
                    ev.type          = ButtonEventType::LONG_PRESS_3S;
                    return true;
                }

                // LONG_PRESS_6S: a partir de 6s, solo una vez
                if (!long6Reported[i] && held >= 6000) {
                    long6Reported[i] = true;
                    ev.id            = static_cast<ButtonId>(i);
                    ev.timestampMs   = now;
                    ev.type          = ButtonEventType::LONG_PRESS_6S;
                    return true;
                }

                // Repetición acelerada después de un delay
                if (!repeating[i] && held >= REPEAT_DELAY_MS) {
                    repeating[i]    = true;
                    lastRepeatMs[i] = now;
                }

                if (repeating[i] && (now - lastRepeatMs[i] >= REPEAT_INTERVAL_MS)) {
                    lastRepeatMs[i] = now;
                    ev.id           = static_cast<ButtonId>(i);
                    ev.timestampMs  = now;
                    ev.type         = ButtonEventType::REPEAT;
                    return true;
                }
            }
        }

        return false;
    }

private:
    int      lastState[3];      // último estado lógico visto (HIGH/LOW)
    uint32_t pressStartMs[3];   // cuándo empezó la pulsación (para long press)
    bool     long3Reported[3];  // flags de long-press
    bool     long6Reported[3];
    uint32_t lastEventMs[3];    // última vez que emitimos un evento por botón
    bool     repeating[3];
    uint32_t lastRepeatMs[3];
};
