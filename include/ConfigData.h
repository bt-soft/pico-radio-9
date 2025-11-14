#pragma once

#include <cstdint>

#include "defines.h"

// Audio vizualizáció típusok enum
enum class AudioComponentType : uint8_t {
    OFF = 0,               // Kikapcsolva
    SPECTRUM_LOW_RES = 1,  // Alacsony felbontású spektrum (sáv alapú)
    SPECTRUM_HIGH_RES = 2, // Magas felbontású spektrum
    OSCILLOSCOPE = 3,      // Oszcilloszkóp (időtartomány)
    ENVELOPE = 4,          // Burkológörbe
    WATERFALL = 5,         // Waterfall diagram
    CW_WATERFALL = 6,      // CW specifikus waterfall
    RTTY_WATERFALL = 7     // RTTY specifikus waterfall
};

// Konfig struktúra típusdefiníció
struct Config_t {
    uint8_t currentBandIdx; // Aktuális sáv indexe

    // Hangfrekvenciás sávszélesség indexek
    uint8_t bwIdxAM;
    uint8_t bwIdxFM;
    uint8_t bwIdxSSB;

    // Step
    uint8_t ssIdxMW;
    uint8_t ssIdxAM;
    uint8_t ssIdxFM;

    // Squelch
    uint8_t currentSquelch;
    bool squelchUsesRSSI; // A squlech RSSI alapú legyen?

    // FM RDS
    bool rdsEnabled;

    // Hangerő
    uint8_t currVolume;

    // AGC
    uint8_t agcGain;
    uint8_t currentAGCgain; // AGC manual értéke

    //--- TFT
    uint16_t tftCalibrateData[5];    // TFT touch kalibrációs adatok
    uint8_t tftBackgroundBrightness; // TFT Háttérvilágítás
    bool tftDigitLight;              // Inaktív szegmens látszódjon?

    //--- System
    uint8_t screenSaverTimeoutMinutes; // Képernyővédő ideje percekben (1-30)
    bool beeperEnabled;                // Hangjelzés engedélyezése
    bool rotaryAccelerationEnabled;    // Rotary gyorsítás engedélyezése

    float audioFftGainConfigAm; // FFT Gain AM módban (dB): -999.0f = Auto Gain, -40.0 ... +40.0 dB tartomány (0dB = 1x, -40dB = 0.01x, +40dB = 100x)
    float audioFftGainConfigFm; // FFT Gain FM módban (dB): -999.0f = Auto Gain, -40.0 ... +40.0 dB tartomány (0dB = 1x, -40dB = 0.01x, +40dB = 100x)

    // CW frekvencia
    uint16_t cwToneFrequencyHz; // CW frekvencia Hz-ben

    // RTTY frekvenciák
    uint16_t rttyMarkFrequencyHz;  // RTTY Mark frekvencia Hz-ben
    uint16_t rttyShiftFrequencyHz; // RTTY Shift frekvencia Hz-ben
    float rttyBaudRate;            // RTTY Baud rate

    // Audio processing beállítások
    uint8_t audioModeAM; // Utolsó audio mód AM képernyőn (AudioComponentType)
    uint8_t audioModeFM; // Utolsó audio mód FM képernyőn (AudioComponentType)
};
