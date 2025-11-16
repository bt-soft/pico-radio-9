/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: pins.h                                                                                                        *
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
 * Last Modified: 2025.11.16, Sunday  09:48:49                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once

// TFT (A TFT_eSPI_User_Setup.h-ban a pinout)

// I2C si4735
#define PIN_SI4735_I2C_SDA 8
#define PIN_SI4735_I2C_SCL 9
#define PIN_SI4735_RESET 10

// Audio FFT bemenet
#define PIN_AUDIO_INPUT A0 // A0/GPIO26 az FFT audio bemenethez

// Feszültségmérés
#define PIN_VBUS_EXTERNAL_MEASURE_INPUT A1 // A1/GPIO27 a VBUS mérésének feszültségosztós bemenetéhez

// Rotary Encoder
#define PIN_ENCODER_DT 17
#define PIN_ENCODER_CLK 16
#define PIN_ENCODER_SW 18

// Others
#define PIN_TFT_BACKGROUND_LED 1 // A NYÁK miatt áthelyezve a  GPIO1-es lábra
#define PIN_AUDIO_MUTE 19
#define PIN_BEEPER 20
