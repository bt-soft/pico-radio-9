/**
 * @file StationStore.cpp
 * @brief Állomás tároló osztályok és alapértelmezett állomáslisták implementációja
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
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
     10550,          // frequency (105.50 MHz)
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
//  -meg még néhány dolog, de majd...
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