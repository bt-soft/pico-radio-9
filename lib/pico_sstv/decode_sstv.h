#ifndef __SSTV_DECODER_H__
#define __SSTV_DECODER_H__

#include "half_band_filter2.h"

namespace SstvConstants {
constexpr int16_t SYNC_FREQ_THRESHOLD_HZ = 1300;
constexpr int16_t BLACK_FREQ_HZ = 1500;
constexpr int16_t WHITE_FREQ_HZ = 2300;
constexpr int32_t SLANT_CORRECTION_TOLERANCE_PERCENT = 1;
constexpr uint32_t SYNC_CONFIRM_SAMPLES = 10;
constexpr uint32_t CONFIRM_RETRIES = 4;
constexpr uint32_t FRACTION_BITS = 8;
} // namespace SstvConstants

class c_sstv_decoder {

  public:
    enum e_mode {
        martin_m1,            // Martin M1
        martin_m2,            // Martin M2
        scottie_s1,           // Scottie S1
        scottie_s2,           // Scottie S2
        scottie_dx,           // Scottie DX
        pd_50,                // PD 50
        pd_90,                // PD 90
        pd_120,               // PD 120
        pd_180,               // PD 180
        sc2_60,               // SC2 60
        sc2_120,              // SC2 120
        sc2_180,              // SC2 180
        robot24,              // Robot 24
        robot36,              // Robot 36
        robot72,              // Robot 72
        bw8,                  // Black and White 8
        bw12,                 // Black and White 12
        NUMBER_OFF_SSTV_MODES // Az elérhető SSTV módok száma
    };

  private:
    enum e_sync_state {
        detect,
        confirm,
    };

    enum e_state {
        detect_sync,
        confirm_sync,
        decode_line,
        wait,
    };

    struct s_sstv_mode {
        const char *name;
        uint16_t width;
        uint16_t max_height;
        uint32_t samples_per_line;
        uint32_t samples_per_colour_line;
        uint32_t samples_per_colour_gap;
        uint32_t samples_per_pixel;
        uint32_t samples_per_hsync;
    };

    s_sstv_mode sstv_mode;
    float m_Fs;
    uint32_t m_scale;
    uint32_t sync_counter = 0;
    uint16_t y_pixel = 0;
    uint16_t last_x = 0;
    uint32_t image_sample = 0;
    uint16_t last_sample = 0;
    uint32_t last_hsync_sample = 0;
    uint32_t sample_number = 0;
    uint32_t confirmed_sync_sample = 0;
    e_state state = detect_sync;
    e_sync_state sync_state = detect;
    void sample_to_pixel(uint16_t &x, uint16_t &y, uint8_t &colour, int32_t image_sample);
    uint8_t frequency_to_brightness(uint16_t x);
    uint32_t mean_samples_per_line;
    uint32_t sync_timeout = 0;
    uint32_t confirm_count;
    uint32_t pixel_accumulator;
    uint16_t pixel_n;
    int16_t last_phase = 0;
    uint8_t ssb_phase = 0;
    half_band_filter2 ssb_filter;
    uint32_t m_smoothed_sample = 0; // simított mintaváltozó a dekódoláshoz
    int16_t frequency;
    e_mode decode_mode;
    s_sstv_mode modes[NUMBER_OFF_SSTV_MODES];
    bool m_auto_slant_correction;
    uint32_t m_timeout;
    uint32_t m_martin_robot_offset;

  public:
    static const char *sstvModeNames[NUMBER_OFF_SSTV_MODES];
    static const char *getSstvModeName(e_mode mode) {
        if (mode < 0 || mode >= NUMBER_OFF_SSTV_MODES) {
            return "Unknown";
        }
        return sstvModeNames[mode];
    }

    c_sstv_decoder(float Fs);
    bool decode(uint16_t sample, uint16_t &line, uint16_t &col, uint8_t &colour, uint8_t &pixel, e_state &debug_state);
    bool decode_iq(int16_t sample_i, int16_t sample_q, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, int16_t &frequency);
    bool decode_audio(int16_t audio, uint16_t &pixel_y, uint16_t &pixel_x, uint8_t &pixel_colour, uint8_t &pixel, int16_t &frequency);
    void reset();

    /**
     * @brief Dekódolási mód lekérdezése
     * @return Aktuális dekódolási mód
     */
    e_mode get_mode() { return decode_mode; }

    /**
     * @brief Elérhető dekódolási módok lekérdezése
     * @return Dekódolási módok tömbje
     */
    s_sstv_mode *get_modes() { return &modes[0]; }

    /**
     * @brief Időtúllépés beállítása
     * @param timeout Időtúllépés másodpercben
     */
    void set_timeout_seconds(uint8_t timeout) { m_timeout = timeout * m_Fs; }

    /**
     * @brief Automatikus ferdítés korrekció engedélyezése/tiltása
     * @param enable Igaz az engedélyezéshez, hamis a tiltáshoz
     */
    void set_auto_slant_correction(bool enable) { m_auto_slant_correction = enable; }
};

#endif
