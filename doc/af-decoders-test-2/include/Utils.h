#pragma once
#include <Arduino.h>

namespace Utils {

/**
 * Eltelt már annyi idő?
 */
inline bool timeHasPassed(long fromWhen, int howLong) { return millis() - fromWhen >= howLong; }

void beepTick();
String formatFrequencyString(float freqHz);

}; // namespace Utils