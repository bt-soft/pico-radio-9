#pragma once

#include <Arduino.h>

#include "pins.h"

//---- Program Information ------------------------------------------

#define PROGRAM_NAME "Pico Radio v9"
#define PROGRAM_VERSION "0.0.9"
#define PROGRAM_AUTHOR "bt-soft (2025)"

//---- Program Information ------------------------------------------

//--- ScreenNames ----
#define SCREEN_NAME_FM "ScreenFM"
#define SCREEN_NAME_AM "ScreenAM"
#define SCREEN_NAME_SCREENSAVER "SaverScreen"
#define SCREEN_NAME_SETUP "ScreenSetup"
#define SCREEN_NAME_SETUP_SYSTEM "ScreenSetupSystem"
#define SCREEN_NAME_SETUP_SI4735 "ScreenSetupSi4735"
#define SCREEN_NAME_SETUP_AUDIO_PROC "ScreenSetupAudioProc"
#define SCREEN_NAME_CW_RTTY "ScreenCwRtty"

#define SCREEN_NAME_MEMORY "ScreenMemory"
#define SCREEN_NAME_SCAN "ScreenScan"

#define SCREEN_NAME_TEST "TestScreen"
#define SCREEN_NAME_EMPTY "EmptyScreen"

//--- Debug ---
#define __DEBUG // Debug mód vezérlése

// Soros portra várakozás a debug üzenetek előtt
// #define DEBUG_WAIT_FOR_SERIAL

// #define SHOW_MEMORY_INFO               // Memória monitor bekapcsolása memory leak nyomon követésére
// #define MEMORY_INFO_INTERVAL 20 * 1000 // 20mp

// Debug keretek rajzolása a UI komponensek köré
// #define DRAW_DEBUG_GUI_FRAMES

//--- Debug ---
#ifdef __DEBUG
#define DEBUG(fmt, ...) Serial.printf(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

//--- Radio EEPROM Size ---
#define RADIO_EEPROM_SIZE_IN_KB 3 // EEPROM méret KB-ban (512-4096 között módosítható)

// Feszültségmérés
#define VBUS_DIVIDER_R1 10.0f // Ellenállás VBUS és A0 között (kOhm)
#define VBUS_DIVIDER_R2 15.0f // Ellenállás A0 és GND között (kOhm)

// Rotary Encoder
#define __USE_ROTARY_ENCODER_IN_HW_TIMER

// TFT háttérvilágítás max érték
#define TFT_BACKGROUND_LED_MAX_BRIGHTNESS 255
#define TFT_BACKGROUND_LED_MIN_BRIGHTNESS 5

//--- Battery ---
#define MIN_BATTERY_VOLTAGE 270 // Minimum akkumulátor feszültség (V*100)
#define MAX_BATTERY_VOLTAGE 405 // Maximum akkumulátor feszültség (V*100)

//--- ScreenSaver
#define SCREEN_SAVER_TIMEOUT_MIN 1
#define SCREEN_SAVER_TIMEOUT_MAX 60
#define SCREEN_SAVER_TIMEOUT 10 // 1 perc a képernyővédő időzítése - tesztelés

//--- Array Utils ---
#define ARRAY_ITEM_COUNT(array) (sizeof(array) / sizeof(array[0]))

//--- Band Table ---
#define BANDTABLE_SIZE 30 // A band tábla mérete (bandTable[] tömb)

//--- C String compare -----
#define STREQ(a, b) (strcmp((a), (b)) == 0)

// Egy másodperc mikroszekundumban
#define ONE_SECOND_IN_MICROS 1000000.0f
