#pragma once
#include <Arduino.h>
#include <math.h>
#include "util/Types.h"

// Redondea la altitud para freefall a múltiplos de 5
//  - Para positivos: hacia abajo (floor).
//  - Para negativos: hacia arriba (ceil) para mantener sentido del signo.
inline float quantizeAltitudeFreefall(float alt)
{
    if (alt >= 0.0f) {
        return floorf(alt / 5.0f) * 5.0f;
    } else {
        return ceilf(alt / 5.0f) * 5.0f;
    }
}

// Aplica las reglas de visualización descritas:
// - Entrada: altToShow en la unidad actual (m o ft).
// - isFreefall: si true, aplica cuantización a múltiplos de 5.
inline String formatAltitudeString(float altToShow, bool isFreefall)
{
    float v = altToShow;

    // 1) Cuantización especial en freefall
    if (isFreefall) {
        v = quantizeAltitudeFreefall(v);
    }

    // 2) Trabajamos con valor absoluto para decidir formato,
    //    pero conservamos el signo aparte.
    float sign = (v < 0.0f) ? -1.0f : 1.0f;
    float absV = fabsf(v);

    String out;

    if (absV < 999.0f) {
        // Mostrar como entero normal
        long rounded = lroundf(v);   // mantiene el signo
        out = String(rounded);
    }
    else if (absV < 9999.0f) {
        // Mostrar en “miles” con 2 decimales
        float vDisp = (v / 1000.0f);
        // redondeamos a 2 decimales
        vDisp = roundf(vDisp * 100.0f) / 100.0f;
        out = String(vDisp, 2);
    }
    else {
        // Mostrar en “miles” con 1 decimal
        float vDisp = (v / 1000.0f);
        vDisp = roundf(vDisp * 10.0f) / 10.0f;
        out = String(vDisp, 1);
    }

    return out;
}
