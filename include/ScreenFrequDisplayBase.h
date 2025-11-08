#pragma once

#include "UICompSevenSegmentFreq.h"
#include "UIScreen.h"

/**
 * @class ScreenFrequDisplayBase
 * @brief Alap UIScreen osztály frekvencia kijelző komponenssel
 * @details Ez az absztrakciós réteg az UIScreen és a konkrét frekvencia kijelzőt használó képernyők között.
 *
 * **Fő funkciók:**
 * - UICompSevenSegmentFreq komponens integrációja
 *
 * **Örökölhető osztályok:**
 * - ScreenRadioBase - Rádió vezérlő képernyők alaposztálya
 */
class ScreenFrequDisplayBase : public UIScreen {
  protected:
    // Frekvencia kijelző komponens
    std::shared_ptr<UICompSevenSegmentFreq> sevenSegmentFreq;

    /**
     * @brief Létrehozza a frekvencia kijelző komponenst
     * @param freqBounds A frekvencia kijelző határai (Rect)
     */
    inline void createSevenSegmentFreq(Rect freqBounds) {
        sevenSegmentFreq = std::make_shared<UICompSevenSegmentFreq>(freqBounds);
        addChild(sevenSegmentFreq);
    }

  public:
    ScreenFrequDisplayBase(const char *name) : UIScreen(name) {}

    /**
     * @brief Frekvencia kijelző komponens lekérése
     */
    inline std::shared_ptr<UICompSevenSegmentFreq> getSevenSegmentFreq() const { return sevenSegmentFreq; }
};