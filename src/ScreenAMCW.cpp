#include "ScreenAMCW.h"
#include "ScreenManager.h"
#include "defines.h"

/**
 * @brief ScreenAMCW konstruktor
 */
ScreenAMCW::ScreenAMCW() : ScreenAMRadioBase(SCREEN_NAME_DECODER_CW), lastPublishedCwWpm(0), lastPublishedCwFreq(0.0f) {
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
    constexpr uint16_t BACK_BUTTON_WIDTH = 70;
    constexpr uint16_t BACK_BUTTON_HEIGHT = 32;
    Rect backButtonRect(::SCREEN_W - BACK_BUTTON_WIDTH, ::SCREEN_H - BACK_BUTTON_HEIGHT, BACK_BUTTON_WIDTH, BACK_BUTTON_HEIGHT);

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
    constexpr uint16_t TEXTBOX_HEIGHT = 150;
    cwTextBox = std::make_shared<UICompTextBox>( //
        5,                                       // x
        ::SCREEN_H - TEXTBOX_HEIGHT,             // y
        400,                                     // width
        TEXTBOX_HEIGHT,                          // height
        tft                                      // TFT instance
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

    // CW dekódolt szöveg frissítése
    this->checkDecodedData();
}

/**
 * @brief CW dekódolt szöveg ellenőrzése és frissítése
 */
void ScreenAMCW::checkDecodedData() {

    static unsigned long lastCwDisplayUpdate = 0;
    uint16_t currentWpm = ::decodedData.cwCurrentWpm;
    float currentFreq = ::decodedData.cwCurrentFreq;

    // Csak akkor frissítjük a kijelzőt, ha jelentős változás történt ÉS eltelt már legalább 1 másodperc
    bool wpmChanged = (lastPublishedCwWpm == 0 && currentWpm != 0) || (abs((int)currentWpm - (int)lastPublishedCwWpm) >= 3);
    bool freqChanged = (lastPublishedCwFreq == 0.0f && currentFreq > 0.0f) || (abs(currentFreq - lastPublishedCwFreq) >= 50.0f);
    if (Utils::timeHasPassed(lastCwDisplayUpdate, 1000) && (wpmChanged || freqChanged)) {
        lastPublishedCwWpm = currentWpm;
        lastPublishedCwFreq = currentFreq;
        lastCwDisplayUpdate = millis();

        // A textbox komponens fölött, balra igazítva jelenjen meg a kiírás
        constexpr uint16_t labelH = 18;
        constexpr uint16_t labelX = 5;
        uint16_t labelY = cwTextBox->getBounds().y - labelH; // 18px magas sáv
        constexpr uint16_t labelW = 100;

        tft.fillRect(labelX, labelY, labelW, labelH, TFT_BLACK); // előző kiírás törlése
        tft.setCursor(labelX, labelY + 2);
        tft.setTextSize(1);
        tft.setTextColor(TFT_CYAN, TFT_BLACK);
        if (currentFreq > 0.0f && currentWpm > 0) {
            tft.printf("%u Hz / %.0f Hz / %u WPM", (uint16_t)config.data.cwToneFrequencyHz, currentFreq, currentWpm);
        } else {
            tft.print("-- Hz / -- Hz / -- WPM");
        }
    }

    char ch;
    while (::decodedData.textBuffer.get(ch)) {
        if (cwTextBox) {
            cwTextBox->addCharacter(ch);
        }
    }
}