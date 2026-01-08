#pragma once
#include <Arduino.h>
#include "util/Types.h"

// Modelos de datos usados por los renderers de UI.

// Modelo para la pantalla principal.
struct MainUiModel {
    AltitudeData alt;
    uint8_t batteryPercent = 0;
    bool lockActive        = false;
    bool climbing          = false;
    bool freefall          = false;
    bool canopy            = false;
    bool charging          = false;
    bool showZzz           = false;
    char         timeText[6];   // "HH:MM"
    float        temperatureC = NAN;
    UnitType     unit = UnitType::METERS;
    uint32_t     totalJumps    = 0;
    bool         minimalFlight = false; // HUD limpio en CLIMB/FF
};
