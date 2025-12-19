/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenFM.cpp                                                                                                  *
 * Created Date: 2025.11.08.                                                                                           *
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
 * Last Modified: 2025.12.19, Friday  04:34:46                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "ScreenFM.h"

// ===================================================================
// FM képernyő specifikus vízszintes gomb azonosítók
// ===================================================================
namespace ScreenFMHorizontalButtonIDs {
static constexpr uint8_t SEEK_DOWN_BUTTON = 60; ///< Seek lefelé (pushable) - FM specifikus
static constexpr uint8_t SEEK_UP_BUTTON = 61;   ///< Seek felfelé (pushable) - FM specifikus
} // namespace ScreenFMHorizontalButtonIDs

// ===================================================================
// Konstruktor és inicializálás
// ===================================================================

/**
 * @brief ScreenFM konstruktor - FM rádió képernyő létrehozása
 *
 * @details Inicializálja az FM rádió képernyőt:
 * - Si4735 chip inicializálása
 * - UI komponensek elrendezése (gombsorok, kijelzők)
 * - Event-driven gombkezelés beállítása
 */
ScreenFM::ScreenFM() : ScreenRadioBase(SCREEN_NAME_FM) {

    // UI komponensek elhelyezése
    layoutComponents();
}

/**
 * @brief ScreenFM destruktor - erőforrások felszabadítása és memóriaszivárgás megelőzése
 * @details Biztosítja a proper cleanup-ot:
 * - RDS komponens cleanup-ja
 * - Stereo indikátor cleanup-ja
 * - Spectrum komponens cleanup-ja
 * - Shared_ptr referenciák nullázása
 */
ScreenFM::~ScreenFM() {
    DEBUG("ScreenFM::~ScreenFM() - Destruktor hívása - erőforrások felszabadítása\n");

    // ===================================================================
    // FM specifikus komponensek cleanup
    // ===================================================================
    if (stereoIndicator) {
        DEBUG("ScreenFM::~ScreenFM() - StereoIndicator cleanup\n");
        removeChild(stereoIndicator);
        stereoIndicator.reset();
    }

    if (rdsComponent) {
        DEBUG("ScreenFM::~ScreenFM() - RDSComponent cleanup\n");
        removeChild(rdsComponent);
        rdsComponent.reset();
    }

    if (spectrumComp) {
        DEBUG("ScreenFM::~ScreenFM() - SpectrumComponent cleanup\n");
        removeChild(spectrumComp);
        spectrumComp.reset();
    }

    DEBUG("ScreenFM::~ScreenFM() - Destruktor befejezve - memória felszabadítva\n");
}

// ===================================================================
// UI komponensek layout és elhelyezés
// ===================================================================

/**
 * @brief UI komponensek létrehozása és képernyőn való elhelyezése
 * @details Létrehozza és elhelyezi az összes UI elemet:
 * - Állapotsor (felül)
 * - Frekvencia kijelző (középen)
 * - S-Meter (jelerősség mérő)
 * - Függőleges gombsor (jobb oldal)
 * - Vízszintes gombsor (alul)
 */
void ScreenFM::layoutComponents() {

    // ===================================================================
    // Állapotsor komponens létrehozása (felső sáv)
    // ===================================================================
    ScreenRadioBase::createStatusLine();

    // ===================================================================
    // Frekvencia kijelző pozicionálás (képernyő közép)
    // ===================================================================
    uint16_t sevenSegmentFreq_Y = 20;
    Rect freqBounds(0, sevenSegmentFreq_Y, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 60, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT - 20);
    ScreenFrequDisplayBase::createSevenSegmentFreq(freqBounds);
    sevenSegmentFreq->setHideUnderline(true); // Alulvonás elrejtése a frekvencia kijelzőn

    // ===================================================================
    // STEREO/MONO jelző létrehozása és pozicionálása
    // ===================================================================
    uint16_t stereoY = sevenSegmentFreq_Y;
    Rect stereoBounds(UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH - 130, stereoY, 50, 20);
    stereoIndicator = std::make_shared<UICompStereoIndicator>(stereoBounds);
    addChild(stereoIndicator);

    // ===================================================================
    // RDS komponens létrehozása és pozicionálása
    // ===================================================================
    rdsComponent = std::make_shared<UICompRDS>(); // RDS komponens létrehozása, a pozíciókat a komponensen belül állítjuk be
    addChild(rdsComponent);

    // RDS Állomásnév közvetlenül a frekvencia kijelző alatt
    uint16_t currentY = sevenSegmentFreq_Y + UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT - 15;
    rdsComponent->setStationNameRect(Rect(2, currentY, 180, 32));

    // RDS Program típus közvetlenül az állomásnév alatt
    currentY += 37; // 18px magasság + 5px kisebb hézag
    rdsComponent->setProgramTypeRect(Rect(0, currentY, 135, 18));
    // Dátum/idő ugyanazon a soron, mint a program típus, jobbra igazítva
    rdsComponent->setDateTimeRect(Rect(0 + 130 + 5, currentY, 105, 18)); // Ugyanaz az Y pozíció, mint a program típus

    // RDS Radio text
    currentY += 35; // 18px magasság + kisebb hézag
    rdsComponent->setRadioTextRect(Rect(2, currentY, 300, 24));
    // ===================================================================
    // S-Meter komponens létrehozása - RadioScreen közös implementáció
    // ===================================================================
    currentY += 24 + 5;
    Rect smeterBounds(2, currentY, SMeterConstants::SMETER_WIDTH, 60);
    ScreenRadioBase::createSMeterComponent(smeterBounds);

    // ===================================================================
    // Spektrum vizualizáció komponens létrehozása
    // ===================================================================
    ScreenRadioBase::createSpectrumComponent(Rect(255, 40, 150, 80), RadioMode::FM, FM_AF_BANDWIDTH_HZ);

    // ===================================================================
    // Gombsorok létrehozása - Event-driven architektúra
    // ===================================================================
    this->createExtendedCommonVerticalButtons();      // Saját ButtonsGroupManager alapú függőleges gombsor egyedi Memo kezelővel
    ScreenRadioBase::createCommonHorizontalButtons(); // Alsó közös + FM specifikus vízszintes gombsor
}

/**
 * @brief Képernyő aktiválása - Event-driven gombállapot szinkronizálás
 * @details Meghívódik, amikor a felhasználó erre a képernyőre vált.
 *
 * Ez az EGYETLEN hely, ahol a gombállapotokat szinkronizáljuk a rendszer állapotával:
 * - Mute gomb szinkronizálása rtv::muteStat-tal
 * - AM gomb szinkronizálása aktuális band típussal
 * - További állapotok szinkronizálása (AGC, Attenuator, stb.)
 */
void ScreenFM::activate() {

    DEBUG("ScreenFM::activate() - Képernyő aktiválása\n");

    // Szülő osztály aktiválása (ScreenRadioBase -> ScreenFrequDisplayBase -> UIScreen)
    ScreenRadioBase::activate();
    Mixin::updateAllVerticalButtonStates(); // Univerzális funkcionális gombok (mixin method)

    // StatusLine frissítése
    ScreenRadioBase::checkAndUpdateMemoryStatus();

    // FM audio dekóder indítása (csak FFT, nincs dekóder)
    ::audioController.startAudioController( //
        DecoderId::ID_DECODER_ONLY_FFT,     //
        FM_AF_RAW_SAMPLES_SIZE,             //
        FM_AF_BANDWIDTH_HZ                  //
    );
    ::audioController.setAgcEnabled(false);            // AGC kikapcsolása
    ::audioController.setManualGain(1.0f);             // Manuális erősítés (1.0 = nincs extra erősítés)
    ::audioController.setSpectrumAveragingCount(0);    // Spektrum nem-koherens átlagolás: x db keret átlagolása, 0 = kikapcsolva
    ::audioController.setNoiseReductionEnabled(false); // Zajszűrés bekapcsolva (tisztább spektrum)
    ::audioController.setSmoothingPoints(0);           // Zajszűrés simítási pontok száma = 5 (erősebb zajszűrés, nincs frekvencia felbontási igény)
}

/**
 * @brief Képernyő deaktiválása - Audio dekóder leállítása
 * @details Meghívódik, amikor a felhasználó másik képernyőre vált.
 *
 * Biztosítja a proper cleanup-ot:
 * - Audio dekóder leállítása (DMA, Core1 kommunikáció)
 * - Megelőzi a memória szivárgást és DMA konfliktusokat
 */
void ScreenFM::deactivate() {

    DEBUG("ScreenFM::deactivate() - Képernyő deaktiválása\n");

    // Audio dekóder leállítása
    ::audioController.stopAudioController();

    // Szülő osztály deaktiválása
    ScreenRadioBase::deactivate();
}

// ===================================================================
// Felhasználói események kezelése - Event-driven architektúra
// ===================================================================

/**
 * @brief Rotary encoder eseménykezelés - Frekvencia hangolás
 * @param event Rotary encoder esemény (forgatás irány, érték, gombnyomás)
 * @return true ha sikeresen kezelte az eseményt, false egyébként
 *
 * @details FM frekvencia hangolás logika:
 * - Csak akkor reagál, ha nincs aktív dialógus
 * - Rotary klikket figyelmen kívül hagyja (más funkciókhoz)
 * - Frekvencia léptetés és mentés a band táblába
 * - Frekvencia kijelző azonnali frissítése
 */
bool ScreenFM::handleRotary(const RotaryEvent &event) {

    // Biztonsági ellenőrzés: csak aktív dialógus nélkül és nem klikk eseménykor
    if (!isDialogActive() && event.buttonState != RotaryEvent::ButtonState::Clicked) {

        // Frekvencia léptetés és automatikus mentés a band táblába
        // Beállítjuk a chip-en és le is mentjük a band táblába a frekvenciát
        uint16_t currFreq = ::pSi4735Manager->stepFrequency(event.value); // Léptetjük a rádiót
        ::pSi4735Manager->getCurrentBand().currFreq = currFreq;           // Beállítjuk a band táblában a frekit

        // RDS cache törlése frekvencia változás miatt
        if (rdsComponent) {
            rdsComponent->clearRdsOnFrequencyChange();
        }

        // Frekvencia kijelző azonnali frissítése
        if (sevenSegmentFreq) {
            sevenSegmentFreq->setFrequency(currFreq);
        }

        // Memória státusz ellenőrzése és frissítése
        checkAndUpdateMemoryStatus();

        return true; // Esemény sikeresen kezelve
    }

    // Ha nem kezeltük az eseményt, továbbítjuk a szülő osztálynak (dialógusokhoz)
    return UIScreen::handleRotary(event);
}

// ===================================================================
// Loop ciklus - Optimalizált teljesítmény
// ===================================================================

/**
 * @brief Folyamatos loop hívás - Csak valóban szükséges frissítések
 * @details Event-driven architektúra: NINCS folyamatos gombállapot pollozás!
 *
 * Csak az alábbi komponenseket frissíti minden ciklusban:
 * - S-Meter (jelerősség) - valós idejű adat
 *
 * Gombállapotok frissítése CSAK:
 * - Képernyő aktiválásakor (activate() metódus)
 * - Specifikus eseményekkor (eseménykezelőkben)
 */
void ScreenFM::handleOwnLoop() {

    // ===================================================================
    // S-Meter (jelerősség) időzített frissítése - Közös RadioScreen implementáció
    // ===================================================================
    // Az S-Meter saját frissítési időzítése van beépítve
    ScreenRadioBase::updateSMeter(true /* FM mód */);

    // ===================================================================
    // RDS adatok valós idejű frissítése
    // ===================================================================
    if (rdsComponent) {
        // 500ms frissítési időköz az RDS adatokhoz a scroll miatt ilyen sűrűn
        static uint32_t lastRdsCall = 0;
        if (Utils::timeHasPassed(lastRdsCall, 500)) {
            rdsComponent->updateRDS();
            lastRdsCall = millis();
        }
    }

    // A Stereo/Mono jelző frissítése 1 másodpercenként
    static uint32_t elapsedTimedValues = 0;
    if (Utils::timeHasPassed(elapsedTimedValues, 1000)) { // 1 másodpercenként frissítjük

        // ===================================================================
        // STEREO/MONO jelző frissítése
        // ===================================================================
        if (stereoIndicator) {
            // Si4735 stereo állapot lekérdezése
            bool isStereo = ::pSi4735Manager->getSi4735().getCurrentPilot();
            stereoIndicator->setStereo(isStereo);
        }
        elapsedTimedValues = millis();
    }
}

// ===================================================================
// Képernyő rajzolás és aktiválás
// ===================================================================

/**
 * @brief Statikus képernyő tartalom kirajzolása
 * @details Csak a statikus elemeket rajzolja ki (nem változó tartalom):
 * - S-Meter skála (vonalak, számok)
 *
 * A dinamikus tartalom (pl. S-Meter érték, spektrum oszlopok) a loop()-ban frissül.
 */
void ScreenFM::drawContent() {
    // S-Meter statikus skála kirajzolása (egyszer, a kezdetekkor)
    if (smeterComp) {
        smeterComp->drawSmeterScale();
    }
}

/**
 * @brief Dialógus bezárásának kezelése - Gombállapot szinkronizálás
 * @details Az utolsó dialógus bezárásakor frissíti a gombállapotokat
 *
 * Ez a metódus biztosítja, hogy a gombállapotok konzisztensek maradjanak
 * a dialógusok bezárása után. Különösen fontos a ValueChangeDialog-ok
 * (Volume, Attenuator, Squelch, Frequency) után.
 */
void ScreenFM::onDialogClosed(UIDialogBase *closedDialog) {

    // Először hívjuk a RadioScreen implementációt (band váltás kezelés)
    ScreenRadioBase::onDialogClosed(closedDialog);

    // Ha ez volt az utolsó dialógus, frissítsük a gombállapotokat
    if (!isDialogActive()) {
        updateAllVerticalButtonStates();      // Függőleges gombok szinkronizálása
        updateCommonHorizontalButtonStates(); // Közös gombok szinkronizálása
        updateHorizontalButtonStates();       // FM specifikus gombok szinkronizálása

        // A gombsor konténer teljes újrarajzolása, hogy biztosan megjelenjenek a gombok
        if (horizontalButtonBar) {
            horizontalButtonBar->markForRedraw(true); // A konténert és annak összes gyerekét újra kell rajzolni.
        }
    }
}

// ===================================================================
// Vízszintes gombsor - Alsó navigációs gombok
// ===================================================================

/**
 * @brief FM specifikus gombok hozzáadása a közös gombokhoz
 * @param buttonConfigs A már meglévő gomb konfigurációk vektora
 * @details Felülírja az ős metódusát, hogy hozzáadja az FM specifikus gombokat
 */
void ScreenFM::addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) {
    // FM specifikus gombok hozzáadása a közös gombok után

    // 1. SEEK DOWN - Automatikus hangolás lefelé
    buttonConfigs.push_back({ScreenFMHorizontalButtonIDs::SEEK_DOWN_BUTTON, //
                             "Seek-",                                       //
                             UIButton::ButtonType::Pushable,                //
                             UIButton::ButtonState::Off,                    //
                             [this](const UIButton::ButtonEvent &event) {   //
                                 handleSeekDownButton(event);
                             }});

    // 2. SEEK UP - Automatikus hangolás felfelé
    buttonConfigs.push_back({                                             //
                             ScreenFMHorizontalButtonIDs::SEEK_UP_BUTTON, //
                             "Seek+",                                     //
                             UIButton::ButtonType::Pushable,              //
                             UIButton::ButtonState::Off,                  //
                             [this](const UIButton::ButtonEvent &event) { //
                                 handleSeekUpButton(event);
                             }});
}

/**
 * @brief Vízszintes gombsor létrehozása a képernyő alján
 * @details 4 navigációs gomb elhelyezése vízszintes elrendezésben:
 *
 * Gombsor pozíció: Bal alsó sarok, 4 gomb szélessége
 * Gombok (balról jobbra):
 * 1. SEEK DOWN - Automatikus hangolás lefelé (Pushable)
 * 2. SEEK UP - Automatikus hangolás felfelé (Pushable)
 * 3. AM - AM képernyőre váltás (Pushable)
 * 4. Test - Test képernyőre váltás (Pushable)
 */
/**
 * @brief FM specifikus vízszintes gombsor állapotainak szinkronizálása
 * @details Event-driven architektúra: CSAK aktiváláskor hívódik meg!
 *
 * Szinkronizált állapotok:
 * - FM specifikus gombok (Seek-, Seek+) alapértelmezett állapotai
 */
void ScreenFM::updateHorizontalButtonStates() {
    if (!horizontalButtonBar) {
        return; // Biztonsági ellenőrzés
    }

    // ===================================================================
    // FM specifikus gombok állapot szinkronizálása
    // ===================================================================

    // Seek gombok alapértelmezett állapotban (kikapcsolva)
    horizontalButtonBar->setButtonState(ScreenFMHorizontalButtonIDs::SEEK_DOWN_BUTTON, UIButton::ButtonState::Off);
    horizontalButtonBar->setButtonState(ScreenFMHorizontalButtonIDs::SEEK_UP_BUTTON, UIButton::ButtonState::Off);
}

// ===================================================================
// Vízszintes gomb eseménykezelők - Seek és navigációs funkciók
// ===================================================================

/**
 * @brief SEEK DOWN gomb eseménykezelő - Automatikus hangolás lefelé
 * @param event Gomb esemény (Clicked)
 *
 * @details Pushable gomb: Automatikus állomáskeresés lefelé
 * A seek során valós időben frissíti a frekvencia kijelzőt
 */
void ScreenFM::handleSeekDownButton(const UIButton::ButtonEvent &event) {
    if (event.state == UIButton::EventButtonState::Clicked) {
        // RDS cache törlése seek indítása előtt
        clearRDSCache(); // Seek lefelé a RadioScreen metódusával
        seekStationDown();

        // Seek befejezése után: RDS és memória státusz frissítése
        clearRDSCache();
        checkAndUpdateMemoryStatus();
    }
}

/**
 * @brief SEEK UP gomb eseménykezelő - Automatikus hangolás felfelé
 * @param event Gomb esemény (Clicked)
 *
 * @details Pushable gomb: Automatikus állomáskeresés felfelé
 * A seek során valós időben frissíti a frekvencia kijelzőt
 */
void ScreenFM::handleSeekUpButton(const UIButton::ButtonEvent &event) {
    if (event.state == UIButton::EventButtonState::Clicked) {
        // RDS cache törlése seek indítása előtt
        clearRDSCache(); // Seek felfelé a RadioScreen metódusával
        seekStationUp();

        // Seek befejezése után: RDS és memória státusz frissítése
        clearRDSCache();
        checkAndUpdateMemoryStatus();
    }
}

/**
 * @brief Egyedi MEMO gomb eseménykezelő - Intelligens memória kezelés
 * @param event Gomb esemény (Clicked)
 *
 * @details Ha az aktuális állomás még nincs a memóriában és van RDS állomásnév,
 * akkor automatikusan megnyitja a MemoryScreen-t név szerkesztő dialógussal
 */
void ScreenFM::handleMemoButton(const UIButton::ButtonEvent &event) {

    if (event.state != UIButton::EventButtonState::Clicked) {
        return;
    }

    auto screenManager = UIScreen::getScreenManager();

    // Ellenőrizzük, hogy az aktuális állomás már a memóriában van-e
    bool isInMemory = checkCurrentFrequencyInMemory();                // RDS állomásnév lekérése (ha van)
    String rdsStationName = ::pSi4735Manager->getCachedStationName(); // Ha új állomás és van RDS név, akkor automatikus hozzáadás

    DEBUG("ScreenFM::handleMemoButton() - Current frequency in memory: %s, RDS station name: %s\n", isInMemory ? "Yes" : "No", rdsStationName.c_str());

    // Paraméter átadása a MemoryScreen-nek
    if (!isInMemory && rdsStationName.length() > 0) {
        auto stationNamePtr = new std::shared_ptr<char>(new char[rdsStationName.length() + 1], std::default_delete<char[]>());
        strcpy(stationNamePtr->get(), rdsStationName.c_str());
        DEBUG("ScreenFM::handleMemoButton() - Navigating to MemoryScreen with RDS station name: %s\n", rdsStationName.c_str());
        screenManager->switchToScreen(SCREEN_NAME_MEMORY, stationNamePtr);
    } else {
        // Ha már a memóriában van, akkor csak visszalépünk a Memória képernyőre
        DEBUG("ScreenFM::handleMemoButton() - Navigating to MemoryScreen without RDS station name\n");
        screenManager->switchToScreen(SCREEN_NAME_MEMORY);
    }
}

/**
 * @brief Egyedi függőleges gombok létrehozása - Memo gomb override-dal
 * @details Felülírja a CommonVerticalButtons alapértelmezett Memo kezelőjét,
 * mert FM módban - ha van - átadjuk az RDS állomásnevet is a MemoryScreen-nek
 */
void ScreenFM::createExtendedCommonVerticalButtons() {

    // Alapértelmezett gombdefiníciók lekérése
    const auto &baseDefs = UICommonVerticalButtons::getButtonDefinitions();

    // Egyedi gombdefiníciók lista létrehozása
    std::vector<UIButtonGroupDefinition> customDefs;
    customDefs.reserve(baseDefs.size());

    // Végigmegyünk az alapértelmezett definíciókon
    for (const auto &def : baseDefs) {
        std::function<void(const UIButton::ButtonEvent &)> callback;

        // Memo gomb speciális kezelése az RDS állomásnév átadásának logikájához
        if (def.id == VerticalButtonIDs::MEMO) {
            // Egyedi Memo handler használata
            callback = [this](const UIButton::ButtonEvent &e) { this->handleMemoButton(e); };

        } else if (def.handler != nullptr) {
            // A többi gombnál az eredeti handlerek használata
            callback = [screen = this, handler = def.handler](const UIButton::ButtonEvent &e) { handler(e, screen); };

        } else {
            // No-op callback üres handlerekhez
            callback = [](const UIButton::ButtonEvent &e) { /* no-op */ };
        }

        // Gombdefiníció hozzáadása a listához
        customDefs.push_back({def.id, def.label, def.type, callback, def.initialState,
                              60, // uniformWidth
                              def.height});
    }

    // Gombok létrehozása és elhelyezése
    UIButtonsGroupManager<ScreenFM>::layoutVerticalButtonGroup(customDefs, &createdVerticalButtons, 0, 0, 5, 60, 32, 3, 4);
}
