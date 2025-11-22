#pragma once
#include <stdint.h>

extern bool beeperEnabled;
extern float audioFftConfigAm;

// Audio processing beállítások
extern uint8_t audioModeAM; // Utolsó audio mód AM képernyőn (AudioComponentType)

extern uint16_t cwToneFrequencyHz;   // CW hang frekvencia Hz-ben
                                     // RTTY frekvenciák
extern uint16_t rttyMarkFrequencyHz; // RTTY Mark frekvencia Hz-ben
extern uint16_t rttyShiftHz;         // RTTY Shift Hz-ben
