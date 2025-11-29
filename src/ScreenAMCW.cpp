/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenAMCW.cpp                                                                                                *
 * Created Date: 2025.11.09.                                                                                           *
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
 * Last Modified: 2025.11.29, Saturday  12:58:38                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */
#include <memory>

#include "CWParamDialogs.h"
#include "ScreenAMCW.h"
#include "ScreenManager.h"
#include "UIMultiButtonDialog.h"
#include "defines.h"

// CW Dekóder képernyő működés debug engedélyezése de csak DEBUG módban
#define __CW_DECODER_DEBUG
#if defined(__DEBUG) && defined(__CW_DECODER_DEBUG)
#define CW_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define CW_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

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
    // TextBox cleanup
    if (cwTextBox) {
        CW_DEBUG("ScreenAMCW::~ScreenAMCW() - TextBox cleanup\n");
        removeChild(cwTextBox);
        cwTextBox.reset();
    }
}

/**
 * @brief UI komponensek létrehozása és képernyőn való elhelyezése
 */
void ScreenAMCW::layoutComponents() {

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

    // Induláskor beállítjuk a CWWaterfall megjelenítési módot
    // TODO: Az CwSnrCurve induláskor lefagy, ezt még javítani kell
    ScreenRadioBase::spectrumComp->setCurrentDisplayMode(UICompSpectrumVis::DisplayMode::CWWaterfall);

    // MEGJEGYZÉS: Az audioController indítása az activate() metódusban történik
    // hogy képernyőváltáskor megfelelően leálljon és újrainduljon

    // TextBox hozzáadása (a S-Meter alatt)
    constexpr uint16_t TEXTBOX_HEIGHT = 130;
    cwTextBox = std::make_shared<UICompTextBox>( //
        5,                                       // x
        150,                                     // y
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

    // CW paraméterek gomb (Params) a Back előtt: megjelenít egy 3-gombos dialógust,
    // amely elindítja a CW tone frequency szerkesztő dialógust és visszatér a szülőhöz.
    constexpr uint8_t CW_PARAMS_BUTTON = 150;
    buttonConfigs.push_back(
        {CW_PARAMS_BUTTON, "Parms", UIButton::ButtonType::Pushable, UIButton::ButtonState::Off, [this](const UIButton::ButtonEvent &event) {
             if (event.state != UIButton::EventButtonState::Clicked)
                 return;

             static const char *options[] = {"Tone"};
             auto paramsDlg = std::make_shared<UIMultiButtonDialog>(this, "CW Params", "Select parameter to edit:", options, 1, nullptr, false);
             paramsDlg->setButtonClickCallback([this, paramsDlg](int idx, const char *label, UIMultiButtonDialog *sender) {
                 paramsDlg->close(UIDialogBase::DialogResult::Accepted);
                 auto childClosedCb = [this, paramsDlg](UIDialogBase *childSender, UIDialogBase::DialogResult result) { this->showDialog(paramsDlg); };
                 if (idx == 0) {
                     CWParamDialogs::showCwToneFreqDialog(this, &::config, childClosedCb);
                 }
             });
             this->showDialog(paramsDlg);
         }});

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
void ScreenAMCW::activate() {

    // Szülő osztály aktiválása
    ScreenAMRadioBase::activate();
    Mixin::updateAllVerticalButtonStates(); // Univerzális funkcionális gombok (mixin method)

    // Biztonságos újragenerálás: ha a vízszintes gombsor már létrejött a szülőben,
    // csökkentjük a gombok szélességét, hogy az extra "Params" gomb elférjen egy sorban.
    if (horizontalButtonBar) {
        horizontalButtonBar->recreateWithButtonWidth(65);
    }

    // CW audio dekóder indítása
    ::audioController.startAudioController( //
        DecoderId::ID_DECODER_CW,           // CW dekóder azonosító
        CW_RAW_SAMPLES_SIZE,                // sampleCount
        CW_AF_BANDWIDTH_HZ,                 // bandwidthHz
        config.data.cwToneFrequencyHz       // cwCenterFreqHz
    );

    // AudioProc-C1 beállítások CW módhoz
    ::audioController.setNoiseReductionEnabled(false); // Zajszűrés kikapcsolva (tisztább spektrum)
    ::audioController.setSmoothingPoints(0);           // Zajszűrés simítási pontok száma = 5 (erősebb zajszűrés, nincs frekvencia felbontási igény)
    ::audioController.setAgcEnabled(false);            // AGC kikapcsolva
    ::audioController.setManualGain(1.0f);             // Manuális erősítés: a kissebb HF sávszéleség miatt erősítünk rajta
    ::audioController.setSpectrumAveragingCount(2);    // Spektrum nem-koherens átlagolás: x db keret átlagolása

    // CW Dekóder specifikus beállítások
    ::audioController.setDecoderUseAdaptiveThreshold(false); // Adaptív AGC küszöb használata a CW dekóderben
    //::audioController.setDecoderBandpassEnabled(true);       // A CW dekódefnek nincs bandpass filtere
}

/**
 * @brief Képernyő deaktiválása
 */
void ScreenAMCW::deactivate() {

    // Audio dekóder leállítása
    ::audioController.stopAudioController();

    // Szülő osztály deaktiválása
    ScreenAMRadioBase::deactivate();
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

/**
 * @brief CW dekódolt szöveg ellenőrzése és frissítése
 */
void ScreenAMCW::checkDecodedData() {

    uint8_t currentWpm = ::decodedData.cwCurrentWpm;
    uint16_t currentFreq = ::decodedData.cwCurrentFreq;

    // Változás detektálás
    bool wpmChanged = (lastPublishedCwWpm == 0 && currentWpm != 0) || (abs((int)currentWpm - (int)lastPublishedCwWpm) >= 3);
    bool freqChanged = (lastPublishedCwFreq == 0 && currentFreq > 0) || (abs((int)currentFreq - (int)lastPublishedCwFreq) >= 50);
    bool anyDataChanged = (wpmChanged || freqChanged);

    // Időzítés: 2 másodpercenként frissítünk (TFT terhelés csökkentése)
    static unsigned long lastCwDisplayUpdate = 0;
    bool timeToUpdate = Utils::timeHasPassed(lastCwDisplayUpdate, 2000);

    // Frissítés csak ha eltelt az idő ÉS történt változás
    // VAGY ha ez az első megjelenítés (lastCwDisplayUpdate == 0)
    if ((timeToUpdate && anyDataChanged) || lastCwDisplayUpdate == 0) {
        lastPublishedCwWpm = currentWpm;
        lastPublishedCwFreq = currentFreq;
        lastCwDisplayUpdate = millis();

        // A textbox komponens fölött, jobbra igazítva jelenjen meg a kiírás
        constexpr uint16_t labelW = 140;
        constexpr uint8_t textHeight = 8; // textSize(1) betűmagasság: 8px
        constexpr uint8_t gap = 2;        // Távolság a textbox tetejétől
        constexpr uint16_t labelX = 250;
        uint16_t textBoxTop = cwTextBox->getBounds().y;
        uint16_t labelY = textBoxTop - gap - textHeight; // Szöveg alja 2px-re a textbox teteje fölött

        tft.fillRect(labelX, labelY, labelW, textHeight, TFT_BLACK); // Csak a szöveg magasságát töröljük
        tft.setCursor(labelX, labelY);
        tft.setFreeFont();
        tft.setTextSize(1);
        tft.setTextColor(TFT_SILVER, TFT_BLACK);

        // Mindig kiírjuk a config CW frekvenciát
        tft.printf("%4u Hz / %4s Hz / %2s WPM",                            //
                   (uint16_t)config.data.cwToneFrequencyHz,                //
                   currentFreq > 0 ? String(currentFreq).c_str() : "----", //
                   currentWpm > 0 ? String(currentWpm).c_str() : "--");
    }

    // Dekódolt karakterek kiolvasása a ring bufferből és hozzáadása a textboxhoz
    char ch;
    while (::decodedData.textBuffer.get(ch)) {
        if (cwTextBox) {
            cwTextBox->addCharacter(ch);
        }
    }
}
