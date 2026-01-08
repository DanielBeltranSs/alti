#include <Arduino.h>
#include <LittleFS.h>

#include "include/config_pins.h"
#include "util/Types.h"

#include "core/AppContext.h"
#include "util/DebugConsole.h"

#include "ui/UiInputController.h"
#include "core/LogbookService.h"
#include "core/JumpRecorder.h"
#include "ui/LogbookUi.h"
#include "game/DoomMiniGame.h"

// Instancias globales de servicios y drivers.
SettingsService    gSettingsService;
AltimetryService   gAltimetryService;
FlightPhaseService gFlightPhaseService;
SleepPolicyService gSleepPolicyService;
UiStateService     gUiStateService;
JumpRecorder       gJumpRecorder;

Bmp390Driver       gBmpDriver;
RtcDs3231Driver    gRtcDriver;
LcdDriver          gLcdDriver;
ButtonsDriver      gButtonsDriver;
BatteryMonitor     gBatteryMonitor;
PowerHw            gPowerHw;
UiRenderer         gUiRenderer(&gLcdDriver, &gBatteryMonitor);
LogbookService     gLogbook;
LogbookUi          gLogbookUi(&gLogbook, &gLcdDriver);
DoomMiniGame       gGame;

AppContext         gAppCtx;
Settings           gSettings;
UiInputController  gUiInputController(gUiStateService,
                                      gSettings,
                                      gSettingsService,
                                      gAltimetryService,
                                      gLcdDriver,
                                      gLogbookUi,
                                      gRtcDriver,
                                      gFlightPhaseService);

// Configura AppContext para apuntar a las instancias globales.
void setupContext() {
    gAppCtx.settings   = &gSettingsService;
    gAppCtx.altimetry  = &gAltimetryService;
    gAppCtx.flight     = &gFlightPhaseService;
    gAppCtx.sleep      = &gSleepPolicyService;
    gAppCtx.uiState    = &gUiStateService;

    gAppCtx.bmp        = &gBmpDriver;
    gAppCtx.rtc        = &gRtcDriver;
    gAppCtx.lcd        = &gLcdDriver;
    gAppCtx.buttons    = &gButtonsDriver;
    gAppCtx.battery    = &gBatteryMonitor;
    gAppCtx.power      = &gPowerHw;
    gAppCtx.uiRenderer = &gUiRenderer;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\nAlti Andes boot...");

    // Montar LittleFS (bitácora, etc.) en la partición "spiffs".
    // Importante: no auto-formatear en fallo de mount para no perder bitácora.
    if (!LittleFS.begin(false, "/littlefs", 5, "spiffs")) {
        Serial.println("LittleFS mount failed");
    }

    // Settings en NVS
    if (!gSettingsService.begin()) {
        Serial.println("Settings NVS init failed");
    }
    gSettings = gSettingsService.load();

    // Drivers
    gButtonsDriver.begin();
    gBatteryMonitor.begin();
    gPowerHw.begin();

    if (!gBmpDriver.begin()) {
        Serial.println("BMP390 init failed");
    }

    // RTC: begin() devuelve void, así que sólo lo llamamos
    gRtcDriver.begin();

    if (!gLcdDriver.begin()) {
        Serial.println("LCD init failed");
    }

    // Aplicar orientación guardada (invertPant) tras init
    gLcdDriver.setRotation(gSettings.inverPant);

    // Logbook backend (sólo persistencia; sin UI por ahora)
    gLogbook.begin();

    // Servicios y UI
    gAltimetryService.begin(&gBmpDriver, &gSettings);
    gFlightPhaseService.begin();
    gSleepPolicyService.begin();
    gUiStateService.begin();
    gJumpRecorder.begin(&gLogbook, &gRtcDriver);
    gUiRenderer.begin();
    gGame.begin(&gLcdDriver, &gUiStateService);

    // Contexto
    setupContext();

    Serial.println("Setup complete");
}

void loop() {
    static FlightPhase s_lastPhase = FlightPhase::GROUND;

    uint32_t now = millis();

    // 1) Botones
    ButtonEvent ev;
    while (gButtonsDriver.poll(ev)) {
        gUiStateService.notifyInteraction(now);
        UiScreen currentScreen = gUiStateService.getScreen();

        // Map a logical id respetando inversión
        ButtonId logicalId = ev.id;
        if (gSettings.inverPant) {
            if (ev.id == ButtonId::UP)   logicalId = ButtonId::DOWN;
            else if (ev.id == ButtonId::DOWN) logicalId = ButtonId::UP;
        }

        if (currentScreen == UiScreen::GAME) {
            // Sólo el juego consume eventos; evitamos que la UI cambie de pantalla accidentalmente.
            gGame.handleButton(logicalId, ev.type);
        } else {
            gUiInputController.handleEvent(ev, now);
            // Cualquier botón → la próxima vez que estemos en MAIN/AHORRO se repinta
            gUiRenderer.notifyMainInteraction();
        }
    }

    // 2) Altimetría y fase de vuelo
    gAltimetryService.setLockActive(gUiStateService.isLocked());
    gAltimetryService.update(now);
    AltitudeData alt = gAltimetryService.getAltitudeData();

    FlightPhase prevPhase = FlightPhase::GROUND;
    gFlightPhaseService.update(alt, now, gSettings.unidadMetros, &prevPhase);
    FlightPhase phase = gFlightPhaseService.getPhase();
    gJumpRecorder.update(alt, gSettings.unidadMetros, phase, prevPhase, now);

    // Si la fase cambió, lo consideramos una interacción (resetea inactividad)
    if (phase != s_lastPhase) {
        gUiStateService.notifyInteraction(now);
        s_lastPhase = phase;
    }

    // Auto-unlock del lock en suelo estable
    bool onGround = (phase == FlightPhase::GROUND);
    gUiStateService.updateLockAutoRelease(onGround, alt.isGroundStable, now);

    // 3) Política de energía (CPU, sleeps, modo BMP, Zzz)
    SleepDecision dec = gSleepPolicyService.evaluate(
        now,
        gUiStateService,
        gFlightPhaseService,
        gSettings,
        gBatteryMonitor
    );

    // Aplicar modo del sensor BMP390 según decisión
    gBmpDriver.setMode(dec.sensorMode);

    // 4) Modelo de UI principal
    MainUiModel model;
    model.alt            = alt;
    model.batteryPercent = gBatteryMonitor.getBatteryPercent();
    model.lockActive     = gUiStateService.isLocked();
    model.climbing       = (phase == FlightPhase::CLIMB) && !alt.isGroundStable;
    model.freefall       = (phase == FlightPhase::FREEFALL);
    model.canopy         = (phase == FlightPhase::CANOPY);
    model.minimalFlight  = gSettings.hudMinimalFlight &&
                           (phase == FlightPhase::CLIMB || phase == FlightPhase::FREEFALL);
    model.charging       = gBatteryMonitor.isChargerConnected();
    model.showZzz        = dec.showZzzHint;  // viene de SleepPolicy
    model.temperatureC   = alt.temperatureC;
    model.unit           = gSettings.unidadMetros;
    LogbookService::Stats lbStats{};
    if (gLogbook.getStats(lbStats)) {
        model.totalJumps = lbStats.totalIds;
    }

    // Hora desde el RTC en formato "HH:MM"
    UtcDateTime nowUtc = gRtcDriver.nowUtc();
    snprintf(model.timeText, sizeof(model.timeText),
             "%02u:%02u",
             static_cast<unsigned>(nowUtc.hour),
             static_cast<unsigned>(nowUtc.minute));

        UiScreen screen = gUiStateService.getScreen();

    if (screen == UiScreen::MAIN) {
        // ¿Contexto de ahorro LCD? MAIN + suelo + modo AHORRO + sin lock
        bool inAhorroMain =
            (phase  == FlightPhase::GROUND) &&
            !model.lockActive &&
            (dec.sensorMode == SensorMode::AHORRO ||
             dec.sensorMode == SensorMode::AHORRO_FORCED);

        gUiRenderer.renderMainIfNeeded(model, gSettings.hud, inAhorroMain, screen, now);
    } else if (screen == UiScreen::MENU_ROOT) {
        // Render del menú raíz
        UtcDateTime nowUtc = gRtcDriver.nowUtc();
        uint8_t menuIdx    = gUiStateService.getMenuIndex();
        gUiRenderer.renderMenuRoot(menuIdx, nowUtc, gSettings);
    } else if (screen == UiScreen::MENU_LOGBOOK) {
        gLogbookUi.render(gSettings);
    } else if (screen == UiScreen::MENU_OFFSET) {
        float   off  = gUiStateService.getOffsetEditValue();
        UnitType unit = gSettings.unidadMetros;
        gUiRenderer.renderOffsetEditor(off, unit, gSettings.idioma);
    } else if (screen == UiScreen::MENU_DATETIME) {
        const auto& st = gUiStateService.getDateTimeEdit();
        gUiRenderer.renderDateTimeEditor(st, gSettings.idioma);
    } else if (screen == UiScreen::MENU_ICONS) {
        uint8_t idx = gUiStateService.getIconMenuIndex();
        gUiRenderer.renderIconsMenu(idx, gSettings.hud, gSettings.idioma);
    } else if (screen == UiScreen::GAME) {
        if (!gGame.isRunning()) {
            gGame.start(now);
        }
        gGame.update(now);
    }

    // Auto-cerrar menús (root y icons) tras 6s de inactividad
    if (screen == UiScreen::MENU_ROOT || screen == UiScreen::MENU_ICONS) {
        uint32_t lastInt = gUiStateService.getLastInteractionMs();
        if (now - lastInt >= 6000) {
            gUiStateService.setScreen(UiScreen::MAIN);
            gUiRenderer.notifyMainInteraction();
            screen = UiScreen::MAIN;
        }
    }


    // 6) Debug centralizado
    debugPrintStatus(gAppCtx, gSettings, dec, now);

    // 7) Aplicar decisión de energía (CPU freq, sleeps)
    if (dec.enterDeepSleep) {
        gLcdDriver.prepareForDeepSleep();
    }

    gPowerHw.apply(dec);

    // Si salimos del juego, asegúrate de detener su ciclo
    if (gUiStateService.getScreen() != UiScreen::GAME && gGame.isRunning()) {
        gGame.stop();
    }
}
