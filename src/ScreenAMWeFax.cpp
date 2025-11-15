#include "ScreenAMWeFax.h"
#include "ScreenManager.h"
#include "defines.h"

// WeFax Dekóder képernyő működés debug engedélyezése de csak DEBUG módban
#define __WEFAX_DECODER_DEBUG
#if defined(__DEBUG) && defined(__WEFAX_DECODER_DEBUG)
#define WEFAX_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define WEFAX_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

/**
 * @brief ScreenAMWeFax konstruktor
 */
ScreenAMWeFax::ScreenAMWeFax() : ScreenAMRadioBase(SCREEN_NAME_DECODER_WEFAX), UICommonVerticalButtons::Mixin<ScreenAMWeFax>() {
    // UI komponensek létrehozása és elhelyezése
    layoutComponents();
}

/**
 * @brief ScreenAMWeFax destruktor
 */
ScreenAMWeFax::~ScreenAMWeFax() {}

/**
 * @brief UI komponensek létrehozása és képernyőn való elhelyezése
 */
void ScreenAMWeFax::layoutComponents() {

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

    // Induláskor beállítjuk a Waterfall megjelenítési módot
    ScreenRadioBase::spectrumComp->setCurrentDisplayMode(UICompSpectrumVis::DisplayMode::Waterfall);

    // MEGJEGYZÉS: Az audioController indítása az activate() metódusban történik
    // hogy képernyőváltáskor megfelelően leálljon és újrainduljon
}

/**
 * @brief WeFax specifikus gombok hozzáadása a közös AM gombokhoz
 * @param buttonConfigs A már meglévő gomb konfigurációk vektora
 * @details Felülírja az ős metódusát, hogy hozzáadja a WeFax-specifikus gombokat
 */
void ScreenAMWeFax::addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) {

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
void ScreenAMWeFax::activate() {

    // Szülő osztály aktiválása
    ScreenAMRadioBase::activate();

    // WeFax audio dekóder indítása
    ::audioController.startAudioController( //
        DecoderId::ID_DECODER_WEFAX,        // WeFax dekóder azonosító
        WEFAX_RAW_SAMPLES_SIZE,             // sampleCount
        WEFAX_AF_BANDWIDTH_HZ               // bandwidthHz
    );
    ::audioController.setNoiseReductionEnabled(true); // Zajszűrés beapcsolva (tisztább spektrum)
    ::audioController.setSmoothingPoints(5);          // Zajszűrés simítási pontok száma = 5 (erősebb zajszűrés, nincs frekvencia felbontási igény)
}

/**
 * @brief Képernyő deaktiválása
 */
void ScreenAMWeFax::deactivate() {

    // Audio dekóder leállítása
    ::audioController.stopAudioController();

    // Szülő osztály deaktiválása
    ScreenAMRadioBase::deactivate();
}

/**
 * @brief Folyamatos loop hívás
 */
void ScreenAMWeFax::handleOwnLoop() {
    // Szülő osztály loop kezelése (S-Meter frissítés, stb.)
    ScreenAMRadioBase::handleOwnLoop();

    // WeFax dekódolt képsor frissítése
    this->checkDecodedData();
}

/**
 * @brief WeFax dekódolt kép ellenőrzése és frissítése
 */
void ScreenAMWeFax::checkDecodedData() {}
