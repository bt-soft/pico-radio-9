/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: CWParamDialogs.cpp                                                                                            *
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
 * Last Modified: 2025.11.16, Sunday  03:06:07                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "CWParamDialogs.h"
#include "Config.h"
#include "UIValueChangeDialog.h"

/**
 * @brief Közös helper a CW paraméter dialógusokhoz (Tone freq)
 */
namespace CWParamDialogs {

/**
 * @brief Megjelenít egy dialógust a CW tone frekvencia szerkesztéséhez
 * @param parent Szülő UIScreen
 * @param cfg Konfiguráció pointer
 * @param cb Visszahívási függvény dialógus eredményének kezelésére
 */
void showCwToneFreqDialog(UIScreen *parent, Config *cfg, DialogCallback cb) {
    auto tempValuePtr = std::make_shared<int>(static_cast<int>(cfg->data.cwToneFrequencyHz));
    auto dlg = std::make_shared<UIValueChangeDialog>(
        parent, "CW Tone Freq", "CW Tone Frequency (Hz):", tempValuePtr.get(), static_cast<int>(400), static_cast<int>(1900), static_cast<int>(10),
        [cfg](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<int>(liveNewValue)) {
                cfg->data.cwToneFrequencyHz = static_cast<uint16_t>(std::get<int>(liveNewValue));
            }
        },
        [cfg, tempValuePtr, cb](UIDialogBase *sender, UIDialogBase::DialogResult result) {
            if (result == UIDialogBase::DialogResult::Accepted) {
                cfg->data.cwToneFrequencyHz = static_cast<uint16_t>(*tempValuePtr);
            }
            if (cb)
                cb(sender, result);
        },
        Rect(-1, -1, 300, 0));
    parent->showDialog(dlg);
}

} // namespace CWParamDialogs
