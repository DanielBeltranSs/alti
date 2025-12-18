#pragma once
#include "core/SettingsService.h"
#include "core/AltimetryService.h"
#include "core/FlightPhaseService.h"
#include "core/SleepPolicyService.h"
#include "core/UiStateService.h"
#include "drivers/Bmp390Driver.h"
#include "drivers/RtcDs3231Driver.h"
#include "drivers/LcdDriver.h"
#include "drivers/ButtonsDriver.h"
#include "drivers/BatteryMonitor.h"
#include "drivers/PowerHw.h"
#include "ui/UiRenderer.h"

// Centralised context for passing references to the various services
// and drivers used in the application.  This simplifies dependency
// management and allows functions to operate on a shared state.
struct AppContext {
    SettingsService*    settings   = nullptr;
    AltimetryService*   altimetry  = nullptr;
    FlightPhaseService* flight     = nullptr;
    SleepPolicyService* sleep      = nullptr;
    UiStateService*     uiState    = nullptr;

    Bmp390Driver*      bmp        = nullptr;
    RtcDs3231Driver*   rtc        = nullptr;
    LcdDriver*         lcd        = nullptr;
    ButtonsDriver*     buttons    = nullptr;
    BatteryMonitor*    battery    = nullptr;
    PowerHw*           power      = nullptr;
    UiRenderer*        uiRenderer = nullptr;
};