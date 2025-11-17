/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: WindowApplier.cpp                                                                                             *
 * Created Date: 2025.11.17.                                                                                           *
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
 * Last Modified: 2025.11.17, Monday  07:09:47                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "WindowApplier.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @brief WindowApplier konstruktor
 * @param n Ablak mérete
 * @param type Ablak típusa
 * @param normalize Normalizálás engedélyezése
 */
WindowApplier::WindowApplier(size_t n, WindowType type, bool normalize) {
    if (n > 0)
        build(n, type, normalize);
}

/**
 * @brief Ablak újraméretezése / újraépítése
 * @param n Ablak mérete
 * @param type Ablak típusa
 * @param normalize Normalizálás engedélyezése
 */
void WindowApplier::build(size_t n, WindowType type, bool normalize) {
    coeffs_.assign(n, 1.0f);
    if (n == 0)
        return;

    if (type == WindowType::Hann) {
        // Hann ablak: w[n] = 0.5 * (1 - cos(2*pi*i/(N-1)))
        for (size_t i = 0; i < n; ++i) {
            coeffs_[i] = 0.5f * (1.0f - cosf((2.0f * M_PI * (float)i) / (float)(n - 1)));
        }
    } else { // Hamming
        // Hamming ablak: w[n] = 0.54 - 0.46 * cos(2*pi*i/(N-1))
        for (size_t i = 0; i < n; ++i) {
            coeffs_[i] = 0.54f - 0.46f * cosf((2.0f * M_PI * (float)i) / (float)(n - 1));
        }
    }

    if (normalize) {
        // Normalizálás: az együtthatók összege legyen ~1.0, így nagyjából megőrizzük a DC erősítést
        float sum = 0.0f;
        for (float v : coeffs_)
            sum += v;
        if (sum > 0.0f) {
            float inv = 1.0f / sum;
            for (float &v : coeffs_)
                v *= inv;
        }
    }
}

/**
 * @brief Ablak alkalmazása: int16 -> float (az out puffernek legalább n elemet kell tartalmaznia)
 * @param in Bemeneti int16 puffer
 * @param out Kimeneti float puffer
 * @param n Minták száma
 */
void WindowApplier::apply(const int16_t *in, float *out, size_t n) const {
    if (n == 0)
        return;
    size_t wn = std::min(n, coeffs_.size());
    for (size_t i = 0; i < wn; ++i) {
        out[i] = (float)in[i] * coeffs_[i];
    }
    // Ha a hívó teljes n elemet vár, de az együtthatók rövidebbek, másoljuk át a maradékot
    for (size_t i = wn; i < n; ++i) {
        out[i] = (float)in[i];
    }
}

/**
 * @brief Ablak alkalmazása helyben egy float pufferre
 * @param buf Float puffer
 * @param n Minták száma
 */
void WindowApplier::applyInPlace(float *buf, size_t n) const {
    if (n == 0)
        return;
    size_t wn = std::min(n, coeffs_.size());
    for (size_t i = 0; i < wn; ++i) {
        buf[i] *= coeffs_[i];
    }
}
