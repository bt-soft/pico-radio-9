/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: AudioController.cpp                                                                                           *
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
 * Last Modified: 2025.11.22, Saturday  10:01:21                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include <Arduino.h>

#include "AudioController.h"

//-------------------------------------------------------------------------------------

/**
 * @brief Indítja a Core1-en futó audio dekódert és elküldi a konfigurációt.
 * Ezt a hívást a Core0 oldalról kell használni, hogy a Core1 beállítsa a mintavételezést,
 * sávszélességet és a dekóder specifikus paramétereket.
 */
void AudioController::startAudioController(DecoderId id, uint32_t sampleCount, uint32_t bandwidthHz, uint32_t cwCenterFreqHz, uint32_t rttyMarkFreqHz,
                                           uint32_t rttySpaceFreqHz, float rttyBaud) {

    DEBUG("AudioController: startAudioController() hívás - dekóder Core0-on: %d, sampleCount=%d, bandwidthHz=%d Hz, cwCenterFreqHz=%d Hz, rttyMarkFreqHz=%d "
          "Hz, rttySpaceFreqHz=%d Hz, rttyBaud=%.2f\n",
          (uint32_t)id, sampleCount, bandwidthHz, cwCenterFreqHz, rttyMarkFreqHz, rttySpaceFreqHz, rttyBaud);

    // Küldjük a dekóder ID-t, a puffer méretet és a kívánt AF sávszélességet a Core1-nek.
    rp2040.fifo.push(RP2040CommandCode::CMD_SET_CONFIG);
    rp2040.fifo.push((uint32_t)id);
    rp2040.fifo.push(sampleCount);
    rp2040.fifo.push(bandwidthHz);

    // opcionális: CW cél frekvencia, de meg kell adni, ha vannak további RTTY paraméterek
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

    DEBUG("AudioController: startAudioController() hívás vége\n");
}

/**
 * @brief Leállítja a dekódert a Core 1-en.
 */
void AudioController::stopAudioController() {

    DEBUG("AudioController: stopAudioController() hívás - aktív dekóder Core0-on: %d\n", (uint32_t)activeDecoderCore0);

    rp2040.fifo.push(RP2040CommandCode::CMD_STOP);
    (void)rp2040.fifo.pop(); // ACK

    // KRITIKUS: Rövid várakozás a Core1-en történő DMA cleanup befejezéséhez
    // Ez biztosítja, hogy a DMA tényleg leáll, mielőtt új konfiguráció érkezik
    delay(20);

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
 * @brief AudioProcessorC1 AGC engedélyezése/letiltása Core1-en.
 */
bool AudioController::setAgcEnabled(bool enabled) {
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_SET_AGC_ENABLED);
    rp2040.fifo.push(enabled ? 1 : 0);
    return rp2040.fifo.pop() == RP2040ResponseCode::RESP_ACK; // ACK jött?
}

/**
 * @brief AudioProcessorC1 zajszűrés engedélyezése/letiltása Core1-en.
 */
bool AudioController::setNoiseReductionEnabled(bool enabled) {
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_SET_NOISE_REDUCTION_ENABLED);
    rp2040.fifo.push(enabled ? 1 : 0);
    return rp2040.fifo.pop() == RP2040ResponseCode::RESP_ACK; // ACK jött?
}

/**
 * @brief AudioProcessorC1 smoothing pontok számának beállítása Core1-en.
 */
bool AudioController::setSmoothingPoints(uint32_t points) {
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_SET_SMOOTHING_POINTS);
    rp2040.fifo.push(points);
    return rp2040.fifo.pop() == RP2040ResponseCode::RESP_ACK; // ACK jött?
}

/**
 * @brief AudioProcessorC1 FFT használatának engedélyezése/letiltása Core1-en.
 */
bool AudioController::setUseFftEnabled(bool enabled) {
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_SET_USE_FFT_ENABLED);
    rp2040.fifo.push(enabled ? 1 : 0);
    return rp2040.fifo.pop() == RP2040ResponseCode::RESP_ACK; // ACK jött?
}

/**
 * @brief Beállítja a spektrum nem-koherens átlagolásának keretszámát a Core1-en.
 * @param n Az átlagolandó keretek száma (1 = nincs átlagolás)
 */
bool AudioController::setSpectrumAveragingCount(uint32_t n) {
    if (n == 0) {
        n = 1;
        DEBUG("AudioController: setSpectrumAveragingCount() - n érték beállítva 1-re (nincs átlagolás)\n");
    }
    if (n > 8) {
        n = 8; // Maximum korlátozás
        DEBUG("AudioController: setSpectrumAveragingCount() - n érték korlátozva 8-ra\n");
    }
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_SET_SPECTRUM_AVERAGING_COUNT);
    rp2040.fifo.push(n);
    return rp2040.fifo.pop() == RP2040ResponseCode::RESP_ACK;
}

/**
 * @brief Engedélyezi vagy tiltja a dekóder oldali bandpass szűrőt a Core1-en.
 * @param enabled true = engedélyez, false = tiltás
 */
bool AudioController::setDecoderBandpassEnabled(bool enabled) {
    rp2040.fifo.push(RP2040CommandCode::CMD_DECODER_SET_BANDPASS_ENABLED);
    rp2040.fifo.push(enabled ? 1 : 0);
    return rp2040.fifo.pop() == RP2040ResponseCode::RESP_ACK;
}

/**
 * @brief Lekérdezi, hogy az AudioProcessorC1 használja-e az FFT-t Core1-en.
 * @return true, ha használja, false, ha nem, vagy hiba esetén false.
 */
bool AudioController::getUseFftEnabled() {
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_GET_USE_FFT_ENABLED);
    uint32_t response_code = rp2040.fifo.pop();
    if (response_code == RP2040ResponseCode::RESP_USE_FFT_ENABLED) {
        uint32_t enabled = rp2040.fifo.pop();
        return enabled != 0;
    } else {
        // Hiba vagy timeout esetén ürítsük a FIFO-t, ha van benne valami
        while (rp2040.fifo.available()) {
            rp2040.fifo.pop();
        }
        return false;
    }
}
void AudioController::setManualGain(float gain) {
    // Float átküldése FIFO-n uint32_t bit patternként
    uint32_t gainBits;
    memcpy(&gainBits, &gain, sizeof(uint32_t));
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_SET_MANUAL_GAIN);
    rp2040.fifo.push(gainBits);
    (void)rp2040.fifo.pop(); // ACK
}

/**
 * @brief Beállítja a blokkoló/nem blokkoló DMA módot Core1-en.
 */
bool AudioController::setBlockingDmaMode(bool blocking) {
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_SET_BLOCKING_DMA_MODE);
    rp2040.fifo.push(blocking ? 1 : 0);
    uint32_t resp = rp2040.fifo.pop();
    return resp == RP2040ResponseCode::RESP_ACK;
}

/**
 * @brief Beállítja, hogy az aktív dekóder adaptív küszöböt használjon-e (Core1 oldalon).
 */
bool AudioController::setDecoderUseAdaptiveThreshold(bool use) {
    rp2040.fifo.push(RP2040CommandCode::CMD_DECODER_SET_USE_ADAPTIVE_THRESHOLD);
    rp2040.fifo.push(use ? 1 : 0);
    return rp2040.fifo.pop() == RP2040ResponseCode::RESP_ACK;
}

/**
 * @brief Lekérdezi, hogy az aktív dekóder adaptív küszöb használata be van-e kapcsolva a Core1-en.
 */
bool AudioController::getDecoderUseAdaptiveThreshold() {
    rp2040.fifo.push(RP2040CommandCode::CMD_DECODER_GET_USE_ADAPTIVE_THRESHOLD);
    uint32_t response_code = rp2040.fifo.pop();
    if (response_code == RP2040ResponseCode::RESP_USE_ADAPTIVE_THRESHOLD) {
        uint32_t enabled = rp2040.fifo.pop();
        return enabled != 0;
    } else {
        while (rp2040.fifo.available()) {
            rp2040.fifo.pop();
        }
        return false;
    }
}

/**
 * @brief Az aktív dekóder resetelése a Core1-en.
 * @details A Core1 oldalán a CMD_DECODER_RESET parancsot fogja feldolgozni,
 * és ACK-et küld vissza.
 */
void AudioController::resetDecoder() {
    rp2040.fifo.push(RP2040CommandCode::CMD_DECODER_RESET);
    (void)rp2040.fifo.pop(); // ACK várás
}

/**
 * @brief Inicializációs lánc: kérjük meg a Core1-et, hogy kalibrálja az ADC DC középpontját.
 * Ezt a hívást a hardveres audio némítás alatt kell végrehajtani, hogy elkerüljük a hallható zajt.
 */
void AudioController::init() {
    DEBUG("AudioController: init() - DC kalibráció kérése a Core1 felé\n");
    rp2040.fifo.push(RP2040CommandCode::CMD_AUDIOPROC_CALIBRATE_DC);
    uint32_t resp = rp2040.fifo.pop();
    if (resp == RP2040ResponseCode::RESP_ACK) {
        DEBUG("AudioController: init() - DC kalibráció ACK érkezett\n");
    } else {
        DEBUG("AudioController: init() - DC kalibráció NACK vagy nincs válasz (kód=%u)\n", resp);
    }
}
