/*
 * Projekt: [pico-radio-9] Raspberry Pi Pico Si4735 Radio
 * Fájl: decoder_api.h
 * Létrehozva: 2025.11.15.
 *
 * Szerző: BT-Soft
 * GitHub: https://github.com/bt-soft
 * Blog: https://electrodiy.blog.hu/
 * -----
 * Copyright (c) 2025 BT-Soft
 * Licenc: MIT
 * 	A forrás szabadon használható, módosítható és terjeszthető; egyetlen feltétel a licenc és a szerző feltüntetése.
 * -----
 * Utolsó módosítás: 2025.11.29.
 * Módosította: BT-Soft
 * -----
 * TÖRTÉNET (HISTORY):
 * Dátum		By	Megjegyzés
 * ---------------------------------------------------------------
 */

#pragma once
#include <cstdint>

#include "RingBuffer.h" // A ring buffer implementációnk
#include "defines.h"

/**
 * @brief CMSIS-DSP Q15 fixpontos típus definíció
 * 16 bites fixpontos adattípus 1.15 formátumban.
 * Csek az kellene az arm_math.h-ból,csak ezért nem húzzuk be a headert
 */
typedef int16_t q15_t;

constexpr float Q15_SCALE = 32768.0f;        // (2^15)
constexpr float Q15_MAX_AS_FLOAT = 32767.0f; // Max pozitív érték float-ként

/**
 * @brief Dekóder azonosítók
 */
enum DecoderId : uint32_t {
    ID_DECODER_NONE = 0,
    ID_DECODER_DOMINANT_FREQ,
    ID_DECODER_SSTV,
    ID_DECODER_CW,
    ID_DECODER_RTTY,
    ID_DECODER_WEFAX,
    ID_DECODER_ONLY_FFT, // Nincs dekóder csak FFT feldolgozás
};

/**
 * @brief RP2040 Parancskódok a core0 -> core1 kommunikációhoz
 */
enum RP2040CommandCode : uint32_t {
    CMD_NOP = 0,
    CMD_STOP,
    CMD_SET_CONFIG,
    CMD_GET_SAMPLING_RATE, //  mintavételezési sebesség lekérésére

    // AudioProcessor specifikus parancsok
    CMD_AUDIOPROC_SET_BLOCKING_DMA_MODE,        // ADC DMA blokkoló/nem-blokkoló mód beállítása
    CMD_AUDIOPROC_SET_AGC_ENABLED,              // AudioProcessor AGC engedélyezése
    CMD_AUDIOPROC_SET_NOISE_REDUCTION_ENABLED,  // AudioProcessor zajcsökkentés engedélyezése
    CMD_AUDIOPROC_SET_SMOOTHING_POINTS,         // AudioProcessor zajcsökkentés simítási pontjainak beállítása
    CMD_AUDIOPROC_SET_MANUAL_GAIN,              // AudioProcessor manuális erősítés beállítása
    CMD_AUDIOPROC_SET_SPECTRUM_AVERAGING_COUNT, // AudioProcessor spektrum nem-koherens átlagolási keretszámának beállítása
    CMD_AUDIOPROC_SET_USE_FFT_ENABLED,          // AudioProcessor FFT engedélyezés beállítása
    CMD_AUDIOPROC_GET_USE_FFT_ENABLED,          // AudioProcessor FFT engedélyezés lekérdezése
    CMD_AUDIOPROC_CALIBRATE_DC,                 // AudioProcessor: DC midpoint kalibrálás Core1-en

    // Dekóder specifikus parancsok
    CMD_DECODER_SET_USE_ADAPTIVE_THRESHOLD, // Dekóder adaptív küszöb használatának beállítása
    CMD_DECODER_GET_USE_ADAPTIVE_THRESHOLD, // Dekóder adaptív küszöb lekérdezése
    CMD_DECODER_RESET,                      // Dekóder reset parancs
    CMD_DECODER_SET_BANDPASS_ENABLED,       // Dekóder sávszűrő engedélyezés/tiltás
};

/**
 * @brief RP2040 FIFO Válaszkódok a core1 -> core0 üzenetekre
 */
enum RP2040ResponseCode : uint32_t {
    // RESP_NOP = 0,
    RESP_ACK = 200,
    RESP_NACK = 201,
    // RESP_ACTUAL_RATE = 202,
    // RESP_CONFIG = 203,
    RESP_DATA_BLOCK = 204,     // az aktív puffer indexét tartalmazza
    RESP_SAMPLING_RATE = 205,  // a mintavételezési sebességhez tartozó válasz
    RESP_USE_FFT_ENABLED = 206 // FFT engedélyezés lekérdezéséhez
    ,
    RESP_USE_ADAPTIVE_THRESHOLD = 207
};

/**
 * @brief Konfigurációs struktúra egyszerűsítve (FIFO-n keresztül mezők push-olva)
 */
struct DecoderConfig {
    DecoderId decoderId;
    uint32_t samplingRate;
    uint32_t sampleCount;
    uint32_t bandwidthHz;
    uint32_t cwCenterFreqHz; // opcionális: CW/tonális dekóderek cél frekvenciája

    // RTTY-specifikus opcionális paraméterek (Hz, Baud)
    uint32_t rttyMarkFreqHz;
    uint32_t rttyShiftFreqHz;
    float rttyBaud; // Baud rate float-ként (pl. 45.45, 50, 75, 100)
};

// Audio FFT bemenet
#define PIN_AUDIO_INPUT A0 // A0/GPIO26 az FFT audio bemenethez

// ADC paraméterek
#define ADC_REFERENCE_VOLTAGE_MV 3300.0f                                            // ADC referencia feszültség mV-ban
#define ADC_LSB_VOLTAGE_MV (ADC_REFERENCE_VOLTAGE_MV / (float)(1 << ADC_BIT_DEPTH)) // 1 ADC LSB minta hány mV?

#define ADC_BIT_DEPTH 12 // ADC felbontás bit-ben
// Az ADC középpontját (midpoint) most futásidőben méri az AudioProcessorC1 és
// a `adcMidpoint_` mezőben tárolja. A fordítási időben használt ADC_MIDPOINT
// konstans eltávolításra került, hogy kezelni tudjuk a valós, nem-ideális DC offseteket.

// --- Megosztott Adatstruktúrák ---

// Nagy sebességű, pillanatkép-szerű adatokhoz (ping-pong bufferelve)
#define MAX_RAW_SAMPLES_SIZE 1024
#define MAX_FFT_SPECTRUM_SIZE 512

//--- Dekóder specifikus paraméterek ---
#define AUDIO_SAMPLING_OVERSAMPLE_FACTOR 1.25f // Az audio mintavételezés túlmintavételezési tényezője

// Audio feldolgozó által kitöltött adatok
struct SharedData {
    // RAW audio minták
    uint16_t rawSampleCount;
    int16_t rawSampleData[MAX_RAW_SAMPLES_SIZE];

    // FFT spektrum adatok (Q15 - CMSIS-DSP fixpontos)
    uint16_t fftSpectrumSize;
    q15_t fftSpectrumData[MAX_FFT_SPECTRUM_SIZE];

    uint32_t dominantFrequency; // Domináns frekvencia Hz-ben
    q15_t dominantAmplitude;    // Amplitúdó a domináns frekvencián (Q15 fixpontos)
    float fftBinWidthHz;        // FFT bin szélessége Hz-ben

    // Opcionális futási megjelenítési határok, amelyeket a Core1 tölt ki, amikor a dekóder konfigurációja megváltozik
    uint16_t displayMinFreqHz; // Javasolt minimális frekvencia megjelenítéshez (Hz)
    uint16_t displayMaxFreqHz; // Javasolt maximális frekvencia megjelenítéshez (Hz)
};

// Dekódolt, stream-szerű text adatokhoz (ring bufferelve)
#define TEXT_BUFFER_SIZE 64 // 64 karakter a CW/RTTY szövegnek (növelve a stabilitás érdekében)

// FM audio sávszélesség
#define FM_AF_BANDWIDTH_HZ MAX_AUDIO_FREQUENCY_HZ // FM dekódolt audio sávszélesség (Hz)
#define FM_AF_RAW_SAMPLES_SIZE 256                // FM módban a minták száma blokkonként (128->256: jobb frekvencia felbontás)

// AM audio sávszélesség (Sima Közép Hullámú vagy SW AM demoduláció esetén)
#define AM_AF_BANDWIDTH_HZ 6000     // AM dekódolt audio sávszélesség (Hz)
#define AM_AF_RAW_SAMPLES_SIZE 1024 // AM módban a minták száma blokkonként

// Dominant Frequency Dekóder paraméterek
// A mintavételezési frekvenciát a sávszélességből számoljuk (Nyquist + margin),
// ezért itt csak a nyers minták számát és a sávszélességet tartjuk meg.
#define DOMINANT_FREQ_AF_BANDWIDTH_HZ MAX_AUDIO_FREQUENCY_HZ // Dominant Frequency audio sávszélesség
#define DOMINANT_FREQ_RAW_SAMPLES_SIZE 1024                  // Dominant Frequency bemeneti audio minták száma blok

// CW paraméterek
// A mintavételezési frekvencia a CW sávszélességből számolódik.
// FONTOS: A CW_RAW_SAMPLES_SIZE szabadon változtatható, de a dekóder belsőleg
//         128 mintás blokkokkal dolgozik (SAMPLES_PER_BLOCK = 128, ez NE változzon!)
#define CW_AF_BANDWIDTH_HZ 1500 // CW audio sávszélesség (szabadon változtatható)
#define CW_RAW_SAMPLES_SIZE 128 // CW bemeneti audio minták száma blok (jelenleg egyenlő a belső blokk mérettel)

// RTTY paraméterek
// Mintavételezési frekvencia a sávszélességből számítódik.
// FONTOS: Az RTTY_RAW_SAMPLES_SIZE szabadon változtatható, de a dekóder belsőleg
//         64 mintás Goertzel blokkokkal dolgozik (TONE_BLOCK_SIZE = 64, ez NE változzon!)
#define RTTY_AF_BANDWIDTH_HZ 6000 // RTTY audio sávszélesség (szabadon változtatható) -> 15kHz volt ..
// RTTY bemeneti audio minták száma blok (lehet több, mint a belső blokk méret)
#define RTTY_RAW_SAMPLES_SIZE 512 // A zoom miatt ilyen magas, hogy a waterfall is kinézzen valahogyan. -> 1024 volt...

// SSTV paraméterek
// Mintavételezési frekvencia a sávszélességből számítódik.
#define C_SSTV_DECODER_SAMPLE_RATE_HZ MAX_AUDIO_FREQUENCY_HZ // A 'c_sstv_decoder' SSTV dekóder 'bevarrt' mintavételezési frekvenciája
// SSTV audio sávszélesség -> Ebből 15kHz lesz a mintavételezés, ez kell az SSTV-nek
// 15kHz / 2 = 7.5kHz Nyquist -> 7.5kHz / 1.25 = 6kHz sávszélesség
#define SSTV_AF_BANDWIDTH_HZ (C_SSTV_DECODER_SAMPLE_RATE_HZ / 2.0f / AUDIO_SAMPLING_OVERSAMPLE_FACTOR)
#define SSTV_RAW_SAMPLES_SIZE 1024 // Bemeneti audio minták száma blokkonként
#define SSTV_LINE_WIDTH 320        // Martin M1 szélesség
#define SSTV_LINE_HEIGHT 256       // Martin M1 magasság
#define SSTV_LINE_BUFFER_SIZE 4    // 4 sor SSTV kép pufferelése, BW12-nek már 4 kell

// WEFAX paraméterek (FM demodulátor)
// Számítás: finalRate = bandwidthHz * 2 * AUDIO_SAMPLING_OVERSAMPLE_FACTOR = bandwidthHz * 2 * 1.25 = bandwidthHz * 2.5
// Tehát: 11025 = bandwidthHz * 2.5  =>  bandwidthHz = 11025 / 2.5 = 4410 Hz
#define WEFAX_SAMPLE_RATE_HZ 11025 // WEFAX mintavételezési frekvencia (fix)
#define WEFAX_AF_BANDWIDTH_HZ 4410 // Számított a 11025 Hz mintavételezéshez (4410 * 2.5 = 11025)
#define WEFAX_RAW_SAMPLES_SIZE 128 // Több sample/blokk a jobb fázis felbontáshoz (FM demod)

// WEFAX képszélesség mód szerint (IOC = Index of Cooperation)
#define WEFAX_IOC576_WIDTH 1809                   // IOC 576: 576 * π ≈ 1809 pixel/sor
#define WEFAX_IOC288_WIDTH 904                    // IOC 288: 288 * π ≈ 904 pixel/sor
#define WEFAX_MAX_OUTPUT_WIDTH WEFAX_IOC576_WIDTH // Maximum szélesség a buffer mérethez

// Dekódolt sor-típus: egy tömb, amely vagy SSTV sort vagy WEFAX sort tartalmaz.
// Mivel egyszerre sosem fut a két dekóder, ugyanazt a memóriaterületet újra tudjuk használni.
struct DecodedLine {
    // A rajzoláshoz szükséges y koordináta (vagy a ring index)
    uint16_t lineNum;

    // Union: SSTV és WEFAX nem fut egyszerre, így ugyanaz a memória terület használható
    union {
        // SSTV: RGB565 pixelek (320px széles × 2 bájt = 640 bájt)
        uint16_t sstvPixels[SSTV_LINE_WIDTH];
        // WEFAX: 8-bit grayscale pixelek (IOC576: 1809px vagy IOC288: 904px, Core-0 skálázza 480px-re)
        uint8_t wefaxPixels[WEFAX_MAX_OUTPUT_WIDTH];
    };
};

// Méret: a ring buffer mérete legyen a maximum, amelyet egyszerre szeretnénk tárolni.
#define DECODED_LINE_BUFFER_SIZE 2

// Dekódolt adatok struktúrája
struct DecodedData {

    // Egy közös szöveg buffer, amelyet CW és RTTY is használhat.
    RingBuffer<char, TEXT_BUFFER_SIZE> textBuffer;

    // Egy közös sor buffer, amelyet SSTV és WEFAX is használhat.
    RingBuffer<DecodedLine, DECODED_LINE_BUFFER_SIZE> lineBuffer;

    // SSTV/WEFAX események (Core-1 írja, Core-0 olvassa és törli)
    volatile bool newImageStarted; // True ha új kép kezdődött (pixel_y == 0)
    volatile bool modeChanged;     // True ha SSTV/WEFAX mód változott
    volatile uint8_t currentMode;  // Aktuális SSTV/WEFAX mód ID (c_sstv_decoder::e_mode)

    // CW-specifikus státuszok (Core1 írja, Core0 olvassa)
    volatile uint8_t cwCurrentWpm;   // Utolsó becsült WPM érték
    volatile uint16_t cwCurrentFreq; // Aktuálisan detektált CW frekvencia (Hz)

    // RTTY-specifikus státuszok (Core1 írja, Core0 olvassa)
    volatile uint16_t rttyMarkFreq;  // Mark frekvencia (Hz)
    volatile uint16_t rttySpaceFreq; // Space frekvencia (Hz)
    volatile float rttyBaudRate;     // Baud sebesség (pl. 45.45, 50, 75, 100)
};

#define DECODER_MODE_UNKNOWN "Unknown"