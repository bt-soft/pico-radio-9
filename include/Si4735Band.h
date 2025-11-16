/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: Si4735Band.h                                                                                                  *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                       *
 * -----                                                                                                               *
 * Last Modified: 2025.11.16, Sunday  09:52:03                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "Band.h"
#include "Si4735Runtime.h"

class Si4735Band : public Si4735Runtime, public Band {

  private:
    // SSB betöltve?
    bool ssbLoaded;

    /**
     * SSB patch betöltése
     */
    void loadSSB();

  protected:
    /**
     * @brief Band beállítása
     */
    void useBand(bool useDefaults = false);

  public:
    /**
     * @brief Si4735Band osztály konstruktora
     */
    Si4735Band() : Si4735Runtime(), Band(), ssbLoaded(false) {}

    /**
     * @brief BandStore beállítása (örökölt a Band osztályból)
     */
    using Band::setBandStore;

    /**
     * @brief band inicializálása
     * @details A band inicializálása, beállítja az alapértelmezett értékeket és a sávszélességet.
     */
    void bandInit(bool sysStart = false);

    /**
     * @brief Band beállítása
     * @param useDefaults default adatok betültése?
     */
    void bandSet(bool useDefaults = false);

    /**
     * HF Sávszélesség beállítása
     */
    void setAfBandWidth();

    /**
     * @brief A hangolás a memória állomásra
     * @param bandIndex A band indexe (FM, MW, SW, LW)
     * @param frequency A hangolási frekvencia
     * @param demodModIndex A demodulációs mód indexe (FM, AM, LSB, USB, CW)
     * @param bandwidthIndex A sávszélesség indexe
     */
    void tuneMemoryStation(uint8_t bandIndex, uint16_t frequency, uint8_t demodModIndex, uint8_t bandwidthIndex);

    /**
     * @brief A frekvencia léptetése a rotary encoder értéke alapján
     * @param rotaryValue A rotary encoder értéke (növelés/csökkentés)
     */
    uint16_t stepFrequency(int16_t rotaryValue);
};
