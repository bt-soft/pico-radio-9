#include "TextBoxComponent.h"
#include "Utils.h"
#include "defines.h"
#include "externs_api.h"

/**
 * @brief konstruktor
 * @param x X pozíció
 * @param y Y pozíció
 * @param w Szélesség
 * @param h Magasság
 * @param tft TFT_eSPI instance
 */
TextBoxComponent::TextBoxComponent(int x, int y, int w, int h, TFT_eSPI *tft)
    : UIComponent(Rect(x, y, w, h)), tft_(tft), needsRedraw_(true), borderDrawn_(false), cursorVisible_(false), lastCursorBlink_(0), lastTouchTime_(0) {

    calculateDimensions();
    lines_.reserve(maxLines_);

    DEBUG("TextBoxComponent created: x=%d, y=%d, w=%d, h=%d, maxLines=%d, maxChars=%d\n", x, y, w, h, maxLines_, maxCharsPerLine_);
}

/**
 * @brief destruktor
 */
TextBoxComponent::~TextBoxComponent() { lines_.clear(); }

/**
 * @brief Számítja a maximum sorok és karakterek számát
 */
void TextBoxComponent::calculateDimensions() {
    // Hasznos terület a border nélkül
    int usableWidth = getWidth() - (2 * BORDER_WIDTH) - (2 * TEXT_PADDING);
    int usableHeight = getHeight() - (2 * BORDER_WIDTH) - (2 * TEXT_PADDING);

    maxCharsPerLine_ = usableWidth / CHAR_WIDTH;
    maxLines_ = usableHeight / LINE_HEIGHT;

    // Legalább 1 sor és 1 karakter
    if (maxLines_ < 1)
        maxLines_ = 1;
    if (maxCharsPerLine_ < 1)
        maxCharsPerLine_ = 1;
}

/**
 * @brief Kurzor rajzolása vagy törlése
 */
void TextBoxComponent::draw() {
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
bool TextBoxComponent::handleTouch(const TouchEvent &touch) {
    if (touch.pressed && isPointInside(touch.x, touch.y)) {

        if (!Utils::timeHasPassed(lastTouchTime_, 500)) {
            return false;
        }
        lastTouchTime_ = millis();

        clear();

        // Csippantunk egyet, ha az engedélyezve van
        if (beeperEnabled) {
            Utils::beepTick();
        }

        return true;
    }
    return false;
}

/**
 * @brief Border rajzolása
 */
void TextBoxComponent::drawBorder() {
    if (!tft_)
        return;

    // Külső keret
    tft_->drawRect(getX(), getY(), getWidth(), getHeight(), BORDER_COLOR);
    borderDrawn_ = true;
}

/**
 * @brief Szöveg törlése
 */
void TextBoxComponent::clear() {
    lines_.clear();
    currentLine_ = "";
    cursorVisible_ = false; // Kurzor elrejtése törléskor
    needsRedraw_ = true;
}

/**
 * @brief Teljes újrarajzolás (border + szöveg)
 */
void TextBoxComponent::redrawAll() {
    if (!tft_)
        return;

    // Háttér törlés
    tft_->fillRect(getX(), getY(), getWidth(), getHeight(), BACKGROUND_COLOR);

    // Border rajzolás
    drawBorder();

    // Szöveg rajzolás
    drawText();

    borderDrawn_ = true;
}

/**
 * @brief Szöveg terület rajzolása
 */
void TextBoxComponent::drawText() {
    if (!tft_)
        return;

    int textX = getX() + BORDER_WIDTH + TEXT_PADDING;
    int textY = getY() + BORDER_WIDTH + TEXT_PADDING;

    tft_->setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
    tft_->setTextSize(1); // Font méret szorzó = 1x (nem 2x!)
    tft_->setTextFont(FONT);

    // Összes sor kirajzolása
    for (int i = 0; i < lines_.size(); i++) {
        int lineY = textY + (i * LINE_HEIGHT);
        tft_->setCursor(textX, lineY);
        tft_->print(lines_[i]);
    }

    // Aktuális sor kirajzolása (ha van)
    if (!currentLine_.isEmpty()) {
        int lineY = textY + (lines_.size() * LINE_HEIGHT);
        tft_->setCursor(textX, lineY);
        tft_->print(currentLine_);
    }
}

/**
 * @brief Új sor rajzolása alulra
 */
void TextBoxComponent::drawNewLine() {
    if (!tft_ || lines_.empty())
        return;

    int textX = getX() + BORDER_WIDTH + TEXT_PADDING;
    int textY = getY() + BORDER_WIDTH + TEXT_PADDING;

    // Utolsó sor pozíciója (lines_.size() - 1, mert a sor már hozzá lett adva)
    int lineIndex = lines_.size() - 1;
    int lineY = textY + (lineIndex * LINE_HEIGHT);

    // Töröljük az aktuális sor teljes területét (a régi karakterek eltűnnek)
    int lineWidth = getWidth() - (2 * BORDER_WIDTH) - (2 * TEXT_PADDING);
    tft_->fillRect(textX, lineY, lineWidth, LINE_HEIGHT, BACKGROUND_COLOR);

    // Rajzoljuk ki az új sor tartalmát a lines_ vektorból
    tft_->setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
    tft_->setTextSize(1); // Font méret szorzó = 1x
    tft_->setTextFont(FONT);
    tft_->setCursor(textX, lineY);
    tft_->print(lines_[lineIndex]);
}

/**
 * @brief Scroll felfelé (összes sor újrarajzolása)
 */
void TextBoxComponent::scrollUp() {
    if (!tft_)
        return;

    // Szöveg terület törlése (border nélkül)
    int textX = getX() + BORDER_WIDTH;
    int textY = getY() + BORDER_WIDTH;
    int textW = getWidth() - (2 * BORDER_WIDTH);
    int textH = getHeight() - (2 * BORDER_WIDTH);

    tft_->fillRect(textX, textY, textW, textH, BACKGROUND_COLOR);

    // Összes sor újrarajzolása
    drawText();

    // Az aktuális sor (currentLine_) területének törlése is
    // Ez azért kell, mert a currentLine_ még nem része a lines_ vektornak,
    // ezért a drawText() nem rajzolja/törli
    int currentLineY = textY + TEXT_PADDING + (lines_.size() * LINE_HEIGHT);
    int lineWidth = textW - (2 * TEXT_PADDING);
    tft_->fillRect(textX + TEXT_PADDING, currentLineY, lineWidth, LINE_HEIGHT, BACKGROUND_COLOR);
}

/**
 * @brief Sor hozzáadása a bufferhez (scroll kezeléssel)
 * @param line A hozzáadandó sor szövege
 */
void TextBoxComponent::addLine(const String &line) {
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
void TextBoxComponent::addCharacter(char c) {
    if (!tft_)
        return;

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

        tft_->setTextColor(TEXT_COLOR, BACKGROUND_COLOR);
        tft_->setTextSize(1); // Font méret szorzó = 1x
        tft_->setTextFont(FONT);
        tft_->setCursor(charX, lineY);
        tft_->print(c);
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
void TextBoxComponent::drawCursor(bool show) {
    if (!tft_)
        return;

    int textX = getX() + BORDER_WIDTH + TEXT_PADDING;
    int textY = getY() + BORDER_WIDTH + TEXT_PADDING;
    int lineY = textY + (lines_.size() * LINE_HEIGHT);
    int cursorX = textX + (currentLine_.length() * CHAR_WIDTH);

    // Kurzor rajzolása vagy törlése
    uint16_t color = show ? CURSOR_COLOR : BACKGROUND_COLOR;
    tft_->fillRect(cursorX, lineY, CURSOR_WIDTH, CURSOR_HEIGHT, color);
}
