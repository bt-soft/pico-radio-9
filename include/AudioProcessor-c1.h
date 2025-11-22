/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: AudioProcessor-c1.h                                                                                           *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                       *
 * -----                                                                                                               *
 * Last Modified: 2025.11.22, Saturday  02:49:11                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once
#include <ArduinoFFT.h>
#include <vector>

#include "AdcDma-c1.h"
#include "decoder_api.h"

/**
 * @brief Audio feldolgozó osztály a Core1-en.
 */
class AudioProcessorC1 {
  public:
    AudioProcessorC1();
    ~AudioProcessorC1();

    bool initialize(const AdcDmaC1::CONFIG &config, bool useFFT = false, bool useBlockingDma = true);
    void start();
    void stop();

    inline uint16_t getSampleCount() { return adcDmaC1.getSampleCount(); }                                // visszaadja a mintaszámot blokkonként
    inline uint32_t getSamplingRate() { return adcDmaC1.getSamplingRate(); }                              // visszaadja a mintavételezési sebességet Hz-ben
    void reconfigureAudioSampling(uint16_t sampleCount, uint16_t samplingRate, uint32_t bandwidthHz = 0); // átméretezi a mintavételezési konfigurációt

    /**
     * @brief Beállítja a DMA blokkoló/nem-blokkoló módját.
     * @param blocking true = blokkoló (SSTV/WEFAX), false = nem-blokkoló (CW/RTTY)
     */
    inline void setBlockingDmaMode(bool blocking) { useBlockingDma = blocking; }

    /**
     * @brief Feldolgozza a legfrissebb audio blokkot és feltölti a megadott SharedData struktúrát.
     * Lefuttatja az FFT-t, kiszámolja a spektrumot, a domináns frekvenciát,
     * és átmásolja a nyers mintákat.
     * @param sharedData A SharedData struktúra referencia, amit fel kell tölteni.
     */
    bool processAndFillSharedData(SharedData &sharedData);

    /**
     * @brief Visszaadja, hogy az audio feldolgozás fut-e.
     */
    inline bool isRunning() { return is_running; }

    // AGC és zajszűrés beállítási metódusok
    inline void setAgcEnabled(bool enabled) { useAgc_ = enabled; }
    inline void setManualGain(float gain) { manualGain_ = constrain(gain, agcMinGain_, agcMaxGain_); }
    inline void setNoiseReductionEnabled(bool enabled) { useNoiseReduction_ = enabled; }
    inline void setSmoothingPoints(uint8_t points) {
        // Csak 0 (nincs simítás), 3 vagy 5 engedélyezett
        if (points == 0)
            smoothingPoints_ = 0;
        else if (points >= 5)
            smoothingPoints_ = 5;
        else
            smoothingPoints_ = 3;
    }

    // AGC állapot lekérdezése
    inline bool isAgcEnabled() const { return useAgc_; }
    inline float getCurrentAgcGain() const { return currentAgcGain_; }
    inline float getManualGain() const { return manualGain_; }
    inline bool isNoiseReductionEnabled() const { return useNoiseReduction_; }
    inline uint8_t getSmoothingPoints() const { return smoothingPoints_; }

  public:
    bool useFFT;

  private:
    void removeDcAndSmooth(const uint16_t *input, int16_t *output, uint16_t count);
    void applyAgc(int16_t *samples, uint16_t count);
    bool checkSignalThreshold(int16_t *samples, uint16_t count);
    void applyFftGaussianWindow(float *data, uint16_t size, float fftBinWidthHz, float boostMinHz, float boostMaxHz, float boostGain);

    AdcDmaC1 adcDmaC1;
    AdcDmaC1::CONFIG adcConfig;
    bool is_running;
    bool useBlockingDma; // Blokkoló (true) vagy nem-blokkoló (false) DMA mód

    // FFT-hez kapcsolódó tagváltozók (Arduino FFT - FLOAT)
    ArduinoFFT<float> FFT;           // Arduino FFT objektum
    std::vector<float> vReal;        // FFT valós komponens (input/output)
    std::vector<float> vImag;        // FFT imaginárius komponens
    std::vector<float> fftMagnitude; // FFT magnitúdó (output)
    uint16_t currentFftSize;         // Aktuális FFT méret

    // Az aktuális FFT bin szélesség, amelyet a konfiguráció során számoltunk ki (Hz)
    float currentBinWidthHz = 0.0f;

    // // Alacsony frekvenciás szűrés konstansai (a spektrum megjelenítés minőségének javítása)
    // static constexpr float LOW_FREQ_ATTENUATION_THRESHOLD_HZ = 300.0f; // Ez alatti frekvenciákat csillapítunk
    // static constexpr float LOW_FREQ_ATTENUATION_FACTOR = 50.0f;          // Ezzel a faktorral osztjuk az alacsony frekvenciák magnitúdóját

    // AGC (Automatikus Erősítésszabályozás) paraméterek
    float agcLevel_;       // AGC mozgó átlag szint
    float agcAlpha_;       // AGC szűrési állandó (exponenciális mozgó átlag)
    float agcTargetPeak_;  // Cél amplitúdó
    float agcMinGain_;     // Minimum erősítés
    float agcMaxGain_;     // Maximum erősítés
    float currentAgcGain_; // Aktuális erősítési faktor
    bool useAgc_;          // AGC be/ki kapcsoló (true = auto AGC, false = manuális gain)
    float manualGain_;     // Manuális erősítés (ha useAgc_ = false)

    // Zajszűrés paraméterek
    bool useNoiseReduction_;  // Zajszűrés be/ki kapcsoló
    uint8_t smoothingPoints_; // Mozgó átlag simítás pontok száma (0=nincs, 3 vagy 5)

    void gainFttMagnitudeValues(SharedData &sharedData);
};
