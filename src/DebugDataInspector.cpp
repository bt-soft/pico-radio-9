/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: DebugDataInspector.cpp                                                                                        *
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
 * Last Modified: 2025.12.22, Monday  10:20:22                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "DebugDataInspector.h"
#include "BandStore.h"
#include "Config.h"
#include "Utils.h"

/**
 * @brief Kiírja az FM állomáslista tartalmát a soros portra.
 * @param fmStore Az FM állomáslista objektum.
 */
void DebugDataInspector::printFmStationData(const FmStationList_t &fmData) {
#ifdef __DEBUG
    DEBUG("=== DebugDataInspector -> FM Station Store ===\n");
    for (size_t i = 0; i < fmData.count; ++i) {
        const StationData &station = fmData.stations[i];
        DEBUG("  Station %d: Freq: %d, Name: %s, Mod: %d, BW: %d\n", i, station.frequency, station.name, station.modulation, station.bandwidthIndex);
    }
    DEBUG("====================\n");
#endif
}

/**
 * @brief Kiírja az AM állomáslista tartalmát a soros portra.
 * @param amStore Az AM állomáslista objektum.
 */
void DebugDataInspector::printAmStationData(const AmStationList_t &amData) {
#ifdef __DEBUG
    Serial.println("=== DebugDataInspector -> AM Station Store ===");
    for (size_t i = 0; i < amData.count; ++i) {
        const StationData &station = amData.stations[i];
        DEBUG("  Station %d: Freq: %d, Name: %s, Mod: %d, BW: %d\n", i, station.frequency, station.name, station.modulation, station.bandwidthIndex);
    }
    DEBUG("====================\n");
#endif
}

/**
 * @brief Kiírja a Config struktúra tartalmát a soros portra.
 * @param config A Config objektum.
 */
void DebugDataInspector::printConfigData(const Config_t &configData) {
#ifdef __DEBUG
    DEBUG("=== DebugDataInspector -> Config Data ===\n");
    DEBUG("  currentBandIdx: %u\n", configData.currentBandIdx);
    DEBUG("  bwIdxAM: %u\n", configData.bwIdxAM);
    DEBUG("  bwIdxFM: %u\n", configData.bwIdxFM);
    DEBUG("  bwIdxSSB: %u\n", configData.bwIdxSSB);
    DEBUG("  ssIdxMW: %u\n", configData.ssIdxMW);
    DEBUG("  ssIdxAM: %u\n", configData.ssIdxAM);
    DEBUG("  ssIdxFM: %u\n", configData.ssIdxFM);
    DEBUG("  currentSquelch: %u\n", configData.currentSquelch);
    DEBUG("  squelchUsesRSSI: %s\n", configData.squelchUsesRSSI ? "true" : "false");
    DEBUG("  rdsEnabled: %s\n", configData.rdsEnabled ? "true" : "false");
    DEBUG("  currVolume: %u\n", configData.currVolume);
    DEBUG("  agcGain: %u\n", configData.agcGain);
    DEBUG("  currentAGCgain: %u\n", configData.currentAGCgain);
    DEBUG("  tftCalibrateData: [%u, %u, %u, %u, %u]\n", configData.tftCalibrateData[0], configData.tftCalibrateData[1], configData.tftCalibrateData[2],
          configData.tftCalibrateData[3], configData.tftCalibrateData[4]);
    DEBUG("  tftBackgroundBrightness: %u\n", configData.tftBackgroundBrightness);
    DEBUG("  tftDigitLight: %s\n", configData.tftDigitLight ? "true" : "false");
    DEBUG("  screenSaverTimeoutMinutes: %u\n", configData.screenSaverTimeoutMinutes);
    DEBUG("  beeperEnabled: %s\n", configData.beeperEnabled ? "true" : "false");
    DEBUG("  rotaryAccelerationEnabled: %s\n", configData.rotaryAccelerationEnabled ? "true" : "false");

    DEBUG("  audioModeAM: %u\n", configData.audioModeAM);
    if (configData.audioFftGainConfigAm == SPECTRUM_GAIN_MODE_AUTO) {
        DEBUG("  audioFftGainConfigAm: Auto Gain\n");
    } else {
        float amDb = static_cast<float>(configData.audioFftGainConfigAm);
        DEBUG("  audioFftGainConfigAm: %.1f dB (linear: %.3fx)\n", amDb, powf(10.0f, amDb / 20.0f));
    }

    DEBUG("  audioModeFM: %u\n", configData.audioModeFM);
    if (configData.audioFftGainConfigFm == SPECTRUM_GAIN_MODE_AUTO) {
        DEBUG("  audioFftGainConfigFm: Auto Gain\n");
    } else {
        float fmDb = static_cast<float>(configData.audioFftGainConfigFm);
        DEBUG("  audioFftGainConfigFm: %.1f dB (linear: %.3fx)\n", fmDb, powf(10.0f, fmDb / 20.0f));
    }
    DEBUG("  cwToneFrequencyHz: %u\n", configData.cwToneFrequencyHz);
    DEBUG("  rttyMarkFrequencyHz: %u\n", configData.rttyMarkFrequencyHz);
    DEBUG("  rttyShiftFrequencyHz: %u\n", configData.rttyShiftFrequencyHz);
    DEBUG("  rttyBaudRate: %f\n", configData.rttyBaudRate);

    DEBUG("====================\n");
#endif
}

/**
 * @brief Kiírja a Band store adatok tartalmát a soros portra.
 * @param bandData A Band store adatok.
 */
void DebugDataInspector::printBandStoreData(const BandStoreData_t &bandData) {
#ifdef __DEBUG
    DEBUG("=== DebugDataInspector -> Band Store Data ===\n");
    for (size_t i = 0; i < BANDTABLE_SIZE; ++i) {
        const BandTableData_t &band = bandData.bands[i];
        // Csak akkor írjuk ki, ha van érvényes adat (currFreq != 0)
        if (band.currFreq != 0) {
            DEBUG("  BandNdx %d: Freq: %u, Step: %u, Mod: %u, AntCap: %u\n", i, band.currFreq, band.currStep, band.currMod, band.antCap);
        }
    }
    DEBUG("====================\n");
#endif
}
