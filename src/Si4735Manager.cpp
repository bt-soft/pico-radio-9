/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: Si4735Manager.cpp                                                                                             *
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
 * Last Modified: 2025.11.16, Sunday  09:44:02                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "Si4735Manager.h"

/**
 * @brief Konstruktor, amely inicializálja a Si4735 eszközt.
 * @param config A konfigurációs objektum, amely tartalmazza a beállításokat.
 * @param band A Band objektum, amely kezeli a rádió sávokat.
 */
Si4735Manager::Si4735Manager() : Si4735Rds() {
    setAudioMuteMcuPin(PIN_AUDIO_MUTE); // Audio Mute pin
    // Audio unmute
    si4735.setAudioMute(false);
}

/**
 * @brief Inicializáljuk az osztályt, beállítjuk a rádió sávot és hangerőt.
 */
void Si4735Manager::init(bool systemStart) {

    DEBUG("Si4735Manager::init(%s) -> Start\n", systemStart ? "true" : "false");

    // A Band  visszaállítása a konfigból
    bandInit(systemStart);

    // A sávra preferált demodulációs mód betöltése
    bandSet(systemStart);

    // Hangerő beállítása
    si4735.setVolume(config.data.currVolume);

    // Rögtön be is állítjuk az AGC-t
    checkAGC();
}

/**
 * Loop függvény a squelchez és a hardver némításhoz.
 * Ez a függvény folyamatosan figyeli a squelch állapotát és kezeli a hardver némítást.
 */
void Si4735Manager::loop() {

    // Squelch kezelése
    manageSquelch();

    // Hardver némítás kezelése
    manageHardwareAudioMuteOnSSB();

    // Signal quality cache frissítése, ha szükséges
    updateSignalCacheIfNeeded();
}