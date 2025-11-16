/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: RTTYParamDialogs.h                                                                                            *
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
 * Last Modified: 2025.11.16, Sunday  02:53:57                                                                         *
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

/*
 * Közös helper a RTTY paraméter dialógusokhoz (Mark, Space, Baudrate)
 */
namespace RTTYParamDialogs {
using DialogCallback = std::function<void(UIDialogBase *sender, UIDialogBase::DialogResult result)>;

// Magyar komment: Megjelenít egy dialógust az RTTY Mark frekvencia szerkesztéséhez
void showMarkFreqDialog(UIScreen *parent, Config *cfg, DialogCallback cb = nullptr);

// Magyar komment: Megjelenít egy dialógust az RTTY Space frekvencia szerkesztéséhez
void showSpaceFreqDialog(UIScreen *parent, Config *cfg, DialogCallback cb = nullptr);

// Magyar komment: Megjelenít egy dialógust az RTTY Baudrate szerkesztéséhez
void showBaudRateDialog(UIScreen *parent, Config *cfg, DialogCallback cb = nullptr);
} // namespace RTTYParamDialogs
