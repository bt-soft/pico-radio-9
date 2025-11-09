#pragma once
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
    uint16_t getSampleCount() { return adcDmaC1.getSampleCount(); }
    uint32_t getSamplingRate();
    void reconfigureAudioSampling(uint16_t sampleCount, uint16_t samplingRate, uint32_t bandwidthHz = 0);

    /**
     * @brief Beállítja a DMA blokkoló/nem-blokkoló módját.
     * @param blocking true = blokkoló (SSTV/WEFAX), false = nem-blokkoló (CW/RTTY)
     */
    void setBlockingDmaMode(bool blocking) { useBlockingDma = blocking; }

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
    bool isRunning() { return is_running; }

  private:
    void applyHanningWindow(q15_t *data, int size);
    bool checkSignalThreshold(SharedData &sharedData);
    void applyAgc(int16_t *samples, uint16_t count);
    void removeDcAndSmooth(const uint16_t *input, int16_t *output, uint16_t count);

    AdcDmaC1 adcDmaC1;
    AdcDmaC1::CONFIG adcConfig;
    bool useFFT;
    bool is_running;
    bool useBlockingDma; // Blokkoló (true) vagy nem-blokkoló (false) DMA mód

    // FFT-hez kapcsolódó tagváltozók
    arm_cfft_instance_q15 fft_inst;   // CMSIS CFFT instance
    std::vector<q15_t> fftInput;      // Bemeneti puffer a CFFT számára (heap-en)
    std::vector<q15_t> hanningWindow; // Hanning ablak táblázat

    // Az aktuális FFT bin szélesség, amelyet a konfiguráció során számoltunk ki (Hz)
    float currentBinWidthHz = 0.0f;

    // AGC (Automatikus Erősítésszabályozás) paraméterek
    float agcLevel_;       // AGC mozgó átlag szint
    float agcAlpha_;       // AGC szűrési állandó (exponenciális mozgó átlag)
    float agcTargetPeak_;  // Cél amplitúdó q15 formátumban
    float agcMinGain_;     // Minimum erősítés
    float agcMaxGain_;     // Maximum erősítés
    float currentAgcGain_; // Aktuális erősítési faktor
    bool useAgc_;          // AGC be/ki kapcsoló (true = auto AGC, false = manuális gain)
    float manualGain_;     // Manuális erősítés (ha useAgc_ = false)

    // Zajszűrés paraméterek
    bool useNoiseReduction_;  // Zajszűrés be/ki kapcsoló
    uint8_t smoothingPoints_; // Mozgó átlag simítás pontok száma (0=nincs, 3 vagy 5)

  public:
    // AGC és zajszűrés beállítási metódusok
    void setAgcEnabled(bool enabled) { useAgc_ = enabled; }
    void setManualGain(float gain) { manualGain_ = constrain(gain, agcMinGain_, agcMaxGain_); }
    void setNoiseReductionEnabled(bool enabled) { useNoiseReduction_ = enabled; }
    void setSmoothingPoints(uint8_t points) {
        // Csak 0 (nincs simítás), 3 vagy 5 engedélyezett
        if (points == 0)
            smoothingPoints_ = 0;
        else if (points >= 5)
            smoothingPoints_ = 5;
        else
            smoothingPoints_ = 3;
    }

    // AGC állapot lekérdezése
    bool isAgcEnabled() const { return useAgc_; }
    float getCurrentAgcGain() const { return currentAgcGain_; }
    float getManualGain() const { return manualGain_; }
    bool isNoiseReductionEnabled() const { return useNoiseReduction_; }
};
