/**
 * @file main.cpp
 * @brief Pico Radio Core-0 fő programfájlja
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */
#include <Arduino.h>

#include "PicoMemoryInfo.h"
#include "PicoSensorUtils.h"
#include "SplashScreen.h"

//------------------ TFT
#include <TFT_eSPI.h>
TFT_eSPI tft;
uint16_t SCREEN_W;
uint16_t SCREEN_H;

//------------------- Rotary Encoder
#include <RPi_Pico_TimerInterrupt.h>
RPI_PICO_Timer rotaryTimer(0); // 0-ás timer használata
#include "RotaryEncoder.h"
RotaryEncoder rotaryEncoder = RotaryEncoder(PIN_ENCODER_CLK, PIN_ENCODER_DT, PIN_ENCODER_SW, ROTARY_ENCODER_STEPS_PER_NOTCH);
#define ROTARY_ENCODER_SERVICE_INTERVAL_IN_MSEC 1 // 1msec

//-------------------- Config
#include "BandStore.h"
#include "Config.h"
#include "EepromLayout.h"
#include "StationStore.h"
#include "StoreEepromBase.h"
extern Config config;
extern FmStationStore fmStationStore;
extern AmStationStore amStationStore;
extern BandStore bandStore;

//------------------ SI4735
#include "Band.h" // Band típusok konstansaihoz
#include "Si4735Manager.h"
Si4735Manager *pSi4735Manager = nullptr; // Si4735Manager: NEM lehet (hardware inicializálás miatt) statikus, mert HW inicializálások is vannak benne

//-------------------- Screens
// Globális képernyőkezelő pointer - inicializálás a setup()-ban történik
#include "ScreenManager.h"
ScreenManager *screenManager = nullptr;
IScreenManager **iScreenManager = (IScreenManager **)&screenManager; // A UIComponent használja

//-------------------- AudioController
#include "AudioController.h"
AudioController audioController;

/**
 * @brief  Hardware timer interrupt service routine a rotaryhoz
 */
bool rotaryTimerHardwareInterruptHandler(struct repeating_timer *t) {
    rotaryEncoder.service();
    return true;
}

/**
 * @brief Rotary encoder események feldolgozása és továbbítása a ScreenManager-nek
 */
void processRotaryEncoderEvent() {

    // RotaryEncoder állapotának lekérdezése
    RotaryEncoder::EncoderState encoderState = rotaryEncoder.read();

    // Ha nem tekergetik vagy nincs gombnyomás, akkor nem csinálunk semmit
    if (encoderState.direction == RotaryEncoder::Direction::None && encoderState.buttonState == RotaryEncoder::ButtonState::Open) {
        return;
    }

    // Rotary tekerés irány lekérdezése
    RotaryEvent::Direction direction = RotaryEvent::Direction::None;
    if (encoderState.direction == RotaryEncoder::Direction::Up) {
        direction = RotaryEvent::Direction::Up;
    } else if (encoderState.direction == RotaryEncoder::Direction::Down) {
        direction = RotaryEvent::Direction::Down;
    }

    // Rotary gomb állapot lekérdezése
    RotaryEvent::ButtonState buttonState = RotaryEvent::ButtonState::NotPressed;
    if (encoderState.buttonState == RotaryEncoder::ButtonState::Clicked) {
        buttonState = RotaryEvent::ButtonState::Clicked;
    } else if (encoderState.buttonState == RotaryEncoder::ButtonState::DoubleClicked) {
        buttonState = RotaryEvent::ButtonState::DoubleClicked;
    }

    // Esemény továbbítása a ScreenManager-nek
    RotaryEvent rotaryEvent(direction, buttonState, encoderState.value);
    screenManager->handleRotary(rotaryEvent);
}

/**
 * @brief Touch események feldolgozása és továbbítása a ScreenManager-nek
 */
void processTouchEvent() {

    static uint16_t lastTouchX = 0;
    static uint16_t lastTouchY = 0;

    uint16_t touchX, touchY;
    bool touchedRaw = tft.getTouch(&touchX, &touchY);
    bool validCoordinates = true;
    if (touchedRaw) {
        if (touchX > SCREEN_W || touchY > SCREEN_H) {
            validCoordinates = false;
        }
    }
    bool touched = touchedRaw && validCoordinates;

    static bool lastTouchState = false;
    // Touch press event (immediate response)
    if (touched && !lastTouchState) {
        TouchEvent touchEvent(touchX, touchY, true);
        screenManager->handleTouch(touchEvent);
        lastTouchX = touchX;
        lastTouchY = touchY;
    } else if (!touched && lastTouchState) { // Touch release event (immediate response)
        TouchEvent touchEvent(lastTouchX, lastTouchY, false);
        screenManager->handleTouch(touchEvent);
    }

    lastTouchState = touched;
}

/**
 * @brief Core0 main belépő függvénye
 */
void setup() {
#ifdef __DEBUG
    Serial.begin(115200);
#endif

    // PICO AD inicializálása
    PicoSensorUtils::init();

    // Beeper
    pinMode(PIN_BEEPER, OUTPUT);
    digitalWrite(PIN_BEEPER, LOW);

    // TFT LED háttérvilágítás kimenet
    pinMode(PIN_TFT_BACKGROUND_LED, OUTPUT);
    Utils::setTftBacklight(TFT_BACKGROUND_LED_MAX_BRIGHTNESS); // TFT inicializálása DC módban
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK); // Fekete háttér a splash screen-hez

    // UI komponensek számára képernyő méretek inicializálása
    SCREEN_W = tft.width();
    SCREEN_H = tft.height();

#ifdef DEBUG_WAIT_FOR_SERIAL
    // Várakozás a soros port megnyitására hibakereséshez
    Utils::debugWaitForSerial(tft);
#endif

    // Csak az általános információkat jelenítjük meg először (SI4735 nélkül)
    // Program cím és build info megjelenítése
    tft.setFreeFont();
    tft.setTextSize(2);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString(PROGRAM_NAME, tft.width() / 2, 20);

    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Version " + String(PROGRAM_VERSION), tft.width() / 2, 50);
    tft.drawString(PROGRAM_AUTHOR, tft.width() / 2, 70);

    // Build info
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Build: " + String(__DATE__) + " " + String(__TIME__), tft.width() / 2, 100);

    // Inicializálási progress
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    tft.drawString("Initializing...", tft.width() / 2, 140);

    // EEPROM inicializálása (A fordítónak muszáj megadni egy típust, itt most egy Config_t-t használunk, de igaziból mindegy)
    tft.drawString("Loading EEPROM...", tft.width() / 2, 160);
    StoreEepromBase<Config_t>::init(); // Meghívjuk a statikus init metódust

    // Ha a bekapcsolás alatt nyomva tartjuk a rotary gombját, akkor töröljük a konfigot
    if (digitalRead(PIN_ENCODER_SW) == LOW) {
        DEBUG("Encoder button pressed during startup, restoring defaults...\n");
        Utils::beepTick();
        delay(1500);
        if (digitalRead(PIN_ENCODER_SW) == LOW) { // Ha még mindig nyomják

            DEBUG("Restoring default settings...\n");
            Utils::beepTick();
            config.loadDefaults();
            fmStationStore.loadDefaults();
            amStationStore.loadDefaults();
            bandStore.loadDefaults();

            DEBUG("Alapértelmezett beállítások mentése...\n");
            Utils::beepTick();
            config.checkSave();
            bandStore.checkSave();
            fmStationStore.checkSave();
            amStationStore.checkSave();

            Utils::beepTick();
            DEBUG("Alapértelmezett beállítások visszaállítva!\n");
        }
    } else {
        // konfig betöltése
        tft.drawString("Configuration loading...", tft.width() / 2, 180);
        config.load();
    }

    // Rotary Encoder beállítása
    rotaryEncoder.setDoubleClickEnabled(true);                                   // Dupla kattintás engedélyezése
    rotaryEncoder.setAccelerationEnabled(config.data.rotaryAccelerationEnabled); // Gyorsítás engedélyezése a rotary enkóderhez
    // Pico HW Timer1 beállítása a rotaryhoz
    rotaryTimer.attachInterruptInterval(ROTARY_ENCODER_SERVICE_INTERVAL_IN_MSEC * 1000, rotaryTimerHardwareInterruptHandler);

    // Kell kalibrálni a TFT Touch-ot?
    if (Utils::isZeroArray(config.data.tftCalibrateData)) {
        Utils::beepError();
        Utils::tftTouchCalibrate(tft, config.data.tftCalibrateData);
        config.checkSave(); // Kalibrációs adatok mentése
    }

    // Beállítjuk a TFT touch screen-t
    tft.setTouch(config.data.tftCalibrateData);

    // Állomáslisták és band adatok betöltése az EEPROM-ból (a config után!)
    tft.drawString("Loading stations & bands...", tft.width() / 2, 200);
    bandStore.load(); // Band adatok betöltése
    fmStationStore.load();
    amStationStore.load();

    // ----------------------- Splash screen megjelenítése az inicializálás közben --------------------
#define SPLASH_SCREEN_PROGRESS_BAR_STEPS 7
    uint8_t splashProgressCnt = 1;
    // Most átváltunk a teljes splash screen-re az SI4735 infókkal
    SplashScreen *splash = new SplashScreen(tft);
    // Splash screen megjelenítése progress bar-ral
    splash->show(true, SPLASH_SCREEN_PROGRESS_BAR_STEPS); // SI4735 init és infók megjelenítése

    // --- Lépés 1: I2C inicializálás
    splash->updateProgress(splashProgressCnt++, SPLASH_SCREEN_PROGRESS_BAR_STEPS, "Initializing SI4735 I2C...");
    // FIGYELEM!!! Az si473x (Nem a default I2C lábakon [4,5] van, hanem az PIN_SI4735_I2C_SDA és az PIN_SI4735_I2C_SCL lábakon !!!)
    Wire.setSDA(PIN_SI4735_I2C_SDA); // I2C for SI4735 SDA
    Wire.setSCL(PIN_SI4735_I2C_SCL); // I2C for SI4735 SCL
    Wire.begin();
    delay(300);

    // --- Lépés 2: SI4735Manager inicializálása itt
    splash->updateProgress(splashProgressCnt++, SPLASH_SCREEN_PROGRESS_BAR_STEPS, "Initializing SI4735Manager...");
    if (pSi4735Manager == nullptr) {
        pSi4735Manager = new Si4735Manager();
        // BandStore beállítása a Si4735Manager-ben
        pSi4735Manager->setBandStore(&bandStore);
    }
    // KRITIKUS: Band tábla dinamikus adatainak EGYSZERI inicializálása RÖGTÖN a Si4735Manager létrehozása után!
    pSi4735Manager->initializeBandTableData(true); // forceReinit = true az első inicializálásnál

    // --- Lépés 3: SI4735 lekérdezése
    splash->updateProgress(splashProgressCnt++, SPLASH_SCREEN_PROGRESS_BAR_STEPS, "Detecting SI4735...");
    int16_t si4735Addr = pSi4735Manager->getDeviceI2CAddress();
    if (si4735Addr == 0) {
        Utils::beepError();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.setTextSize(2);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("SI4735 NOT DETECTED!", tft.width() / 2, tft.height() / 2);
        DEBUG("Si4735 not detected");
        while (true) // nem megyünk tovább
            ;
    }

    // --- Lépés 4: SI4735 konfigurálás
    splash->updateProgress(splashProgressCnt++, SPLASH_SCREEN_PROGRESS_BAR_STEPS, "Configuring SI4735...");
    pSi4735Manager->setDeviceI2CAddress(si4735Addr == 0x11 ? 0 : 1); // Sets the I2C Bus Address, erre is szükség van...
    splash->drawSI4735Info(pSi4735Manager->getSi4735());
    delay(300);

    // --- Lépés 5: Frekvencia beállítások
    splash->updateProgress(splashProgressCnt++, SPLASH_SCREEN_PROGRESS_BAR_STEPS, "Setting up radio...");
    pSi4735Manager->init(true);
    pSi4735Manager->getSi4735().setVolume(config.data.currVolume); // Hangerő visszaállítása
    delay(100);

    // --- Lépés 6: AudioController inicializálása
    splash->updateProgress(splashProgressCnt++, SPLASH_SCREEN_PROGRESS_BAR_STEPS, "AudioController initializing...");
    audioController.stopAudioController(); // Alaphelyzetbe állítás
    delay(100);

    // --- Lépés 7: Kezdő képernyőtípus beállítása
    splash->updateProgress(splashProgressCnt++, SPLASH_SCREEN_PROGRESS_BAR_STEPS, "Preparing display...");
    const char *startScreenName = pSi4735Manager->getCurrentBandType() == FM_BAND_TYPE ? SCREEN_NAME_FM : SCREEN_NAME_AM;
    screenManager = new ScreenManager();
    screenManager->switchToScreen(startScreenName); // A kezdő képernyőre váltás
    delay(100);

    // Splash screen eltűntetése
    splash->hide();
    delete splash;

    //-----------------------------------------------------------------------------------------------
    PicoMemoryInfo::MemoryStatus_t memStatus = PicoMemoryInfo::getMemoryStatus();
    DEBUG("core-0: System clock: %u MHz, Heap: used: %u kB, free: %u kB\n", (unsigned)clock_get_hz(clk_sys) / 1000000u, memStatus.usedHeap / 1024u, memStatus.freeHeap / 1024);

    // Csippantunk egyet a végén
    Utils::beepTick();
}

/**
 * @brief Core0 fő ciklus függvénye
 */
void loop() {

    // EEPROM mentés figyelése
#define EEPROM_SAVE_CHECK_INTERVAL 1000 * 60 * 5 // 5 perc
    static uint32_t lastEepromSaveCheck = 0;
    if (Utils::timeHasPassed(lastEepromSaveCheck, EEPROM_SAVE_CHECK_INTERVAL)) {
        config.checkSave();         // Config mentése
        bandStore.checkSave();      // Band adatok mentése
        fmStationStore.checkSave(); // FM állomások mentése
        amStationStore.checkSave(); // AM állomások mentése
        lastEepromSaveCheck = millis();
    }

    // Memória információk megjelenítése ha engedélyezve van
#ifdef SHOW_MEMORY_INFO
    static uint32_t lastDebugMemoryInfo = 0;
    if (Utils::timeHasPassed(lastDebugMemoryInfo, MEMORY_INFO_INTERVAL)) {
        PicoMemoryInfo::debugMemoryInfo();
        lastDebugMemoryInfo = millis();
    }
#endif

    // Touch események feldolgozása
    processTouchEvent();

    // Rotary Encoder események feldolgozása
    processRotaryEncoderEvent();

    // Képernyő loop-ok, képernyő iterációk, screensaver kezelése
    screenManager->loop();

    // SI4735 loop hívása, squelch és hardver némítás kezelése
    pSi4735Manager->loop();
}