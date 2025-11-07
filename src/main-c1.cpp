/**
 * @file main-c1.cpp
 * @brief Pico Radio Core-1 fő programfájlja
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */
#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <memory>

#include "AudioProcessor-c1.h"
#include "CwDecoder-c1.h"
#include "RttyDecoder-c1.h"
#include "SstvDecoder-c1.h"
#include "WefaxDecoder-c1.h"
#include "defines.h"

// A Core-1-nek stack külön legyen a Core-0-tól
// https://arduino-pico.readthedocs.io/en/latest/multicore.html#stack-sizes
bool core1_separate_stack = true;

//-------------------------------------------------------------------------------------
//  Osztott memóriaterületek a Core-0 és Core-1 között
//-------------------------------------------------------------------------------------
// A megosztott adatpufferek Core-0 és Core-1 között
// A két elemű sharedData[2] tömb azért van, hogy a két mag (Core 0 és Core 1) között
// biztonságosan lehessen adatot cserélni, "ping-pong" vagy "double-buffering" technikával.
SharedData sharedData[2];
volatile uint8_t activeSharedDataIndex = 0; // Aktív SharedData index (0 vagy 1)

// A dekódolt adatok globális példánya, ezt éri el a Core-0 is
DecodedData decodedData;
//-------------------------------------------------------------------------------------

// Audio feldolgozó példányja
static AudioProcessorC1 audioProc;

// Előre deklarálás a segédfüggvényhez, amely frissíti és publikálja a kijelzőre vonatkozó frekvencia-javaslatokat
static void updateDisplayHints(const DecoderConfig &cfg);

// Core-1 aktív dekóder azonosítója
static DecoderId activeDecoderIdCore1 = ID_DECODER_NONE;
std::unique_ptr<IDecoder> activeDecoderCore1 = nullptr;

/**
 * @brief Frissíti a megjelenítési javaslatokat a megadott dekóder konfiguráció alapján.
 * @param cfg A dekóder konfiguráció
 */
static void updateDisplayHints(const DecoderConfig &cfg) {

    uint8_t backBufferIndex = 1 - activeSharedDataIndex;

    // Alapértelmezett értékek
    uint16_t dispMin = 300u; // Alsó határ az analizátor kijelzéséhez (Hz)
    uint16_t dispMax = 0u;

    if (cfg.decoderId == ID_DECODER_CW) {
        uint32_t center = cfg.cwCenterFreqHz;
        uint32_t hfBandwidth = (cfg.bandwidthHz > 0) ? cfg.bandwidthHz : CW_AF_BANDWIDTH_HZ;

        float cwSpan = std::max(600.0f, hfBandwidth / 2.0f);
        uint16_t half = static_cast<uint16_t>(std::round(cwSpan / 2.0f));

        dispMin = (center > half) ? static_cast<uint16_t>(center - half) : 0u;
        dispMax = static_cast<uint16_t>(center + half);

    } else if (cfg.decoderId == ID_DECODER_RTTY) {
        uint16_t f_mark = cfg.rttyMarkFreqHz;
        uint16_t f_space = (f_mark > cfg.rttyShiftFreqHz) ? static_cast<uint16_t>(f_mark - cfg.rttyShiftFreqHz) : 0u;

        uint32_t hfBandwidth = (cfg.bandwidthHz > 0) ? cfg.bandwidthHz : RTTY_AF_BANDWIDTH_HZ;
        float margin = std::max(300.0f, hfBandwidth * 0.15f);

        uint16_t minf = (std::min(f_mark, f_space) > static_cast<uint16_t>(margin)) ? static_cast<uint16_t>(std::min(f_mark, f_space) - static_cast<uint16_t>(margin)) : 0u;
        uint16_t maxf = static_cast<uint16_t>(std::max(f_mark, f_space) + static_cast<uint16_t>(margin));

        dispMin = minf;
        dispMax = maxf;

    } else {
        // Általános eset: az analizátort a default alsó határtól a konfigurált AF sávszélességig mutatjuk
        dispMax = cfg.bandwidthHz > 0 ? static_cast<uint16_t>(cfg.bandwidthHz) : DOMINANT_FREQ_AF_BANDWIDTH_HZ; // fallback
    }

    // Csak akkor írunk, ha az értékek megváltoztak, hogy elkerüljük a felesleges hátsó puffert frissítéseket
    if (sharedData[backBufferIndex].displayMinFreqHz != dispMin || sharedData[backBufferIndex].displayMaxFreqHz != dispMax) {
        sharedData[backBufferIndex].displayMinFreqHz = dispMin;
        sharedData[backBufferIndex].displayMaxFreqHz = dispMax;
        DEBUG("core-1: updateDisplayHints() -> min=%u Hz, max=%u Hz (back=%u)\n", dispMin, dispMax, backBufferIndex);
    }
}

/**
 * Az aktív dekóder leállítása és felszabadítása.
 */
void stopActiveDecoder() {
    if (activeDecoderCore1 != nullptr) {
        activeDecoderCore1->stop();
        DEBUG("core-1: Dekóder '%s' leállítva\n", activeDecoderCore1->getDecoderName());
        activeDecoderCore1.reset();
        activeDecoderIdCore1 = ID_DECODER_NONE;
    }
}

/**
 * Általános dekóder vezérlő függvény.
 * @param decoderConfig Az új dekóder konfiguráció
 */
void decoderController(DecoderConfig decoderConfig) {

    // Ha van dekóder, de új ID jönött, akkor leállítjuk a régit
    if (decoderConfig.decoderId != activeDecoderIdCore1 && activeDecoderCore1 != nullptr) {
        stopActiveDecoder();
    }

    // Ha nem kell dekóder, akkor kilépünk
    if (decoderConfig.decoderId == ID_DECODER_NONE) {
        activeDecoderIdCore1 = ID_DECODER_NONE;
        return;
    }

    // Pufferek törlése új dekóder indításakor (CW/RTTY esetén a szöveg buffer, SSTV/WEFAX esetén a kép buffer)
    decodedData.textBuffer.clear();
    decodedData.lineBuffer.clear();
    decodedData.cwCurrentWpm = 0;

    // Létrehozzuk az új dekódert
    switch (decoderConfig.decoderId) {

        case ID_DECODER_DOMINANT_FREQ:
            activeDecoderIdCore1 = ID_DECODER_DOMINANT_FREQ;
            DEBUG("core-1: Dominant Frequency dekóder elindítva\n");
            break;

        case ID_DECODER_CW:
            // CW mód: Goertzel alapú tónus detektálás + Morse dekódolás
            activeDecoderCore1 = std::make_unique<CwDecoderC1>();
            activeDecoderCore1->start(decoderConfig);
            activeDecoderIdCore1 = ID_DECODER_CW;
            DEBUG("core-1: CW dekóder elindítva (%u Hz, adaptív)\n", decoderConfig.cwCenterFreqHz);
            break;

        case ID_DECODER_RTTY:
            activeDecoderCore1 = std::make_unique<RttyDecoderC1>();
            activeDecoderCore1->start(decoderConfig);
            activeDecoderIdCore1 = ID_DECODER_RTTY;
            DEBUG("core-1: RTTY dekóder elindítva\n");
            break;

        case ID_DECODER_SSTV:
            // Létrehozzuk az SSTV dekódert
            activeDecoderCore1 = std::make_unique<SstvDecoderC1>();
            activeDecoderCore1->start(decoderConfig);
            activeDecoderIdCore1 = ID_DECODER_SSTV;
            break;

        case ID_DECODER_WEFAX:
            activeDecoderCore1 = std::make_unique<WefaxDecoderC1>();
            activeDecoderCore1->start(decoderConfig);
            activeDecoderIdCore1 = ID_DECODER_WEFAX;
            break;

        default:
            DEBUG("core-1: HIBA - Ismeretlen dekóder ID: %d\n", decoderConfig.decoderId);
            activeDecoderIdCore1 = ID_DECODER_NONE;
            return;
    }

    if (activeDecoderCore1 != nullptr) {
        DEBUG("core-1: Dekóder '%s' elindítva\n", activeDecoderCore1->getDecoderName());
    }
}

/**
 * @brief Parancsok feldolgozása a Core 0-tól érkező FIFO-n keresztül.
 */
void processFifoCommands() {

    // Ha nincs FIFO adat, akkor nem megyünk tovább
    if (!rp2040.fifo.available()) {
        return;
    }

    // Parancsok kezelése a Core 0-tól
    uint32_t command = rp2040.fifo.pop();
    switch (command) {

        case RP2040CommandCode::CMD_SET_CONFIG: {
            DecoderConfig decoderConfig;
            decoderConfig.decoderId = (DecoderId)rp2040.fifo.pop();
            decoderConfig.sampleCount = rp2040.fifo.pop();
            decoderConfig.bandwidthHz = rp2040.fifo.pop();

            // opcionális CW cél frekvencia (Hz)
            decoderConfig.cwCenterFreqHz = rp2040.fifo.pop();

            // RTTY paraméterek
            decoderConfig.rttyMarkFreqHz = rp2040.fifo.pop();
            decoderConfig.rttyShiftFreqHz = rp2040.fifo.pop();
            // Float átalakítás FIFO-ból (uint32_t bit pattern)
            uint32_t baudBits = rp2040.fifo.pop();
            memcpy(&decoderConfig.rttyBaud, &baudBits, sizeof(float));

            // WEFAX IOC mód automatikusan detektálódik

            decodedData.textBuffer.clear();
            decodedData.lineBuffer.clear();
            decodedData.cwCurrentWpm = 0;

            AdcDmaC1::CONFIG adcDmaConfig;
            adcDmaConfig.audioPin = PIN_AUDIO_INPUT;
            adcDmaConfig.sampleCount = static_cast<uint16_t>(decoderConfig.sampleCount); // Átadjuk a sampleCount-ot

            // Számoljuk ki a szükséges mintavételi frekvenciát a megadott hangfrekvenciás sávszélességből
            uint32_t finalRate = 0;
            if (decoderConfig.bandwidthHz > 0) {
                uint32_t nyquist = decoderConfig.bandwidthHz * 2u; // Nyquist frekvencia kiszámítása
                // Túlmintavételezés kiszámítása
                uint32_t suggested = static_cast<uint32_t>(ceilf(nyquist * AUDIO_SAMPLING_OVERSAMPLE_FACTOR));
                finalRate = suggested;
            }
            if (finalRate == 0) {
                finalRate = 44100;
            }
            if (finalRate > 65535u) {
                finalRate = 65535u;
            }

            // WEFAX speciális kezelés: PONTOS 11025 Hz mintavételezés
            if (decoderConfig.decoderId == ID_DECODER_WEFAX) {
                finalRate = WEFAX_SAMPLE_RATE_HZ; // Felülírjuk fix 11025 Hz-re
            }

            adcDmaConfig.samplingRate = static_cast<uint16_t>(finalRate); // Átadjuk a számított mintavételi frekvenciát

            // Töltsük vissza a számított mintavételi frekvenciát a decoderConfig-be,
            // hogy a dekóder objektumok (pl. SSTV) megkapják az Fs-et, ha szükségük van rá.
            decoderConfig.samplingRate = finalRate;

            // FFT-t csak azokban a módokban használunk, ahol spektrum vagy FFT-alapú dekódolás szükséges.
            // SSTV: Nincs FFT (saját decode_sstv könyvtár használja)
            // WEFAX: Nincs FFT (FM dekódolás)
            // CW, RTTY, DomFreq: FFT-alapú feldolgozás (AudioProcessor Q15 FFT)
            bool useFFT = (decoderConfig.decoderId != ID_DECODER_SSTV && decoderConfig.decoderId != ID_DECODER_WEFAX);

            // DMA mód beállítása dekóder típusa szerint:
            // - SSTV és WEFAX: BLOKKOLÓ mód (garantált teljes blokk szükséges a pixel-pontos dekódoláshoz)
            // - CW, RTTY, DomFreq: NEM-BLOKKOLÓ mód (kisebb késleltetés, minta-alapú feldolgozás)
            bool useBlockingDma = (decoderConfig.decoderId == ID_DECODER_SSTV || decoderConfig.decoderId == ID_DECODER_WEFAX);

            // DMA és audio processzor inicializálása
            audioProc.initialize(adcDmaConfig, useFFT, useBlockingDma);
            audioProc.reconfigureAudioSampling(adcDmaConfig.sampleCount, adcDmaConfig.samplingRate, decoderConfig.bandwidthHz);

            // Dekóder indítása
            decoderController(decoderConfig);

            // Publikáljuk a futási megjelenítési javaslatokat a Core0 számára (Spectrum UI)
            updateDisplayHints(decoderConfig);

            // Válasz a Core 0 felé
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_STOP: {
            audioProc.stop();    // Audio feldolgozás leállítása
            stopActiveDecoder(); // Dekóder leállítása

            // Pufferek törlése
            memset(sharedData, 0, sizeof(sharedData));
            decodedData.textBuffer.clear();
            decodedData.lineBuffer.clear();
            decodedData.cwCurrentWpm = 0;

            // Válasz a Core 0 felé
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_GET_DATA_BLOCK: {
            // Válasz a Core 0 felé
            rp2040.fifo.push(RP2040ResponseCode::RESP_DATA_BLOCK);
            rp2040.fifo.push(activeSharedDataIndex);
            break;
        }

        case RP2040CommandCode::CMD_GET_SAMPLING_RATE: {
            // Válasz a Core 0 felé
            rp2040.fifo.push(RP2040ResponseCode::RESP_SAMPLING_RATE);
            rp2040.fifo.push(audioProc.getSamplingRate());
            break;
        }
    }
}

/**
 * @brief Audio feldolgozás és dekódolás
 */
void processAudioAndDecoding() {

    // Ping-pong buffer index állítása
    uint8_t backBufferIndex = 1 - activeSharedDataIndex;

    // ADC + DMA műveletek
    if (audioProc.processAndFillSharedData(sharedData[backBufferIndex])) {

        // Sikeres feldolgozás esetén puffert cserélünk
        activeSharedDataIndex = backBufferIndex;

        // Az aktív dekóder futtatása a frissen feldolgozott adatokon
        SharedData &currentData = sharedData[activeSharedDataIndex];

        // Audio feldolgozás és dekódolás
        if (activeDecoderCore1 != nullptr) {
            activeDecoderCore1->processSamples(currentData.rawSampleData, currentData.rawSampleCount);
        }
    }
}

//--- EEprom safe Writer segédfüggvények -------------------------------------------------------------------------------------
void startAudioSamplingC1() { audioProc.start(); }

void stopAudioSamplingC1() { audioProc.stop(); }

bool isAudioSamplingRunningC1() { return audioProc.isRunning(); }

//--- Core-1 Arduino belépési pontok -----------------------------------------------------------------------------------------

/**
 * @brief Core 1 inicializálása.
 */
void setup1() {

    delay(1500);
    DEBUG("core-1: System clock: %u Hz\n", (unsigned)clock_get_hz(clk_sys));
    memset(sharedData, 0, sizeof(sharedData));
}

/**
 * Core 1 fő ciklusfüggvénye.
 */
void loop1() {

    // Parancsok kezelése a Core 0-tól
    processFifoCommands();

    // Audio feldolgozás és dekódolás
    if (activeDecoderIdCore1 != ID_DECODER_NONE) {
        processAudioAndDecoding();

    } else {
#ifdef __DEBUG
        static uint32_t warnCount = 0;
        if (++warnCount % 1000 == 0) {
            DEBUG("core-1: processAudioAndDecoding: Inactive\n");
        }
#endif
        sleep_ms(5);
    }
}
