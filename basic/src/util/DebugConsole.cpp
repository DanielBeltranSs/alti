#include <Arduino.h>
#include "util/DebugConsole.h"
#include "core/AppContext.h"
#include "core/SleepPolicyService.h"

// Cada cuántas llamadas al loop imprimimos 1 vez.
// Con delay(10) en loop, 200 ~ 2 segundos, 500 ~ 5 segundos, etc.
#ifndef DEBUG_CONSOLE_EVERY_N_CALLS
#define DEBUG_CONSOLE_EVERY_N_CALLS 100
#endif

void debugPrintStatus(const AppContext& ctx,
                      const Settings& settings,
                      const SleepDecision& dec,
                      uint32_t nowMs)
{
#if !DEBUG_CONSOLE_ENABLED
    (void)ctx; (void)settings; (void)dec; (void)nowMs;
    return;
#endif

    static uint32_t callCounter = 0;
    callCounter++;
    if (callCounter % DEBUG_CONSOLE_EVERY_N_CALLS != 0) {
        return; // solo imprimimos 1 de cada N llamadas
    }

    AltitudeData alt = ctx.altimetry->getAltitudeData();
    uint8_t battPct  = ctx.battery->getBatteryPercent();

    // Leer presión directamente del BMP para ver si realmente varía
    float pressurePa = 0.0f;
    float tempC      = 0.0f;
    bool  gotPressure = ctx.bmp->read(pressurePa, tempC);
    Serial.print(F("AltI: "));
    Serial.print(alt.rawAlt, 2);
    Serial.print(F(" u, "));

    if (gotPressure) {
        Serial.print(F("P: "));
        Serial.print(pressurePa, 2);
        Serial.print(F(" Pa, "));
    } else {
        Serial.print(F("P: ERR, "));
    }

Serial.print(F("Battery: "));
Serial.print(battPct);
Serial.print(F("%, CPU: "));
Serial.print(dec.cpuFreqMHz);
Serial.print(F(" MHz, SM: "));

switch (dec.sensorMode) {
case SensorMode::AHORRO:  Serial.print(F("AHORRO"));  break;
case SensorMode::PRECISO: Serial.print(F("PRECISO")); break;
case SensorMode::FREEFALL:Serial.print(F("FREEFALL"));break;
}

Serial.print(F(", Ls:"));
Serial.print(dec.enterLightSleep ? 1 : 0);
Serial.print(F(" ("));
Serial.print(dec.lightSleepMaxMs);
Serial.print(F("ms), Ds:"));
Serial.print(dec.enterDeepSleep ? 1 : 0);
Serial.print(F(", Zzz:"));
Serial.print(dec.showZzzHint ? 1 : 0);
Serial.println();
Serial.println();

(void)settings;
(void)nowMs;
}
