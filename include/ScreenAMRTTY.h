#pragma once

#include "ScreenAMRadioBase.h"
#include "UICommonVerticalButtons.h"
#include "UICompTextBox.h"

/**
 * @brief AM CW dekóder képernyő
 * @details CW (Morse) dekóder megjelenítése és kezelése
 */
class ScreenAMRTTY : public ScreenAMRadioBase, public UICommonVerticalButtons::Mixin<ScreenAMRTTY> {

  public:
    /**
     * @brief Konstruktor
     */
    ScreenAMRTTY();

    /**
     * @brief Destruktor
     */
    virtual ~ScreenAMRTTY() override;

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
    std::shared_ptr<UICompTextBox> rttyTextBox; ///< RTTY dekódolt szöveg megjelenítése

    /**
     * @brief RTTY dekódolt szöveg ellenőrzése és frissítése
     */
    void checkDecodedData();

    uint16_t lastPublishedRttyMark;
    uint16_t lastPublishedRttySpace;
    float lastPublishedRttyBaud;
};
