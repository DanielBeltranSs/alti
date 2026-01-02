#pragma once
#include <Arduino.h>
#include "util/Types.h"
#include "include/config_power.h"
#include "core/UiStateService.h"
#include "core/FlightPhaseService.h"
#include "drivers/BatteryMonitor.h"


// Estructura que describe la decisión de gestión de energía.
struct SleepDecision {
    bool enterLightSleep     = false;
    bool enterDeepSleep      = false;
    uint32_t lightSleepMaxMs = 1000;
    uint32_t cpuFreqMHz      = 40;
    SensorMode sensorMode    = SensorMode::AHORRO_FORCED;
    bool     showZzzHint         = false;
};


// Servicio que decide, en base al estado del sistema, qué hacer con la
// frecuencia de CPU y los modos de sleep. De momento es una implementación
class SleepPolicyService {
public:
    void begin() {}

    SleepDecision evaluate(uint32_t nowMs,
                           UiStateService& ui,
                           const FlightPhaseService& flight,
                           const Settings& settings,
                           BatteryMonitor& battery)
    {
        SleepDecision d;

        // Valores por defecto
        d.cpuFreqMHz      = CPU_FREQ_LOW;
        d.sensorMode      = SensorMode::AHORRO_FORCED;
        d.enterLightSleep = false;
        d.enterDeepSleep  = false;
        d.lightSleepMaxMs = 0;
        d.showZzzHint     = false;

        FlightPhase phase   = flight.getPhase();
        UiScreen   screen   = ui.getScreen();
        bool       locked   = ui.isLocked();
        uint32_t   lastInt  = ui.getLastInteractionMs();
        uint32_t   inactivityMs = (nowMs >= lastInt) ? (nowMs - lastInt) : 0;
        bool       suspendReq = ui.hasSuspendRequest();

        float battVoltage = battery.getBatteryVoltage();
        bool  charger     = battery.isChargerConnected();

        // Constantes de política (tuneables)
        const uint32_t NO_SLEEP_GRACE_MS     = 5000;                // ms tras interacción
        const uint32_t LIGHT_SLEEP_GROUND_MS = 90000;               // light sleep \"grande\" en suelo
        const uint32_t LIGHT_SLEEP_FLIGHT_MS = 20;                  // light sleep corto en vuelo
        const uint32_t ZZZ_HINT_BEFORE_MS    = 5UL * 60UL * 1000UL; // 5 minutos
        const float    LOW_BATT_VOLTAGE      = 3.36f;
        const uint32_t WAKE_GAP_THRESHOLD_MS = 30000;               // gap grande -> asumimos wake de LS larga
        const uint32_t WAKE_GRACE_MS         = 0;                   // sin ventana fija: probes breves
        const uint32_t WAKE_PROBE_SLEEP_MS   = 80;                  // light sleep breve para obtener VS (>30 ms)
        const uint8_t  WAKE_PROBE_COUNT      = 2;                   // nº de ciclos breves tras wake largo

        // Detectar que venimos de un sleep largo (gap grande entre evaluate).
        if (lastEvaluateMs != 0) {
            uint32_t gap = nowMs - lastEvaluateMs;
            if (gap >= WAKE_GAP_THRESHOLD_MS) {
                wakeGraceUntilMs = (WAKE_GRACE_MS > 0) ? (nowMs + WAKE_GRACE_MS) : 0;
                wakeProbeRemaining = WAKE_PROBE_COUNT; // haremos 2 ciclos cortos para medir VS
            }
        }
        lastEvaluateMs = nowMs;

        // Suspender manual: sólo en suelo y sin lock para evitar apagar en vuelo.
        if (suspendReq &&
            !locked &&
            phase == FlightPhase::GROUND)
        {
            (void)ui.consumeSuspendRequest();
            d.enterDeepSleep  = true;
            d.enterLightSleep = false;
            d.lightSleepMaxMs = 0;
            d.cpuFreqMHz      = CPU_FREQ_LOW;
            d.sensorMode      = SensorMode::AHORRO_FORCED;
            d.showZzzHint     = false;
            return d;
        }

        // 1) Si estamos en menú: CPU media, sin sleeps
        if (screen != UiScreen::MAIN) {
            d.cpuFreqMHz = CPU_FREQ_MEDIUM;      // 80 MHz
            d.sensorMode = SensorMode::PRECISO;  // algo más preciso que ahorro
            return d;
        }

        // 2) CPU y modo de sensor base según fase de vuelo
        switch (phase) {
        case FlightPhase::GROUND:
            d.cpuFreqMHz = CPU_FREQ_LOW;         // 40 MHz
            d.sensorMode = SensorMode::AHORRO_FORCED; // forced para bajar consumo del BMP
            break;
        case FlightPhase::CLIMB:
        case FlightPhase::CANOPY:
            d.cpuFreqMHz = CPU_FREQ_MEDIUM;      // 80 MHz soportado
            d.sensorMode = SensorMode::PRECISO;
            break;
        case FlightPhase::FREEFALL:
            d.cpuFreqMHz = CPU_FREQ_HIGH;        // 160 MHz
            d.sensorMode = SensorMode::FREEFALL;
            break;
        }

        // Si detectamos vuelo o lock, cancelamos probes pendientes.
        if (phase != FlightPhase::GROUND || locked) {
            wakeProbeRemaining = 0;
        }

        // Ciclos de "sospecha" tras un wake largo: dos sleeps breves para obtener VS.
        if (!locked &&
            screen == UiScreen::MAIN &&
            phase == FlightPhase::GROUND &&
            wakeProbeRemaining > 0)
        {
            wakeProbeRemaining--;
            d.cpuFreqMHz      = CPU_FREQ_LOW;
            d.sensorMode      = SensorMode::AHORRO_FORCED;
            d.enterLightSleep = true;
            d.lightSleepMaxMs = WAKE_PROBE_SLEEP_MS;
            d.enterDeepSleep  = false;
            d.showZzzHint     = false;
            return d;
        }

        // 3) Lock "rompe modo ahorro": consideramos estado "tipo vuelo"
        //    para la política de sleep, aunque phase == GROUND.
        bool isFlightLike = (phase != FlightPhase::GROUND) || locked;

        // 4) Tiempo de gracia tras interacción o justo tras despertar de LS largo
        bool graceActive =
            (inactivityMs < NO_SLEEP_GRACE_MS) ||
            (wakeGraceUntilMs != 0 && nowMs < wakeGraceUntilMs);

        if (graceActive) {
            return d; // sólo devolvemos CPU + sensorMode
        }

        // 5) Light sleep
        if (isFlightLike) {
            // Vuelo / lock: light sleep corto (no en freefall)
            if (phase != FlightPhase::FREEFALL) {
                d.enterLightSleep   = true;
                d.lightSleepMaxMs   = LIGHT_SLEEP_FLIGHT_MS;
            }
        } else {
            // Suelo sin lock: ahorro agresivo
            d.enterLightSleep   = true;
            d.lightSleepMaxMs   = LIGHT_SLEEP_GROUND_MS;
        }

        // 6) Deep sleep – sólo permitido en suelo y sin lock
        if (!isFlightLike && phase == FlightPhase::GROUND) {
            uint32_t deepTimeoutMs = deepSleepTimeoutForOption(settings.ahorroTimeoutOption);

            bool lowBattery = (battVoltage > 0.1f) && (battVoltage <= LOW_BATT_VOLTAGE);

            // a) Deep sleep inmediato por batería baja (si no hay cargador)
            if (lowBattery && !charger) {
                d.enterDeepSleep  = true;
                d.enterLightSleep = false;
                d.lightSleepMaxMs = 0;
                d.showZzzHint     = false; // se duerme ya, no tiene sentido avisar
                return d;
            }

            // b) Deep sleep por inactividad
            if (deepTimeoutMs > 0) {
                if (inactivityMs >= deepTimeoutMs) {
                    d.enterDeepSleep  = true;
                    d.enterLightSleep = false;
                    d.lightSleepMaxMs = 0;
                } else {
                    // c) Icono "zzz" cuando falten <= 5 min para deep sleep
                    uint32_t hintStartMs =
                        (deepTimeoutMs > ZZZ_HINT_BEFORE_MS)
                            ? (deepTimeoutMs - ZZZ_HINT_BEFORE_MS)
                            : 0;
                    if (inactivityMs >= hintStartMs) {
                        d.showZzzHint = true;
                    }
                }
            }
        }

        return d;
    }

private:
    // Mapea ahorroTimeoutOption (0/1/2) a un timeout real de deep sleep.
    static uint32_t deepSleepTimeoutForOption(uint8_t ahorroOption) {
        // 0 -> 5 min, 1 -> 10 min (default), 2 -> 20 min, 3 -> OFF (sin deep sleep)
        constexpr uint32_t BASE = POWER_IDLE_DEEP_SLEEP_TIMEOUT_MS; // 10 min en config_power.h
        switch (ahorroOption) {
        case 0: return BASE / 2;      // 5 min
        case 1: return BASE;          // 10 min
        case 2: return BASE*2;        // 20 min
        case 3: return 0;             // OFF
        default: return BASE;
        }
    }

    uint32_t lastEvaluateMs   = 0;
    uint32_t wakeGraceUntilMs = 0;
    uint8_t  wakeProbeRemaining = 0;
};
