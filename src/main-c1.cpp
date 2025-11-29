/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: main-c1.cpp                                                                                                   *
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
 * Last Modified: 2025.11.29, Saturday  06:02:30                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include <Arduino.h>
#include <algorithm>
#include <cstring>
#include <hardware/adc.h> // ADC hardware hozzáférés Core1 szenzor mérésekhez
#include <memory>

#include "AudioProcessor-c1.h"
#include "DecoderCW-c1.h"
#include "DecoderRTTY-c1.h"
#include "DecoderSSTV-c1.h"
#include "DecoderWeFax-c1.h"
#include "Utils.h"
#include "adc-constants.h"
#include "defines.h"

// Core-1 debug engedélyezése de csak DEBUG módban
#define __CORE1_DEBUG
#if defined(__DEBUG) && defined(__CORE1_DEBUG)
#define CORE1_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define CORE1_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// A Core-1-nek stack külön legyen a Core-0-tól
// https://arduino-pico.readthedocs.io/en/latest/multicore.html#stack-sizes
bool core1_separate_stack = true;

//-------------------------------------------------------------------------------------
//  Globális osztott memóriaterületek a Core-0 és Core-1 között
//-------------------------------------------------------------------------------------
// Osztott adatpufferek Core-0 és Core-1 között
// A két elemű sharedData[2] tömb azért van, hogy a két mag (Core 0 és Core 1) között
// biztonságosan lehessen adatot cserélni, "ping-pong" vagy "double-buffering" technikával.
SharedData sharedData[2];
volatile uint8_t activeSharedDataIndex = 0; // Aktív SharedData index (0 vagy 1)

// A dekódolt adatok globális példánya, ezt éri el a Core-0 is
DecodedData decodedData;

// CORE1 szenzor adatok (Core1 olvassa az ADC-t, Core0 csak kijelzi!)
volatile float core1_VbusVoltage;    // VBUS feszültség (Volt) - Core1 méri ADC1-ről
volatile float core1_CpuTemperature; // CPU hőmérséklet (Celsius) - Core1 méri ADC4-ről

//-------------------------------------------------------------------------------------

// Audio feldolgozó példányja
static AudioProcessorC1 audioProcC1; // Static -> global instance

// Core-1 aktív dekóder azonosítója
static DecoderId activeDecoderIdCore1 = ID_DECODER_NONE;
std::unique_ptr<IDecoder> activeDecoderCore1 = nullptr;

//--- EEprom safe Writer segédfüggvények -------------------------------------------------------------------------------------

/**
 * @brief Core1 audio mintavételezés indítása
 */
void startAudioSamplingC1() { audioProcC1.start(); }

/**
 * @brief Core1 audio mintavételezés leállítása
 */
void stopAudioSamplingC1() { audioProcC1.stop(); }

/**
 * @brief Core1 audio mintavételezés fut?
 */
bool isAudioSamplingRunningC1() { return audioProcC1.isRunning(); }

/**
 * @brief Core1 szenzor mérések - VBUS feszültség és CPU hőmérséklet
 * @details Ez a függvény a Core1-en fut, így TELJES KONTROLLJA van az ADC felett.
 * BIZTONSÁGOS MÉRÉSI STRATÉGIA:
 * 1. Audio DMA SZÜNETEL a mérés alatt (ez FONTOS!)
 * 2. Arduino analogRead() / analogReadTemp() wrapper használata
 * 3. ADC csatorna visszaállítás ADC0-ra
 * 4. Audio DMA FOLYTATÁSA
 */
void readSensorsOnCore1() {

    // // BIZTONSÁGI ELLENŐRZÉS: Csak akkor mérünk, ha az audio DMA NEM fut
    // // Ha bármilyen audio feldolgozás aktív (FFT, CW, RTTY), NE szakítsuk meg a DMA-t
    // // → race condition elkerülése a Core0 TFT műveletekkel (spectrum rajzolás, stb.)
    // bool isAudioRunning = audioProcC1.isRunning();

    // if (isAudioRunning) {
    //     // Ha audio DMA fut, NE szakítsuk meg - a cache-olt értékek továbbra is elérhetők
    //     // (core1_VbusVoltage, core1_CpuTemperature)
    //     return;
    // }

    // Ha fut az Audio DMA, akkor lecsukjuk a mérés idejéig
    bool wasAudioActive = isAudioSamplingRunningC1();
    if (wasAudioActive) {
        stopAudioSamplingC1();
    }

    // CSAK akkor mérünk, ha az audio DMA teljesen inaktív (BIZTONSAGOS)

    // VBUS feszültség mérése 12-bit ADC olvasással
    float voltageOut = (analogRead(PIN_VBUS_EXTERNAL_MEASURE_INPUT) * CORE1_ADC_V_REFERENCE) / CORE1_ADC_CONVERSION_FACTOR;
    core1_VbusVoltage = voltageOut * CORE1_VBUSDIVIDER_RATIO;

    // CPU hőmérséklet mérése (analogReadTemp() is 12-bit az RP2040-en)
    core1_CpuTemperature = analogReadTemp();

    // KRITIKUS: ADC csatorna VISSZAÁLLÍTÁSA az audio bemenetre (ADC0)!
    adc_select_input(0); // ADC0 (GPIO26 = A0)

    // Audio DMA visszaindítása, ha előtte futott
    if (wasAudioActive) {
        startAudioSamplingC1();
    }

    CORE1_DEBUG("core-1: Sensors: VBUS=%.2fV, Temp=%.1f°C\n", //
                core1_VbusVoltage,                            //
                core1_CpuTemperature);
}

/**
 * @brief Frissíti a megjelenítési javaslatokat a megadott dekóder konfiguráció alapján.
 * @param cfg A dekóder konfiguráció
 */
void updateDisplayHints(const DecoderConfig &cfg) {

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

        uint16_t minf =
            (std::min(f_mark, f_space) > static_cast<uint16_t>(margin)) ? static_cast<uint16_t>(std::min(f_mark, f_space) - static_cast<uint16_t>(margin)) : 0u;
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
        CORE1_DEBUG("core-1: updateDisplayHints() -> min=%u Hz, max=%u Hz (back=%u)\n", dispMin, dispMax, backBufferIndex);
    }
}

/**
 * Az aktív dekóder leállítása és felszabadítása.
 */
void stopActiveDecoder() {
    if (activeDecoderCore1 != nullptr) {
        activeDecoderCore1->stop();
        CORE1_DEBUG("core-1: Dekóder '%s' leállítva\n", activeDecoderCore1->getDecoderName());
        activeDecoderCore1.reset();
        activeDecoderIdCore1 = ID_DECODER_NONE;
        CORE1_DEBUG("core-1: Dekóder objektum felszabadítva (reset)\n");
    }
}

/**
 * Általános dekóder vezérlő függvény.
 * @param decoderConfig Az új dekóder konfiguráció
 */
void startDecoder(DecoderConfig decoderConfig) {

    // Ha van dekóder, de új ID jött, akkor leállítjuk a régit
    if (decoderConfig.decoderId != activeDecoderIdCore1 && activeDecoderCore1 != nullptr) {
        stopActiveDecoder();
    }

    // Ha nem kell dekóder, akkor kilépünk
    if (decoderConfig.decoderId == ID_DECODER_NONE) {
        activeDecoderIdCore1 = ID_DECODER_NONE;
        CORE1_DEBUG("core-1: Nincs dekóder kiválasztva, kilépés\n");
        return;
    }

    // Pufferek törlése új dekóder indításakor (CW/RTTY esetén a szöveg buffer, SSTV/WEFAX esetén a kép buffer)
    decodedData.textBuffer.clear();
    decodedData.lineBuffer.clear();
    decodedData.cwCurrentWpm = 0;

    // Létrehozzuk az új dekódert
    switch (decoderConfig.decoderId) {

        // Nincs dekóder csak FFT feldolgozás
        case ID_DECODER_ONLY_FFT:
            activeDecoderIdCore1 = ID_DECODER_ONLY_FFT;
            CORE1_DEBUG("core-1: Csak FFT feldolgozás elindítva\n");
            break;

            // Domináns frekvencia detektor
        case ID_DECODER_DOMINANT_FREQ:
            activeDecoderIdCore1 = ID_DECODER_DOMINANT_FREQ;
            CORE1_DEBUG("core-1: Dominant Frequency dekóder elindítva\n");
            break;

            // CW mód: Goertzel alapú tónus detektálás + Morse dekódolás
        case ID_DECODER_CW:
            activeDecoderCore1 = std::make_unique<DecoderCW_C1>();
            activeDecoderCore1->start(decoderConfig);
            activeDecoderIdCore1 = ID_DECODER_CW;
            CORE1_DEBUG("core-1: CW dekóder elindítva (%u Hz, adaptív)\n", decoderConfig.cwCenterFreqHz);
            break;

            // RTTY mód: Goertzel alapú tone detektálás + Baudot dekódolás
        case ID_DECODER_RTTY:
            activeDecoderCore1 = std::make_unique<DecoderRTTY_C1>();
            activeDecoderCore1->start(decoderConfig);
            activeDecoderIdCore1 = ID_DECODER_RTTY;
            CORE1_DEBUG("core-1: RTTY dekóder elindítva\n");
            break;

            // SSTV mód: kép dekódolás audio mintákból
        case ID_DECODER_SSTV:
            activeDecoderCore1 = std::make_unique<DecoderSSTV_C1>();
            activeDecoderCore1->start(decoderConfig);
            activeDecoderIdCore1 = ID_DECODER_SSTV;
            break;

            // WEFAX mód: teljes WEFAX FM dekódolás Goertzel-lel
        case ID_DECODER_WEFAX:
            activeDecoderCore1 = std::make_unique<DecoderWeFax_C1>();
            activeDecoderCore1->start(decoderConfig);
            activeDecoderIdCore1 = ID_DECODER_WEFAX;
            break;

        default:
            CORE1_DEBUG("core-1: HIBA - Ismeretlen dekóder ID: %d\n", decoderConfig.decoderId);
            activeDecoderIdCore1 = ID_DECODER_NONE;
            return;
    }

    if (activeDecoderCore1 != nullptr) {
        CORE1_DEBUG("core-1: Dekóder '%s' elindítva\n", activeDecoderCore1->getDecoderName());
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
            // KRITIKUS: Először leállítjuk az audioProcC1-et és a dekódert
            // hogy ne legyen DMA konfliktus újrakonfiguráláskor
            CORE1_DEBUG("core-1: CMD_SET_CONFIG - DMA és dekóder leállítása...\n");
            audioProcC1.stop();
            stopActiveDecoder();

            CORE1_DEBUG("core-1: CMD_SET_CONFIG - Konfiguráció olvasása a FIFO-ból...\n");
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

            // Pufferek törlése új konfiguráció előtt
            memset(sharedData, 0, sizeof(sharedData));
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
            CORE1_DEBUG("core-1: CMD_SET_CONFIG - AudioProcessor inicializálása (sampleCount=%d, samplingRate=%d, useFFT=%d, blocking=%d)\n",
                        adcDmaConfig.sampleCount, adcDmaConfig.samplingRate, useFFT, useBlockingDma);
            audioProcC1.initialize(adcDmaConfig, useFFT, useBlockingDma);
            audioProcC1.reconfigureAudioSampling(adcDmaConfig.sampleCount, adcDmaConfig.samplingRate, decoderConfig.bandwidthHz);

            // Dekóder indítása
            CORE1_DEBUG("core-1: CMD_SET_CONFIG - Dekóder indítása (ID=%d)...\n", (int)decoderConfig.decoderId);
            startDecoder(decoderConfig);

            // Publikáljuk a futási megjelenítési javaslatokat a Core0 számára (Spectrum UI)
            updateDisplayHints(decoderConfig);

            CORE1_DEBUG("core-1: CMD_SET_CONFIG - Kész, ACK küldése\n");
            // Válasz a Core 0 felé
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_STOP: {
            audioProcC1.stop();  // Audio feldolgozás leállítása
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

        case RP2040CommandCode::CMD_GET_SAMPLING_RATE: {
            // Válasz a Core 0 felé
            rp2040.fifo.push(RP2040ResponseCode::RESP_SAMPLING_RATE);
            rp2040.fifo.push(audioProcC1.getSamplingRate());
            break;
        }
        case RP2040CommandCode::CMD_AUDIOPROC_GET_USE_FFT_ENABLED: {
            rp2040.fifo.push(RP2040ResponseCode::RESP_USE_FFT_ENABLED);
            rp2040.fifo.push(audioProcC1.isUseFFT() ? 1 : 0);
            break;
        }

        case RP2040CommandCode::CMD_AUDIOPROC_SET_AGC_ENABLED: {
            bool enabled = (rp2040.fifo.pop() != 0);
            audioProcC1.setAgcEnabled(enabled);
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_AUDIOPROC_SET_NOISE_REDUCTION_ENABLED: {
            bool enabled = (rp2040.fifo.pop() != 0);
            audioProcC1.setNoiseReductionEnabled(enabled);
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_AUDIOPROC_SET_SMOOTHING_POINTS: {
            uint32_t points = rp2040.fifo.pop();
            audioProcC1.setSmoothingPoints(static_cast<uint8_t>(points));
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_AUDIOPROC_SET_SPECTRUM_AVERAGING_COUNT: {
            uint32_t n = rp2040.fifo.pop();
            // Biztonsági korlátozás: ha túl nagy érték jön, korlátozzuk (pl. 1..32)
            if (n == 0)
                n = 1;
            if (n > 64)
                n = 64;
            audioProcC1.setSpectrumAveragingCount(static_cast<uint8_t>(n));
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_AUDIOPROC_SET_MANUAL_GAIN: {
            uint32_t gainBits = rp2040.fifo.pop();
            float gain;
            memcpy(&gain, &gainBits, sizeof(float));
            audioProcC1.setManualGain(gain);
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }
        case RP2040CommandCode::CMD_AUDIOPROC_SET_BLOCKING_DMA_MODE: {
            bool blocking = (rp2040.fifo.pop() != 0);
            audioProcC1.setBlockingDmaMode(blocking);
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_AUDIOPROC_SET_USE_FFT_ENABLED: {
            bool enabled = (rp2040.fifo.pop() != 0);
            audioProcC1.setUseFFT(enabled);
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_AUDIOPROC_CALIBRATE_DC: {
            // Perform DC midpoint calibration on Core1 and ACK
            audioProcC1.calibrateDcMidpoint();
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_DECODER_SET_USE_ADAPTIVE_THRESHOLD: {
            bool enabled = (rp2040.fifo.pop() != 0);
            // Beállítjuk a dekóder adaptív küszöb használatát
            if (activeDecoderCore1 != nullptr) {
                activeDecoderCore1->setUseAdaptiveThreshold(enabled);
            }
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_DECODER_RESET: {
            // Core0 kéri az aktív dekóder resetelését
            if (activeDecoderCore1 != nullptr) {
                activeDecoderCore1->reset();
                CORE1_DEBUG("core-1: CMD_DECODER_RESET - Aktív dekóder resetelve\n");
            }
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_DECODER_SET_BANDPASS_ENABLED: {
            bool enabled = (rp2040.fifo.pop() != 0);
            if (activeDecoderCore1 != nullptr) {
                activeDecoderCore1->enableBandpass(enabled);
                CORE1_DEBUG("core-1: CMD_DECODER_SET_BANDPASS_ENABLED -> %d\n", enabled);
            }
            rp2040.fifo.push(RP2040ResponseCode::RESP_ACK);
            break;
        }

        case RP2040CommandCode::CMD_DECODER_GET_USE_ADAPTIVE_THRESHOLD: {
            // Visszaküldjük a dekóder adaptív küszöb állapotát (ha CW dekóder aktív)
            uint32_t enabled = 0;
            if (activeDecoderCore1 != nullptr) {
                enabled = activeDecoderCore1->getUseAdaptiveThreshold() ? 1 : 0;
            }
            rp2040.fifo.push(RP2040ResponseCode::RESP_USE_ADAPTIVE_THRESHOLD);
            rp2040.fifo.push(enabled);
            break;
        }
        default:
            CORE1_DEBUG("core-1: Ismeretlen parancs a FIFO-ból: %u\n", command);
            break;
    }
}

/**
 * @brief Audio feldolgozás és dekódolás
 */
void processAudioAndDecoding() {

    // Ping-pong buffer index állítása
    uint8_t backBufferIndex = 1 - activeSharedDataIndex;

    // ADC + DMA műveletek
    if (audioProcC1.processAndFillSharedData(sharedData[backBufferIndex])) {

        // CORE1_DEBUG("core-1: processAudioAndDecoding(): Audio feldolgozás kész, SharedData index váltás %u -> %u\n", activeSharedDataIndex, backBufferIndex);

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

//--- Core-1 Arduino belépési pontok -----------------------------------------------------------------------------------------

/**
 * @brief Core 1 inicializálása.
 */
void setup1() {

    // ADC felbontás beállítása 12-bitre (mint az audio DMA)
    analogReadResolution(CORE1_ADC_RESOLUTION);

    // Shared területek inicializálása
    memset(sharedData, 0, sizeof(sharedData));
    core1_VbusVoltage = 0.0f;
    core1_CpuTemperature = 0.0f;

    // Az első szenzor olvasás rögtön az induláskor
    readSensorsOnCore1();

    delay(3000); // Várakozás a Core-0 indulására és inicializálására
    CORE1_DEBUG("core-1:setup1(): System clock: %u MHz\n", (unsigned)clock_get_hz(clk_sys) / 1000000u);
}

/**
 * Core 1 fő ciklusfüggvénye.
 */
void loop1() {

    // Parancsok kezelése a Core 0-tól
    processFifoCommands();

    // --- Core1 Szenzor Mérések  ---
    // #ifdef __DEBUG
    // #define CORE1_SENSOR_READ_INTERVAL_MS 30 * 1000 //  30 másodperc szenzor frissítési időköz (DEBUG módban gyakoribb)
    // #else

#define CORE1_SENSOR_READ_INTERVAL_MS 15 * 60 * 1000 // 5 perc a szenzor frissítési időköz

    // #endif

    static uint32_t lastSensorRead = 0;
    if (Utils::timeHasPassed(lastSensorRead, CORE1_SENSOR_READ_INTERVAL_MS)) {
        readSensorsOnCore1();
        lastSensorRead = millis();
    }

    // Audio feldolgozás és dekódolás
    if (activeDecoderIdCore1 != ID_DECODER_NONE) {
        processAudioAndDecoding();

    } else {
#ifdef __DEBUG
        static uint32_t warnCount = 0;
        if (++warnCount % 1000 == 0) {
            CORE1_DEBUG("core-1:loop1(): processAudioAndDecoding: Inactive\n");
        }
#endif
        sleep_ms(5);
    }
}
