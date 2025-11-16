/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: IScrollableListDataSource.h                                                                                   *
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
 * Last Modified: 2025.11.16, Sunday  09:48:31                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include <Arduino.h>

/**
 * @brief Interfész a UIScrollableListComponent adatforrásához.
 *
 * Ez az interfész definiálja azokat a metódusokat, amelyeket egy
 * adatforrásnak implementálnia kell, hogy a UIScrollableListComponent
 * meg tudja jeleníteni az elemeit.
 */
class IScrollableListDataSource {
  public:
    virtual ~IScrollableListDataSource() = default;

    /** @brief Visszaadja a lista elemeinek teljes számát. */
    virtual uint8_t getItemCount() const = 0;

    /** @brief Visszaadja a megadott indexű elem címke (label) részét. */
    virtual String getItemLabelAt(int index) const = 0;

    /** @brief Visszaadja a megadott indexű elem érték (value) részét. Lehet üres String, ha nincs érték. */
    virtual String getItemValueAt(int index) const = 0;

    /**
     * @brief Akkor hívódik meg, amikor egy elemre kattintanak (kiválasztják).
     * @return true, ha a lista komponensnek továbbra is teljes újrarajzolást kell végeznie, false egyébként.
     */
    virtual bool onItemClicked(int index) = 0;
};
