/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UICompRDS.cpp                                                                                                 *
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
 * Last Modified: 2025.12.21, Sunday  03:02:30                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "UICompRDS.h"
#include "Si4735Manager.h"

// RDS működés debug engedélyezése de csak DEBUG módban
#define __RDS_DEBUG
#if defined(__DEBUG) && defined(__RDS_DEBUG)
#define RDS_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define RDS_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

// ===================================================================
// Konstruktor és inicializálás
// ===================================================================
/**
 * @brief UICompRDS konstruktor
 */
UICompRDS::UICompRDS(const Rect &bounds)
    : UIComponent(bounds, ColorScheme::defaultScheme()), lastScrollUpdate(0), scrollSprite(nullptr), scrollOffset(0), radioTextPixelWidth(0),
      needsScrolling(false), scrollSpriteCreated(false) {

    // Alapértelmezett színek beállítása
    stationNameColor = TFT_CYAN;   // Állomásnév - cián
    programTypeColor = TFT_ORANGE; // Program típus - narancs
    radioTextColor = TFT_WHITE;    // Radio text - fehér
    dateTimeColor = TFT_YELLOW;    // Dátum/idő - sárga
    backgroundColor = TFT_BLACK;   // Háttér - fekete

    // Alapértelmezett pozíciók beállítása (a jelenlegi elrendezés alapján)
    calculateDefaultLayout();
}

/**
 * @brief Destruktor - erőforrások felszabadítása
 */
UICompRDS::~UICompRDS() { cleanupScrollSprite(); }

// ===================================================================
// Layout és konfigurálás
// ===================================================================

/**
 * @brief Alapértelmezett layout számítása - most abszolút pozíciókkal
 * @details A részkomponensek alapértelmezett pozíciói.
 * Ezek felülírhatók a set...Area() metódusokkal.
 */
void UICompRDS::calculateDefaultLayout() {
    // Alapértelmezett pozíciók - ezek később felülírhatók
    const uint16_t defaultY = 150; // Alapértelmezett Y pozíció
    const uint16_t margin = 10;
    const uint16_t lineHeight = 18;
    const uint16_t dateTimeWidth = 85;
    const uint16_t stationNameWidth = 200;

    // Állomásnév - bal oldal
    stationNameRect = Rect(margin, defaultY, stationNameWidth, lineHeight);

    // Program típus - középen
    programTypeRect = Rect(220, defaultY, 150, lineHeight);

    // Dátum/idő - jobb oldal
    dateTimeRect = Rect(380, defaultY, dateTimeWidth, lineHeight);

    // Radio text - alsó sor
    radioTextRect = Rect(margin, defaultY + 20, 460, lineHeight);
}

/**
 * @brief Állomásnév területének beállítása
 */
void UICompRDS::setStationNameRect(const Rect &rect) { stationNameRect = rect; }

/**
 * @brief Program típus területének beállítása
 */
void UICompRDS::setProgramTypeRect(const Rect &rect) { programTypeRect = rect; }

/**
 * @brief Radio text területének beállítása
 */
void UICompRDS::setRadioTextRect(const Rect &rect) {
    radioTextRect = rect;
    // Ha változott a terület, újra kell inicializálni a scroll sprite-ot
    if (scrollSpriteCreated) {
        cleanupScrollSprite();
        initializeScrollSprite();
    }
}

/**
 * @brief Dátum/idő területének beállítása
 */
void UICompRDS::setDateTimeRect(const Rect &rect) { dateTimeRect = rect; }

/**
 * @brief RDS színek testreszabása
 */
void UICompRDS::setRdsColors(uint16_t stationColor, uint16_t typeColor, uint16_t textColor, uint16_t timeColor, uint16_t bgColor) {
    stationNameColor = stationColor;
    programTypeColor = typeColor;
    radioTextColor = textColor;
    dateTimeColor = timeColor;
    backgroundColor = bgColor;

    // Ha már létezik scroll sprite, frissítjük a színeit
    if (scrollSprite && scrollSpriteCreated) {
        scrollSprite->setTextColor(radioTextColor, backgroundColor);
    }
}

/**
 * @brief Scroll sprite inicializálása
 */
void UICompRDS::initializeScrollSprite() {
    if (scrollSprite || scrollSpriteCreated) {
        cleanupScrollSprite();
    }
    if (radioTextRect.width > 0 && radioTextRect.height > 0) {
        scrollSprite = new TFT_eSprite(&tft);
        if (scrollSprite->createSprite(radioTextRect.width, radioTextRect.height)) {
            scrollSprite->setFreeFont(); // Alapértelmezett font
            scrollSprite->setTextSize(2);
            scrollSprite->setTextColor(radioTextColor, backgroundColor);
            scrollSprite->setTextDatum(TL_DATUM);
            scrollSpriteCreated = true;
        } else {
            delete scrollSprite;
            scrollSprite = nullptr;
            scrollSpriteCreated = false;
        }
    }
}

/**
 * @brief Scroll sprite felszabadítása
 */
void UICompRDS::cleanupScrollSprite() {
    if (scrollSprite) {
        if (scrollSpriteCreated) {
            scrollSprite->deleteSprite();
        }
        delete scrollSprite;
        scrollSprite = nullptr;
        scrollSpriteCreated = false;
    }
}

// ===================================================================
// RDS adatok kezelése
// ===================================================================

/**
 * @brief RDS adatok frissítése a Si4735Manager-től
 * @return true ha az adatok változtak és újrarajzolás szükséges
 */
bool UICompRDS::updateRdsData() {

    // Az Si4735Rds osztály cache funkcionalitását használjuk
    bool dataChanged = ::pSi4735Manager->updateRdsDataWithCache(); // Ha változott a radio text, újraszámítjuk a scroll paramétereket
    if (dataChanged) {
        String newRadioText = ::pSi4735Manager->getCachedRadioText();
        if (!newRadioText.isEmpty()) {
            // Radio text feldolgozása - ha több mint 2 egymás utáni szóköz van, levágás az elsőnél
            // String processedRadioText = normalizeRadioText(newRadioText);

            // Radio text változott - scroll újraszámítás
            tft.setFreeFont();
            tft.setTextSize(2);
            radioTextPixelWidth = tft.textWidth(newRadioText);
            needsScrolling = (radioTextPixelWidth > radioTextRect.width);
            scrollOffset = 0; // Scroll restart
        } else {
            needsScrolling = false;
        }

        RDS_DEBUG("RDS: Radio Text updated: '%s', needsScrolling: %s\n", newRadioText.c_str(), needsScrolling ? "true" : "false");
    }

    return dataChanged;
}

// ===================================================================
// Rajzolási metódusok
// ===================================================================

/**
 * @brief Állomásnév kirajzolása
 */
void UICompRDS::drawStationName() {
    String stationName = ::pSi4735Manager->getCachedStationName();

    // Terület törlése
    tft.fillRect(stationNameRect.x, stationNameRect.y, stationNameRect.width, stationNameRect.height, backgroundColor);

#ifdef DRAW_DEBUG_GUI_FRAMES
    // DEBUG KERET - piros
    tft.drawRect(stationNameRect.x, stationNameRect.y, stationNameRect.width, stationNameRect.height, TFT_RED);
#endif

    if (stationName.isEmpty()) {
        return; // Nincs megjeleníthető adat
    }
    tft.setFreeFont();
    tft.setTextSize(3);
    tft.setTextColor(stationNameColor, backgroundColor);
    tft.setTextDatum(MC_DATUM); // Middle Center - vízszintesen és függőlegesen is középre igazítva

    // Szöveg kirajzolása - terület közepére (vízszintes és függőleges központ)
    int16_t centerX = stationNameRect.x + stationNameRect.width / 2;
    int16_t centerY = stationNameRect.y + stationNameRect.height / 2;
    tft.drawString(stationName, centerX, centerY);
}

/**
 * @brief Program típus kirajzolása
 */
void UICompRDS::drawProgramType() {
    String programType = ::pSi4735Manager->getCachedProgramType();

    // Terület törlése
    tft.fillRect(programTypeRect.x, programTypeRect.y, programTypeRect.width, programTypeRect.height, backgroundColor);

#ifdef DRAW_DEBUG_GUI_FRAMES
    // DEBUG KERET - zöld
    tft.drawRect(programTypeRect.x, programTypeRect.y, programTypeRect.width, programTypeRect.height, TFT_GREEN);
#endif

    if (programType.isEmpty()) {
        return; // Nincs megjeleníthető adat
    }
    tft.setFreeFont();
    tft.setTextSize(2);
    tft.setTextColor(programTypeColor, backgroundColor);
    tft.setTextDatum(MC_DATUM); // Middle Center - vízszintesen és függőlegesen is középre igazítva

    // Szöveg kirajzolása - terület közepére (vízszintes és függőleges központ)
    int16_t centerX = programTypeRect.x + programTypeRect.width / 2;
    int16_t centerY = programTypeRect.y + programTypeRect.height / 2;
    tft.drawString(programType, centerX, centerY);
}

/**
 * @brief Radio text kirajzolása (scroll támogatással)
 */
void UICompRDS::drawRadioText() {
    String radioText = ::pSi4735Manager->getCachedRadioText();

    // Radio text feldolgozása - többszörös szóközök kezelése
    // String processedRadioText = normalizeRadioText(radioText);

    // Terület törlése
    tft.fillRect(radioTextRect.x, radioTextRect.y, radioTextRect.width, radioTextRect.height, backgroundColor);

#ifdef DRAW_DEBUG_GUI_FRAMES
    // DEBUG KERET - sárga
    tft.drawRect(radioTextRect.x, radioTextRect.y, radioTextRect.width, radioTextRect.height, TFT_YELLOW);
#endif
    if (radioText.isEmpty()) {
        return;
    }
    if (!needsScrolling) {
        // Egyszerű megjelenítés, ha elfér
        tft.setFreeFont();
        tft.setTextSize(2);
        tft.setTextColor(radioTextColor, backgroundColor);
        tft.setTextDatum(ML_DATUM); // Middle Left - függőlegesen középre, balra igazítva

        // Szöveg kirajzolása - függőlegesen középre + 1px gap
        int16_t centerY = radioTextRect.y + radioTextRect.height / 2;
        tft.drawString(radioText, radioTextRect.x + 5, centerY); // +5px gap a bal oldaltól
    } else {
        // Scroll esetén sprite használata
        if (!scrollSpriteCreated) {
            initializeScrollSprite();
        }

        if (scrollSprite && scrollSpriteCreated) {
            handleRadioTextScroll();
        }
    }
}

/**
 * @brief Dátum és idő kirajzolása
 */
void UICompRDS::drawDateTime() {
    String dateTime = ::pSi4735Manager->getCachedDateTime();

    // Háttér törlése
    tft.fillRect(dateTimeRect.x, dateTimeRect.y, dateTimeRect.width, dateTimeRect.height, backgroundColor);

#ifdef DRAW_DEBUG_GUI_FRAMES
    // DEBUG KERET - kék
    tft.drawRect(dateTimeRect.x, dateTimeRect.y, dateTimeRect.width, dateTimeRect.height, TFT_BLUE);
#endif
    if (dateTime.isEmpty()) {
        return;
    }
    tft.setFreeFont();
    tft.setTextSize(1);
    tft.setTextColor(dateTimeColor, backgroundColor);
    tft.setTextDatum(ML_DATUM); // Middle Left - függőlegesen középre, balra igazítva

    // Szöveg kirajzolása - függőlegesen középre + 1px gap a bal oldaltól
    int16_t centerY = dateTimeRect.y + dateTimeRect.height / 2;
    tft.drawString(dateTime, dateTimeRect.x + 1, centerY); // +1px gap a bal oldaltól
}

/**
 * @brief Radio text scroll kezelése
 */
void UICompRDS::handleRadioTextScroll() {
    if (!scrollSprite || !scrollSpriteCreated || !needsScrolling) {
        return;
    }

    // Sebesség szabályozása
    if (!Utils::timeHasPassed(lastScrollUpdate, SCROLL_INTERVAL_MS)) {
        return;
    }
    lastScrollUpdate = millis();

    // Sprite törlése
    scrollSprite->fillScreen(backgroundColor); // Aktuális radio text lekérése és feldolgozása
    String radioText = ::pSi4735Manager->getCachedRadioText();
    // String processedRadioText = normalizeRadioText(radioText);

    // Fő szöveg rajzolása (balra mozog)
    scrollSprite->drawString(radioText, -scrollOffset, 0);

    // Ha szükséges, "újra beúszó" szöveg rajzolása
    const int gapPixels = radioTextRect.width; // Szóköz a szöveg vége és újrakezdés között
    int secondTextX = -scrollOffset + radioTextPixelWidth + gapPixels;

    if (secondTextX < radioTextRect.width) {
        scrollSprite->drawString(radioText, secondTextX, 0);
    }

    // Sprite kirakása a képernyőre
    scrollSprite->pushSprite(radioTextRect.x, radioTextRect.y);

    // Scroll pozíció frissítése
    scrollOffset += SCROLL_STEP_PIXELS;

    // Ciklus újraindítása
    if (scrollOffset >= radioTextPixelWidth + gapPixels) {
        scrollOffset = 0;
    }
}

// ===================================================================
// UIComponent interface implementáció
// ===================================================================

/**
 * @brief Komponens teljes újrarajzolása
 * @details Most már nem rajzol közös keretet vagy hátteret,
 * mivel a részkomponensek függetlenül pozícionálhatók
 */
void UICompRDS::draw() {
    // Minden elem újrarajzolása a saját pozíciójukban
    drawStationName();
    drawProgramType();
    drawRadioText();
    drawDateTime();

    // Clear the redraw flag after drawing
    needsRedraw = false;
}

// ===================================================================
// Publikus interface
// ===================================================================

/**
 * @brief RDS adatok frissítése (az FM-screen handleOwnLoop-ból hívjuk)
 */
void UICompRDS::updateRDS() {

    // Adaptív frissítési időköz figyelembevételével frissítjük az RDS adatokat
    bool dataChanged = updateRdsData();

    // Ha a UIComponent szintjén újrarajzolás szükséges, akkor teljes újrarajzolás
    if (isRedrawNeeded()) {
        draw();
        needsRedraw = false; // Fontos: töröljük a flag-et
        return;
    }

    // Egyébként csak akkor rajzoljuk újra, ha változtak az adatok
    if (dataChanged) {
        DEBUG("UICompRDS: RDS data változás történt, frissítjük a megjelenítést.\n");
        // Csak az érintett részek újrarajzolása (nem a keret!)
        drawStationName();
        drawProgramType();
        drawRadioText();
        drawDateTime();
        dataChanged = false;
    }

    // Scroll kezelése még akkor is, ha nincs adatváltozás
    if (needsScrolling) {
        handleRadioTextScroll();
    }
}

/**
 * @brief RDS adatok törlése
 */
void UICompRDS::clearRDS() {
    // Si4735Rds cache törlése
    ::pSi4735Manager->clearRdsCache();

    // UI állapot resetelés
    needsScrolling = false;
    scrollOffset = 0;

    draw(); // Teljes törlés

    cleanupScrollSprite();
}

/**
 * @brief RDS cache törlése frekvencia változáskor
 * @details Azonnal törli az összes RDS adatot és reseteli az időzítőket.
 * Használatos frekvencia váltáskor, amikor az RDS adatok már nem érvényesek.
 */
void UICompRDS::clearRdsOnFrequencyChange() {
    // Si4735Rds cache törlése
    ::pSi4735Manager->clearRdsCache();

    // UI állapot resetelés
    needsScrolling = false;
    scrollOffset = 0;

    // Sprite tisztítás
    cleanupScrollSprite();

    // Képernyő frissítés
    markForRedraw(false);
}

/**
 * @brief Ellenőrzi, hogy van-e érvényes RDS adat
 */
bool UICompRDS::hasValidRDS() const {
    return ::pSi4735Manager->isRdsAvailable() &&                      //
           (                                                          //
               !::pSi4735Manager->getCachedStationName().isEmpty()    //
               || !::pSi4735Manager->getCachedProgramType().isEmpty() //
               || !::pSi4735Manager->getCachedRadioText().isEmpty()   //
               || !::pSi4735Manager->getCachedDateTime().isEmpty()    //
           );
}
