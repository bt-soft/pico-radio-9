/**
 * @file ScreenManager.cpp
 * @brief ScreenManager osztály implementációja
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */
#include "ScreenManager.h"

// #include "ScreenAM.h"
// #include "ScreenCwRtty.h"
#include "ScreenFM.h"
#include "ScreenMemory.h"
// #include "ScreenScan.h"
#include "ScreenScreenSaver.h"
// #include "ScreenSetup.h"
// #include "ScreenSetupAudioProc.h"
// #include "ScreenSetupSi4735.h"
// #include "ScreenSetupSystem.h"

// Fejlesztői képernyők
// #include "ScreenEmpty.h"
// #include "ScreenTest.h"

/**
 * @brief Konstruktor - regisztrálja az alapértelmezett képernyő factory-kat
 */
ScreenManager::ScreenManager() : previousScreenName(nullptr), lastActivityTime(millis()) { registerDefaultScreenFactories(); }

/**
 * @brief Alapértelmezett képernyő factory-k regisztrálása
 */
void ScreenManager::registerDefaultScreenFactories() {
    registerScreenFactory(SCREEN_NAME_FM, []() { return std::make_shared<ScreenFM>(); });
    // registerScreenFactory(SCREEN_NAME_AM, []() { return std::make_shared<ScreenAM>(); });
    registerScreenFactory(SCREEN_NAME_SCREENSAVER, []() { return std::make_shared<ScreenScreenSaver>(); });
    registerScreenFactory(SCREEN_NAME_MEMORY, []() { return std::make_shared<ScreenMemory>(); });
    // registerScreenFactory(SCREEN_NAME_SCAN, []() { return std::make_shared<ScreenScan>(); });

    // // Setup képernyők regisztrálása
    // registerScreenFactory(SCREEN_NAME_SETUP, []() { return std::make_shared<ScreenSetup>(); });
    // registerScreenFactory(SCREEN_NAME_SETUP_SYSTEM, []() { return std::make_shared<ScreenSetupSystem>(); });
    // registerScreenFactory(SCREEN_NAME_SETUP_SI4735, []() { return std::make_shared<ScreenSetupSi4735>(); });
    // registerScreenFactory(SCREEN_NAME_SETUP_AUDIO_PROC, []() { return std::make_shared<ScreenSetupAudioProc>(); });

    // registerScreenFactory(SCREEN_NAME_CW_RTTY, []() { return std::make_shared<ScreenCwRtty>(); });

    // Teszt képernyők regisztrálása
    // registerScreenFactory(SCREEN_NAME_TEST, []() { return std::make_shared<ScreenTest>(); });
    // registerScreenFactory(SCREEN_NAME_EMPTY, []() { return std::make_shared<ScreenEmpty>(); });
}

/**
 * @brief Aktuális képernyő lekérdezése
 * @return Aktuális UIScreen shared_ptr
 */
std::shared_ptr<UIScreen> ScreenManager::getCurrentScreen() const { return currentScreen; }

/**
 * @brief Előző képernyő neve
 * @return Előző képernyő neve
 */
String ScreenManager::getPreviousScreenName() const { return previousScreenName; }

/**
 * @brief Képernyő factory regisztrálása
 * @param screenName A képernyő egyedi neve
 * @param factory A képernyő létrehozó függvénye
 */
void ScreenManager::registerScreenFactory(const char *screenName, ScreenFactory factory) { screenFactories[screenName] = factory; }

/**
 * @brief Deferred képernyő váltás - biztonságos váltás eseménykezelés közben
 * @param screenName A cél képernyő neve
 * @param params Opcionális paraméterek a képernyőnek
 */
void ScreenManager::deferSwitchToScreen(const char *screenName, void *params) { deferredActions.push(DeferredAction(DeferredAction::SwitchScreen, screenName, params)); }

/**
 * @brief Deferred vissza váltás
 */
void ScreenManager::deferGoBack() { deferredActions.push(DeferredAction(DeferredAction::GoBack)); }

/**
 * @brief Deferred actions feldolgozása - a main loop-ban hívandó
 */
void ScreenManager::processDeferredActions() {

    while (!deferredActions.empty()) {
        const DeferredAction &action = deferredActions.front();

        if (action.type == DeferredAction::SwitchScreen) {
            immediateSwitch(action.screenName, action.params);
        } else if (action.type == DeferredAction::GoBack) {
            immediateGoBack();
        }

        deferredActions.pop();
    }
}

/**
 * @brief Képernyő váltás név alapján - biztonságos verzió - IScreenManager
 * @param screenName A cél képernyő neve
 * @param params Opcionális paraméterek a képernyőnek
 * @return true, ha a váltás sikeres volt, false egyébként
 */
bool ScreenManager::switchToScreen(const char *screenName, void *params) {
    if (processingEvents) {
        // Eseménykezelés közben - halasztott váltás
        deferSwitchToScreen(screenName, params);
        return true;
    } else {
        // Biztonságos - azonnali váltás
        return immediateSwitch(screenName, params);
    }
}

/**
 * @brief Azonnali képernyő váltás - csak biztonságos kontextusban hívható
 * @param screenName A cél képernyő neve
 * @param params Opcionális paraméterek a képernyőnek
 * @param isBackNavigation Vissza navigációs jelző
 * @return true, ha a váltás sikeres volt, false egyébként
 */
bool ScreenManager::immediateSwitch(const char *screenName, void *params, bool isBackNavigation) {

    // Ha már ez a képernyő aktív, nem csinálunk semmit
    if (currentScreen && STREQ(currentScreen->getName(), screenName)) {
        return true;
    }

    // Factory keresése
    auto it = screenFactories.find(screenName);
    if (it == screenFactories.end()) {
        DEBUG("ScreenManager: nem található a(z) '%s' képernyő létrehozó függvénye\n", screenName);
        Utils::beepError();
        return false;
    }

    // Navigációs stack kezelése KÉPERNYŐVÁLTÁS ELŐTT - csak forward navigációnál
    if (currentScreen && !isBackNavigation) {
        const char *currentName = currentScreen->getName();

        // SCREENSAVER SPECIÁLIS KEZELÉS:
        if (STREQ(screenName, SCREEN_NAME_SCREENSAVER)) {
            // Ha képernyővédőre váltunk, eltároljuk az aktuális képernyő nevét
            screenBeforeScreenSaver = String(currentName);

        } else if (!STREQ(currentName, SCREEN_NAME_SCREENSAVER)) {
            // Normál forward navigáció - jelenlegi képernyő hozzáadása a stackhez
            // (de csak ha nem screensaver-ről váltunk)
            navigationStack.push_back(String(currentName));
        }

    } else if (isBackNavigation) {
        // Vissza navigáció - nem adunk hozzá a stackhez
    }

    // Jelenlegi képernyő törlése
    if (currentScreen) {
        const char *currentName = currentScreen->getName();

        // previousScreenName csak akkor frissül, ha nem képernyővédőre váltunk
        // Ez biztosítja, hogy a képernyővédő után vissza tudjunk térni az eredeti képernyőre
        if (!STREQ(screenName, SCREEN_NAME_SCREENSAVER)) {
            previousScreenName = currentName;
        }

        currentScreen->deactivate();
        currentScreen.reset(); // Memória felszabadítása
    }

    // TFT display törlése a képernyőváltás előtt
    ::tft.fillScreen(TFT_BLACK);

    // Új képernyő létrehozása
    currentScreen = it->second();
    if (currentScreen) {
        currentScreen->setScreenManager(this);
        if (params) {
            currentScreen->setParameters(params);
        }
        // Fontos: Az activate() hívása *előtt* állítjuk be a lastActivityTime-ot,
        // ha nem a képernyővédőre váltunk, hogy az activate() felülírhassa, ha akarja.
        if (!STREQ(screenName, SCREEN_NAME_SCREENSAVER)) {
            lastActivityTime = millis();
        }
        currentScreen->activate();
        return true;
    } else {
        DEBUG("ScreenManager: Hiba történt a(z) '%s' képernyő létrehozásakor\n", screenName);
    }
    return false;
}

/**
 * @brief Vissza az előző képernyőre - biztonságos verzió - IScreenManager
 * @return true, ha a váltás sikeres volt, false egyébként
 */
bool ScreenManager::goBack() {
    if (processingEvents) {
        // Eseménykezelés közben - halasztott váltás
        deferGoBack();
        return true;
    } else {
        // Biztonságos - azonnali váltás
        return immediateGoBack();
    }
}

/**
 * @brief Azonnali visszaváltás - csak biztonságos kontextusban hívható
 * @return true, ha a váltás sikeres volt, false egyébként
 */
bool ScreenManager::immediateGoBack() {
    // Speciális kezelés: ha screensaver-ből jövünk vissza
    if (currentScreen && STREQ(currentScreen->getName(), SCREEN_NAME_SCREENSAVER)) {
        if (!screenBeforeScreenSaver.isEmpty()) {
            String targetScreen = screenBeforeScreenSaver;
            screenBeforeScreenSaver = String();                          // Clear after use
            return immediateSwitch(targetScreen.c_str(), nullptr, true); // isBackNavigation = true
        }
    }

    // Navigációs stack használata a többszintű back navigációhoz
    if (!navigationStack.empty()) {
        String previousScreen = navigationStack.back();
        navigationStack.pop_back();
        return immediateSwitch(previousScreen.c_str(), nullptr, true); // isBackNavigation = true
    }

    // Fallback - régi egyszintű viselkedés
    if (previousScreenName != nullptr) {
        return immediateSwitch(previousScreenName, nullptr, true); // isBackNavigation = true
    }

    return false;
}

/**
 * @brief Touch esemény kezelése
 * @param event TouchEvent referencia
 * @return true, ha az eseményt kezelte a képernyő, false egyébként
 */
bool ScreenManager::handleTouch(const TouchEvent &event) {
    if (currentScreen) {
        if (!STREQ(currentScreen->getName(), SCREEN_NAME_SCREENSAVER)) {
            lastActivityTime = millis();
        }
        processingEvents = true;
        bool result = currentScreen->handleTouch(event);
        processingEvents = false;

        return result;
    }
    return false;
}

/**
 * @brief Rotary encoder esemény kezelése
 * @param event RotaryEvent referencia
 * @return true, ha az eseményt kezelte a képernyő, false egyébként
 */
bool ScreenManager::handleRotary(const RotaryEvent &event) {
    if (currentScreen) {
        if (!STREQ(currentScreen->getName(), SCREEN_NAME_SCREENSAVER)) {
            lastActivityTime = millis();
        }
        processingEvents = true;
        bool result = currentScreen->handleRotary(event);
        processingEvents = false;
        return result;
    }
    return false;
}

/**
 * @brief Loop hívás
 */
void ScreenManager::loop() {

    // Először a halasztott műveletek feldolgozása
    processDeferredActions();

    // Ha nincs aktuális képernyő, akkor nem megyünk tovább
    if (!currentScreen) {
        return;
    }

    // Képernyővédő időzítő ellenőrzése
    uint32_t screenSaverTimeoutMs = config.data.screenSaverTimeoutMinutes * 60 * 1000; // Percek milliszekundumra konvertálva
    if (screenSaverTimeoutMs > 0 &&                                                    // Ha a képernyővédő engedélyezve van (idő > 0)
        !STREQ(currentScreen->getName(), SCREEN_NAME_SCREENSAVER) &&                   // És nem a képernyővédőn vagyunk
        lastActivityTime != 0 &&                                                       // És volt már aktivitás
        (millis() - lastActivityTime > screenSaverTimeoutMs)) {                        // És lejárt az idő

        switchToScreen(SCREEN_NAME_SCREENSAVER);
        // lastActivityTime frissül, amikor a felhasználó újra interakcióba lép a képernyővédőn,
        // és visszaváltáskor az immediateSwitch-ben.
    }

    // Csak akkor rajzolunk, ha valóban szükséges
    if (currentScreen->isRedrawNeeded()) {
        currentScreen->draw();
    }

    // Megíhvjuk az aktuális képernyő loop-ját is
    currentScreen->loop();
}

/**
 * Van aktív dialógus az aktuális képernyőn?
 * @return true, ha van aktív dialógus, false egyébként
 */
bool ScreenManager::isCurrentScreenDialogActive() {

    auto currentScreen = this->getCurrentScreen();
    if (currentScreen == nullptr) {
        return false;
    }

    return currentScreen->isDialogActive();
}
