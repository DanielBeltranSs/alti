#pragma once
#include <Arduino.h>
#include "util/Types.h"

// Activa o desactiva el debug globalmente
#ifndef DEBUG_CONSOLE_ENABLED
#define DEBUG_CONSOLE_ENABLED 1
#endif

struct AppContext;
struct Settings;
struct SleepDecision;

void debugPrintStatus(const AppContext& ctx,
                      const Settings& settings,
                      const SleepDecision& dec,
                      uint32_t nowMs);
