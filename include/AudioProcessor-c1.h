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
 * Last Modified: 2025.12.20, Saturday  06:29:48                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 * 2025.12.20   BT  Újratervezés: AGC és Goertzel eltávolítva, tiszta fixpontos CMSIS-DSP Q15 FFT pipeline             *
 */

#pragma once

#include <arm_math.h>
#include <vector>

#include "AdcDma-c1.h"
#include "adc-constants.h"
#include "decoder_api.h"

/**
 * @brief Audio feldolgozó osztály a Core1-en.
 *
 * Újratervezett, egyszerűsített verzió:
 * - Tiszta fixpontos (Q15) feldolgozási lánc a CMSIS-DSP könyvtárral
 * - AGC eltávolítva (nem szükséges)
 * - Goertzel eltávolítva (a dekóderek saját implementációt használnak)
 * - Zajszűrés és spektrum átlagolás kikapcsolva (de a kód megmaradt későbbi használatra)
 *
 * Feldolgozási lánc:
 * 1. ADC minták beolvasása (12-bit, uint16_t)
 * 2. DC offset eltávolítása (int16_t/q15_t)
 * 3. Hanning ablak alkalmazása (Q15 szorzás)
 * 4. CMSIS-DSP Q15 FFT
 * 5. Magnitude számítás (Q15)
 * 6. Domináns frekvencia keresése
 */
class AudioProcessorC1 {
  public:
    AudioProcessorC1();
    ~AudioProcessorC1();

    /**
     * @brief Inicializálja az audio feldolgozót.
     * @param config ADC és DMA konfiguráció
     * @param useFFT FFT használata (true = spektrum számítás, false = csak nyers minták)
     * @param useBlockingDma Blokkoló DMA mód (true = SSTV/WEFAX, false = CW/RTTY)
     * @return Sikeres inicializálás esetén true
     */
    bool initialize(const AdcDmaC1::CONFIG &config, bool useFFT = false, bool useBlockingDma = true);

    void start(); ///< Audio feldolgozás indítása
    void stop();  ///< Audio feldolgozás leállítása

    // --- Állapot lekérdezések ---
    inline bool isRunning() const { return is_running; }
    inline bool isUseFFT() const { return useFFT; }
    inline void setUseFFT(bool enabled) { useFFT = enabled; }
    inline uint16_t getSampleCount() { return adcDmaC1.getSampleCount(); }
    inline uint32_t getSamplingRate() { return adcDmaC1.getSamplingRate(); }

    /**
     * @brief Átméretezi a mintavételezési konfigurációt.
     * @param sampleCount Mintaszám blokkonként
     * @param samplingRate Mintavételezési sebesség (Hz)
     * @param bandwidthHz Audio sávszélesség (Hz) - opcionális, a bin-kizáráshoz
     */
    void reconfigureAudioSampling(uint16_t sampleCount, uint16_t samplingRate, uint32_t bandwidthHz = 0);

    /**
     * @brief ADC DC középpont kalibrálása.
     * Futásidőben méri az ADC középpontot a pontos DC offset eltávolításhoz.
     * @param sampleCount Kalibráció mintaszáma
     */
    void calibrateDcMidpoint(uint32_t sampleCount = ADC_MIDPOINT_MEASURE_SAMPLE_COUNT);

    /**
     * @brief DMA blokkoló/nem-blokkoló mód beállítása.
     * @param blocking true = blokkoló (SSTV/WEFAX), false = nem-blokkoló (CW/RTTY)
     */
    inline void setBlockingDmaMode(bool blocking) { useBlockingDma = blocking; }

    /**
     * @brief Feldolgozza a legfrissebb audio blokkot és feltölti a SharedData struktúrát.
     *
     * A feldolgozási lánc:
     * 1. DMA puffer lekérése
     * 2. DC offset eltávolítása (ADC midpoint levonása)
     * 3. Ha useFFT=true: Q15 FFT + magnitude + domináns frekvencia
     *
     * @param sharedData Kimeneti struktúra a feldolgozott adatokkal
     * @return true ha sikeres, false ha nincs adat vagy hiba történt
     */
    bool processAndFillSharedData(SharedData &sharedData);

    // --- Spektrum átlagolás (jelenleg kikapcsolva) ---
    void setSpectrumAveragingCount(uint8_t n);
    uint8_t getSpectrumAveragingCount() const;

    // --- Zajszűrés (jelenleg kikapcsolva) ---
    inline void setNoiseReductionEnabled(bool enabled) { useNoiseReduction_ = enabled; }
    inline bool isNoiseReductionEnabled() const { return useNoiseReduction_; }
    inline void setSmoothingPoints(uint8_t points) {
        // Csak 0 (nincs), 3 vagy 5 engedélyezett
        if (points == 0) {
            smoothingPoints_ = 0;
        } else if (points >= 5) {
            smoothingPoints_ = 5;
        } else {
            smoothingPoints_ = 3;
        }
    }
    inline uint8_t getSmoothingPoints() const { return smoothingPoints_; }

  private:
    // --- Alapvető állapot ---
    AdcDmaC1 adcDmaC1;          ///< ADC DMA kezelő
    AdcDmaC1::CONFIG adcConfig; ///< ADC konfiguráció
    bool is_running;            ///< Feldolgozás fut-e
    bool useFFT;                ///< FFT használata
    bool useBlockingDma;        ///< Blokkoló DMA mód

    // --- FFT állapot ---
    uint16_t currentFftSize;     ///< Aktuális FFT méret
    float currentBinWidthHz;     ///< Egy FFT bin szélessége Hz-ben (megjelenítéshez)
    uint32_t currentBandwidthHz; ///< Audio sávszélesség Hz-ben (bin-kizáráshoz)

    // --- CMSIS-DSP Q15 FFT ---
    arm_cfft_instance_q15 fft_inst_q15;   ///< CMSIS-DSP FFT példány
    std::vector<q15_t> fftInput_q15;      ///< FFT bemenet (komplex: re,im,re,im...)
    std::vector<q15_t> magnitude_q15;     ///< FFT magnitude kimenet
    std::vector<q15_t> hanningWindow_q15; ///< Hanning ablak Q15 formátumban

    // --- DC offset ---
    uint32_t adcMidpoint_; ///< Mért ADC középpont (12-bit esetén ~2048)

    // --- Zajszűrés (jelenleg kikapcsolva) ---
    bool useNoiseReduction_;  ///< Zajszűrés engedélyezve
    uint8_t smoothingPoints_; ///< Mozgó átlag simítás (0, 3 vagy 5)

    // --- Spektrum átlagolás (jelenleg kikapcsolva) ---
    uint8_t spectrumAveragingCount_; ///< Átlagolandó keretek száma (1 = nincs)

    // --- Privát metódusok ---

    /**
     * @brief DC offset eltávolítása a nyers ADC mintákból.
     * uint16_t -> int16_t konverzió az ADC midpoint levonásával.
     * @param input Bemeneti ADC minták (12-bit, 0-4095)
     * @param output Kimeneti DC-mentes minták (-2048..+2047)
     * @param count Minták száma
     */
    void removeDcOffset(const uint16_t *input, int16_t *output, uint16_t count);

    /**
     * @brief Q15 FFT inicializálása.
     * CMSIS-DSP FFT példány és pufferek előkészítése.
     * @param sampleCount FFT méret (2 hatványa)
     */
    void initFixedPointFFT(uint16_t sampleCount);

    /**
     * @brief Hanning ablak létrehozása Q15 formátumban.
     * @param size Ablak mérete
     */
    void buildHanningWindow_q15(uint16_t size);

    /**
     * @brief Q15 FFT feldolgozás végrehajtása.
     * @param sharedData Kimeneti struktúra
     * @return true ha sikeres
     */
    bool processFixedPointFFT(SharedData &sharedData);

    // --- Segédfüggvények ---
    inline q15_t floatToQ15(float val) const { return (q15_t)(val * Q15_MAX_AS_FLOAT); }
    inline float q15ToFloat(q15_t val) const { return (float)val / Q15_MAX_AS_FLOAT; }
};
