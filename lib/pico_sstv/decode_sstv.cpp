#include <algorithm>
#include <cmath>
#include <cstdint>

#include "cordic.h"
#include "decode_sstv.h"

// A sample_to_pixel metódus a mintaszám alapján meghatározza a színt és az x/y koordinátákat.
// A Martin és Scottie módokban a szín sorrend g-b-r, amelyet r-g-b sorrendbe kell leképezni.
// A vízszintes szinkron (hsync) kezelésére egy lambda függvényt használunk.

// A frequency_to_brightness metódus a frekvenciát fényerővé alakítja.
// A fényerő értéke 0 és 255 között van, a bemeneti frekvencia alapján számítva.

// A parity_check metódus ellenőrzi egy bájt paritását.
// A paritásellenőrzés XOR műveletekkel történik.

// A c_sstv_decoder konstruktor inicializálja az osztály tagjait.
// A különböző SSTV módokhoz tartozó paramétereket is beállítja, például a minták számát soronként.

const char *c_sstv_decoder::sstvModeNames[] = {"Martin M1", "Martin M2", "Scottie S1", "Scottie S2", "Scottie DX", "PD 50",    "PD 90", "PD 120", "PD 180",
                                               "SC2 60",    "SC2 120",   "SC2 180",    "Robot 24",   "Robot 36",   "Robot 72", "BW 8",  "BW 12"};

/**
 * @brief Visszaállítja a dekóder állapotgépét az alapállapotba.
 */
void c_sstv_decoder::reset() {
    state = detect_sync;
    sync_state = detect;
    sync_counter = 0;
    y_pixel = 0;
    last_x = 0;
    image_sample = 0;
    last_sample = 0;
    last_hsync_sample = 0;
    sample_number = 0;
    confirmed_sync_sample = 0;
    confirm_count = 0;
    pixel_accumulator = 0;
    pixel_n = 0;
    last_phase = 0;
    ssb_phase = 0;
}

/**
 * @brief c_sstv_decoder konstruktor
 * @param Fs Mintavételi frekvencia
 */
c_sstv_decoder::c_sstv_decoder(float Fs) {

    m_Fs = Fs;
    static const uint32_t scale = 1 << SstvConstants::FRACTION_BITS;
    m_scale = scale;

    m_auto_slant_correction = true;
    m_timeout = m_Fs * 30;
    m_martin_robot_offset = m_scale * m_Fs * 1.25 / 1000.0;

    // martin m1
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 4.862;
        const float colour_gap_ms = 0.572;
        const float colour_time_ms = 146.342;
        modes[martin_m1].name = sstvModeNames[martin_m1];
        modes[martin_m1].width = width;
        modes[martin_m1].samples_per_line = scale * Fs * ((colour_time_ms * 3) + (colour_gap_ms * 4) + hsync_pulse_ms) / 1000.0;
        modes[martin_m1].samples_per_colour_line = scale * Fs * (colour_time_ms + colour_gap_ms) / 1000.0;
        modes[martin_m1].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[martin_m1].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[martin_m1].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[martin_m1].max_height = 256;
    }

    // martin m2
    {
        const uint16_t width = 160;
        const float hsync_pulse_ms = 4.862;
        const float colour_gap_ms = 0.572;
        const float colour_time_ms = 73.216;
        modes[martin_m2].name = sstvModeNames[martin_m2];
        modes[martin_m2].width = width;
        modes[martin_m2].samples_per_line = scale * Fs * ((colour_time_ms * 3) + (colour_gap_ms * 4) + hsync_pulse_ms) / 1000.0;
        modes[martin_m2].samples_per_colour_line = scale * Fs * (colour_time_ms + colour_gap_ms) / 1000.0;
        modes[martin_m2].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[martin_m2].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[martin_m2].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[martin_m2].max_height = 256;
    }

    // scottie s1
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 9;
        const float colour_gap_ms = 1.5;
        const float colour_time_ms = 138.240;
        modes[scottie_s1].name = sstvModeNames[scottie_s1];
        modes[scottie_s1].width = width;
        modes[scottie_s1].samples_per_line = scale * Fs * ((colour_time_ms * 3) + (colour_gap_ms * 3) + hsync_pulse_ms) / 1000.0;
        modes[scottie_s1].samples_per_colour_line = scale * Fs * (colour_time_ms + colour_gap_ms) / 1000.0;
        modes[scottie_s1].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[scottie_s1].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[scottie_s1].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[scottie_s1].max_height = 256;
    }

    // scottie s2
    {
        const uint16_t width = 160;
        const float hsync_pulse_ms = 9;
        const float colour_gap_ms = 1.5;
        const float colour_time_ms = 88.064;
        modes[scottie_s2].name = sstvModeNames[scottie_s2];
        modes[scottie_s2].width = width;
        modes[scottie_s2].samples_per_line = scale * Fs * ((colour_time_ms * 3) + (colour_gap_ms * 3) + hsync_pulse_ms) / 1000.0;
        modes[scottie_s2].samples_per_colour_line = scale * Fs * (colour_time_ms + colour_gap_ms) / 1000.0;
        modes[scottie_s2].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[scottie_s2].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[scottie_s2].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[scottie_s2].max_height = 256;
    }

    // scottie dx
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 9;
        const float colour_gap_ms = 1.5;
        const float colour_time_ms = 345.600;
        modes[scottie_dx].name = sstvModeNames[scottie_dx];
        modes[scottie_dx].width = width;
        modes[scottie_dx].samples_per_line = scale * Fs * ((colour_time_ms * 3) + (colour_gap_ms * 3) + hsync_pulse_ms) / 1000.0;
        modes[scottie_dx].samples_per_colour_line = scale * Fs * (colour_time_ms + colour_gap_ms) / 1000.0;
        modes[scottie_dx].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[scottie_dx].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[scottie_dx].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[scottie_dx].max_height = 256;
    }

    // pd 50
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 20;
        const float colour_gap_ms = 2.08;
        const float colour_time_ms = 91.520;
        modes[pd_50].name = sstvModeNames[pd_50];
        modes[pd_50].width = width;
        modes[pd_50].samples_per_line = scale * Fs * ((colour_time_ms * 4) + (colour_gap_ms * 1) + hsync_pulse_ms) / 1000.0;
        modes[pd_50].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[pd_50].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[pd_50].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[pd_50].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[pd_50].max_height = 128;
    }

    // pd 90
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 20;
        const float colour_gap_ms = 2.08;
        const float colour_time_ms = 170.240;
        modes[pd_90].name = sstvModeNames[pd_90];
        modes[pd_90].width = width;
        modes[pd_90].samples_per_line = scale * Fs * ((colour_time_ms * 4) + (colour_gap_ms * 1) + hsync_pulse_ms) / 1000.0;
        modes[pd_90].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[pd_90].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[pd_90].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[pd_90].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[pd_90].max_height = 128;
    }

    // pd 120
    {
        const uint16_t width = 320; // 320 használata 640 helyett egyszerűbb skálázáshoz
        const float hsync_pulse_ms = 20;
        const float colour_gap_ms = 2.08;
        const float colour_time_ms = 121.600;
        modes[pd_120].name = sstvModeNames[pd_120];
        modes[pd_120].width = width;
        modes[pd_120].samples_per_line = scale * Fs * ((colour_time_ms * 4) + (colour_gap_ms * 1) + hsync_pulse_ms) / 1000.0;
        modes[pd_120].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[pd_120].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[pd_120].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[pd_120].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[pd_120].max_height = 248;
    }

    // pd 180
    {
        const uint16_t width = 320; // 320 használata 640 helyett egyszerűbb skálázáshoz
        const float hsync_pulse_ms = 20;
        const float colour_gap_ms = 2.08;
        const float colour_time_ms = 183.040;
        modes[pd_180].name = sstvModeNames[pd_180];
        modes[pd_180].width = width;
        modes[pd_180].samples_per_line = scale * Fs * ((colour_time_ms * 4) + (colour_gap_ms * 1) + hsync_pulse_ms) / 1000.0;
        modes[pd_180].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[pd_180].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[pd_180].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[pd_180].samples_per_hsync = scale * Fs * hsync_pulse_ms / 1000.0;
        modes[pd_180].max_height = 248;
    }

    // SC260
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 5;
        const float colour_gap_ms = 0;
        const float colour_time_ms = 78.468;
        modes[sc2_60].name = sstvModeNames[sc2_60];
        modes[sc2_60].width = width;
        modes[sc2_60].samples_per_line = scale * Fs * ((colour_time_ms * 3) + hsync_pulse_ms) / 1000.0;
        modes[sc2_60].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[sc2_60].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[sc2_60].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[sc2_60].samples_per_hsync = m_scale * Fs * hsync_pulse_ms / 1000.0;
        modes[sc2_60].max_height = 256;
    }

    // SC2120
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 5;
        const float colour_gap_ms = 0;
        const float colour_time_ms = 156.852;
        modes[sc2_120].name = sstvModeNames[sc2_120];
        modes[sc2_120].width = width;
        modes[sc2_120].samples_per_line = scale * Fs * ((colour_time_ms * 3) + hsync_pulse_ms) / 1000.0;
        modes[sc2_120].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[sc2_120].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[sc2_120].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[sc2_120].samples_per_hsync = m_scale * Fs * hsync_pulse_ms / 1000.0;
        modes[sc2_120].max_height = 256;
    }

    // SC2180
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 5;
        const float colour_gap_ms = 0;
        const float colour_time_ms = 235.362;
        modes[sc2_180].name = sstvModeNames[sc2_180];
        modes[sc2_180].width = width;
        modes[sc2_180].samples_per_line = scale * Fs * ((colour_time_ms * 3) + hsync_pulse_ms) / 1000.0;
        modes[sc2_180].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[sc2_180].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[sc2_180].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[sc2_180].samples_per_hsync = m_scale * Fs * hsync_pulse_ms / 1000.0;
        modes[sc2_180].max_height = 256;
    }

    // Robot24
    {
        const uint16_t width = 160;
        const float hsync_pulse_ms = 4;
        const float colour_gap_ms = 1.5;
        const float colour_time_ms = 46;
        modes[robot24].name = sstvModeNames[robot24];
        modes[robot24].width = width;
        modes[robot24].samples_per_line = scale * Fs * ((colour_time_ms + hsync_pulse_ms) * 4) / 1000.0;
        modes[robot24].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[robot24].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[robot24].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[robot24].samples_per_hsync = m_scale * Fs * hsync_pulse_ms / 1000.0;
        modes[robot24].max_height = 120;
    }

    // Robot36 tot:150
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 9;
        const float colour_gap_ms = 6;
        const float colour_time_ms = 44;
        modes[robot36].name = sstvModeNames[robot36];
        modes[robot36].width = width;
        modes[robot36].samples_per_line = scale * Fs * ((colour_time_ms * 3) + (colour_gap_ms * 1.5) + hsync_pulse_ms) / 1000.0;
        modes[robot36].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[robot36].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[robot36].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[robot36].samples_per_hsync = m_scale * Fs * hsync_pulse_ms / 1000.0;
        modes[robot36].max_height = 240;
    }

    // Robot72
    {
        const uint16_t width = 320;
        const float hsync_pulse_ms = 6;
        const float colour_gap_ms = 1.5;
        const float colour_time_ms = 69;
        modes[robot72].name = sstvModeNames[robot72];
        modes[robot72].width = width;
        modes[robot72].samples_per_line = scale * Fs * ((colour_time_ms + hsync_pulse_ms) * 4) / 1000.0;
        modes[robot72].samples_per_colour_line = scale * Fs * (colour_time_ms) / 1000.0;
        modes[robot72].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[robot72].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[robot72].samples_per_hsync = m_scale * Fs * hsync_pulse_ms / 1000.0;
        modes[robot72].max_height = 240;
    }

    // bw8
    {
        const uint16_t width = 160;
        const float hsync_pulse_ms = 10;
        const float colour_gap_ms = 0;
        const float colour_time_ms = 57;
        modes[bw8].name = sstvModeNames[bw8];
        modes[bw8].width = width;
        modes[bw8].samples_per_line = scale * Fs * (colour_time_ms + hsync_pulse_ms) / 1000.0;
        modes[bw8].samples_per_colour_line = scale * Fs * colour_time_ms / 1000.0;
        modes[bw8].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[bw8].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[bw8].samples_per_hsync = m_scale * Fs * hsync_pulse_ms / 1000.0;
        modes[bw8].max_height = 120;
    }

    // bw12
    {
        const uint16_t width = 160;
        const float hsync_pulse_ms = 7;
        const float colour_gap_ms = 0;
        const float colour_time_ms = 93;
        modes[bw12].name = sstvModeNames[bw12];
        modes[bw12].width = width;
        modes[bw12].samples_per_line = scale * Fs * (colour_time_ms + hsync_pulse_ms) / 1000.0;
        modes[bw12].samples_per_colour_line = scale * Fs * colour_time_ms / 1000.0;
        modes[bw12].samples_per_colour_gap = scale * Fs * colour_gap_ms / 1000.0;
        modes[bw12].samples_per_pixel = scale * Fs * colour_time_ms / (1000.0 * width);
        modes[bw12].samples_per_hsync = m_scale * Fs * hsync_pulse_ms / 1000.0;
        modes[bw12].max_height = 120;
    }

    cordic_init();
}

/**
 * @brief Audió jel dekódolása pixel adatokra
 * @param audio Bemeneti audió minta
 * @param pixel_y Kimeneti pixel y koordináta
 * @param pixel_x Kimeneti pixel x koordináta
 * @param pixel_colour Kimeneti pixel szín
 * @param pixel Kimeneti pixel érték
 * @param frequency Kimeneti frekvencia
 * @return Igaz, ha egy pixel sikeresen dekódolva lett, hamis egyébként
 */
bool c_sstv_decoder::decode_audio(int16_t audio, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, int16_t &frequency) {
    // shift frequency by +FS/4
    //       __|__
    //   ___/  |  \___
    //         |
    //   <-----+----->

    //        | ____
    //  ______|/    \
    //        |
    //  <-----+----->

    // filter -Fs/4 to +Fs/4

    //        | __
    //  ______|/  \__
    //        |
    //  <-----+----->

    ssb_phase = (ssb_phase + 1) & 3u;
    audio = audio >> 1;

    const int16_t audio_i[4] = {audio, 0, (int16_t)-audio, 0};
    const int16_t audio_q[4] = {0, (int16_t)-audio, 0, audio};
    int16_t ii = audio_i[ssb_phase];
    int16_t qq = audio_q[ssb_phase];
    ssb_filter.filter(ii, qq); // half band bandpass filter Fs/4 +/- 7.5kHz

    // shift frequency by -FS/4
    //         | __
    //   ______|/  \__
    //         |
    //   <-----+----->

    //     __ |
    //  __/  \|______
    //        |
    //  <-----+----->

    const int16_t sample_i[4] = {(int16_t)-qq, (int16_t)-ii, qq, ii};
    const int16_t sample_q[4] = {ii, (int16_t)-qq, (int16_t)-ii, qq};
    int16_t i = sample_i[ssb_phase];
    int16_t q = sample_q[ssb_phase];

    return decode_iq(i, q, pixel_y, pixel_x, pixel_colour, pixel, frequency);
}

/**
 * @brief IQ minták dekódolása pixel adatokra
 * @param sample_i Bemeneti I minta
 * @param sample_q Bemeneti Q minta
 * @param pixel_y Kimeneti pixel y koordináta
 * @param pixel_x Kimeneti pixel x koordináta
 * @param pixel_colour Kimeneti pixel szín
 * @param pixel Kimeneti pixel érték
 * @param smoothed_sample_16 Kimeneti simított minta
 * @return Igaz, ha egy pixel sikeresen dekódolva lett, hamis egyébként
 */
bool c_sstv_decoder::decode_iq(int16_t sample_i, int16_t sample_q, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, int16_t &smoothed_sample_16) {

    uint16_t magnitude;
    int16_t phase;

    cordic_rectangular_to_polar(sample_i, sample_q, magnitude, phase);
    frequency = last_phase - phase;
    last_phase = phase;

    int16_t sample = (int32_t)frequency * 15000 >> 16;

    m_smoothed_sample = ((m_smoothed_sample << 3) + sample - m_smoothed_sample) >> 3;
    smoothed_sample_16 = std::min(std::max(m_smoothed_sample, (uint32_t)1000u), (uint32_t)2500u);

    e_state debug_state;
    return decode(smoothed_sample_16, pixel_y, pixel_x, pixel_colour, pixel, debug_state);
}

/**
 * @brief Audió minta dekódolása pixel adatokra
 * @param sample Bemeneti audió minta
 * @param pixel_y Kimeneti pixel y koordináta
 * @param pixel_x Kimeneti pixel x koordináta
 * @param pixel_colour Kimeneti pixel szín
 * @param pixel Kimeneti pixel érték
 * @param debug_state Kimeneti állapot (hibakereséshez)
 * @return Igaz, ha egy pixel sikeresen dekódolva lett, hamis egyébként
 */
bool c_sstv_decoder::decode(uint16_t sample, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, e_state &debug_state) {

    // detect scan syncs
    bool sync_found = false;
    uint32_t line_length = 0u;
    if (sync_state == detect) {
        if (sample < SstvConstants::SYNC_FREQ_THRESHOLD_HZ && last_sample >= SstvConstants::SYNC_FREQ_THRESHOLD_HZ) {
            sync_state = confirm;
            sync_counter = 0;
        }
    } else if (sync_state == confirm) {
        if (sample < SstvConstants::SYNC_FREQ_THRESHOLD_HZ) {
            sync_counter++;
        } else if (sync_counter > 0) {
            sync_counter--;
        }

        if (sync_counter == SstvConstants::SYNC_CONFIRM_SAMPLES) {
            sync_found = true;
            line_length = sample_number - last_hsync_sample;
            last_hsync_sample = sample_number;
            sync_state = detect;
        }
    }

    bool pixel_complete = false;
    switch (state) {

        case detect_sync:

            if (sync_found) {
                uint32_t least_error = UINT32_MAX;
                for (uint8_t mode = 0; mode < NUMBER_OFF_SSTV_MODES; ++mode) {

                    if (line_length > ((100 - SstvConstants::SLANT_CORRECTION_TOLERANCE_PERCENT) * modes[mode].samples_per_line) / (100 * m_scale) and
                        line_length < ((100 + SstvConstants::SLANT_CORRECTION_TOLERANCE_PERCENT) * modes[mode].samples_per_line) / (100 * m_scale)) {
                        uint32_t error = abs((int32_t)(line_length) - (int32_t)(modes[mode].samples_per_line / m_scale));
                        if (error < least_error) {
                            decode_mode = (e_mode)mode;
                            least_error = error;
                            mean_samples_per_line = modes[mode].samples_per_line;
                        }
                        confirm_count = 0;
                        state = confirm_sync;
                    }
                }
            }

            break;

        case confirm_sync:

            if (sync_found) {
                if (line_length > ((100 - SstvConstants::SLANT_CORRECTION_TOLERANCE_PERCENT) * modes[decode_mode].samples_per_line) / (100 * m_scale) and
                    line_length < ((100 + SstvConstants::SLANT_CORRECTION_TOLERANCE_PERCENT) * modes[decode_mode].samples_per_line) / (100 * m_scale)) {
                    state = decode_line;
                    confirmed_sync_sample = sample_number;
                    pixel_accumulator = 0;
                    pixel_n = 0;
                    last_x = 0;
                    image_sample = 0;
                    sync_timeout = m_timeout;

                    // Új kép kezdete: tisztítsuk a fázis/szűrő és simítási állapotot,
                    // különösen fontos, ha a következő kép más módban érkezik.
                    last_phase = 0;
                    ssb_phase = 0;
                    // újrainicializáljuk a half-band szűrőt, hogy ne maradjon benne
                    // előző képből származó bufferadat
                    ssb_filter = half_band_filter2();
                    // reseteljük a simított sample állapotot és hivatkozó mintavételi értékeket
                    m_smoothed_sample = 0;
                    last_sample = 0;
                    sync_state = detect;
                    // újraállítjuk a mean_samples_per_line az aktuális módhoz
                    mean_samples_per_line = modes[decode_mode].samples_per_line;
                } else {
                    confirm_count++;
                    if (confirm_count == SstvConstants::CONFIRM_RETRIES) {
                        state = detect_sync;
                    }
                }
            }

            break;

        case decode_line: {

            uint16_t x, y;
            uint8_t colour;
            sample_to_pixel(x, y, colour, image_sample);

            if (x != last_x && colour < 4 && pixel_n) {
                // output pixel
                pixel_complete = true;
                pixel = pixel_accumulator / pixel_n;
                pixel_y = y;
                pixel_x = last_x;
                pixel_colour = colour;

                // reset accumulator for next pixel
                pixel_accumulator = 0;
                pixel_n = 0;
                last_x = x;
            }

            // end of image
            if (y == 256 || y == modes[decode_mode].max_height) {
                state = detect_sync;
                sync_counter = 0;
                break;
            }

            // Auto Slant Correction
            if (sync_found) {
                // confirm sync if close to expected time
                if (line_length > ((100 - SstvConstants::SLANT_CORRECTION_TOLERANCE_PERCENT) * modes[decode_mode].samples_per_line) / (100 * m_scale) &&
                    line_length < ((100 + SstvConstants::SLANT_CORRECTION_TOLERANCE_PERCENT) * modes[decode_mode].samples_per_line) / (100 * m_scale)) {
                    sync_timeout = m_timeout; // reset timeout on each good sync pulse
                    const uint32_t samples_since_confirmed = sample_number - confirmed_sync_sample;
                    const uint32_t divisor = modes[decode_mode].samples_per_line;
                    const uint16_t num_lines = (1ULL * m_scale * samples_since_confirmed + (divisor / 2)) / divisor;
                    if (m_auto_slant_correction && num_lines > 0) {
                        mean_samples_per_line = mean_samples_per_line - (mean_samples_per_line >> 2) + ((1ULL * m_scale * samples_since_confirmed / num_lines) >> 2);
                    }
                }
            }

            // if no hsync seen, go back to idle
            else {
                sync_timeout--;
                if (!sync_timeout) {
                    state = detect_sync;
                    sync_counter = 0;
                    break;
                }
            }

            // colour pixels
            pixel_accumulator += frequency_to_brightness(sample);
            pixel_n++;
            image_sample += m_scale;

            break;
        }
    }

    sample_number++;
    last_sample = sample;
    debug_state = state;
    return pixel_complete;
}

/**
 * @brief Mintaszám alapján meghatározza a pixel koordinátáit és színét
 * @param x Kimeneti pixel x koordináta
 * @param y Kimeneti pixel y koordináta
 * @param colour Kimeneti pixel szín
 * @param image_sample Bemeneti mintaszám
 */
void c_sstv_decoder::sample_to_pixel(uint16_t &x, uint16_t &y, uint8_t &colour, int32_t image_sample) {
    static const uint8_t colourmap[4] = {1, 2, 0, 4};

    auto handle_hsync = [&]() {
        if (image_sample < 0) {
            x = 0;
            y = 0;
            colour = 4;
            return true;
        }
        return false;
    };

    if (decode_mode == martin_m1 || decode_mode == martin_m2) {
        image_sample += m_martin_robot_offset;
        image_sample -= modes[decode_mode].samples_per_hsync;
        if (handle_hsync())
            return;

        y = image_sample / mean_samples_per_line;
        image_sample -= y * mean_samples_per_line;
        colour = image_sample / modes[decode_mode].samples_per_colour_line;
        image_sample -= colour * modes[decode_mode].samples_per_colour_line;
        colour = colourmap[colour];
        x = image_sample / modes[decode_mode].samples_per_pixel;
    } else if (decode_mode == robot36) {
        image_sample -= modes[decode_mode].samples_per_hsync;
        if (handle_hsync())
            return;

        y = image_sample / mean_samples_per_line;
        image_sample -= y * mean_samples_per_line;

        if (image_sample < modes[decode_mode].samples_per_colour_line * 2) {
            colour = 0;
            x = image_sample / (modes[decode_mode].samples_per_pixel * 2);
        } else if (image_sample < modes[decode_mode].samples_per_colour_line * 2 + modes[robot36].samples_per_colour_gap) {
            colour = 3;
            x = (image_sample - modes[decode_mode].samples_per_colour_line * 2) / modes[decode_mode].samples_per_pixel;
        } else {
            colour = 1 + (y % 2);
            image_sample -= modes[decode_mode].samples_per_colour_line * 2 + modes[robot36].samples_per_colour_gap;
            x = image_sample / modes[decode_mode].samples_per_pixel;
        }
    } else if (decode_mode == robot24 || decode_mode == robot72) {
        image_sample += m_martin_robot_offset;
        y = image_sample / mean_samples_per_line;
        image_sample -= y * mean_samples_per_line;

        uint32_t samples_per_colour = modes[decode_mode].samples_per_colour_line + modes[decode_mode].samples_per_hsync;

        if (image_sample < 2 * samples_per_colour) {
            colour = 0;
            image_sample -= 2 * modes[decode_mode].samples_per_hsync;
            x = image_sample / (2 * modes[decode_mode].samples_per_pixel);
        } else if (image_sample < 3 * samples_per_colour) {
            colour = 1;
            image_sample -= modes[decode_mode].samples_per_hsync;
            image_sample -= 2 * samples_per_colour;
            x = image_sample / (modes[decode_mode].samples_per_pixel);
        } else if (image_sample < 4 * samples_per_colour) {
            colour = 2;
            image_sample -= modes[decode_mode].samples_per_hsync;
            image_sample -= 3 * samples_per_colour;
            x = image_sample / (modes[decode_mode].samples_per_pixel);
        } else {
            colour = 4;
            x = 0;
        }

        if (image_sample < 0) {
            x = 0;
            y = 0;
            colour = 4;
            return;
        }

    } else if (decode_mode == bw8 || decode_mode == bw12) {
        y = image_sample / mean_samples_per_line;
        image_sample -= y * mean_samples_per_line;

        uint32_t samples_per_colour = modes[decode_mode].samples_per_colour_line + modes[decode_mode].samples_per_hsync;

        if (image_sample < samples_per_colour) {
            colour = 0;
            image_sample -= modes[decode_mode].samples_per_hsync;
            x = image_sample / modes[decode_mode].samples_per_pixel;
        } else {
            colour = 4;
            x = 0;
        }

        if (image_sample < 0) {
            x = 0;
            y = 0;
            colour = 4;
            return;
        }

    } else if (decode_mode == scottie_s1 || decode_mode == scottie_s2 || decode_mode == scottie_dx) {

        image_sample -= modes[decode_mode].samples_per_colour_line;
        image_sample -= modes[decode_mode].samples_per_hsync;
        if (handle_hsync())
            return;

        y = image_sample / mean_samples_per_line;
        image_sample -= y * mean_samples_per_line;

        if (image_sample < 2 * modes[decode_mode].samples_per_colour_line) {
            colour = image_sample / modes[decode_mode].samples_per_colour_line;
            image_sample -= colour * modes[decode_mode].samples_per_colour_line;
        } else {
            image_sample -= 2 * modes[decode_mode].samples_per_colour_line;
            image_sample -= modes[decode_mode].samples_per_hsync;
            colour = 2 + (image_sample / modes[decode_mode].samples_per_colour_line);
        }
        if (image_sample < 0) {
            x = 0;
            y = 0;
            colour = 4;
            return;
        }

        colour = colourmap[colour];
        x = image_sample / modes[decode_mode].samples_per_pixel;

    } else if (decode_mode == pd_50 || decode_mode == pd_90 || decode_mode == pd_120 || decode_mode == pd_180) {
        static const uint8_t colourmap[5] = {0, 1, 2, 3, 4};

        image_sample -= modes[decode_mode].samples_per_hsync;
        if (handle_hsync())
            return;
        y = image_sample / mean_samples_per_line;
        image_sample -= y * mean_samples_per_line;
        colour = image_sample / modes[decode_mode].samples_per_colour_line;
        image_sample -= colour * modes[decode_mode].samples_per_colour_line;
        colour = colourmap[colour];
        x = image_sample / modes[decode_mode].samples_per_pixel;
    } else if (decode_mode == sc2_60 || decode_mode == sc2_120 || decode_mode == sc2_180) {

        image_sample += m_martin_robot_offset;

        image_sample -= modes[decode_mode].samples_per_hsync;
        if (handle_hsync())
            return;

        y = image_sample / mean_samples_per_line;
        image_sample -= y * mean_samples_per_line;

        if (image_sample < modes[decode_mode].samples_per_colour_line) {
            colour = 0;
            x = image_sample / modes[decode_mode].samples_per_pixel;
        } else if (image_sample < 2 * modes[decode_mode].samples_per_colour_line) {
            colour = 1;
            image_sample -= modes[decode_mode].samples_per_colour_line;
            x = image_sample / modes[decode_mode].samples_per_pixel;
        } else if (image_sample < 3 * modes[decode_mode].samples_per_colour_line) {
            colour = 2;
            image_sample -= 2 * modes[decode_mode].samples_per_colour_line;
            x = image_sample / modes[decode_mode].samples_per_pixel;
        } else {
            colour = 4;
            x = 0;
        }

        if (image_sample < 0) {
            x = 0;
            y = 0;
            colour = 4;
            return;
        }
    }
}

/**
 * @brief Frekvencia fényerővé alakítása
 * @param x Bemeneti frekvencia
 * @return Fényerő érték (0-255)
 */
uint8_t c_sstv_decoder::frequency_to_brightness(uint16_t x) {
    int16_t brightness = (256 * (x - SstvConstants::BLACK_FREQ_HZ)) / (SstvConstants::WHITE_FREQ_HZ - SstvConstants::BLACK_FREQ_HZ);
    return std::min(std::max(brightness, (int16_t)0), (int16_t)255);
}

/**
 * @brief Paritás ellenőrzése egy bájton
 * @param x Bemeneti bájt
 * @return Igaz, ha a paritás helyes, hamis egyébként
 */
bool parity_check(uint8_t x) {
    x ^= x >> 4;
    x ^= x >> 2;
    x ^= x >> 1;
    return (~x) & 1;
}
