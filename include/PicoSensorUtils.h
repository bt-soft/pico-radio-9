#pragma once

#include <cstdint>

#include "defines.h" // PIN_VBUS

namespace PicoSensorUtils {

// Cache konstans
// KRITIKUS: A PicoSensorUtils analogRead() hívásai megzavarják a Core1 audio ADC DMA-t!
// Ezért HOSSZÚ cache időt használunk (30 sec), hogy RITKÁN olvassuk a szenzorokat.
// Így az audio feldolgozás nem szakad meg az ADC csatorna váltások miatt.
#define PICO_SENSORS_CACHE_TIMEOUT_MS (30 * 1000) // 30 másodperc a cache idő (volt: 5 sec)

// Cache struktúra
struct SensorCache {
    float vBusExtValue;            // VBUS KÜLSŐ feszültség utolsó mért értéke (Volt)
    unsigned long vBusExtLastRead; // VBUS utolsó mérésének időpontja (ms)
    bool vBusExtValid;             // VBUS cache érvényessége

    float vBusIntValue;            // VBUS BELSŐ feszültség utolsó mért értéke (Volt)
    unsigned long vBusIntLastRead; // VBUS BELSŐ utolsó mérésének időpontja (ms)
    bool vBusIntValid;             // VBUS BELSŐ cache érvényessége

    float temperatureValue;            // Hőmérséklet utolsó mért értéke (Celsius)
    unsigned long temperatureLastRead; // Hőmérséklet utolsó mérésének időpontja (ms)
    bool temperatureValid;             // Hőmérséklet cache érvényessége

    // Konstruktor: minden értéket alaphelyzetbe állít
    SensorCache()
        : temperatureValue(0.0f), temperatureLastRead(0), temperatureValid(false), //
          vBusExtValue(0.0f), vBusExtLastRead(0), vBusExtValid(false),             //
          vBusIntValue(0.0f), vBusIntLastRead(0), vBusIntValid(false) {}

    SensorCache(const SensorCache &) = default;
};

// Globális cache példány
static SensorCache sensorCache;

/**
 * AD inicializálása
 */
void init();

/**
 * ADC olvasás és VBUS feszültség kiszámítása KÜLSŐ osztóval
 * @return A VBUS mért feszültsége Voltban.
 */
float readVBusExternal();

/**
 * Kiolvassa a processzor hőmérsékletét
 * @return processzor hőmérséklete Celsius fokban
 */
float readCoreTemperature();

}; // namespace PicoSensorUtils
