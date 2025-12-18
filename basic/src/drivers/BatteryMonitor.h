#pragma once
#include <Arduino.h>
#include "include/config_pins.h"

// Lee VBAT y presencia de cargador usando divisores 100k/100k.
// - Voltaje filtrado con oversampling + filtro exponencial.
// - % de batería que baja de 1 en 1 sin rebotes al descargar.
// - En carga se permite subir/bajar de 1 en 1 hasta converger.

class BatteryMonitor {
public:
    void begin() {
        analogReadResolution(12);

        // VBAT (divisor 100k/100k, 4.2V -> ~2.1V en el pin)
        // 11 dB da más margen y suele ser más lineal cerca de 2.1V en ESP32.
        analogSetPinAttenuation(PIN_BATT_VOLTAGE, ADC_11db);
        // Cargador (5V -> ~2.5V en el pin)
        analogSetPinAttenuation(PIN_CHARGER_SENSE, ADC_11db);

        pinMode(PIN_CHARGER_SENSE, INPUT);

        _initialized    = false;
        _pctInitialized = false;
        _filteredVoltage = 0.0f;
        _lastPercent     = 100;
    }

    // Voltaje de batería en voltios, filtrado.
    float getBatteryVoltage() {
        // Oversampling fuerte (en mV calibrados)
        const int   NUM_SAMPLES = 64;
        uint32_t    accMv       = 0;

        for (int i = 0; i < NUM_SAMPLES; ++i) {
            accMv += analogReadMilliVolts(PIN_BATT_VOLTAGE);
            delayMicroseconds(200);
        }

        float avgMv = accMv / float(NUM_SAMPLES);

        float vAdc = avgMv / 1000.0f;

        // Divisor 100k/100k → VBAT = vAdc * 2
        constexpr float VBAT_DIVIDER_RATIO = 2.0f;
        // Calibración opcional (ajustar si tu multímetro no coincide)
        constexpr float VBAT_CAL = 1.0f;

        float vBatRaw = vAdc * VBAT_DIVIDER_RATIO * VBAT_CAL;

        // Filtro exponencial (suaviza mucho el ruido)
        const float alpha = 0.05f; // 0.05 ~ muy suave

        if (!_initialized) {
            _filteredVoltage = vBatRaw;
            _initialized     = true;
        } else {
            _filteredVoltage = _filteredVoltage + alpha * (vBatRaw - _filteredVoltage);
        }

        return _filteredVoltage;
    }

    bool isChargerConnected() {
        const int   NUM_SAMPLES = 8;
        uint32_t    accMv       = 0;

        for (int i = 0; i < NUM_SAMPLES; ++i) {
            accMv += analogReadMilliVolts(PIN_CHARGER_SENSE);
            delayMicroseconds(200);
        }

        float avgMv = accMv / float(NUM_SAMPLES);
        // Umbral simple: por encima de ~1.5V en el pin (con divisor 1/2 -> ~3V en USB)
        return (avgMv > 1500.0f);
    }

    // Usa ésta en el resto del código
    uint8_t getBatteryPercent() {
        float   v        = getBatteryVoltage();
        bool    charging = isChargerConnected();
        uint8_t rawPct   = computePercentFromVoltage(v);

        if (!_pctInitialized) {
            _lastPercent    = rawPct;
            _pctInitialized = true;
            return _lastPercent;
        }

        // Descargando (sin cargador): NO permitimos subir por ruido.
        if (!charging) {
            if (rawPct < _lastPercent) {
                // Baja de 1 en 1 como pediste
                _lastPercent -= 1;
            }
            // Si rawPct >= lastPercent, mantenemos el valor (no sube).
            return _lastPercent;
        }

        // Con cargador: dejamos que se mueva de 1 en 1 hasta converger
        if (rawPct > _lastPercent) {
            _lastPercent += 1;
        } else if (rawPct < _lastPercent) {
            _lastPercent -= 1;
        }

        return _lastPercent;
    }

private:
    uint8_t computePercentFromVoltage(float v) {
        // Ajusta estos umbrales según cómo veas la curva real
        constexpr float V_EMPTY = 3.30f; // 0 %
        constexpr float V_FULL  = 4.10f; // 100 %

        if (v <= V_EMPTY) return 0;
        if (v >= V_FULL)  return 100;

        float pct = (v - V_EMPTY) / (V_FULL - V_EMPTY) * 100.0f;

        if (pct < 0.0f)   pct = 0.0f;
        if (pct > 100.0f) pct = 100.0f;

        return static_cast<uint8_t>(pct + 0.5f);
    }

    bool   _initialized       = false;
    bool   _pctInitialized    = false;
    float  _filteredVoltage   = 0.0f;
    uint8_t _lastPercent      = 100;
};
