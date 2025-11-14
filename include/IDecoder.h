#pragma once

#include <cstddef>
#include <cstdint>

#include "decoder_api.h"
#include "defines.h"

/**
 * @brief Dekóder interfész osztály
 */
class IDecoder {
  public:
    virtual ~IDecoder() = default;

    /**
     * @brief Indítás / inicializáció
     * @param decoderConfig A dekóder konfigurációs struktúrája
     * @return true ha sikerült elindítani
     */
    virtual bool start(const DecoderConfig &decoderConfig) = 0;

    /**
     * @brief Leállítás és takarítás
     */
    virtual void stop() = 0;

    virtual const char *getDecoderName() const = 0;

    /**
     * @brief Minták feldolgozása
     * @param samples pointer a mintákhoz
     * @param count  minták száma
     */
    virtual void processSamples(const int16_t *samples, size_t count) { //
        DEBUG("IDecoder::processSamples - Alapértelmezett üres implementáció\n");
    };

    /**
     * @brief FFT spektrum adatok feldolgozása
     * @param fftSpectrumData pointer az FFT spektrum adatokhoz
     * @param size  az adatok mérete
     */
    virtual void processFFT(const int16_t *fftSpectrumData, size_t size) { //
        DEBUG("IDecoder::processFFT - Alapértelmezett üres implementáció\n");
    };

    /**
     * @brief Domináns frekvencia és amplitúdó feldolgozása
     * @param dominantFrequency Domináns frekvencia Hz-ben
     * @param dominantAmplitude Amplitúdó a domináns frekvencián (FLOAT - Arduino FFT)
     */
    virtual void processDomFreq(uint32_t dominantFrequency, float dominantAmplitude) { //
        DEBUG("IDecoder::processDomFreq - Alapértelmezett üres implementáció\n");
    };
};