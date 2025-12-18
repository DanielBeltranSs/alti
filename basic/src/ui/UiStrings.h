#pragma once
#include <Arduino.h>
#include "include/config_ui.h"
#include "util/Types.h"

// Labels de menú raíz por idioma. Mantener el orden consistente con las
// acciones en UiInputController (MenuItemId).
static const char* const UI_MENU_LABELS_ES[UI_MENU_ITEM_COUNT] = {
    "Unidad",
    "Luz",
    "Bitacora",
    "Ahorro",
    "Invertir",
    "Offset",
    "Fecha/hora",
    "Idioma",
    "Salir"
};

static const char* const UI_MENU_LABELS_EN[UI_MENU_ITEM_COUNT] = {
    "Units",
    "Light",
    "Logbook",
    "Sleep",
    "Invert",
    "Offset",
    "Date/Time",
    "Language",
    "Exit"
};

inline const char* uiMenuLabel(Language lang, uint8_t idx) {
    if (idx >= UI_MENU_ITEM_COUNT) return "";
    return (lang == Language::EN) ? UI_MENU_LABELS_EN[idx]
                                  : UI_MENU_LABELS_ES[idx];
}
