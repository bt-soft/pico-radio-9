/**
 * @file Config.cpp
 * @brief Konfigurációs adatok implementációja
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
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
    .currVolume = 50,

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
    .audioFftGainConfigAm = 0.0f, // Auto Gain
    .audioFftGainConfigFm = 1.0f, // Manual Gain 1.0x (visszaállítva Arduino FFT-hez)

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
