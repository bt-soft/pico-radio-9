/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: DecoderWeFax-c1.cpp                                                                                           *
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
 * Last Modified: 2025.12.22, Monday  09:54:50                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include <cmath>
#include <cstring>

#include "DecoderWeFax-c1.h"
#include "Utils.h"

// Globális dekódolt adat objektum, megosztva a magok között
extern DecodedData decodedData;

// Global debug reset flag
bool g_wefax_debug_reset = false;

// WEFAX működés debug engedélyezése de csak DEBUG módban
// #define __WEFAX_DEBUG  // KIKAPCSOLVA - túl sok log
#if defined(__DEBUG) && defined(__WEFAX_DEBUG)
#define WEFAX_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define WEFAX_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// =============================================================================
// WEFAX KONSTANSOK
// =============================================================================

// Választás a mért phasing soridő és fix 500ms között
// 1 = Mért phasing alapú LPM használata (pontosabb, de balra dőlhet ha rossz a mérés)
// 0 = Fix 500ms soridő (biztonságos, de lehet pontatlan)
#define USE_MEASURED_LPM 0 // Ha kell a mért phasing,  1-re kell állítani

#define WEFAX_IMAGE_HEIGHT 250
#define WEFAX_LPM 120
#define WEFAX_CARRIER_FREQ 1900.0f // Vivőfrekvencia (1500 Hz fekete, 2300 Hz fehér)
#define WEFAX_SHIFT 800.0f         // Deviáció (±400 Hz, teljes tartomány 800 Hz)
#define TWOPI (2.0f * M_PI)

#define WEAK_SIGNAL_IN_SECONDS 30.0f // Gyenge jel időkorlát (másodpercben) - enyhítve

/**
 * @brief Konstruktor
 */
DecoderWeFax_C1::DecoderWeFax_C1() {}

/**
 * @brief Visszaadja a WEFAX mód nevét
 */
const char *DecoderWeFax_C1::getModeName(WefaxMode mode) const {
    switch (mode) {
        case WefaxMode::IOC576:
            return "IOC576";
        case WefaxMode::IOC288:
            return "IOC288";
        default:
            return DECODER_MODE_UNKNOWN;
    }
}

/**
 * @brief Dekóder inicializálása és indítása
 * @param decoderConfig Dekóder konfigurációs
 * @return Sikeres indítás esetén true, egyébként false
 */
bool DecoderWeFax_C1::start(const DecoderConfig &decoderConfig) {

    // Mintavételi frekvencia: 11025 Hz
    sample_rate = WEFAX_SAMPLE_RATE_HZ;

    // IOC mód alapértelmezett: 576 (phasing detektálás automatikusan frissíti ha 288)
    current_ioc = 576;
    img_width = WEFAX_IOC576_WIDTH;

    // Vivő fázis lépés számítása (1900 Hz vivőhöz)
    phase_increment = TWOPI * WEFAX_CARRIER_FREQ / sample_rate;

    // Deviáció arány számítása (phase_diff → gray value konverzióhoz)
    // phase_diff (rad/sample) * sample_rate / (2*PI) = frekvencia (Hz)
    // Skálázás: gray = 128 + phase_diff * deviation_ratio
    // Fekete (1500 Hz = -400 Hz) → gray = 0, Fehér (2300 Hz = +400 Hz) → gray = 255
    // deviation_ratio = (sample_rate / TWOPI) * (255 / WEFAX_SHIFT)
    // KALIBRÁCIÓ: A phase_diff empirikusan ~10x nagyobb mint várható → osztva 10-zel
    float theoretical_ratio = (sample_rate / TWOPI) * (255.0f / WEFAX_SHIFT);
    deviation_ratio = theoretical_ratio / 10.0f; // Empirikus kalibrációs faktor

    WEFAX_DEBUG("WeFax-C1: \n--------------------------------------------------\n");
    WEFAX_DEBUG("    WeFax Start\n");
    WEFAX_DEBUG("--------------------------------------------------\n");
    WEFAX_DEBUG(" Mintavétel: %.0f Hz (FM)\n", sample_rate);
    WEFAX_DEBUG(" Vivő: %.0f Hz | Shift: ±%.0f Hz (teljes: %.0f Hz)\n", WEFAX_CARRIER_FREQ, WEFAX_SHIFT / 2.0f, WEFAX_SHIFT);
    WEFAX_DEBUG(" Fekete: %.0f Hz | Fehér: %.0f Hz\n", WEFAX_CARRIER_FREQ - WEFAX_SHIFT / 2.0f, WEFAX_CARRIER_FREQ + WEFAX_SHIFT / 2.0f);
    WEFAX_DEBUG(" Deviation ratio: %.2f (phase_diff → gray)\n", deviation_ratio);
    WEFAX_DEBUG("---------------------------------------------------\n");
    WEFAX_DEBUG(" Azonnali képvétel párhuzamos szinkron kereséssel\n");
    WEFAX_DEBUG(" Működés: kép rajzolás + phasing detektálás egyszerre\n");
    WEFAX_DEBUG("---------------------------------------------------\n\n");

    // FM demodulátor állapot nullázása
    phase_accumulator = 0.0f;
    prevz_real = 0.0f;
    prevz_imag = 0.0f;

    // DC blocker reset
    dc_prev_input = 0.0f;
    dc_prev_output = 0.0f;

    // Gray DC offset reset
    gray_dc_avg = 127.0f;

    // Debug counter reset trigger
    extern bool g_wefax_debug_reset; // Global flag
    g_wefax_debug_reset = true;

    // I/Q szűrő pufferek nullázása
    memset(i_buffer, 0, sizeof(i_buffer));
    memset(q_buffer, 0, sizeof(q_buffer));
    iq_buffer_index = 0;

    // Phasing detektálás nullázása
    rx_state = RXIMAGE; // Indítás AZONNAL képvétel módban (párhuzamos phasing keresés)
    phasing_count = 0;
    memset(phasing_history, 0, sizeof(phasing_history));
    phase_high = false;
    curr_phase_len = 0;
    curr_phase_high = 0;
    curr_phase_low = 0;
    phase_lines = 0;
    lpm_sum = 0.0f;

    // Minták száma soronként (frissül a phasing alapján)
    samples_per_line = sample_rate * 60.0f / WEFAX_LPM; // Alapértelmezett 120 LPM

    // Képfogadás nullázása
    img_sample = 0;
    last_col = 0;
    current_line_index = 0;
    line_started = false;
    memset(current_wefax_line, 0, WEFAX_MAX_OUTPUT_WIDTH); // Fekete háttér

    // Pixel átlagolás nullázása
    pixel_val = 0;
    pix_samples_nb = 0;

    // Jelezzük a Core0-nak az IOC módot
    decodedData.currentMode = (current_ioc == 576) ? 0 : 1; // 0=IOC576, 1=IOC288
    decodedData.modeChanged = true;                         // Mód változás jelzése
    decodedData.newImageStarted = true;                     // Jelezzük, hogy új kép kezdődött, hogy alapállapotba kerüljön a kijelző

    return true;
}

/**
 * @brief Dekóder leállítása és erőforrások felszabadítása
 */
void DecoderWeFax_C1::stop() {
    if (rx_state != IDLE) {
        WEFAX_DEBUG("WeFax-C1: \n--------------------------------------------------\n");
        WEFAX_DEBUG("    WeFax Stop\n");
        WEFAX_DEBUG("--------------------------------------------------\n");
        if (rx_state == RXIMAGE) {
            WEFAX_DEBUG("Fogadott sorok: %d/%d\n", current_line_index, WEFAX_IMAGE_HEIGHT);
        } else {
            WEFAX_DEBUG("Állapot: Phasing keresés megszakítva\n");
        }
        WEFAX_DEBUG("--------------------------------------------------\n\n");
    }
    rx_state = IDLE;
    current_line_index = 0; // Sor index nullázása, hogy újraindításkor a kép tetejéről induljon
}

/**
 * @brief WEFAX Dekóder resetelése
 */
void DecoderWeFax_C1::reset() {
    WEFAX_DEBUG("WeFax-C1: \n--------------------------------------------------\n");
    WEFAX_DEBUG("    WeFax Reset\n");
    WEFAX_DEBUG("--------------------------------------------------\n");

    // Állapot alaphelyzetbe állítása
    rx_state = IDLE;
    current_line_index = 0;

    // Phasing / demod állapotok törlése - tiszta indulás
    phase_lines = 0;
    lpm_sum = 0.0f;
    curr_phase_len = 0;
    curr_phase_high = 0;
    curr_phase_low = 0;
    phasing_count = 0;
    phasing_calls_nb = 0;
    phase_high = false;
    memset(phasing_history, 0, sizeof(phasing_history));

    // fldigi korreláció változók resetelése
    corr_calls_nb = 0;
    curr_corr_avg = 0.0;
    imag_corr_max = 0.0;
    corr_buffer_index = 0;
    last_corr_time = 0;
    memset(correlation_buffer, 0, sizeof(correlation_buffer));

    // DC blocker és AGC reset
    dc_prev_input = 0.0f;
    dc_prev_output = 0.0f;
    gray_dc_avg = 127.0f;

    // Kép-pufferek és pix számlálók alaphelyzetbe
    img_sample = 0;
    last_col = 0;
    pixel_val = 0;
    pix_samples_nb = 0;
    memset(current_wefax_line, 255, img_width);

    // Jelezzük a Core0-nak, hogy új kép kezdődött (kijelző törlés)
    ::decodedData.modeChanged = true;
    ::decodedData.currentMode = -1;
    ::decodedData.newImageStarted = true;

    // Azonnal induljon a phasing-keresés
    rx_state = RXPHASING;
}

// =============================================================================
// PROCESS SAMPLES - FŐ BELÉPÉSI PONT
// =============================================================================

/**
 * @brief Nyers audio minták feldolgozása - TELJES WEFAX dekódolás Goertzel-lel
 * @param samples Pointer a nyers audio mintákhoz (DC-centrált int16_t)
 * @param count Minták száma
 */
void DecoderWeFax_C1::processSamples(const int16_t *samples, size_t count) {

    // Demodulált szürkeérték puffer
    static uint8_t demod_buffer[256]; // Maximum blokk méret
    int demod_count = 0;

    // Jelv esztés detektálásához statisztikai változók
    static int signal_counter = 0;
    static int signal_gray_sum = 0;
    static int signal_gray_min = 255;
    static int signal_gray_max = 0;
    static int signal_black_count = 0;
    static int signal_white_count = 0;
    static float last_curr_mag = 0.0f;   // Debug: utolsó curr_mag érték
    static float last_phase_diff = 0.0f; // Debug: utolsó phase_diff érték
    static int last_gray_raw = 127;      // Debug: utolsó gray_raw érték

#ifdef __WEFAX_DEBUG
    // Debug: Periodikus kiírás a feldolgozott mintákról (csak debug módban)
    static int debug_counter = 0;
    static int debug_gray_sum = 0;
    static int debug_gray_min = 255;
    static int debug_gray_max = 0;
#endif

    // FM demoduláció (I/Q demoduláció vivővel + fázis differenciálás)
    for (size_t i = 0; i < count && i < 256; i++) {

        // DC blocker IIR filter (high-pass ~1 Hz @ 11025 Hz)
        // y[n] = alpha * (y[n-1] + x[n] - x[n-1])
        float input = (float)samples[i];
        float dc_blocked = dc_alpha * (dc_prev_output + input - dc_prev_input);
        dc_prev_input = input;
        dc_prev_output = dc_blocked;

        float audio_sample = dc_blocked;

        // I/Q demoduláció vivővel
        float cos_val = cosf(phase_accumulator);
        float sin_val = sinf(phase_accumulator);
        phase_accumulator += phase_increment;
        if (phase_accumulator > TWOPI) {
            phase_accumulator -= TWOPI;
        }

        float i_raw = audio_sample * cos_val;
        float q_raw = audio_sample * sin_val;

        // Egyszerű mozgóátlag szűrő I/Q komponensekre
        i_buffer[iq_buffer_index] = i_raw;
        q_buffer[iq_buffer_index] = q_raw;
        iq_buffer_index = (iq_buffer_index + 1) % IQ_FILTER_SIZE;

        float i_filtered = 0.0f;
        float q_filtered = 0.0f;
        for (int j = 0; j < IQ_FILTER_SIZE; j++) {
            i_filtered += i_buffer[j];
            q_filtered += q_buffer[j];
        }
        i_filtered /= IQ_FILTER_SIZE;
        q_filtered /= IQ_FILTER_SIZE;

        float currz_real = i_filtered;
        float currz_imag = q_filtered;

        // CLIP ellenőrzés
        // DC-korrigált jel, kis amplitúdó (~±100), I/Q szűrés után még kisebb
        const float CLIP = 0.01f; // Gyenge jel küszöb (drastikusan csökkentve)
        float curr_mag = sqrtf(currz_real * currz_real + currz_imag * currz_imag);
        float prev_mag = sqrtf(prevz_real * prevz_real + prevz_imag * prevz_imag);

        last_curr_mag = curr_mag; // Debug céljára mentés

        int gray_value;

        if (curr_mag <= CLIP && prev_mag <= CLIP) {
            // Gyenge jel - alapértelmezett KÖZÉPSZÜRKE (128)
            gray_value = 128;
        } else {
            // Fázis differenciálás
            float phase_diff = complex_arg_diff(prevz_real, prevz_imag, currz_real, currz_imag);
            last_phase_diff = phase_diff; // Debug mentés

            // Átalakítás szürkeértékre (HELYES képlet)
            // gray = 128 + phase_diff * deviation_ratio
            // Fekete (1500 Hz, -400 Hz) → negatív phase_diff → 128 + (-) = kis érték → sötét ✓
            // Fehér (2300 Hz, +400 Hz) → pozitív phase_diff → 128 + (+) = nagy érték → világos ✓
            float gray_float = 128.0f + deviation_ratio * phase_diff;
            int gray_raw = (int)roundf(gray_float);
            gray_raw = constrain(gray_raw, 0, 255);
            last_gray_raw = gray_raw; // Debug mentés

            // DC offset eltávolítása a gray value-ból (running average)
            gray_dc_avg = gray_dc_alpha * gray_dc_avg + (1.0f - gray_dc_alpha) * gray_raw;
            gray_value = gray_raw - (int)gray_dc_avg + 127;
            gray_value = constrain(gray_value, 0, 255);
        }

        prevz_real = currz_real;
        prevz_imag = currz_imag;

        demod_buffer[demod_count++] = (uint8_t)gray_value;

        // Jelvesztés detektáláshoz statisztika gyűjtése
        signal_counter++;
        signal_gray_sum += gray_value;
        if (gray_value < signal_gray_min) {
            signal_gray_min = gray_value;
        }
        if (gray_value > signal_gray_max) {
            signal_gray_max = gray_value;
        }
        if (gray_value < 64) {
            signal_black_count++;
        }
        if (gray_value > 192) {
            signal_white_count++;
        }

#ifdef __WEFAX_DEBUG
        // Debug statisztika gyűjtése
        debug_counter++;
        debug_gray_sum += gray_value;
        if (gray_value < debug_gray_min) {
            debug_gray_min = gray_value;
        }
        if (gray_value > debug_gray_max) {
            debug_gray_max = gray_value;
        }
#endif
    }

    // A jelvesztés periodikus ellenőrzése
    static unsigned long last_signal_check_time = millis();
    if (Utils::timeHasPassed(last_signal_check_time, 1000)) { // 1 másodperc
        last_signal_check_time = millis();

        if (signal_counter > 0) {
            int signal_gray_avg = signal_gray_sum / signal_counter;
            float signal_black_ratio = (float)signal_black_count / signal_counter;
            //float signal_white_ratio = (float)signal_white_count / signal_counter;
            int signal_dynamic_range = signal_gray_max - signal_gray_min;

            // DEBUG: minden esetben kiírjuk az első 60 másodpercben
            extern bool g_wefax_debug_reset;
            static int temp_debug_counter = 0;
            if (g_wefax_debug_reset) {
                temp_debug_counter = 0;
                g_wefax_debug_reset = false;
            }
            if (temp_debug_counter++ < 60) {
                Serial.printf("WeFax DEBUG: gray_avg=%d [%d-%d] | DC_avg=%.1f | phase_diff=%.4f | gray_raw=%d\n", //
                              signal_gray_avg, signal_gray_min, signal_gray_max, gray_dc_avg, last_phase_diff, last_gray_raw);
            }

#ifdef __WEFAX_DEBUG
            // Debug kiírás (csak debug módban)
            if (debug_counter > 0) {
                int debug_gray_avg = debug_gray_sum / debug_counter;
                int debug_range = debug_gray_max - debug_gray_min;

                if (rx_state == IDLE) {
                    WEFAX_DEBUG("WeFax-C1: IDLE | Jel: %d±%d [%d-%d]\n", debug_gray_avg, debug_range / 2, debug_gray_min, debug_gray_max);

                } else if (rx_state == RXPHASING) {
                    WEFAX_DEBUG("WeFax-C1: SZINKRON KERESÉS | Jel: %d±%d [%d-%d]\n", debug_gray_avg, debug_range / 2, debug_gray_min, debug_gray_max);

                } else if (rx_state == RXIMAGE) {
                    // IMAGE módban: sor progress megjelenítése
                    float progress = (float)current_line_index / WEFAX_IMAGE_HEIGHT * 100.0f;
                    WEFAX_DEBUG("WeFax-C1: KÉP %d/%d (%.0f%%) | IOC%d %.0f LPM | Jel: %d [%d-%d]\n", current_line_index, WEFAX_IMAGE_HEIGHT, progress,
                                current_ioc, (phase_lines > 0) ? (lpm_sum / phase_lines) : 120.0f, debug_gray_avg, debug_gray_min, debug_gray_max);
                }
            }
#endif

            // Jelvesztés detektálás IMAGE módban (kombinált logika, WEFAX képekhez optimalizálva)
            static int weak_signal_count = 0;
            if (rx_state == RXIMAGE) {
                bool weak = false;

                // 1. Csak NAGYON extrém esetekben (túl szigorú volt)
                //    WEFAX képek: átlag 235-245, tartomány 0-255 → NORMÁLIS
                //    Gyenge jel fehér: átlag > 254, tartomány < 10 → HIBÁS
                //    Gyenge jel fekete: átlag < 5, tartomány < 10 → HIBÁS
                if (signal_dynamic_range < 10 && (signal_gray_avg > 254 || signal_gray_avg < 5)) {
                    weak = true;
                }

                // 2. Túl sok fekete (> 98%) - majdnem csak fekete jel (enyhítve)
                //    WEFAX képek: fekete 1-5%, fehér 90-96% → NORMÁLIS
                //    Gyenge jel: fekete > 98% → HIBÁS (nincs adás)
                if (signal_black_ratio > 0.98f) {
                    weak = true;
                }

                // 3. KIKAPCSOLVA - Középszürke jelek normálisak lehetnek
                //    A fekete/fehér arány logika túl szigorú volt
                //    Ha van dinamikatartomány (>100), akkor a jel valószínűleg jó
                /*
                if (signal_black_ratio < 0.03f && signal_white_ratio < 0.20f) {
                    weak = true;
                }
                */

                // 4. KIKAPCSOLVA - A középszürke + nagy dinamika feltétel hibás volt
                //    127±126 jel tökéletes, nem AGC-zaj!
                //    Nagy dinamikatartomány (200+) pont azt jelenti hogy JÓ a jel!
                /*
                if (signal_gray_avg > 80 && signal_gray_avg < 180 && signal_dynamic_range > 240) {
                    weak = true;
                }
                */

                // Ha jelvesztés van
                if (weak) {
                    weak_signal_count++;

                    // Ha a jelvesztés eléri a küszöböt (másodpercben mérünk így a számláló is másodperc alapú lesz)
                    if (weak_signal_count >= WEAK_SIGNAL_IN_SECONDS) {
                        WEFAX_DEBUG("WeFax-C1: \n-------------------------------------------------\n");
                        WEFAX_DEBUG(" ⚠  JELVESZTÉS - VÉTEL LEÁLLÍTVA\n");
                        WEFAX_DEBUG("-------------------------------------------------\n");
                        WEFAX_DEBUG("Jelstatisztika (%.0f sec gyenge jel): \n", WEAK_SIGNAL_IN_SECONDS);
                        WEFAX_DEBUG(" • Átlag: %d (%s)\n", signal_gray_avg,
                                    signal_gray_avg > 240  ? "túl világos"
                                    : signal_gray_avg < 15 ? "túl sötét"
                                                           : "normális");
                        WEFAX_DEBUG(" • Tartomány: %d-%d (range=%d)\n", signal_gray_min, signal_gray_max, signal_dynamic_range);
                        WEFAX_DEBUG(" • Fekete: %.1f%% | Fehér: %.1f%%\n", signal_black_ratio * 100, signal_white_ratio * 100);
                        WEFAX_DEBUG("---------------------------------------------------\n");
                        WEFAX_DEBUG(" → IDLE módba váltás\n");
                        WEFAX_DEBUG("---------------------------------------------------\n");
                        rx_state = IDLE;
                        weak_signal_count = 0;
                    }

                } else {
                    weak_signal_count = 0;
                }
            }

            // Reset jelvesztés statisztikák
            signal_counter = 0;
            signal_gray_sum = 0;
            signal_gray_min = 255;
            signal_gray_max = 0;
            signal_black_count = 0;
            signal_white_count = 0;

#ifdef __WEFAX_DEBUG
            // Debug statisztikák törlése
            debug_counter = 0;
            debug_gray_sum = 0;
            debug_gray_min = 255;
            debug_gray_max = 0;
#endif
        }
    }

    // Demodulált értékek feldolgozása
    for (int i = 0; i < demod_count; i++) {
        int gray_value = demod_buffer[i];

        // Mindig futtatjuk a phasing detektálást, bármilyen állapotban is vagyunk
        this->decode_phasing(gray_value);

        // Ha KÉPFOGADÁS módban vagyunk, dekódoljuk a képet
        if (rx_state == RXIMAGE) {
            this->decode_image(gray_value, &current_line_index);
        }
        // Ha IDLE, akkor csak phasing detektálás történik
    }
}

/**
 * @brief Kiszámolja a komplex számok argumentum különbségét
 *  Komplex argumentum különbség: arg(conj(prevz) * currz) = atan2(imag, real)
 * conj(a + bi) * (c + di) = (a - bi) * (c + di) = (ac + bd) + (ad - bc)i
 * @param prev_real Előző komplex szám valós része
 * @param prev_imag Előző komplex szám képzetes része
 * @param curr_real Jelenlegi komplex szám valós része
 * @param curr_imag Jelenlegi komplex szám képzetes része
 * @return Az argumentum különbség radiánban
 */
float DecoderWeFax_C1::complex_arg_diff(float prev_real, float prev_imag, float curr_real, float curr_imag) {
    float real_part = prev_real * curr_real + prev_imag * curr_imag;
    float imag_part = prev_real * curr_imag - prev_imag * curr_real;
    return atan2f(imag_part, real_part);
}

// =============================================================================
// PHASING DEKÓDOLÁS
// =============================================================================

/**
 * @brief Phasing sor dekódolása
 */
void DecoderWeFax_C1::decode_phasing(int gray_value) {
    // Mozgóátlag szűrő 16 mintán
    phasing_history[phasing_count % PHASING_FILTER_SIZE] = gray_value;
    phasing_count++;

    if (phasing_count >= PHASING_FILTER_SIZE) {
        int filtered_value = 0;
        for (int i = 0; i < PHASING_FILTER_SIZE; i++) {
            filtered_value += phasing_history[i];
        }
        gray_value = filtered_value / PHASING_FILTER_SIZE;
    }

    // Minták számlálása fázisonként
    curr_phase_len++;

    // GLOBÁLIS PHASING TIMER: mióta vagyunk PHASING módban?
    // Ez független az 5 másodperces reset-től!
    static int total_phasing_samples = 0;
    if (rx_state == RXPHASING) {
        total_phasing_samples++;
    } else {
        total_phasing_samples = 0; // Reset ha IMAGE módba váltunk
    }

    // KIKAPCSOLVA - Már induláskor RXIMAGE módban vagyunk, nincs szükség timeout-ra
    if (false && total_phasing_samples > 30 * sample_rate && phase_lines == 0) {
        WEFAX_DEBUG("WeFax-C1: \n-------------------------------------------------\n");
        WEFAX_DEBUG("⚠  PHASING TIMEOUT - 10 másodperc eltelt\n");
        WEFAX_DEBUG("-------------------------------------------------\n");
        WEFAX_DEBUG(" Nincs érvényes phasing szinkron jel\n");
        WEFAX_DEBUG(" → Fallback: 500ms soridő használata\n");
        WEFAX_DEBUG(" → Képfogadás indul alapértelmezett paraméterrel\n");
        WEFAX_DEBUG("-------------------------------------------------\n\n");

        // Mindig 500ms fallback, USE_MEASURED_LPM-től függetlenül
        samples_per_line = sample_rate * 0.5f; // 500ms = 0.5 sec

        rx_state = RXIMAGE;
        img_sample = 0;
        last_col = 0;
        phase_lines = 1;           // Ne próbálkozzon újra
        total_phasing_samples = 0; // Reset

        return;
    }

    // Magas/alacsony pixelek számlálása (ADAPTÍV küszöbök)
    // WEFAX standard: Fehér ~200-240, Fekete ~10-50 (0-255 skálán)
    // Fehér: > 160, Fekete: < 80
    if (gray_value > 160) {
        curr_phase_high++;
    } else if (gray_value < 80) {
        curr_phase_low++;
    }

    // Periodikus phasing állapot kiírás + adaptív jel követés (minden másodpercben)
    static int phasing_status_timer = 0;
    static int gray_hist_high = 0;  // Max érték az utolsó másodpercben
    static int gray_hist_low = 255; // Min érték az utolsó másodpercben

    // Folyamatos min/max követés a jel tartományához
    if (gray_value > gray_hist_high) {
        gray_hist_high = gray_value;
    }
    if (gray_value < gray_hist_low) {
        gray_hist_low = gray_value;
    }

    // Phasing állapot vizsgálat reset 1 másodpercenként
    if (++phasing_status_timer >= WEFAX_SAMPLE_RATE_HZ) { // 1mp-ként Phasing állapot kiírás (1 sec @ 11025 Hz)
#ifdef __WEFAX_DEBUG_NEMKELL
        float elapsed_sec = curr_phase_len / sample_rate;
        float white_pct = (curr_phase_len > 0) ? (100.0f * curr_phase_high / curr_phase_len) : 0;
        float black_pct = (curr_phase_len > 0) ? (100.0f * curr_phase_low / curr_phase_len) : 0;
        WEFAX_DEBUG("WeFax-C1:  Phasing: %.1fs | Fehér: %.1f%% | Fekete: %.1f%% | Fázis: %s | Tartomány: %d-%d\n", elapsed_sec, white_pct, black_pct,
                    phase_high ? "MAGAS" : "ALACSONY", gray_hist_low, gray_hist_high);
#endif

        phasing_status_timer = 0;
        gray_hist_high = 0;
        gray_hist_low = 255;
    }

    // Átmenetek detektálása (ENYHÍTETT küszöbök)
    // Fehér kezdet: > 120 (enyhítve), Fekete kezdet (SYNC): < 120 (enyhítve)
    if (gray_value > 120 && !phase_high) {
        // FEKETE → FEHÉR átmenet
        phase_high = true;
        WEFAX_DEBUG("WeFax-C1:  >>> FEHÉR kezdet: gray=%d\n", gray_value);
    } else if (gray_value < 120 && phase_high) {
        // FEHÉR → FEKETE átmenet (sorszinkron!)
        phase_high = false;
        WEFAX_DEBUG("WeFax-C1:  <<< FEKETE SYNC: gray=%d (sor hossz: %.2fs)\n", gray_value, curr_phase_len / sample_rate);
        // Érvényes phasing sor ellenőrzése (NAGYON ENYHÍTETT kritériumok)
        // Phasing sor: bármilyen fehér→fekete átmenet ami elfogadható időtartamú
        float white_ratio = (float)curr_phase_high / curr_phase_len;
        float black_ratio = (float)curr_phase_low / curr_phase_len;
        bool valid_ratios = (white_ratio >= 0.001f) && (black_ratio >= 0.10f);                                   // 0.1% fehér, 10% fekete
        bool valid_duration = (curr_phase_len >= 0.20f * sample_rate) && (curr_phase_len <= 1.0f * sample_rate); // 200ms-1s

        // WEFAX_DEBUG("WeFax-C1: Phasing ellenőrzés: %.1fs | F:%.1f%% Sz:%.1f%% | Érvényes: arány=%s idő=%s\n", curr_phase_len / sample_rate, white_ratio *
        // 100,
        //             black_ratio * 100, valid_ratios ? "✓" : "✗", valid_duration ? "✓" : "✗");

        if (valid_ratios && valid_duration) {
            // ÉRVÉNYES PHASING SOR detektálva!
#ifdef __WEFAX_DEBUG
            float line_time_ms = curr_phase_len * 1000.0f / sample_rate;
#endif
            float tmp_lpm = 60.0f * sample_rate / curr_phase_len;

            // outlier szűrés: csak 90-300 LPM közötti értékeket fogadunk el
            // (IOC576 = 120 LPM, IOC288 = 240 LPM, ±50% mozgástér)
            bool valid_lpm = (tmp_lpm >= 90.0f) && (tmp_lpm <= 300.0f);

            // Ha IDLE állapotban vagyunk és érvényes phasing sort detektáltunk, automatikusan RXPHASING-re váltunk
            if (rx_state == IDLE && valid_lpm) {
                WEFAX_DEBUG("WeFax-C1: 🔄 AUTOMATIKUS ÚJRAINDÍTÁS: Phasing jel detektálva\n");
                rx_state = RXPHASING;
                // Phasing számlálók nullázása, hogy tiszta lappal induljon
                phase_lines = 0;
                lpm_sum = 0.0f;
            }

            if (valid_lpm) {
                lpm_sum += tmp_lpm;
                phase_lines++;
                // Szinkron jel progressz megjelenítése (4-ből hány van meg)
#ifdef __WEFAX_DEBUG
                const char *progress_bar[] = {"▪", "▪▪", "▪▪▪", "▪▪▪▪"};
                WEFAX_DEBUG("WeFax-C1: 🔵 Szinkron jel %d/4 %s | %.1f LPM | Soridő: %.0f ms | F:%.0f%% Sz:%.0f%%\n", //
                            phase_lines,                                                                             //
                            (phase_lines <= 4) ? progress_bar[phase_lines - 1] : "▪▪▪▪+",                            //
                            tmp_lpm,                                                                                 //
                            line_time_ms,                                                                            //
                            white_ratio * 100, black_ratio * 100);
#endif

            } else {
                // Outlier detektálva - NEM számítjuk bele az átlagba!
                // WEFAX_DEBUG("WeFax-C1: 🔴 Hibás szinkron (%.1f LPM - érvénytelen, 90-300 tartományon kívül)\n", tmp_lpm);
            }

            // Folyamatosan frissítjük az LPM-et minden phasing sornál
            float avg_lpm = (phase_lines > 0) ? (lpm_sum / phase_lines) : 120.0f;

#if USE_MEASURED_LPM
            // MÉRT phasing alapú soridő használata
            samples_per_line = sample_rate * 60.0f / avg_lpm;
            float avg_line_time_ms = samples_per_line * 1000.0f / sample_rate;
#else
            // FIX 500ms soridő használata (biztonságos módszer)
            samples_per_line = sample_rate * 0.5f; // 500ms = 0.5 sec
#endif

            // IOC mód detektálás LPM alapján (120 LPM=IOC576, 240 LPM=IOC288)
            uint32_t detected_ioc = (avg_lpm > 180.0f) ? 288 : 576;
            if (detected_ioc != current_ioc) {
                current_ioc = detected_ioc;
                img_width = (current_ioc == 576) ? WEFAX_IOC576_WIDTH : WEFAX_IOC288_WIDTH;
                decodedData.currentMode = (current_ioc == 576) ? 0 : 1;
                decodedData.modeChanged = true;
            }

            // fldigi: több phasing sor gyűjtése jobb átlaghoz (20 sor helyett 10-15)
            // Elegendő phasing sor után átváltunk IMAGE módba
            if (phase_lines >= 2 && phase_lines <= num_phase_lines) {
                phasing_calls_nb++;

                // fldigi módon: csak minden 5. phasing sornál frissítjük az LPM-et
                if ((phasing_calls_nb % 5) == 0 || phase_lines == num_phase_lines) {
                    WEFAX_DEBUG("WeFax-C1: \n-------------------------------------------------\n");

                    // Ha már IMAGE módban voltunk → ÚJ KÉP KEZDŐDÖTT!
                    if (rx_state == RXIMAGE) {
                        WEFAX_DEBUG(" 🔄 ÚJ KÉP KEZDŐDIK (phasing újra)\n");
                    } else {
                        WEFAX_DEBUG(" ✓ SZINKRONIZÁLVA - KÉPFOGADÁS INDUL\n");
                    }

                    WEFAX_DEBUG("-------------------------------------------------\n");
#if USE_MEASURED_LPM
                    WEFAX_DEBUG(" Sebesség: %.1f LPM (mért)\n", avg_lpm);
                    WEFAX_DEBUG(" Soridő: %.1f ms (%.0f minta/sor)\n", avg_line_time_ms, samples_per_line);
#else
                    WEFAX_DEBUG(" Sebesség: %.1f LPM (detektált)", avg_lpm);
                    WEFAX_DEBUG(" Soridő: 500.0 ms FIX (%.0f minta/sor)", samples_per_line);
#endif
                    WEFAX_DEBUG(" Mód: IOC%d | Képszélesség: %d pixel", current_ioc, img_width);
                    WEFAX_DEBUG(" Magasság: %d sor", WEFAX_IMAGE_HEIGHT);
                    WEFAX_DEBUG("--------------------------------------------------\n");
                    WEFAX_DEBUG(" Kép dekódolása folyamatban...\n");
                    WEFAX_DEBUG(" ℹ Finomhangolás: További szinkronoknál\n");
                    WEFAX_DEBUG("--------------------------------------------------\n\n");

                    rx_state = RXIMAGE;
                    img_sample = (int)(1.025f * samples_per_line);

                    float tmp_pos = fmodf((float)img_sample, samples_per_line) / samples_per_line;
                    last_col = (int)(tmp_pos * img_width);

                    // ÚJ KÉP JELZÉSE a Core0-nak (képernyő törlés + pozíció nullázás)
                    current_line_index = 0;
                    decodedData.newImageStarted = true;
                }
            } else if (phase_lines > 4 && rx_state == RXIMAGE && valid_lpm) {
                // IMAGE módban folytatjuk a phasing mérést - finomhangoljuk az LPM-et
#if USE_MEASURED_LPM
                // MÉRT mód: frissítjük a samples_per_line értéket AZONNAL!
                samples_per_line = sample_rate * 60.0f / avg_lpm;
                WEFAX_DEBUG("WeFax-C1: 🔧 Finomhangolás #%d: %.1f LPM → %.0f minta/sor (frissítve)\n", phase_lines, avg_lpm, samples_per_line);
#else
                // FIX mód: csak logoljuk, nem frissítünk
                WEFAX_DEBUG("WeFax-C1: ℹ Szinkron #%d: %.1f LPM detektálva (FIX 500ms használatban)\n", phase_lines, avg_lpm);
#endif
            }

            // CSAK az érvényes phasing sor után reseteljük!
            // Ez azért fontos, mert így a következő mérés tiszta lappal indul.
            curr_phase_len = 0;
            curr_phase_high = 0;
            curr_phase_low = 0;

        } else {
            // NEM érvényes phasing sor detektálva (pl.: kép tartalom, zaj, stb.)
            // Ha túl hosszú lett (5 sec timeout), akkor reset
            if (curr_phase_len > 5 * sample_rate) {
                // WEFAX_DEBUG("[WEFAX] Phasing timeout (5 sec) - reset\n");
                curr_phase_len = 0;
                curr_phase_high = 0;
                curr_phase_low = 0;
            }
            // Ha csak rövid vagy rossz arány, akkor NEM resetelünk - folytatjuk a számlálást!
        }
    }
}

// =============================================================================
// KÉP DEKÓDOLÁS
// =============================================================================

/**
 * @brief Képsor dekódolása
 * @param gray_value Aktuális szürkeérték
 * @param current_line_idx Pointer az aktuális sor indexre
 */
void DecoderWeFax_C1::decode_image(int gray_value, uint16_t *current_line_idx) {
    float current_row_dbl = (float)img_sample / samples_per_line;
    int current_row = (int)current_row_dbl;
    float fractional_part = current_row_dbl - current_row;
    int col = (int)(img_width * fractional_part);

    if (col < 0) {
        col = 0;
    }

    if (col >= img_width) {
        col = img_width - 1;
    }

    if (col < last_col) {
        if (pix_samples_nb > 0 && last_col < WEFAX_MAX_OUTPUT_WIDTH) {
            current_wefax_line[last_col] = (uint8_t)(pixel_val / pix_samples_nb);
            pixel_val = 0;
            pix_samples_nb = 0;
        }
        if (line_started) {
            DecodedLine newLine;
            newLine.lineNum = *current_line_idx;
            memcpy(newLine.wefaxPixels, current_wefax_line, img_width);
            if (!decodedData.lineBuffer.put(newLine)) {
                WEFAX_DEBUG("WeFax-C1: ⚠ BUFFER TELE! Sor #%d elveszett (Core0 lassú?)\n", *current_line_idx);
            }

            // fldigi: line-to-line korreláció számítás minden sor végén
            // De csak másodpercenként egyszer (CPU spórolás)
            unsigned long now = millis();
            if (now - last_corr_time >= 1000) { // 1 másodpercenként
                correlation_calc();
                last_corr_time = now;
            }
        }
        *current_line_idx = (*current_line_idx + 1) % WEFAX_IMAGE_HEIGHT;
        memset(current_wefax_line, 255, img_width);
        line_started = true;
    }

    if (col != last_col) {
        if (pix_samples_nb > 0 && last_col >= 0 && last_col < WEFAX_MAX_OUTPUT_WIDTH) {
            current_wefax_line[last_col] = (uint8_t)(pixel_val / pix_samples_nb);
        }
        pixel_val = 0;
        pix_samples_nb = 0;
        last_col = col;
    }

    pixel_val += gray_value;
    pix_samples_nb++;
    img_sample++;

    // fldigi: correlation buffer feltöltése (ring buffer)
    correlation_buffer[corr_buffer_index] = (uint8_t)gray_value;
    corr_buffer_index = (corr_buffer_index + 1) % CORR_BUFFER_SIZE;
}

// =============================================================================
// fldigi LINE-TO-LINE KORRELÁCIÓ (KÉPMINŐSÉG ELLENŐRZÉS)
// =============================================================================

/**
 * @brief Kiszámítja a korrelációt két sor között
 * @param line_length Sor hossza (mintákban)
 * @param line_offset Eltolás (mintákban) - tipikusan 1 sor hossza
 * @return Korreláció érték (0.0-1.0)
 *
 * fldigi alapú line-to-line correlation számítás.
 * Ezt használja az fldigi a kép minőségének ellenőrzésére és az APT stop detektáláshoz.
 */
double DecoderWeFax_C1::correlation_from_index(size_t line_length, size_t line_offset) const {
    // Ring buffer indexelés
    size_t line_length_plus_img_sample = line_length + img_sample;

    // Átlagok számítása
    int avg_pred = 0, avg_curr = 0;
    for (size_t i = img_sample; i < line_length_plus_img_sample; ++i) {
        int pix_pred = correlation_buffer[(i) % CORR_BUFFER_SIZE];
        int pix_curr = correlation_buffer[(i + line_offset) % CORR_BUFFER_SIZE];
        avg_pred += pix_pred;
        avg_curr += pix_curr;
    }
    avg_pred /= line_length;
    avg_curr /= line_length;

    // Korreláció számítás
    int numerator = 0, denom_pred = 0, denom_curr = 0;
    for (size_t i = img_sample; i < line_length_plus_img_sample; ++i) {
        int pix_pred = correlation_buffer[(i) % CORR_BUFFER_SIZE];
        int pix_curr = correlation_buffer[(i + line_offset) % CORR_BUFFER_SIZE];
        int delta_pred = pix_pred - avg_pred;
        int delta_curr = pix_curr - avg_curr;
        numerator += delta_pred * delta_curr;
        denom_pred += delta_pred * delta_pred;
        denom_curr += delta_curr * delta_curr;
    }

    double denominator = sqrt((double)denom_pred * (double)denom_curr);
    if (denominator == 0.0) {
        return 0.0;
    } else {
        return fabs(numerator / denominator);
    }
}

/**
 * @brief Periodikus korreláció számítás (fldigi módon)
 *
 * Ezt hívjuk meg minden sor végén a kép minőségének nyomon követéséhez.
 * Az fldigi ezt használja APT stop detektáláshoz és minőségellenőrzéshez.
 */
void DecoderWeFax_C1::correlation_calc() {
    corr_calls_nb++;

    // Egy sor hossza mintákban
    size_t corr_smpl_lin = (size_t)samples_per_line;
    if (corr_smpl_lin == 0 || corr_smpl_lin > CORR_BUFFER_SIZE / 2) {
        return; // Hibás érték
    }

    // Korreláció számítás az előző sorhoz képest
    double current_corr = correlation_from_index(corr_smpl_lin, corr_smpl_lin);

    // Bound checking
    if (current_corr > 1.0) {
        current_corr = 1.0;
    }

    // fldigi módon: exponenciális mozgóátlag (decayavg szerű)
    static const int min_corr_rows = 5; // Minimum sorok száma az átlagoláshoz

    if (corr_calls_nb < min_corr_rows) {
        curr_corr_avg = current_corr;
        imag_corr_max = 0.0;
    } else {
        // Mozgóátlag: weight = min_corr_rows / (min_corr_rows + 1)
        curr_corr_avg = (curr_corr_avg * min_corr_rows + current_corr) / (min_corr_rows + 1);
        imag_corr_max = (curr_corr_avg > imag_corr_max) ? curr_corr_avg : imag_corr_max;
    }

    // Debug minden 10. híváskor
    if ((corr_calls_nb % 10) == 0) {
        WEFAX_DEBUG("WeFax-C1: Correlation: curr=%.3f avg=%.3f max=%.3f calls=%d\n", current_corr, curr_corr_avg, imag_corr_max, corr_calls_nb);
    }
}
