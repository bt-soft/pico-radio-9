#pragma once

#include <cstdint>

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
};

/**
 * @brief Parancskódok a core0 -> core1 kommunikációhoz
 */
enum CommandCode : uint32_t {
    CMD_NOP = 0,
    CMD_STOP = 1,
    CMD_SET_CONFIG = 2,
    CMD_GET_CONFIG = 3,
    CMD_PING = 4,
    CMD_GET_DATA_BLOCK = 5,   //  megosztott adatblokk indexének lekérésére
    CMD_GET_SAMPLING_RATE = 6 //  mintavételezési sebesség lekérésére
};

/**
 * @brief Válaszkódok a core1 -> core0 üzenetekre
 */
enum ResponseCode : uint32_t {
    RESP_NOP = 0,
    RESP_DOM_FREQ = 100, // Elavult
    RESP_ACK = 200,
    RESP_NACK = 201,
    RESP_ACTUAL_RATE = 202,
    RESP_CONFIG = 203,
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
