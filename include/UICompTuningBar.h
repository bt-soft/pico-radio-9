/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio
 * File: UICompTuningBar.h
 * Created Date: 2025.12.22.
 *
 * Author: BT-Soft
 * GitHub: https://github.com/bt-soft
 * Blog: https://electrodiy.blog.hu/
 * -----
 * Copyright (c) 2025 BT-Soft
 * License: MIT License
 * -----
 * DESCRIPTION:
 * Kompakt spektrum bar hangolási segéd WeFax és SSTV dekóderekhez.
 * Valós idejű FFT-alapú függőleges bar graph célfrekvencia jelölőkkel.
 */

#pragma once

#include "UIComponent.h"
#include "decoder_api.h"
#include <TFT_eSPI.h>
#include <vector>

/**
 * @brief Kompakt hangolási spektrum bar komponens
 * @details Függőleges bar graph megjelenítés FFT adatokból, célfrekvencia marker vonalakkal
 */
class UICompTuningBar : public UIComponent {
  public:
    /**
     * @brief Célfrekvencia marker definíció
     */
    struct FrequencyMarker {
        uint16_t frequencyHz; // Célfrekvencia (Hz)
        uint16_t color;       // Marker szín (RGB565)
        const char *label;    // Opcionális felirat (nullptr = nincs)
    };

    /**
     * @brief Konstruktor
     * @param bounds Komponens pozíció és méret
     * @param minFreqHz Minimum frekvencia (Hz)
     * @param maxFreqHz Maximum frekvencia (Hz)
     * @param gain Erősítési tényező (1.0 = alapértelmezett, 2.0 = dupla érzékenység, stb.)
     */
    UICompTuningBar(const Rect &bounds, uint16_t minFreqHz, uint16_t maxFreqHz, float gain = 4.0f);

    /**
     * @brief Destruktor - sprite felszabadítása
     */
    ~UICompTuningBar();

    /**
     * @brief Frekvencia marker hozzáadása
     * @param frequencyHz Célfrekvencia (Hz)
     * @param color Marker szín (RGB565)
     * @param label Opcionális felirat
     */
    void addMarker(uint16_t frequencyHz, uint16_t color, const char *label = nullptr);

    /**
     * @brief Spektrum adatok frissítése
     * @param fftData FFT spektrum tömb (q15_t formátum: -32768..32767)
     * @param fftSize FFT spektrum méret
     * @param binWidthHz FFT bin szélesség (Hz/bin)
     */
    void updateSpectrum(const int16_t *fftData, uint16_t fftSize, float binWidthHz);

    /**
     * @brief Komponens kirajzolása
     * @param tft TFT display objektum
     */
    void draw(TFT_eSPI &tft);

    /**
     * @brief UIComponent pure virtual draw() implementáció
     * @details Átirányítja a hívást a tft paraméterrel rendelkező draw()-ra
     */
    virtual void draw() override {
        // Nem használjuk, mert nekünk TFT referencia kell
        // A képernyők közvetlenül a draw(tft) verziót hívják
    }

  private:
    uint16_t minFreqHz;                    // Minimum frekvencia
    uint16_t maxFreqHz;                    // Maximum frekvencia
    float gain;                            // Erősítési tényező (érzékenység szabályozás)
    std::vector<FrequencyMarker> markers;  // Frekvencia markerek
    std::vector<uint8_t> barHeights;       // Bar magasságok (cache)
    std::vector<uint8_t> displayedHeights; // Megjelenített magasságok (peak hold + decay)
    std::vector<uint8_t> peakHoldCounters; // Peak hold számlálók
    float currentBinWidthHz;               // Aktuális FFT bin szélesség
    bool needsRedraw;                      // Újrarajzolás szükséges flag
    TFT_eSprite *sprite;                   // Off-screen buffer a villogás elkerülésére
    bool spriteCreated;                    // Sprite inicializálva flag
    uint32_t lastUpdateMs;                 // Utolsó frissítés ideje (FPS limitálás)

    /**
     * @brief Frekvencia -> képernyő X koordináta konverzió
     * @param frequencyHz Frekvencia (Hz)
     * @return X koordináta (képernyő pixel)
     */
    int16_t frequencyToX(uint16_t frequencyHz) const;

    /**
     * @brief FFT bin index kiszámítása frekvenciából
     * @param frequencyHz Frekvencia (Hz)
     * @return FFT bin index
     */
    uint16_t frequencyToBin(uint16_t frequencyHz) const;
};
