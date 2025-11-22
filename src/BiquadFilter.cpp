/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: BiquadFilter.cpp                                                                                              *
 * Created Date: 2025.11.22.                                                                                           *
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
 * Last Modified: 2025.11.22, Saturday  09:53:13                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include <algorithm>
#include <cmath>

#include "BiquadFilter.h"

/*
 * Egyszerű Biquad band-pass filter (RBJ cookbook) core-1
 */

/**
 * @brief BiquadBandpass konstruktor
 */
BiquadBandpass::BiquadBandpass() : b0_(0), b1_(0), b2_(0), a1_(0), a2_(0), z1_(0), z2_(0), fs_(0), initialized_(false) {}

/**
 * @brief Filter állapotának visszaállítása
 */
void BiquadBandpass::reset() {
    z1_ = 0.0f;
    z2_ = 0.0f;
}

/**
 * @brief Biquad sávszűrő inicializálása
 * @param sampleRate Mintavételezési frekvencia (Hz)
 * @param centerFreqHz Középfrekvencia (Hz)
 * @param bandwidthHz Sávszélesség (Hz)
 */
void BiquadBandpass::init(float sampleRate, float centerFreqHz, float bandwidthHz) {
    if (sampleRate <= 0.0f || centerFreqHz <= 0.0f || bandwidthHz <= 0.0f) {
        initialized_ = false;
        return;
    }

    fs_ = sampleRate;
    // Az RBJ sávszűrő (constant skirt gain, peak gain = Q)
    float omega = 2.0f * M_PI * centerFreqHz / fs_;
    float bw = bandwidthHz;

    // Q = center / BW
    float Q = centerFreqHz / bw;
    float alpha = sinf(omega) / (2.0f * Q);

    float cosw = cosf(omega);
    float a0 = 1.0f + alpha;

    b0_ = alpha / a0;
    b1_ = 0.0f;
    b2_ = -alpha / a0;
    a1_ = -2.0f * cosw / a0;
    a2_ = (1.0f - alpha) / a0;

    reset();
    initialized_ = true;
}

/**
 * @brief Biquad sávszűrő alkalmazása int16_t mintákra
 * @param in Bemeneti minták tömbje
 * @param out Kimeneti minták tömbje
 * @param count Feldolgozandó minták száma
 */
void BiquadBandpass::processInPlace(const int16_t *in, int16_t *out, size_t count) {
    if (!initialized_) {
        // passthrough
        for (size_t i = 0; i < count; ++i)
            out[i] = in[i];
        return;
    }

    for (size_t n = 0; n < count; ++n) {
        float x = (float)in[n];
        float y = b0_ * x + b1_ * z1_ + b2_ * z2_ - a1_ * z1_ - a2_ * z2_;

        // shift states
        z2_ = z1_;
        z1_ = x;

        // clamp to int16 range
        float yf = std::clamp(y, -32768.0f, 32767.0f);
        out[n] = (int16_t)yf;
    }
}
