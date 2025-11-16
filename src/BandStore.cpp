/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: BandStore.cpp                                                                                                 *
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
 * Last Modified: 2025.11.16, Sunday  09:40:30                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "BandStore.h"
#include "Band.h" // BandTable struktúra definíciójához

/**
 * BandTable változó adatainak betöltése a tárolt adatokból
 * @param bandTable A BandTable tömb referenciája
 */
void BandStore::loadToBandTable(BandTable *bandTable) {
    for (uint8_t i = 0; i < BANDTABLE_SIZE; i++) {
        if (data.bands[i].currFreq != 0) {
            bandTable[i].currFreq = data.bands[i].currFreq;
            bandTable[i].currStep = data.bands[i].currStep;
            bandTable[i].currDemod = data.bands[i].currMod;
            bandTable[i].antCap = data.bands[i].antCap;
        }
    }
}

/**
 * BandTable változó adatainak mentése a store-ba
 * @param bandTable A BandTable tömb referenciája
 */
void BandStore::saveFromBandTable(const BandTable *bandTable) {
    for (uint8_t i = 0; i < BANDTABLE_SIZE; i++) {
        data.bands[i].currFreq = bandTable[i].currFreq;
        data.bands[i].currStep = bandTable[i].currStep;
        data.bands[i].currMod = bandTable[i].currDemod;
        data.bands[i].antCap = bandTable[i].antCap;
    }
    // Az adatok megváltoztak, a checkSave() fogja észlelni
}
