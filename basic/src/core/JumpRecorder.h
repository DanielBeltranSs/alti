#pragma once
#include <Arduino.h>
#include "core/LogbookService.h"
#include "core/AltimetryService.h"
#include "core/FlightPhaseService.h"
#include "drivers/RtcDs3231Driver.h"

// Acumula métricas de un salto basándose en las transiciones de FlightPhaseService.
// Usa altitud filtrada (alt.altToShow pero en unidad interna metros) para vmax y tiempos.
class JumpRecorder {
public:
    void begin(LogbookService* lb, RtcDs3231Driver* rtc) {
        logbook = lb;
        rtcDrv  = rtc;
        reset();
    }

    // Llamar en cada loop con el estado actual.
    void update(const AltitudeData& alt,
                UnitType unit,
                FlightPhase phase,
                FlightPhase prevPhase,
                uint32_t nowMs) {
        // Detectar inicio de salto: GROUND -> CLIMB
        if (prevPhase == FlightPhase::GROUND && phase == FlightPhase::CLIMB) {
            startJump(alt, unit, nowMs);
            Serial.printf("[REC] start jump at %.2f (unit=%s)\n",
                          alt.rawAlt,
                          (unit == UnitType::METERS) ? "m" : "ft");
        }

        // Acumular altura máxima durante CLIMB
        if (jumping && phase == FlightPhase::CLIMB) {
            float altM = toMeters(alt.rawAlt, unit);
            if (!isfinite(maxAltClimb) || altM > maxAltClimb) {
                maxAltClimb = altM;
            }
        }

        // Marcar salida al inicio de FREEFALL
        if (jumping && prevPhase == FlightPhase::CLIMB && phase == FlightPhase::FREEFALL) {
            markExitAndStartFF(unit, nowMs);
            Serial.printf("[REC] enter FF, exit=%.2f m\n", exitAltM);
        }

        // Marcar deploy: FREEFALL -> CANOPY
        if (prevPhase == FlightPhase::FREEFALL && phase == FlightPhase::CANOPY) {
            markDeploy(alt, unit, nowMs);
            Serial.printf("[REC] deploy at %.2f (unit %s)\n",
                          deployAltM,
                          (unit == UnitType::METERS) ? "m" : "ft");
        }

        // Finalizar: requiere fase GROUND y suelo estable por un mínimo
        if (jumping && phase == FlightPhase::GROUND) {
            if (alt.isGroundStable) {
                if (groundStableStart == 0) groundStableStart = nowMs;
                if (nowMs - groundStableStart >= MIN_GROUND_MS) {
                    finalize(alt, unit, nowMs);
                    Serial.println("[REC] finalize jump (ground stable)");
                }
            } else {
                groundStableStart = 0;
            }
        } else {
            groundStableStart = 0;
        }

        // Track vmax mientras estamos en salto
        if (jumping) {
            accumulateVmax(alt, unit, nowMs, phase);
        }
    }

private:
    void reset() {
        jumping       = false;
        deployMarked  = false;
        startMs       = 0;
        ffStartMs     = 0;
        ffEndMs       = 0;
        vmaxFF        = 0.0f;
        vmaxCanopy    = 0.0f;
        exitAltM      = 0.0f;
        deployAltM    = 0.0f;
        maxAltClimb   = NAN;
        groundStableStart = 0;
    }

    void startJump(const AltitudeData& alt, UnitType unit, uint32_t nowMs) {
        jumping      = true;
        deployMarked = false;
        startMs      = nowMs;
        ffStartMs    = 0;
        ffEndMs      = 0;
        vmaxFF       = 0.0f;
        vmaxCanopy   = 0.0f;
        exitAltM     = toMeters(alt.rawAlt, unit);
        deployAltM   = 0.0f;
        maxAltClimb  = exitAltM;
    }

    void markDeploy(const AltitudeData& alt, UnitType unit, uint32_t nowMs) {
        if (!jumping) return;
        deployMarked = true;
        deployAltM   = toMeters(alt.rawAlt, unit);
        ffEndMs      = nowMs;
    }

    void markExitAndStartFF(UnitType unit, uint32_t nowMs) {
        if (!jumping) return;
        if (isfinite(maxAltClimb)) {
            exitAltM = maxAltClimb;
        }
        ffStartMs = nowMs;
    }

    void accumulateVmax(const AltitudeData& alt, UnitType unit, uint32_t nowMs, FlightPhase phase) {
        // Convertir VS a m/s si está en ft/s
        float vMag = fabsf(toMetersPerSecond(alt.verticalSpeed, unit));
        if (phase == FlightPhase::FREEFALL) {
            if (ffStartMs == 0) ffStartMs = nowMs;
            if (vMag > vmaxFF) vmaxFF = vMag;
        } else if (phase == FlightPhase::CANOPY) {
            if (vMag > vmaxCanopy) vmaxCanopy = vMag;
        }
    }

    void finalize(const AltitudeData& alt, UnitType unit, uint32_t nowMs) {
        if (!jumping || !logbook) {
            reset();
            return;
        }

        // Si no hubo deploy marcado, usar alt actual
        if (!deployMarked) {
            deployAltM = toMeters(alt.rawAlt, unit);
            ffEndMs    = nowMs;
        }

        float ffTimeS = 0.0f;
        if (ffStartMs > 0 && ffEndMs > ffStartMs) {
            ffTimeS = (ffEndMs - ffStartMs) / 1000.0f;
        }

        LogbookService::Record rec{};
        rec.tsUtc        = getEpoch();
        rec.exitAltM     = exitAltM;
        rec.deployAltM   = deployAltM;
        rec.freefallTimeS= ffTimeS;
        rec.vmaxFFmps    = vmaxFF;
        rec.vmaxCanopymps= vmaxCanopy;
        rec.flags        = 0;

        bool ok = logbook->append(rec);
        Serial.printf("[REC] append jump id=%lu exit=%.1f deploy=%.1f ff=%.1fs vff=%.1f vcan=%.1f ok=%d\n",
                      (unsigned long)rec.id,
                      rec.exitAltM,
                      rec.deployAltM,
                      rec.freefallTimeS,
                      rec.vmaxFFmps,
                      rec.vmaxCanopymps,
                      ok ? 1 : 0);

        reset();
    }

    uint32_t getEpoch() const {
        if (!rtcDrv) return 0;
        UtcDateTime dt = rtcDrv->nowUtc();
        // Convertir a epoch UTC (sin depender de TZ del sistema)
        return utcToEpoch(dt);
    }

    // days-from-civil (Howard Hinnant) → días desde 1970-01-01 (UTC).
    static int64_t daysFromCivil(int y, unsigned m, unsigned d) {
        y -= (m <= 2);
        const int      era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);                    // [0, 399]
        const unsigned doy = (153 * (m + (m > 2 ? (unsigned)-3 : 9)) + 2) / 5 + d - 1; // [0, 365]
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;        // [0, 146096]
        return (int64_t)era * 146097 + (int64_t)doe - 719468;              // 719468 = 1970-01-01
    }

    static uint32_t utcToEpoch(const UtcDateTime& dt) {
        if (dt.year < 1970) return 0;
        if (dt.month < 1 || dt.month > 12) return 0;
        if (dt.day < 1 || dt.day > 31) return 0;
        if (dt.hour > 23 || dt.minute > 59 || dt.second > 59) return 0;

        int64_t days = daysFromCivil((int)dt.year, (unsigned)dt.month, (unsigned)dt.day);
        int64_t sec  = days * 86400LL +
                      (int64_t)dt.hour   * 3600LL +
                      (int64_t)dt.minute * 60LL +
                      (int64_t)dt.second;
        if (sec < 0) return 0;
        if (sec > 0xFFFFFFFFLL) return 0;
        return (uint32_t)sec;
    }

    static float toMeters(float v, UnitType unit) {
        const float M_TO_FT = 3.2808399f;
        return (unit == UnitType::FEET) ? (v / M_TO_FT) : v;
    }

    static float toMetersPerSecond(float vs, UnitType unit) {
        const float M_TO_FT = 3.2808399f;
        return (unit == UnitType::FEET) ? (vs / M_TO_FT) : vs;
    }

    LogbookService*   logbook = nullptr;
    RtcDs3231Driver*  rtcDrv  = nullptr;

    bool     jumping      = false;
    bool     deployMarked = false;
    uint32_t startMs      = 0;
    uint32_t ffStartMs    = 0;
    uint32_t ffEndMs      = 0;
    float    vmaxFF       = 0.0f;
    float    vmaxCanopy   = 0.0f;
    float    exitAltM     = 0.0f;
    float    deployAltM   = 0.0f;
    float    maxAltClimb  = NAN;
    uint32_t groundStableStart = 0;
    static constexpr uint32_t MIN_GROUND_MS = 2000; // 2s en suelo estable para cerrar
};
