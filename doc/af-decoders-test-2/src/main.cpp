#include <Arduino.h>

#include <TFT_eSPI.h>
#include <string>

#include "AudioController.h"
#include "SpectrumVisualizationComponent.h"
#include "TextBoxComponent.h"
#include "Utils.h"
#include "arm_math.h"
#include "decode_sstv.h"
#include "decoder_api.h"
#include "defines.h"
#include "pins.h"

//-------------------------------------------------------------------------------------
// Extern deklarációk a Core-1-en osztott memóriaterületekhez
//-------------------------------------------------------------------------------------
extern SharedData sharedData[2];
extern DecodedData decodedData;
//-------------------------------------------------------------------------------------

// --- Core 0 Implementation ---
DecoderId activeDecoderCore0 = ID_DECODER_NONE;
DecoderId oldActiveDecoderCore0 = ID_DECODER_NONE;
AudioController audioController;

// TFT kijelző objektum
TFT_eSPI tft = TFT_eSPI();
#define TFT_ROTATION 1                    // 0=0°, 1=90°, 2=180°, 3=270°
constexpr uint8_t TFT_BANNER_HEIGHT = 30; // TFT felső sáv magassága pixelben

/// Spektrum vizualizáció komponens - audio spektrum és oszcilloszkóp
std::shared_ptr<SpectrumVisualizationComponent> spectrumComp;

/// TextBox komponens - dekódolt szöveg megjelenítése (CW/RTTY)
std::shared_ptr<TextBoxComponent> textBoxComp;

//--- Externs API ---
bool beeperEnabled = true;     // Beeper engedélyezése alapértelmezetten
float audioFftConfigAm = 0.2f; // -1.0f: Disabled, 0.0f: Auto, >0.0f: Manual Gain Factor
uint8_t audioModeAM = 1;       // Utolsó audio mód AM képernyőn (AudioComponentType)

// CW hang frekvencia Hz-ben
uint16_t cwToneFrequencyHz = 800; // CW hang frekvencia Hz-ben

// RTTY frekvenciák (alapértelmezés: DWD Pinneberg 50Bd/450Hz shift)
uint16_t rttyMarkFrequencyHz = 2125; // RTTY Mark frekvencia Hz-ben
uint16_t rttyShiftHz = 450;          // RTTY Shift Hz-ben
float rttyBaud = 50.0f;              // RTTY Baud rate (pl. 45.45, 50, 75, 100)

/**
 * @brief Soros portról egy teljes sort olvas be.
 * @return A beolvasott sor String formátumban.
 */
static String readLine() {
    String line = "";
    while (true) {
        if (Serial.available()) {
            char c = Serial.read();
            DEBUG("%c", c);
            if (c == '\r')
                continue;
            if (c == '\n')
                break;
            line += c;
        }
        delay(1);
    }
    return line;
}

/**
 * @brief Kiírja a spektrumadatokat.
 * @param data A megosztott adatokat tartalmazó struktúra.
 */
void printSpectrum(const SharedData &data) {
    DEBUG("Spectrum (size: %d, 1/8): ", data.fftSpectrumSize);
    for (int i = 0; i < data.fftSpectrumSize; i += 8) {
        DEBUG("%d ", data.fftSpectrumData[i]);
    }
    DEBUG("\n");
}

/**
 * @brief Kiírja az aktuális módot
 * @param mode Az aktuális mód azonosítója.
 * @param modeInfo Az aktuális mód információja.
 */
void printTftMode(const char *mode, const char *modeInfo) {
    // Kijelző felső sávjának törlése a korábbi mód név eltávolításához
    tft.fillRect(0, 0, tft.width(), TFT_BANNER_HEIGHT, TFT_BLACK);

    // Mód név kiírása (kisebb betűméret, hogy ne legyen átfedés a paraméterekkel)
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(5, 10);
    tft.printf("Mode:");

    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.setCursor(45, 10);
    tft.print(mode);

    // mód információ kiírása (ha van)
    if (modeInfo != nullptr) {
        tft.setCursor(45, 20);
        tft.setTextColor(TFT_CYAN, TFT_BROWN);
        tft.print(modeInfo);
    }
}

/**
 * @brief Ellenőrzi és kiírja a Dominant Frequency adatokat.
 */
void checkDominantFreq() {
    // Dominant Frequency adat kiolvasása és kiírása

    // // Periodikus kijelzés
    static uint32_t lastTick = 0;
    if (Utils::timeHasPassed(lastTick, 5000)) { // 5 másodpercenként frissítünk
        lastTick = millis();
        int8_t activeSharedDataIndex = audioController.getActiveSharedDataIndex();
        if (activeSharedDataIndex != -1) {
            const SharedData &data = sharedData[activeSharedDataIndex];
            DEBUG("Core-0 - DomFreq: %u Hz, Amp: %d\n", data.dominantFrequency, data.dominantAmplitude);
        }
    }
}

/**
 * @brief Ellenőrzi és kezeli az SSTV dekódolt adatokat.
 */
void checkSstvData() {

    // SSTV mód változás ellenőrzése
    if (decodedData.modeChanged) {
        decodedData.modeChanged = false; // Flag törlése

        // SSTV mód név lekérése és kiírása a TFT-re
        const char *modeName = c_sstv_decoder::getSstvModeName((c_sstv_decoder::e_mode)decodedData.currentMode);
        DEBUG("core-0: SSTV mód változás: %s (ID: %d)\n", modeName, decodedData.currentMode);
        printTftMode("SSTV", modeName);
    }

    // Új kép kezdés ellenőrzése
    if (decodedData.newImageStarted) {
        decodedData.newImageStarted = false; // Flag törlése

        DEBUG("core-0: Új SSTV kép kezdődött - képterület törlése\n");

        // Képterület törlése (50,50) pozíciótól 320x256 méretben
        tft.fillRect(50, 50, SSTV_LINE_WIDTH, SSTV_LINE_HEIGHT, TFT_BLACK);
    }

    // SSTV képsorok kiolvasása a közös lineBuffer-ből
    DecodedLine dline;
    if (decodedData.lineBuffer.get(dline)) {
        // Byte-swap a pixel értékeket a TFT_eSPI-hez
        static uint16_t displayBuffer[SSTV_LINE_WIDTH];
        for (int i = 0; i < SSTV_LINE_WIDTH; ++i) {
            uint16_t v = dline.sstvPixels[i]; // RGB565 formátumban
            displayBuffer[i] = (uint16_t)((v >> 8) | (v << 8));
        }
        tft.pushImage(50, dline.lineNum + 50, SSTV_LINE_WIDTH, 1, displayBuffer);
    }
}

/**
 * @brief Ellenőrzi és kezeli a WEFAX dekódolt adatokat.
 * Scroll logika: Az új sorok alulról jönnek be és felfelé tolják a régi képet.
 */
void checkWefaxData() {

    // WEFAX mód változás ellenőrzése
    static uint8_t cachedMode = -1;
    static uint16_t cachedDisplayWidth = -1;
    static uint16_t displayWidth = 0;
    static uint16_t sourceWidth = 0;
    static uint16_t sourceHeight = 0;
    static float scale = 1.0f;
    static uint16_t targetHeight = 0;
    constexpr uint16_t MAX_DISPLAY_WIDTH = 800;
    static uint16_t displayBuffer[MAX_DISPLAY_WIDTH];
    static float accumulatedTargetLine = 0.0f;
    static uint16_t lastDrawnTargetLine = -1;

    bool modeChanged = false;
    if (decodedData.modeChanged) {
        decodedData.modeChanged = false;
        modeChanged = true;
        // Mód név lekérése és kiírása
        const char *modeName = (decodedData.currentMode == 0) ? "IOC576" : "IOC288";
        DEBUG("core-0: WEFAX mód változás: %s\n", modeName);
        printTftMode("WEFAX", modeName);
        tft.fillRect(0, TFT_BANNER_HEIGHT - 2, tft.width(), tft.height() - TFT_BANNER_HEIGHT - 2, TFT_BLACK);
    }

    // Új kép kezdés ellenőrzése
    static bool hasWrapped = false; // Jelzi hogy már volt wraparound (fekete vonal csak ekkor kell)
    if (decodedData.newImageStarted) {
        decodedData.newImageStarted = false;
        DEBUG("core-0: Új WEFAX kép kezdődött - képterület törlése\n");
        tft.fillRect(0, TFT_BANNER_HEIGHT, tft.width(), tft.height() - TFT_BANNER_HEIGHT, TFT_BLACK);
        // A Scroll állapot nullázása új kép esetén
        accumulatedTargetLine = 0.0f;
        lastDrawnTargetLine = -1;
        hasWrapped = false; // Új kép, még nem volt wraparound
    }

    // A nem változó értékek cache-elése, kivéve ha a mód vagy a kijelző mérete változik
    uint8_t currentMode = decodedData.currentMode;
    uint16_t currentDisplayWidth = tft.width();
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
            if (displayWidth > MAX_DISPLAY_WIDTH) {
                break;
            }

            // Wraparound: Ha eléri a kijelző alját, ugorjon vissza a tetejére
            uint16_t maxDisplayHeight = tft.height() - TFT_BANNER_HEIGHT;
            if (lastDrawnTargetLine >= maxDisplayHeight) {
                lastDrawnTargetLine = 0;
                hasWrapped = true; // Jelezzük hogy volt wraparound
                DEBUG("core-0: WEFAX wraparound - vissza a kép tetejére\n");
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
            tft.pushImage(0, lastDrawnTargetLine + TFT_BANNER_HEIGHT, displayWidth, 1, displayBuffer);

            // Színes kurzor vonal CSAK wraparound után (jelzi a régi kép felülírását)
            if (hasWrapped) {
                uint16_t nextLine = (lastDrawnTargetLine + 1) % (tft.height() - TFT_BANNER_HEIGHT);
                tft.drawFastHLine(0, nextLine + TFT_BANNER_HEIGHT, displayWidth, TFT_ORANGE);
            }
        }
    }
}

/**
 * @brief Ellenőrzi és kiírja a dekódolt adatokat a soros portra.
 */
void checkDecodedData() {
    static uint16_t lastPublishedCwWpm = 0;
    static float lastPublishedCwFreq = 0.0f;
    static float lastPublishedRttyMark = 0.0f;
    static float lastPublishedRttySpace = 0.0f;
    static float lastPublishedRttyBaud = 0.0f;
    static float lastPublishedRttyMeasured = 0.0f;

    if (oldActiveDecoderCore0 != activeDecoderCore0) {
        oldActiveDecoderCore0 = activeDecoderCore0;
        DEBUG("core-0: Aktív dekóder változás: %d\n", activeDecoderCore0);
        tft.fillRect(0, 0, tft.width(), 30, TFT_BLACK);

        if (activeDecoderCore0 == ID_DECODER_DOMINANT_FREQ) {
            printTftMode("Dominant Frequency", nullptr);
        } else if (activeDecoderCore0 == ID_DECODER_CW) {
            printTftMode("CW", nullptr);
            lastPublishedCwWpm = 0;
            lastPublishedCwFreq = 0.0f;
        } else if (activeDecoderCore0 == ID_DECODER_RTTY) {
            printTftMode("RTTY", nullptr);
            lastPublishedRttyMark = 0.0f;
            lastPublishedRttySpace = 0.0f;
            lastPublishedRttyBaud = 0.0f;
            lastPublishedRttyMeasured = 0.0f;
        } else if (activeDecoderCore0 == ID_DECODER_SSTV) {
            printTftMode("SSTV", nullptr);
        } else if (activeDecoderCore0 == ID_DECODER_WEFAX) {
            printTftMode("WEFAX", nullptr);
        }
    }

    if (activeDecoderCore0 == ID_DECODER_CW) {
        static unsigned long lastCwDisplayUpdate = 0;
        uint16_t currentWpm = decodedData.cwCurrentWpm;
        float currentFreq = decodedData.cwCurrentFreq;

        // Csak akkor frissítjük a kijelzőt, ha jelentős változás történt ÉS eltelt már legalább 1 másodperc
        bool wpmChanged = (lastPublishedCwWpm == 0 && currentWpm != 0) || (abs((int)currentWpm - (int)lastPublishedCwWpm) >= 3);
        bool freqChanged = (lastPublishedCwFreq == 0.0f && currentFreq > 0.0f) || (abs(currentFreq - lastPublishedCwFreq) >= 50.0f);
        if (Utils::timeHasPassed(lastCwDisplayUpdate, 1000) && (wpmChanged || freqChanged)) {
            lastPublishedCwWpm = currentWpm;
            lastPublishedCwFreq = currentFreq;
            lastCwDisplayUpdate = millis();

            // Frissítjük a kijelző felső sávját
            tft.fillRect(110, 0, tft.width() - 110, TFT_BANNER_HEIGHT, TFT_BLACK);
            tft.setCursor(110, 10);
            tft.setTextSize(1);
            tft.setTextColor(TFT_CYAN, TFT_BLACK);
            if (currentFreq > 0.0f && currentWpm > 0) {
                tft.printf("%u Hz / %.0f Hz / %u WPM", (unsigned)cwToneFrequencyHz, currentFreq, currentWpm);
            } else {
                tft.print("-- Hz / -- Hz / -- WPM");
            }
        }
    } else if (activeDecoderCore0 == ID_DECODER_RTTY) {
        float currentMark = decodedData.rttyMarkFreq;
        float currentSpace = decodedData.rttySpaceFreq;
        float currentBaud = decodedData.rttyBaudRate;
        // Csak akkor frissítjük a kijelzőt, ha jelentős változás történt VAGY a szín változna
        bool markChanged = (lastPublishedRttyMark == 0.0f && currentMark > 0.0f) || (abs(currentMark - lastPublishedRttyMark) >= 5.0f);
        bool spaceChanged = (lastPublishedRttySpace == 0.0f && currentSpace > 0.0f) || (abs(currentSpace - lastPublishedRttySpace) >= 5.0f);
        bool baudChanged = (lastPublishedRttyBaud == 0.0f && currentBaud > 0.0f) || (abs(currentBaud - lastPublishedRttyBaud) >= 0.5f);

        if (markChanged || spaceChanged || baudChanged) {
            lastPublishedRttyMark = currentMark;
            lastPublishedRttySpace = currentSpace;
            lastPublishedRttyBaud = currentBaud;

            // Frissítjük a kijelző felső sávját
            tft.fillRect(110, 0, tft.width() - 110, TFT_BANNER_HEIGHT, TFT_BLACK);
            tft.setCursor(110, 10);
            tft.setTextSize(1);
            tft.setTextColor(TFT_CYAN, TFT_BLACK);
            if (currentMark > 0.0f && currentSpace > 0.0f && currentBaud > 0.0f) {
                tft.printf("M:%.0f S:%.0f Sh:%.0f Bd:%.2f", currentMark, currentSpace, currentMark - currentSpace, currentBaud);
            } else {
                tft.print("M:-- S:-- Sh:-- Bd:--");
            }
        }
    } else {
        lastPublishedCwWpm = 0;
        lastPublishedCwFreq = 0.0f;
        lastPublishedRttyMark = 0.0f;
        lastPublishedRttySpace = 0.0f;
        lastPublishedRttyBaud = 0.0f;
        lastPublishedRttyMeasured = 0.0f;
    }

    // Adatok kezelése dekóder szerint
    if (activeDecoderCore0 == ID_DECODER_DOMINANT_FREQ) {
        checkDominantFreq();
    } else if (activeDecoderCore0 == ID_DECODER_SSTV) {
        checkSstvData();
    } else if (activeDecoderCore0 == ID_DECODER_WEFAX) {
        checkWefaxData();
    } else {
        // CW/RTTY szöveg kiolvasása és megjelenítése a TextBox-ban
        char ch;
        while (decodedData.textBuffer.get(ch)) {
            if (textBoxComp) {
                textBoxComp->addCharacter(ch);
            }
        }
    }
}

/**
 * @brief Létrehozza a spektrum vizualizációs komponenst.
 */
void createSpectrumComponent() {
    if (!spectrumComp) {
        tft.fillScreen(TFT_BLACK);
        spectrumComp = std::make_shared<SpectrumVisualizationComponent>(300, 35, 150, 80, RadioMode::AM);
        spectrumComp->loadModeFromConfig(); // AM/FM mód betöltése config-ból
    }
}

/**
 * @brief Létrehozza a spektrum + textbox komponenseket CW/RTTY módhoz.
 *
 * Elrendezés:
 * - Banner: 0-30px (TFT_BANNER_HEIGHT)
 * - Spectrum: eredeti méret és pozíció (x=300, y=50, w=150, h=80)
 * - TextBox: spektrum alatt, teljes szélesség, maradék magasság
 */
void createDecoderComponents() {
    bool needsCreation = (!spectrumComp || !textBoxComp);

    if (needsCreation) {
        tft.fillScreen(TFT_BLACK);
    }

    // Spectrum komponens (eredeti méret és pozíció)
    if (!spectrumComp) {
        spectrumComp = std::make_shared<SpectrumVisualizationComponent>( //
            300,                                                         // x
            35,                                                          // y
            150,                                                         // szélesség = 150
            80,                                                          // magasság = 80
            RadioMode::AM);
        spectrumComp->loadModeFromConfig();
    }

    // TextBox komponens (spektrum alatt, teljes szélesség)
    int textBoxY = 50 + 80 + 10; // spectrum y + magasság + 10px padding
    int textBoxHeight = tft.height() - textBoxY;

    if (!textBoxComp) {
        textBoxComp = std::make_shared<TextBoxComponent>( //
            0,                                            // x = 0 (bal széltől)
            textBoxY,                                     // y = spectrum alatt + padding
            tft.width(),                                  // szélesség = teljes szélesség
            textBoxHeight,                                // magasság = maradék
            &tft);
        textBoxComp->redrawAll();
    }
}

/**
 * @brief Inicializálja a Core 0-at.
 */
void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(PIN_BEEPER, OUTPUT);
    Serial.begin(115200);

    tft.init();
    tft.setRotation(TFT_ROTATION);
    tft.fillScreen(TFT_BLACK);

    // Beállítjuk a touch scren-t
    uint16_t calibData[5] = {373, 3290, 265, 3500, 7};
    tft.setTouch(calibData);

    // Prompt
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("CW/RTTY/SSTV/WEFAX Decoder Tests");

    delay(1000);
    DEBUG("core-0: System clock: %u Hz\n", (unsigned)clock_get_hz(clk_sys));
    DEBUG("core-0: indulás, Parancsok: domfreq, cw, rtty, sstv, wefax, stop, status\n");
}

/**
 * @brief Core 0 fő ciklusfüggvénye.
 */
void loop() {

    // 1. Dekódolt adatok folyamatos ellenőrzése és kiírása
    checkDecodedData();

    // 2. Parancsok fogadása a soros portról
    if (Serial.available()) {
        String cmd = readLine();
        cmd.trim();
        Serial.println(); // Új sor a jobb olvashatóságért

        if (cmd.length() > 0) {
            if (cmd.equalsIgnoreCase("domfreq")) {
                // A samplingRate-et a Core1 számolja ki az Fs-t a bandwidth alapján.
                DEBUG("core-0: parancs: domfreq\n");
                audioController.stop(); // Régi dekóder leállítása Core1-en
                audioController.setDecoder(ID_DECODER_DOMINANT_FREQ, DOMINANT_FREQ_RAW_SAMPLES_SIZE, DOMINANT_FREQ_AF_BANDWIDTH_HZ);
                activeDecoderCore0 = ID_DECODER_DOMINANT_FREQ;

                // TextBox törlése, ha volt (domfreq nem használja)
                textBoxComp.reset();
                createSpectrumComponent(); // Spektrum komponens létrehozása

            } else if (cmd.equalsIgnoreCase("sstv")) {
                // SSTV: sampleCount és AF sávszélesség megadása
                DEBUG("core-0: parancs: sstv\n");
                audioController.stop(); // Régi dekóder leállítása Core1-en
                audioController.setDecoder(ID_DECODER_SSTV, SSTV_RAW_SAMPLES_SIZE, SSTV_AF_BANDWIDTH_HZ);
                activeDecoderCore0 = ID_DECODER_SSTV;

                // Spektrum és TextBox komponensek törlése
                spectrumComp.reset();
                textBoxComp.reset();

            } else if (cmd.equalsIgnoreCase("wefax")) {
                // Töröljük a képet a képernyőről
                DEBUG("core-0: parancs: wefax\n");
                audioController.stop(); // Régi dekóder leállítása Core1-en
                tft.fillRect(0, TFT_BANNER_HEIGHT, tft.width(), tft.height() - TFT_BANNER_HEIGHT, TFT_BLACK);
                audioController.setDecoder(ID_DECODER_WEFAX, WEFAX_RAW_SAMPLES_SIZE, WEFAX_AF_BANDWIDTH_HZ);
                activeDecoderCore0 = ID_DECODER_WEFAX;

                // Spektrum és TextBox komponensek törlése
                spectrumComp.reset();
                textBoxComp.reset();

            } else if (cmd.startsWith("cw")) {
                // cw [freq]
                float _cwFreq = cwToneFrequencyHz; // alapértelmezett

                // parszoljuk az argumentumot, ha van
                int spacePos = cmd.indexOf(' ');
                if (spacePos > 0) {
                    String arg = cmd.substring(spacePos + 1);
                    arg.trim();
                    if (arg.length() > 0) {
                        _cwFreq = arg.toFloat();
                        cwToneFrequencyHz = (uint16_t)_cwFreq; // Globális változó frissítése
                    }
                }
                DEBUG("core-0: parancs: cw (freq=%u)\n", (uint32_t)_cwFreq);
                // CW: megadjuk a mintaszámot és a kívánt AF sávszélességet
                audioController.stop(); // Régi dekóder leállítása Core1-en
                audioController.setDecoder(ID_DECODER_CW, CW_RAW_SAMPLES_SIZE, CW_AF_BANDWIDTH_HZ, (uint32_t)_cwFreq);
                activeDecoderCore0 = ID_DECODER_CW;

                // Spektrum + TextBox komponensek létrehozása CW módban
                createDecoderComponents();
                // TextBox tartalmának törlése
                if (textBoxComp) {
                    textBoxComp->clear();
                }

                // Hangolási segéd frissítése az új frekvenciával
                if (spectrumComp) {
                    spectrumComp->updateTuningAidParameters();
                }

            } else if (cmd.startsWith("rtty")) {
                // rtty [markHz] [shiftHz] [baud]
                uint32_t _markHz = rttyMarkFrequencyHz;
                uint32_t _shiftHz = rttyShiftHz;
                float _baud = rttyBaud;
                // parszoljuk az argumentumokat
                int firstSpace = cmd.indexOf(' ');
                if (firstSpace > 0) {
                    String args = cmd.substring(firstSpace + 1);
                    args.trim();
                    // tokenizálás
                    int idx = 0;
                    while (args.length() > 0 && idx < 3) {
                        int sp = args.indexOf(' ');
                        String token;
                        if (sp >= 0) {
                            token = args.substring(0, sp);
                            args = args.substring(sp + 1);
                        } else {
                            token = args;
                            args = "";
                        }
                        token.trim();
                        if (token.length() > 0) {
                            if (idx == 0)
                                _markHz = (uint32_t)token.toInt();
                            else if (idx == 1)
                                _shiftHz = (uint32_t)token.toInt();
                            else if (idx == 2)
                                _baud = token.toFloat();
                        }
                        idx++;
                    }
                }

                // Globális változók frissítése
                rttyMarkFrequencyHz = (uint16_t)_markHz;
                rttyShiftHz = (uint16_t)_shiftHz;
                rttyBaud = _baud;

                // Ha nem adtak meg értékeket, maradjon 0 (dekóder defaultokat használhatja)
                DEBUG("core-0: parancs: rtty (mark=%u, shift=%u, baud=%.2f)\n", _markHz, _shiftHz, _baud);
                audioController.stop(); // Régi dekóder leállítása Core1-en
                audioController.setDecoder(ID_DECODER_RTTY, RTTY_RAW_SAMPLES_SIZE, RTTY_AF_BANDWIDTH_HZ, 0, _markHz, _shiftHz, _baud);
                activeDecoderCore0 = ID_DECODER_RTTY;

                // Spektrum + TextBox komponensek létrehozása RTTY módban
                createDecoderComponents();
                // TextBox tartalmának törlése
                if (textBoxComp) {
                    textBoxComp->clear();
                }

                // Hangolási segéd frissítése az új paraméterekkel
                if (spectrumComp) {
                    spectrumComp->updateTuningAidParameters();
                }

            } else if (cmd.equalsIgnoreCase("stop")) {
                DEBUG("core-0: parancs: stop\n");
                audioController.stop();
                activeDecoderCore0 = ID_DECODER_NONE;

                // Spektrum és TextBox komponensek törlése
                spectrumComp.reset();
                textBoxComp.reset();

            } else if (cmd.equalsIgnoreCase("status")) {
                int8_t activeSharedDataIndex = audioController.getActiveSharedDataIndex();
                if (activeSharedDataIndex != -1) {
                    const SharedData &data = sharedData[activeSharedDataIndex];
                    DEBUG("--- Status ---\n");
                    DEBUG("ActiveSharedDataIndex: %d, Decoder: %d, Freq: %u Hz, Amp: %d\n", activeSharedDataIndex, activeDecoderCore0, data.dominantFrequency, data.dominantAmplitude);
                    printSpectrum(data);
                    DEBUG("--------------\n");
                } else {
                    DEBUG("Hiba: Nem sikerült adatot lekérni a Core 1-től.\n");
                }
            } else {
                DEBUG("Ismeretlen parancs. Használat: sstv, cw, rtty, stop, status\n");
            }
        }
    }

    // Komponensek rajzolása
    if (spectrumComp) {
        // Spektrum komponens frissítése és rajzolása
        spectrumComp->draw();
    }

    if (textBoxComp) {
        // TextBox komponens rajzolása
        textBoxComp->draw();
    }

    //------------------- Touch esemény kezelése
    uint16_t touchX, touchY;
    bool touchedRaw = tft.getTouch(&touchX, &touchY);
    bool validCoordinates = true;
    if (touchedRaw) {
        if (touchX > tft.width() || touchY > tft.height()) {
            validCoordinates = false;
        }
    }

    static bool lastTouchState = false;
    static uint16_t lastTouchX = 0, lastTouchY = 0;
    bool touched = touchedRaw && validCoordinates;

    // Touch lenyomás esemény (azonnali válasz)
    if (touched && !lastTouchState) {
        TouchEvent touchEvent(touchX, touchY, true);

        if (spectrumComp) {
            spectrumComp->handleTouch(touchEvent);
        }
        if (textBoxComp) {
            textBoxComp->handleTouch(touchEvent);
        }

        lastTouchX = touchX;
        lastTouchY = touchY;
    }
}
