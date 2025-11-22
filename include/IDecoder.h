/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: IDecoder.h                                                                                                    *
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
 * Last Modified: 2025.11.22, Saturday  09:55:55                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

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

    /**
     * @brief Dekóder adaptív küszöb használatának beállítása/lekérdezése
     */
    virtual void setUseAdaptiveThreshold(bool enabled) { //
        DEBUG("IDecoder::setUseAdaptiveThreshold - Alapértelmezett üres implementáció\n");
    }

    /**
     * @brief Dekóder adaptív küszöb lekérdezése
     */
    virtual bool getUseAdaptiveThreshold() const {
        DEBUG("IDecoder::getUseAdaptiveThreshold - Alapértelmezett üres implementáció\n");
        return false;
    }

    /**
     * @brief Dekóder resetelése
     */
    virtual void reset() { DEBUG("IDecoder::reset - Alapértelmezett üres implementáció\n"); }

    /**
     * @brief Sávszűrő engedélyezése / tiltása
     * @param enabled true: engedélyezve, false: letiltva
     */
    virtual void enableBandpass(bool enabled) { DEBUG("IDecoder::enableBandpass - Alapértelmezett üres implementáció\n"); }
};