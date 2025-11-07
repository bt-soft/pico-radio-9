/**
 * @file StationStore.cpp
 * @brief Állomás tároló osztályok és alapértelmezett állomáslisták implementációja
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */

#include "StationStore.h"

// Alapértelmezett (üres) listák definíciója
const FmStationList_t DEFAULT_FM_STATIONS = {};
const AmStationList_t DEFAULT_AM_STATIONS = {};

// Globális példányok definíciója
FmStationStore fmStationStore;
AmStationStore amStationStore;