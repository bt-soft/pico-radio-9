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

//------------------- Rotary Encoder
#include <RPi_Pico_TimerInterrupt.h>
RPI_PICO_Timer rotaryTimer(0); // 0-ás timer használata
#include "RotaryEncoder.h"
RotaryEncoder rotaryEncoder = RotaryEncoder(PIN_ENCODER_CLK, PIN_ENCODER_DT, PIN_ENCODER_SW, ROTARY_ENCODER_STEPS_PER_NOTCH);
#define ROTARY_ENCODER_SERVICE_INTERVAL_IN_MSEC 1 // 1msec

//------------------ TFT
#include <TFT_eSPI.h>
TFT_eSPI tft;
uint16_t SCREEN_W;
uint16_t SCREEN_H;

/**
 * @brief  Hardware timer interrupt service routine a rotaryhoz
 */
bool rotaryTimerHardwareInterruptHandler(struct repeating_timer *t) {
    rotaryEncoder.service();
    return true;
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

    // Kell kalibrálni a TFT Touch-t?
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

    // ----------------------- Splash screen megjelenítése inicializálás közben --------------------
    // Most átváltunk a teljes splash screen-re az SI4735 infókkal
    SplashScreen *splash = new SplashScreen(tft);
    splash->show(true, 8); // SI4735 init és infók megjelenítése

    // Splash screen megjelenítése progress bar-ral    // Lépés 1: I2C inicializálás
    splash->updateProgress(1, 9, "Initializing I2C...");
    //
    // FIGYELEM!!! Az si473x (Nem a default I2C lábakon [4,5] van, hanem az PIN_SI4735_I2C_SDA és az PIN_SI4735_I2C_SCL lábakon !!!)
    //
    Wire.setSDA(PIN_SI4735_I2C_SDA); // I2C for SI4735 SDA
    Wire.setSCL(PIN_SI4735_I2C_SCL); // I2C for SI4735 SCL
    Wire.begin();
    delay(300);

    //-----------------------------------------------------------------------------------------------

    delay(1000);
    PicoMemoryInfo::MemoryStatus_t memStatus = PicoMemoryInfo::getMemoryStatus();
    DEBUG("core-0: System clock: %u Hz, Heap: used: %u bytes, free: %u bytes\n", (unsigned)clock_get_hz(clk_sys), memStatus.usedHeap, memStatus.freeHeap);
}

/**
 * @brief Core0 fő ciklus függvénye
 */
void loop() {}