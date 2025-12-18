#pragma once
#include <Arduino.h>
#include <math.h>
#include "util/Types.h"

// FlightPhaseService "PRO":
//  - Mantiene la API actual (begin / update / getPhase / timeInCurrentPhase).
//  - Mejora la lógica de transición entre fases usando múltiples umbrales,
//    estados candidatos y temporizadores para acercarse al comportamiento
//    de altímetros de gama alta (Ares / Optima / etc.).
//
// Requiere que AltitudeData entregue:
//  - rawAlt            : altura relativa a un cero (en backend).
//  - verticalSpeed     : velocidad vertical filtrada (m/s en backend).
//  - isGroundStable    : true cuando AltimetryService detecta suelo estable.

class FlightPhaseService {
public:
    void begin() {
        phase                  = FlightPhase::GROUND;
        lastPhaseChangeMs      = millis();

        climbCandidateStartMs  = 0;
        freefallCandidateStartMs = 0;
        canopyCandidateStartMs = 0;
        groundCandidateStartMs = 0;

        groundRefAlt           = 0.0f;
        freefallStartAlt       = 0.0f;
        maxDownVs              = 0.0f;
        hasSeenStrongFall      = false;
        lastVerticalSpeed      = 0.0f;
    }

    // Update the current phase based on altitude data and the current timestamp.
    //
    // unit: unidad actual del backend (para convertir umbrales m/ft).
    // prevPhaseOut: si no es nullptr, devuelve la fase anterior antes de
    //               cualquier transición (útil para generar eventos).
    void update(const AltitudeData& alt,
                uint32_t nowMs,
                UnitType unit,
                FlightPhase* prevPhaseOut = nullptr)
    {
        if (prevPhaseOut) {
            *prevPhaseOut = phase;
        }

        const float M_TO_FT = 3.2808399f;

        // --- Umbrales "en metros" (referencia física) ---
        // Se convierten a la unidad actual para comparar con alt.rawAlt y alt.verticalSpeed.
        constexpr float VS_CLIMB_MIN_M        = 1.5f;   // ~ ascenso claro
        constexpr float CLIMB_GAIN_MIN_M      = 50.0f;  // al menos +50 m sobre el suelo

        constexpr float MIN_EXIT_ALT_M        = 250.0f; // ~800 ft sobre suelo para permitir FF
        constexpr float VS_FREEFALL_M         = -13.0f; // caída fuerte (≈ -42 ft/s)
        constexpr float STRONG_FALL_M         = -20.0f; // "freefall serio" para habilitar canopy

        constexpr float VS_CANOPY_FLOOR_M     = -9.0f;  // por encima de esto solemos estar bajo campana
        constexpr float VS_GROUND_MAX_M       = 0.5f;   // "casi quieto" en suelo
        constexpr float GROUND_ALT_BAND_M     = 2.0f;   // ±2 m alrededor del suelo

        // --- Tiempos de persistencia ---
        constexpr uint32_t CLIMB_PERSIST_MS       = 3000; // CLIMB sostenido
        constexpr uint32_t FREEFALL_CONFIRM_MS    = 400;  // vs fuerte sostenida
        constexpr uint32_t CANOPY_CONFIRM_MS      = 800;  // patrón canopy sostenido
        constexpr uint32_t GROUND_PERSIST_MS      = 2000; // quietud para volver a suelo
        constexpr uint32_t MIN_FREEFALL_MS        = 1500; // al menos 1.5s en FF antes de canopy
        constexpr uint32_t MIN_CANOPY_MS_FOR_LAND = 3000; // tiempo mínimo en canopy antes de suelo opcional

        // --- Conversión de umbrales según unidad ---
        float vsClimbMin      = (unit == UnitType::METERS) ? VS_CLIMB_MIN_M        : VS_CLIMB_MIN_M        * M_TO_FT;
        float climbGainMin    = (unit == UnitType::METERS) ? CLIMB_GAIN_MIN_M      : CLIMB_GAIN_MIN_M      * M_TO_FT;
        float minExitAlt      = (unit == UnitType::METERS) ? MIN_EXIT_ALT_M        : MIN_EXIT_ALT_M        * M_TO_FT;
        float vsFreefall      = (unit == UnitType::METERS) ? VS_FREEFALL_M         : VS_FREEFALL_M         * M_TO_FT;
        float strongFall      = (unit == UnitType::METERS) ? STRONG_FALL_M         : STRONG_FALL_M         * M_TO_FT;
        float vsCanopyFloor   = (unit == UnitType::METERS) ? VS_CANOPY_FLOOR_M     : VS_CANOPY_FLOOR_M     * M_TO_FT;
        float vsGroundMax     = (unit == UnitType::METERS) ? VS_GROUND_MAX_M       : VS_GROUND_MAX_M       * M_TO_FT;
        float groundAltBand   = (unit == UnitType::METERS) ? GROUND_ALT_BAND_M     : GROUND_ALT_BAND_M     * M_TO_FT;

        float altAboveGround  = alt.rawAlt - groundRefAlt;
        float vs              = alt.verticalSpeed;

        // Actualizar referencia de suelo cuando estamos en GROUND y el backend
        // declara suelo estable. Esto ayuda al auto ground-zero gradual.
        if (phase == FlightPhase::GROUND && alt.isGroundStable) {
            groundRefAlt = alt.rawAlt;
            altAboveGround = 0.0f;
        }

        // Actualizar máximos de caída en FREEFALL
        if (phase == FlightPhase::FREEFALL) {
            if (vs < maxDownVs) {
                maxDownVs = vs; // vs es negativo, el más "grande" en magnitud es el más pequeño numéricamente
            }
        }

        switch (phase) {
        case FlightPhase::GROUND: {
            // GROUND -> CLIMB:
            // Requerimos VS positiva clara, ganancia de altura y tiempo de persistencia.
            if (vs > vsClimbMin && altAboveGround > climbGainMin) {
                if (climbCandidateStartMs == 0) {
                    climbCandidateStartMs = nowMs;
                }
                if ((nowMs - climbCandidateStartMs) >= CLIMB_PERSIST_MS) {
                    phase = FlightPhase::CLIMB;
                    lastPhaseChangeMs     = nowMs;
                    climbCandidateStartMs = 0;

                    // Reset de contexto de vuelo
                    freefallCandidateStartMs = 0;
                    canopyCandidateStartMs   = 0;
                    maxDownVs                = 0.0f;
                    hasSeenStrongFall        = false;
                    freefallStartAlt         = 0.0f;
                }
            } else {
                climbCandidateStartMs = 0;
            }
            break;
        }

        case FlightPhase::CLIMB: {
            // CLIMB -> FREEFALL:
            // Sólo si estamos por encima de una altura mínima y la VS es muy negativa
            // durante cierto tiempo.
            bool aboveExitAlt   = (altAboveGround > minExitAlt);
            bool strongDescent  = (vs < vsFreefall);

            if (aboveExitAlt && strongDescent) {
                if (freefallCandidateStartMs == 0) {
                    freefallCandidateStartMs = nowMs;
                    freefallStartAlt         = alt.rawAlt;
                    maxDownVs                = vs; // empezamos a trackear caída
                } else {
                    if (vs < maxDownVs) {
                        maxDownVs = vs;
                    }
                }

                if ((nowMs - freefallCandidateStartMs) >= FREEFALL_CONFIRM_MS) {
                    phase = FlightPhase::FREEFALL;
                    lastPhaseChangeMs      = nowMs;
                    hasSeenStrongFall      = (vs < strongFall); // ya en transición o lo veremos luego
                    canopyCandidateStartMs = 0;
                }
            } else {
                freefallCandidateStartMs = 0;
            }

            // CLIMB -> GROUND (ride-down):
            // Si el altímetro vuelve a estar cerca del suelo y estable sin haber
            // entrado a FREEFALL.
            if (fabsf(altAboveGround) < groundAltBand &&
                fabsf(vs) < vsGroundMax &&
                alt.isGroundStable)
            {
                if (groundCandidateStartMs == 0) {
                    groundCandidateStartMs = nowMs;
                }
                if ((nowMs - groundCandidateStartMs) >= GROUND_PERSIST_MS) {
                    phase = FlightPhase::GROUND;
                    lastPhaseChangeMs      = nowMs;
                    groundCandidateStartMs = 0;
                    freefallCandidateStartMs = 0;
                }
            } else {
                groundCandidateStartMs = 0;
            }

            break;
        }

        case FlightPhase::FREEFALL: {
            uint32_t timeInFF = timeInCurrentPhase(nowMs);

            // Actualizar flag de "strong fall" (freefall serio)
            if (vs < strongFall) {
                hasSeenStrongFall = true;
            }

            // FREEFALL -> CANOPY:
            //  - Debe haber habido una caída fuerte en algún momento (hasSeenStrongFall).
            //  - Llevamos al menos MIN_FREEFALL_MS en FF.
            //  - VS se reduce claramente: |vs| menor que la mitad del máximo de FF
            //    y además por encima del piso de canopy (vsCanopyFloor, es decir, descenso mucho más lento).
            float absMaxDownVs = fabsf(maxDownVs); // maxDownVs es negativo
            float absVs        = fabsf(vs);

            bool canCheckCanopy = (hasSeenStrongFall &&
                                   absMaxDownVs > 0.1f && // evita divisiones raras
                                   timeInFF >= MIN_FREEFALL_MS);

            if (canCheckCanopy) {
                bool vsMuchSlower = (absVs < 0.5f * absMaxDownVs) && (vs > vsCanopyFloor);
                if (vsMuchSlower) {
                    if (canopyCandidateStartMs == 0) {
                        canopyCandidateStartMs = nowMs;
                    }
                    if ((nowMs - canopyCandidateStartMs) >= CANOPY_CONFIRM_MS) {
                        phase = FlightPhase::CANOPY;
                        lastPhaseChangeMs      = nowMs;
                        canopyCandidateStartMs = 0;
                        groundCandidateStartMs = 0;
                    }
                } else {
                    canopyCandidateStartMs = 0;
                }
            } else {
                canopyCandidateStartMs = 0;
            }

            break;
        }

        case FlightPhase::CANOPY: {
            uint32_t timeInCanopy = timeInCurrentPhase(nowMs);

            // CANOPY -> GROUND:
            //  - VS muy pequeña.
            //  - Altitud muy cerca del suelo.
            //  - Backend declara suelo estable.
            //  - (Opcional) tiempo mínimo en canopy para no pasar directo FF->GROUND.
            if (fabsf(vs) < vsGroundMax &&
                fabsf(altAboveGround) < groundAltBand &&
                alt.isGroundStable &&
                timeInCanopy >= MIN_CANOPY_MS_FOR_LAND)
            {
                if (groundCandidateStartMs == 0) {
                    groundCandidateStartMs = nowMs;
                }
                if ((nowMs - groundCandidateStartMs) >= GROUND_PERSIST_MS) {
                    phase = FlightPhase::GROUND;
                    lastPhaseChangeMs      = nowMs;
                    groundCandidateStartMs = 0;

                    // Preparar siguiente ciclo de salto
                    climbCandidateStartMs    = 0;
                    freefallCandidateStartMs = 0;
                    canopyCandidateStartMs   = 0;
                    hasSeenStrongFall        = false;
                    maxDownVs                = 0.0f;
                }
            } else {
                groundCandidateStartMs = 0;
            }

            break;
        }
        } // switch

        // Mantener último VS para posibles lógicas futuras (cruces de signo, etc.).
        lastVerticalSpeed = vs;
    }

    // Retrieve the current flight phase.
    FlightPhase getPhase() const {
        return phase;
    }

    // How long we've been in the current phase.
    uint32_t timeInCurrentPhase(uint32_t nowMs) const {
        return nowMs - lastPhaseChangeMs;
    }

private:
    FlightPhase phase              = FlightPhase::GROUND;
    uint32_t    lastPhaseChangeMs  = 0;

    // Timers para candidatos de transición
    uint32_t    climbCandidateStartMs    = 0;
    uint32_t    freefallCandidateStartMs = 0;
    uint32_t    canopyCandidateStartMs   = 0;
    uint32_t    groundCandidateStartMs   = 0;

    // Referencia de suelo (altura backend donde consideramos "0")
    float       groundRefAlt       = 0.0f;

    // Contexto de freefall
    float       freefallStartAlt   = 0.0f;
    float       maxDownVs          = 0.0f;   // velocidad vertical más negativa en FF
    bool        hasSeenStrongFall  = false;

    // Historial simple de VS
    float       lastVerticalSpeed  = 0.0f;
};
