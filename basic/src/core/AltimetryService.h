#pragma once
#include <Arduino.h>
#include <math.h>

#include "util/Types.h"
#include "drivers/Bmp390Driver.h"
#include "core/SettingsService.h"

//---------------------------------------------
// Parámetros de altimetría (backend)
//---------------------------------------------
constexpr float ALT_DEADBAND_METERS      =8.0f;    // zona muerta (en metros) antes de llevar a 0 visual
constexpr float GROUND_ALT_THRESH_METERS = 1.0f;    // |altura_rel_suelo| < 1 m -> cerca del suelo
constexpr float GROUND_VS_THRESH_MPS     = 0.3f;    // |velocidad vertical| < 0.3 m/s -> casi quieto
constexpr uint32_t GROUND_STABLE_TIME_MS = 2'000;   // ms continuos para considerar suelo "estable"
constexpr float ALT_FILTER_ALPHA         = 0.2f;    // filtro exponencial para suavizar altura/VS
constexpr float MIN_VS_DT_SECONDS        = 0.03f;   // ignora dt demasiado pequeños (ruido)

// Ecuación barométrica (ISA) para convertir presión relativa en altitud.
// h = BARO_COEFF * (1 - (P / Pref) ^ BARO_EXP)
// Pref = P / (1 - h / BARO_COEFF) ^ BARO_INV_EXP
constexpr float BARO_COEFF    = 44330.0f;       // metros a nivel del mar ISA
constexpr float BARO_EXP      = 0.190294957f;   // 1 / 5.2558797
constexpr float BARO_INV_EXP  = 5.2558797f;
constexpr float M_TO_FT       = 3.2808399f;

// Auto ground-zero (drift lento en suelo)
constexpr float   GZ_DRIFT_STEP_M          = 0.5f;      // ajuste máximo por iteración (m)
constexpr float   GZ_DRIFT_MAX_ABS_M       = 50.0f;     // límite absoluto de drift acumulado
constexpr uint32_t GZ_DRIFT_INTERVAL_MS    = 2000;      // intervalo mínimo entre ajustes

// Movimiento / traslado seguro (auto ground-zero robusto)
constexpr float   STATIONARY_VS_THRESH_MPS   = 0.3f;     // VS baja para considerar quietud
constexpr uint32_t STATIONARY_MIN_MS         = 2000;     // ms en quietud para "stationary"
constexpr float   FAR_FROM_ZERO_M            = 30.0f;    // distancia al cero que arma airborne
constexpr uint32_t FAR_FROM_ZERO_MIN_MS      = 5000;     // tiempo lejos del cero para armar
constexpr float   MOVING_VS_HIGH_MPS         = 2.0f;     // VS típica de avión subiendo
constexpr float   MOVING_PEAK_ALT_GAIN_M     = 200.0f;   // ganancia clara de avión
constexpr uint32_t MOVING_HIGH_VS_TIME_MS    = 10000;    // tiempo de VS alta para avión
constexpr uint32_t MOVEMENT_END_STATIONARY_MS= 30000;    // quietud para cerrar sesión de movimiento
constexpr uint32_t AIRBORNE_CLEAR_MS         = 60000;    // suelo estable para limpiar latch
constexpr uint32_t RELOCATION_STABLE_MS      = 120000;   // quietud prolongada antes de re-cero por traslado
constexpr float   RELOCATION_MIN_DELTA_M     = 10.0f;    // diferencia mínima para re-cero por traslado

//---------------------------------------------
// Servicio de Altimetría
//---------------------------------------------
//
// - Inicializa refPressurePa con la primera lectura válida.
// - Entrega altura relativa (respecto a refPressurePa), convertida a m/ft.
// - Aplica offset de usuario (alturaOffset) en la misma unidad de UI.
// - Calcula velocidad vertical y estado de suelo estable.
// - **Nuevo**: Recalibra automáticamente a 0 una sola vez, al detectar
//   suelo estable por primera vez tras el arranque.
//
class AltimetryService {
public:
    AltimetryService() = default;

    void begin(Bmp390Driver* drv) { begin(drv, nullptr); }

    void begin(Bmp390Driver* drv, const Settings* settingsPtr) {
        bmp                  = drv;
        settings             = settingsPtr;
        altData              = {};
        refPressurePa        = NAN;
        currentAltMeters     = 0.0f;
        filteredAltMeters    = 0.0f;
        lastAltMeters        = 0.0f;
        lastFilteredAlt      = 0.0f;
        lastUpdateMs         = 0;
        groundStableSinceMs  = 0;
        isGroundStableFlag   = false;
        didInitialGroundZero = false;
        driftAccumMeters     = 0.0f;
        lastDriftAdjustMs    = 0;
        stationarySinceMs    = 0;
        farFromZeroSinceMs   = 0;
        airborneArmed        = false;
        vsHighAccumMs        = 0;
        movementActive       = false;
        movementStartMs      = 0;
        moveStartAltMeters   = 0.0f;
        peakAltGainMeters    = 0.0f;
        peakVsUp             = 0.0f;
        timeVsHighMs         = 0;
        lastVehicleStopMs    = 0;
        movementLastSampleMs = 0;
        relocationDone       = false;
        lockActive           = false;
    }

    // Permite deshabilitar recalibraciones automáticas mientras el lock está activo.
    void setLockActive(bool locked) { lockActive = locked; }

    // Llamar cada loop(), pasando millis().
    void update(uint32_t nowMs) {
        if (!bmp) return;

        float pressurePa = 0.0f;
        float tempC      = 0.0f;

        // 1) Leer sensor. Si falla, no tocamos altData.
        if (!bmp->read(pressurePa, tempC)) {
            return;
        }

        // 2) Primera referencia de presión (toma el 0 físico inicial).
        if (!isfinite(refPressurePa)) {
            // No fijamos ref si el valor es absurdo; rango típico ~ 90–110 kPa
            if (pressurePa > 90'000.0f && pressurePa < 110'000.0f) {
                refPressurePa = pressurePa;
            } else {
                // Lectura inválida: espera una próxima vuelta
                return;
            }
        }

        // 3) Altura relativa en metros respecto a refPressurePa (ecuación barométrica ISA).
        float pressureRatio = pressurePa / refPressurePa;
        currentAltMeters = BARO_COEFF * (1.0f - powf(pressureRatio, BARO_EXP));

        // 4) Suavizado de altura y cálculo de velocidad vertical (m/s).
        if (!isfinite(filteredAltMeters)) {
            filteredAltMeters = currentAltMeters;
            lastFilteredAlt   = filteredAltMeters;
        }

        // Filtro exponencial para reducir ruido
        filteredAltMeters = filteredAltMeters +
                            ALT_FILTER_ALPHA * (currentAltMeters - filteredAltMeters);

        float verticalSpeedMps = 0.0f;
        if (lastUpdateMs != 0) {
            float dt = (nowMs - lastUpdateMs) / 1000.0f;
            // Solo consumimos el delta si el dt es suficientemente grande.
            if (dt > MIN_VS_DT_SECONDS) {
                verticalSpeedMps = (filteredAltMeters - lastFilteredAlt) / dt;
                lastFilteredAlt  = filteredAltMeters;
                lastUpdateMs     = nowMs;
            }
            // Si el dt es demasiado pequeño, no actualizamos lastFilteredAlt/lastUpdateMs
            // para no “gastar” el delta y perder la velocidad.
        } else {
            // Primera lectura: inicializar referencias
            lastFilteredAlt = filteredAltMeters;
            lastUpdateMs    = nowMs;
        }
        lastAltMeters   = currentAltMeters;

        // 5) Unidad y offset (desde Settings, si existen)
        UnitType unit       = UnitType::METERS;
        float    offsetUnit = 0.0f;
        if (settings) {
            unit       = settings->unidadMetros;
            offsetUnit = settings->alturaOffset;
        }

        // 6) Offset en METROS para decisiones backend (suelo)
        float offsetMeters = offsetUnit;
        if (unit == UnitType::FEET) {
            offsetMeters = offsetUnit / M_TO_FT;
        }

        // 7) Detección de "suelo" en términos de metros (independiente de unidad UI)
        //    Cerca de suelo => altitud_rel_metros ≈ offset_metros
        float relToGroundMeters = filteredAltMeters - offsetMeters; // 0 cuando UI debería estar en 0
        bool nearGround = fabsf(relToGroundMeters) < GROUND_ALT_THRESH_METERS;
        bool lowVS      = fabsf(verticalSpeedMps)  < GROUND_VS_THRESH_MPS;

        // Quietud genérica (no necesariamente cerca de cero)
        bool isStationary = (fabsf(verticalSpeedMps) < STATIONARY_VS_THRESH_MPS);
        if (isStationary) {
            if (stationarySinceMs == 0) stationarySinceMs = nowMs;
        } else {
            stationarySinceMs = 0;
        }

        if (nearGround && lowVS) {
            if (groundStableSinceMs == 0) {
                groundStableSinceMs = nowMs;
            }
            isGroundStableFlag = (nowMs - groundStableSinceMs) >= GROUND_STABLE_TIME_MS;
        } else {
            groundStableSinceMs = 0;
            isGroundStableFlag  = false;
        }

        // Latch de “airborne” si estamos lejos del cero o con VS alta durante tiempo.
        bool farFromZero = fabsf(relToGroundMeters) > FAR_FROM_ZERO_M;
        if (farFromZero) {
            if (farFromZeroSinceMs == 0) farFromZeroSinceMs = nowMs;
            else if ((nowMs - farFromZeroSinceMs) >= FAR_FROM_ZERO_MIN_MS) {
                airborneArmed = true;
            }
        } else {
            farFromZeroSinceMs = 0;
        }

        if (verticalSpeedMps > MOVING_VS_HIGH_MPS) {
            vsHighAccumMs += (lastUpdateMs != 0) ? (nowMs - lastUpdateMs) : 0;
            if (vsHighAccumMs >= MOVING_HIGH_VS_TIME_MS) {
                airborneArmed = true;
            }
        } else {
            vsHighAccumMs = 0;
        }

        // 8) **Recalibración automática una sola vez** cuando hay suelo estable.
        //    Queremos que la UI muestre 0 => alt_rel_m = 0 => alt_m = offset_m.
        if (!didInitialGroundZero && isGroundStableFlag) {
            refPressurePa        = computeRefPressure(pressurePa, offsetMeters);
            currentAltMeters     = offsetMeters;      // así relToGroundMeters pasa a 0 exacto
            filteredAltMeters    = offsetMeters;
            lastAltMeters        = currentAltMeters;  // evita pico de VS
            lastFilteredAlt      = filteredAltMeters;
            groundStableSinceMs  = nowMs;             // mantenemos estado
            didInitialGroundZero = true;
            driftAccumMeters     = 0.0f;
            lastDriftAdjustMs    = nowMs;
            // No return: continuamos para proyectar a unidad/ui y publicar altData
        }

        // 8b) Auto-ajuste gradual de cero mientras se mantiene suelo estable
        if (isGroundStableFlag) {
            if (lastDriftAdjustMs == 0) {
                lastDriftAdjustMs = nowMs;
            }
            uint32_t elapsed = nowMs - lastDriftAdjustMs;
            if (elapsed >= GZ_DRIFT_INTERVAL_MS) {
                float errorMeters = relToGroundMeters; // cuánto nos alejamos de 0 físico
                // Paso limitado
                float step = errorMeters;
                if (step >  GZ_DRIFT_STEP_M) step =  GZ_DRIFT_STEP_M;
                if (step < -GZ_DRIFT_STEP_M) step = -GZ_DRIFT_STEP_M;

                float newAccum = driftAccumMeters + step;
                if (fabsf(newAccum) <= GZ_DRIFT_MAX_ABS_M) {
                    float targetAlt = offsetMeters - step;
                    refPressurePa   = computeRefPressure(pressurePa, targetAlt);
                    // Ajustamos alturas internas para evitar saltos al usuario
                    currentAltMeters  -= step;
                    filteredAltMeters -= step;
                    lastAltMeters      = currentAltMeters;
                    lastFilteredAlt    = filteredAltMeters;
                    driftAccumMeters   = newAccum;
                }
                lastDriftAdjustMs = nowMs;
            }
        } else {
            // Si salimos de suelo estable, reiniciamos temporizador de drift (no el acumulado)
            lastDriftAdjustMs = 0;
        }

        // Gestión de movimiento (sesiones para clasificar avión vs vehículo).
        bool longStationary = (stationarySinceMs != 0) &&
                              ((nowMs - stationarySinceMs) >= MOVEMENT_END_STATIONARY_MS);

        if (!movementActive && !isStationary) {
            movementActive     = true;
            movementStartMs    = nowMs;
            moveStartAltMeters = filteredAltMeters;
            peakAltGainMeters  = 0.0f;
            peakVsUp           = verticalSpeedMps;
            timeVsHighMs       = 0;
            movementLastSampleMs = nowMs;
            relocationDone     = false; // permitir nuevo re-cero tras este traslado
        }

        if (movementActive) {
            float gain = filteredAltMeters - moveStartAltMeters;
            if (gain > peakAltGainMeters) peakAltGainMeters = gain;
            if (verticalSpeedMps > peakVsUp) peakVsUp = verticalSpeedMps;

            if (verticalSpeedMps > MOVING_VS_HIGH_MPS && movementLastSampleMs != 0) {
                timeVsHighMs += (nowMs - movementLastSampleMs);
            }

            if (longStationary) {
                movementActive = false;
                movementStartMs = 0;
                movementLastSampleMs = nowMs;

                bool aircraftLikely =
                    (peakVsUp > 3.0f) ||
                    (timeVsHighMs >= MOVING_HIGH_VS_TIME_MS) ||
                    (peakAltGainMeters > MOVING_PEAK_ALT_GAIN_M);

                if (aircraftLikely) {
                    airborneArmed = true;
                } else {
                    lastVehicleStopMs = nowMs;
                }
            } else {
                movementLastSampleMs = nowMs;
            }
        }

        // Desarmar latch airborne sólo tras suelo estable prolongado y sin lock.
        if (airborneArmed &&
            isGroundStableFlag &&
            !lockActive &&
            (nowMs - groundStableSinceMs) >= AIRBORNE_CLEAR_MS) {
            airborneArmed = false;
        }

        // Auto re-cero por traslado: sólo si no estamos armados, sin lock, quietos mucho tiempo
        // y lejos del cero actual.
        bool relocationEligible =
            !airborneArmed &&
            !lockActive &&
            (stationarySinceMs != 0) &&
            ((nowMs - stationarySinceMs) >= RELOCATION_STABLE_MS) &&
            (fabsf(relToGroundMeters) > RELOCATION_MIN_DELTA_M) &&
            !relocationDone;

        if (relocationEligible) {
            refPressurePa        = computeRefPressure(pressurePa, offsetMeters);
            currentAltMeters     = offsetMeters;
            filteredAltMeters    = offsetMeters;
            lastAltMeters        = currentAltMeters;
            lastFilteredAlt      = filteredAltMeters;
            driftAccumMeters     = 0.0f;
            lastDriftAdjustMs    = nowMs;
            didInitialGroundZero = true;
            relocationDone       = true;
        }

        // 9) Proyección a unidad del usuario y aplicación de offset
        float altInUnit = filteredAltMeters;
        float vsUnit    = verticalSpeedMps;
        if (unit == UnitType::FEET) {
            altInUnit *= M_TO_FT;
            vsUnit    *= M_TO_FT;
        }

        float altRel    = altInUnit - offsetUnit;  // lo que mostramos
        float deadbandU = ALT_DEADBAND_METERS * ((unit == UnitType::FEET) ? M_TO_FT : 1.0f);
        float altToShow = (fabsf(altRel) < deadbandU) ? 0.0f : altRel;

        // 10) Publicar datos
        altData.rawAlt         = altRel;
        altData.altToShow      = altToShow;
        altData.verticalSpeed  = vsUnit;
        altData.isGroundStable = isGroundStableFlag;
        altData.temperatureC   = tempC;
    }

    // Recalibra el cero a partir de la presión actual, fijando que la UI muestre desiredAltUnit
    // (por defecto 0). Respeta unidades y offset actual.
    void recalibrateGround(float desiredAltUnit = 0.0f) {
        if (!bmp) return;

        float pressurePa = 0.0f;
        float tempC      = 0.0f;
        if (!bmp->read(pressurePa, tempC)) return;

        UnitType unit = UnitType::METERS;
        if (settings) unit = settings->unidadMetros;

        float desiredAltMeters = desiredAltUnit;
        if (unit == UnitType::FEET) {
            desiredAltMeters = desiredAltUnit / M_TO_FT;
        }

        // Queremos que alt_rel_m = desiredAlt_m  =>  alt_m = desiredAlt_m + offset_m
        float offsetMeters = 0.0f;
        if (settings) {
            float offU = settings->alturaOffset;
            offsetMeters = (unit == UnitType::FEET) ? (offU / M_TO_FT) : offU;
        }

        float targetAltMeters = desiredAltMeters + offsetMeters;
        refPressurePa         = computeRefPressure(pressurePa, targetAltMeters);

        // Ajustes auxiliares para evitar saltos
        currentAltMeters = targetAltMeters;
        lastAltMeters    = currentAltMeters;
        filteredAltMeters = targetAltMeters;
        lastFilteredAlt   = filteredAltMeters;
    }

    // Acceso a datos de salida
    AltitudeData getAltitudeData() const { return altData; }

    // Getters útiles para debug
    float getRefPressurePa() const { return refPressurePa; }

private:
    Bmp390Driver*   bmp       = nullptr;
    const Settings* settings  = nullptr;

    AltitudeData altData{};

    // Conversión inversa de la ecuación barométrica: devuelve la presión de referencia
    // necesaria para que la altitud calculada sea targetAltMeters cuando medimos pressurePa.
    float computeRefPressure(float pressurePa, float targetAltMeters) const {
        // Evita valores no físicos (h >= BARO_COEFF haría ratio <= 0)
        float ratio = 1.0f - (targetAltMeters / BARO_COEFF);
        if (ratio <= 0.0f) {
            ratio = 0.01f; // clamp suave para no producir infinitos
        }
        return pressurePa / powf(ratio, BARO_INV_EXP);
    }

    float    refPressurePa        = NAN;
    float    currentAltMeters     = 0.0f;
    float    filteredAltMeters    = 0.0f;
    float    lastAltMeters        = 0.0f;
    float    lastFilteredAlt      = 0.0f;
    uint32_t lastUpdateMs         = 0;

    uint32_t groundStableSinceMs  = 0;
    bool     isGroundStableFlag   = false;
    uint32_t stationarySinceMs    = 0;
    uint32_t farFromZeroSinceMs   = 0;
    uint32_t vsHighAccumMs        = 0;
    bool     airborneArmed        = false;

    // Nuevo: bandera para hacer la recalibración inicial sólo una vez
    bool     didInitialGroundZero = false;

    // Auto ground-zero en suelo estable
    float    driftAccumMeters     = 0.0f;
    uint32_t lastDriftAdjustMs    = 0;

    // Movimiento / traslado
    bool     movementActive       = false;
    uint32_t movementStartMs      = 0;
    uint32_t movementLastSampleMs = 0;
    float    moveStartAltMeters   = 0.0f;
    float    peakAltGainMeters    = 0.0f;
    float    peakVsUp             = 0.0f;
    uint32_t timeVsHighMs         = 0;
    uint32_t lastVehicleStopMs    = 0;
    bool     relocationDone       = false;

    // Guardas
    bool     lockActive           = false;
};
