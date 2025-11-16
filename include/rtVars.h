/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: rtVars.h                                                                                                      *
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
 * Last Modified: 2025.11.16, Sunday  09:50:02                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once

#include <cstdint>

/**
 * Futássi idejű változók a rádió működéséhez
 */
namespace rtv {

// Némítás
extern bool mute;

// Frekvencia kijelzés pozíciója
extern uint16_t freqDispX;
extern uint16_t freqDispY;

extern uint8_t freqstepnr; // A frekvencia kijelző digit száma, itt jelezzük
                           // SSB/CW-ben, hogy mi a frekvencia lépés
extern uint16_t freqstep;
extern int16_t freqDec;

// BFO állapotok
extern bool bfoOn; // BFO mód aktív?
extern bool bfoTr; // BFO kijelző animáció trigger

// BFO értékek
extern int16_t currentBFO;
extern int16_t lastBFO;        // Utolsó BFO érték
extern int16_t currentBFOmanu; // Manuális BFO eltolás (pl. -999 ... +999 Hz)
extern int16_t lastmanuBFO;    // Utolsó manuális BFO érték X-Tal segítségével
extern uint8_t currentBFOStep; // BFO lépésköz (pl. 1, 10, 25 Hz)

// Mute
#define AUDIO_MUTE_ON true
#define AUDIO_MUTE_OFF false
extern bool muteStat;

// Squelch
#define SQUELCH_DECAY_TIME 500
#define MIN_SQUELCH 0
#define MAX_SQUELCH 50
extern long squelchDecay;

// Scan
extern bool SCANbut;
extern bool SCANpause;

// Seek
extern bool SEEK;

// CW shift
extern bool CWShift;

} // namespace rtv
