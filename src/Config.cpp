/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: Config.cpp                                                                                                    *
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
 * Last Modified: 2025.11.16, Sunday  09:40:37                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "Config.h"

/**
 * Alapértelmezett readonly konfigurációs adatok
 */
const Config_t DEFAULT_CONFIG = {
    //-- Band
    .currentBandIdx = 0, // Default band, FM

    // BandWidht
    .bwIdxAM = 0,  // BandWidth AM - Band::bandWidthAM index szerint -> "6.0" kHz
    .bwIdxFM = 0,  // BandWidth FM - Band::bandWidthFM[] index szerint -> "AUTO"
    .bwIdxSSB = 0, // BandWidth SSB - Band::bandWidthSSB[] index szerint ->
                   // "1.2" kHz  (the valid values are 0, 1, 2, 3, 4 or 5)

    // Step
    .ssIdxMW = 2, // Band::stepSizeAM[] index szerint -> 9kHz
    .ssIdxAM = 1, // Band::stepSizeAM[] index szerint -> 5kHz
    .ssIdxFM = 1, // Band::stepSizeFM[] index szerint -> 100kHz

    // Squelch
    .currentSquelch = 0,      // Squelch szint (0...50)
    .squelchUsesRSSI = false, // A squlech RSSI alapú legyen?

    // FM RDS
    .rdsEnabled = true,

    // Hangerő
    .currVolume = 50, // hangerő

    // AGC
    .agcGain = 1,        // static_cast<uint8_t>(Si4735Runtime::AgcGainMode::Automatic),        // -> 1,
    .currentAGCgain = 1, // static_cast<uint8_t>(Si4735Runtime::AgcGainMode::Automatic), // -> 1

    //--- TFT
    .tftCalibrateData = {0, 0, 0, 0, 0},                          // TFT touch kalibrációs adatok
                                                                  //.tftCalibrateData = {214, 3721, 239, 3606, 7},
    .tftBackgroundBrightness = TFT_BACKGROUND_LED_MAX_BRIGHTNESS, // TFT Háttérvilágítás
    .tftDigitLight = true,                                        // Inaktív szegmens látszódjon?

    //--- System
    .screenSaverTimeoutMinutes = SCREEN_SAVER_TIMEOUT, // Képernyővédő alapértelmezetten 5 perc
    .beeperEnabled = true,                             // Hangjelzés engedélyezése
    .rotaryAccelerationEnabled = true,                 // Rotary gyorsítás engedélyezése

    // AudioFft módok
    .audioFftGainConfigAm = -18.0,                             // Manuális a sok zaj elnyomására
    .audioFftGainConfigFm = SPECTRUM_GAIN_MODE_MANUAL_DEFAULT, // Manuális, 0dB

    // CW beállítások
    .cwToneFrequencyHz = 850, // x Hz CW frequency

    // RTTY beállítások
    .rttyMarkFrequencyHz = 1000, // Hogy az 1.2kHz-es HF sávszélességbe is beleférjen  //2125,   // RTTY Mark frequency
    .rttyShiftFrequencyHz = 450, // RTTY Shift frequency
    .rttyBaudRate = 50.0f,       // RTTY Baud rate

    // Audio processing alapértelmezett beállítások
    .audioModeAM = 1, // AudioComponentType::SPECTRUM_LOW_RES
    .audioModeFM = 1, // AudioComponentType::SPECTRUM_LOW_RES
                      // .audioFftGain = 1.0f, // Alapértelmezett FFT erősítés
};

// Globális konfiguráció példány
Config config;

// Globális BandStore példány
#include "BandStore.h"
BandStore bandStore;
