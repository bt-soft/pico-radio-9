/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenAMRTTY.cpp                                                                                              *
 * Created Date: 2025.11.13.                                                                                           *
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
 * Last Modified: 2025.11.22, Saturday  10:19:26                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "ScreenAMRTTY.h"
#include "RTTYParamDialogs.h"
#include "ScreenManager.h"
#include "UIMultiButtonDialog.h"
#include "defines.h"

/**
 * @brief ScreenAMRTTY konstruktor
 */
ScreenAMRTTY::ScreenAMRTTY()
    : ScreenAMRadioBase(SCREEN_NAME_DECODER_RTTY), lastPublishedRttyMark(0), lastPublishedRttySpace(0), lastPublishedRttyBaud(0.0f), lastRTTYDisplayUpdate(0) {
    // UI komponensek létrehozása és elhelyezése
    layoutComponents();
}

/**
 * @brief ScreenAMRTTY destruktor
 */
ScreenAMRTTY::~ScreenAMRTTY() {
    // TextBox cleanup
    if (rttyTextBox) {
        DEBUG("ScreenAMRTTY::~ScreenAMRTTY() - TextBox cleanup\n");
        removeChild(rttyTextBox);
        rttyTextBox.reset();
    }
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
    // Induláskor beállítjuk a RTTYWaterfall megjelenítési módot
    // TODO: Az RttySnrCurve induláskor lefagy, ezt még javítani kell
    ScreenRadioBase::spectrumComp->setCurrentDisplayMode(UICompSpectrumVis::DisplayMode::RTTYWaterfall);

    // MEGJEGYZÉS: Az audioController indítása az activate() metódusban történik
    // hogy képernyőváltáskor megfelelően leálljon és újrainduljon

    // TextBox hozzáadása (a S-Meter alatt)
    constexpr uint16_t TEXTBOX_HEIGHT = 130;
    rttyTextBox = std::make_shared<UICompTextBox>( //
        5,                                         // x
        150,                                       // y
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

    // Hozzáadunk egy RTTY Paraméterek gombot a Back elé. Megnyomáskor
    // megjelenít egy 3 gombos dialógust (Mark / Space / Baud), amely elindítja
    // a megfelelő paraméter dialógust, és a gyerek bezárásakor visszaállítja
    // a 3 gombos szülő dialógust (ahogy a ScreenTest-ben látható).
    buttonConfigs.push_back(                          //
        {                                             //
         150,                                         // ID
         "Parms",                                     // Címke
         UIButton::ButtonType::Pushable,              // Típus
         UIButton::ButtonState::Off,                  // Állapot
         [this](const UIButton::ButtonEvent &event) { // lambda callback
             if (event.state != UIButton::EventButtonState::Clicked) {
                 return;
             }

             // RTTY Paraméterek gomb megnyomva -> megjelenítjük a 3 gombos dialógust
             static const char *options[] = {"Mark", "Space", "Baud"};

             // Létrehozzuk a szülő 3-gombos dialógust automatikus bezárás nélkül,
             // így manuálisan tudjuk bezárni, amikor gyereket nyitunk, majd
             // újra megjeleníteni, amikor a gyerek bezáródik.
             auto paramsDlg = std::make_shared<UIMultiButtonDialog>( //
                 this,                                               // szülő képernyő
                 "RTTY Params",                                      // cím
                 "Select parameter to edit:",                        // üzenet
                 options,                                            // gombok
                 3,                                                  // gombok száma
                 nullptr,                                            // a callback-ot később állítjuk be
                 false                                               // autoClose = false
             );

             // Beállítjuk a gombkattintás callback-et (capture shared_ptr)
             paramsDlg->setButtonClickCallback([this, paramsDlg](int idx, const char *label, UIMultiButtonDialog *sender) {
                 // Bezárjuk a szülő dialógust, hogy helyet adjunk a gyerek dialógusnak
                 paramsDlg->close(UIDialogBase::DialogResult::Accepted);

                 // A gyerek bezárulásakor a callback újra megjeleníti a szülő dialógust
                 auto childClosedCb = [this, paramsDlg](UIDialogBase *childSender, UIDialogBase::DialogResult result) {
                     // Nullázzuk az időzítőt, hogy a következő ciklusban azonnal frissüljön a kiírás
                     this->lastRTTYDisplayUpdate = 0;

                     // A gyerek bezárulása után a szülő 3-gombos dialógust újra megjelenítjük
                     this->showDialog(paramsDlg);
                 };

                 // Elindítjuk a kiválasztott paraméter dialógust
                 if (idx == 0) { // Mark
                     RTTYParamDialogs::showMarkFreqDialog(this, &::config, childClosedCb);
                 } else if (idx == 1) { // Space
                     RTTYParamDialogs::showSpaceFreqDialog(this, &::config, childClosedCb);
                 } else if (idx == 2) { // Baud
                     RTTYParamDialogs::showBaudRateDialog(this, &::config, childClosedCb);
                 }
             });

             // Megjelenítjük a szülő dialógust
             this->showDialog(paramsDlg);
         }});

    // Back button
    buttonConfigs.push_back(                          //
        {                                             //
         100,                                         // ID
         "Back",                                      // Címke
         UIButton::ButtonType::Pushable,              //  Típus
         UIButton::ButtonState::Off,                  // Kezdeti állapot
         [this](const UIButton::ButtonEvent &event) { // lambda callback
             if (getScreenManager()) {
                 getScreenManager()->goBack();
             }
         }});
}

/**
 * @brief Képernyő aktiválása
 */
void ScreenAMRTTY::activate() {

    // Szülő osztály aktiválása
    ScreenAMRadioBase::activate();
    Mixin::updateAllVerticalButtonStates(); // Univerzális funkcionális gombok (mixin method)

    // Biztonságos újragenerálás: ha a vízszintes gombsor már létrejött a szülőben,
    // beállítjuk kisebb gombszélességet, így az új gombok elférnek egy sorban.
    if (horizontalButtonBar) {
        horizontalButtonBar->recreateWithButtonWidth(65);
    }

    // RTTY audio dekóder indítása
    ::audioController.startAudioController( //
        DecoderId::ID_DECODER_RTTY,         // RTTY dekóder azonosító
        RTTY_RAW_SAMPLES_SIZE,              // RTTY bemeneti audio minták száma blokkonként
        RTTY_AF_BANDWIDTH_HZ,               // RTTY audio sávszélesség
        0,                                  // cwCenterFreqHz muszáj megadni, de az RTTY-nél nem használjuk
        config.data.rttyMarkFrequencyHz,    // RTTY Mark frekvencia
        config.data.rttyShiftFrequencyHz,   // RTTY Space frekvencia
        config.data.rttyBaudRate            // RTTY Baud rate
    );

    // AudioProc-C1 beállítások az RTTY módhoz
    ::audioController.setNoiseReductionEnabled(false); // Zajszűrés kikapcsolva (tisztább spektrum)
    ::audioController.setSmoothingPoints(0);           // Zajszűrés simítási pontok száma = 5 (erősebb zajszűrés, nincs frekvencia felbontási igény)
    ::audioController.setAgcEnabled(false);            // AGC kikapcsolva
    ::audioController.setManualGain(1.0f);             // Manuális erősítés: a kissebb HF sávszéleség miatt erősítünk rajta
    ::audioController.setSpectrumAveragingCount(2);    // Spektrum nem-koherens átlagolás: 2 keret átlagolása

    // RTTY Dekóder specifikus beállítások
    ::audioController.setDecoderBandpassEnabled(true); // Engedélyezzük a dekóder oldali bandpass szűrőt

    // RTTY infókat frissítsük!
    lastRTTYDisplayUpdate = 0;
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
    bool timeToUpdate = Utils::timeHasPassed(lastRTTYDisplayUpdate, 2000);

    // Frissítés: ha ez az első megjelenítés vagy lejárt a 2s timeout, újrarajzolunk.
    // A 'lastPublished...' értékeket csak akkor frissítjük, ha tényleges változás történt
    // vagy ha ez az első rajzolás.
    if (timeToUpdate || lastRTTYDisplayUpdate == 0) {
        bool updatePublished = anyDataChanged || lastRTTYDisplayUpdate == 0;
        if (updatePublished) {
            lastPublishedRttyMark = currentMark;
            lastPublishedRttySpace = currentSpace;
            lastPublishedRttyBaud = currentBaud;
        }
        lastRTTYDisplayUpdate = millis();

        // A textbox komponens fölött, jobbra igazítva jelenjen meg a kiírás
        constexpr uint16_t labelW = 140;
        constexpr uint8_t textHeight = 8; // textSize(1) betűmagasság: 8px
        constexpr uint8_t gap = 2;        // Távolság a textbox tetejétől
        constexpr uint16_t labelX = 95;   //
        uint16_t textBoxTop = rttyTextBox->getBounds().y;
        uint16_t labelY = textBoxTop - gap - textHeight; // Szöveg alja 2px-re a textbox teteje fölött

        tft.fillRect(labelX, labelY, labelW, textHeight, TFT_BLACK); // Csak a szöveg magasságát töröljük
        tft.setCursor(labelX, labelY);
        tft.setTextSize(1);
        tft.setTextColor(TFT_SILVER, TFT_BLACK);

        // RTTY beállítások kiírása: Mark / Space / Baud
        tft.printf("Mark: %4.0f /  Space: %3.0f / Shift: %4.0f / Baud: %3.2f",                                //
                   currentMark > 0 ? String(currentMark).c_str() : "----",                                    //
                   currentSpace > 0 ? String(currentSpace).c_str() : "---",                                   //
                   currentMark > 0 && currentSpace > 0 ? String(currentMark - currentSpace).c_str() : "----", //
                   currentBaud > 0 ? String(currentBaud).c_str() : "----");
    }

    // Dekódolt karakterek kiolvasása a ring bufferből és hozzáadása a textboxhoz
    char ch;
    while (::decodedData.textBuffer.get(ch)) {
        if (rttyTextBox) {
            rttyTextBox->addCharacter(ch);
        }
    }
}
