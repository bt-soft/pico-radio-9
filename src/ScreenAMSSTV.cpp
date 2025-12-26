/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenAMSSTV.cpp                                                                                              *
 * Created Date: 2025.11.15.                                                                                           *
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
 * Last Modified: 2025.12.26, Friday  09:14:49                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

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

#define SSTV_PICTURE_START_X 20 // Kép kezdő X pozíciója (bal oldaltól 20px-re)
#define SSTV_PICTURE_START_Y 90 // Kép kezdő Y pozíciója

#define MODE_TXT_HEIGHT 15
#define MODE_TXT_X SSTV_PICTURE_START_X
#define MODE_TXT_Y SSTV_PICTURE_START_Y - MODE_TXT_HEIGHT

/**
 * @brief ScreenAMSSTV konstruktor
 */
ScreenAMSSTV::ScreenAMSSTV()
    : ScreenAMRadioBase(SCREEN_NAME_DECODER_SSTV), UICommonVerticalButtons::Mixin<ScreenAMSSTV>(), //
      accumulatedTargetLine(0.0f), lastDrawnTargetLine(0), lastModeDisplayed(-1) {

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

    // --- Reset gomb elhelyezése a kép jobb oldalán, 20px-el jobbra ---
    const int resetBtnX = SSTV_PICTURE_START_X + SSTV_SCALED_WIDTH + 20;
    const int resetBtnY = 30; 

    if (!resetButton) {
        // Létrehozzuk és hozzáadjuk a képernyőhöz
        resetButton = std::make_shared<UIButton>(        //
            200,                                         // egyedi ID
            Rect(resetBtnX, resetBtnY),                  // gomb pozíció default méretekkel
            "Reset",                                     // gomb felirata
            UIButton::ButtonType::Pushable,              // gomb típusa
            [this](const UIButton::ButtonEvent &event) { // eseménykezelő lambda
                if (event.state == UIButton::EventButtonState::Clicked) {
                    // Töröljük a képterületet
                    this->clearPictureArea();
                    // Kérjük meg a Core1-et is, hogy resetelje a dekódert az AudioController-en keresztül
                    ::audioController.resetDecoder();
                }
            });

        UIContainerComponent::addChild(resetButton);
    }

    // Tuning Bar: Spektrum sáv a Reset gomb alatt
    constexpr uint16_t tuningBarHeight = 36;                                              // 2x magasabb (18 -> 36)
    constexpr uint16_t tuningBarY = resetBtnY + UIButton::DEFAULT_BUTTON_HEIGHT + 2 + 10; // Reset gomb alatt 2px-lel + 10px lejjebb
    constexpr uint16_t tuningBarWidth = UIButton::DEFAULT_BUTTON_WIDTH * 2;               // 2x szélesebb
    constexpr uint16_t tuningBarX = resetBtnX;                                            // Reset gomb alatt
    if (!tuningBar) {
        tuningBar = std::make_shared<UICompTuningBar>(Rect(tuningBarX, tuningBarY, tuningBarWidth, tuningBarHeight), // bounds
                                                      1000,                                                          // minFreqHz: 1000 Hz
                                                      2500                                                           // maxFreqHz: 2500 Hz
        );
        // Frekvencia markerek konfigurálása SSTV esetén
        tuningBar->addMarker(1200, TFT_GREEN, "1200");  // Sync (szinkron impulzus) - zölden
        tuningBar->addMarker(1500, TFT_CYAN, "1500");   // Black (fekete szint) - ciánnal
        tuningBar->addMarker(2300, TFT_YELLOW, "2300"); // White (fehér szint) - sárgán
        UIContainerComponent::addChild(tuningBar);
    }

    // SSTV kép helyének kirajzolása
    this->clearPictureArea();
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
 * @brief Képernyő tartalom rajzolása
 */
void ScreenAMSSTV::drawContent() {

    // Képterület törlése
    this->clearPictureArea();

    // A 'Mode:' felirat megjelenítése
    tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    tft.setTextDatum(BC_DATUM);
    tft.setTextFont(0);
    tft.setTextSize(1);
    tft.setCursor(SSTV_PICTURE_START_X, SSTV_PICTURE_START_Y - MODE_TXT_HEIGHT);
    tft.print("SSTV Mode:");
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

    // SSTV kép helyének törlése
    tft.fillRect(SSTV_PICTURE_START_X, SSTV_PICTURE_START_Y, SSTV_SCALED_WIDTH, SSTV_SCALED_HEIGHT, TFT_BLACK);

    // Fehér keret rajzolása a kép körül (1px-el kívül)
    tft.drawRect(SSTV_PICTURE_START_X - 1, SSTV_PICTURE_START_Y - 1, SSTV_SCALED_WIDTH + 2, SSTV_SCALED_HEIGHT + 2, TFT_WHITE);

    // Mód név törlése
    this->drawSstvMode(nullptr);
}

/**
 * @brief SSTV mód név kirajzolása
 * @param modeName A megjelenítendő mód név
 */
void ScreenAMSSTV::drawSstvMode(const char *modeName) {

    constexpr int MODE_VALUE_X = SSTV_PICTURE_START_X + 60;

    // Módnév törlése (hátha volt ott valami)
    tft.fillRect(MODE_VALUE_X, SSTV_PICTURE_START_Y - MODE_TXT_HEIGHT - 4, SSTV_SCALED_WIDTH - 45, MODE_TXT_HEIGHT, TFT_BLACK);

    // Módnév kiírása
    if (modeName != nullptr && !STREQ(modeName, DECODER_MODE_UNKNOWN)) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextDatum(BC_DATUM);
        tft.setTextFont(0);
        tft.setTextSize(1);
        tft.setCursor(MODE_VALUE_X, MODE_TXT_Y);
        tft.print(modeName);
    }
}

/**
 * @brief Folyamatos loop hívás
 */
void ScreenAMSSTV::handleOwnLoop() {
    // Szülő osztály loop kezelése (S-Meter frissítés, stb.)
    ScreenAMRadioBase::handleOwnLoop();

    // FFT spektrum frissítése a tuning bar számára (Core1 -> Core0 irány)
    if (tuningBar && sharedData[1].fftSpectrumSize > 0) {
        tuningBar->updateSpectrum(sharedData[1].fftSpectrumData, sharedData[1].fftSpectrumSize, sharedData[1].fftBinWidthHz);
        tuningBar->draw(tft);
    }

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

        // Mód név kiírása (ha van)
        if (decodedData.currentMode >= 0) {
            this->drawSstvMode(modeName);
            lastModeDisplayed = decodedData.currentMode;
        } else {
            // Ha nincs információ, töröljük a korábbi módnevet
            this->drawSstvMode(nullptr);
            lastModeDisplayed = -1;
        }

        // Scaling állapot reset
        accumulatedTargetLine = 0.0f;
        lastDrawnTargetLine = 0;
    }

    // SSTV képsorok kiolvasása a közös lineBuffer-ből
    DecodedLine dline;
    if (decodedData.lineBuffer.get(dline)) {

        // Kicsinyítés -> egyszerű nearest neighbor scaling - gyors és tiszta
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
