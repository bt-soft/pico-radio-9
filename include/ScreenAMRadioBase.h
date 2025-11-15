#pragma once

#include "ScreenRadioBase.h"

/**
 * @brief AM rádió képernyő alap osztály
 * @details Közös AM specifikus funkcionalitás és UI elemek kezelése
 */
class ScreenAMRadioBase : public ScreenRadioBase {

  public:
    /**
     * @brief Közös AM képernyő specifikus vízszintes gomb azonosítók
     * @details Alsó vízszintes gombsor - AM specifikus funkcionalitás
     */
    static constexpr uint8_t BFO_BUTTON = 70;    ///< Beat Frequency Oscillator
    static constexpr uint8_t ANTCAP_BUTTON = 71; ///< Antenna Capacitor
    static constexpr uint8_t DEMOD_BUTTON = 72;  ///< Demodulation
    static constexpr uint8_t AFBW_BUTTON = 73;   ///< Audio Filter Bandwidth
    static constexpr uint8_t STEP_BUTTON = 74;   ///< Step tuning

    ScreenAMRadioBase(const char *screenName);

    /**
     * @brief Képernyő aktiválása - Event-driven gombállapot szinkronizálás
     * @details Ez az EGYETLEN hely, ahol gombállapotokat szinkronizálunk!
     *
     * Szinkronizálási pontok:
     * - Mute gomb ↔ rtv::muteStat állapot
     * - FM gomb ↔ aktuális band típus (AM vs FM)
     * - AGC/Attenuator gombok ↔ Si4735 állapotok (TODO)
     * - Bandwidth gomb ↔ AM szűrő beállítások
     */
    virtual void activate() override;

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
    virtual bool handleRotary(const RotaryEvent &event) override;

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
    virtual void handleOwnLoop() override;

  protected:
    /**
     * @brief UI komponensek létrehozása és képernyőn való elhelyezése
     * @details Létrehozza és pozicionálja az összes UI elemet:
     * - Állapotsor (felül)
     * - Frekvencia kijelző (középen)
     * - S-Meter (jelerősség mérő)
     * - Függőleges gombsor (jobb oldal) - Közös FMScreen-nel
     * - Vízszintes gombsor (alul) - FM gombbal
     */
    virtual void layoutComponents(Rect sevenSegmentFreqBounds, Rect smeterBounds);

    /**
     * @brief Frissíti a vízszintes gombok állapotát
     * @details Közös gombok állapot frissítése az aktuális rádió állapot alapján
     */
    virtual void updateHorizontalButtonStates();

    /**
     * @brief Frissíti a SevenSegmentFreq szélességét az aktuális band típus alapján
     * @details Dinamikusan állítja be a frekvencia kijelző szélességét
     */
    void updateSevenSegmentFreqWidth();

    /**
     * @brief AM specifikus gombok hozzáadása a közös gombokhoz
     * @param buttonConfigs A már meglévő gomb konfigurációk vektora
     * @details Felülírja az ős metódusát, hogy hozzáadja az AM specifikus gombokat
     */
    virtual void addSpecificHorizontalButtons(std::vector<UIHorizontalButtonBar::ButtonConfig> &buttonConfigs) override;

    /**
     * @brief BFO gomb eseménykezelő - Beat Frequency Oscillator
     * @param event Gomb esemény (Clicked)
     * @details AM specifikus funkcionalitás
     */
    void handleBFOButton(const UIButton::ButtonEvent &event);

    /**
     * @brief AfBW gomb eseménykezelő - Audio Filter Bandwidth
     * @param event Gomb esemény (Clicked)
     * @details AM specifikus funkcionalitás
     */
    void handleAfBWButton(const UIButton::ButtonEvent &event);

    /**
     * @brief AntCap gomb eseménykezelő - Antenna Capacitor
     * @param event Gomb esemény (Clicked)
     * @details AM specifikus funkcionalitás
     */
    void handleAntCapButton(const UIButton::ButtonEvent &event);

    /**
     * @brief Demod gomb eseménykezelő - Demodulation
     * @param event Gomb esemény (Clicked)
     * @details AM specifikus funkcionalitás
     */
    void handleDemodButton(const UIButton::ButtonEvent &event);

    /**
     * @brief BFO gomb állapotának frissítése
     * @details Csak SSB/CW módban engedélyezett
     */
    void updateBFOButtonState();

    /**
     * @brief Step gomb állapotának frissítése
     * @details SSB/CW módban csak akkor engedélyezett, ha BFO be van kapcsolva
     */
    void updateStepButtonState();
};