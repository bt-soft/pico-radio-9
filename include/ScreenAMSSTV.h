#pragma once

#include "ScreenAMRadioBase.h"
#include "UICommonVerticalButtons.h"
#include "UICompTextBox.h"

/**
 * @brief AM SSTV dekóder képernyő
 * @details SSTV dekóder megjelenítése és kezelése
 */
class ScreenAMSSTV : public ScreenAMRadioBase, public UICommonVerticalButtons::Mixin<ScreenAMSSTV> {

  public:
    /**
     * @brief Konstruktor
     */
    ScreenAMSSTV();

    /**
     * @brief Destruktor
     */
    virtual ~ScreenAMSSTV() override;

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
     * @brief SSTV specifikus gombok hozzáadása a közös AM gombokhoz
     * @param buttonConfigs A már meglévő gomb konfigurációk vektora
     * @details Hozzáadja a SSTV-specifikus gombokat a vízszintes gombsorhoz
     */
    virtual void addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) override;

  private:
    // Static változók a függőleges scaling akkumulálásához
    float accumulatedTargetLine;
    uint16_t lastDrawnTargetLine;

    void checkDecodedData();
    void clearPictureArea();
};
