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
    DEBUG("ScreenAMCW::~ScreenAMCW() - Destruktor hívása - erőforrások felszabadítása\n");

    // TextBox cleanup
    if (cwTextBox) {
        DEBUG("ScreenAMCW::~ScreenAMCW() - TextBox cleanup\n");
        removeChild(cwTextBox);
        cwTextBox.reset();
    }

    DEBUG("ScreenAMCW::~ScreenAMCW() - Destruktor befejezve\n");
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
    ScreenRadioBase::createSpectrumComponent(Rect(255, 40, 150, 80), RadioMode::AM);
    // Induláskor beállítjuk a CwSnrCurve megjelenítési módot
    ScreenRadioBase::spectrumComp->setCurrentMode(UICompSpectrumVis::DisplayMode::CwSnrCurve);

    // MEGJEGYZÉS: Az audioController indítása az activate() metódusban történik
    // hogy képernyőváltáskor megfelelően leálljon és újrainduljon

    // TextBox hozzáadása (a S-Meter alatt)
    constexpr uint16_t TEXTBOX_HEIGHT = 130;
    cwTextBox = std::make_shared<UICompTextBox>( //
        5,                                       // x
        150,                                     //::SCREEN_H - TEXTBOX_HEIGHT,             // y
        400,                                     // width
        TEXTBOX_HEIGHT,                          // height
        tft                                      // TFT instance
    );

    // Komponens hozzáadása a képernyőhöz
    children.push_back(cwTextBox);
}

/**
 * @brief Képernyő aktiválása
 */
void ScreenAMCW::activate() {

    DEBUG("ScreenAMCW::activate() - ELŐTTE - Free heap: %d bytes\n", rp2040.getFreeHeap());

    // Szülő osztály aktiválása
    ScreenAMRadioBase::activate();

    // CW audio dekóder indítása
    ::audioController.startAudioController(DecoderId::ID_DECODER_CW, CW_RAW_SAMPLES_SIZE, CW_AF_BANDWIDTH_HZ, config.data.cwToneFrequencyHz);

    DEBUG("ScreenAMCW::activate() - UTÁNA - Free heap: %d bytes\n", rp2040.getFreeHeap());
}

/**
 * @brief Képernyő deaktiválása
 */
void ScreenAMCW::deactivate() {

    DEBUG("ScreenAMCW::deactivate() - ELŐTTE - Free heap: %d bytes\n", rp2040.getFreeHeap());

    // Audio dekóder leállítása
    ::audioController.stopAudioController();

    // Szülő osztály deaktiválása
    ScreenAMRadioBase::deactivate();

    DEBUG("ScreenAMCW::deactivate() - UTÁNA - Free heap: %d bytes\n", rp2040.getFreeHeap());
}

/**
 * @brief CW dekódolt szöveg ellenőrzése és frissítése
 */
void ScreenAMCW::checkDecodedData() {

    uint8_t currentWpm = ::decodedData.cwCurrentWpm;
    uint16_t currentFreq = ::decodedData.cwCurrentFreq;

    // Csak akkor frissítjük a kijelzőt, ha jelentős változás történt ÉS eltelt már legalább 2 másodperc
    // NÖVELT INTERVALLUM: 1000ms -> 2000ms a TFT terhelés csökkentésére
    bool wpmChanged = (lastPublishedCwWpm == 0 && currentWpm != 0) || (abs((int)currentWpm - (int)lastPublishedCwWpm) >= 3);
    bool freqChanged = (lastPublishedCwFreq == 0 && currentFreq > 0) || (abs((int)currentFreq - (int)lastPublishedCwFreq) >= 50);

    // FIX: Csak 2 másodpercenként frissítünk, és csak ha TÉNYLEG változott az adat
    static unsigned long lastCwDisplayUpdate = 0;
    if (Utils::timeHasPassed(lastCwDisplayUpdate, 2000) && (wpmChanged || freqChanged)) {
        lastPublishedCwWpm = currentWpm;
        lastPublishedCwFreq = currentFreq;
        lastCwDisplayUpdate = millis();

        // A textbox komponens fölött, balra igazítva jelenjen meg a kiírás
        constexpr uint16_t labelW = 140;
        constexpr uint16_t labelH = 20;
        uint16_t labelX = 260;
        uint16_t labelY = cwTextBox->getBounds().y - labelH; // 18px magas sáv

        tft.fillRect(labelX, labelY, labelW, labelH, TFT_BLACK); // előző kiírás törlése
        tft.setCursor(labelX, labelY + 6);
        tft.setTextSize(1);
        tft.setTextColor(TFT_BROWN, TFT_SILVER);
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

/**
 * @brief Folyamatos loop hívás
 */
void ScreenAMCW::handleOwnLoop() {
    // Szülő osztály loop kezelése (S-Meter frissítés, stb.)
    ScreenAMRadioBase::handleOwnLoop();

    // CW dekódolt szöveg frissítése - RITKÁBBAN (2mp-ként)
    this->checkDecodedData();
}
