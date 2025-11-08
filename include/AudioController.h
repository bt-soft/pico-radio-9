#pragma once

#include <stdint.h>

#include "arm_math.h"
#include "decoder_api.h"
#include "defines.h"

//-------------------------------------------------------------------------------------
// Extern deklarációk a Core-1-en osztott memóriaterületekhez
//-------------------------------------------------------------------------------------
extern SharedData sharedData[2];
extern DecodedData decodedData;
//-------------------------------------------------------------------------------------

/**
 * @brief AudioController osztály a Core1 dekóder vezérléséhez.
 *
 */
class AudioController {
  public:
    AudioController() = default;

    // A mintavételezési frekvencia a sávszélességből számolódik, ezért samplingRate paraméter elhagyva.
    void start(DecoderId id, uint32_t sampleCount, uint32_t bandwidthHz, uint32_t cwCenterFreqHz = 0, uint32_t rttyMarkFreqHz = 0, uint32_t rttySpaceFreqHz = 0, float rttyBaud = 0.0f);
    void stop();
    uint32_t getSamplingRate();

    /**
     * @brief Lekérdezi a Core1 által használt aktív adatpuffer indexét.
     * @return Az aktív puffer indexe (0 vagy 1), vagy -1 hiba esetén.
     */
    int8_t getActiveSharedDataIndex();

  private:
    DecoderId activeDecoderCore0 = ID_DECODER_NONE;
    // DecoderId oldActiveDecoderCore0 = ID_DECODER_NONE;
};

extern AudioController audioController; // main.cpp-ban definiált global instance