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
     * @brief Statikus képernyő tartalom kirajzolása
     */
    virtual void drawContent() override;

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
     * @brief CW specifikus gombok állapotának frissítése
     */
    void updateHorizontalButtonStates();

  private:
    std::shared_ptr<UICompTextBox> cwTextBox; ///< CW dekódolt szöveg megjelenítése
};
