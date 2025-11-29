/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: AdcConstants.h                                                                                                *
 * Created Date: 2025.11.29.                                                                                           *
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
 * Last Modified: 2025.11.29, Saturday  06:07:22                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */
#pragma once

// Feszültségmérés
#define VBUS_DIVIDER_R1 10.0f // Ellenállás VBUS és A0 között (kOhm)
#define VBUS_DIVIDER_R2 15.0f // Ellenállás A0 és GND között (kOhm)

// ADC konstansok (Core1 szenzor olvasáshoz, DC középpont kalibrációhoz)
// Az RP2040 ADC hardware 12-bit, és az audio DMA is 12-bittel dolgozik!
#define CORE1_ADC_RESOLUTION 12
#define CORE1_ADC_V_REFERENCE 3.3f
#define CORE1_ADC_CONVERSION_FACTOR (1 << CORE1_ADC_RESOLUTION) // 4096
#define CORE1_VBUSDIVIDER_RATIO ((VBUS_DIVIDER_R1 + VBUS_DIVIDER_R2) / VBUS_DIVIDER_R2)

#define ADC_MIDPOINT_MEASURE_SAMPLE_COUNT 256 // Alapértelmezett mintaszám az ADC DC középpont kalibrációhoz