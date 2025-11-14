#pragma once
#include <cstdint>

#include "RingBuffer.h" // A ring buffer implementációnk
#include "defines.h"

/**
 * @brief Dekóder azonosítók
 */
enum DecoderId : uint32_t {
    ID_DECODER_NONE = 0,
    ID_DECODER_DOMINANT_FREQ = 1,
    ID_DECODER_SSTV = 2,
    ID_DECODER_CW = 3,
    ID_DECODER_RTTY = 4,
    ID_DECODER_WEFAX = 5,
    ID_DECODER_ONLY_FFT = 6, // Nincs dekóder csak FFT feldolgozás
};

/**
 * @brief RP2040 Parancskódok a core0 -> core1 kommunikációhoz
 */
enum RP2040CommandCode : uint32_t {
    CMD_SET_MANUAL_GAIN = 10,
    CMD_NOP = 0,
    CMD_STOP = 1,
    CMD_SET_CONFIG = 2,
    // CMD_GET_CONFIG = 3,
    // CMD_PING = 4,
    CMD_GET_DATA_BLOCK = 5,    //  megosztott adatblokk indexének lekérésére
    CMD_GET_SAMPLING_RATE = 6, //  mintavételezési sebesség lekérésére
    CMD_SET_AGC_ENABLED = 7,
    CMD_SET_NOISE_REDUCTION_ENABLED = 8,
    CMD_SET_SMOOTHING_POINTS = 9
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
    RESP_DATA_BLOCK = 204,   // az aktív puffer indexét tartalmazza
    RESP_SAMPLING_RATE = 205 // a mintavételezési sebességhez tartozó válasz
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
#define ADC_BIT_DEPTH 12
#define ADC_MIDPOINT (1 << (ADC_BIT_DEPTH - 1)) // DC offset az ADC-hez (2048 a 12 bithez)

// --- Megosztott Adatstruktúrák ---

// Nagy sebességű, pillanatkép-szerű adatokhoz (ping-pong bufferelve)
#define SPECTRUM_SIZE 512
#define MAX_RAW_SAMPLES_SIZE 1024
//--- Dekóder specifikus paraméterek ---
#define AUDIO_SAMPLING_OVERSAMPLE_FACTOR 1.25f // Az audio mintavételezés túlmintavételezési tényezője

// Audio feldolgozó által kitöltött adatok
struct SharedData {
    // RAW audio minták
    uint16_t rawSampleCount;
    int16_t rawSampleData[MAX_RAW_SAMPLES_SIZE];

    // FFT spektrum adatok
    uint16_t fftSpectrumSize;
    int16_t fftSpectrumData[SPECTRUM_SIZE];

    uint32_t dominantFrequency; // Domináns frekvencia Hz-ben
    int16_t dominantAmplitude;  // Amplitúdó a domináns frekvencián
    float fftBinWidthHz;        // FFT bin szélessége Hz-ben

    // Opcionális futási megjelenítési határok, amelyeket a Core1 tölt ki, amikor a dekóder konfigurációja megváltozik
    uint16_t displayMinFreqHz; // Javasolt minimális frekvencia megjelenítéshez (Hz)
    uint16_t displayMaxFreqHz; // Javasolt maximális frekvencia megjelenítéshez (Hz)
};

// Dekódolt, stream-szerű text adatokhoz (ring bufferelve)
#define TEXT_BUFFER_SIZE 64 // 64 karakter a CW/RTTY szövegnek (növelve a stabilitás érdekében)

// FM audio sávszélesség
#define FM_AF_BANDWIDTH_HZ 15000   // FM dekódolt audio sávszélesség (Hz)
#define FM_AF_RAW_SAMPLES_SIZE 128 // FM módban a minták száma blokkonként

// AM audio sávszélesség (Sima Közép Hullámú vagy SW AM demoduláció esetén)
#define AM_AF_BANDWIDTH_HZ 6000     // AM dekódolt audio sávszélesség (Hz)
#define AM_AF_RAW_SAMPLES_SIZE 1024 // AM módban a minták száma blokkonként

// Dominant Frequency Dekóder paraméterek
// A mintavételezési frekvenciát a sávszélességből számoljuk (Nyquist + margin),
// ezért itt csak a nyers minták számát és a sávszélességet tartjuk meg.
#define DOMINANT_FREQ_AF_BANDWIDTH_HZ 15000 // Dominant Frequency audio sávszélesség
#define DOMINANT_FREQ_RAW_SAMPLES_SIZE 1024 // Dominant Frequency bemeneti audio minták száma blok

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
#define RTTY_AF_BANDWIDTH_HZ 15000 // RTTY audio sávszélesség (szabadon változtatható)
// RTTY bemeneti audio minták száma blok (lehet több, mint a belső blokk méret)
#define RTTY_RAW_SAMPLES_SIZE 1024 // A zoom miatt ilyen magas, hogy a waterfall is kinézzen valahyogyan.

// SSTV paraméterek
// Mintavételezési frekvencia a sávszélességből számítódik.
#define C_SSTV_DECODER_SAMPLE_RATE_HZ 15000 // A 'c_sstv_decoder' SSTV dekóder 'bevarrt' mintavételezési frekvenciája
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
