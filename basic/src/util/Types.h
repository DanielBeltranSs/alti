#pragma once
#include <Arduino.h>

// Enum definitions and simple structs used throughout the firmware.

// Supported units for altitude display.  METERS denotes meters, FEET
// denotes feet.
enum class UnitType : uint8_t {
    METERS,
    FEET
};

// Modos de operación del sensor de presión / rendimiento
enum class SensorMode : uint8_t {
    AHORRO,   // suelo / bajo consumo
    PRECISO,  // subida y campana
    FREEFALL  // alta frecuencia en caída libre
};


// Language selections.  Extend as additional languages are supported.
enum class Language : uint8_t {
    ES, // Spanish
    EN  // English
};

// Screens of the UI finite state machine.
enum class UiScreen : uint8_t {
    MAIN,
    MENU_ROOT,
    MENU_UNITS,
    MENU_BRIGHTNESS,
    MENU_LOGBOOK,
    MENU_SLEEP,
    MENU_INVERT,
    MENU_OFFSET,
    MENU_DATETIME,
    MENU_LANGUAGE
};

// Flight phases identified by FlightPhaseService.
enum class FlightPhase : uint8_t {
    GROUND,
    CLIMB,
    FREEFALL,
    CANOPY
};

// Struct representing altitude and motion information.  Computed by
// AltimetryService and used by higher‑level components to decide
// behaviour.
struct AltitudeData {
    float altToShow      = 0.0f; // formatted altitude for UI display
    float rawAlt         = 0.0f; // raw altitude relative to reference
    float verticalSpeed  = 0.0f; // vertical speed in m/s or ft/s
    bool  isGroundStable = true; // whether the ground altitude is stable
    float temperatureC   = NAN;  // ambient temperature (C) from BMP390
};

struct UtcDateTime {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
};
