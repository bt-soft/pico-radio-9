/*
 * Project: [pico-radio-9] Raspberry Pi Pico Si4735 Radio                                                              *
 * File: UICompTextBox.cpp                                                                                             *
 * Created Date: 2025.11.09.                                                                                           *
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
 * Last Modified: 2025.11.16, Sunday  09:45:17                                                                         *
 * Modified By: BT-Soft                                                                                                *
 * -----                                                                                                               *
 * HISTORY:                                                                                                            *
 * Date      	By	Comments                                                                                           *
 * ----------	---	-------------------------------------------------------------------------------------------------  *
 */

#include "UICompTextBox.h"
#include "Utils.h"
#include "defines.h"

/**
 * @brief konstruktor
 * @param x X pozíció
 * @param y Y pozíció
 * @param w Szélesség
 * @param h Magasság
 * @param tft TFT_eSPI instance
 */
UICompTextBox::UICompTextBox(uint16_t x, uint16_t y, uint16_t w, uint16_t h, TFT_eSPI &tft)
    : UIComponent(Rect(x, y, w, h)), tft_(tft), needsRedraw_(true), borderDrawn_(false), cursorVisible_(false), lastCursorBlink_(0), lastTouchTime_(0) {

    calculateDimensions();
    lines_.reserve(maxLines_);
}

/**
 * @brief destruktor
 */
UICompTextBox::~UICompTextBox() { lines_.clear(); }

/**
 * @brief Kiszámítja a maximum sorok és karakterek számát
 */
void UICompTextBox::calculateDimensions() {
    // Hasznos terület a border nélkül
    uint16_t usableWidth = getWidth() - (2 * BORDER_WIDTH) - (2 * TEXT_PADDING);
    uint16_t usableHeight = getHeight() - (2 * BORDER_WIDTH) - (2 * TEXT_PADDING);

    maxCharsPerLine_ = usableWidth / CHAR_WIDTH;
    maxLines_ = usableHeight / LINE_HEIGHT;

    // Legalább 1 sor és 1 karakter
    if (maxLines_ < 1) {
        maxLines_ = 1;
    }
    if (maxCharsPerLine_ < 1) {
        maxCharsPerLine_ = 1;
    }
}

/**
 * @brief Dialog eltűnésekor meghívódó metódus
 */
void UICompTextBox::onDialogDismissed() {
    // Dialog épp eltűnt - újra kell rajzolni a bordert
    borderDrawn_ = false;
    needsRedraw_ = true;
    UIComponent::onDialogDismissed(); // Hívjuk meg az ősosztály implementációját (markForRedraw)
}

/**
 * @brief Kurzor rajzolása vagy törlése
 */
void UICompTextBox::draw() {
    // Ha van aktív dialog a képernyőn, ne rajzoljunk semmit
    if (isCurrentScreenDialogActive()) {
        return;
    }

    if (needsRedraw_) {
        redrawAll();
        needsRedraw_ = false;
    }

    // Kurzor villogtatása
    unsigned long now = millis();
    if (now - lastCursorBlink_ >= CURSOR_BLINK_MS) {
        lastCursorBlink_ = now;
        cursorVisible_ = !cursorVisible_;
        drawCursor(cursorVisible_);
    }
}

/**
 * @brief Touch esemény kezelése
 * @param touch A touch esemény adatai
 */
bool UICompTextBox::handleTouch(const TouchEvent &touch) {
    if (touch.pressed && isPointInside(touch.x, touch.y)) {

        if (!Utils::timeHasPassed(lastTouchTime_, 500)) {
            return false;
        }
        lastTouchTime_ = millis();

        clear();

        // Csippantunk egyet, ha az engedélyezve van
        Utils::beepTick();

        return true;
    }
    return false;
}

/**
 * @brief Border rajzolása
 */
void UICompTextBox::drawBorder() {

    // Külső keret
    tft_.drawRect(getX(), getY(), getWidth(), getHeight(), BORDER_COLOR);
    borderDrawn_ = true;
}

/**
 * @brief Szöveg törlése
 */
void UICompTextBox::clear() {
    lines_.clear();
    currentLine_ = "";
    cursorVisible_ = false; // Kurzor elrejtése törléskor
    needsRedraw_ = true;
}

/**
 * @brief Teljes újrarajzolás (border + szöveg)
 */
void UICompTextBox::redrawAll() {
    // Háttér törlés
    tft_.fillRect(getX(), getY(), getWidth(), getHeight(), TFT_BLACK);

    // Border rajzolás
    drawBorder();

    // Szöveg rajzolás
    drawText();

    borderDrawn_ = true;
}

/**
 * @brief Szöveg terület rajzolása
 */
void UICompTextBox::drawText() {

    uint16_t textX = getX() + BORDER_WIDTH + TEXT_PADDING;
    uint16_t textY = getY() + BORDER_WIDTH + TEXT_PADDING;

    tft_.setTextColor(TEXT_COLOR, TFT_BLACK);
    tft_.setTextSize(1); // Font méret szorzó = 1x
    tft_.setTextFont(1); // Font 1

    // Összes sor kirajzolása
    for (uint16_t i = 0; i < lines_.size(); i++) {
        uint16_t lineY = textY + (i * LINE_HEIGHT);
        tft_.setCursor(textX, lineY);
        tft_.print(lines_[i]);
    }

    // Aktuális sor kirajzolása (ha van)
    if (!currentLine_.isEmpty()) {
        uint16_t lineY = textY + (lines_.size() * LINE_HEIGHT);
        tft_.setCursor(textX, lineY);
        tft_.print(currentLine_);
    }
}

/**
 * @brief Új sor rajzolása alulra
 */
void UICompTextBox::drawNewLine() {
    if (lines_.empty()) {
        return;
    }

    uint16_t textX = getX() + BORDER_WIDTH + TEXT_PADDING;
    uint16_t textY = getY() + BORDER_WIDTH + TEXT_PADDING;

    // Utolsó sor pozíciója (lines_.size() - 1, mert a sor már hozzá lett adva)
    uint16_t lineIndex = lines_.size() - 1;
    uint16_t lineY = textY + (lineIndex * LINE_HEIGHT);

    // Töröljük az aktuális sor teljes területét (a régi karakterek eltűnnek)
    uint16_t lineWidth = getWidth() - (2 * BORDER_WIDTH) - (2 * TEXT_PADDING);
    tft_.fillRect(textX, lineY, lineWidth, LINE_HEIGHT, TFT_BLACK);

    // Rajzoljuk ki az új sor tartalmát a lines_ vektorból
    tft_.setTextColor(TEXT_COLOR, TFT_BLACK);
    tft_.setTextSize(1); // Font méret szorzó = 1x
    tft_.setTextFont(1); // Font 1
    tft_.setCursor(textX, lineY);
    tft_.print(lines_[lineIndex]);
}

/**
 * @brief Scroll felfelé (összes sor újrarajzolása)
 */
void UICompTextBox::scrollUp() {

    // Szöveg terület törlése (border nélkül)
    uint16_t textX = getX() + BORDER_WIDTH;
    uint16_t textY = getY() + BORDER_WIDTH;
    uint16_t textW = getWidth() - (2 * BORDER_WIDTH);
    uint16_t textH = getHeight() - (2 * BORDER_WIDTH);

    tft_.fillRect(textX, textY, textW, textH, TFT_BLACK);

    // Összes sor újrarajzolása
    drawText();

    // Az aktuális sor (currentLine_) területének törlése is
    // Ez azért kell, mert a currentLine_ még nem része a lines_ vektornak,
    // ezért a drawText() nem rajzolja/törli
    uint16_t currentLineY = textY + TEXT_PADDING + (lines_.size() * LINE_HEIGHT);
    uint16_t lineWidth = textW - (2 * TEXT_PADDING);
    tft_.fillRect(textX + TEXT_PADDING, currentLineY, lineWidth, LINE_HEIGHT, TFT_BLACK);
}

/**
 * @brief Sor hozzáadása a bufferhez (scroll kezeléssel)
 * @param line A hozzáadandó sor szövege
 */
void UICompTextBox::addLine(const String &line) {
    lines_.push_back(line);

    // Ha több sor van mint a maximum, töröljük az elsőt és scrollozunk
    if (lines_.size() > maxLines_) {
        lines_.erase(lines_.begin());
        scrollUp();
    } else {
        // Nincs scroll, csak rajzoljuk ki az új sort
        drawNewLine();
    }
}

/**
 * @brief Karakter hozzáadása a textboxhoz
 * @param c Karakter
 */
void UICompTextBox::addCharacter(char c) {
    // Kurzor törlése, mielőtt módosítjuk a pozíciót
    drawCursor(false);

    // Speciális karakterek kezelése
    if (c == '\n' || c == '\r') {
        // Új sor
        if (!currentLine_.isEmpty()) {
            addLine(currentLine_);
            currentLine_ = ""; // Töröljük a buffert AZONNAL az addLine után
        }
        return;
    }

    // Nem nyomtatható karakterek kihagyása
    if (c < 32 || c > 126) {
        return;
    }

    // Karakter hozzáadása az aktuális sorhoz
    currentLine_ += c;

    // Ha a sor megtelt, törd le
    if (currentLine_.length() >= maxCharsPerLine_) {
        addLine(currentLine_);
        currentLine_ = ""; // Töröljük a buffert AZONNAL az addLine után
        // addLine() már kezeli a scroll-t és az új sor rajzolását
    } else {
        // Csak az új karakter rajzolása (gyorsabb)
        int textX = getX() + BORDER_WIDTH + TEXT_PADDING;
        int textY = getY() + BORDER_WIDTH + TEXT_PADDING;
        int lineY = textY + (lines_.size() * LINE_HEIGHT);
        int charX = textX + ((currentLine_.length() - 1) * CHAR_WIDTH);

        tft_.setTextColor(TEXT_COLOR, TFT_BLACK);
        tft_.setTextSize(1); // Font méret szorzó = 1x
        tft_.setTextFont(1); // Font 1
        tft_.setCursor(charX, lineY);
        tft_.print(c);
    }

    // Kurzor újrarajzolása az új pozícióban
    cursorVisible_ = true;
    lastCursorBlink_ = millis(); // Reset villogás időzítő
    drawCursor(true);
}

/**
 * @brief Kurzor rajzolása vagy törlése
 * @param show true = rajzolja a kurzort, false = törli
 */
void UICompTextBox::drawCursor(bool show) {
    int textX = getX() + BORDER_WIDTH + TEXT_PADDING;
    int textY = getY() + BORDER_WIDTH + TEXT_PADDING;
    int lineY = textY + (lines_.size() * LINE_HEIGHT);
    int cursorX = textX + (currentLine_.length() * CHAR_WIDTH);

    // Kurzor rajzolása vagy törlése
    uint16_t color = show ? CURSOR_COLOR : TFT_BLACK;
    tft_.fillRect(cursorX, lineY, CURSOR_WIDTH, CURSOR_HEIGHT, color);
}
