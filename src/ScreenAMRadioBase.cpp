/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenAMRadioBase.cpp                                                                                         *
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
 * Last Modified: 2025.11.16, Sunday  09:42:17                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "ScreenAMRadioBase.h"
#include "UIMultiButtonDialog.h"
#include "UIValueChangeDialog.h"

// ===================================================================
// Vízszintes gombsor azonosítók - Képernyő-specifikus navigáció
// ===================================================================

/**
 * @brief ScreenAMRadioBase osztály konstruktora
 * @param screenName Képernyő egyedi neve
 */
ScreenAMRadioBase::ScreenAMRadioBase(const char *screenName) : ScreenRadioBase(screenName) {}

/**
 * @brief Képernyő aktiválása - Event-driven gombállapot szinkronizálás
 * @details Meghívódik, amikor a felhasználó erre a képernyőre vált.
 *
 * Ez az EGYETLEN hely, ahol a gombállapotokat szinkronizáljuk a rendszer állapotával:
 * - Függőleges gombok: Mute, AGC, Attenuator állapotok
 * - Vízszintes gombok: Navigációs gombok állapotai
 *
 * **Event-driven előnyök**:
 * - NINCS folyamatos polling a loop()-ban
 * - Csak aktiváláskor történik szinkronizálás
 * - Jelentős teljesítményjavulás
 * - Univerzális gombkezelés (CommonVerticalButtons)
 *
 * **Szinkronizált állapotok**:
 * - MUTE gomb ↔ rtv::muteStat
 * - AGC gomb ↔ Si4735 AGC állapot (TODO)
 * - ATTENUATOR gomb ↔ Si4735 attenuator állapot (TODO)
 */
void ScreenAMRadioBase::activate() {

    // Szülő osztály aktiválása (ScreenRadioBase -> ScreenFrequDisplayBase -> UIScreen)
    ScreenRadioBase::activate();
    ScreenRadioBase::updateCommonHorizontalButtonStates(); // Közös gombok szinkronizálása
    this->updateHorizontalButtonStates();                  // AM-specifikus gombok szinkronizálása
    this->updateSevenSegmentFreqWidth();                   // SevenSegmentFreq szélességének frissítése
}

/**
 * @brief UI komponensek létrehozása és képernyőn való elhelyezése
 * @details Létrehozza és pozicionálja az összes UI elemet:
 * - Állapotsor (felül)
 * - Frekvencia kijelző (középen)
 * - S-Meter (jelerősség mérő)
 * - Függőleges gombsor (jobb oldal) - Közös FMScreen-nel
 * - Vízszintes gombsor (alul) - FM gombbal
 */
void ScreenAMRadioBase::layoutComponents(Rect sevenSegmentFreqBounds, Rect smeterBounds) {

    // Állapotsor komponens létrehozása (felső sáv)
    this->createStatusLine();

    // Frekvencia kijelző létrehozása
    this->createSevenSegmentFreq(sevenSegmentFreqBounds);

    // S-Meter létrehozása
    this->createSMeterComponent(smeterBounds);

    // Dinamikus szélesség beállítása band típus alapján
    this->updateSevenSegmentFreqWidth();
};

/**
 * @brief AM specifikus gombok hozzáadása a közös gombokhoz
 * @param buttonConfigs A már meglévő gomb konfigurációk vektora
 * @details Felülírja az ős metódusát, hogy hozzáadja az AM specifikus gombokat
 */
void ScreenAMRadioBase::addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) {
    // BFO - Beat Frequency Oscillator
    buttonConfigs.push_back({ScreenAMRadioBase::BFO_BUTTON, "BFO", UIButton::ButtonType::Toggleable, UIButton::ButtonState::Off, [this](const UIButton::ButtonEvent &event) { this->handleBFOButton(event); }});

    // AntCap - Antenna Capacitor
    buttonConfigs.push_back({ScreenAMRadioBase::ANTCAP_BUTTON, "AntCap", UIButton::ButtonType::Pushable, UIButton::ButtonState::Off, [this](const UIButton::ButtonEvent &event) { this->handleAntCapButton(event); }});

    // Demod - Demodulation
    buttonConfigs.push_back({ScreenAMRadioBase::DEMOD_BUTTON, "Demod", UIButton::ButtonType::Pushable, UIButton::ButtonState::Off, [this](const UIButton::ButtonEvent &event) { this->handleDemodButton(event); }});

    // AfBW - Audio Filter Bandwidth
    buttonConfigs.push_back({ScreenAMRadioBase::AFBW_BUTTON, "AfBW", UIButton::ButtonType::Pushable, UIButton::ButtonState::Off, [this](const UIButton::ButtonEvent &event) { this->handleAfBWButton(event); }});
}

/**
 * @brief Frissíti a vízszintes gombok állapotát
 * @details Közös gombok állapot frissítése az aktuális rádió állapot alapján
 */
void ScreenAMRadioBase::updateHorizontalButtonStates() {
    ScreenRadioBase::updateCommonHorizontalButtonStates(); // Közös gombok állapot frissítése

    // Többi AM specifikus gomb alapértelmezett állapotban
    if (ScreenRadioBase::horizontalButtonBar) {
        ScreenRadioBase::horizontalButtonBar->setButtonState(ScreenAMRadioBase::AFBW_BUTTON, UIButton::ButtonState::Off);
        ScreenRadioBase::horizontalButtonBar->setButtonState(ScreenAMRadioBase::ANTCAP_BUTTON, UIButton::ButtonState::Off);
        ScreenRadioBase::horizontalButtonBar->setButtonState(ScreenAMRadioBase::DEMOD_BUTTON, UIButton::ButtonState::Off);
    }

    // BFO gomb update
    this->updateBFOButtonState();

    // Step gomb update
    this->updateStepButtonState();
}

/**
 * @brief Frissíti a SevenSegmentFreq szélességét az aktuális band típus alapján
 * @details Dinamikusan állítja be a frekvencia kijelző szélességét
 */
void ScreenAMRadioBase::updateSevenSegmentFreqWidth() {
    if (!sevenSegmentFreq) {
        return; // Biztonsági ellenőrzés
    }
    auto bandType = ::pSi4735Manager->getCurrentBandType();
    uint16_t newWidth;

    switch (bandType) {
        case MW_BAND_TYPE:
        case LW_BAND_TYPE:
            newWidth = UICompSevenSegmentFreq::AM_BAND_WIDTH;
            break;
        case FM_BAND_TYPE:
            newWidth = UICompSevenSegmentFreq::FM_BAND_WIDTH;
            break;
        case SW_BAND_TYPE:
            newWidth = UICompSevenSegmentFreq::SW_BAND_WIDTH;
            break;
        default:
            newWidth = UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 25; // Alapértelmezett
            break;
    }

    sevenSegmentFreq->setWidth(newWidth);
}

/**
 * @brief Rotary encoder eseménykezelés - AM frekvencia hangolás implementáció
 * @param event Rotary encoder esemény (forgatás irány, érték, gombnyomás)
 * @return true ha sikeresen kezelte az eseményt, false egyébként
 *
 * @details AM frekvencia hangolás logika:
 * - Csak akkor reagál, ha nincs aktív dialógus
 * - Rotary klikket figyelmen kívül hagyja (más funkciókhoz)
 * - AM/MW/LW/SW frekvencia léptetés és mentés a band táblába
 * - Frekvencia kijelző azonnali frissítése
 * - Hasonló az FMScreen rotary kezeléshez, de AM-specifikus tartományokkal
 */
bool ScreenAMRadioBase::handleRotary(const RotaryEvent &event) {

    // Biztonsági ellenőrzés: Ha van dialóg, akkor nem kezeljük az eseményt itt
    if (UIScreen::isDialogActive()) {
        // Nem kezeltük az eseményt, továbbítjuk a szülő osztálynak (végső soron majd a dialógusokhoz)
        return UIScreen::handleRotary(event);
    }

    // Rotary klikk esemény kezelése - frekvencia lépés ciklus változtatása
    if (event.buttonState == RotaryEvent::ButtonState::Clicked) {
        if (sevenSegmentFreq) {
            sevenSegmentFreq->cycleFreqStep(); // Léptetjük a frekvencia lépés értékét
            // kilépünk, mert a tekergetés a klikk közben nem fogadjuk el
            return true;
        }
    }

    uint16_t newFreq;

    BandTable &currentBand = ::pSi4735Manager->getCurrentBand();

    // Az SI4735 osztály cache-ból olvassuk az aktuális frekvenciát, nem használunk chip olvasást
    uint16_t currentFrequency = ::pSi4735Manager->getSi4735().getCurrentFrequency();

    bool isCurrentDemodSSBorCW = ::pSi4735Manager->isCurrentDemodSSBorCW();
    if (isCurrentDemodSSBorCW) {

        if (rtv::bfoOn) {

            int16_t step = rtv::currentBFOStep;
            rtv::currentBFOmanu += (event.direction == RotaryEvent::Direction::Up) ? step : -step;
            rtv::currentBFOmanu = constrain(rtv::currentBFOmanu, -999, 999);

        } else {

            // Hangolás felfelé
            if (event.direction == RotaryEvent::Direction::Up) {

                rtv::freqDec = rtv::freqDec - rtv::freqstep;
                uint32_t freqTot = (uint32_t)(currentFrequency * 1000) + (rtv::freqDec * -1);

                if (freqTot > (uint32_t)(currentBand.maximumFreq * 1000)) {
                    ::pSi4735Manager->getSi4735().setFrequency(currentBand.maximumFreq);
                    rtv::freqDec = 0;
                }

                if (rtv::freqDec <= -16000) {
                    rtv::freqDec = rtv::freqDec + 16000;
                    int16_t freqPlus16 = currentFrequency + 16;
                    //::pSi4735Manager->hardwareAudioMuteOnInSSB(); //csak pattogást okoz, nem kell
                    ::pSi4735Manager->getSi4735().setFrequency(freqPlus16);
                    delay(10);
                }

            } else {

                // Hangolás lefelé
                rtv::freqDec = rtv::freqDec + rtv::freqstep;
                uint32_t freqTot = (uint32_t)(currentFrequency * 1000) - rtv::freqDec;
                if (freqTot < (uint32_t)(currentBand.minimumFreq * 1000)) {
                    ::pSi4735Manager->getSi4735().setFrequency(currentBand.minimumFreq);
                    rtv::freqDec = 0;
                }

                if (rtv::freqDec >= 16000) {
                    rtv::freqDec = rtv::freqDec - 16000;
                    int16_t freqMin16 = currentFrequency - 16;
                    //::pSi4735Manager->hardwareAudioMuteOn(); //csak pattogást okoz, nem kell
                    ::pSi4735Manager->getSi4735().setFrequency(freqMin16);
                    delay(10);
                }
            }
            rtv::currentBFO = rtv::freqDec;
            rtv::lastBFO = rtv::currentBFO;
        }

        // Lekérdezzük a beállított frekvenciát
        // Az SI4735 osztály cache-ból olvassuk az aktuális frekvenciát, nem használunk chip olvasást
        newFreq = ::pSi4735Manager->getSi4735().getCurrentFrequency();

        // SSB hangolás esetén a BFO eltolás beállítása
        const int16_t cwBaseOffset = (currentBand.currDemod == CW_DEMOD_TYPE) ? 750 : 0; // Ideiglenes konstans CW offset
        int16_t bfoToSet = cwBaseOffset + rtv::currentBFO + rtv::currentBFOmanu;
        ::pSi4735Manager->getSi4735().setSSBBfo(bfoToSet);

    } else {
        // Léptetjük a rádiót, ez el is menti a band táblába
        newFreq = ::pSi4735Manager->stepFrequency(event.value);
    }

    // AGC
    ::pSi4735Manager->checkAGC();

    // Frekvencia kijelző azonnali frissítése
    if (sevenSegmentFreq) {
        // SSB/CW módban mindig frissítjük a kijelzőt, mert a finomhangolás (rtv::freqDec)
        // változhat anélkül, hogy a chip frekvencia megváltozna
        sevenSegmentFreq->setFrequency(newFreq, isCurrentDemodSSBorCW);
    }

    // Memória státusz ellenőrzése és frissítése
    ScreenRadioBase::checkAndUpdateMemoryStatus();

    return true; // Esemény sikeresen kezelve
}

/**
 * @brief Folyamatos loop hívás - Event-driven optimalizált implementáció
 * @details Csak valóban szükséges frissítések - NINCS folyamatos gombállapot pollozás!
 *
 * Csak az alábbi komponenseket frissíti minden ciklusban:
 * - S-Meter (jelerősség) - valós idejű adat AM módban
 *
 * Gombállapotok frissítése CSAK:
 * - Képernyő aktiválásakor (activate() metódus)
 * - Specifikus eseményekkor (eseménykezelőkben)
 *
 * **Event-driven előnyök**:
 * - Jelentős teljesítményjavulás a polling-hoz képest
 * - CPU terhelés csökkentése
 * - Univerzális gombkezelés (CommonVerticalButtons)
 */
void ScreenAMRadioBase::handleOwnLoop() {

    // S-Meter (jelerősség) időzített frissítése - Közös RadioScreen implementáció
    ScreenRadioBase::updateSMeter(false /* AM mód */);
}

/**
 * @brief BFO gomb eseménykezelő - Beat Frequency Oscillator
 * @param event Gomb esemény (Clicked)
 * @details AM specifikus funkcionalitás - BFO állapot váltása és Step gomb frissítése
 */
void ScreenAMRadioBase::handleBFOButton(const UIButton::ButtonEvent &event) {

    // Csak változtatásra reagálunk, nem kattintásra
    if (event.state != UIButton::EventButtonState::On && event.state != UIButton::EventButtonState::Off) {
        return;
    }

    // Csak SSB/CW módban működik
    if (!::pSi4735Manager->isCurrentDemodSSBorCW()) {
        return;
    }

    // BFO állapot váltása
    rtv::bfoOn = !rtv::bfoOn;
    rtv::bfoTr = true; // BFO animáció trigger beállítása

    // A Step gombok állapotának frissítése
    this->updateStepButtonState();

    // Frissítjük a frekvencia kijelzőt is, hogy BFO állapot változás volt
    sevenSegmentFreq->forceFullRedraw();
}

/**
 * @brief AfBW gomb eseménykezelő - Audio Frequency Bandwidth
 * @param event Gomb esemény (Clicked)
 * @details AM specifikus funkcionalitás - sávszélesség váltás
 */
void ScreenAMRadioBase::handleAfBWButton(const UIButton::ButtonEvent &event) {
    if (event.state != UIButton::EventButtonState::Clicked) {
        return; // Csak kattintásra reagálunk
    }

    // Aktuális demodulációs mód
    uint8_t currDemodMod = ::pSi4735Manager->getCurrentBand().currDemod;
    // Jelenlegi sávszélesség felirata
    const char *currentBw = ::pSi4735Manager->getCurrentBandWidthLabel();

    // Megállapítjuk a lehetséges sávszélességek tömbjét
    const char *title;
    size_t labelsCount;
    const char **labels;
    uint16_t w = 250;
    uint16_t h = 170;

    if (currDemodMod == FM_DEMOD_TYPE) {
        title = "FM Filter in kHz";
        labels = ::pSi4735Manager->getBandWidthLabels(Band::bandWidthFM, labelsCount);

    } else if (currDemodMod == AM_DEMOD_TYPE) {
        title = "AM Filter in kHz";
        w = 350;
        h = 160;

        labels = ::pSi4735Manager->getBandWidthLabels(Band::bandWidthAM, labelsCount);

    } else {
        title = "SSB/CW Filter in kHz";
        w = 380;
        h = 130;

        labels = ::pSi4735Manager->getBandWidthLabels(Band::bandWidthSSB, labelsCount);
    }

    auto afBwDialog = std::make_shared<UIMultiButtonDialog>(
        this,                                                                                         // Képernyő referencia
        title, "",                                                                                    // Dialógus címe és üzenete
        labels, labelsCount,                                                                          // Gombok feliratai és számuk
        [this, currDemodMod](int buttonIndex, const char *buttonLabel, UIMultiButtonDialog *dialog) { // Gomb kattintás kezelése
            //

            if (currDemodMod == FM_DEMOD_TYPE) {
                config.data.bwIdxFM = ::pSi4735Manager->getBandWidthIndexByLabel(Band::bandWidthFM, buttonLabel);
            } else if (currDemodMod == AM_DEMOD_TYPE) {
                config.data.bwIdxAM = ::pSi4735Manager->getBandWidthIndexByLabel(Band::bandWidthAM, buttonLabel);
            } else {
                config.data.bwIdxSSB = ::pSi4735Manager->getBandWidthIndexByLabel(Band::bandWidthSSB, buttonLabel);
            }

            // Beállítjuk a rádió chip-en a kiválasztott HF sávszélességet
            ::pSi4735Manager->setAfBandWidth();

        },
        true,              // Automatikusan bezárja-e a dialógust gomb kattintáskor
        currentBw,         // Az alapértelmezett (jelenlegi) gomb felirata
        true,              // Ha true, az alapértelmezett gomb le van tiltva; ha false, csak vizuálisan kiemelve
        Rect(-1, -1, w, h) // Dialógus mérete (ha -1, akkor automatikusan a képernyő közepére igazítja)
    );
    this->showDialog(afBwDialog);
}

/**
 * @brief AntCap gomb eseménykezelő - Antenna Capacitor
 * @param event Gomb esemény (Clicked)
 */
void ScreenAMRadioBase::handleAntCapButton(const UIButton::ButtonEvent &event) {
    if (event.state != UIButton::EventButtonState::Clicked) {
        return;
    }
    BandTable &currband = ::pSi4735Manager->getCurrentBand(); // Kikeressük az aktuális Band rekordot

    // Az antCap értékének eltárolása egy helyi int változóban, hogy a dialógus működjön vele.
    static int antCapTempValue; // Statikus, hogy a dialógus élettartama alatt megmaradjon.
    antCapTempValue = static_cast<int>(currband.antCap);

    auto antCapDialog = std::make_shared<UIValueChangeDialog>(
        this,                                                                                                                        //
        "Antenna Tuning capacitor", "Capacitor value [pF]:",                                                                         //
        &antCapTempValue,                                                                                                            //
        1, currband.currDemod == FM_DEMOD_TYPE ? Si4735Constants::SI4735_MAX_ANT_CAP_FM : Si4735Constants::SI4735_MAX_ANT_CAP_AM, 1, //
        [this](const std::variant<int, float, bool> &liveNewValue) {
            if (std::holds_alternative<int>(liveNewValue)) {
                int currentDialogVal = std::get<int>(liveNewValue);
                ::pSi4735Manager->getCurrentBand().antCap = static_cast<uint16_t>(currentDialogVal);
                ::pSi4735Manager->getSi4735().setTuneFrequencyAntennaCapacitor(currentDialogVal);
            }
        },
        nullptr, // Callback a változásra
        Rect(-1, -1, 280, 0));
    this->showDialog(antCapDialog);
}

/**
 * @brief Demod gomb eseménykezelő - Demodulation
 * @param event Gomb esemény (Clicked)
 * @details AM specifikus funkcionalitás - alapértelmezett implementáció
 */
void ScreenAMRadioBase::handleDemodButton(const UIButton::ButtonEvent &event) {
    if (event.state != UIButton::EventButtonState::Clicked) {
        return;
    }

    uint8_t labelsCount;
    const char **labels = ::pSi4735Manager->getAmDemodulationModes(labelsCount);

    auto demodDialog = std::make_shared<UIMultiButtonDialog>(
        this,                                                                           // Képernyő referencia
        "Demodulation Mode", "",                                                        // Dialógus címe és üzenete
        labels, labelsCount,                                                            // Gombok feliratai és számuk
        [this](int buttonIndex, const char *buttonLabel, UIMultiButtonDialog *dialog) { // Gomb kattintás kezelése
            // Kikeressük az aktuális Band rekordot
            BandTable &currentband = ::pSi4735Manager->getCurrentBand();

            // FONTOS: Mentjük a jelenlegi frekvenciát, mielőtt megváltoztatjuk a demodulációs módot
            uint16_t currentFrequency = ::pSi4735Manager->getSi4735().getCurrentFrequency();
            currentband.currFreq = currentFrequency;

            // Demodulációs mód beállítása
            currentband.currDemod = buttonIndex + 1; // Az FM  mód indexe 0, azt kihagyjuk

            // Újra beállítjuk a sávot az új móddal (false -> ne a preferáltat töltse)
            ::pSi4735Manager->bandSet(false);

            // A demod mód változása után frissítjük a BFO és Step gombok állapotát
            // (fontos, mert SSB/CW módban mindkét gomb állapota más)
            this->updateBFOButtonState();
            this->updateStepButtonState();

        },
        true,                                           // Automatikusan bezárja-e a dialógust gomb kattintáskor
        ::pSi4735Manager->getCurrentBandDemodModDesc(), // Az alapértelmezett (jelenlegi) gomb felirata
        true,                                           // Ha true, az alapértelmezett gomb le van tiltva; ha false, csak vizuálisan kiemelve
        Rect(-1, -1, 320, 130)                          // Dialógus mérete (ha -1, akkor automatikusan a képernyő közepére igazítja)
    );

    this->showDialog(demodDialog);
}

/**
 * @brief BFO gomb állapotának frissítése
 * @details Csak SSB/CW módban engedélyezett
 */
void ScreenAMRadioBase::updateBFOButtonState() {
    if (!horizontalButtonBar) {
        return; // Biztonsági ellenőrzés
    }

    auto bfoButton = horizontalButtonBar->getButton(ScreenAMRadioBase::BFO_BUTTON);
    if (!bfoButton) {
        return;
    }

    if (::pSi4735Manager->isCurrentDemodSSBorCW()) {
        // SSB/CW módban: BFO állapot szerint be/ki kapcsolva
        bfoButton->setEnabled(true);
        bfoButton->setButtonState(rtv::bfoOn ? UIButton::ButtonState::On : UIButton::ButtonState::Off);
    } else {
        // AM/egyéb módban: letiltva
        bfoButton->setEnabled(false);
    }
}

/**
 * @brief Step gomb állapotának frissítése
 * @details SSB/CW módban csak akkor engedélyezett, ha BFO be van kapcsolva
 */
void ScreenAMRadioBase::updateStepButtonState() {

    if (!horizontalButtonBar) {
        return; // Biztonsági ellenőrzés
    }

    auto stepButton = horizontalButtonBar->getButton(ScreenAMRadioBase::STEP_BUTTON);
    if (!stepButton) {
        return;
    }

    bool enabled = true;
    if (::pSi4735Manager->isCurrentDemodSSBorCW()) {
        enabled = rtv::bfoOn;
    }

    stepButton->setEnabled(enabled);

    if (enabled) {
        stepButton->setButtonState(UIButton::ButtonState::Off);
    }
}