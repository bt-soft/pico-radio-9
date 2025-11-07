/**
 * @file main.cpp
 * @brief Pico Radio Core-0 fő programfájlja
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */
#include <Arduino.h>

#include "PicoMemoryInfo.h"
#include "PicoSensorUtils.h"

//-------------------- Config
#include "BandStore.h"
#include "Config.h"
#include "EepromLayout.h"
#include "StationStore.h"
#include "StoreEepromBase.h"
extern Config config;
extern FmStationStore fmStationStore;
extern AmStationStore amStationStore;
extern BandStore bandStore;

/**
 * @brief Core0 main belépő függvénye
 */
void setup() {}

/**
 * @brief Core0 fő ciklus függvénye
 */
void loop() {}