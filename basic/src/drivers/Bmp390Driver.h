#pragma once
#include <Arduino.h>
#include <Wire.h>

#include "include/config_pins.h"
#include "util/Types.h"  // para SensorMode

// Incluimos la API de Bosch desde src/bmp3
#include "bmp3/bmp3.h"
#include "bmp3/bmp3_defs.h"

// Adaptadores para la API de Bosch (I2C + delay)
// Usan Wire + constantes de la API
namespace {

    // mi 390L es direccion 0x77
    uint8_t g_bmp3_i2c_addr = BMP3_ADDR_I2C_SEC;

    // Función de lectura I2C que la API de Bosch va a usar internamente
    BMP3_INTF_RET_TYPE bmp3_i2c_read(uint8_t reg_addr,
                                     uint8_t *reg_data,
                                     uint32_t len,
                                     void *intf_ptr)
    {
        (void)intf_ptr; // no la usamos, pero la API la pide

        Wire.beginTransmission(g_bmp3_i2c_addr);
        Wire.write(reg_addr);
        if (Wire.endTransmission(false) != 0) {  // false = no STOP, repeated start
            return BMP3_E_COMM_FAIL;
        }

        uint32_t i = 0;
        Wire.requestFrom((uint8_t)g_bmp3_i2c_addr, (uint8_t)len);
        while (Wire.available() && (i < len)) {
            reg_data[i++] = Wire.read();
        }

        return (i == len) ? BMP3_OK : BMP3_E_COMM_FAIL;
    }

    // Función de escritura I2C
    BMP3_INTF_RET_TYPE bmp3_i2c_write(uint8_t reg_addr,
                                      const uint8_t *reg_data,
                                      uint32_t len,
                                      void *intf_ptr)
    {
        (void)intf_ptr;

        Wire.beginTransmission(g_bmp3_i2c_addr);
        Wire.write(reg_addr);
        for (uint32_t i = 0; i < len; ++i) {
            Wire.write(reg_data[i]);
        }
        if (Wire.endTransmission() != 0) {
            return BMP3_E_COMM_FAIL;
        }

        return BMP3_OK;
    }

    // Delay en microsegundos que la API usa internamente
    void bmp3_delay_us(uint32_t period, void *intf_ptr)
    {
        (void)intf_ptr;
        // Para periodos grandes se podría optimizar, pero vale así
        delayMicroseconds(period);
    }
} // namespace anónimo

// Driver de alto nivel para el BMP390, usando la API oficial de Bosch.
class Bmp390Driver {
public:
    // Inicializa I2C + API de Bosch + configura modo AHORRO por defecto.
    bool begin() {
        // Iniciar bus I2C con tus pines
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
        // Frecuencia por defecto: modo ahorro → 100 kHz
        Wire.setClock(100000);

        // Configurar estructura de dispositivo de Bosch
        dev.intf      = BMP3_I2C_INTF;
        dev.read      = bmp3_i2c_read;
        dev.write     = bmp3_i2c_write;
        dev.delay_us  = bmp3_delay_us;
        dev.intf_ptr  = &g_bmp3_i2c_addr;
        dev.calib_data = {};   // por si acaso, limpiamos

        int8_t rslt = bmp3_init(&dev);
        if (rslt != BMP3_OK) {
            Serial.print("bmp3_init error: ");
            Serial.println(rslt);
            initialized = false;
            return false;
        }

        // Configuración base: presión + temperatura activadas, interrupción DRDY
        memset(&settings, 0, sizeof(settings));
        settings.int_settings.drdy_en = BMP3_ENABLE;
        settings.press_en             = BMP3_ENABLE;
        settings.temp_en              = BMP3_ENABLE;

        // 👇 IMPORTANTE: marcar como inicializado ANTES de setMode
        initialized = true;

        // Arrancamos en modo ahorro (esto ahora sí configura el sensor)
        setMode(SensorMode::AHORRO);

        return true;
    }

    // Cambia dinámicamente el modo del sensor según tu diseño:
    // - AHORRO: oversampling alto, IIR fuerte, ODR baja (25 Hz), I2C 100 kHz
    // - PRECISO: oversampling medio, IIR medio, ODR 50 Hz, I2C 400 kHz
    // - FREEFALL: oversampling mínimo, sin filtro, ODR alta (ej. 200 Hz), I2C 400 kHz
    void setMode(SensorMode mode) {
        if (!initialized) {
            // Si begin() falló, no intentamos configurar
            currentMode = mode;
            return;
        }

        forcedMode = (mode == SensorMode::AHORRO_FORCED);

        // Base: siempre presión + temperatura + DRDY
        settings.int_settings.drdy_en = BMP3_ENABLE;
        settings.press_en             = BMP3_ENABLE;
        settings.temp_en              = BMP3_ENABLE;

        switch (mode) {
        case SensorMode::AHORRO:
        case SensorMode::AHORRO_FORCED:
            // Alta precisión pero poca frecuencia, filtro fuerte
            settings.odr_filter.press_os   = BMP3_OVERSAMPLING_8X;
            settings.odr_filter.temp_os    = BMP3_OVERSAMPLING_2X;
            settings.odr_filter.iir_filter = BMP3_IIR_FILTER_COEFF_15;
            settings.odr_filter.odr        = forcedMode ? BMP3_ODR_3_1_HZ : BMP3_ODR_25_HZ;
            Wire.setClock(100000); // 100 kHz para ahorro / forced
            break;

        case SensorMode::PRECISO:
            // Modo “ultra preciso”: compromiso entre ruido y respuesta
            settings.odr_filter.press_os   = BMP3_OVERSAMPLING_4X;
            settings.odr_filter.temp_os    = BMP3_OVERSAMPLING_2X;
            settings.odr_filter.iir_filter = BMP3_IIR_FILTER_COEFF_7;
            settings.odr_filter.odr        = BMP3_ODR_50_HZ;
            Wire.setClock(400000); // 400 kHz
            break;

        case SensorMode::FREEFALL:
            // Alta velocidad, poco oversampling, sin filtro
            settings.odr_filter.press_os   = BMP3_NO_OVERSAMPLING;
            settings.odr_filter.temp_os    = BMP3_OVERSAMPLING_2X;
            settings.odr_filter.iir_filter = BMP3_IIR_FILTER_DISABLE;
            settings.odr_filter.odr        = BMP3_ODR_200_HZ;
            Wire.setClock(400000); // 400 kHz
            break;
        }

        uint16_t sel = 0;
        sel |= BMP3_SEL_PRESS_EN;
        sel |= BMP3_SEL_TEMP_EN;
        sel |= BMP3_SEL_DRDY_EN;
        sel |= BMP3_SEL_PRESS_OS;
        sel |= BMP3_SEL_TEMP_OS;
        sel |= BMP3_SEL_IIR_FILTER;
        sel |= BMP3_SEL_ODR;

        int8_t rslt = bmp3_set_sensor_settings(sel, &settings, &dev);
        if (rslt != BMP3_OK) {
            Serial.print("bmp3_set_sensor_settings error: ");
            Serial.println(rslt);
        }

        // Modo operativo: NORMAL (continuo) o FORCED (toma puntual y duerme)
        settings.op_mode = forcedMode ? BMP3_MODE_FORCED : BMP3_MODE_NORMAL;
        rslt = bmp3_set_op_mode(&settings, &dev);
        if (rslt != BMP3_OK) {
            Serial.print("bmp3_set_op_mode error: ");
            Serial.println(rslt);
        }

        // Reset de cache de lectura forced
        if (forcedMode) {
            lastForcedSampleMs = 0;
            forcedSampleValid  = false;
        }

        currentMode = mode;
    }

    // Lee presión (Pa) y temperatura (°C). Devuelve true si todo OK.
    bool read(float &pressurePa, float &temperatureC) {
        if (!initialized) {
            return false;
        }

        uint32_t now = millis();

        // Throttle en forced: no disparamos una conversión nueva hasta FORCED_MIN_INTERVAL_MS.
        if (forcedMode && forcedSampleValid) {
            if ((now - lastForcedSampleMs) < FORCED_MIN_INTERVAL_MS) {
                pressurePa   = lastPressurePa;
                temperatureC = lastTempC;
                return true;
            }
        }

        int samplesToTake = forcedMode ? FORCED_SAMPLES_PER_READ : 1;
        bool gotSample    = false;

        for (int i = 0; i < samplesToTake; ++i) {
            if (forcedMode) {
                settings.op_mode = BMP3_MODE_FORCED;
                int8_t setRslt = bmp3_set_op_mode(&settings, &dev);
                if (setRslt != BMP3_OK) {
                    continue; // intenta siguiente lectura forced si aplica
                }
            }

            int8_t rslt = bmp3_get_sensor_data(BMP3_PRESS_TEMP, &data, &dev);
            if (rslt != BMP3_OK) {
                // DEBUG: ver por qué falla
                static uint8_t errCount = 0;
                if (errCount < 10) { // no spamear infinito 😅
                    Serial.print("bmp3_get_sensor_data error: ");
                    Serial.println(rslt);
                    errCount++;
                }
                continue;
            }

        #ifdef BMP3_FLOAT_COMPENSATION
            temperatureC = data.temperature;
            pressurePa   = data.pressure;
        #else
            temperatureC = data.temperature / 100.0f;
            pressurePa   = data.pressure   / 100.0f;
        #endif

            gotSample = true;
            // Si necesitamos varias muestras forced, nos quedamos con la última
        }

        if (!gotSample) {
            return false;
        }

        if (forcedMode) {
            lastForcedSampleMs = now;
            lastPressurePa     = pressurePa;
            lastTempC          = temperatureC;
            forcedSampleValid  = true;
        }

        // DEBUG: ver las primeras lecturas de presión / temperatura
        static uint8_t dbgCount = 0;
        if (dbgCount < 10) {
            Serial.print("BMP390 P=");
            Serial.print(pressurePa);
            Serial.print(" Pa, T=");
            Serial.print(temperatureC);
            Serial.println(" C");
            dbgCount++;
        }

        return true;
    }

    SensorMode getMode() const { return currentMode; }

private:
    struct bmp3_dev      dev{};
    struct bmp3_settings settings{};
    struct bmp3_data     data{};
    bool                 initialized = false;
    SensorMode           currentMode = SensorMode::AHORRO;

    bool     forcedMode          = false;
    bool     forcedSampleValid   = false;
    uint32_t lastForcedSampleMs  = 0;
    float    lastPressurePa      = 0.0f;
    float    lastTempC           = 0.0f;

    static constexpr uint32_t FORCED_MIN_INTERVAL_MS = 500; // limita spam en modo forced
    static constexpr int      FORCED_SAMPLES_PER_READ = 2;  // dos lecturas puntuales por wake
};
