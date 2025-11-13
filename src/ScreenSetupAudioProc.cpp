#include "ScreenSetupAudioProc.h"
#include "Config.h"
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

    settingItems.push_back(SettingItem("CW Tone Frequency", String(config.data.cwToneFrequencyHz) + " Hz", static_cast<int>(AudioProcItemAction::CW_TONE_FREQUENCY)));

    settingItems.push_back(SettingItem("RTTY Mark Frequency", String(config.data.rttyMarkFrequencyHz) + " Hz", static_cast<int>(AudioProcItemAction::RTTY_MARK_FREQUENCY)));
    settingItems.push_back(SettingItem("RTTY Shift Frequency", String(config.data.rttyShiftFrequencyHz) + " Hz", static_cast<int>(AudioProcItemAction::RTTY_SHIFT_FREQUENCY)));
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
    auto tempValuePtr = std::make_shared<int>(static_cast<int>(config.data.cwToneFrequencyHz));

    auto cwToneFrequencyDialog = std::make_shared<UIValueChangeDialog>(
        this, "CW Tone Frequency", "CW Tone Frequency (Hz):", tempValuePtr.get(),
        static_cast<int>(400),  // Min: 400Hz
        static_cast<int>(1900), // Max: 1900Hz
        static_cast<int>(10),   // Step: 10Hz
        [this, index](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<int>(liveNewValue)) {
                int currentDialogVal = std::get<int>(liveNewValue);
                config.data.cwToneFrequencyHz = static_cast<uint16_t>(currentDialogVal);
                DEBUG("ScreenSetupAudioProc: CW tone frequency: %u Hz\n", config.data.cwToneFrequencyHz);
            }
        },
        [this, index, tempValuePtr](UIDialogBase *sender, UIMessageDialog::DialogResult dialogResult) {
            if (dialogResult == UIMessageDialog::DialogResult::Accepted) {
                config.data.cwToneFrequencyHz = static_cast<uint16_t>(*tempValuePtr);
                settingItems[index].value = String(config.data.cwToneFrequencyHz) + " Hz";
                updateListItem(index);
            }
        },
        Rect(-1, -1, 280, 0));
    this->showDialog(cwToneFrequencyDialog);
}

/**
 * @brief RTTY mark frequency beállítása dialógussal
 *
 * @param index A menüpont indexe a lista frissítéséhez
 */
void ScreenSetupAudioProc::handleRttyMarkFrequencyDialog(int index) {
    auto tempValuePtr = std::make_shared<int>(static_cast<int>(config.data.rttyMarkFrequencyHz));

    auto rttyMarkDialog = std::make_shared<UIValueChangeDialog>(
        this, "RTTY Mark Freq", "RTTY Mark Frequency (Hz):", tempValuePtr.get(),
        static_cast<int>(600),  // Min: 600Hz
        static_cast<int>(2500), // Max: 2500Hz
        static_cast<int>(25),   // Step: 25Hz
        [this, index](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<int>(liveNewValue)) {
                int currentDialogVal = std::get<int>(liveNewValue);
                config.data.rttyMarkFrequencyHz = static_cast<uint16_t>(currentDialogVal);
            }
        },
        [this, index, tempValuePtr](UIDialogBase *sender, UIMessageDialog::DialogResult dialogResult) {
            if (dialogResult == UIMessageDialog::DialogResult::Accepted) {
                config.data.rttyMarkFrequencyHz = static_cast<uint16_t>(*tempValuePtr);
                settingItems[index].value = String(config.data.rttyMarkFrequencyHz) + " Hz";
                updateListItem(index);
            }
        },
        Rect(-1, -1, 280, 0));
    this->showDialog(rttyMarkDialog);
}

/**
 * @brief RTTY shift beállítása dialógussal
 *
 * @param index A menüpont indexe a lista frissítéséhez
 */
void ScreenSetupAudioProc::handleRttyShiftFrequencyDialog(int index) {
    auto tempValuePtr = std::make_shared<int>(static_cast<int>(config.data.rttyShiftFrequencyHz));

    auto rttyShiftFrequencyDialog = std::make_shared<UIValueChangeDialog>(
        this, "RTTY Shift", "RTTY Shift Frequency (Hz):", tempValuePtr.get(),
        static_cast<int>(80),   // Min: 80Hz
        static_cast<int>(1000), // Max: 1000Hz
        static_cast<int>(10),   // Step: 10Hz
        [this, index](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<int>(liveNewValue)) {
                int currentDialogVal = std::get<int>(liveNewValue);
                config.data.rttyShiftFrequencyHz = static_cast<uint16_t>(currentDialogVal);
            }
        },
        [this, index, tempValuePtr](UIDialogBase *sender, UIMessageDialog::DialogResult dialogResult) {
            if (dialogResult == UIMessageDialog::DialogResult::Accepted) {
                config.data.rttyShiftFrequencyHz = static_cast<uint16_t>(*tempValuePtr);
                settingItems[index].value = String(config.data.rttyShiftFrequencyHz) + " Hz";
                updateListItem(index);
            }
        },
        Rect(-1, -1, 280, 0));
    this->showDialog(rttyShiftFrequencyDialog);
}

/**
 * @brief RTTY shift beállítása dialógussal
 *
 * @param index A menüpont indexe a lista frissítéséhez
 */
void ScreenSetupAudioProc::handleRttyBaudRateDialog(int index) {
    auto tempValuePtr = std::make_shared<float>(static_cast<float>(config.data.rttyBaudRate));

    auto rttyBaudRateDialog = std::make_shared<UIValueChangeDialog>(
        this, "RTTY Baud Rate", "RTTY Baud Rate (bps):", tempValuePtr.get(),
        20.0f,  // Min: 20bps
        150.0f, // Max: 150bps
        0.5f,   // Step: 0.5bps
        [this, index](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<float>(liveNewValue)) {
                float currentDialogVal = std::get<float>(liveNewValue);
                config.data.rttyBaudRate = static_cast<float>(currentDialogVal);
            }
        },
        [this, index, tempValuePtr](UIDialogBase *sender, UIMessageDialog::DialogResult dialogResult) {
            if (dialogResult == UIMessageDialog::DialogResult::Accepted) {
                config.data.rttyBaudRate = static_cast<float>(*tempValuePtr);
                settingItems[index].value = String(config.data.rttyBaudRate) + " bps";
                updateListItem(index);
            }
        },
        Rect(-1, -1, 280, 0));
    this->showDialog(rttyBaudRateDialog);
}

/**
 * @brief FFT gain érték dekódolása olvasható szöveggé
 *
 * @param value Az FFT gain érték
 * @return Olvasható string reprezentáció
 */
String ScreenSetupAudioProc::decodeFFTGain(float value) {
    if (value == -1.0f) {
        return "Disabled";
    } else if (value == 0.0f) {
        return "Auto Gain";
    } else {
        return "Manual: x " + Utils::floatToString(value, 3);
    }
}

/**
 * @brief FFT manuális gain dialógus kezelése AM vagy FM módhoz
 *
 * @param index A menüpont indexe a lista frissítéséhez
 * @param isAM true = AM mód, false = FM mód
 */
void ScreenSetupAudioProc::handleFFTGainDialog(int index, bool isAM) {

    float &currentConfig = isAM ? config.data.audioFftGainConfigAm : config.data.audioFftGainConfigFm;
    const char *title = isAM ? "FFT Gain AM" : "FFT Gain FM";

    uint8_t defaultSelection = 0; // Disabled
    if (currentConfig == 0.0f) {
        defaultSelection = 1; // Auto Gain
    } else if (currentConfig > 0.0f) {
        defaultSelection = 2; // Manual Gain
    }

    const char *options[] = {"Disabled", "Auto G", "Manual G"};

    auto fftDialog = std::make_shared<UIMultiButtonDialog>(
        this, title, "Select FFT gain mode:", options, ARRAY_ITEM_COUNT(options),
        [this, index, isAM, &currentConfig, title](int buttonIndex, const char *buttonLabel, UIMultiButtonDialog *dialog) {
            switch (buttonIndex) {
                case 0: // Disabled
                    currentConfig = -1.0f;
                    settingItems[index].value = "Disabled";
                    updateListItem(index);
                    dialog->close(UIDialogBase::DialogResult::Accepted);
                    break;

                case 1: // Auto Gain
                    currentConfig = 0.0f;
                    settingItems[index].value = "Auto Gain";
                    updateListItem(index);
                    dialog->close(UIDialogBase::DialogResult::Accepted);
                    break;

                case 2: // Manual Gain
                {
                    dialog->close(UIDialogBase::DialogResult::Accepted);

                    auto tempGainValuePtr = std::make_shared<float>((currentConfig > 0.0f) ? currentConfig : 1.0f);

                    auto gainDialog = std::make_shared<UIValueChangeDialog>(
                        this, (String(title) + " - Manual Gain").c_str(), "Set gain factor (0.001 - 1.0):",                           //
                        tempGainValuePtr.get(),                                                                                       // Pointer a temp értékhez
                        0.01f, 1.0f, 0.01f,                                                                                           // Min, Max, Step
                        nullptr,                                                                                                      // Élő előnézet callback (nem szükséges)
                        [this, index, &currentConfig, tempGainValuePtr](UIDialogBase *sender, UIMessageDialog::DialogResult result) { // Eredmény kezelése
                            if (result == UIMessageDialog::DialogResult::Accepted) {
                                currentConfig = *tempGainValuePtr;
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
