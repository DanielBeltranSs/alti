#pragma once
#include <Arduino.h>
#include "include/config_pins.h"

// Lee VBAT y presencia de cargador usando divisores 100k/100k.
// - Voltaje filtrado con oversampling + filtro exponencial.
// - % de batería con curva no lineal real de LiPo.
// - Actualización inteligente que previene desfases en descarga.
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

        _initialized         = false;
        _pctInitialized      = false;
        _filteredVoltage     = 0.0f;
        _lastPercent         = 100;
        _lastVoltageSampleMs = 0;
        _lastPercentUpdateMs = 0;
        _chargerInitialized  = false;
        _chargerPresent      = false;
        _lastChargerSampleMs = 0;
    }

    // Voltaje de batería en voltios, filtrado.
    float getBatteryVoltage() {
        uint32_t now   = millis();
        uint32_t gapMs = (_lastVoltageSampleMs == 0) ? 999999 : (now - _lastVoltageSampleMs);

        // Actualizar solo si pasó el período de muestreo (1 segundo)
        // o si es la primera vez
        constexpr uint32_t VOLTAGE_SAMPLE_PERIOD_MS = 1000;
        
        if (!_initialized || gapMs >= VOLTAGE_SAMPLE_PERIOD_MS) {
            float vBatRaw = sampleBatteryVoltageRaw();
            
            // Filtro exponencial (suaviza mucho el ruido)
            const float alpha = 0.05f; // 0.05 ~ muy suave

            if (!_initialized) {
                _filteredVoltage = vBatRaw;
                _initialized     = true;
            } else {
                _filteredVoltage = _filteredVoltage + alpha * (vBatRaw - _filteredVoltage);
            }
            
            _lastVoltageSampleMs = now;
        }

        return _filteredVoltage;
    }

    bool isChargerConnected() {
        uint32_t now = millis();
        
        // Cache de 500ms para reducir lecturas ADC
        constexpr uint32_t CHARGER_SAMPLE_PERIOD_MS = 500;
        
        if (!_chargerInitialized || 
            (now - _lastChargerSampleMs) >= CHARGER_SAMPLE_PERIOD_MS) {
            
            const int   NUM_SAMPLES = 8;
            uint32_t    accMv       = 0;

            for (int i = 0; i < NUM_SAMPLES; ++i) {
                accMv += analogReadMilliVolts(PIN_CHARGER_SENSE);
                delayMicroseconds(200);
            }

            float avgMv = accMv / float(NUM_SAMPLES);
            // Umbral simple: por encima de ~1.5V en el pin (con divisor 1/2 -> ~3V en USB)
            _chargerPresent = (avgMv > 1500.0f);
            _lastChargerSampleMs = now;
            _chargerInitialized  = true;
        }
        
        return _chargerPresent;
    }

    // Usa ésta en el resto del código
    uint8_t getBatteryPercent() {
        uint32_t now     = millis();
        float    v       = getBatteryVoltage();
        bool     charging = isChargerConnected();
        uint8_t  rawPct  = computePercentFromVoltage(v);

        // Inicialización: anclar directo al valor real
        if (!_pctInitialized) {
            _lastPercent         = rawPct;
            _lastPercentUpdateMs = now;
            _pctInitialized      = true;
            
            #ifdef DEBUG_BATTERY
            Serial.printf("[BATT] Init: V=%.3fV raw=%u%%\n", v, rawPct);
            #endif
            
            return _lastPercent;
        }

        uint32_t elapsed = now - _lastPercentUpdateMs;
        int16_t  delta   = (int16_t)rawPct - (int16_t)_lastPercent;

        #ifdef DEBUG_BATTERY
        static uint32_t lastDebug = 0;
        if (now - lastDebug > 10000) { // cada 10s
            Serial.printf("[BATT] V=%.3fV raw=%u%% last=%u%% delta=%d charging=%d elapsed=%lums\n",
                          v, rawPct, _lastPercent, delta, charging ? 1 : 0, elapsed);
            lastDebug = now;
        }
        #endif

        // DESCARGANDO (sin cargador)
        if (!charging) {
            // Condiciones para reanclar directamente:
            // 1. Pasó mucho tiempo sin actualizar (> 2 minutos)
            // 2. Caída brusca detectada (> 15%)
            // 3. Diferencia muy grande y tiempo significativo (> 10% y > 30s)
            if (elapsed > 120000 || delta < -15 || (delta < -10 && elapsed > 30000)) {
                _lastPercent = rawPct;
                _lastPercentUpdateMs = now;
                
                #ifdef DEBUG_BATTERY
                Serial.printf("[BATT] Reancla: V=%.3fV -> %u%% (gap=%lums)\n", 
                              v, rawPct, elapsed);
                #endif
                
                return _lastPercent;
            }

            // Caída normal: permitir descenso controlado
            if (delta < 0) {
                // ✅ FIX: Cast explícito para evitar conflicto de tipos
                uint32_t elapsedSec = elapsed / 1000;
                uint8_t maxDrop = (elapsedSec > 1) ? (uint8_t)elapsedSec : 1;
                uint8_t actualDrop = (uint8_t)(-delta);
                
                if (actualDrop > maxDrop) {
                    _lastPercent -= maxDrop;
                } else {
                    _lastPercent = rawPct; // permitir la caída completa si es razonable
                }
                
                _lastPercentUpdateMs = now;
            }
            // Si rawPct >= _lastPercent: mantener (no sube por ruido)
            
            return _lastPercent;
        }

        // CARGANDO: permitir movimiento suave hacia el valor real
        // Movimiento de 1 en 1 con rate limit
        constexpr uint32_t minUpdateInterval = 1000; // máximo 1% por segundo
        
        if (elapsed >= minUpdateInterval) {
            if (delta > 0) {
                // ✅ FIX: Cast explícito
                uint8_t newPct = _lastPercent + 1;
                _lastPercent = (newPct < rawPct) ? newPct : rawPct;
                _lastPercentUpdateMs = now;
            } else if (delta < 0) {
                // ✅ FIX: Manejo seguro de resta sin overflow
                if (_lastPercent > 0) {
                    uint8_t newPct = _lastPercent - 1;
                    _lastPercent = (newPct > rawPct) ? newPct : rawPct;
                } else {
                    _lastPercent = rawPct;
                }
                _lastPercentUpdateMs = now;
            }
        }

        return _lastPercent;
    }

    // Para debug: obtener el valor raw sin filtrado
    uint8_t getRawPercent() {
        float v = getBatteryVoltage();
        return computePercentFromVoltage(v);
    }

private:
    // Muestreo raw del ADC con oversampling
    float sampleBatteryVoltageRaw() {
        const int   NUM_SAMPLES = 64;
        uint32_t    accMv       = 0;

        for (int i = 0; i < NUM_SAMPLES; ++i) {
            accMv += analogReadMilliVolts(PIN_BATT_VOLTAGE);
            delayMicroseconds(200);
        }

        float avgMv = accMv / float(NUM_SAMPLES);
        float vAdc  = avgMv / 1000.0f;

        // Divisor 100k/100k → VBAT = vAdc * 2
        constexpr float VBAT_DIVIDER_RATIO = 2.0f;
        // Calibración opcional (ajustar si tu multímetro no coincide)
        constexpr float VBAT_CAL = 1.0f;

        return vAdc * VBAT_DIVIDER_RATIO * VBAT_CAL;
    }

    uint8_t computePercentFromVoltage(float v) {
        // Curva de descarga no lineal para LiPo 3.7V (1S)
        // Basada en curva típica con límites 4.10V (100%) y 3.40V (0%)
        
        // Voltajes descendentes (100% -> 0%)
        static const float voltages[] = {
            4.10f, 4.05f, 4.00f, 3.95f, 3.90f, 3.85f, 3.82f, 3.79f,
            3.77f, 3.74f, 3.72f, 3.70f, 3.68f, 3.66f, 3.64f, 3.62f,
            3.60f, 3.58f, 3.55f, 3.50f, 3.45f, 3.40f
        };
        
        // Porcentajes correspondientes
        static const uint8_t percents[] = {
            100, 95, 90, 85, 80, 75, 70, 65,
            60, 55, 50, 45, 40, 35, 30, 25,
            20, 15, 10, 5, 2, 0
        };
        
        static const uint8_t numPoints = sizeof(voltages) / sizeof(voltages[0]);

        // Clamp a rango válido
        if (v >= voltages[0]) return percents[0];  // 100%
        if (v <= voltages[numPoints - 1]) return percents[numPoints - 1];  // 0%

        // Interpolación lineal entre puntos de la LUT
        for (uint8_t i = 0; i < numPoints - 1; i++) {
            if (v <= voltages[i] && v >= voltages[i + 1]) {
                // Ratio entre los dos puntos
                float ratio = (v - voltages[i + 1]) / (voltages[i] - voltages[i + 1]);
                float pct = percents[i + 1] + ratio * (percents[i] - percents[i + 1]);
                return (uint8_t)(pct + 0.5f);  // redondeo
            }
        }

        return 0;  // fallback (no debería llegar aquí)
    }

    bool     _initialized         = false;
    bool     _pctInitialized      = false;
    float    _filteredVoltage     = 0.0f;
    uint8_t  _lastPercent         = 100;
    uint32_t _lastVoltageSampleMs = 0;
    uint32_t _lastPercentUpdateMs = 0;
    
    // Cache para detección de cargador
    bool     _chargerInitialized  = false;
    bool     _chargerPresent      = false;
    uint32_t _lastChargerSampleMs = 0;
};