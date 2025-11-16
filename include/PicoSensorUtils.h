/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: PicoSensorUtils.h                                                                                             *
 * Created Date: 2025.11.07.                                                                                           *
 *                                                                                                                     *
 * Author: BT-Soft                                                                                                     *
 * GitHub: https://github.com/bt-soft                                                                                  *
 * Blog: https://electrodiy.blog.hu/                                                                                   *
 * -----                                                                                                               *
 * Copyright (c) 2025 BT-Soft                                                                                          *
 * License: MIT License                                                                                                *
 * 	Bárki szabadon használhatja, módosíthatja, terjeszthet, beépítheti más                                             *
 * 	projektbe (akár zártkódúba is), akár pénzt is kereshet vele                                                        *
 * 	Egyetlen feltétel:                                                                                                 *
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                     *
 * -----                                                                                                               *
 * Last Modified: 2025.11.16, Sunday  09:48:44                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

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
