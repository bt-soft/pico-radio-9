/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: WindowApplier.h                                                                                               *
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
 * Last Modified: 2025.11.17, Monday  07:06:17                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

enum class WindowType { Hann, Hamming };

class WindowApplier {
  public:
    // Ablak létrehozása n méretben. Ha normalize==true, az ablak együtthatóit úgy
    // méretezzük át, hogy azok összege 1 legyen. Ez az ablakolás után megőrzi
    // a DC (átlag) erősítést.
    WindowApplier(size_t n = 0, WindowType type = WindowType::Hann, bool normalize = true);

    // Resize / rebuild the window
    void build(size_t n, WindowType type = WindowType::Hann, bool normalize = true);

    // Access coefficients
    const std::vector<float> &coeffs() const { return coeffs_; }
    size_t size() const { return coeffs_.size(); }

    // Ablak alkalmazása: int16 -> float (az out puffernek legalább n elemet kell tartalmaznia)
    void apply(const int16_t *in, float *out, size_t n) const;

    // Ablak alkalmazása helyben egy float pufferre
    void applyInPlace(float *buf, size_t n) const;

  private:
    std::vector<float> coeffs_;
};
