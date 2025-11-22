#pragma once

#include "arm_math.h"
#include "decoder_api.h"
#include "defines.h"
#include <stdint.h>

class AudioController {
  public:
    AudioController() = default;

    // A mintavételezési frekvencia a sávszélességből számolódik, ezért samplingRate paraméter elhagyva.
    void setDecoder(DecoderId id, uint32_t sampleCount, uint32_t bandwidthHz, uint32_t cwCenterFreqHz = 0, uint32_t rttyMarkFreqHz = 0, uint32_t rttySpaceFreqHz = 0, float rttyBaud = 0.0f);
    void stop();
    uint32_t getSamplingRate();

    /**
     * @brief Lekérdezi a Core1 által használt aktív adatpuffer indexét.
     * @return Az aktív puffer indexe (0 vagy 1), vagy -1 hiba esetén.
     */
    int8_t getActiveSharedDataIndex();
};
