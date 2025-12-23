#pragma once
#include <Arduino.h>


#define DEBUG_DISABLE_SLEEP 0
// Default power management settings and thresholds.  These values are
// used by SleepPolicyService to decide when to change CPU frequency,
// enter light sleep, or enter deep sleep.  Adjust these to balance
// responsiveness and battery life.

// Time in milliseconds before the device may enter light sleep when
// idle and in ground state (no menu interaction).
constexpr uint32_t POWER_IDLE_LIGHT_SLEEP_TIMEOUT_MS = 60000; // 60 seconds (valor previo)

// Time in milliseconds of inactivity before entering deep sleep.  Deep
// sleep will only be entered from the GROUND phase when the device
// isn’t locked and the battery level is sufficient.
constexpr uint32_t POWER_IDLE_DEEP_SLEEP_TIMEOUT_MS  = 600000; // 10 minutes

// CPU frequency options (in MHz).  SleepPolicyService will select from
// these based on current activity.
constexpr uint32_t CPU_FREQ_LOW     = 40;
constexpr uint32_t CPU_FREQ_MEDIUM  = 80;
constexpr uint32_t CPU_FREQ_HIGH    = 160;
