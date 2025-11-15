#pragma once

#include "ScreenAMRadioBase.h"
#include "UICommonVerticalButtons.h"
#include "UICompTextBox.h"

/**
 * @brief AM WeFax dekóder képernyő
 * @details WeFax dekóder megjelenítése és kezelése
 */
class ScreenAMWeFax : public ScreenAMRadioBase, public UICommonVerticalButtons::Mixin<ScreenAMWeFax> {

  public:
    /**
     * @brief Konstruktor
     */
    ScreenAMWeFax();

    /**
     * @brief Destruktor
     */
    virtual ~ScreenAMWeFax() override;

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
    // WEFAX mód változás ellenőrzése
    uint8_t cachedMode;
    uint16_t cachedDisplayWidth;
    uint16_t displayWidth;
    uint16_t sourceWidth;
    uint16_t sourceHeight;
    float scale;
    uint16_t targetHeight;
#define WEFAX_MAX_DISPLAY_WIDTH 800
    uint16_t displayBuffer[WEFAX_MAX_DISPLAY_WIDTH];
    float accumulatedTargetLine;
    uint16_t lastDrawnTargetLine;

    void clearPictureArea();
    void checkDecodedData();
};
