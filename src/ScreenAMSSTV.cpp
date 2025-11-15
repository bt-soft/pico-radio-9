#include "ScreenAMSSTV.h"
#include "ScreenManager.h"
#include "decode_sstv.h"
#include "defines.h"

// SSTV Dekóder képernyő működés debug engedélyezése de csak DEBUG módban
#define __SSTV_DECODER_DEBUG
#if defined(__DEBUG) && defined(__SSTV_DECODER_DEBUG)
#define SSTV_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define SSTV_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

#define SSTV_PICTURE_START_X 100
#define SSTV_PICTURE_START_Y 50

/**
 * @brief ScreenAMSSTV konstruktor
 */
ScreenAMSSTV::ScreenAMSSTV() : ScreenAMRadioBase(SCREEN_NAME_DECODER_SSTV), UICommonVerticalButtons::Mixin<ScreenAMSSTV>() {
    // UI komponensek létrehozása és elhelyezése
    layoutComponents();
}

/**
 * @brief ScreenAMSSTV destruktor
 */
ScreenAMSSTV::~ScreenAMSSTV() {}

/**
 * @brief UI komponensek létrehozása és képernyőn való elhelyezése
 */
void ScreenAMSSTV::layoutComponents() {

    // Állapotsor komponens létrehozása (felső sáv)
    ScreenRadioBase::createStatusLine();

    // Frekvencia kijelző pozicionálás
    uint16_t FreqDisplayY = 20;
    Rect sevenSegmentFreqBounds(0, FreqDisplayY, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT + 10);
    ScreenRadioBase::createSevenSegmentFreq(sevenSegmentFreqBounds); // SevenSegmentFreq komponens létrehozása
    this->updateSevenSegmentFreqWidth();                             // Dinamikus szélesség beállítása band típus alapján

    // Függőleges gombok létrehozása
    Mixin::createCommonVerticalButtons(); // UICommonVerticalButtons-ban definiált UIButtonsGroupManager alapú függőleges gombsor egyedi Memo kezelővel

    // Alsó vízszintes gombsor - CSAK az AM specifikus 4 gomb (BFO, AFBW, ANTCAP, DEMOD)
    // addDefaultButtons = false -> NEM rakja be a HAM, Band, Scan gombokat
    ScreenRadioBase::createCommonHorizontalButtons(false);

    // ===================================================================
    // Spektrum vizualizáció komponens létrehozása
    // ===================================================================
    // ScreenRadioBase::createSpectrumComponent(Rect(255, 40, 150, 80), RadioMode::AM);

    // Induláskor beállítjuk a Waterfall megjelenítési módot
    // ScreenRadioBase::spectrumComp->setCurrentDisplayMode(UICompSpectrumVis::DisplayMode::Waterfall);

    // SSTV kép helye
    // Képterület törlése (50,50) pozíciótól 320x256 méretben
    tft.fillRect(SSTV_PICTURE_START_X, SSTV_PICTURE_START_Y, SSTV_LINE_WIDTH, SSTV_LINE_HEIGHT, TFT_BLACK);
    // Fehér keret rajzolása a kép körül (1px-el kívül)
    tft.drawRect(SSTV_PICTURE_START_X - 1, SSTV_PICTURE_START_Y - 1, SSTV_LINE_WIDTH + 2, SSTV_LINE_HEIGHT + 2, TFT_WHITE);
}

/**
 * @brief SSTV specifikus gombok hozzáadása a közös AM gombokhoz
 * @param buttonConfigs A már meglévő gomb konfigurációk vektora
 * @details Felülírja az ős metódusát, hogy hozzáadja a SSTV-specifikus gombokat
 */
void ScreenAMSSTV::addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) {

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
void ScreenAMSSTV::activate() {

    // Szülő osztály aktiválása
    ScreenAMRadioBase::activate();

    // SSTV audio dekóder indítása
    ::audioController.startAudioController( //
        DecoderId::ID_DECODER_SSTV,         // SSTV dekóder azonosító
        SSTV_RAW_SAMPLES_SIZE,              // sampleCount
        SSTV_AF_BANDWIDTH_HZ                // bandwidthHz
    );
    ::audioController.setNoiseReductionEnabled(true); // Zajszűrés beapcsolva (tisztább spektrum)
    ::audioController.setSmoothingPoints(5);          // Zajszűrés simítási pontok száma = 5 (erősebb zajszűrés, nincs frekvencia felbontási igény)
}

/**
 * @brief Képernyő deaktiválása
 */
void ScreenAMSSTV::deactivate() {

    // Audio dekóder leállítása
    ::audioController.stopAudioController();

    // Szülő osztály deaktiválása
    ScreenAMRadioBase::deactivate();
}

/**
 * @brief Folyamatos loop hívás
 */
void ScreenAMSSTV::handleOwnLoop() {
    // Szülő osztály loop kezelése (S-Meter frissítés, stb.)
    ScreenAMRadioBase::handleOwnLoop();

    // SSTV dekódolt képsor frissítése
    this->checkDecodedData();
}

/**
 * @brief SSTV dekódolt kép ellenőrzése és frissítése
 */
void ScreenAMSSTV::checkDecodedData() {

    // Új kép kezdés ellenőrzése
    if (decodedData.newImageStarted) {
        decodedData.newImageStarted = false; // Flag törlése

        SSTV_DEBUG("ScreenAMSSTV::checkDecodedData: Új SSTV kép kezdődött - képterület törlése\n");

        // Képterület törlése (50,50) pozíciótól 320x256 méretben
        tft.fillRect(SSTV_PICTURE_START_X, SSTV_PICTURE_START_Y, SSTV_LINE_WIDTH, SSTV_LINE_HEIGHT, TFT_BLACK);

        // Fehér keret rajzolása a kép körül (1px-el kívül)
        tft.drawRect(SSTV_PICTURE_START_X - 1, SSTV_PICTURE_START_Y - 1, SSTV_LINE_WIDTH + 2, SSTV_LINE_HEIGHT + 2, TFT_WHITE);
    }

    // SSTV képsorok kiolvasása a közös lineBuffer-ből
    DecodedLine dline;
    if (decodedData.lineBuffer.get(dline)) {
        // Byte-swap a pixel értékeket a TFT_eSPI-hez
        static uint16_t displayBuffer[SSTV_LINE_WIDTH];
        for (int i = 0; i < SSTV_LINE_WIDTH; ++i) {
            uint16_t v = dline.sstvPixels[i]; // RGB565 formátumban
            displayBuffer[i] = (uint16_t)((v >> 8) | (v << 8));
        }
        tft.pushImage(SSTV_PICTURE_START_X, SSTV_PICTURE_START_Y + dline.lineNum, SSTV_LINE_WIDTH, 1, displayBuffer);
    }
}
