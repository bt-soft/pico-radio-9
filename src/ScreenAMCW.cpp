#include "ScreenAMCW.h"
#include "ScreenManager.h"
#include "defines.h"

/**
 * @brief ScreenAMCW konstruktor
 */
ScreenAMCW::ScreenAMCW() : ScreenAMRadioBase(SCREEN_NAME_DECODER_CW), lastPublishedCwWpm(0), lastPublishedCwFreq(0) {
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

    // Alsó vízszintes gombsor - CSAK az AM specifikus 4 gomb (BFO, AFBW, ANTCAP, DEMOD)
    // addDefaultButtons = false -> NEM rakja be a HAM, Band, Scan gombokat
    ScreenRadioBase::createCommonHorizontalButtons(false);

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
 * @brief CW specifikus gombok hozzáadása a közös AM gombokhoz
 * @param buttonConfigs A már meglévő gomb konfigurációk vektora
 * @details Felülírja az ős metódusát, hogy hozzáadja a CW-specifikus gombokat
 */
void ScreenAMCW::addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) {

    // Szülő osztály (ScreenAMRadioBase) közös AM gombjainak hozzáadása
    // Ez tartalmazza: BFO, AFBW, ANTCAP, DEMOD gombokat
    ScreenAMRadioBase::addSpecificHorizontalButtons(buttonConfigs);

    // Itt később hozzáadhatsz CW-specifikus gombokat, például:
    // - WPM beállítás
    // - Speed beállítás
    // - Decoder mode váltás
    // - stb.

    // Példa CW-specifikus gombra (jelenleg nincs implementálva):
    /*
    buttonConfigs.push_back({
        CW_WPM_BUTTON,
        "WPM",
        UIButton::ButtonType::Pushable,
        UIButton::ButtonState::Off,
        [this](const UIButton::ButtonEvent &event) {
            handleWpmButton(event);
        }
    });
    */
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

        // A textbox komponens fölött, jobbra igazítva jelenjen meg a kiírás
        // textSize(1) betűmagasság: 8px
        constexpr uint16_t labelW = 140;
        constexpr uint8_t textHeight = 8; // textSize(1) font magassága
        constexpr uint8_t gap = 2;        // Távolság a textbox tetejétől
        uint16_t labelX = 260;
        uint16_t textBoxTop = cwTextBox->getBounds().y;
        uint16_t labelY = textBoxTop - gap - textHeight; // Szöveg alja 2px-re a textbox teteje fölött

        tft.fillRect(labelX, labelY, labelW, textHeight, TFT_BLACK); // Csak a szöveg magasságát töröljük
        tft.setCursor(labelX, labelY);
        tft.setTextSize(1);
        tft.setTextColor(TFT_SILVER, TFT_BLACK);
        if (currentFreq > 0 && currentWpm > 0) {
            tft.printf("%u Hz / %u Hz / %u WPM", (uint16_t)config.data.cwToneFrequencyHz, currentFreq, currentWpm);
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
