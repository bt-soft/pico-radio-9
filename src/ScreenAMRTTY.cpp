#include "ScreenAMRTTY.h"
#include "ScreenManager.h"
#include "defines.h"

/**
 * @brief ScreenAMRTTY konstruktor
 */
ScreenAMRTTY::ScreenAMRTTY() : ScreenAMRadioBase(SCREEN_NAME_DECODER_RTTY), lastPublishedRttyMark(0), lastPublishedRttySpace(0), lastPublishedRttyBaud(0.0f) {
    // UI komponensek létrehozása és elhelyezése
    layoutComponents();
}

/**
 * @brief ScreenAMRTTY destruktor
 */
ScreenAMRTTY::~ScreenAMRTTY() {
    DEBUG("ScreenAMRTTY::~ScreenAMRTTY() - Destruktor hívása - erőforrások felszabadítása\n");

    // TextBox cleanup
    if (rttyTextBox) {
        DEBUG("ScreenAMRTTY::~ScreenAMRTTY() - TextBox cleanup\n");
        removeChild(rttyTextBox);
        rttyTextBox.reset();
    }

    DEBUG("ScreenAMRTTY::~ScreenAMRTTY() - Destruktor befejezve\n");
}

/**
 * @brief UI komponensek létrehozása és képernyőn való elhelyezése
 */
void ScreenAMRTTY::layoutComponents() {

    // Frekvencia kijelző pozicionálás
    uint16_t FreqDisplayY = 20;
    Rect sevenSegmentFreqBounds(0, FreqDisplayY, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT + 10);
    // S-Meter komponens pozícionálása
    Rect smeterBounds(2, FreqDisplayY + UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT - 10, SMeterConstants::SMETER_WIDTH, 70);
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
    ScreenRadioBase::spectrumComp->setCurrentDisplayMode(UICompSpectrumVis::DisplayMode::CwSnrCurve);

    // MEGJEGYZÉS: Az audioController indítása az activate() metódusban történik
    // hogy képernyőváltáskor megfelelően leálljon és újrainduljon

    // TextBox hozzáadása (a S-Meter alatt)
    constexpr uint16_t TEXTBOX_HEIGHT = 130;
    rttyTextBox = std::make_shared<UICompTextBox>( //
        5,                                         // x
        150,                                       //::SCREEN_H - TEXTBOX_HEIGHT,             // y
        400,                                       // width
        TEXTBOX_HEIGHT,                            // height
        tft                                        // TFT instance
    );

    // Komponens hozzáadása a képernyőhöz
    children.push_back(rttyTextBox);
}

/**
 * @brief RTTY specifikus gombok hozzáadása a közös AM gombokhoz
 * @param buttonConfigs A már meglévő gomb konfigurációk vektora
 * @details Felülírja az ős metódusát, hogy hozzáadja a RTTY-specifikus gombokat
 */
void ScreenAMRTTY::addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) {

    // Szülő osztály (ScreenAMRadioBase) közös AM gombjainak hozzáadása
    // Ez tartalmazza: BFO, AFBW, ANTCAP, DEMOD gombokat
    ScreenAMRadioBase::addSpecificHorizontalButtons(buttonConfigs);

    constexpr uint8_t BACK_BUTTON = 100;
    buttonConfigs.push_back(             //
        {                                //
         BACK_BUTTON,                    //
         "Back",                         //
         UIButton::ButtonType::Pushable, //
         UIButton::ButtonState::Off,     //
         [this](const UIButton::ButtonEvent &event) {
             if (getScreenManager()) {
                 getScreenManager()->goBack();
             }
         }} //
    );
}

/**
 * @brief Képernyő aktiválása
 */
void ScreenAMRTTY::activate() {

    // Szülő osztály aktiválása
    ScreenAMRadioBase::activate();

    // RTTY audio dekóder indítása
    ::audioController.startAudioController( //
        DecoderId::ID_DECODER_RTTY,         //
        RTTY_RAW_SAMPLES_SIZE,              //
        RTTY_AF_BANDWIDTH_HZ,               //
        config.data.rttyMarkFrequencyHz,    //
        config.data.rttyShiftFrequencyHz,   //
        config.data.rttyBaudRate            //
    );
}

/**
 * @brief Képernyő deaktiválása
 */
void ScreenAMRTTY::deactivate() {

    // Audio dekóder leállítása
    ::audioController.stopAudioController();

    // Szülő osztály deaktiválása
    ScreenAMRadioBase::deactivate();
}

/**
 * @brief Folyamatos loop hívás
 */
void ScreenAMRTTY::handleOwnLoop() {
    // Szülő osztály loop kezelése (S-Meter frissítés, stb.)
    ScreenAMRadioBase::handleOwnLoop();

    // RTTY dekódolt szöveg frissítése - RITKÁBBAN (2mp-ként)
    this->checkDecodedData();
}

/**
 * @brief RTTY dekódolt szöveg ellenőrzése és frissítése
 */
void ScreenAMRTTY::checkDecodedData() {

    uint16_t currentMark = decodedData.rttyMarkFreq;
    uint16_t currentSpace = decodedData.rttySpaceFreq;
    float currentBaud = decodedData.rttyBaudRate;

    // Változás detektálás
    bool markChanged = (lastPublishedRttyMark == 0.0f && currentMark > 0.0f) || (abs(currentMark - lastPublishedRttyMark) >= 5.0f);
    bool spaceChanged = (lastPublishedRttySpace == 0.0f && currentSpace > 0.0f) || (abs(currentSpace - lastPublishedRttySpace) >= 5.0f);
    bool baudChanged = (lastPublishedRttyBaud == 0.0f && currentBaud > 0.0f) || (abs(currentBaud - lastPublishedRttyBaud) >= 0.5f);
    bool anyDataChanged = (markChanged || spaceChanged || baudChanged);

    // Időzítés: 2 másodpercenként frissítünk (TFT terhelés csökkentése)
    static unsigned long lastRTTYDisplayUpdate = 0;
    bool timeToUpdate = Utils::timeHasPassed(lastRTTYDisplayUpdate, 2000);

    // Frissítés csak ha eltelt az idő ÉS történt változás
    // VAGY ha ez az első megjelenítés (lastRTTYDisplayUpdate == 0)
    if ((timeToUpdate && anyDataChanged) || lastRTTYDisplayUpdate == 0) {
        lastPublishedRttyMark = currentMark;
        lastPublishedRttySpace = currentSpace;
        lastPublishedRttyBaud = currentBaud;
        lastRTTYDisplayUpdate = millis();

        // A textbox komponens fölött, jobbra igazítva jelenjen meg a kiírás
        constexpr uint16_t labelW = 140;
        constexpr uint8_t textHeight = 8; // textSize(1) betűmagasság: 8px
        constexpr uint8_t gap = 2;        // Távolság a textbox tetejétől
        uint16_t labelX = 250;
        uint16_t textBoxTop = rttyTextBox->getBounds().y;
        uint16_t labelY = textBoxTop - gap - textHeight; // Szöveg alja 2px-re a textbox teteje fölött

        tft.fillRect(labelX, labelY, labelW, textHeight, TFT_BLACK); // Csak a szöveg magasságát töröljük
        tft.setCursor(labelX, labelY);
        tft.setTextSize(1);
        tft.setTextColor(TFT_SILVER, TFT_BLACK);

        // RTTY beállítások kiírása: Mark / Space / Baud
        if (currentMark > 0.0f && currentSpace > 0.0f && currentBaud > 0.0f) {
            tft.printf("M:%.0f S:%.0f Sh:%.0f Bd:%.2f", currentMark, currentSpace, currentMark - currentSpace, currentBaud);
        } else {
            tft.print("M:-- S:-- Sh:-- Bd:--");
        }
    }

    char ch;
    while (::decodedData.textBuffer.get(ch)) {
        if (rttyTextBox) {
            rttyTextBox->addCharacter(ch);
        }
    }
}
