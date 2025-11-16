/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: StationStore.cpp                                                                                              *
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
 * Last Modified: 2025.11.16, Sunday  09:44:36                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "StationStore.h"

// Alapértelmezett listák definíciója
const FmStationList_t DEFAULT_FM_STATIONS = {{
    {FM_BAND_TYPE,  // bandIndex
     9390,          // frequency (93.90 MHz)
     FM_DEMOD_TYPE, // modulation
     0,             // Auto bandwidth index
     //----
     "MR2 Petofi"},  // name
    {FM_BAND_TYPE,   // bandIndex
     9460,           // frequency (94.60 MHz)
     FM_DEMOD_TYPE,  // modulation
     0,              // Auto bandwidth index
     "Katolikus"},   // name
                     //----
    {FM_BAND_TYPE,   // bandIndex
     9830,           // frequency (98.30 MHz)
     FM_DEMOD_TYPE,  // modulation
     0,              // Auto bandwidth index
     "HirFM"},       // name
                     //----
    {FM_BAND_TYPE,   // bandIndex
     10500,          // frequency (105.00 MHz)
     FM_DEMOD_TYPE,  // modulation
     0,              // Auto bandwidth index
     "Bartok"},      // name
                     //----
    {FM_BAND_TYPE,   // bandIndex
     10720,          // frequency (107.20 MHz)
     FM_DEMOD_TYPE,  // modulation
     0,              // Auto bandwidth index
     "MR1 Kossuth"}, // name
}};

// TODO: Még valami kínja van az AM memória értékeknek
//  - tárolni kellene, hogy HAM-e, vagy sima AM
//  - meg még néhány dolog, de majd valamikor...
const AmStationList_t DEFAULT_AM_STATIONS = {{
    {MW_BAND_TYPE,      // bandIndex
     540,               // frequency (540 kHz)
     AM_DEMOD_TYPE,     // modulation
     0,                 // 6kHz bandwidth index
     "Kossuth"},        // name
                        //----
    {SW_BAND_TYPE,      // bandIndex
     10100,             // frequency (10.100 MHz)
     LSB_DEMOD_TYPE,    // modulation
     2,                 // 3.0kHz bandwidth index
     "Pinneberg DDK9"}, // name
}};

// Globális példányok definíciója
FmStationStore fmStationStore;
AmStationStore amStationStore;