/**
 * @file DebugDataInspector.cpp
 * @brief DebugDataInspector osztály implementációja
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
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
    DEBUG("  tftCalibrateData: [%u, %u, %u, %u, %u]\n", configData.tftCalibrateData[0], configData.tftCalibrateData[1], configData.tftCalibrateData[2], configData.tftCalibrateData[3], configData.tftCalibrateData[4]);
    DEBUG("  tftBackgroundBrightness: %u\n", configData.tftBackgroundBrightness);
    DEBUG("  tftDigitLight: %s\n", configData.tftDigitLight ? "true" : "false");
    DEBUG("  screenSaverTimeoutMinutes: %u\n", configData.screenSaverTimeoutMinutes);
    DEBUG("  beeperEnabled: %s\n", configData.beeperEnabled ? "true" : "false");
    DEBUG("  rotaryAccelerationEnabled: %s\n", configData.rotaryAccelerationEnabled ? "true" : "false");
    if (configData.audioFftGainConfigAm == -1.0f) {
        DEBUG("  audioFftGainConfigAm: Disabled\n");
    } else if (configData.audioFftGainConfigAm == 0.0f) {
        DEBUG("  audioFftGainConfigAm: Auto Gain\n");
    } else {
        DEBUG("  audioFftGainConfigAm: Manual Gain %sx\n", Utils::floatToString(configData.audioFftGainConfigAm).c_str());
    }
    if (configData.audioFftGainConfigFm == -1.0f) {
        DEBUG("  audioFftGainConfigFm: Disabled\n");
    } else if (configData.audioFftGainConfigFm == 0.0f) {
        DEBUG("  audioFftGainConfigFm: Auto Gain\n");
    } else {
        DEBUG("  audioFftGainConfigFm: Manual Gain %sx\n", Utils::floatToString(configData.audioFftGainConfigFm).c_str());
    }
    if (configData.miniAudioFftConfigAnalyzer == -1.0f) {
        DEBUG("  miniAudioFftConfigAnalyzer: Disabled\n");
    } else if (configData.miniAudioFftConfigAnalyzer == 0.0f) {
        DEBUG("  miniAudioFftConfigAnalyzer: Auto Gain\n");
    } else {
        DEBUG("  miniAudioFftConfigAnalyzer: Manual Gain %sx\n", Utils::floatToString(configData.miniAudioFftConfigAnalyzer).c_str());
    }
    if (configData.miniAudioFftConfigRtty == -1.0f) {
        DEBUG("  miniAudioFftConfigRtty: Disabled\n");
    } else if (configData.miniAudioFftConfigRtty == 0.0f) {
        DEBUG("  miniAudioFftConfigRtty: Auto Gain\n");
    } else {
        DEBUG("  miniAudioFftConfigRtty: Manual Gain %sx\n", Utils::floatToString(configData.miniAudioFftConfigRtty).c_str());
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
