/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UIButton.h                                                                                                    *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                     *
 * -----                                                                                                               *
 * Last Modified: 2025.11.16, Sunday  09:53:07                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once

#include <algorithm> // For std::max
#include <functional>

#include "Config.h"
#include "UIComponent.h"
#include "utils.h"

/**
 * @brief UI Button komponens
 *
 * Egyszerű gomb komponens, amely kezeli a lenyomást és a megjelenítést.
 */
class UIButton : public UIComponent {

  public:
    // Alapértelmezett gomb méretek
    static constexpr uint16_t DEFAULT_BUTTON_WIDTH = 72;       // Alapértelmezett gomb szélesség
    static constexpr uint16_t DEFAULT_BUTTON_HEIGHT = 35;      // Alapértelmezett gomb magasság
    static constexpr uint16_t HORIZONTAL_TEXT_PADDING = 2 * 8; // 8px padding a szöveg mindkét oldalán
    static constexpr uint16_t BUTTON_TOUCH_MARGIN = 6;         // Gomb érintési érzékenység margója

    // Gomb típusok
    enum class ButtonType {
        Pushable,  // Egyszerű nyomógomb
        Toggleable // Váltógomb (on/off)
    };

    // Gomb esemény állapotok
    enum class EventButtonState {
        Off = 0,
        On,
        Disabled,      // Állapot riportáláshoz
        CurrentActive, // Jelenleg aktív mód jelzése
        Clicked,       // Lenyomás történt (Pushable gomboknál)
        LongPressed    // Hosszú nyomás
    };

    // Gomb esemény struktúra
    struct ButtonEvent {
        uint8_t id;
        const char *label;
        EventButtonState state;
        ButtonEvent(uint8_t id, const char *label, EventButtonState state) : id(id), label(label), state(state) {}
    };

    // Gomb logikai állapotai (a Disabled állapotot az ősosztály kezeli)
    enum class ButtonState { Off, On, CurrentActive };

  private:
    static constexpr uint8_t CORNER_RADIUS = 5;
    static constexpr uint32_t LONG_PRESS_THRESHOLD = 1000; // ms

    uint8_t buttonId;
    const char *label;
    ButtonType buttonType = ButtonType::Pushable;
    ButtonState currentState = ButtonState::Off;
    bool autoSizeToText = false;
    bool useMiniFont = false;
    uint32_t pressStartTime = 0;
    bool longPressThresholdMet = false; // Jelzi, ha a hosszú lenyomás küszöbét elértük

    ButtonColorScheme currentButtonScheme; // Gomb-specifikus színséma
    std::function<void(const ButtonEvent &)> eventCallback;
    std::function<void()> clickCallback; // Backward compatibility

    // Gomb állapot színek
    struct StateColors {
        uint16_t background;
        uint16_t border;
        uint16_t text;
        uint16_t led;
    };

    /**
     * @brief Lekéri a gomb aktuális állapotának megfelelő színeket
     * @return StateColors struktúra, amely tartalmazza a háttér, keret, szöveg és LED színeket
     */
    StateColors getStateColors() const {
        StateColors resultColors;

        if (isDisabled()) {
            resultColors.background = this->currentButtonScheme.disabledBackground;
            resultColors.border = this->currentButtonScheme.disabledBorder;
            resultColors.text = this->currentButtonScheme.disabledForeground;
            resultColors.led = this->currentButtonScheme.ledOffColor;
        } else if (this->pressed) {
            resultColors.background = this->currentButtonScheme.pressedBackground;
            resultColors.border = this->currentButtonScheme.pressedBorder;
            resultColors.text = this->currentButtonScheme.pressedForeground;

            if (buttonType == ButtonType::Toggleable) {
                resultColors.led = (currentState == ButtonState::On) ? this->currentButtonScheme.ledOnColor : this->currentButtonScheme.ledOffColor;
            } else { // Pushable
                resultColors.led = TFT_BLACK;
            }
        } else {
            if (currentState == ButtonState::On) {
                resultColors.background = this->currentButtonScheme.activeBackground;
                resultColors.border = this->currentButtonScheme.ledOnColor;
                resultColors.text = this->currentButtonScheme.activeForeground;
                resultColors.led = this->currentButtonScheme.ledOnColor;
            } else if (currentState == ButtonState::CurrentActive) {
                resultColors.background = this->currentButtonScheme.activeBackground;
                resultColors.border = TFT_BLUE;
                resultColors.text = this->currentButtonScheme.activeForeground;
                resultColors.led = TFT_BLACK;
            } else { // ButtonState::Off
                resultColors.background = this->currentButtonScheme.background;
                resultColors.border = this->currentButtonScheme.border;
                resultColors.text = this->currentButtonScheme.foreground;
                resultColors.led = (buttonType == ButtonType::Toggleable) ? this->currentButtonScheme.ledOffColor : TFT_BLACK;
            }
        }
        return resultColors;
    }

    uint16_t darkenColor(uint16_t color, uint8_t amount) const {
        uint8_t r = (color & 0xF800) >> 11;
        uint8_t g = (color & 0x07E0) >> 5;
        uint8_t b = (color & 0x001F);
        uint8_t darkenAmount = amount > 0 ? (amount >> 3) : 0;
        r = (r > darkenAmount) ? r - darkenAmount : 0;
        g = (g > darkenAmount) ? g - darkenAmount : 0;
        b = (b > darkenAmount) ? b - darkenAmount : 0;
        return (r << 11) | (g << 5) | b;
    }

    void drawPressedEffect(uint16_t baseColorForEffect) {
        const uint8_t steps = 6;
        uint8_t stepWidth = bounds.width / steps;
        uint8_t stepHeight = bounds.height / steps;
        for (uint8_t i = 0; i < steps; i++) {
            uint16_t fadedColor = darkenColor(baseColorForEffect, i * 30);
            tft.fillRoundRect(bounds.x + i * stepWidth / 2, bounds.y + i * stepHeight / 2, bounds.width - i * stepWidth, bounds.height - i * stepHeight, CORNER_RADIUS, fadedColor);
        }
    }

    void updateWidthToFitText() {
        if (!autoSizeToText || label == nullptr) {
            return;
        }

        uint8_t prevDatum = tft.getTextDatum();
        uint8_t currentTftTextSize = tft.textsize;

        tft.setTextSize(1);
        if (useMiniFont) {
            tft.setFreeFont();
        } else {
            tft.setFreeFont(&FreeSansBold9pt7b);
        }

        uint16_t textW = (strlen(label) > 0) ? tft.textWidth(label) : 0;
        uint16_t newWidth = textW + HORIZONTAL_TEXT_PADDING;

        newWidth = std::max(newWidth, static_cast<uint16_t>(bounds.height > 0 ? bounds.height : DEFAULT_BUTTON_HEIGHT));
        newWidth = std::max(newWidth, static_cast<uint16_t>(DEFAULT_BUTTON_WIDTH / 2));

        if (bounds.width != newWidth) {
            bounds.width = newWidth;
            markForRedraw();
        }

        tft.setTextSize(currentTftTextSize);
        tft.setTextDatum(prevDatum);
    }

  private:
    // Privát "fő" konstruktor, amelyre a publikus konstruktorok delegálnak.
    // Ez csökkenti a kódduplikációt és központosítja az inicializálási logikát.
    UIButton(uint8_t id, const Rect &bounds, const char *label, ButtonType type, ButtonState initialState, bool initiallyDisabled, std::function<void(const ButtonEvent &)> callback, const ButtonColorScheme &scheme,
             bool autoSize)
        : UIComponent(Rect(bounds.x, bounds.y, (bounds.width == 0 && !autoSize ? DEFAULT_BUTTON_WIDTH : bounds.width), (bounds.height == 0 ? DEFAULT_BUTTON_HEIGHT : bounds.height)), scheme), buttonId(id), label(label),
          buttonType(type), currentState(initialState), currentButtonScheme(scheme), eventCallback(callback), autoSizeToText(autoSize) {

        if (initiallyDisabled) {
            this->disabled = true; // Közvetlen beállítás a redraw elkerülése érdekében
        }

        if (buttonType == ButtonType::Pushable && currentState == ButtonState::On) {
            currentState = ButtonState::Off;
        }

        if (autoSizeToText) {
            updateWidthToFitText();
        }
    }

  public:
    static uint16_t calculateWidthForText(const char *text, bool btnUseMiniFont, uint16_t currentButtonHeight) {
        if (text == nullptr)
            text = "";
        uint8_t prevDatum = ::tft.getTextDatum();
        uint8_t currentTftTextSize = ::tft.textsize;
        ::tft.setTextSize(1);
        if (btnUseMiniFont) {
            ::tft.setFreeFont();
        } else {
            ::tft.setFreeFont(&FreeSansBold9pt7b);
        }
        uint16_t textW = strlen(text) > 0 ? ::tft.textWidth(text) : 0;
        uint16_t calculatedWidth = textW + HORIZONTAL_TEXT_PADDING;
        uint16_t minHeight = (currentButtonHeight > 0) ? currentButtonHeight : DEFAULT_BUTTON_HEIGHT;
        calculatedWidth = std::max(calculatedWidth, minHeight);
        calculatedWidth = std::max(calculatedWidth, static_cast<uint16_t>(DEFAULT_BUTTON_WIDTH / 2));
        ::tft.setTextSize(currentTftTextSize);
        ::tft.setTextDatum(prevDatum);
        return calculatedWidth;
    }

    /**
     * @brief Elsődleges gomb konstruktor.
     * @param initiallyDisabled Opcionális: A gomb letiltott állapottal induljon-e.
     */
    UIButton(uint8_t id, const Rect &bounds, const char *label, ButtonType type = ButtonType::Pushable, ButtonState initialState = ButtonState::Off, std::function<void(const ButtonEvent &)> callback = nullptr,
             const ButtonColorScheme &scheme = UIColorPalette::createDefaultButtonScheme(), bool autoSizeToText = false, bool initiallyDisabled = false)
        : UIButton(id, bounds, label, type, initialState, initiallyDisabled, callback, scheme, autoSizeToText) {}

    /**
     * @brief Konstruktor a callback és típus kötelező megadásával.
     */
    UIButton(uint8_t id, const Rect &bounds, const char *label, ButtonType type, std::function<void(const ButtonEvent &)> callback, const ButtonColorScheme &scheme = UIColorPalette::createDefaultButtonScheme(),
             bool autoSizeToText = false)
        : UIButton(id, bounds, label, type, ButtonState::Off, false, callback, scheme, autoSizeToText) {}

    /**
     * @brief Egyszerűsített konstruktor "Pushable" gombokhoz.
     */
    UIButton(uint8_t id, const Rect &bounds, const char *label, std::function<void(const ButtonEvent &)> callback = nullptr, bool autoSizeToText = false)
        : UIButton(id, bounds, label, ButtonType::Pushable, ButtonState::Off, false, callback, UIColorPalette::createDefaultButtonScheme(), autoSizeToText) {}

    // ================================
    // Getters/Setters
    // ================================

    uint8_t getId() const { return buttonId; }
    void setId(uint8_t id) { buttonId = id; }

    ButtonType getButtonType() const { return buttonType; }
    void setButtonType(ButtonType type) {
        if (buttonType != type) {
            buttonType = type;
            markForRedraw();
        }
    }

    ButtonState getButtonState() const { return currentState; }

    /**
     * @brief Gomb logikai állapotának beállítása. A letiltást a setEnabled() kezeli.
     */
    void setButtonState(ButtonState newState) {
        if (buttonType == ButtonType::Pushable && newState == ButtonState::On) {
            newState = ButtonState::Off;
        }

        if (currentState != newState) {
            currentState = newState;
            markForRedraw();
        }
    }

    /**
     * @brief Gomb engedélyezése vagy letiltása.
     */
    void setEnabled(bool enable) {
        // Az ősosztály setDisabled metódusa kezeli a 'disabled' flag-et és a redraw-t.
        UIComponent::setDisabled(!enable);
    }

    void setAutoSizeToText(bool enable) {
        if (autoSizeToText != enable) {
            autoSizeToText = enable;
            if (autoSizeToText) {
                updateWidthToFitText();
            }
            markForRedraw();
        }
    }

    bool getAutoSizeToText() const { return autoSizeToText; }

    void setLabel(const char *newLabel) {
        label = newLabel;
        if (autoSizeToText) {
            updateWidthToFitText();
        } else {
            markForRedraw();
        }
    }

    const char *getLabel() const { return label; }

    void setUseMiniFont(bool mini) {
        if (useMiniFont != mini) {
            useMiniFont = mini;
            if (autoSizeToText) { // A méret is változhat
                updateWidthToFitText();
            } else {
                markForRedraw();
            }
        }
    }

    bool isUseMiniFont() const { return useMiniFont; }

    void setEventCallback(std::function<void(const ButtonEvent &)> callback) { eventCallback = callback; }

    void setClickCallback(std::function<void()> callback) { clickCallback = callback; }

    void setAsDefaultChoiceButton() {
        setEnabled(false);
        ColorScheme baseScheme = UIColorPalette::createDefaultChoiceButtonScheme();
        ButtonColorScheme btnScheme(baseScheme, baseScheme.activeBorder, TFT_DARKGREY);
        setButtonColorScheme(btnScheme);
    }

    virtual void draw() override {
        if (!needsRedraw)
            return;

        StateColors currentDrawColors = getStateColors();

        if (this->pressed) {
            drawPressedEffect(currentDrawColors.background);
        } else {
            tft.fillRoundRect(bounds.x, bounds.y, bounds.width, bounds.height, CORNER_RADIUS, currentDrawColors.background);
        }

        tft.drawRoundRect(bounds.x, bounds.y, bounds.width, bounds.height, CORNER_RADIUS, currentDrawColors.border);

        if (label != nullptr) {
            tft.setTextSize(1);
            if (useMiniFont) {
                tft.setFreeFont();
            } else {
                tft.setFreeFont(&FreeSansBold9pt7b);
            }

            tft.setTextColor(currentDrawColors.text);
            tft.setTextDatum(MC_DATUM);

            bool willHaveLed = (buttonType == ButtonType::Toggleable && !useMiniFont && currentDrawColors.led != TFT_BLACK);
            int16_t textY = bounds.centerY();
            if (useMiniFont)
                textY += 1;

            if (willHaveLed) {
                constexpr uint8_t LED_HEIGHT = 5;
                constexpr uint8_t LED_GAP = 3;
                int16_t ledTopY = bounds.y + bounds.height - LED_HEIGHT - 3;
                int16_t desiredTextBottomY = ledTopY - LED_GAP;
                int16_t textHeight = tft.fontHeight(); // Robusztusabb magasság lekérdezés
                int16_t adjustedTextY = desiredTextBottomY - (textHeight / 2);

                if (adjustedTextY < textY) {
                    textY = adjustedTextY;
                }
            }

            tft.drawString(label, bounds.centerX(), textY);
        }

        if (buttonType == ButtonType::Toggleable && !useMiniFont && currentDrawColors.led != TFT_BLACK) {
            constexpr uint8_t LED_HEIGHT = 5;
            constexpr uint8_t LED_MARGIN = 10;
            tft.fillRect(bounds.x + LED_MARGIN, bounds.y + bounds.height - LED_HEIGHT - 3, bounds.width - 2 * LED_MARGIN, LED_HEIGHT, currentDrawColors.led);
        }

        needsRedraw = false;
    }

    void setButtonColorScheme(const ButtonColorScheme &newScheme) {
        this->currentButtonScheme = newScheme;
        UIComponent::setColorScheme(newScheme);
        markForRedraw();
    }

  protected:
    virtual void onTouchDown(const TouchEvent &event) override {
        UIComponent::onTouchDown(event);
        if (isDisabled())
            return;

        longPressThresholdMet = false;
        pressStartTime = millis();
    }

    virtual void onTouchUp(const TouchEvent &event) override {
        UIComponent::onTouchUp(event);
        // A logika átkerült az onClick-be, itt már csak a flag-ek resetelése történik,
        // amit az onTouchCancel és az onClick is elvégez.
    }

    /**
     * @brief Gomb kattintás esemény kezelése. Eldönti, hogy rövid vagy hosszú lenyomás történt-e.
     */
    virtual bool onClick(const TouchEvent &event) override {
        UIComponent::onClick(event);
        if (isDisabled())
            return false;

        bool wasLongPress = longPressThresholdMet;

        // Flag-ek azonnali törlése a következő interakcióhoz.
        pressStartTime = 0;
        longPressThresholdMet = false;

        if (wasLongPress) {
            if (eventCallback) {
                DEBUG("UIButton: Long press event fired for button %d (%s)\n", buttonId, label);
                eventCallback(ButtonEvent(buttonId, label, EventButtonState::LongPressed));
            }
        } else {
            // Normál (rövid) kattintás
            if (buttonType == ButtonType::Toggleable) {
                currentState = (currentState == ButtonState::Off || currentState == ButtonState::CurrentActive) ? ButtonState::On : ButtonState::Off;
                if (eventCallback) {
                    eventCallback(ButtonEvent(buttonId, label, (currentState == ButtonState::On) ? EventButtonState::On : EventButtonState::Off));
                }
            } else { // Pushable
                if (eventCallback) {
                    eventCallback(ButtonEvent(buttonId, label, EventButtonState::Clicked));
                }
            }

            if (clickCallback) { // Visszafelé kompatibilitás
                clickCallback();
            }
        }

        markForRedraw();

        if (config.data.beeperEnabled) {
            Utils::beepTick();
        }

        return true; // Kezeltük a kattintást
    }

    /**
     * @brief Gomb lenyomás megszakítása (pl. ujj lecsúszik).
     */
    virtual void onTouchCancel(const TouchEvent &event) override {
        UIComponent::onTouchCancel(event);
        if (isDisabled())
            return;

        // Lenyomás megszakadt, töröljük a flag-eket.
        pressStartTime = 0;
        longPressThresholdMet = false;
    }

  public:
    virtual bool allowsVisualPressedFeedback() const override { return true; }

    virtual void loop() override {
        UIComponent::loop();
        if (isDisabled() || !this->pressed)
            return;

        if (!longPressThresholdMet && pressStartTime > 0) {
            if (millis() - pressStartTime >= LONG_PRESS_THRESHOLD) {
                longPressThresholdMet = true;
                // Vizuális visszajelzés arról, hogy a hosszú lenyomás "élesítve" van.
                markForRedraw();
            }
        }
    }

    virtual int16_t getTouchMargin() const override { return BUTTON_TOUCH_MARGIN; }
};
