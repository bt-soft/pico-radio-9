/**
 * @file AudioController.cpp
 * @brief AudioController osztály implementációja a Core-0 számára
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */
#include <Arduino.h>

#include "AudioController.h"

//-------------------------------------------------------------------------------------

/**
 * @brief Beállítja a dekódert és a mintavételezési konfigurációt a Core 1-en.
 * @param id A dekóder azonosítója.
 * @param samplingRate A mintavételezési sebesség Hz-ben.
 * @param sampleCount A minták száma blokkonként.
 * @param bandwidthHz A sávszélesség Hz-ben.
 *
 */
void AudioController::start(DecoderId id, uint32_t sampleCount, uint32_t bandwidthHz, uint32_t cwCenterFreqHz, uint32_t rttyMarkFreqHz, uint32_t rttySpaceFreqHz, float rttyBaud) {

    // Küldjük a dekóder ID-t, a puffer méretet és a kívánt AF sávszélességet a Core1-nek.
    rp2040.fifo.push(RP2040CommandCode::CMD_SET_CONFIG);
    rp2040.fifo.push((uint32_t)id);
    rp2040.fifo.push(sampleCount);
    rp2040.fifo.push(bandwidthHz);

    // opcionális: CW cél frekvencia
    rp2040.fifo.push(cwCenterFreqHz);

    // opcionális: RTTY paraméterek
    rp2040.fifo.push(rttyMarkFreqHz);
    rp2040.fifo.push(rttySpaceFreqHz);
    uint32_t baudBits; // Float átalakítás FIFO-ra (uint32_t bit pattern)
    memcpy(&baudBits, &rttyBaud, sizeof(uint32_t));
    rp2040.fifo.push(baudBits);

    (void)rp2040.fifo.pop(); // ACK

    // Beállítjuk az aktív dekóder mutatót
    activeDecoderCore0 = id;
}

/**
 * @brief Leállítja a dekódert a Core 1-en.
 */
void AudioController::stop() {
    rp2040.fifo.push(RP2040CommandCode::CMD_STOP);
    (void)rp2040.fifo.pop(); // ACK

    // Alaphelyzetbe állítjuk az aktív dekóder mutatót
    activeDecoderCore0 = ID_DECODER_NONE;
}

/**
 * @brief Lekérdezi a mintavételezési sebességet a Core 1-től.
 * @return A mintavételezési sebesség Hz-ben.
 */
uint32_t AudioController::getSamplingRate() {

    rp2040.fifo.push(RP2040CommandCode::CMD_GET_SAMPLING_RATE);

    uint32_t response_code = rp2040.fifo.pop();
    if (response_code == RP2040ResponseCode::RESP_SAMPLING_RATE) {
        uint32_t samplingRate = rp2040.fifo.pop();
        return samplingRate;
    } else {
        // Hiba vagy timeout esetén ürítsük a FIFO-t, ha van benne valami
        while (rp2040.fifo.available()) {
            rp2040.fifo.pop();
        }
        return 0; // Hibás válasz esetén 0-t adunk vissza
    }
}

/**
 * @brief Lekérdezi a Core1 által használt aktív adatpuffer indexét.
 * @return Az aktív puffer indexe (0 vagy 1), vagy -1 hiba esetén.
 */
int8_t AudioController::getActiveSharedDataIndex() {

    rp2040.fifo.push(RP2040CommandCode::CMD_GET_DATA_BLOCK);

    uint32_t response_code = rp2040.fifo.pop();
    if (response_code == RP2040ResponseCode::RESP_DATA_BLOCK) {
        uint32_t activeSharedDataIndex = rp2040.fifo.pop();
        return static_cast<int8_t>(activeSharedDataIndex);
    } else {
        // Hiba vagy timeout esetén ürítsük a FIFO-t, ha van benne valami
        while (rp2040.fifo.available()) {
            rp2040.fifo.pop();
        }
        return -1;
    }
}