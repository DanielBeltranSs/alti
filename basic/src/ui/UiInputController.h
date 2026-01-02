#pragma once
#include <Arduino.h>
#include "util/Types.h"
#include "core/UiStateService.h"
#include "core/AltimetryService.h"
#include "drivers/LcdDriver.h"
#include "core/SettingsService.h"
#include "drivers/RtcDs3231Driver.h"
#include "include/config_ui.h"
#include "ui/LogbookUi.h"

class UiInputController {
public:
    UiInputController(UiStateService&    uiState,
                      Settings&          settings,
                      SettingsService&   settingsService,
                      AltimetryService&  altimetry,
                      LcdDriver&         lcd,
                      LogbookUi&         logbookUi,
                      RtcDs3231Driver&   rtc,
                      FlightPhaseService& flightPhase)
        : uiState(uiState),
          settings(settings),
          settingsService(settingsService),
          altimetry(altimetry),
          lcd(lcd),
          logbookUi(logbookUi),
          rtcDrv(rtc),
          flight(&flightPhase)
    {}

    void handleEvent(const ButtonEvent& ev, uint32_t nowMs) {
        (void)nowMs;

        ButtonId logicalId = logicalButtonId(ev.id);
        UiScreen screen    = uiState.getScreen();
        bool     locked    = uiState.isLocked();

        // Lock: la UI debe quedar bloqueada en MAIN. Si por algún motivo
        // estamos en otro screen con lock activo, forzamos vuelta a MAIN.
        // Importante: no retornamos para no “perder” un LONG_PRESS_6S de
        // desbloqueo (sólo se emite una vez).
        if (locked && screen != UiScreen::MAIN) {
            uiState.setScreen(UiScreen::MAIN);
            screen = UiScreen::MAIN;
        }

        switch (screen) {
        case UiScreen::MAIN:
            handleMainScreenButton(ev, logicalId, locked);
            break;
        case UiScreen::MENU_ROOT:
            handleMenuScreenButton(ev, logicalId, locked);
            break;
        case UiScreen::MENU_OFFSET:
            handleOffsetScreenButton(ev, logicalId);
            break;
        case UiScreen::MENU_LOGBOOK:
            handleLogbookScreenButton(ev, logicalId);
            break;
        case UiScreen::MENU_DATETIME:
            handleDateTimeScreenButton(ev, logicalId);
            break;
        case UiScreen::MENU_ICONS:
            handleIconsScreenButton(ev, logicalId);
            break;
        default:
            // Otros submenús en el futuro
            handleMenuScreenButton(ev, logicalId, locked);
            break;
        }
    }

private:
    UiStateService&   uiState;
    Settings&         settings;
    SettingsService&  settingsService;
    AltimetryService& altimetry;
    LcdDriver&        lcd;
    LogbookUi&        logbookUi;
    RtcDs3231Driver&  rtcDrv;
    FlightPhaseService* flight = nullptr;

    // Estado del backlight (para toggle). Arrancamos apagado.
    bool backlightOn = false;
    bool dtReturnOnRelease = false;

    // Mapeo de botones según invertPant (pantalla girada).
    ButtonId logicalButtonId(ButtonId physical) {
        if (!settings.inverPant) {
            return physical;
        }
        // invertido 180°: UP <-> DOWN, MID igual
        if (physical == ButtonId::UP)   return ButtonId::DOWN;
        if (physical == ButtonId::DOWN) return ButtonId::UP;
        return physical; // MID
    }

    uint8_t backlightLevelForSetting(uint8_t brilloSetting) {
        if (brilloSetting == 0) {
            return 0;     // desactivado
        } else {
            return 255;   // activado
        }
    }


    void handleMainScreenButton(const ButtonEvent& ev,
                                ButtonId logicalId,
                                bool locked)
    {
          // UP: toggle backlight (si "Luz" está activada)
        if (logicalId == ButtonId::UP &&
            ev.type == ButtonEventType::PRESS) {

            // Si Luz está desactivada en ajustes, el botón no enciende nada.
            if (settings.brilloPantalla == 0) {
                // Nos aseguramos de que esté realmente apagado
                lcd.setBacklight(0);
                backlightOn = false;
                Serial.println(F("[BTN] Luz desactivada en ajustes -> sin backlight"));
                return;
            }

            if (backlightOn) {
                lcd.setBacklight(0);
                backlightOn = false;
            } else {
                uint8_t lvl = backlightLevelForSetting(settings.brilloPantalla);
                lcd.setBacklight(lvl);
                backlightOn = true;
            }
            return;
        }


        // MID: entra al menú raíz
        if (logicalId == ButtonId::MID &&
            ev.type == ButtonEventType::PRESS) {

            // Lock activo: bloqueamos la UI (no se puede entrar al menú).
            if (locked) {
                return;
            }

            uiState.setMenuIndex(0);              // empezamos desde arriba
            uiState.setScreen(UiScreen::MENU_ROOT);
            Serial.println(F("[BTN] Enter MENU_ROOT from MAIN"));
            return;
        }

        // DOWN: lock / unlock según long-press
        if (logicalId == ButtonId::DOWN) {

            // ACTIVAR LOCK: sólo en suelo (fase GROUND) y desbloqueado
            if (!locked &&
                ev.type == ButtonEventType::LONG_PRESS_3S &&
                flight && flight->getPhase() == FlightPhase::GROUND) {

                uiState.setLocked(true);

                // Recalibra altura según offset configurado
                altimetry.recalibrateGround(settings.alturaOffset);

                Serial.println(F("[LOCK] Activado (3s)"));
                return;
            }

            // DESACTIVAR LOCK: long press 6s desde estado bloqueado
            if (locked &&
                ev.type == ButtonEventType::LONG_PRESS_6S) {

                uiState.setLocked(false);
                Serial.println(F("[LOCK] Desactivado (6s)"));
                return;
            }
        }
    }

    void handleMenuScreenButton(const ButtonEvent& ev,
                                ButtonId logicalId,
                                bool locked)
    {
        (void)locked;

        if (ev.type != ButtonEventType::PRESS &&
            ev.type != ButtonEventType::REPEAT) {
            // Por ahora, sólo reaccionamos a PRESS/REPEAT en menú
            return;
        }

        uint8_t idx = uiState.getMenuIndex();

        // UP / DOWN: navegación por items
        if (logicalId == ButtonId::UP) {
            if (idx == 0) {
                idx = UI_MENU_ITEM_COUNT - 1;
            } else {
                idx -= 1;
            }
            uiState.setMenuIndex(idx);
            Serial.printf("[MENU] Move UP -> idx=%u\n", idx);
            return;
        }

        if (logicalId == ButtonId::DOWN) {
            idx = (idx + 1) % UI_MENU_ITEM_COUNT;
            uiState.setMenuIndex(idx);
            Serial.printf("[MENU] Move DOWN -> idx=%u\n", idx);
            return;
        }

        // MID: seleccionar opción actual
        if (logicalId == ButtonId::MID) {
            handleMenuSelect(idx);
            return;
        }
    }

    void handleMenuSelect(uint8_t idx) {
        // Orden de items (toggles primero, luego subpantallas/acciones):
        // 0: Unidad
        // 1: Brillo
        // 2: Ahorro
        // 3: Invertir
        // 4: Idioma
        // 5: Iconos HUD
        // 6: Bitácora
        // 7: Offset
        // 8: Fecha y hora
        // 9: Suspender (deep sleep manual)
        // 10: Juego (demo)
        // 11: Salir

        switch (idx) {
        case 0: { // Unidad m/ft
            UnitType oldUnit = settings.unidadMetros;
            UnitType newUnit = (oldUnit == UnitType::METERS) ? UnitType::FEET
                                                             : UnitType::METERS;

            // Convertimos el offset a la nueva unidad para mantener el mismo cero físico
            float offset = settings.alturaOffset;
            if (oldUnit == UnitType::METERS && newUnit == UnitType::FEET) {
                offset = offset * M_TO_FT;
            } else if (oldUnit == UnitType::FEET && newUnit == UnitType::METERS) {
                offset = offset / M_TO_FT;
            }

            settings.unidadMetros = newUnit;
            settings.alturaOffset = offset;
            settingsService.save(settings);
            Serial.printf("[MENU] Unidad -> %s\n",
                          settings.unidadMetros == UnitType::METERS ? "m" : "ft");
            break;
        }

        case 1: { // Luz: activado / desactivado
            // 0 = desactivado, 1 = activado
            settings.brilloPantalla = (settings.brilloPantalla == 0) ? 1 : 0;
            settingsService.save(settings);

            Serial.printf("[MENU] Luz -> %s\n",
                          settings.brilloPantalla == 0 ? "desactivada"
                                                       : "activada");

            // Si apagamos Luz mientras el backlight estaba encendido, lo apagamos.
            if (settings.brilloPantalla == 0) {
                lcd.setBacklight(0);
                backlightOn = false;
            } else if (backlightOn) {
                // Luz activada y usuario ya tenía luz encendida: dejamos 255.
                uint8_t lvl = backlightLevelForSetting(settings.brilloPantalla);
                lcd.setBacklight(lvl);
            }
            break;
        }


        case 2: { // Ahorro (deep sleep timeout option 0..2)
            settings.ahorroTimeoutOption =
                (settings.ahorroTimeoutOption + 1) % 4; // agrega OFF (3)
            settingsService.save(settings);
            Serial.printf("[MENU] Ahorro -> option %u\n",
                          static_cast<unsigned>(settings.ahorroTimeoutOption));
            break;
        }

        case 3: { // Invertir pantalla
            settings.inverPant = !settings.inverPant;
            settingsService.save(settings);
            lcd.setRotation(settings.inverPant);
            Serial.printf("[MENU] Invertir -> %s\n",
                          settings.inverPant ? "ON" : "OFF");
            break;
        }

        case 4: { // Idioma ES/EN
            if (settings.idioma == Language::ES) {
                settings.idioma = Language::EN;
            } else {
                settings.idioma = Language::ES;
            }
            settingsService.save(settings);
            Serial.printf("[MENU] Idioma -> %s\n",
                          settings.idioma == Language::ES ? "ES" : "EN");
            break;
        }

        case 5: { // Iconos HUD
            uiState.setIconMenuIndex(0);
            uiState.setScreen(UiScreen::MENU_ICONS);
            Serial.println(F("[MENU] Iconos HUD"));
            break;
        }

        case 6: // Bitácora (TODO submenú)
            logbookUi.enter();
            uiState.setScreen(UiScreen::MENU_LOGBOOK);
            Serial.println(F("[MENU] Bit\u00e1cora -> UI"));
            break;

        case 7: // Offset (editor más adelante)
            uiState.startOffsetEdit(settings.alturaOffset);
            uiState.setScreen(UiScreen::MENU_OFFSET);
            Serial.println(F("[MENU] Offset editor"));
            break;

        case 8: // Fecha y hora (editor más adelante)
            {
                UtcDateTime now = rtcDrv.nowUtc();
                uiState.startDateTimeEdit(now);
                uiState.setScreen(UiScreen::MENU_DATETIME);
                Serial.println(F("[MENU] Fecha/hora -> editor"));
            }
            break;

        case 9: // Suspender
            uiState.requestSuspend();
            uiState.setScreen(UiScreen::MAIN); // volvemos a MAIN para permitir sleep
            Serial.println(F("[MENU] Suspender -> solicitar deep sleep"));
            break;

        case 10: // Juego
            uiState.setScreen(UiScreen::GAME);
            Serial.println(F("[MENU] Juego -> DEMO"));
            break;

        case 11: // Salir del menú
            uiState.setScreen(UiScreen::MAIN);
            Serial.println(F("[MENU] Salir -> MAIN"));
            break;

        default:
            break;
        }
    }

    // --- Offset screen ---
    void handleOffsetScreenButton(const ButtonEvent& ev, ButtonId logicalId) {
        constexpr float STEP_FINE = 1.0f;
        constexpr float STEP_FAST = 5.0f;
        constexpr float STEP_LONG = 25.0f;

        auto applyStep = [&](float step){
            uiState.adjustOffsetEdit(step);
            Serial.printf("[OFFSET] %+0.1f -> %0.1f\n", step, uiState.getOffsetEditValue());
        };

        if (ev.type == ButtonEventType::PRESS || ev.type == ButtonEventType::REPEAT) {
            float step = (ev.type == ButtonEventType::PRESS) ? STEP_FINE : STEP_FAST;
            if (logicalId == ButtonId::UP) {
                applyStep(step);
                return;
            }
            if (logicalId == ButtonId::DOWN) {
                applyStep(-step);
                return;
            }
            if (logicalId == ButtonId::MID && ev.type == ButtonEventType::PRESS) {
                settings.alturaOffset = uiState.getOffsetEditValue();
                settingsService.save(settings);

                // Recalibramos para que el nuevo offset sea efectivo ya
                altimetry.recalibrateGround(settings.alturaOffset);

                uiState.setScreen(UiScreen::MENU_ROOT);
                Serial.printf("[OFFSET] Guardado: %.1f\n", settings.alturaOffset);
                return;
            }
        }

        if (ev.type == ButtonEventType::LONG_PRESS_3S ||
            ev.type == ButtonEventType::LONG_PRESS_6S) {
            float step = (ev.type == ButtonEventType::LONG_PRESS_6S)
                           ? (STEP_LONG * 2.0f)
                           : STEP_LONG;
            if (logicalId == ButtonId::UP) {
                applyStep(step);
            } else if (logicalId == ButtonId::DOWN) {
                applyStep(-step);
            }
        }
    }

    // --- Logbook screen ---
    void handleLogbookScreenButton(const ButtonEvent& ev, ButtonId logicalId) {
        logbookUi.handleEvent(ev, logicalId, uiState, settings);
    }

    // --- Date/time screen ---
    void handleDateTimeScreenButton(const ButtonEvent& ev, ButtonId logicalId) {
        // Si acabamos de guardar/cancelar, con el RELEASE de MID aseguramos salida.
        if (dtReturnOnRelease &&
            logicalId == ButtonId::MID &&
            ev.type == ButtonEventType::RELEASE) {
            dtReturnOnRelease = false;
            uiState.setScreen(UiScreen::MENU_ROOT);
            return;
        }

        // Navegación / ajuste de campo
        if (ev.type == ButtonEventType::PRESS || ev.type == ButtonEventType::REPEAT) {
            int delta = (ev.type == ButtonEventType::PRESS) ? 1 : 5;
            if (logicalId == ButtonId::UP) {
                uiState.adjustDateTimeField(delta);
                return;
            }
            if (logicalId == ButtonId::DOWN) {
                uiState.adjustDateTimeField(-delta);
                return;
            }
            if (logicalId == ButtonId::MID && ev.type == ButtonEventType::PRESS) {
                uiState.advanceDateTimeCursor();
                return;
            }
        }

        // Ajuste grueso con long-press
        if (ev.type == ButtonEventType::LONG_PRESS_3S ||
            ev.type == ButtonEventType::LONG_PRESS_6S) {
            int delta = (ev.type == ButtonEventType::LONG_PRESS_6S) ? 30 : 10;
            if (logicalId == ButtonId::UP) {
                uiState.adjustDateTimeField(delta);
                return;
            }
            if (logicalId == ButtonId::DOWN) {
                uiState.adjustDateTimeField(-delta);
                return;
            }
            if (logicalId == ButtonId::MID && ev.type == ButtonEventType::LONG_PRESS_3S) {
                // Guardar en RTC y volver al menú raíz
                rtcDrv.setUtc(uiState.getDateTimeEdit().value);
                uiState.setScreen(UiScreen::MENU_ROOT);
                dtReturnOnRelease = true; // por si el RELEASE llega luego
                Serial.println(F("[DT] Guardado en RTC"));
                return;
            }
            if (logicalId == ButtonId::MID && ev.type == ButtonEventType::LONG_PRESS_6S) {
                // Cancelar y salir sin guardar
                uiState.setScreen(UiScreen::MENU_ROOT);
                dtReturnOnRelease = true;
                Serial.println(F("[DT] Cancelado (sin guardar)"));
                return;
            }
        }
    }

    // --- Iconos HUD ---
    void handleIconsScreenButton(const ButtonEvent& ev, ButtonId logicalId) {
        if (ev.type != ButtonEventType::PRESS &&
            ev.type != ButtonEventType::REPEAT &&
            ev.type != ButtonEventType::LONG_PRESS_3S &&
            ev.type != ButtonEventType::LONG_PRESS_6S) {
            return;
        }

        uint8_t idx = uiState.getIconMenuIndex();

        auto wrap = [](int v) {
            if (v < 0) return (int)(UI_HUD_MENU_COUNT - 1);
            if (v >= UI_HUD_MENU_COUNT) return 0;
            return v;
        };

        // Navegación
        if (logicalId == ButtonId::UP &&
            (ev.type == ButtonEventType::PRESS || ev.type == ButtonEventType::REPEAT)) {
            idx = (uint8_t)wrap((int)idx - 1);
            uiState.setIconMenuIndex(idx);
            return;
        }

        if (logicalId == ButtonId::DOWN &&
            (ev.type == ButtonEventType::PRESS || ev.type == ButtonEventType::REPEAT)) {
            idx = (uint8_t)wrap((int)idx + 1);
            uiState.setIconMenuIndex(idx);
            return;
        }

        // Toggle
        if (logicalId == ButtonId::MID &&
            ev.type == ButtonEventType::PRESS) {
            if (idx < UI_HUD_ICON_COUNT) {
                toggleHudOption(idx);
            } else {
                uiState.setScreen(UiScreen::MENU_ROOT); // Volver
            }
            return;
        }
    }

    void toggleHudOption(uint8_t idx) {
        switch (idx) {
        case 0: settings.hud.showArrows = !settings.hud.showArrows; break;
        case 1: settings.hud.showTime   = !settings.hud.showTime;   break;
        case 2: settings.hud.showTemp   = !settings.hud.showTemp;   break;
        case 3: settings.hud.showUnits  = !settings.hud.showUnits;  break;
        case 4: settings.hud.showBorder = !settings.hud.showBorder; break;
        case 5: settings.hud.showJumps  = !settings.hud.showJumps;  break;
        default: return;
        }
        settingsService.save(settings);
        Serial.printf("[HUD] idx=%u mask=0x%02X\n", static_cast<unsigned>(idx), settings.hud.toMask());
    }
};
