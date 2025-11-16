/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: Si4735Base.h                                                                                                  *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                       *
 * -----                                                                                                               *
 * Last Modified: 2025.11.16, Sunday  09:52:09                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include <SI4735.h>
#include <stdint.h>

#include "Band.h"
#include "Config.h"
#include "defines.h"
#include "rtVars.h"

namespace Si4735Constants {
// Hangerő beállítások
static constexpr int SI4735_MIN_VOLUME = 0;
static constexpr int SI4735_MAX_VOLUME = 63;

// Antenna kapacitás beállítások
static constexpr int SI4735_MAX_ANT_CAP_FM = 191;
static constexpr int SI4735_MAX_ANT_CAP_AM = 6143;

// AGC
static constexpr int SI4735_MIN_ATTENNUATOR = 1;     // Minimum attenuator érték
static constexpr int SI4735_MAX_ATTENNUATOR_FM = 26; // FM: 0-26 közötti tartomány az LNA (Low Noise Amplifier) számára
static constexpr int SI4735_MAX_ATTENNUATOR_AM = 37; // AM/SSB: 0-37+ATTN_BACKUP közötti tartomány

}; // namespace Si4735Constants

/**
 * @brief Si4735Base osztály
 */
class Si4735Base {

  protected:
    SI4735 si4735;

  public:
    Si4735Base() {}

    /**
     * @brief  SI4735 referencia lekérése
     * @return SI4735 referencia
     */
    SI4735 &getSi4735() { return si4735; }

    /**
     * @brief  I2C busz cím lekérése a SEN pin alapján
     * @return int16_t I2C eszköz cím
     */
    inline int16_t getDeviceI2CAddress() { return si4735.getDeviceI2CAddress(PIN_SI4735_RESET); }

    /**
     * @brief I2C eszköz cím beállítása a SEN pin alapján
     * @param senPin 0 -  SI4735 device: Ha a pin SEN (16 az SSOP verzión vagy pin 6 a QFN verzión) alacsonyra (GND - 0V) van állítva;
     *               1 -  Si4735 device: Ha a pin SEN (16 az SSOP verzión vagy pin 6 a QFN verzión) magasra (+3.3V) van állítva.
     *               Ha SI4732 eszközt használsz, akkor fordítsd meg a fenti logikát (1 - GND vagy 0 - +3.3V).
     */
    inline void setDeviceI2CAddress(uint8_t senPin) { si4735.setDeviceI2CAddress(senPin); }

    /**
     * @brief Inicializálja a Si4735 eszközt.
     * @param pin Ha 0 vagy nagyobb, beállítja a MCU digitális lábát, amely vezérli a külső áramkört.
     */
    inline void setAudioMuteMcuPin(uint8_t pin) { si4735.setAudioMuteMcuPin(pin); }
};
