/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UIHorizontalButtonBar.h                                                                                       *
 * Created Date: 2025.11.08.                                                                                           *
 *                                                                                                                     *
 * Author: BT-Soft                                                                                                     *
 * GitHub: https://github.com/bt-soft                                                                                  *
 * Blog: https://electrodiy.blog.hu/                                                                                   *
 * -----                                                                                                               *
 * Copyright (c) 2025 BT-Soft                                                                                          *
 * License: MIT License                                                                                                *
 * 	Bárki szabadon használhatja, módosíthatja, terjeszthet, beépítheti más                                             *
 * 	projektbe (akár zártkódúba is), akár pénzt is kereshet vele                                                        *
 * 	Egyetlen feltétel:                                                                                                 *
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                       *
 * -----                                                                                                               *
 * Last Modified: 2025.11.16, Sunday  09:54:37                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include <memory>
#include <vector>

#include "UIButton.h"
#include "UIContainerComponent.h"

/**
 * @brief Vízszintes gombsor komponens
 *
 * Ez a komponens automatikusan elrendezi a gombokat vízszintesen,
 * egységes mérettel és távolsággal. Speciálisan alsó státuszsor
 * vagy kiegészítő funkció gombokhoz tervezve.
 */
class UIHorizontalButtonBar : public UIContainerComponent {
  public:
    /**
     * @brief Gomb konfiguráció struktúra
     */
    struct ButtonConfig {
        uint8_t id;
        const char *label;
        UIButton::ButtonType type;
        UIButton::ButtonState initialState;
        std::function<void(const UIButton::ButtonEvent &)> callback;
        bool initiallyDisabled;

        ButtonConfig(uint8_t id, const char *label, UIButton::ButtonType type = UIButton::ButtonType::Pushable, UIButton::ButtonState initialState = UIButton::ButtonState::Off,
                     std::function<void(const UIButton::ButtonEvent &)> callback = nullptr, bool initiallyDisabled = false)
            : id(id), label(label), type(type), initialState(initialState), callback(callback), initiallyDisabled(initiallyDisabled) {}
    };

    /**
     * @brief Konstruktor
     * @param bounds A gombsor pozíciója és mérete
     * @param buttonConfigs Gombok konfigurációja
     * @param buttonWidth Egyetlen gomb szélessége (alapértelmezett: 60px)
     * @param buttonHeight Egyetlen gomb magassága (alapértelmezett: 35px)
     * @param buttonGap Gombok közötti távolság (alapértelmezett: 3px)
     * @param rowGap Sorok közötti távolság (alapértelmezett: 5px)
     */
    UIHorizontalButtonBar(const Rect &bounds, const std::vector<ButtonConfig> &buttonConfigs, uint16_t buttonWidth = 60, uint16_t buttonHeight = 35, uint16_t buttonGap = 3, uint16_t rowGap = 5);

    virtual ~UIHorizontalButtonBar() = default;

    /**
     * @brief Gomb állapotának beállítása ID alapján
     * @param buttonId A gomb azonosítója
     * @param state Az új állapot
     */
    void setButtonState(uint8_t buttonId, UIButton::ButtonState state);

    /**
     * @brief Gomb állapotának lekérdezése ID alapján
     * @param buttonId A gomb azonosítója
     * @return A gomb aktuális állapota
     */
    UIButton::ButtonState getButtonState(uint8_t buttonId) const;

    /**
     * @brief Egy gomb referenciájának megszerzése ID alapján
     * @param buttonId A gomb azonosítója
     * @return A gomb shared_ptr-e, vagy nullptr ha nem található
     */
    std::shared_ptr<UIButton> getButton(uint8_t buttonId) const;

    /**
     * @brief Futás közbeni gombszélesség változtatás és a gombok újraépítése
     * @param newButtonWidth Az új gomb szélesség pixelben
     * Megjegyzés: a meglévő konfigurációkat a konstruktor tárolja, így elég csak az új szélességet megadni.
     */
    void recreateWithButtonWidth(uint16_t newButtonWidth);

  private:
    uint16_t buttonWidth;
    uint16_t buttonHeight;
    uint16_t buttonGap;
    uint16_t rowGap; // Távolság sorok között
    std::vector<std::shared_ptr<UIButton>> buttons;
    // A konstruktorban átadott gomb konfigurációk tárolása, hogy később újra tudjuk építeni a sort
    std::vector<ButtonConfig> storedButtonConfigs;

    /**
     * @brief Gombok létrehozása és elhelyezése többsoros támogatással
     */
    void createButtons(const std::vector<ButtonConfig> &buttonConfigs);

    /**
     * @brief Számítja ki, hány gomb fér el egy sorban
     */
    uint16_t calculateButtonsPerRow() const;

    /**
     * @brief Számítja ki a szükséges sorok számát
     */
    uint16_t calculateRequiredRows(uint16_t totalButtons) const;
};
