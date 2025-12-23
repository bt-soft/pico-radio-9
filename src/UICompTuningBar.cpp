/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio
 * File: UICompTuningBar.cpp
 * Created Date: 2025.12.22.
 *
 * Author: BT-Soft
 * GitHub: https://github.com/bt-soft
 * Blog: https://electrodiy.blog.hu/
 * -----
 * Copyright (c) 2025 BT-Soft
 * License: MIT License
 */

#include "UICompTuningBar.h"
#include <algorithm>

UICompTuningBar::UICompTuningBar(const Rect &bounds, uint16_t minFreqHz, uint16_t maxFreqHz)
    : UIComponent(bounds), minFreqHz(minFreqHz), maxFreqHz(maxFreqHz), currentBinWidthHz(0.0f), needsRedraw(true), sprite(nullptr), spriteCreated(false),
      lastUpdateMs(0) {
    // Bar heights cache inicializálása
    barHeights.reserve(bounds.width);
    displayedHeights.reserve(bounds.width);
    peakHoldCounters.reserve(bounds.width);
}

UICompTuningBar::~UICompTuningBar() {
    if (sprite != nullptr) {
        sprite->deleteSprite();
        delete sprite;
        sprite = nullptr;
    }
}

void UICompTuningBar::addMarker(uint16_t frequencyHz, uint16_t color, const char *label) {
    markers.push_back({frequencyHz, color, label});
    needsRedraw = true;
}

void UICompTuningBar::updateSpectrum(const int16_t *fftData, uint16_t fftSize, float binWidthHz) {
    if (fftData == nullptr || fftSize == 0 || binWidthHz <= 0.0f) {
        return;
    }

    currentBinWidthHz = binWidthHz;
    barHeights.clear();
    barHeights.reserve(bounds.width);

    // Képernyő X pixel -> frekvencia -> FFT bin mapping
    for (int16_t x = 0; x < bounds.width; x++) {
        // X koordináta -> frekvencia
        float freq = minFreqHz + (float)x * (maxFreqHz - minFreqHz) / bounds.width;

        // Frekvencia -> FFT bin index
        uint16_t binIndex = (uint16_t)(freq / binWidthHz);

        if (binIndex < fftSize) {
            // FFT magnitúdó (q15_t: -32768..32767) -> bar magasság (0-bounds.height)
            // q15 típus: abszolút érték + skálázás 0-255 tartományba
            // 4x gain alkalmazva a jobb láthatóságért
            int16_t q15Value = fftData[binIndex];
            uint16_t absValue = (q15Value < 0) ? -q15Value : q15Value; // Abszolút érték
            uint32_t scaledValue = (uint32_t)absValue * 4;             // 4x gain
            if (scaledValue > 32767)
                scaledValue = 32767;                                    // Clipping
            uint8_t magnitude = (uint8_t)((scaledValue * 255) / 32767); // Skálázás 0-255-re
            uint8_t barHeight = (uint8_t)((magnitude * bounds.height) / 255);
            barHeights.push_back(barHeight);
        } else {
            barHeights.push_back(0);
        }
    }

    needsRedraw = true;
}

void UICompTuningBar::draw(TFT_eSPI &tft) {
    // FPS limitálás: 25 FPS (40ms/frame)
    constexpr uint32_t FRAME_TIME_MS = 40;
    uint32_t currentMs = millis();
    if (currentMs - lastUpdateMs < FRAME_TIME_MS) {
        return; // Még nem telt el elég idő
    }
    lastUpdateMs = currentMs;

    // Sprite létrehozása első rajzoláskor
    if (!spriteCreated) {
        sprite = new TFT_eSprite(&tft);
        if (sprite->createSprite(bounds.width, bounds.height)) {
            spriteCreated = true;
        } else {
            // Ha nem sikerült a sprite létrehozás, direktben rajzolunk
            delete sprite;
            sprite = nullptr;
            spriteCreated = false;
        }
    }

    if (!needsRedraw) {
        return;
    }

    // Ha van sprite, abba rajzolunk, különben közvetlenül a képernyőre
    TFT_eSPI *drawTarget = (sprite != nullptr) ? (TFT_eSPI *)sprite : &tft;
    int16_t offsetX = (sprite != nullptr) ? 0 : bounds.x;
    int16_t offsetY = (sprite != nullptr) ? 0 : bounds.y;

    // Háttér törlése
    drawTarget->fillRect(offsetX, offsetY, bounds.width, bounds.height, TFT_BLACK);

    // Keret rajzolása
    drawTarget->drawRect(offsetX, offsetY, bounds.width, bounds.height, TFT_DARKGREY);

    // Peak hold konstansok
    constexpr uint8_t PEAK_HOLD_FRAMES = 15; // ~600ms tartás 25 FPS-nél
    constexpr uint8_t DECAY_SPEED = 1;       // Lassú esés sebessége

    // Bar graph rajzolása (függőleges oszlopok) peak hold logikával
    for (size_t i = 0; i < barHeights.size(); i++) {
        // Peak hold logika: új érték >= megjelenített -> frissítés
        if (barHeights[i] >= displayedHeights[i]) {
            displayedHeights[i] = barHeights[i];
            peakHoldCounters[i] = PEAK_HOLD_FRAMES; // Reset hold timer
        } else {
            // Decay: csak akkor csökken, ha a hold timer lejárt
            if (peakHoldCounters[i] > 0) {
                peakHoldCounters[i]--; // Hold phase - tartás
            } else if (displayedHeights[i] > 0) {
                // Lassú esés
                displayedHeights[i] = (displayedHeights[i] > DECAY_SPEED) ? (displayedHeights[i] - DECAY_SPEED) : 0;
            }
        }

        // Bar rajzolása a displayedHeights alapján
        if (displayedHeights[i] > 0) {
            int16_t x = offsetX + i;
            int16_t barTop = offsetY + bounds.height - displayedHeights[i];
            int16_t barBottom = offsetY + bounds.height - 1;

            // Színkódolás: magasság alapján (zöld->sárga->piros)
            uint16_t barColor;
            if (displayedHeights[i] < bounds.height / 3) {
                barColor = TFT_GREEN;
            } else if (displayedHeights[i] < 2 * bounds.height / 3) {
                barColor = TFT_YELLOW;
            } else {
                barColor = TFT_RED;
            }

            // Függőleges vonal rajzolása
            drawTarget->drawFastVLine(x, barTop, barBottom - barTop + 1, barColor);
        }
    }

    // Frekvencia markerek rajzolása (függőleges vonalak)
    for (const auto &marker : markers) {
        int16_t markerX = frequencyToX(marker.frequencyHz);
        int16_t relativeMarkerX = markerX - bounds.x;

        if (relativeMarkerX >= 0 && relativeMarkerX < bounds.width) {
            // Szaggatott függőleges vonal (marker)
            for (int16_t y = 2; y < bounds.height - 2; y += 3) {
                drawTarget->drawPixel(offsetX + relativeMarkerX, offsetY + y, marker.color);
            }

            // Opcionális felirat (ha van hely)
            if (marker.label != nullptr && bounds.height > 12) {
                drawTarget->setTextColor(marker.color, TFT_BLACK);
                drawTarget->setTextDatum(TC_DATUM);
                drawTarget->setTextFont(0);
                drawTarget->setTextSize(1);
                drawTarget->drawString(marker.label, offsetX + relativeMarkerX, offsetY + 1);
            }
        }
    }

    // Sprite push a képernyőre (ha használjuk)
    if (sprite != nullptr) {
        sprite->pushSprite(bounds.x, bounds.y);
    }

    needsRedraw = false;
}

int16_t UICompTuningBar::frequencyToX(uint16_t frequencyHz) const {
    if (frequencyHz < minFreqHz || frequencyHz > maxFreqHz) {
        return -1; // Tartományon kívül
    }

    float normalized = (float)(frequencyHz - minFreqHz) / (maxFreqHz - minFreqHz);
    return bounds.x + (int16_t)(normalized * bounds.width);
}

uint16_t UICompTuningBar::frequencyToBin(uint16_t frequencyHz) const {
    if (currentBinWidthHz <= 0.0f) {
        return 0;
    }
    return (uint16_t)(frequencyHz / currentBinWidthHz);
}
