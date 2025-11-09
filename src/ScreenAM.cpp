#include "ScreenAM.h"
#include "PicoMemoryInfo.h"
#include "UIMultiButtonDialog.h"

namespace ScreenAMHorizontalButtonIDs {
static constexpr uint8_t DIGIT_BUTTON = 75; ///< CW/RTTY
} // namespace ScreenAMHorizontalButtonIDs

// =====================================================================
// Konstruktor és inicializálás
// =====================================================================

/**
 * @brief ScreenAM konstruktor implementáció - RadioScreen alaposztályból származik
 * @param tft TFT display referencia
 * @param si4735Manager Si4735 rádió chip kezelő referencia
 */
ScreenAM::ScreenAM() : ScreenAMRadioBase(SCREEN_NAME_AM) {
    // UI komponensek létrehozása és elhelyezése
    layoutComponents();
}

/**
 * @brief ScreenAM destruktor - erőforrások felszabadítása és memóriaszivárgás megelőzése
 * @details Biztosítja a proper cleanup-ot:
 * - Dekóderek leállítása és felszabadítása (CW, RTTY)
 * - UI komponensek cleanup-ja (TextBox, Spectrum)
 * - Shared_ptr referenciák nullázása
 * - Parent pointer cleanup (MiniAudioDisplay)
 */
ScreenAM::~ScreenAM() {}

/**
 * @brief Statikus képernyő tartalom kirajzolása - AM képernyő specifikus elemek
 * @details Csak a statikus UI elemeket rajzolja ki (nem változó tartalom):
 * - S-Meter skála vonalak és számok (AM módhoz optimalizálva)
 * - Band információs terület (AM/MW/LW/SW jelzők)
 * - Statikus címkék és szövegek
 *
 * A dinamikus tartalom (pl. S-Meter érték, frekvencia) a loop()-ban frissül.
 *
 * **TODO implementációk**:
 * - S-Meter skála: RSSI alapú AM skála (0-60 dB tartomány)
 * - Band indikátor: Aktuális band típus megjelenítése
 * - Frekvencia egység: kHz/MHz megfelelő formátumban
 */
void ScreenAM::drawContent() {

    //  Finomhangolás jel (aláhúzás) megjelenítése SSB/CW módokban, elrejtése egyéb módokban
    //  Itt állítjuk be, mert ha vált SW módban SSB-re, akkor is frissíteni kell a frekvencia kijelzőt a dialógus bezárásakor
    BandTable &currentband = ::pSi4735Manager->getCurrentBand();
    bool isSSBorCW = currentband.currDemod == LSB_DEMOD_TYPE || currentband.currDemod == USB_DEMOD_TYPE || currentband.currDemod == CW_DEMOD_TYPE;
    sevenSegmentFreq->setHideUnderline(!::pSi4735Manager->isCurrentDemodSSBorCW() && !isSSBorCW);
}

/**
 * @brief Képernyő aktiválása - Event-driven gombállapot szinkronizálás
 * @details Ez az EGYETLEN hely, ahol gombállapotokat szinkronizálunk!
 */
void ScreenAM::activate() {

    // Szülő osztály aktiválása
    ScreenAMRadioBase::activate();

    updateAllVerticalButtonStates(); // Univerzális funkcionális gombok (mixin method)
    updateHorizontalButtonStates();  // AM-specifikus gombok szinkronizálása
}

/**
 * @brief AM képernyő deaktiválása - audioDecoderTimer leállítása
 * @details Hívja meg a képernyőváltó logika, amikor elhagyjuk az AM képernyőt!
 */
void ScreenAM::deactivate() {

    // Szülő osztály deaktiválása
    ScreenRadioBase::deactivate();
}

// =====================================================================
// UIScreen interface megvalósítás
// =====================================================================

/**
 * @brief Dialógus bezárásának kezelése - Gombállapot szinkronizálás
 * @details Az utolsó dialógus bezárásakor frissíti a gombállapotokat
 *
 * Ez a metódus biztosítja, hogy a gombállapotok konzisztensek maradjanak
 * a dialógusok bezárása után. Különösen fontos a ValueChangeDialog-ok
 * (Volume, Attenuator, Squelch, Frequency) után.
 */
void ScreenAM::onDialogClosed(UIDialogBase *closedDialog) {

    // Először hívjuk a RadioScreen implementációt (band váltás kezelés)
    ScreenRadioBase::onDialogClosed(closedDialog);

    // Ha ez volt az utolsó dialógus, frissítsük a gombállapotokat
    if (!isDialogActive()) {
        updateAllVerticalButtonStates();                  // Függőleges gombok szinkronizálása
        updateCommonHorizontalButtonStates();             // Közös gombok szinkronizálása
        updateHorizontalButtonStates();                   // AM specifikus gombok szinkronizálása
        ScreenAMRadioBase::updateSevenSegmentFreqWidth(); // SevenSegmentFreq szélességének frissítése

        // A gombsor konténer teljes újrarajzolása, hogy biztosan megjelenjenek a gombok
        if (horizontalButtonBar) {
            horizontalButtonBar->markForRedraw(true);
        }
    }
}

// =====================================================================
// UI komponensek layout és management
// =====================================================================

/**
 * @brief UI komponensek létrehozása és képernyőn való elhelyezése
 */
void ScreenAM::layoutComponents() {

    // Frekvencia kijelző pozicionálás
    uint16_t FreqDisplayY = 20;
    Rect sevenSegmentFreqBounds(0, FreqDisplayY, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT + 10);

    // S-Meter komponens pozícionálása
    Rect smeterBounds(2, FreqDisplayY + UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT, SMeterConstants::SMETER_WIDTH, 70);

    // Szülő osztály layout meghívása (állapotsor, frekvencia, S-Meter)
    ScreenAMRadioBase::layoutComponents(sevenSegmentFreqBounds, smeterBounds);

    // Függőleges gombok létrehozása
    Mixin::createCommonVerticalButtons(); // UICommonVerticalButtons-ban definiált UIButtonsGroupManager alapú függőleges gombsor egyedi Memo kezelővel

    // Alsó közös + AM specifikus vízszintes gombsor az őstől
    ScreenRadioBase::createCommonHorizontalButtons();

    // ===================================================================
    // Spektrum vizualizáció komponens létrehozása
    // ===================================================================
    ScreenRadioBase::createSpectrumComponent(Rect(255, 60, 150, 80), RadioMode::AM);

    // ===================================================================
    // Audio dekóder konfigurálása dekóder nélkül (csak FFT lesz) 6kHz sávszélességgel, 1024-es mintavételi mérettel
    // ===================================================================
    ::audioController.startAudioController(DecoderId::ID_DECODER_ONLY_FFT, AM_AF_RAW_SAMPLES_SIZE, AM_AF_BANDWIDTH_HZ);
}

/**
 * @brief AM specifikus gombok hozzáadása a közös gombokhoz
 * @param buttonConfigs A már meglévő gomb konfigurációk vektora
 * @details Felülírja az ős metódusát, hogy hozzáadja az AM specifikus gombokat
 */
void ScreenAM::addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) {

    // Szülő osztály közös gombjainak legyártása
    ScreenAMRadioBase::addSpecificHorizontalButtons(buttonConfigs);

    // Step - Frequency Step gomb hozzáadása
    buttonConfigs.push_back(                          //
        {                                             //
         ScreenAMRadioBase::STEP_BUTTON,              //
         "Step",                                      //
         UIButton::ButtonType::Pushable,              //
         UIButton::ButtonState::Off,                  //
         [this](const UIButton::ButtonEvent &event) { //
             handleStepButton(event);                 //
         }});

    // Step - CW/RTTY gomb hozzáadása
    buttonConfigs.push_back(                          //
        {                                             //
         ScreenAMHorizontalButtonIDs::DIGIT_BUTTON,   //
         "Digit",                                     //
         UIButton::ButtonType::Pushable,              //
         UIButton::ButtonState::Off,                  //
         [this](const UIButton::ButtonEvent &event) { //
             getScreenManager()->switchToScreen(SCREEN_NAME_CW_RTTY);
         }});
}

// =====================================================================
// EVENT-DRIVEN GOMBÁLLAPOT SZINKRONIZÁLÁS
// =====================================================================

/**
 * @brief AM specifikus vízszintes gombsor állapotainak szinkronizálása
 * @details Event-driven architektúra: CSAK aktiváláskor hívódik meg!
 *
 * Szinkronizált állapotok:
 * - AM specifikus gombok alapértelmezett állapotai
 */
void ScreenAM::updateHorizontalButtonStates() {

    // BFO gomb update
    ScreenAMRadioBase::updateBFOButtonState();

    // Step gomb update
    ScreenAMRadioBase::updateStepButtonState();

    // Többi AM specifikus gomb alapértelmezett állapotban
    if (horizontalButtonBar) {
        horizontalButtonBar->setButtonState(ScreenAMRadioBase::AFBW_BUTTON, UIButton::ButtonState::Off);
        horizontalButtonBar->setButtonState(ScreenAMRadioBase::ANTCAP_BUTTON, UIButton::ButtonState::Off);
        horizontalButtonBar->setButtonState(ScreenAMRadioBase::DEMOD_BUTTON, UIButton::ButtonState::Off);
    }
}

// =====================================================================
// AM specifikus gomb eseménykezelők
// =====================================================================

/**
 * @brief Step gomb eseménykezelő - Frequency Step
 * @param event Gomb esemény (Clicked)
 * @details AM specifikus funkcionalitás - alapértelmezett implementáció
 */
void ScreenAM::handleStepButton(const UIButton::ButtonEvent &event) {
    if (event.state != UIButton::EventButtonState::Clicked) {
        return;
    }

    // Aktuális demodulációs mód
    uint8_t currMod = ::pSi4735Manager->getCurrentBand().currDemod;

    // Az aktuális freki lépés felirata
    const char *currentStepStr = ::pSi4735Manager->currentStepSizeStr();

    // Megállapítjuk a lehetséges lépések méretét
    const char *title;
    size_t labelsCount;
    const char **labels;
    uint16_t w = 290;
    uint16_t h = 130;

    if (rtv::bfoOn) {
        title = "Step tune BFO";
        labels = ::pSi4735Manager->getStepSizeLabels(Band::stepSizeBFO, labelsCount);

    } else if (currMod == FM_DEMOD_TYPE) {
        title = "Step tune FM";
        labels = ::pSi4735Manager->getStepSizeLabels(Band::stepSizeFM, labelsCount);
        w = 300;
        h = 100;
    } else {
        title = "Step tune AM/SSB";
        labels = ::pSi4735Manager->getStepSizeLabels(Band::stepSizeAM, labelsCount);
        w = 290;
        h = 130;
    }

    auto stepDialog = std::make_shared<UIMultiButtonDialog>(
        this,                                                                                    // Képernyő referencia
        title, "",                                                                               // Dialógus címe és üzenete
        labels, labelsCount,                                                                     // Gombok feliratai és számuk
        [this, currMod](int buttonIndex, const char *buttonLabel, UIMultiButtonDialog *dialog) { // Gomb kattintás kezelése
            // Kikeressük az aktuális Band rekordot
            BandTable &currentband = ::pSi4735Manager->getCurrentBand();

            // Kikeressük az aktuális Band típust
            uint8_t currentBandType = currentband.bandType;

            // SSB módban a BFO be van kapcsolva?
            if (rtv::bfoOn && ::pSi4735Manager->isCurrentDemodSSBorCW()) {

                // BFO step állítás - a buttonIndex közvetlenül használható
                rtv::currentBFOStep = ::pSi4735Manager->getStepSizeByIndex(Band::stepSizeBFO, buttonIndex);

            } else { // Nem SSB + BFO módban vagyunk

                // Beállítjuk a konfigban a stepSize-t - a buttonIndex közvetlenül használható
                if (currMod == FM_DEMOD_TYPE) {
                    // FM módban
                    config.data.ssIdxFM = buttonIndex;
                    currentband.currStep = ::pSi4735Manager->getStepSizeByIndex(Band::stepSizeFM, buttonIndex);

                } else {
                    // AM módban
                    if (currentBandType == MW_BAND_TYPE or currentBandType == LW_BAND_TYPE) {
                        // MW vagy LW módban
                        config.data.ssIdxMW = buttonIndex;
                    } else {
                        // Sima AM vagy SW módban
                        config.data.ssIdxAM = buttonIndex;
                    }
                }
                currentband.currStep = ::pSi4735Manager->getStepSizeByIndex(Band::stepSizeAM, buttonIndex);
            }
        },
        true,              // Automatikusan bezárja-e a dialógust gomb kattintáskor
        currentStepStr,    // Az alapértelmezett (jelenlegi) gomb felirata
        true,              // Ha true, az alapértelmezett gomb le van tiltva; ha false, csak vizuálisan kiemelve
        Rect(-1, -1, w, h) // Dialógus mérete (ha -1, akkor automatikusan a képernyő közepére igazítja)
    );

    this->showDialog(stepDialog);
}
