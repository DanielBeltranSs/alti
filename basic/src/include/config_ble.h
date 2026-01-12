#pragma once

// Habilita/Deshabilita la funcionalidad BLE en tiempo de compilación.
// Para la versión básica sin BLE, define BLE_FEATURE_ENABLED a 0 en build_flags
// o ajusta este valor.
#ifndef BLE_FEATURE_ENABLED
#define BLE_FEATURE_ENABLED 1
#endif

// Longitud máxima del nombre BLE (incluyendo prefijo). Debe caber en advertising.
#ifndef BLE_NAME_MAX_LEN
#define BLE_NAME_MAX_LEN 16
#endif
