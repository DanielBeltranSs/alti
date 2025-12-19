#pragma once
#include <Arduino.h>
#include <math.h>
#include "util/Types.h"

// Aplica las reglas de visualización descritas:
// - Entrada: altToShow en la unidad actual (m o ft).
// - isFreefall: si true, aplica cuantización en el tramo con 2 decimales.
inline String formatAltitudeString(float altToShow, bool isFreefall)
{
    float v = altToShow;

    // 1) Cuantización especial en freefall: en el rango que se muestra con
    //    2 decimales (≈1k-10k), limitar a saltos de 50 unidades (0.05k) para
    //    evitar números muy cambiantes durante la caída.
    if (isFreefall) {
        float absV = fabsf(v);
        if (absV >= 999.0f && absV < 9999.0f) {
            constexpr float STEP = 50.0f;
            v = (v >= 0.0f)
                    ? floorf(v / STEP) * STEP
                    : ceilf(v / STEP)  * STEP;
        }
    }

    // 2) Trabajamos con valor absoluto para decidir formato.
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
