/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: CWParamDialogs.h                                                                                              *
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
 * Last Modified: 2025.11.16, Sunday  03:05:34                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#pragma once

#include <functional>
#include <memory>

#include "UIDialogBase.h"

class UIScreen;
struct Config;

/**
 * @brief Közös helper a CW paraméter dialógusokhoz (Tone freq)
 */
namespace CWParamDialogs {
using DialogCallback = std::function<void(UIDialogBase *sender, UIDialogBase::DialogResult result)>;

// Megjelenít egy dialógust a CW tone frekvencia szerkesztéséhez
void showCwToneFreqDialog(UIScreen *parent, Config *cfg, DialogCallback cb = nullptr);
} // namespace CWParamDialogs
