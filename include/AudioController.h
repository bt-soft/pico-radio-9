/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: AudioController.h                                                                                             *
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
 * Last Modified: 2025.11.22, Saturday  09:32:50                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include <stdint.h>

#include "decoder_api.h"
#include "defines.h"

//-------------------------------------------------------------------------------------
// Extern deklarációk a Core-1-en osztott memóriaterületekhez
//-------------------------------------------------------------------------------------
extern SharedData sharedData[2];
extern DecodedData decodedData;
extern volatile uint8_t activeSharedDataIndex;
//-------------------------------------------------------------------------------------

/**
 * @brief AudioController osztály a Core1 dekóder vezérléséhez.
 *
 */
class AudioController {
  public:
    AudioController() = default;

    // A mintavételezési frekvencia a sávszélességből számolódik, ezért samplingRate paraméter elhagyva.
    void startAudioController(DecoderId id, uint32_t sampleCount, uint32_t bandwidthHz, uint32_t cwCenterFreqHz = 0, uint32_t rttyMarkFreqHz = 0,
                              uint32_t rttySpaceFreqHz = 0, float rttyBaud = 0.0f);
    void stopAudioController();
    uint32_t getSamplingRate();

    // Vezérlő metódusok
    bool setAgcEnabled(bool enabled);
    bool setNoiseReductionEnabled(bool enabled);
    bool setSmoothingPoints(uint32_t points);
    void setManualGain(float gain);
    bool setBlockingDmaMode(bool blocking);

    // CW adaptive threshold (AGC-like) control through UI
    bool setDecoderUseAdaptiveThreshold(bool use);
    bool getDecoderUseAdaptiveThreshold();

    // Kérjük a Core1-et, hogy resetelje az aktív dekódert
    void resetDecoder();

    // FFT vezérlés
    bool setUseFftEnabled(bool enabled);
    bool getUseFftEnabled();
    // Inicializációs lánc: kérjük meg a Core1-et az ADC DC középpont kalibrálására
    void init();
    // Spektrális átlagolás beállítása (1 = nincs átlagolás)
    bool setSpectrumAveragingCount(uint32_t n);
    // Engedélyezi/tiltja a dekóder oldali bandpass szűrőt (ha a dekóder implementálja)
    bool setDecoderBandpassEnabled(bool enabled);

  private:
    DecoderId activeDecoderCore0 = ID_DECODER_NONE;
    // DecoderId oldActiveDecoderCore0 = ID_DECODER_NONE;
};

extern AudioController audioController; // main.cpp-ban definiált global instance