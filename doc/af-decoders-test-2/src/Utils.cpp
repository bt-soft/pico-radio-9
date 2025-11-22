#include <Arduino.h>

#include "Utils.h"
#include "externs_api.h"
#include "pins.h"

namespace Utils {
/**
 *  Pitty hangjelzés
 */
void beepTick() {
    // Csak akkor csipogunk, ha a beeper engedélyezve van
    if (!beeperEnabled)
        return;
    tone(PIN_BEEPER, 800);
    delay(10);
    noTone(PIN_BEEPER);
}

/**
 * @brief Frekvencia formázása: ha egész, akkor csak egész, ha van tizedes, akkor max 1 tizedesjegy (ha nem nulla)
 * @param freqHz frekvencia Hz-ben
 */
String formatFrequencyString(float freqHz) {
    String str;
    if (freqHz >= 1000.0f) {
        float freqKHz = freqHz / 1000.0f;
        int freqInt = static_cast<int>(freqKHz);
        float frac = freqKHz - freqInt;
        if (fabs(frac) < 0.05f) {
            str = String(freqInt) + "kHz";
        } else {
            str = String(freqKHz, 1) + "kHz";
        }
    } else {
        str = String(static_cast<int>(freqHz + 0.5f)) + "Hz";
        // Elapsed time string formatting
    }
    return str;
}

}; // namespace Utils