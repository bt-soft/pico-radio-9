/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenManager.h                                                                                               *
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
 * 		a licencet és a szerző nevét meg kell tartani a forrásban!                                                       *
 * -----                                                                                                               *
 * Last Modified: 2025.11.16, Sunday  09:51:09                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                             *
 * ----------	---	-------------------------------------------------------------------------------------------------    *
 */

#pragma once

#include <functional>
#include <map>
#include <queue>
#include <vector> // Navigációs stack-hez

#include "Config.h"
#include "IScreenManager.h"
#include "UIScreen.h"

// Deferred action struktúra - biztonságos képernyőváltáshoz
struct DeferredAction {
    enum Type { SwitchScreen, GoBack };

    Type type;
    const char *screenName;
    void *params;

    DeferredAction(Type t, const char *name = nullptr, void *p = nullptr) : type(t), screenName(name), params(p) {}
};

// Képernyő factory típus
using ScreenFactory = std::function<std::shared_ptr<UIScreen>()>;

/**
 * @brief Képernyőkezelő osztály
 */
class ScreenManager : public IScreenManager {

  private:
    std::map<String, ScreenFactory> screenFactories;
    std::shared_ptr<UIScreen> currentScreen;
    const char *previousScreenName;
    uint32_t lastActivityTime;

    // Navigációs stack - többszintű back navigációhoz
    std::vector<String> navigationStack;

    // Screensaver előtti képernyő neve - screensaver visszatéréshez
    String screenBeforeScreenSaver;

    // Deferred action queue - biztonságos képernyőváltáshoz
    std::queue<DeferredAction> deferredActions;
    bool processingEvents = false;

    void registerDefaultScreenFactories();

  public:
    // Képernyőkezelő osztály konstruktor
    ScreenManager();

    // Aktuális képernyő lekérdezése
    std::shared_ptr<UIScreen> getCurrentScreen() const;

    // Előző képernyő neve
    String getPreviousScreenName() const;

    // Képernyő factory regisztrálása
    void registerScreenFactory(const char *screenName, ScreenFactory factory);

    // Deferred képernyő váltás - biztonságos váltás eseménykezelés közben
    void deferSwitchToScreen(const char *screenName, void *params = nullptr);

    // Deferred vissza váltás
    void deferGoBack();

    // Deferred actions feldolgozása - a main loop-ban hívandó
    void processDeferredActions();

    // Képernyő váltás név alapján - biztonságos verzió - IScreenManager
    bool switchToScreen(const char *screenName, void *params = nullptr) override;

    // Azonnali képernyő váltás - csak biztonságos kontextusban hívható
    bool immediateSwitch(const char *screenName, void *params = nullptr, bool isBackNavigation = false);

    // Vissza az előző képernyőre - biztonságos verzió - IScreenManager
    bool goBack() override;

    // Azonnali visszaváltás - csak biztonságos kontextusban hívható
    bool immediateGoBack();

    // Touch esemény kezelése
    bool handleTouch(const TouchEvent &event);

    // Rotary encoder esemény kezelése
    bool handleRotary(const RotaryEvent &event);

    // Loop hívás
    void loop();

    /**
     * segédfüggvény a dialog állapot ellenőrzéséhez
     */
    bool isCurrentScreenDialogActive() override;
};
