#pragma once
#include <Arduino.h>
#include "include/config_ui.h"
#include "util/Types.h"

// Labels de menú raíz por idioma. Mantener el orden consistente con las
// acciones en UiInputController (MenuItemId).
static const char* const UI_MENU_LABELS_ES[UI_MENU_ITEM_COUNT] = {
    "Unidad",
    "Luz",
    "Ahorro",
    "Invertir",
    "Idioma",
    "Iconos",
    "Pantalla limpia",
    "Bit\u00e1cora",
    "Offset",
    "Fecha/hora",
    "Suspender",
    "Juego",
    "Salir"
};

static const char* const UI_MENU_LABELS_EN[UI_MENU_ITEM_COUNT] = {
    "Units",
    "Light",
    "Sleep",
    "Invert",
    "Language",
    "Icons",
    "Clean HUD",
    "Logbook",
    "Offset",
    "Date/Time",
    "Suspend",
    "Game",
    "Exit"
};

inline const char* uiMenuLabel(Language lang, uint8_t idx) {
    if (idx >= UI_MENU_ITEM_COUNT) return "";
    return (lang == Language::EN) ? UI_MENU_LABELS_EN[idx]
                                  : UI_MENU_LABELS_ES[idx];
}
