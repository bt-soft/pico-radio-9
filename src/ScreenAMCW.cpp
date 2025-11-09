#include "ScreenAMCW.h"
#include "ScreenManager.h"
#include "defines.h"

/**
 * @brief ScreenAMCW konstruktor
 */
ScreenAMCW::ScreenAMCW() : ScreenAMRadioBase(SCREEN_NAME_DECODER_CW) {
    // UI komponensek létrehozása és elhelyezése
    layoutComponents();
}

/**
 * @brief ScreenAMCW destruktor
 */
ScreenAMCW::~ScreenAMCW() {
    // Cleanup ha szükséges
}

/**
 * @brief UI komponensek létrehozása és képernyőn való elhelyezése
 */
void ScreenAMCW::layoutComponents() {

    // Frekvencia kijelző pozicionálás
    uint16_t FreqDisplayY = 20;
    Rect sevenSegmentFreqBounds(0, FreqDisplayY, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT + 10);
    // S-Meter komponens pozícionálása
    Rect smeterBounds(2, FreqDisplayY + UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT, SMeterConstants::SMETER_WIDTH, 70);
    // Szülő osztály layout meghívása (állapotsor, frekvencia, S-Meter)
    ScreenAMRadioBase::layoutComponents(sevenSegmentFreqBounds, smeterBounds);

    // Függőleges gombok létrehozása
    Mixin::createCommonVerticalButtons(); // UICommonVerticalButtons-ban definiált UIButtonsGroupManager alapú függőleges gombsor egyedi Memo kezelővel

    // Alsó közös + AM specifikus vízszintes gombsor az őstől
    // ScreenRadioBase::createCommonHorizontalButtons();

    // Back gomb külön, jobb alsó sarokhoz igazítva (ugyanakkora mint a vertikális gombok: 60x32)
    constexpr uint16_t VERTICAL_BUTTON_WIDTH = 60;
    constexpr uint16_t VERTICAL_BUTTON_HEIGHT = 32;
    Rect backButtonRect(::SCREEN_W - VERTICAL_BUTTON_WIDTH, ::SCREEN_H - VERTICAL_BUTTON_HEIGHT, VERTICAL_BUTTON_WIDTH, VERTICAL_BUTTON_HEIGHT);

    constexpr uint8_t BACK_BUTTON = 80;              ///< Vissza az AM képernyőre
    auto backButton = std::make_shared<UIButton>(    //
        BACK_BUTTON,                                 //
        backButtonRect,                              //
        "Back",                                      //
        UIButton::ButtonType::Pushable,              //
        UIButton::ButtonState::Off,                  //
        [this](const UIButton::ButtonEvent &event) { //
            if (event.state != UIButton::EventButtonState::Clicked) {
                return;
            }
            // Visszalépés az AM képernyőre
            getScreenManager()->goBack();
        });
    addChild(backButton);

    // ===================================================================
    // Spektrum vizualizáció komponens létrehozása
    // ===================================================================
    ScreenRadioBase::createSpectrumComponent(Rect(255, 70, 150, 80), RadioMode::AM);

    // ===================================================================
    // Audio dekóder konfigurálása CW dekóderrel 1.5kHz sávszélességgel, 128-as mintavételi mérettel
    // ===================================================================
    ::audioController.startAudioController(DecoderId::ID_DECODER_CW, CW_AF_BANDWIDTH_HZ, CW_AF_BANDWIDTH_HZ, config.data.cwToneFrequencyHz);

    // TextBox hozzáadása (a S-Meter alatt)
    cwTextBox = std::make_shared<UICompTextBox>(   //
        5,                                         // x
        smeterBounds.y + smeterBounds.height + 20, // y
        400,                                       // width
        150,                                       // height
        tft                                        // TFT instance
    );

    // Komponens hozzáadása a képernyőhöz
    children.push_back(cwTextBox);
}

/**
 * @brief Statikus képernyő tartalom kirajzolása
 */
void ScreenAMCW::drawContent() {
    // CW specifikus statikus tartalom (ha szükséges)
}

/**
 * @brief Képernyő aktiválása
 */
void ScreenAMCW::activate() {
    // Szülő osztály aktiválása
    ScreenAMRadioBase::activate();

    // CW specifikus gombok frissítése
    updateHorizontalButtonStates();
}

/**
 * @brief Képernyő deaktiválása
 */
void ScreenAMCW::deactivate() {
    // Szülő osztály deaktiválása
    ScreenAMRadioBase::deactivate();
}

/**
 * @brief Folyamatos loop hívás
 */
void ScreenAMCW::handleOwnLoop() {
    // Szülő osztály loop kezelése (S-Meter frissítés, stb.)
    ScreenAMRadioBase::handleOwnLoop();

    // CW dekódolt szöveg frissítése (TODO: dekóder implementáció)
}

/**
 * @brief CW specifikus gombok állapotának frissítése
 */
void ScreenAMCW::updateHorizontalButtonStates() {
    // Jelenleg nincs CW specifikus gombállapot frissítés
    // A Back gomb mindig aktív
}
