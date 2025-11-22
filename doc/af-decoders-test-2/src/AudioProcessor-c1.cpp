#include <Arduino.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring> // For memcpy
#include <hardware/uart.h>
#include <memory>

#include "AudioProcessor-c1.h"
#include "defines.h"

// AudioProcessor működés debug engedélyezése de csak DEBUG módban
#if defined(__DEBUG) && defined(__ADPROC_DEBUG)
#define ADPROC_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define ADPROC_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

/**
 * @brief AudioProcessorC1 konstruktor.
 */
AudioProcessorC1::AudioProcessorC1() : useFFT(false), is_running(false), useBlockingDma(true) {}

/**
 * @brief AudioProcessorC1 destruktor.
 */
AudioProcessorC1::~AudioProcessorC1() { stop(); }

/**
 * @brief Visszaadja a mintavételezési sebességet Hz-ben.
 */
uint32_t AudioProcessorC1::getSamplingRate() { return adcDmaC1.getSamplingRate(); }

/**
 * @brief Inicializálja az AudioProcessorC1 osztályt a megadott konfigurációval.
 * @param config Az ADC és DMA konfigurációs beállításai.
 * @param useFFT Jelzi, hogy FFT-t használunk-e a feldolgozáshoz.
 * @param useBlockingDma Blokkoló (true, SSTV/WEFAX) vagy nem-blokkoló (false, CW/RTTY) DMA mód.
 * @return Sikeres inicializálás esetén true, egyébként false.
 */
bool AudioProcessorC1::initialize(const AdcDmaC1::CONFIG &config, bool useFFT, bool useBlockingDma) {
    this->useFFT = useFFT;
    this->adcConfig = config;
    this->useBlockingDma = useBlockingDma;
    return true;
}

/**
 * @brief Elindítja az audio feldolgozást.
 */
void AudioProcessorC1::start() {
    adcDmaC1.initialize(this->adcConfig);
    is_running = true;
}

/**
 * @brief Leállítja az audio feldolgozást.
 */
void AudioProcessorC1::stop() {
    if (!is_running) {
        return;
    }
    adcDmaC1.finalize();
    is_running = false;
}

/**
 * @brief Átméretezi a mintavételezési konfigurációt.
 * @param sampleCount Az új mintaszám blokkonként.
 * @param samplingRate Az új mintavételezési sebesség Hz-ben.
 * @param bandwidthHz A sávszélesség Hz-ben (opcionális).
 */
void AudioProcessorC1::reconfigureAudioSampling(uint16_t sampleCount, uint16_t samplingRate, uint32_t bandwidthHz) {
    stop();

    ADPROC_DEBUG("AudioProc::reconfigureAudioSampling() HÍVÁS - sampleCount=%d, samplingRate=%d, bandwidthHz=%d\n", sampleCount, samplingRate, bandwidthHz);

    const float oversampleFactor = 1.25f;
    uint32_t finalRate = samplingRate;
    if (bandwidthHz > 0) {
        uint32_t nyquist = bandwidthHz * 2u;
        uint32_t suggested = static_cast<uint32_t>(ceilf(nyquist * oversampleFactor));
        ADPROC_DEBUG("AudioProc::reconfigureAudioSampling() - nyquist=%d, suggested=%d, finalRate(előtte)=%d\n", nyquist, suggested, finalRate);
        if (finalRate == 0)
            finalRate = suggested;
        else if (finalRate < nyquist)
            finalRate = suggested;
        ADPROC_DEBUG("AudioProc::reconfigureAudioSampling() - finalRate(utána)=%d\n", finalRate);
    }

    // A mintavételezési frekvencia (finalRate) mindig egy érvényes, biztonságos tartományban legyen
    if (finalRate == 0) {
        finalRate = 44100u;
    }
    if (finalRate > 65535u) {
        finalRate = 65535u;
    }

    // Ha FFT-t használunk, előkészítjük a szükséges erőforrásokat
    if (useFFT) {
        ADPROC_DEBUG("core1: FFT init, sampleCount=%d\n", sampleCount);
        this->fftInput.resize(sampleCount * 2);
#ifdef __ADPROC_DEBUG
        arm_status status =
#endif
            arm_cfft_init_q15(&this->fft_inst, sampleCount);
        ADPROC_DEBUG("core1: FFT init status=%d, useFFT=%d\n", status, useFFT);

        // Hanning ablak előkészítése
        this->hanningWindow.resize(sampleCount);
        for (int i = 0; i < sampleCount; i++) {
            float w = 0.5f * (1.0f - cosf(2.0f * PI * i / (sampleCount - 1)));
            this->hanningWindow[i] = (q15_t)(w * 32767.0f);
        }

        // Egy bin szélessége Hz-ben
        float binWidth = (sampleCount > 0) ? ((float)finalRate / (float)sampleCount) : 0.0f;

#ifdef __ADPROC_DEBUG
        // Kiírjuk a FFT-hez kapcsolódó paramétereket
        uint32_t afBandwidth = bandwidthHz;                                                                                                                   // hangfrekvenciás sávszélesség
        uint16_t bins = (sampleCount / 2);                                                                                                                    // bin-ek száma
        ADPROC_DEBUG("AudioProc FFT paraméterek: HF sávszélesség=%u Hz, mintavételi frekvencia=%u Hz, mintaszám=%u, bin_db=%u, egy bin szélessége=%.2f Hz\n", //
                     afBandwidth, finalRate, (unsigned)sampleCount, (unsigned)bins, binWidth);
#endif

        // Eltároljuk a kiszámított bin szélességet az példányban, hogy a feldolgozás a SharedData-ba írhassa
        this->currentBinWidthHz = binWidth;
    }

    adcConfig.sampleCount = sampleCount;
    adcConfig.samplingRate = static_cast<uint16_t>(finalRate);

    ADPROC_DEBUG("AudioProc::reconfigureAudioSampling() - adcConfig frissítve: sampleCount=%d, samplingRate=%d\n", adcConfig.sampleCount, adcConfig.samplingRate);

    start();
    ADPROC_DEBUG("core1: AudioProc reconfig: elindítva, sampleCount=%d, samplingRate=%d, useFFT=%d, is_running=%d\n", adcConfig.sampleCount, adcConfig.samplingRate, useFFT, is_running);
}

/**
 * @brief Alkalmazza a Hanning ablakot a bemeneti adatokra.
 * @param data A bemeneti adatok pointere (komplex formátum: re, im, re, im,...).
 * @param size Az adatok mérete (minták száma).
 */
void AudioProcessorC1::applyHanningWindow(q15_t *data, int size) {
    if (hanningWindow.empty() || (int)hanningWindow.size() < size) {
        return; // Biztonsági ellenőrzés
    }
    arm_mult_q15(data,                 // bemenet
                 hanningWindow.data(), // előre kiszámított Hanning ablak
                 data,                 // kimenet (in-place)
                 size                  // mintaszám
    );
}

/**
 * @brief Ellenőrzi, hogy a bemeneti jel meghaladja-e a küszöbértéket.
 * @param sharedData A SharedData struktúra referencia, amit fel kell tölteni.
 * @return true, ha a jel meghaladja a küszöböt, különben false.
 */
bool AudioProcessorC1::checkSignalThreshold(SharedData &sharedData) {

    int32_t maxAbsRaw = 0;
    for (int i = 0; i < (int)sharedData.rawSampleCount; ++i) {
        int32_t v = (int32_t)sharedData.rawSampleData[i];
        if (v < 0) {
            v = -v;
        }
        if (v > maxAbsRaw) {
            maxAbsRaw = v;
        }
    }
    // Küszöb: ha a bemenet túl kicsi, akkor ne vegyen fel hamis spektrum-csúcsot.
    constexpr int32_t RAW_SIGNAL_THRESHOLD = 80; // nyers ADC egységben, hangolandó
    if (maxAbsRaw < RAW_SIGNAL_THRESHOLD) {
        // Ha túl kicsi a jel, rögtön jelezzük
        ADPROC_DEBUG("AudioProc: nincs audió jel (maxAbsRaw=%d) -- FFT kihagyva\n", (int)maxAbsRaw);
        return false;
    }

    // Jel meghaladja a küszöböt
    return true;
}

/**
 * @brief Feldolgozza a legfrissebb audio blokkot és feltölti a megadott SharedData struktúrát.
 * SSTV és WEFAX módban csak a nyers mintákat másoljuk, FFT nélkül.
 * Más módokban lefuttatja az FFT-t, kiszámolja a spektrumot és a domináns frekvenciát.
 */
bool AudioProcessorC1::processAndFillSharedData(SharedData &sharedData) {
    if (!is_running) {
        return false;
    }
#ifdef __ADPROC_DEBUG
    uint32_t methodStartTime = micros();
#endif

    // DMA buffer lekérése blokkoló vagy nem-blokkoló módon
    // - BLOKKOLÓ mód (true): SSTV/WEFAX - garantáltan teljes blokk
    // - NEM-BLOKKOLÓ mód (false): CW/RTTY - azonnal visszatér, nullptr ha nincs adat
    uint16_t *completePingPongBufferPtr = adcDmaC1.getCompletePingPongBufferPtr(useBlockingDma);

    if (completePingPongBufferPtr == nullptr) {
        // Nincs még kész adat (csak nem-blokkoló módban), később próbálkozunk újra
        ADPROC_DEBUG("AudioProc: DMA még dolgozik (nem-blokkoló mód)\n");
        return false;
    }

#ifdef __ADPROC_DEBUG
    uint32_t start = micros();
    // start itt a getCompletePingPongBufferPtr() hívás után van; a waitTime mutatja, mennyit vártunk a
    // friss ping-pong bufferre (blokkoló módban ez okozza a nagy Total értéket)
    uint32_t waitTime = (start >= methodStartTime) ? (start - methodStartTime) : 0;
#endif

    // 1. A nyers minták másolása a megosztott pufferbe, a DC komponens (fél táp) eltávolításával
    sharedData.rawSampleCount = std::min((uint16_t)adcConfig.sampleCount, (uint16_t)MAX_RAW_SAMPLES_SIZE); // Biztonsági ellenőrzés

    arm_offset_q15((q15_t *)completePingPongBufferPtr, // bemenet
                   -ADC_MIDPOINT,                      // offset (negatív, hogy kivonjon)
                   (q15_t *)sharedData.rawSampleData,  // kimenet
                   sharedData.rawSampleCount           // mintaszám
    );

    // Ha nem kell FFT (pl. SSTV), akkor nem megyünk tovább
    if (!useFFT) {
        sharedData.fftSpectrumSize = 0;   // nincs spektrum
        sharedData.dominantFrequency = 0; // nincs domináns frekvencia
        sharedData.dominantAmplitude = 0; // nincs a domináns frekvenciának amplitúdója
        sharedData.fftBinWidthHz = 0.0f;  // nincs bin szélesség
        return true;
    }

    // Biztonsági ellenőrzés: a puffer mérete megfelelő-e?
    if (fftInput.size() < adcConfig.sampleCount * 2) {
        return false;
    }

    // 2. Bemenet előkészítése a CFFT-hez a tagváltozó pufferbe
    // A CMSIS CFFT várt bemeneti formátuma: [Re0, Im0, Re1, Im1, ...].
    for (int i = 0; i < adcConfig.sampleCount; i++) {
        // Shifteljük 12 bites ADC értéket 15 bites q15 formátumra
        q15_t val = ((q15_t)sharedData.rawSampleData[i]) << (15 - ADC_BIT_DEPTH);
        this->fftInput[2 * i] = val;   // Re
        this->fftInput[2 * i + 1] = 0; // Im
    }

    // 3. Hanning ablak (opcionális - CW/RTTY-hez hasznos, WEFAX-nál nem befolyásolja a domináns frekvenciát jelentősen)
    applyHanningWindow(this->fftInput.data(), adcConfig.sampleCount);

#ifdef __ADPROC_DEBUG
    uint32_t preprocTime = micros() - start;
#endif

    // FFT futtatása a nyers mintákon, a SpectrumVisualizationComponent számára
    // 4. FFT futtatása
#ifdef __ADPROC_DEBUG
    start = micros();
#endif
    arm_cfft_q15(              //
        &this->fft_inst,       // FFT instance
        this->fftInput.data(), // adat
        0,                     // FFT (nem IFFT)
        1                      // bit-reverse rendezettség
    );
#ifdef __ADPROC_DEBUG
    uint32_t fftTime = micros() - start;
#endif

    // 5. Spektrum számítása és másolása a megosztott pufferbe
#ifdef __ADPROC_DEBUG
    start = micros();
#endif
    uint16_t spectrumSize = adcConfig.sampleCount / 2;
    sharedData.fftSpectrumSize = std::min(spectrumSize, (uint16_t)SPECTRUM_SIZE);
    arm_abs_q15(                        //
        (q15_t *)this->fftInput.data(), // bemenet (komplex számok)
        (q15_t *)this->fftInput.data(), // kimenet (abszolút értékek)
        adcConfig.sampleCount * 2       // mintaszám (2x a komplex miatt)
    );

    // most minden re és im abszolút értéke megvan, össze kell őket adni (Re + Im)
    for (int i = 0; i < sharedData.fftSpectrumSize; i++) {
        sharedData.fftSpectrumData[i] = this->fftInput[2 * i] + this->fftInput[2 * i + 1];
    }
#ifdef __ADPROC_DEBUG
    uint32_t copyTime = micros() - start;
#endif

    // Az aktuális FFT bin szélesség beállítása a sharedData-ban
    sharedData.fftBinWidthHz = this->currentBinWidthHz;

    // 6. Domináns frekvencia keresése
#ifdef __ADPROC_DEBUG
    start = micros();
#endif
    uint32_t maxIndex = 0;
    q15_t maxValue = 0;
    arm_max_q15(                    //
        sharedData.fftSpectrumData, // bemenet
        sharedData.fftSpectrumSize, // méret
        &maxValue,                  // kimenet: max érték
        &maxIndex                   //
    );
    sharedData.dominantAmplitude = maxValue;
    sharedData.dominantFrequency = (adcConfig.samplingRate / adcConfig.sampleCount) * maxIndex;

#ifdef __ADPROC_DEBUG
    uint32_t dominantTime = micros() - start;
    // Számoljuk újra explicit módon a total-t a részek összegével, hogy ellenőrizhető legyen
    uint32_t totalSum = waitTime + preprocTime + fftTime + copyTime + dominantTime;
    uint32_t totalTime = (micros() >= methodStartTime) ? (micros() - methodStartTime) : 0;

    // DEBUG kimenet: időmérések olvasható formában
    ADPROC_DEBUG("AudioProc: Total(micros()-methodStart)=%lu usec, Total(sumParts)=%lu usec, Wait=%lu usec, PreProc=%lu usec, FFT=%lu usec, Copy=%lu usec, DomSearch=%lu usec, maxIndex=%d, amp=%d\n", //
                 totalTime, totalSum, waitTime, preprocTime, fftTime, copyTime, dominantTime, maxIndex, (int)maxValue);

#endif

    return true;
}