#pragma once
#include <arm_math.h>
#include <vector>

#include "AdcDma-c1.h"
#include "decoder_api.h"

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
};
