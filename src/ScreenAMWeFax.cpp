/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: ScreenAMWeFax.cpp                                                                                             *
 * Created Date: 2025.11.15.                                                                                           *
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
 * Last Modified: 2025.11.22, Saturday  07:42:14                                                                       *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

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

// #define WEFAX_SCALE 300.0f / WEFAX_MAX_OUTPUT_WIDTH                                  // Kép skálázási tényező
// #define WEFAX_SCALED_WIDTH ((uint16_t)(WEFAX_MAX_OUTPUT_WIDTH * WEFAX_SCALE + 0.5f)) // Új szélesség skálázva
//  #define WEFAX_SCALED_HEIGHT ((uint16_t)(WEFAX_LINE_HEIGHT * WEFAX_SCALE + 0.5f))     // Új magasság skálázva
#define WEFAX_SCALED_WIDTH 400  // Fix szélesség a skálázott képnek
#define WEFAX_SCALED_HEIGHT 190 // Fix magasság a skálázott képnek

#define WEFAX_PICTURE_START_X 2  // Kép kezdő X pozíciója
#define WEFAX_PICTURE_START_Y 90 // Kép kezdő Y pozíciója

#define MODE_TXT_HEIGHT 15
#define MODE_TXT_X WEFAX_PICTURE_START_X
#define MODE_TXT_Y WEFAX_PICTURE_START_Y - MODE_TXT_HEIGHT

/**
 * @brief ScreenAMWeFax konstruktor
 */
ScreenAMWeFax::ScreenAMWeFax()
    : ScreenAMRadioBase(SCREEN_NAME_DECODER_WEFAX), UICommonVerticalButtons::Mixin<ScreenAMWeFax>(), //
      cachedMode(-1), cachedDisplayWidth(-1), displayWidth(0), sourceWidth(0), sourceHeight(0), scale(1.0f), targetHeight(0), lastDrawnTargetLine(-1),
      accumulatedTargetLine(0.0f) {
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

    // Állapotsor komponens létrehozása (felső sáv)
    ScreenRadioBase::createStatusLine();

    // Frekvencia kijelző pozicionálás
    uint16_t FreqDisplayY = 20;
    Rect sevenSegmentFreqBounds(0, FreqDisplayY, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_WIDTH, UICompSevenSegmentFreq::SEVEN_SEGMENT_FREQ_HEIGHT + 10);
    ScreenRadioBase::createSevenSegmentFreq(sevenSegmentFreqBounds); // SevenSegmentFreq komponens létrehozása
    this->updateSevenSegmentFreqWidth();                             // Dinamikus szélesség beállítása band típus alapján

    // Függőleges gombok létrehozása
    Mixin::createCommonVerticalButtons(); // UICommonVerticalButtons-ban definiált UIButtonsGroupManager alapú függőleges gombsor egyedi Memo kezelővel

    // Alsó vízszintes gombsor - CSAK az AM specifikus 4 gomb (BFO, AFBW, ANTCAP, DEMOD)
    // addDefaultButtons = false -> NEM rakja be a HAM, Band, Scan gombokat
    ScreenRadioBase::createCommonHorizontalButtons(false);

    // Wefax kép helyének kirajzolása

    // Reset gomb elhelyezése: a gomb jobb oldala egy vonalban legyen a kép jobb oldalával,
    // a gomb teteje pedig 10px-el legyen a kép teteje felett.
    const int resetBtnRightX = WEFAX_PICTURE_START_X + WEFAX_SCALED_WIDTH;              // kép jobb oldala
    const int resetBtnX = resetBtnRightX - UIButton::DEFAULT_BUTTON_WIDTH;              // gomb bal oldala
    const int resetBtnY = WEFAX_PICTURE_START_Y - 15 - UIButton::DEFAULT_BUTTON_HEIGHT; // 10px a kép felett

    if (!resetButton) {
        resetButton = std::make_shared<UIButton>( //
            201,                                  // egyedi ID
            Rect(resetBtnX, resetBtnY),           // gomb helye és mérete
            "Reset",                              // gomb felirata
            UIButton::ButtonType::Pushable,       // gomb típusa
            [this](const UIButton::ButtonEvent &event) {
                if (event.state == UIButton::EventButtonState::Clicked) {
                    this->clearPictureArea();
                    ::audioController.resetDecoder();
                }
            });
        UIContainerComponent::addChild(resetButton);
    }

    // Wefax kép helyének kirajzolása
    this->clearPictureArea();
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
    Mixin::updateAllVerticalButtonStates(); // Univerzális funkcionális gombok (mixin method)

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

void ScreenAMWeFax::drawContent() {

    // Képterület törlése
    this->clearPictureArea();

    // A 'Mode:' felirat megjelenítése
    tft.setTextColor(TFT_SKYBLUE, TFT_BLACK);
    tft.setTextDatum(BC_DATUM);
    tft.setTextFont(0);
    tft.setTextSize(1);
    tft.setCursor(WEFAX_PICTURE_START_X, WEFAX_PICTURE_START_Y - MODE_TXT_HEIGHT);
    tft.print("HF WeFax Mode:");
}

/**
 * @brief Képterület törlése
 */
void ScreenAMWeFax::clearPictureArea() {
    // WEFAX kép helye
    tft.fillRect(WEFAX_PICTURE_START_X, WEFAX_PICTURE_START_Y, WEFAX_SCALED_WIDTH, WEFAX_SCALED_HEIGHT, TFT_BLACK);

    // Fehér keret rajzolása a kép körül (1px-el kívül)
    tft.drawRect(WEFAX_PICTURE_START_X - 1, WEFAX_PICTURE_START_Y - 1, WEFAX_SCALED_WIDTH + 2, WEFAX_SCALED_HEIGHT + 2, TFT_WHITE);

    this->drawWeFaxMode(nullptr); // Mód név törlése
}

/**
 * @brief Wefax mód név kirajzolása
 * @param modeName A mód neve (pl. "IOC576", "IOC288"), vagy nullptr a törléshez
 */
void ScreenAMWeFax::drawWeFaxMode(const char *modeName) {

    constexpr int MODE_VALUE_X = WEFAX_PICTURE_START_X + 100;

    // Módnév törlése (hátha volt ott valami)
    tft.fillRect(MODE_VALUE_X, WEFAX_PICTURE_START_Y - MODE_TXT_HEIGHT - 4, 100, MODE_TXT_HEIGHT, TFT_BLACK);

    // Módnév kiírása
    if (modeName != nullptr && !STREQ(modeName, DECODER_MODE_UNKNOWN)) {
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextDatum(BC_DATUM);
        tft.setTextFont(0);
        tft.setTextSize(1);
        tft.setCursor(MODE_VALUE_X, MODE_TXT_Y);
        tft.print(modeName);
    }
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
void ScreenAMWeFax::checkDecodedData() {

    bool modeChanged = false;
    if (decodedData.modeChanged) {
        decodedData.modeChanged = false;
        modeChanged = true;
        // Mód név lekérése és kiírása
        const char *modeName = (decodedData.currentMode == 0) ? "IOC576" : "IOC288";
        WEFAX_DEBUG("core-0: WEFAX mód változás: %s\n", modeName);

        // Töröljük a képterületet módváltáskor
        clearPictureArea();
    }

    // Új kép kezdés ellenőrzése
    static bool hasWrapped = false; // Jelzi hogy már volt wraparound (fekete vonal csak ekkor kell)
    if (decodedData.newImageStarted) {
        decodedData.newImageStarted = false;
        WEFAX_DEBUG("core-0: Új WEFAX kép kezdődött - képterület törlése\n");
        clearPictureArea();
        // A Scroll állapot nullázása új kép esetén
        accumulatedTargetLine = 0.0f;
        lastDrawnTargetLine = -1;
        hasWrapped = false; // Új kép, még nem volt wraparound
    }

    // A nem változó értékek cache-elése, kivéve ha a mód vagy a kijelző mérete változik
    uint8_t currentMode = decodedData.currentMode;
    uint16_t currentDisplayWidth = WEFAX_SCALED_WIDTH;
    if (currentMode != cachedMode || currentDisplayWidth != cachedDisplayWidth) {
        displayWidth = currentDisplayWidth;
        sourceWidth = (currentMode == 0) ? WEFAX_IOC576_WIDTH : WEFAX_IOC288_WIDTH;
#define WEFAX_IMAGE_HEIGHT 1024 // Maximum WEFAX magasság a kép átméretezéséhez
        sourceHeight = WEFAX_IMAGE_HEIGHT;
        scale = (float)displayWidth / (float)sourceWidth;
        targetHeight = (uint16_t)(sourceHeight * scale);
        cachedMode = currentMode;
        cachedDisplayWidth = currentDisplayWidth;
    }

    // WEFAX képsorok kiolvasása és scrollozás
    DecodedLine dline;
    if (decodedData.lineBuffer.get(dline)) {
        // Minden bejövő forrás sorhoz növeljük az akkumulátort
        accumulatedTargetLine += scale;

        // Amíg van kirajzolható cél sor, rajzoljuk ki
        while (accumulatedTargetLine >= 1.0f) {
            lastDrawnTargetLine++;
            accumulatedTargetLine -= 1.0f;

            // Displaybuffer méret ellenőrzés
            if (displayWidth > WEFAX_MAX_DISPLAY_WIDTH) {
                break;
            }

            // Wraparound: Ha eléri a kijelző alját, ugorjon vissza a tetejére
            if (lastDrawnTargetLine >= WEFAX_SCALED_HEIGHT) {
                lastDrawnTargetLine = 0;
                hasWrapped = true; // Jelezzük hogy volt wraparound
                WEFAX_DEBUG("core-0: WEFAX wraparound - vissza a kép tetejére\n");
                // Fekete vonal a következő sorba
                tft.drawFastHLine(WEFAX_PICTURE_START_X, WEFAX_PICTURE_START_Y, displayWidth, TFT_ORANGE);
            }

            // Minden cél pixelhez kiszámoljuk, hogy melyik forrás pixelek tartoznak hozzá
            float invScale = (float)sourceWidth / (float)displayWidth;
            for (uint16_t x = 0; x < displayWidth; x++) {
                float srcPos = x * invScale;
                uint16_t srcStart = (uint16_t)srcPos;
                uint16_t srcEnd = (uint16_t)(srcPos + invScale);
                if (srcEnd > sourceWidth) {
                    srcEnd = sourceWidth;
                }

                uint16_t sum = 0;
                uint16_t count = 0;
                for (uint16_t sx = srcStart; sx < srcEnd && sx < sourceWidth; sx++) {
                    sum += dline.wefaxPixels[sx]; // 8-bit grayscale
                    count++;
                }

                uint8_t grayscale = (count > 0) ? (sum / count) : 255;

                // Grayscale → RGB565 konverzió
                uint16_t gray5 = grayscale >> 3;
                uint16_t gray6 = grayscale >> 2;
                displayBuffer[x] = (gray5 << 11) | (gray6 << 5) | gray5;
            }

            // Kirajzolás a cél sorpozícióra (arányos magasság)
            tft.pushImage(WEFAX_PICTURE_START_X, WEFAX_PICTURE_START_Y + lastDrawnTargetLine, displayWidth, 1, displayBuffer);

            // Színes kurzor vonal CSAK wraparound után (jelzi a régi kép felülírását)
            if (hasWrapped) {
                uint16_t nextLine = (lastDrawnTargetLine + 1) % (WEFAX_SCALED_HEIGHT);
                tft.drawFastHLine(WEFAX_PICTURE_START_X, WEFAX_PICTURE_START_Y + nextLine, displayWidth, TFT_ORANGE);
            }
        }
    }
}
