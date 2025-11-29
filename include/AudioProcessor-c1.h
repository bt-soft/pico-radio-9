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
 * Last Modified: 2025.11.29, Saturday  12:58:38                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once
// Arduino környezetben implementált fixpontos függvények
#include <arm_math.h>
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

    inline bool isUseFFT() const { return useFFT; } // visszaadja, hogy FFT-t használunk-e
    inline void setUseFFT(bool enabled) { useFFT = enabled; }
    inline uint16_t getSampleCount() { return adcDmaC1.getSampleCount(); }                                // visszaadja a mintaszámot blokkonként
    inline uint32_t getSamplingRate() { return adcDmaC1.getSamplingRate(); }                              // visszaadja a mintavételezési sebességet Hz-ben
    void reconfigureAudioSampling(uint16_t sampleCount, uint16_t samplingRate, uint32_t bandwidthHz = 0); // átméretezi a mintavételezési konfigurációt

    // Spektrális átlagolás beállítása: hány keretet átlagoljunk (nem koherens)
    void setSpectrumAveragingCount(uint8_t n);
    uint8_t getSpectrumAveragingCount() const;

    // ADC DC középpont kalibrálása: az ADC közvetlen mintavételezése (Core1-en kell hívni)
    // Visszatér a mért középponttal (egész ADC egységekben).
    uint32_t calibrateDcMidpoint(uint32_t sampleCount = 128);

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
        if (points == 0) {
            smoothingPoints_ = 0;
        } else if (points >= 5) {
            smoothingPoints_ = 5;
        } else {
            smoothingPoints_ = 3;
        }
    }

    // AGC állapot lekérdezése
    inline bool isAgcEnabled() const { return useAgc_; }
    inline float getCurrentAgcGain() const { return currentAgcGain_; }
    inline float getManualGain() const { return manualGain_; }
    inline bool isNoiseReductionEnabled() const { return useNoiseReduction_; }
    inline uint8_t getSmoothingPoints() const { return smoothingPoints_; }

    // Fixpontos FFT beállítások
    inline void setFixedPointFFTEnabled(bool enabled) { useFixedPointFFT_ = enabled; }
    inline bool isFixedPointFFTEnabled() const { return useFixedPointFFT_; }

  private:
    AdcDmaC1 adcDmaC1;
    AdcDmaC1::CONFIG adcConfig;
    bool is_running;
    bool useFFT;
    bool useBlockingDma; // Blokkoló (true) vagy nem-blokkoló (false) DMA mód

    uint16_t currentFftSize; // Aktuális FFT méret

    // Fixpontos FFT támogatás (CMSIS-DSP)
    bool useFixedPointFFT_ = true;        // Fixpontos FFT használata (alapértelmezett: igen)
    arm_cfft_instance_q15 fft_inst_q15;   // CMSIS-DSP FFT instance
    std::vector<q15_t> fftInput_q15;      // Fixpontos FFT bemenet (komplex: re,im,re,im...)
    std::vector<q15_t> magnitude_q15;     // Fixpontos magnitude eredmény
    std::vector<q15_t> hanningWindow_q15; // Fixpontos Hanning ablak

    // Spektrális átlagoló puffer: körkörös puffer a korábbi magnitúdó-keretekhez (nem koherens átlagolás)
    uint8_t spectrumAveragingCount_ = 1; // alapértelmezés: 1 -> nincs átlagolás
    std::vector<float> avgBuffer;        // méret = spectrumSize * spectrumAveragingCount_
    uint8_t avgWriteIndex_ = 0;

    // Az utolsó feldolgozott nyers minták (DC eltávolítva, AGC alkalmazva) a segéd detektorokhoz (Goertzel)
    std::vector<int16_t> lastRawSamples;
    uint16_t lastRawSampleCount = 0;

    // Az aktuális FFT bin szélesség, amelyet a konfiguráció során számoltunk ki (Hz)
    float currentBinWidthHz = 0.0f;

    // Az aktuális audio bandwidth (Hz), amelyet a reconfigureAudioSampling állít be
    // AM ~6kHz, FM ~15kHz - ezt használjuk a dinamikus bin-kizáráshoz
    uint32_t currentBandwidthHz = 0;

    // Runtime measured ADC midpoint (in ADC units, e.g., ~2048 for 12-bit). Measured on Core1.
    uint32_t adcMidpoint_ = (1u << (ADC_BIT_DEPTH - 1));

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

    void removeDcAndSmooth(const uint16_t *input, int16_t *output, uint16_t count);
    void applyAgc(int16_t *samples, uint16_t count);
    void applyFftGaussianWindow(float *data, uint16_t size, float fftBinWidthHz, float boostMinHz, float boostMaxHz, float boostGain);

    // CMSIS-DSP Q15 fixpontos FFT metódusok
    // A FFT és a domináns frekvencia keresés idejét mikroszekundumban adja vissza kimeneti referenciákon keresztül
    bool processFixedPointFFT(SharedData &sharedData, uint32_t &fftTime_us, uint32_t &domTime_us);
    void initFixedPointFFT(uint16_t sampleCount);
    void buildHanningWindow_q15(uint16_t size);

    // Konverziós segédfüggvények
    inline q15_t floatToQ15(float val) { return (q15_t)(val * Q15_MAX_AS_FLOAT); }
    inline float q15ToFloat(q15_t val) { return (float)val / Q15_MAX_AS_FLOAT; }

    // Goertzel detektor API: kiszámolja a magnitúdót egy célfrekvenciára az utolsó feldolgozott minták alapján
    // Visszatérési érték: magnitúdó (lineáris), vagy -1 hibára
    float computeGoertzelMagnitude(float targetFreqHz);

    bool isBinInAudioRange(uint16_t binIndex, float binWidthHz, uint16_t spectrumSize) const;
};
