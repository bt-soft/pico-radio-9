/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: BiquadFilter.h                                                                                                *
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
 * Last Modified: 2025.11.22, Saturday  09:18:26                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once

#include <cstddef>
#include <cstdint>

/*
 * Egyszerű Biquad band-pass filter (RBJ cookbook) core-1
 */
class BiquadBandpass {
  public:
    BiquadBandpass();
    // Initialize with sample rate (Hz), center frequency (Hz) and bandwidth (Hz)
    void init(float sampleRate, float centerFreqHz, float bandwidthHz);
    // Process in-place int16_t buffer (uses float internally)
    void processInPlace(const int16_t *in, int16_t *out, size_t count);
    // Reset state
    void reset();
    // Check initialized
    bool isInitialized() const { return initialized_; }

  private:
    // filter coefficients
    float b0_, b1_, b2_, a1_, a2_;
    // state
    float z1_, z2_;
    float fs_;
    bool initialized_;
};
