/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: StationStore.h                                                                                                *
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
 * Last Modified: 2025.11.16, Sunday  09:52:50                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once

#include "DebugDataInspector.h"
#include "EepromLayout.h" // EEPROM címek konstansokhoz
#include "StationData.h"
#include "StationStoreBase.h"

// Üres alapértelmezett listák deklarációja (definíció a .cpp fájlban)
extern const FmStationList_t DEFAULT_FM_STATIONS;
extern const AmStationList_t DEFAULT_AM_STATIONS;

// --- FM Station Store ---
class FmStationStore : public BaseStationStore<FmStationList_t, MAX_FM_STATIONS> {
  protected:
    const char *getClassName() const override { return "FmStationStore"; }

    // Felülírjuk a mentést/betöltést a helyes címmel és névvel
    uint16_t performSave() override {
        uint16_t savedCrc = StoreEepromBase<FmStationList_t>::save(getData(), EEPROM_FM_STATIONS_ADDR, getClassName());
#ifdef __DEBUG
        if (savedCrc != 0)
            DebugDataInspector::printFmStationData(getData());
#endif
        return savedCrc;
    }

    uint16_t performLoad() override {
        uint16_t loadedCrc = StoreEepromBase<FmStationList_t>::load(getData(), EEPROM_FM_STATIONS_ADDR, getClassName());
#ifdef __DEBUG
        DebugDataInspector::printFmStationData(getData());
#endif
        // Count ellenőrzés
        if (data.count > MAX_FM_STATIONS) {
            DEBUG("[%s] Figyelem: Az FM állomás számát %d-ról %d-ra korrigáltam.\n", getClassName(), data.count, MAX_FM_STATIONS);
            data.count = MAX_FM_STATIONS;
        }
        return loadedCrc;
    }

  public:
    FmStationStore() : BaseStationStore<FmStationList_t, MAX_FM_STATIONS>() { data = DEFAULT_FM_STATIONS; }

    void loadDefaults() override {
        memcpy(&data, &DEFAULT_FM_STATIONS, sizeof(FmStationList_t));
        // Számoljuk meg hány állomás van a default listában
        data.count = 0;
        for (uint8_t i = 0; i < MAX_FM_STATIONS; i++) {
            if (DEFAULT_FM_STATIONS.stations[i].frequency != 0) {
                data.count++;
            } else {
                break; // Első üres állomásnál megállunk
            }
        }
        DEBUG("FM Station default értékek betöltve. Count: %d\n", data.count);
    }
};

// --- AM Station Store ---
class AmStationStore : public BaseStationStore<AmStationList_t, MAX_AM_STATIONS> {
  protected:
    const char *getClassName() const override { return "AmStationStore"; }

    // Felülírjuk a mentést/betöltést a helyes címmel és névvel
    uint16_t performSave() override {
        uint16_t savedCrc = StoreEepromBase<AmStationList_t>::save(getData(), EEPROM_AM_STATIONS_ADDR, getClassName());
#ifdef __DEBUG
        if (savedCrc != 0)
            DebugDataInspector::printAmStationData(getData());
#endif
        return savedCrc;
    }

    uint16_t performLoad() override {
        uint16_t loadedCrc = StoreEepromBase<AmStationList_t>::load(getData(), EEPROM_AM_STATIONS_ADDR, getClassName());
#ifdef __DEBUG
        DebugDataInspector::printAmStationData(getData());
#endif
        // Count ellenőrzés
        if (data.count > MAX_AM_STATIONS) {
            DEBUG("[%s] Figyelem: Az AM állomás számát %d-ról %d-ra korrigáltam.\n", getClassName(), data.count, MAX_AM_STATIONS);
            data.count = MAX_AM_STATIONS;
        }
        return loadedCrc;
    }

  public:
    AmStationStore() : BaseStationStore<AmStationList_t, MAX_AM_STATIONS>() { data = DEFAULT_AM_STATIONS; }

    void loadDefaults() override {
        memcpy(&data, &DEFAULT_AM_STATIONS, sizeof(AmStationList_t));
        // Számoljuk meg hány állomás van a default listában
        data.count = 0;
        for (uint8_t i = 0; i < MAX_AM_STATIONS; i++) {
            if (DEFAULT_AM_STATIONS.stations[i].frequency != 0) {
                data.count++;
            } else {
                break; // Első üres állomásnál megállunk
            }
        }
        DEBUG("AM Station default értékek betöltve. Count: %d\n", data.count);
    }
};

// Globális példányok deklarációja (definíció a .cpp fájlban)
extern FmStationStore fmStationStore;
extern AmStationStore amStationStore;
