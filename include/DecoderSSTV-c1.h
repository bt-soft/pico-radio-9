#pragma once

#include <memory>

#include "IDecoder.h"
#include "decode_sstv.h"

/**
 * @brief SSTV dekóder osztály - core1 számára
 */
class DecoderSSTV_C1 : public IDecoder {

  public:
    /**
     * Konstruktor
     * @param samplingRate A bemeneti audio mintavételezési sebesség Hz-ben.
     */
    DecoderSSTV_C1();

    /**
     * Destruktor
     */
    ~DecoderSSTV_C1() = default;

    /**
     * @brief Dekóder neve
     * @return A dekóder neve stringként
     */
    const char *getDecoderName() const override { return "SSTV"; }

    /**
     * @brief Indítás / inicializáció
     * @param decoderConfig A dekóder konfigurációs struktúrája
     * @return true ha sikerült elindítani
     */
    bool start(const DecoderConfig &decoderConfig) override;

    /**
     * @brief Leállítás és takarítás
     */
    void stop() override;

    /**
     * @brief Feldolgozza a bemeneti audio mintákat és dekódolja az SSTV képet.
     * @param rawAudioSamples A bemeneti audio minták tömbje (SSTV_RAW_SAMPLES_SIZE elem).
     * @param count A minták száma.
     */
    void processSamples(const int16_t *rawAudioSamples, size_t count) override;

  private:
    //--- SSTV Dekóder ---
    std::unique_ptr<c_sstv_decoder> sstv_decoder; // SSTV dekóder objektum, 15kHz mintavételezésre van optimalizálva!!
    uint16_t last_pixel_y = 0;                    // Az utoljára dekódolt pixel_y pozíció
    uint8_t line_rgb[320][4];                     // Az aktuális sor RGB és Cr,Cb értékei
    bool first_image_sent = false;                // Jelző, hogy az első kép értesítése megtörtént-e

    int8_t last_mode_id = -1; // Az utoljára jelzett mód azonosító

    /**
     * @brief Egy sort feltol a line ring-be
     * @param src Forrás pixel tömb (hossz: LineBufferRing::WIDTH)
     * @param y Rajzolási y koordináta
     * @return true ha sikerült, false ha a ring tele volt
     */
    bool pushLineToBuffer(const uint16_t *src, uint16_t y);
};