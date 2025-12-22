/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: RTTYParamDialogs.cpp                                                                                          *
 * Created Date: 2025.11.16.                                                                                           *
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
 * Last Modified: 2025.12.22, Monday  06:01:52                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "RTTYParamDialogs.h"
#include "Config.h"
#include "UIValueChangeDialog.h"

/*
 * Közös helper a RTTY paraméter dialógusokhoz (Mark, Space, Baudrate)
 */

namespace RTTYParamDialogs {

/**
 * @brief RTTY Mark frekvencia dialógus megjelenítése
 * @param parent Szülő UIScreen
 * @param cfg Konfiguráció pointer
 * @param cb Visszahívási függvény dialógus eredményének kezelésére
 */
void showMarkFreqDialog(UIScreen *parent, Config *cfg, DialogCallback cb) {
    auto tempValuePtr = std::make_shared<int>(static_cast<int>(cfg->data.rttyMarkFrequencyHz));
    auto dlg = std::make_shared<UIValueChangeDialog>(
        parent, "RTTY Mark Freq", "RTTY Mark Frequency (Hz):", tempValuePtr.get(), static_cast<int>(600), static_cast<int>(2500), static_cast<int>(25),
        [cfg](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<int>(liveNewValue)) {
                cfg->data.rttyMarkFrequencyHz = static_cast<uint16_t>(std::get<int>(liveNewValue));
            }
        },
        [cfg, tempValuePtr, cb](UIDialogBase *sender, UIDialogBase::DialogResult result) {
            if (result == UIDialogBase::DialogResult::Accepted) {
                cfg->data.rttyMarkFrequencyHz = static_cast<uint16_t>(*tempValuePtr);
            }
            if (cb)
                cb(sender, result);
        },
        Rect(-1, -1, 280, 0));
    parent->showDialog(dlg);
}

/**
 * @brief RTTY Shift frekvencia dialógus megjelenítése preset gombokkal
 * @param parent Szülő UIScreen
 * @param cfg Konfiguráció pointer
 * @param cb Visszahívási függvény dialógus eredményének kezelésére
 */
void showShiftFreqDialog(UIScreen *parent, Config *cfg, DialogCallback cb) {
    auto tempValuePtr = std::make_shared<int>(static_cast<int>(cfg->data.rttyShiftFrequencyHz));
    auto dlg = std::make_shared<UIValueChangeDialog>(
        parent, "RTTY Shift Freq", "RTTY Shift Frequency (Hz):", tempValuePtr.get(), static_cast<int>(80), static_cast<int>(1000), static_cast<int>(10),
        [cfg](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<int>(liveNewValue)) {
                cfg->data.rttyShiftFrequencyHz = static_cast<uint16_t>(std::get<int>(liveNewValue));
            }
        },
        [cfg, tempValuePtr, cb](UIDialogBase *sender, UIDialogBase::DialogResult result) {
            if (result == UIDialogBase::DialogResult::Accepted) {
                cfg->data.rttyShiftFrequencyHz = static_cast<uint16_t>(*tempValuePtr);
            }
            if (cb)
                cb(sender, result);
        },
        Rect(-1, -1, 280, 200)); // Megnövelt magasság (200px) a preset gomboknak

    // Preset gombok hozzáadása: 170, 200, 425, 450, 800, 850 Hz
    constexpr int presetValues[] = {170, 200, 425, 450, 800, 850};
    constexpr const char *presetLabels[] = {"170", "200", "425", "450", "800", "850"};
    constexpr int presetCount = 6;

    // weak_ptr a dialóghoz (elkerüljük a circular reference-t)
    std::weak_ptr<UIValueChangeDialog> weakDlg = dlg;

    // Preset gombok létrehozása és hozzáadása a dialóghoz
    for (int i = 0; i < presetCount; i++) {
        int presetValue = presetValues[i];
        auto presetButton = std::make_shared<UIButton>( //
            10 + i,                                     // Button ID (10-15)
            Rect(0, 0, 40, 25),                         // Méret, pozíció később állítódik be
            presetLabels[i], UIButton::ButtonType::Pushable, [weakDlg, tempValuePtr, presetValue, cfg](const UIButton::ButtonEvent &event) {
                if (event.state == UIButton::EventButtonState::Clicked) {
                    // Érték beállítása - a tempValuePtr-t frissítjük
                    *tempValuePtr = presetValue;
                    // A config értékét is frissítjük
                    cfg->data.rttyShiftFrequencyHz = static_cast<uint16_t>(presetValue);

                    // Dialógus és ÖSSZES gyerek újrarajzolása (true = gyerekek is!)
                    if (auto sharedDlg = weakDlg.lock()) {
                        sharedDlg->markForRedraw(true);
                    }
                }
            });
        presetButton->setUseMiniFont(true);
        dlg->addChild(presetButton);
    }

    // FONTOS: Újra kell rendezni a dialógust, hogy a preset gombok is elrendeződjenek!
    dlg->relayout();

    parent->showDialog(dlg);
}

/**
 * @brief RTTY Baud Rate dialógus megjelenítése
 * @param parent Szülő UIScreen
 * @param cfg Konfiguráció pointer
 * @param cb Visszahívási függvény dialógus eredményének kezelésére
 */
void showBaudRateDialog(UIScreen *parent, Config *cfg, DialogCallback cb) {
    auto tempValuePtr = std::make_shared<float>(static_cast<float>(cfg->data.rttyBaudRate));
    auto dlg = std::make_shared<UIValueChangeDialog>(
        parent, "RTTY Baud Rate", "RTTY Baud Rate (bps):", tempValuePtr.get(), 20.0f, 150.0f, 0.5f,
        [cfg](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<float>(liveNewValue)) {
                cfg->data.rttyBaudRate = std::get<float>(liveNewValue);
            }
        },
        [cfg, tempValuePtr, cb](UIDialogBase *sender, UIDialogBase::DialogResult result) {
            if (result == UIDialogBase::DialogResult::Accepted) {
                cfg->data.rttyBaudRate = static_cast<float>(*tempValuePtr);
            }
            if (cb)
                cb(sender, result);
        },
        Rect(-1, -1, 280, 0));
    parent->showDialog(dlg);
}

} // namespace RTTYParamDialogs
