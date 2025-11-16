/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenSetupAudioProc.h                                                                                        *
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
 * Last Modified: 2025.11.16, Sunday  09:51:40                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include "ScreenSetupBase.h"

/**
 * @brief Audió feldolgozás beállítások képernyő.
 *
 * Ez a képernyő az audió feldolgozás specifikus beállításait kezeli:
 * - CW receiver offset beállítása (400Hz - 1900Hz)
 * - RTTY shift beállítása (80Hz - 1000Hz)
 * - RTTY mark frequency beállítása (1200Hz - 2500Hz)
 * - FFT konfigurációk AM és FM módokhoz
 */
class ScreenSetupAudioProc : public ScreenSetupBase {
  private:
    /**
     * @brief Audió feldolgozás specifikus menüpont akciók
     */
    enum class AudioProcItemAction {
        NONE = 0,
        CW_TONE_FREQUENCY = 400,
        RTTY_MARK_FREQUENCY,
        RTTY_SHIFT_FREQUENCY,
        RTTY_BAUDRATE,
        FFT_GAIN_AM,
        FFT_GAIN_FM,
    };

    // Segédfüggvények
    String decodeFFTGain(float value);

    // Audió feldolgozás specifikus dialógus kezelő függvények
    void handleCwToneFrequencyDialog(int index);

    void handleRttyMarkFrequencyDialog(int index);
    void handleRttyShiftFrequencyDialog(int index);
    void handleRttyBaudRateDialog(int index);

    void handleFFTGainDialog(int index, bool isAM);
    void handleToggleItem(int index, bool &configValue);

  protected:
    // SetupScreenBase virtuális metódusok implementációja
    virtual void populateMenuItems() override;
    virtual void handleItemAction(int index, int action) override;
    virtual const char *getScreenTitle() const override;

  public:
    /**
     * @brief Konstruktor.
     */
    ScreenSetupAudioProc();
    virtual ~ScreenSetupAudioProc() = default;
};
