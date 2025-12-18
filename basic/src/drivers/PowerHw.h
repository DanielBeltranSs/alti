#pragma once
#include <Arduino.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "include/config_power.h"
#include "include/config_pins.h"
#include "core/SleepPolicyService.h"

// Aplica SleepDecision al hardware (CPU freq, light sleep, deep sleep).
class PowerHw {
public:
    void begin() {
        // Nada especial por ahora.
    }

    void apply(const SleepDecision& d) {
        // 1) Frecuencia de CPU
        static uint32_t s_lastCpuFreq = 0;
        if (d.cpuFreqMHz > 0 && d.cpuFreqMHz != s_lastCpuFreq) {
            setCpuFrequencyMhz(d.cpuFreqMHz);
            s_lastCpuFreq = d.cpuFreqMHz;
        }

    #if DEBUG_DISABLE_SLEEP
        return;
    #endif

        // 2) Deep sleep tiene prioridad: si está pedido, lo hacemos
        if (d.enterDeepSleep) {
            enterDeepSleep();
            // esp_deep_sleep_start() no vuelve
        }

        // 3) Light sleep
        if (d.enterLightSleep && d.lightSleepMaxMs > 0) {
            enterLightSleep(d.lightSleepMaxMs);
        }
    }

private:
    // Configura un pin RTC como entrada con PULLDOWN para deep sleep.
    static void configureRtcPulldown(uint8_t pin) {
        gpio_num_t g = static_cast<gpio_num_t>(pin);
        rtc_gpio_init(g);
        rtc_gpio_set_direction(g, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_dis(g);
        rtc_gpio_pulldown_en(g);
    }

    static void enterDeepSleep() {
        Serial.println(F("[POWER] Deep sleep solicitado, preparando..."));

        // 1) Si algún botón está en HIGH, cancelamos deep sleep
        if (digitalRead(PIN_BTN_UP)   == HIGH ||
            digitalRead(PIN_BTN_MID)  == HIGH ||
            digitalRead(PIN_BTN_DOWN) == HIGH) {
            Serial.println(F("[POWER] Deep sleep cancelado: botón en HIGH."));
            return;
        }

        // ⚠️ OPCIONAL:
        // Si quieres evitar deep sleep por inactividad mientras hay cargador
        // conectado, descomenta este bloque:
        /*
        pinMode(PIN_CHARGER_SENSE, INPUT);
        if (digitalRead(PIN_CHARGER_SENSE) == HIGH) {
            Serial.println(F("[POWER] Deep sleep cancelado: cargador presente."));
            return;
        }
        */

        // 2) Configurar pulls RTC para los pines de wake (botones + cargador)
        configureRtcPulldown(PIN_BTN_UP);
        configureRtcPulldown(PIN_BTN_MID);
        configureRtcPulldown(PIN_BTN_DOWN);
        configureRtcPulldown(PIN_CHARGER_SENSE);

        // 3) Configurar fuentes de wake (EXT1) y entrar en deep sleep
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

        const uint64_t mask =
            (1ULL << PIN_BTN_UP)   |
            (1ULL << PIN_BTN_MID)  |
            (1ULL << PIN_BTN_DOWN) |
            (1ULL << PIN_CHARGER_SENSE);

        esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);

        Serial.println(F("[POWER] Entrando a deep sleep..."));
        Serial.flush();
        esp_deep_sleep_start();
    }

    static void enterLightSleep(uint32_t maxMs) {
        uint64_t sleepUs = (uint64_t)maxMs * 1000ULL;

        // No hacer light sleep si algún botón está presionado
        if (digitalRead(PIN_BTN_UP)   == HIGH ||
            digitalRead(PIN_BTN_MID)  == HIGH ||
            digitalRead(PIN_BTN_DOWN) == HIGH) {
            return;
        }

        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

        // Wake por GPIO (dominio digital) como en tu firmware viejo
        gpio_wakeup_enable((gpio_num_t)PIN_BTN_UP,   GPIO_INTR_HIGH_LEVEL);
        gpio_wakeup_enable((gpio_num_t)PIN_BTN_MID,  GPIO_INTR_HIGH_LEVEL);
        gpio_wakeup_enable((gpio_num_t)PIN_BTN_DOWN, GPIO_INTR_HIGH_LEVEL);
        gpio_wakeup_enable((gpio_num_t)PIN_CHARGER_SENSE, GPIO_INTR_HIGH_LEVEL);
        esp_sleep_enable_gpio_wakeup();

        // Wake por timer
        esp_sleep_enable_timer_wakeup(sleepUs);

        Serial.flush();
        esp_light_sleep_start();

        // DEBUG: quién me despertó
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        Serial.print("[POWER] Wakeup cause (LS): ");
        Serial.println((int)cause);
    }
};
