/**
 * @file WefaxDecoder-c1.cpp
 * @brief WEFAX dekóder implementációja Core-1 számára
 * @project Pico Radio
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */
#include <cmath>
#include <cstring>

#include "Utils.h"
#include "WefaxDecoder-c1.h"

// Globális dekódolt adat objektum, megosztva a magok között
extern DecodedData decodedData;

// WEFAX működés debug engedélyezése de csak DEBUG módban
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
#define WEFAX_SHIFT 400.0f         // Deviáció (KÍSÉRLETI: csökkentve 800→400 a DC-korrigált jelhez)
#define TWOPI (2.0f * M_PI)

#define WEAK_SIGNAL_IN_SECONDS 6.0f // Gyenge jel időkorlát (másodpercben)

/**
 * @brief Konstruktor
 */
WefaxDecoderC1::WefaxDecoderC1() {}

/**
 * @brief Visszaadja a WEFAX mód nevét
 */
const char *WefaxDecoderC1::getModeName(WefaxMode mode) const {
    switch (mode) {
        case WefaxMode::IOC576:
            return "IOC576";
        case WefaxMode::IOC288:
            return "IOC288";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Dekóder inicializálása és indítása
 * @param decoderConfig Dekóder konfigurációs
 * @return Sikeres indítás esetén true, egyébként false
 */
bool WefaxDecoderC1::start(const DecoderConfig &decoderConfig) {

    // Mintavételi frekvencia: 11025 Hz
    sample_rate = WEFAX_SAMPLE_RATE_HZ;

    // IOC mód alapértelmezett: 576 (phasing detektálás automatikusan frissíti ha 288)
    current_ioc = 576;
    img_width = WEFAX_IOC576_WIDTH;

    // Vivő fázis lépés számítása (1900 Hz vivőhöz)
    phase_increment = TWOPI * WEFAX_CARRIER_FREQ / sample_rate;

    // Deviáció arány számítása
    deviation_ratio = (sample_rate / WEFAX_SHIFT) / TWOPI;

    WEFAX_DEBUG("\n--------------------------------------------------\n");
    WEFAX_DEBUG("    WeFax Start\n");
    WEFAX_DEBUG("--------------------------------------------------\n");
    WEFAX_DEBUG(" Mintavétel: %.0f Hz (FM)\n", sample_rate);
    WEFAX_DEBUG(" Vivő: %.0f Hz | Shift: ±%.0f Hz\n", WEFAX_CARRIER_FREQ, WEFAX_SHIFT);
    WEFAX_DEBUG("---------------------------------------------------\n");
    WEFAX_DEBUG(" Phasing szinkron keresése...\n");
    WEFAX_DEBUG(" Várakozás: fehér→fekete szinkronjelre\n");
    WEFAX_DEBUG(" Timeout: 10 mp után 500ms fallback\n");
    WEFAX_DEBUG("---------------------------------------------------\n\n");

    // FM demodulátor állapot nullázása
    phase_accumulator = 0.0f;
    prevz_real = 0.0f;
    prevz_imag = 0.0f;

    // I/Q szűrő pufferek nullázása
    memset(i_buffer, 0, sizeof(i_buffer));
    memset(q_buffer, 0, sizeof(q_buffer));
    iq_buffer_index = 0;

    // Phasing detektálás nullázása
    rx_state = RXPHASING; // Indítás közvetlenül phasing módban
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
void WefaxDecoderC1::stop() {
    if (rx_state != IDLE) {
        WEFAX_DEBUG("\n--------------------------------------------------\n");
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

// =============================================================================
// PROCESS SAMPLES - FŐ BELÉPÉSI PONT
// =============================================================================

/**
 * @brief Nyers audio minták feldolgozása - TELJES WEFAX dekódolás Goertzel-lel
 * @param samples Pointer a nyers audio mintákhoz (DC-centrált int16_t)
 * @param count Minták száma
 */
void WefaxDecoderC1::processSamples(const int16_t *samples, size_t count) {

    // Demodulált szürkeérték puffer
    static uint8_t demod_buffer[256]; // Maximum blokk méret
    int demod_count = 0;

    // Jelvesztés detektáláshoz statisztikai változók
    static int signal_counter = 0;
    static int signal_gray_sum = 0;
    static int signal_gray_min = 255;
    static int signal_gray_max = 0;
    static int signal_black_count = 0;
    static int signal_white_count = 0;

#ifdef __WEFAX_DEBUG
    // Debug: Periodikus kiírás a feldolgozott mintákról (csak debug módban)
    static int debug_counter = 0;
    static int debug_gray_sum = 0;
    static int debug_gray_min = 255;
    static int debug_gray_max = 0;
#endif

    // FM demoduláció (I/Q demoduláció vivővel + fázis differenciálás)
    for (size_t i = 0; i < count && i < 256; i++) {

        // NEM normalizálunk!
        //  Az ADC eleve DC-korrigált így kis amplitúdójú jeleket ad (~±100)
        float audio_sample = (float)samples[i];

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
        // DC-korrigált jel, nincs normalizálás → nagyobb abszolút értékek
        const float CLIP = 0.1f; // Gyenge jel küszöb (adjusted for non-normalized samples)
        float curr_mag = sqrtf(currz_real * currz_real + currz_imag * currz_imag);
        float prev_mag = sqrtf(prevz_real * prevz_real + prevz_imag * prevz_imag);

        int gray_value;
        float phase_diff = 0.0f; // Deklaráció itt, hogy debug-ban használható legyen

        if (curr_mag <= CLIP && prev_mag <= CLIP) {
            // Gyenge jel - alapértelmezett fehér
            gray_value = 255;
        } else {
            // Fázis differenciálás
            phase_diff = complex_arg_diff(prevz_real, prevz_imag, currz_real, currz_imag);

            // Átalakítás szürkeértékre
            float gray_float = 255.0f * (0.5f - deviation_ratio * phase_diff);
            gray_value = (int)roundf(gray_float);

            // Korlátozás 0-255 közé
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
            float signal_white_ratio = (float)signal_white_count / signal_counter;
            int signal_dynamic_range = signal_gray_max - signal_gray_min;

#ifdef __WEFAX_DEBUG
            // Debug kiírás (csak debug módban)
            if (debug_counter > 0) {
                int debug_gray_avg = debug_gray_sum / debug_counter;
                int debug_range = debug_gray_max - debug_gray_min;

                if (rx_state == IDLE) {
                    WEFAX_DEBUG("IDLE | Jel: %d±%d [%d-%d]\n", debug_gray_avg, debug_range / 2, debug_gray_min, debug_gray_max);

                } else if (rx_state == RXPHASING) {
                    WEFAX_DEBUG("SZINKRON KERESÉS | Jel: %d±%d [%d-%d]\n", debug_gray_avg, debug_range / 2, debug_gray_min, debug_gray_max);

                } else if (rx_state == RXIMAGE) {
                    // IMAGE módban: sor progress megjelenítése
                    float progress = (float)current_line_index / WEFAX_IMAGE_HEIGHT * 100.0f;
                    WEFAX_DEBUG("KÉP %d/%d (%.0f%%) | IOC%d %.0f LPM | Jel: %d [%d-%d]\n", current_line_index, WEFAX_IMAGE_HEIGHT, progress, current_ioc, (phase_lines > 0) ? (lpm_sum / phase_lines) : 120.0f,
                                debug_gray_avg, debug_gray_min, debug_gray_max);
                }
            }
#endif

            // Jelvesztés detektálás IMAGE módban (kombinált logika, WEFAX képekhez optimalizálva)
            static int weak_signal_count = 0;
            if (rx_state == RXIMAGE) {
                bool weak = false;

                // 1. Kis dinamikatartomány ÉS szélsőséges átlag (túl fehér VAGY túl fekete)
                //    WEFAX képek: átlag 235-245, tartomány 0-255 → NORMÁLIS
                //    Gyenge jel fehér: átlag > 250, tartomány < 20 → HIBÁS
                //    Gyenge jel fekete: átlag < 10, tartomány < 20 → HIBÁS
                if (signal_dynamic_range < 20 && (signal_gray_avg > 250 || signal_gray_avg < 10)) {
                    weak = true;
                }

                // 2. Túl sok fekete (> 95%) - nincs kép, csak fekete jel
                //    WEFAX képek: fekete 1-5%, fehér 90-96% → NORMÁLIS
                //    Gyenge jel: fekete > 95% → HIBÁS (nincs adás)
                if (signal_black_ratio > 0.95f) {
                    weak = true;
                }

                // 3. Fekete ÉS fehér arány is nagyon alacsony (középszürke zaj, nincs karakteres jel)
                //    WEFAX képek: fehér 90-96%, fekete 1-5% → NORMÁLIS
                //    Gyenge jel: fehér < 40%, fekete < 5% → HIBÁS
                if (signal_black_ratio < 0.05f && signal_white_ratio < 0.40f) {
                    weak = true;
                }

                // 4. Átlag középszürke ÉS nagy dinamikatartomány (AGC-zaj, hamis tartomány)
                //    WEFAX képek: átlag 235-245 → nem triggerel
                //    AGC-zaj: átlag 60-200, tartomány > 200 → HIBÁS
                if (signal_gray_avg > 60 && signal_gray_avg < 200 && signal_dynamic_range > 200) {
                    weak = true;
                }

                // Ha jelvesztés van
                if (weak) {
                    weak_signal_count++;

                    // Ha a jelvesztés eléri a küszöböt (másodpercben mérünk így a számláló is másodperc alapú lesz)
                    if (weak_signal_count >= WEAK_SIGNAL_IN_SECONDS) {
                        WEFAX_DEBUG("\n-------------------------------------------------\n");
                        WEFAX_DEBUG(" ⚠  JELVESZTÉS - VÉTEL LEÁLLÍTVA\n");
                        WEFAX_DEBUG("-------------------------------------------------\n");
                        WEFAX_DEBUG("Jelstatisztika (%.0f sec gyenge jel): \n", WEAK_SIGNAL_IN_SECONDS);
                        WEFAX_DEBUG(" • Átlag: %d (túl %s)\n", signal_gray_avg, signal_gray_avg > 200 ? "világos" : "sötét");
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
float WefaxDecoderC1::complex_arg_diff(float prev_real, float prev_imag, float curr_real, float curr_imag) {
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
void WefaxDecoderC1::decode_phasing(int gray_value) {
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

    // Ha 10 másodperc után nincs phasing detektálás → fallback
    if (total_phasing_samples > 10 * sample_rate && phase_lines == 0) {
        WEFAX_DEBUG("\n-------------------------------------------------\n");
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
        WEFAX_DEBUG("[WEFAX] Phasing: %.1fs | Fehér: %.1f%% | Fekete: %.1f%% | Fázis: %s | Tartomány: %d-%d\n", elapsed_sec, white_pct, black_pct, phase_high ? "MAGAS" : "ALACSONY", gray_hist_low, gray_hist_high);
#endif

        phasing_status_timer = 0;
        gray_hist_high = 0;
        gray_hist_low = 255;
    }

    // Átmenetek detektálása (ADAPTÍV küszöbök)
    // Fehér kezdet: > 140, Fekete kezdet (SYNC): < 100
    if (gray_value > 140 && !phase_high) {
        // FEKETE → FEHÉR átmenet
        phase_high = true;
        // DEBUG: WEFAX_DEBUG("[WEFAX] >>> Fehér kezdet: gray=%d\n", gray_value);
    } else if (gray_value < 100 && phase_high) {
        // FEHÉR → FEKETE átmenet (sorszinkron!)
        phase_high = false;
        // DEBUG: WEFAX_DEBUG("[WEFAX] <<< Fekete SYNC: gray=%d\n", gray_value);

        // Érvényes phasing sor ellenőrzése (REÁLIS kritériumok WEFAX jelhez)
        // Phasing sor tipikus szerkezete: 5% fehér + 95% fekete, időtartam: 0.4-0.6s
        float white_ratio = (float)curr_phase_high / curr_phase_len;
        float black_ratio = (float)curr_phase_low / curr_phase_len;
        bool valid_ratios = (white_ratio >= 0.02f) && (black_ratio >= 0.30f);
        bool valid_duration = (curr_phase_len >= 0.35f * sample_rate) && (curr_phase_len <= 0.65f * sample_rate);

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
                WEFAX_DEBUG("🔄 AUTOMATIKUS ÚJRAINDÍTÁS: Phasing jel detektálva\n");
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
                WEFAX_DEBUG("🔵 Szinkron jel %d/4 %s | %.1f LPM | Soridő: %.0f ms | F:%.0f%% Sz:%.0f%%\n", //
                            phase_lines,                                                                   //
                            (phase_lines <= 4) ? progress_bar[phase_lines - 1] : "▪▪▪▪+",                  //
                            tmp_lpm,                                                                       //
                            line_time_ms,                                                                  //
                            white_ratio * 100, black_ratio * 100);
#endif

            } else {
                // Outlier detektálva - NEM számítjuk bele az átlagba!
                WEFAX_DEBUG("⚠ Hibás szinkron (%.1f LPM - érvénytelen, 90-300 tartományon kívül)\n", tmp_lpm);
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

            // 6 phasing sor után átváltunk IMAGE módba, DE mérés folytatódik!
            if (phase_lines == 6) {
                WEFAX_DEBUG("\n-------------------------------------------------\n");

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

            } else if (phase_lines > 4 && rx_state == RXIMAGE && valid_lpm) {
                // IMAGE módban folytatjuk a phasing mérést - finomhangoljuk az LPM-et
#if USE_MEASURED_LPM
                // MÉRT mód: frissítjük a samples_per_line értéket AZONNAL!
                samples_per_line = sample_rate * 60.0f / avg_lpm;
                WEFAX_DEBUG("🔧 Finomhangolás #%d: %.1f LPM → %.0f minta/sor (frissítve)\n", phase_lines, avg_lpm, samples_per_line);
#else
                // FIX mód: csak logoljuk, nem frissítünk
                WEFAX_DEBUG("ℹ Szinkron #%d: %.1f LPM detektálva (FIX 500ms használatban)\n", phase_lines, avg_lpm);
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
void WefaxDecoderC1::decode_image(int gray_value, uint16_t *current_line_idx) {
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
                WEFAX_DEBUG("⚠ BUFFER TELE! Sor #%d elveszett (Core0 lassú?)\n", *current_line_idx);
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
}
