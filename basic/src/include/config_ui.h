#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// Configuration constants for the UI layer.  This header defines
// compile‑time settings such as display strings, font sizes, and menu
// options.  Extend this file as you implement the UI.

// Placeholder definitions for brightness levels (0=low, 1=medium, 2=high)
constexpr uint8_t UI_BRIGHTNESS_LOW  = 0;
constexpr uint8_t UI_BRIGHTNESS_MED  = 1;
constexpr uint8_t UI_BRIGHTNESS_HIGH = 2;

// Language strings could be defined here. For example, menu labels in
// Spanish and English.  You can expand this when implementing UiRenderer.
// Example:
// constexpr const char* STR_MENU_UNITS_ES = "Unidades";
// constexpr const char* STR_MENU_UNITS_EN = "Units";

constexpr const uint8_t* UI_FONT_ALT_MAIN    = u8g2_font_logisoso32_tn; // alturas siempre logisoso
constexpr const uint8_t* UI_FONT_TEXT_EN     = u8g2_font_6x10_tf;       // inglés (ASCII)
// Español: fuente con tildes/ñ (Latin-1)
// Español: variante más pequeña con Latin‑1 para tildes/ñ
constexpr const uint8_t* UI_FONT_TEXT_ES     = u8g2_font_6x13_tf;
constexpr const uint8_t* UI_FONT_TEXT_SMALL  = UI_FONT_TEXT_EN;         // alias legacy

// Fuente para modo “pantalla limpia” (altura grande). Ajusta si quieres mayor tamaño.
constexpr const uint8_t* UI_FONT_ALT_CLEAR   = u8g2_font_logisoso50_tn;
// Línea base para modo claro (ajusta para probar centrado vertical)
constexpr uint8_t UI_CLEAR_ALT_Y             = 58;

constexpr uint8_t UI_MENU_ITEM_COUNT = 13;

// Número de iconos configurables en la pantalla principal.
// Iconos configurables (flechas, hora, temperatura, unidad, borde, saltos) + opción de volver.
constexpr uint8_t UI_HUD_ICON_COUNT = 6;
constexpr uint8_t UI_HUD_MENU_COUNT = UI_HUD_ICON_COUNT + 1; // incluye "Volver"
