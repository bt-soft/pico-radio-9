/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: DecoderWeFax-c1.h                                                                                             *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                       *
 * -----                                                                                                               *
 * Last Modified: 2025.11.22, Saturday  07:31:44                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "IDecoder.h"
#include "defines.h"

/**
 * @brief WEFAX dekóder osztály - Core1 számára
 */
class DecoderWeFax_C1 : public IDecoder {
  public:
    DecoderWeFax_C1();
    ~DecoderWeFax_C1() = default;

    /**
     * @brief Dekóder neve
     */
    const char *getDecoderName() const override { return "WEFAX-FM"; };

    /**
     * @brief Indítás / inicializáció
     */
    bool start(const DecoderConfig &decoderConfig) override;

    /**
     * @brief Leállítás és takarítás
     */
    void stop() override;

    /**
     * @brief Dekóder resetelése
     */
    void reset() override;

    /**
     * @brief Nyers audio minták feldolgozása - TELJES WEFAX dekódolás Goertzel-lel
     *
     * Ez az EGYETLEN belépési pont a WEFAX dekóderhez. Minden állapotban (start tone,
     * phasing, image decoding)
     *
     * IDecoder::processSamples() interfész implementációja.
     *
     * @param samples Pointer a nyers audio mintákhoz (DC-centrált int16_t)
     * @param count Minták száma
     */
    void processSamples(const int16_t *samples, size_t count) override;

    /**
     * @brief Visszaadja az aktuális (legutóbb írt) sor indexét
     */
    inline uint16_t getCurrentLineIndex() const { return current_line_index; }

  private:
    // WEFAX mód típusok
    enum class WefaxMode {
        IOC576 = 0, // IOC 576 (1809 pixel szélesség, 25ms leading white)
        IOC288 = 1  // IOC 288 (909 pixel szélesség, 25ms leading white)
    };

    // Visszaadja a mód nevét
    const char *getModeName(WefaxMode mode) const;

    void decode_phasing(int gray_value);
    void decode_image(int gray_value, uint16_t *current_line_idx);

    float complex_arg_diff(float prev_real, float prev_imag, float curr_real, float curr_imag);

    // FM demodulátor állapot
#define IQ_FILTER_SIZE 32 // I/Q szűrő mérete (egyszerűsített mozgóátlag) - növelve a zajos pontok csökkentésére
    float phase_accumulator = 0.0f;
    float phase_increment = 0.0f;
    float deviation_ratio = 0.0f;
    float i_buffer[IQ_FILTER_SIZE] = {0};
    float q_buffer[IQ_FILTER_SIZE] = {0};
    int iq_buffer_index = 0;
    float prevz_real = 0.0f;
    float prevz_imag = 0.0f;

    // Phasing detektálás állapot
#define PHASING_FILTER_SIZE 32 // Phasing detektálás szűrője - növelve a stabilabb szinkronhoz
    enum RxState { IDLE = 0, RXPHASING, RXIMAGE };
    RxState rx_state = IDLE;
    int phasing_count = 0;
    int phasing_history[PHASING_FILTER_SIZE] = {0};
    bool phase_high = false;
    int curr_phase_len = 0;
    int curr_phase_high = 0;
    int curr_phase_low = 0;
    int phase_lines = 0;
    float lpm_sum = 0.0f;
    float samples_per_line = 0.0f;
    float sample_rate = 0.0f;

    // Kép fogadás állapot
    int img_sample = 0;
    int last_col = 0;
    int img_width = WEFAX_IOC576_WIDTH;
    uint32_t current_ioc = 576;

    uint16_t current_line_index = 0; // A sor, ahova éppen írunk (0-249)
    uint8_t current_wefax_line[WEFAX_MAX_OUTPUT_WIDTH];
    bool line_started = false;
    int pixel_val = 0;
    int pix_samples_nb = 0;
};
