#pragma once
#include <Arduino.h>

#include "util/Types.h"
#include "drivers/LcdDriver.h"
#include "core/SettingsService.h"
#include "ui/UiModels.h"
#include "ui/MainScreenRenderer.h"
#include "ui/MainRepaintController.h"
#include "ui/MenuRenderer.h"
#include "ui/OffsetScreenRenderer.h"
#include "ui/DateTimeScreenRenderer.h"
#include "ui/IconsScreenRenderer.h"

class BatteryMonitor;

// Orquestador de UI: delega en renderers especializados y controla
// cuándo repintar en contexto de ahorro.
class UiRenderer {
public:
    UiRenderer(LcdDriver* lcdDriver, BatteryMonitor* battMon)
        : lcd(lcdDriver),
          batt(battMon),
          mainRenderer(lcdDriver),
          menuRenderer(lcdDriver),
          offsetRenderer(lcdDriver),
          dateTimeRenderer(lcdDriver),
          iconsRenderer(lcdDriver) {}

    void begin() {
        // Nada especial de momento; LcdDriver::begin() ya inicializa u8g2
    }

    // Aviso de interacción en pantalla principal para forzar repintado.
    void notifyMainInteraction() {
        repaintController.force();
    }

    // Renderiza la pantalla principal según el contexto de ahorro.
    void renderMainIfNeeded(const MainUiModel& model,
                            const HudConfig& hudCfg,
                            bool inAhorroMain,
                            UiScreen screen,
                            uint32_t nowMs)
    {
        if (screen != UiScreen::MAIN) {
            lastScreen = screen;
            return;
        }

        // Volver desde otro screen → repintar
        if (lastScreen != UiScreen::MAIN) {
            repaintController.force();
        }

        bool mustRepaint = false;
        if (inAhorroMain) {
            mustRepaint = repaintController.shouldRepaint(model, hudCfg, nowMs);
        } else {
            repaintController.reset();
            mustRepaint = true;
        }

        if (mustRepaint) {
            repaintCounter++;
            mainRenderer.render(model, hudCfg, repaintCounter);
        }

        lastScreen = screen;
    }

    // Dibuja menú raíz (lista + fecha + número de página + valores actuales)
    void renderMenuRoot(uint8_t selectedIndex,
                        const UtcDateTime& nowUtc,
                        const Settings& settings) {
        menuRenderer.renderRoot(selectedIndex, nowUtc, settings);
    }

    // Renderiza la pantalla de edición de offset
    void renderOffsetEditor(float offsetValue,
                            UnitType unit,
                            Language lang) {
        offsetRenderer.render(offsetValue, unit, lang);
    }

    void renderDateTimeEditor(const UiStateService::DateTimeEditState& st,
                              Language lang) {
        dateTimeRenderer.render(st, lang);
    }

    void renderIconsMenu(uint8_t selectedIdx,
                         const HudConfig& hud,
                         Language lang) {
        iconsRenderer.render(selectedIdx, hud, lang);
    }

private:
    LcdDriver*      lcd  = nullptr;
    BatteryMonitor* batt = nullptr; // reservado para futuro icono batería

    MainScreenRenderer    mainRenderer;
    MenuRenderer          menuRenderer;
    MainRepaintController repaintController;
    OffsetScreenRenderer  offsetRenderer;
    DateTimeScreenRenderer dateTimeRenderer;
    IconsScreenRenderer   iconsRenderer;

    UiScreen  lastScreen     = UiScreen::MAIN;
    uint32_t  repaintCounter = 0;
};
