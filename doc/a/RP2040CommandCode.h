/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: RP2040CommandCode.h                                                                                           *
 * Created Date: 2025.11.15.                                                                                           *
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
 * Last Modified: 2025.11.16, Sunday  09:49:34                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once
#include <cstdint>

// Command codes for RP2040 FIFO communication
enum class RP2040CommandCode : uint32_t {
    CMD_SET_CONFIG = 1,
    CMD_STOP = 2,
    CMD_GET_SAMPLING_RATE = 3,
    CMD_GET_DATA_BLOCK = 4,
    CMD_SET_AGC_ENABLED = 5,
    CMD_SET_NOISE_REDUCTION_ENABLED = 6,
    CMD_SET_SMOOTHING_POINTS = 7,
    CMD_SET_MANUAL_GAIN = 8,
    CMD_SET_BLOCKING_DMA_MODE = 9,
    CMD_SET_USE_FFT_ENABLED = 10,
    CMD_GET_USE_FFT_ENABLED = 11
};
