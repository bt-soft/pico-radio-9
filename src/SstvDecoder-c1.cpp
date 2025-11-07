/**
 * @file SstvDecoder-c1.cpp
 * @brief SSTV dekóder implementációja Core-1 számára
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 *
 * inspired by: 1001 things, https://github.com/dawsonjon/PicoSSTV
 */
#include <cstring>

#include "SstvDecoder-c1.h"
#include "defines.h"

#if defined(__DEBUG) && defined(__SSTV_DEBUG)
#define SSTV_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define SSTV_DEBUG(fmt, ...) // Üres makró, ha az __DEBUG nincs definiálva
#endif

// Nyújtás beállításai
#define STRETCH true

// Ferdeség korrekció beállításai
#define ENABLE_SLANT_CORRECTION true

// Jelvesztés időtúllépés másodpercekben
#define SSTV_LOST_SIGNAL_TIMEOUT_SECONDS 25

// A színek előállítása RGB565 formátumban makróval (hasonló a TFT könyvtáréhoz)
#ifndef COLOR565
#define COLOR565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#endif

extern DecodedData decodedData;

/**
 * @brief Konstruktor
 */
SstvDecoderC1::SstvDecoderC1() {}

/**
 * @brief Egy sor pixelt feltol a line ring-be
 * @param src forrás pixel tömb RGB565 formátumban (hossz: SSTV_LINE_WIDTH)
 * @param y a kirajzoláshoz használatos y koordináta
 * @return true ha sikerült a foglalás+másolás+commit, false ha a ring tele volt
 */
bool SstvDecoderC1::pushLineToBuffer(const uint16_t *src, uint16_t y) {
    DecodedLine newLine;
    newLine.lineNum = y;

    // Másoljuk át a pixeleket az sstvPixels tömbbe (RGB565 formátum)
    memcpy(newLine.sstvPixels, src, SSTV_LINE_WIDTH * sizeof(uint16_t));

    // Próbáljuk meg hozzáadni az új sort a közös lineBuffer-hez
    if (!decodedData.lineBuffer.put(newLine)) {
        SSTV_DEBUG("SstvDecoderC1::pushLineToBuffer - Ring buffer FULL, y=%d\n", y);
        return false;
    }

    SSTV_DEBUG("SstvDecoderC1::pushLineToBuffer - Successfully pushed line y=%d\n", y);
    return true;
}

/**
 * @brief Indítás / inicializáció
 * @param decoderConfig A dekóder konfigurációs struktúrája
 * @return true ha sikerült elindítani
 */
bool SstvDecoderC1::start(const DecoderConfig &decoderConfig) {

    // Ha van már dekóder, akkor azt töröljük
    if (sstv_decoder != nullptr) {
        sstv_decoder.reset();
    }

    // Debug: mindig írjuk ki a fontos indítási paramétereket a fő logra
    DEBUG("core-1: SSTV dekóder inicializálása samplingRate=%u, sampleCount=%u\n", decoderConfig.samplingRate, decoderConfig.sampleCount);

    // Létrehozzuk az SSTV dekódert a megadott mintavételezési frekvenciával
    // sstv_decoder = std::make_unique<c_sstv_decoder>(decoderConfig.samplingRate);
    sstv_decoder = std::make_unique<c_sstv_decoder>(static_cast<float>(decoderConfig.samplingRate)); // Korábban a dekóderbe be volt "bevasalva" egy 15kHz-es Fs; most a futó samplingRate-et adjuk meg.

    // Beállítjuk az elcsúsztatás korrekciót
    sstv_decoder->set_auto_slant_correction(ENABLE_SLANT_CORRECTION);
    // Beállítjuk a jelvesztés időtúllépést
    sstv_decoder->set_timeout_seconds(SSTV_LOST_SIGNAL_TIMEOUT_SECONDS);

    // Inicializáljuk a line_rgb tömböt
    for (int x = 0; x < 320; x++) {
        for (int c = 0; c < 4; c++) {
            line_rgb[x][c] = 0;
        }
    }

    return true;
};

/**
 * @brief Leállítás és takarítás
 */
void SstvDecoderC1::stop() {
    // Ha van már dekóder, akkor azt töröljük
    if (sstv_decoder != nullptr) {
        sstv_decoder.reset();
    }
};

/**
 * @brief Feldolgozza a bemeneti audio mintákat és dekódolja az SSTV képet.
 * @param rawAudioSamples A bemeneti audio minták tömbje (SSTV_RAW_SAMPLES_SIZE elem).
 * @param count A minták száma.
 */
void SstvDecoderC1::processSamples(const int16_t *rawAudioSamples, size_t count) {

    // Rövid diagnosztika: jelezzük, hogy kaptunk-e adatot a dekódernek
    if (count == 0) {
        return;
    }

    for (uint16_t idx = 0; idx < count; idx++) {

        int16_t rawSample = rawAudioSamples[idx];

        uint16_t pixel_y;
        uint16_t pixel_x;
        uint8_t pixel_colour;
        uint8_t pixel;
        int16_t frequency;

        if (sstv_decoder == nullptr) {
            DEBUG("SSTV: HIBA - sstv_decoder NULL processSamples közben\n");
            return;
        }

        if (sstv_decoder->decode_audio(rawSample, pixel_y, pixel_x, pixel_colour, pixel, frequency)) {
            // Debug log minden találatra (lehet sok, de most kell a hibakereséshez)
            SSTV_DEBUG("SSTV: decode_audio HIT pixel_y=%u pixel_x=%u colour=%u pixel=%u freq=%d\n", (unsigned)pixel_y, (unsigned)pixel_x, (unsigned)pixel_colour, (unsigned)pixel, frequency);

            c_sstv_decoder::e_mode mode = sstv_decoder->get_mode();
            // Ha a felismerés módban változás történt (beleértve az első felismerést is),
            // értesítsük azonnal a consumer-t, hogy a Core0 megjeleníthesse az
            // első képhez tartozó módot.
            int8_t mode_id_now = (int8_t)mode;
            if (mode_id_now != last_mode_id) {
                last_mode_id = mode_id_now;
                // Jelöljük a shared memory-ban hogy mód változott
                decodedData.modeChanged = true;
                decodedData.currentMode = (uint8_t)mode_id_now;

                SSTV_DEBUG("SstvDecoderC1: Módváltozás észlelve, új mode_id=%d, név=%s\n", //
                           mode_id_now,                                                    //
                           c_sstv_decoder::getSstvModeName((c_sstv_decoder::e_mode)mode_id_now));
            }

            // Ha új kép kezdődött (a sor számláló 0-ra visszatekert), értesítsük a consumer-t (Core0)
            // Kezeljük a speciális esetet is: ha még sosem küldtünk első képet (kezdeti állapot),
            // akkor az első pixel_y==0 helyzetet is vegyük új kép kezdetnek, hogy a banner és a
            // képterület törlése megtörténjen.
            if ((pixel_y == 0 && last_pixel_y != 0) || (pixel_y == 0 && !first_image_sent)) {

                SSTV_DEBUG("SstvDecoderC1: Új kép kezdődik, pixel_y=0, mode_id=%d, név=%s\n", //
                           (uint8_t)mode,                                                     //
                           c_sstv_decoder::getSstvModeName((c_sstv_decoder::e_mode)mode_id_now));

                // Jelöljük a shared memory-ban hogy új kép kezdődött
                decodedData.newImageStarted = true;

                // Jelöljük, hogy az első kép értesítése megtörtént
                first_image_sent = true;

                // Tisztítsuk meg a line_rgb tömböt új kép kezdetekor
                for (int x = 0; x < 320; x++) {
                    for (int c = 0; c < 4; c++) {
                        line_rgb[x][c] = 0;
                    }
                }
                // DEBUG("SstvDecoder: A line_rgb tömb tisztítva új képhez\n");
            }

            if (pixel_y > last_pixel_y) {
                uint16_t line_rgb565[320];
                uint16_t scaled_pixel_y = 0;

                if (mode == c_sstv_decoder::pd_50 || mode == c_sstv_decoder::pd_90 || mode == c_sstv_decoder::pd_120 || mode == c_sstv_decoder::pd_180) {
                    if (mode == c_sstv_decoder::pd_120 || mode == c_sstv_decoder::pd_180) {
                        scaled_pixel_y = (uint32_t)last_pixel_y * 240 / 496;
                    } else {
                        scaled_pixel_y = last_pixel_y;
                    }

                    for (uint16_t x = 0; x < 320; ++x) {
                        int16_t y = line_rgb[x][0];
                        int16_t cr = line_rgb[x][1];
                        int16_t cb = line_rgb[x][2];

                        // Ellenőrizzük hogy vannak-e érvényes értékek
                        if (y == 0 && cr == 0 && cb == 0) {
                            // Ha még nincs pixel adat, használjunk középszürke alapértelmezést
                            y = 128;
                            cr = 128;
                            cb = 128;
                        }

                        cr = cr - 128;
                        cb = cb - 128;
                        int16_t r = y + 45 * cr / 32;
                        int16_t g = y - (11 * cb + 23 * cr) / 32;
                        int16_t b = y + 113 * cb / 64;
                        r = r < 0 ? 0 : (r > 255 ? 255 : r);
                        g = g < 0 ? 0 : (g > 255 ? 255 : g);
                        b = b < 0 ? 0 : (b > 255 ? 255 : b);
                        line_rgb565[x] = COLOR565(r, g, b);
                    }

                    // Első pixel-sor commitolása
                    if (!pushLineToBuffer(line_rgb565, scaled_pixel_y * 2)) {
                        SSTV_DEBUG("SstvDecoderC1: HIBA - Ring buffer tele, nem sikerült elküldeni a PD sort\n");
                    }

                    for (uint16_t x = 0; x < 320; ++x) {
                        int16_t y = line_rgb[x][3];
                        int16_t cr = line_rgb[x][1];
                        int16_t cb = line_rgb[x][2];
                        cr = cr - 128;
                        cb = cb - 128;
                        int16_t r = y + 45 * cr / 32;
                        int16_t g = y - (11 * cb + 23 * cr) / 32;
                        int16_t b = y + 113 * cb / 64;
                        r = r < 0 ? 0 : (r > 255 ? 255 : r);
                        g = g < 0 ? 0 : (g > 255 ? 255 : g);
                        b = b < 0 ? 0 : (b > 255 ? 255 : b);
                        line_rgb565[x] = COLOR565(r, g, b);
                    }
                    // Második pixel-sor commitolása
                    if (!pushLineToBuffer(line_rgb565, scaled_pixel_y * 2 + 1)) {
                        // ha tele, nem csinálunk semmit (log már fent van)
                    }
                } else if (mode == c_sstv_decoder::bw8 || mode == c_sstv_decoder::bw12) {
                    for (uint16_t x = 0; x < 320; ++x) {
                        int16_t r = line_rgb[x][0];
                        int16_t g = line_rgb[x][0];
                        int16_t b = line_rgb[x][0];

                        line_rgb565[x] = COLOR565(r, g, b);
                    }
                    // Két sor commit a ring-be a BW módnál
                    if (!pushLineToBuffer(line_rgb565, last_pixel_y * 2)) {
                        SSTV_DEBUG("SstvDecoderC1: HIBA - Ring buffer tele (BW0)\n");
                    }
                    if (!pushLineToBuffer(line_rgb565, last_pixel_y * 2 + 1)) {
                        SSTV_DEBUG("SstvDecoderC1: HIBA - Ring buffer tele (BW1)\n");
                    }
                } else if (mode == c_sstv_decoder::robot24 || mode == c_sstv_decoder::robot72) {
                    for (uint16_t x = 0; x < 320; ++x) {
                        int16_t y = line_rgb[x][0];
                        int16_t cr = line_rgb[x][1];
                        int16_t cb = line_rgb[x][2];

                        cr = cr - 128;
                        cb = cb - 128;
                        int16_t r = y + 45 * cr / 32;
                        int16_t g = y - (11 * cb + 23 * cr) / 32;
                        int16_t b = y + 113 * cb / 64;
                        r = r < 0 ? 0 : (r > 255 ? 255 : r);
                        g = g < 0 ? 0 : (g > 255 ? 255 : g);
                        b = b < 0 ? 0 : (b > 255 ? 255 : b);

                        line_rgb565[x] = COLOR565(r, g, b);
                    }
                    if (mode == c_sstv_decoder::robot24) {
                        // Robot24: két sor
                        if (!pushLineToBuffer(line_rgb565, last_pixel_y * 2)) {
                            SSTV_DEBUG("SstvDecoderC1: HIBA - Ring buffer tele (R24_0)\n");
                        }
                        if (!pushLineToBuffer(line_rgb565, last_pixel_y * 2 + 1)) {
                            SSTV_DEBUG("SstvDecoderC1: HIBA - Ring buffer tele (R24_1)\n");
                        }
                    } else {
                        // Robot72: egy sor
                        if (!pushLineToBuffer(line_rgb565, last_pixel_y)) {
                            SSTV_DEBUG("SstvDecoderC1: HIBA - Ring buffer tele (R72)\n");
                        }
                    }
                } else if (mode == c_sstv_decoder::robot36) {
                    // Krominancia fázis detektálása
                    uint8_t count = 0;
                    for (uint16_t x = 0; x < 40; ++x) {
                        if (line_rgb[x][3] > 128) {
                            count++;
                        }
                    }

                    uint8_t crc = 2;
                    uint8_t cbc = 1;

                    if ((count < 20 && (last_pixel_y % 2 == 0)) || (count > 20) && (last_pixel_y % 2 == 1)) {
                        crc = 1;
                        cbc = 2;
                    }

                    for (uint16_t x = 0; x < 320; ++x) {
                        int16_t y = line_rgb[x][0];
                        int16_t cr = line_rgb[x][crc];
                        int16_t cb = line_rgb[x][cbc];

                        cr = cr - 128;
                        cb = cb - 128;
                        int16_t r = y + 45 * cr / 32;
                        int16_t g = y - (11 * cb + 23 * cr) / 32;
                        int16_t b = y + 113 * cb / 64;
                        r = r < 0 ? 0 : (r > 255 ? 255 : r);
                        g = g < 0 ? 0 : (g > 255 ? 255 : g);
                        b = b < 0 ? 0 : (b > 255 ? 255 : b);

                        line_rgb565[x] = COLOR565(r, g, b);
                    }
                    // Robot36: egy sor commit
                    if (!pushLineToBuffer(line_rgb565, last_pixel_y)) {
                        SSTV_DEBUG("SstvDecoderC1: HIBA - Ring buffer tele (R36)\n");
                    }
                } else {
                    for (uint16_t x = 0; x < 320; ++x) {
                        line_rgb565[x] = COLOR565(line_rgb[x][0], line_rgb[x][1], line_rgb[x][2]);
                    }
                    // Általános színes mód: másoljuk át a sor pixeleit a LineBufferRing-be és commitoljuk
                    if (!pushLineToBuffer(line_rgb565, last_pixel_y)) {
                        SSTV_DEBUG("SstvDecoderC1: HIBA - A Ring buffer tele, sor eldobva:  %d\n", last_pixel_y);
                    }
                }

                for (uint16_t x = 0; x < 320; ++x) {
                    line_rgb[x][0] = 0;
                    // Robot36 cr and cb must persist 2 lines
                    if (mode != c_sstv_decoder::robot36) {
                        line_rgb[x][1] = line_rgb[x][2] = 0;
                    }
                }
            }
            last_pixel_y = pixel_y;

            if (pixel_x < 320 && pixel_y < 256 && pixel_colour < 4) {
                if (STRETCH && sstv_decoder->get_modes()[mode].width == 160) {
                    if (pixel_x < 160) {
                        line_rgb[pixel_x * 2][pixel_colour] = pixel;
                        line_rgb[pixel_x * 2 + 1][pixel_colour] = pixel;
                    }
                } else {
                    line_rgb[pixel_x][pixel_colour] = pixel;
                }
            }
        }
    }
}