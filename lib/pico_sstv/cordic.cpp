//  _  ___  _   _____ _     _
// / |/ _ \/ | |_   _| |__ (_)_ __   __ _ ___
// | | | | | |   | | | '_ \| | '_ \ / _` / __|
// | | |_| | |   | | | | | | | | | | (_| \__ \
// |_|\___/|_|   |_| |_| |_|_|_| |_|\__, |___/
//                                  |___/
//
// Copyright (c) Jonathan P Dawson 2023
// filename: cordic.cpp
// description: convert rectangular to polar using cordic
// License: MIT
//

#include <array>
#include <cmath>
#include <cstdint>

constexpr uint8_t CORDIC_ITERATIONS = 16;
constexpr int16_t HALF_PI = 16384;
std::array<int16_t, CORDIC_ITERATIONS + 1> thetas;
int16_t recip_gain;

/**
 * CORDIC inicializálása: szögek és reciprok erősítés kiszámítása
 */
void cordic_init() {
    double k = 1.0;

    // Calculate theta lookup table
    for (uint8_t idx = 0; idx <= CORDIC_ITERATIONS; idx++) {
        thetas[idx] = static_cast<int16_t>(round(atan(k) * 32768 / M_PI));
        k *= 0.5;
    }

    // Calculate cordic gain
    double gain = 1.0;
    for (uint8_t idx = 0; idx < CORDIC_ITERATIONS; idx++) {
        gain *= sqrt(1.0 + pow(2.0, -2 * idx));
    }

    recip_gain = static_cast<int16_t>(32767 / gain);
}

/**
 * CORDIC algoritmus: téglalap alakú koordináták poláris koordinátákká alakítása
 */
void cordic_rectangular_to_polar(int16_t i, int16_t q, uint16_t &magnitude, int16_t &phase) {
    int32_t temp_i;
    int32_t i_32 = i;
    int32_t q_32 = q;

    // Initial +/- 90 degree rotation
    if (i_32 < 0) {
        temp_i = i_32;
        if (q_32 > 0) {
            i_32 = q_32;
            q_32 = -temp_i;
            phase = -HALF_PI;
        } else {
            i_32 = -q_32;
            q_32 = temp_i;
            phase = HALF_PI;
        }
    } else {
        phase = 0;
    }

    // Perform CORDIC iterations
    for (uint8_t idx = 0; idx <= CORDIC_ITERATIONS; idx++) {
        temp_i = i_32;
        if (q_32 >= 0) {
            i_32 += q_32 >> idx;
            q_32 -= temp_i >> idx;
            phase -= thetas[idx];
        } else {
            i_32 -= q_32 >> idx;
            q_32 += temp_i >> idx;
            phase += thetas[idx];
        }
    }

    magnitude = static_cast<uint16_t>((i_32 * recip_gain) >> 14);
}
