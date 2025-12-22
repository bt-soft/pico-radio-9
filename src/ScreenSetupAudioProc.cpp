/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenSetupAudioProc.cpp                                                                                      *
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
 * Last Modified: 2025.11.30, Sunday  03:41:12                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "ScreenSetupAudioProc.h"
#include "CWParamDialogs.h"
#include "Config.h"
#include "RTTYParamDialogs.h"
#include "UIMultiButtonDialog.h"
#include "UIValueChangeDialog.h"

/**
 * @brief ScreenSetupAudioProc konstruktor
 */
ScreenSetupAudioProc::ScreenSetupAudioProc() : ScreenSetupBase(SCREEN_NAME_SETUP_AUDIO_PROC) { layoutComponents(); }

/**
 * @brief Képernyő címének visszaadása
 *
 * @return A képernyő címe
 */
const char *ScreenSetupAudioProc::getScreenTitle() const { return "Audio Processing"; }

/**
 * @brief Menüpontok feltöltése audió feldolgozás specifikus beállításokkal
 *
 * Ez a metódus feltölti a menüpontokat az audió feldolgozás aktuális
 * konfigurációs értékeivel.
 */
void ScreenSetupAudioProc::populateMenuItems() {
    // Korábbi menüpontok törlése
    settingItems.clear();

    settingItems.push_back(
        SettingItem("CW Tone Frequency", String(config.data.cwToneFrequencyHz) + " Hz", static_cast<int>(AudioProcItemAction::CW_TONE_FREQUENCY)));

    settingItems.push_back(
        SettingItem("RTTY Mark Frequency", String(config.data.rttyMarkFrequencyHz) + " Hz", static_cast<int>(AudioProcItemAction::RTTY_MARK_FREQUENCY)));
    settingItems.push_back(
        SettingItem("RTTY Shift Frequency", String(config.data.rttyShiftFrequencyHz) + " Hz", static_cast<int>(AudioProcItemAction::RTTY_SHIFT_FREQUENCY)));
    settingItems.push_back(SettingItem("RTTY baudrate", String(config.data.rttyBaudRate), static_cast<int>(AudioProcItemAction::RTTY_BAUDRATE)));

    settingItems.push_back(SettingItem("FFT Gain AM", decodeFFTGain(config.data.audioFftGainConfigAm), static_cast<int>(AudioProcItemAction::FFT_GAIN_AM)));
    settingItems.push_back(SettingItem("FFT Gain FM", decodeFFTGain(config.data.audioFftGainConfigFm), static_cast<int>(AudioProcItemAction::FFT_GAIN_FM)));

    // Lista komponens újrarajzolásának kérése, ha létezik
    if (menuList) {
        menuList->markForRedraw();
    }
}

/**
 * @brief Menüpont akció kezelése
 *
 * Ez a metódus kezeli az audió feldolgozás specifikus menüpontok kattintásait.
 *
 * @param index A menüpont indexe
 * @param action Az akció azonosító
 */
void ScreenSetupAudioProc::handleItemAction(int index, int action) {

    AudioProcItemAction audioProcAction = static_cast<AudioProcItemAction>(action);

    switch (audioProcAction) {
        case AudioProcItemAction::CW_TONE_FREQUENCY:
            handleCwToneFrequencyDialog(index);
            break;

        case AudioProcItemAction::RTTY_MARK_FREQUENCY:
            handleRttyMarkFrequencyDialog(index);
            break;

        case AudioProcItemAction::RTTY_SHIFT_FREQUENCY:
            handleRttyShiftFrequencyDialog(index);
            break;

        case AudioProcItemAction::RTTY_BAUDRATE:
            handleRttyBaudRateDialog(index);
            break;

        case AudioProcItemAction::FFT_GAIN_AM:
            handleFFTGainDialog(index, true);
            break;

        case AudioProcItemAction::FFT_GAIN_FM:
            handleFFTGainDialog(index, false);
            break;

        case AudioProcItemAction::NONE:
        default:
            DEBUG("ScreenSetupAudioProc: Unknown action: %d\n", action);
            break;
    }
}

/**
 * @brief CW tone frequency beállítása dialógussal
 *
 * @param index A menüpont indexe a lista frissítéséhez
 */
void ScreenSetupAudioProc::handleCwToneFrequencyDialog(int index) {
    std::function<void(UIDialogBase *, UIDialogBase::DialogResult)> cb = [this, index](UIDialogBase *sender, UIDialogBase::DialogResult result) {
        if (result == UIDialogBase::DialogResult::Accepted) {
            settingItems[index].value = String(config.data.cwToneFrequencyHz) + " Hz";
            updateListItem(index);
        }
    };
    CWParamDialogs::showCwToneFreqDialog(this, &config, cb);
}

/**
 * @brief RTTY mark frequency beállítása dialógussal
 *
 * @param index A menüpont indexe a lista frissítéséhez
 */
void ScreenSetupAudioProc::handleRttyMarkFrequencyDialog(int index) {
    {
        std::function<void(UIDialogBase *, UIDialogBase::DialogResult)> cb = [this, index](UIDialogBase *sender, UIDialogBase::DialogResult result) {
            if (result == UIDialogBase::DialogResult::Accepted) {
                settingItems[index].value = String(config.data.rttyMarkFrequencyHz) + " Hz";
                updateListItem(index);
            }
        };
        RTTYParamDialogs::showMarkFreqDialog(this, &config, cb);
    }
}

/**
 * @brief RTTY shift beállítása dialógussal
 *
 * @param index A menüpont indexe a lista frissítéséhez
 */
void ScreenSetupAudioProc::handleRttyShiftFrequencyDialog(int index) {
    {
        std::function<void(UIDialogBase *, UIDialogBase::DialogResult)> cb = [this, index](UIDialogBase *sender, UIDialogBase::DialogResult result) {
            if (result == UIDialogBase::DialogResult::Accepted) {
                settingItems[index].value = String(config.data.rttyShiftFrequencyHz) + " Hz";
                updateListItem(index);
            }
        };
        RTTYParamDialogs::showShiftFreqDialog(this, &config, cb);
    }
}

/**
 * @brief RTTY shift beállítása dialógussal
 *
 * @param index A menüpont indexe a lista frissítéséhez
 */
void ScreenSetupAudioProc::handleRttyBaudRateDialog(int index) {
    {
        std::function<void(UIDialogBase *, UIDialogBase::DialogResult)> cb = [this, index](UIDialogBase *sender, UIDialogBase::DialogResult result) {
            if (result == UIDialogBase::DialogResult::Accepted) {
                settingItems[index].value = String(config.data.rttyBaudRate) + " bps";
                updateListItem(index);
            }
        };
        RTTYParamDialogs::showBaudRateDialog(this, &config, cb);
    }
}

/**
 * @brief FFT gain érték dekódolása olvasható szöveggé
 *
 * @param value Az FFT gain érték
 * @return Olvasható string reprezentáció
 */
String ScreenSetupAudioProc::decodeFFTGain(int8_t value) {
    if (value == SPECTRUM_GAIN_MODE_AUTO) {
        return "Auto Gain";
    } else {
        // dB formátumban megjelenítés
        char buf[16];
        float fval = static_cast<float>(value);
        if (fval >= 0.0f) {
            sprintf(buf, "+%.1f dB", fval);
        } else {
            sprintf(buf, "%.1f dB", fval);
        }
        return String(buf);
    }
}

/**
 * @brief FFT manuális gain dialógus kezelése AM vagy FM módhoz
 *
 * @param index A menüpont indexe a lista frissítéséhez
 * @param isAM true = AM mód, false = FM mód
 */
void ScreenSetupAudioProc::handleFFTGainDialog(int index, bool isAM) {

    int8_t &currentConfig = isAM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    const char *title = isAM ? "FFT Gain AM" : "FFT Gain FM";

    uint8_t defaultSelection = 0; // Auto Gain
    if (currentConfig != SPECTRUM_GAIN_MODE_AUTO) {
        defaultSelection = 1; // Manual Gain
    }

    const char *options[] = {"Auto G", "Manual G"};

    auto fftDialog = std::make_shared<UIMultiButtonDialog>(
        this, title, "Select FFT gain mode:", options, ARRAY_ITEM_COUNT(options),
        [this, index, isAM, &currentConfig, title](int buttonIndex, const char *buttonLabel, UIMultiButtonDialog *dialog) {
            switch (buttonIndex) {
                case 0: // Auto Gain
                    currentConfig = SPECTRUM_GAIN_MODE_AUTO;
                    settingItems[index].value = "Auto Gain";
                    updateListItem(index);
                    dialog->close(UIDialogBase::DialogResult::Accepted);
                    break;

                case 1: // Manual Gain
                {
                    dialog->close(UIDialogBase::DialogResult::Accepted);

                    // tempGain must outlive the dialog; allocate on heap and keep shared_ptr in lambda capture
                    auto tempGainPtr = std::make_shared<int>((currentConfig == SPECTRUM_GAIN_MODE_AUTO) ? 0 : static_cast<int>(currentConfig));

                    auto gainDialog = std::make_shared<UIValueChangeDialog>(
                        this, (String(title) + " - Manual Gain").c_str(), "Set gain (dB): -20 ... +20", //
                        tempGainPtr.get(),                                                              // Pointer a temp értékhez (int*)
                        -20, 20, 1,                                                                     // Min, Max, Step (dB-ben)
                        nullptr,                                                                        // Élő előnézet callback (nem szükséges)
                        [this, index, &currentConfig, tempGainPtr](UIDialogBase *sender, UIMessageDialog::DialogResult result) { // Eredmény kezelése
                            if (result == UIMessageDialog::DialogResult::Accepted) {
                                int clamped = constrain(*tempGainPtr, -20, 20);
                                currentConfig = static_cast<int8_t>(clamped);
                                populateMenuItems(); // Teljes frissítés a helyes érték megjelenítéséhez
                            }
                        },
                        Rect(-1, -1, 300, 0));
                    this->showDialog(gainDialog);
                } break;
            }
        },
        false, defaultSelection, false, Rect(-1, -1, 340, 120));
    this->showDialog(fftDialog);
}

/**
 * @brief Boolean beállítások váltása
 *
 * @param index A menüpont indexe
 * @param configValue Referencia a módosítandó boolean értékre
 */
void ScreenSetupAudioProc::handleToggleItem(int index, bool &configValue) {
    configValue = !configValue;

    if (index >= 0 && index < settingItems.size()) {
        settingItems[index].value = String(configValue ? "ON" : "OFF");
        updateListItem(index);
    }
}
