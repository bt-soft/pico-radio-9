#pragma once

#include "ScreenAMRadioBase.h"
#include "UICommonVerticalButtons.h"
#include "UICompTextBox.h"

/**
 * @brief AM CW dekóder képernyő
 * @details CW (Morse) dekóder megjelenítése és kezelése
 */
class ScreenAMCW : public ScreenAMRadioBase, public UICommonVerticalButtons::Mixin<ScreenAMCW> {

  public:
    /**
     * @brief Konstruktor
     */
    ScreenAMCW();

    /**
     * @brief Destruktor
     */
    virtual ~ScreenAMCW() override;

    /**
     * @brief Képernyő aktiválása
     */
    virtual void activate() override;

    /**
     * @brief Képernyő deaktiválása
     */
    virtual void deactivate() override;

    /**
     * @brief Folyamatos loop hívás
     */
    virtual void handleOwnLoop() override;

  protected:
    /**
     * @brief UI komponensek létrehozása és képernyőn való elhelyezése
     */
    void layoutComponents();

    /**
     * @brief CW specifikus gombok hozzáadása a közös AM gombokhoz
     * @param buttonConfigs A már meglévő gomb konfigurációk vektora
     * @details Hozzáadja a CW-specifikus gombokat (pl. WPM, Speed, stb.)
     */
    virtual void addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) override;

  private:
    std::shared_ptr<UICompTextBox> cwTextBox; ///< CW dekódolt szöveg megjelenítése

    /**
     * @brief CW dekódolt szöveg ellenőrzése és frissítése
     */
    void checkDecodedData();

    uint8_t lastPublishedCwWpm;
    uint16_t lastPublishedCwFreq;
};
