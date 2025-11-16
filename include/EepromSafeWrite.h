/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: EepromSafeWrite.h                                                                                             *
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
 * Last Modified: 2025.11.16, Sunday  09:48:17                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

// Core1 audio mintavételezés indítása, leállítása és állapot lekérdezése
void startAudioSamplingC1();
void stopAudioSamplingC1();
bool isAudioSamplingRunningC1();

/**
 * @brief EEPROM biztonságos írás Core1 audio szüneteltetésével
 *
 * Ez a wrapper biztosítja, hogy EEPROM írás közben a Core1 audio
 * feldolgozás szünetelve legyen.
 */
class EepromSafeWrite {
  private:
    static inline bool wasAudioActive = false;

  public:
    /**
     * @brief EEPROM biztonságos írás indítása
     * Ha fut az audio feldolgozás, leállítja azt.
     */
    static void begin() {
        wasAudioActive = isAudioSamplingRunningC1();
        if (wasAudioActive) {
            stopAudioSamplingC1();
        }
    }

    /**
     * @brief EEPROM biztonságos írás befejezése
     * Ha futott az audio feldolgozás, újraindítja azt.
     */
    static void end() {
        if (wasAudioActive) {
            startAudioSamplingC1();
        }
    }

    /**
     * @brief RAII-stílusú EEPROM védelem automatikus destruktorral
     */
    class Guard {
      public:
        Guard() { begin(); }
        ~Guard() { end(); }
    };
};
