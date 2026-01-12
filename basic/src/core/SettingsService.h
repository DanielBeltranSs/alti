#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <string.h>
#include "util/Types.h"
#include "include/config_ble.h"

// Servicio de configuración persistente sobre NVS.
// Guarda: unidades, brillo, tiempo de ahorro, offset, idioma, invert, usuario.

struct HudConfig {
    // Iconos opcionales
    bool showArrows = true;
    bool showTime   = true;
    bool showTemp   = true;
    bool showUnits  = true;
    bool showBorder = true;
    bool showJumps  = true;

    uint8_t toMask() const {
        uint8_t m = 0;
        if (showArrows) m |= 1 << 0;
        if (showTime)   m |= 1 << 1;
        if (showTemp)   m |= 1 << 2;
        if (showUnits)  m |= 1 << 3;
        if (showBorder) m |= 1 << 4;
        if (showJumps)  m |= 1 << 5;
        return m;
    }

    static HudConfig fromMask(uint8_t mask) {
        HudConfig c;
        c.showArrows = (mask & (1 << 0)) != 0;
        c.showTime   = (mask & (1 << 1)) != 0;
        c.showTemp   = (mask & (1 << 2)) != 0;
        c.showUnits  = (mask & (1 << 3)) != 0;
        c.showBorder = (mask & (1 << 4)) != 0;
        c.showJumps  = (mask & (1 << 5)) != 0;
        return c;
    }
};

struct Settings {
    UnitType   unidadMetros        = UnitType::METERS;
    uint8_t    brilloPantalla      = 1;     // 0=low, 1=medium, 2=high
    uint8_t    ahorroTimeoutOption = 1;     // índice opciones de deep sleep
    float      alturaOffset        = 0.0f;  // offset de altura (en unidad de usuario)
    Language   idioma              = Language::ES;
    bool       inverPant           = false; // true = invertir pantalla
    uint8_t    usrActual           = 0;     // reservado multi-usuario
    HudConfig  hud;                        // configuración de iconos de pantalla principal
    bool       hudMinimalFlight    = false; // pantalla limpia en CLIMB/FF
    bool       bleEnabled          = false; // BLE activado por usuario (si la build lo soporta)
    char       blePin[7]           = "000000"; // PIN BLE persistente (ASCII 6 dígitos)
    char       bleName[BLE_NAME_MAX_LEN] = "ALTI-0000"; // Nombre visible en advertising
};

class SettingsService {
public:
    // Inicializa NVS. Llamar en setup().
    bool begin() {
        return prefs.begin("alti_cfg", false);
    }

    // Carga configuración desde NVS. Si no hay valores, usa defaults.
    Settings load() {
        Settings s;

        // Unidad: 0=METERS, 1=FEET. Default METERS si desconocido.
        uint8_t unit = prefs.getUChar("unit", static_cast<uint8_t>(UnitType::METERS));
        s.unidadMetros = (unit == static_cast<uint8_t>(UnitType::FEET))
                           ? UnitType::FEET
                           : UnitType::METERS;

        // Brillo
        s.brilloPantalla = prefs.getUChar("bright", 1);

        // Opción de timeout de ahorro
        s.ahorroTimeoutOption = prefs.getUChar("slpopt", 1);
        if (s.ahorroTimeoutOption > 3) {
            s.ahorroTimeoutOption = 1; // valor seguro
        }

        // Offset de altura
        s.alturaOffset = prefs.getFloat("offset", 0.0f);
        Serial.print("[Settings] offset cargado = ");
Serial.println(s.alturaOffset);

        // *** Protección temporal: limpiar offsets corruptos o heredados ***
        // Si el offset es muy grande (por ejemplo, > ±500 unidades),
        // asumimos que viene de una versión vieja / basura y lo reseteamos.
        if (s.alturaOffset < -500.0f || s.alturaOffset > 500.0f) {
            Serial.println("[Settings] offset fuera de rango, reseteando a 0");
            s.alturaOffset = 0.0f;
            prefs.putFloat("offset", 0.0f);  // lo dejamos limpio en NVS
        }

        // Idioma: por defecto ES
        uint8_t lang = prefs.getUChar("lang", static_cast<uint8_t>(Language::ES));
        s.idioma = (lang == static_cast<uint8_t>(Language::EN))
                     ? Language::EN
                     : Language::ES;

        // Pantalla invertida
        s.inverPant = prefs.getBool("invert", false);

        // Usuario actual
        s.usrActual = prefs.getUChar("user", 0);

        // HUD (iconos)
        uint8_t hudMask = prefs.getUChar("hudmask", HudConfig{}.toMask()); // default: ON
        s.hud = HudConfig::fromMask(hudMask);

        // Pantalla limpia en vuelo/FF (off por defecto)
        s.hudMinimalFlight = prefs.getBool("minhud", false);

#if BLE_FEATURE_ENABLED
        // BLE on/off
        s.bleEnabled = prefs.getBool("ble", false);

        // Nombre BLE
        String name = prefs.getString("blename", "");
        if (name.length() < 4 || name.length() >= BLE_NAME_MAX_LEN) {
            name = generateNameFromMac();
            prefs.putString("blename", name);
        }
        name.toCharArray(s.bleName, sizeof(s.bleName));

        // PIN BLE persistente: 6 dígitos ASCII. Si no existe, se genera con la MAC.
        String pin = prefs.getString("pin", "");
        if (pin.length() != 6) {
            pin = generatePinFromMac();
            prefs.putString("pin", pin);
        }
        pin.toCharArray(s.blePin, sizeof(s.blePin));
#else
        s.bleEnabled = false;
        strncpy(s.blePin, "000000", sizeof(s.blePin));
        strncpy(s.bleName, "ALTI-0000", sizeof(s.bleName));
#endif

        return s;
    }

    // Guarda configuración en NVS.
    void save(const Settings& s) {
        prefs.putUChar("unit",   static_cast<uint8_t>(s.unidadMetros));
        prefs.putUChar("bright", s.brilloPantalla);
        prefs.putUChar("slpopt", s.ahorroTimeoutOption);
        prefs.putFloat("offset", s.alturaOffset);
        prefs.putUChar("lang",   static_cast<uint8_t>(s.idioma));
        prefs.putBool("invert",  s.inverPant);
        prefs.putUChar("user",   s.usrActual);
        prefs.putUChar("hudmask", s.hud.toMask());
        prefs.putBool("minhud",  s.hudMinimalFlight);
#if BLE_FEATURE_ENABLED
        prefs.putBool("ble",     s.bleEnabled);
        prefs.putString("blename", s.bleName);
        prefs.putString("pin",   s.blePin);
#endif
    }

private:
    // Genera un PIN de 6 dígitos a partir de la MAC por defecto (determinista por dispositivo).
    static String generatePinFromMac() {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac); // MAC base única por dispositivo
        uint32_t v = 0;
        for (int i = 0; i < 6; ++i) {
            v = (v << 5) ^ mac[i] ^ (v >> 2);
        }
        // LCG simple para mezclar y limitar a 6 dígitos
        v = (1103515245u * v + 12345u) & 0x7fffffff;
        uint32_t pin = (v % 900000u) + 100000u; // rango 100000..999999
        char buf[7];
        snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(pin));
        return String(buf);
    }

    // Genera nombre "ALTI-XXXX" desde la MAC
    static String generateNameFromMac() {
        uint8_t mac[6] = {0};
        esp_efuse_mac_get_default(mac);
        char buf[BLE_NAME_MAX_LEN];
        snprintf(buf, sizeof(buf), "ALTI-%02X%02X", mac[4], mac[5]);
        return String(buf);
    }

    Preferences prefs;
};
