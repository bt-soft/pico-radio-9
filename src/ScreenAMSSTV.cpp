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

#define SSTV_SCALE 0.7f                                                       // Kép skálázási tényező
#define SSTV_SCALED_WIDTH ((uint16_t)(SSTV_LINE_WIDTH * SSTV_SCALE + 0.5f))   // Új szélesség skálázva
#define SSTV_SCALED_HEIGHT ((uint16_t)(SSTV_LINE_HEIGHT * SSTV_SCALE + 0.5f)) // Új magasság skálázva

#define SSTV_PICTURE_START_X 100 // Kép kezdő X pozíciója
#define SSTV_PICTURE_START_Y 90  // Kép kezdő Y pozíciója

/**
 * @brief ScreenAMSSTV konstruktor
 */
ScreenAMSSTV::ScreenAMSSTV()
    : ScreenAMRadioBase(SCREEN_NAME_DECODER_SSTV), UICommonVerticalButtons::Mixin<ScreenAMSSTV>(), //
      accumulatedTargetLine(0.0f), lastDrawnTargetLine(0) {

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

    // SSTV kép helyének kirajzolása
    clearPictureArea();
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
    Mixin::updateAllVerticalButtonStates(); // Univerzális funkcionális gombok (mixin method)

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
 * @brief Képterület törlése
 */
void ScreenAMSSTV::clearPictureArea() {
    // SSTV kép helye
    tft.fillRect(SSTV_PICTURE_START_X, SSTV_PICTURE_START_Y, SSTV_SCALED_WIDTH, SSTV_SCALED_HEIGHT, TFT_BLACK);
    // Fehér keret rajzolása a kép körül (1px-el kívül)
    tft.drawRect(SSTV_PICTURE_START_X - 1, SSTV_PICTURE_START_Y - 1, SSTV_SCALED_WIDTH + 2, SSTV_SCALED_HEIGHT + 2, TFT_WHITE);
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
        decodedData.newImageStarted = false; // Új kép flag törlése

        SSTV_DEBUG("ScreenAMSSTV::checkDecodedData: Új SSTV kép kezdődött - képterület törlése\n");

        // Képterület törlése
        clearPictureArea();

        // SSTV mód név lekérése és kiírása a TFT-re
        const char *modeName = c_sstv_decoder::getSstvModeName((c_sstv_decoder::e_mode)decodedData.currentMode);
        DEBUG("core-0: SSTV mód változás: %s (ID: %d)\n", modeName, decodedData.currentMode);

#define TFT_BANNER_HEIGHT 15
#define MODE_TXT_X SSTV_PICTURE_START_X
#define MODE_TXT_Y SSTV_PICTURE_START_Y - TFT_BANNER_HEIGHT
        // Kijelző felső sávjának törlése a korábbi mód név eltávolításához
        tft.fillRect(MODE_TXT_X, MODE_TXT_Y, SSTV_SCALED_WIDTH, TFT_BANNER_HEIGHT - 5, TFT_BLACK);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.setTextDatum(BC_DATUM);
        tft.setTextFont(0);
        tft.setTextSize(1);
        tft.setCursor(MODE_TXT_X, MODE_TXT_Y);
        tft.printf("Mode:");

        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setCursor(MODE_TXT_X + 45, MODE_TXT_Y);
        tft.print(modeName);

        // Scaling állapot reset
        accumulatedTargetLine = 0.0f;
        lastDrawnTargetLine = 0;
    }

    // SSTV képsorok kiolvasása a közös lineBuffer-ből
    DecodedLine dline;
    if (decodedData.lineBuffer.get(dline)) {

        // Egyszerű nearest neighbor scaling - gyors és tiszta
        static uint16_t scaledBuffer[SSTV_SCALED_WIDTH];

        for (uint16_t x = 0; x < SSTV_SCALED_WIDTH; ++x) {
            // Nearest neighbor: legközelebbi forrás pixel
            uint16_t srcX = (uint16_t)((x * SSTV_LINE_WIDTH) / SSTV_SCALED_WIDTH);
            uint16_t v = dline.sstvPixels[srcX];
            scaledBuffer[x] = (v >> 8) | (v << 8); // Byte-swap
        }

        // Függőleges pozíció számítása nearest neighbor módszerrel
        uint16_t scaledY = (uint16_t)((dline.lineNum * SSTV_SCALED_HEIGHT) / SSTV_LINE_HEIGHT);
        if (scaledY < SSTV_SCALED_HEIGHT) {
            tft.pushImage(SSTV_PICTURE_START_X, SSTV_PICTURE_START_Y + scaledY, SSTV_SCALED_WIDTH, 1, scaledBuffer);
        }
    }
}
