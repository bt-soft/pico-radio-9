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
 * Last Modified: 2025.11.16, Sunday  02:55:12                                                                         *
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
 * @brief RTTY Space frekvencia dialógus megjelenítése
 * @param parent Szülő UIScreen
 * @param cfg Konfiguráció pointer
 * @param cb Visszahívási függvény dialógus eredményének kezelésére
 */
void showSpaceFreqDialog(UIScreen *parent, Config *cfg, DialogCallback cb) {
    auto tempValuePtr = std::make_shared<int>(static_cast<int>(cfg->data.rttyShiftFrequencyHz));
    auto dlg = std::make_shared<UIValueChangeDialog>(
        parent, "RTTY Space Freq", "RTTY Space Frequency (Hz):", tempValuePtr.get(), static_cast<int>(80), static_cast<int>(1000), static_cast<int>(10),
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
        Rect(-1, -1, 280, 0));
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
