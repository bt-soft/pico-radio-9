#pragma once

#include <TFT_eSPI.h>
#include <vector>

#include "UIComponent.h"

/**
 * @brief TextBox komponens dekódolt szöveg megjelenítéséhez (CW/RTTY)
 *
 * Automatikus sortörés, felfelé scrollozás, border kerettel.
 */
class UICompTextBox : public UIComponent {
  public:
    /**
     * @brief Konstruktor
     * @param x X pozíció
     * @param y Y pozíció
     * @param w Szélesség
     * @param h Magasság
     * @param tft TFT_eSPI instance
     */
    UICompTextBox(int x, int y, int w, int h, TFT_eSPI &tft);

    /**
     * @brief Destruktor
     */
    ~UICompTextBox();

    // UIComponent interface
    void draw() override;
    bool handleTouch(const TouchEvent &touch) override;

  protected:
    /**
     * @brief Dialog eltűnésekor meghívódik (ősosztályból örökölt)
     */
    void onDialogDismissed() override;

  public:
    /**
     * @brief Karakter hozzáadása a textboxhoz
     * @param c Karakter
     */
    void addCharacter(char c);

    /**
     * @brief Textbox törlése
     */
    void clear();

    /**
     * @brief Border rajzolása
     */
    void drawBorder();

    /**
     * @brief Teljes újrarajzolás (border + szöveg)
     */
    void redrawAll();

    /**
     * @brief Getter a bounds-hoz (örökölt UIComponent-ből)
     */
    inline int getX() const { return bounds.x; }
    inline int getY() const { return bounds.y; }
    inline int getWidth() const { return bounds.width; }
    inline int getHeight() const { return bounds.height; }

  private:
    TFT_eSPI &tft_;

    // Border paraméterek
    static constexpr int BORDER_WIDTH = 2;
    static constexpr uint16_t BORDER_COLOR = TFT_CYAN;
    static constexpr uint16_t BACKGROUND_COLOR = TFT_BLACK;
    static constexpr uint16_t TEXT_COLOR = TFT_WHITE;

    // Font és sor paraméterek
    static constexpr int FONT = 1;         // Font 1 (8px magasság, kisebb)
    static constexpr int CHAR_WIDTH = 6;   // Karakter szélesség font 1-nél
    static constexpr int LINE_HEIGHT = 10; // Sor magasság (8px + 2px spacing)
    static constexpr int TEXT_PADDING = 4; // Belső margó a bordertől

    // Szöveg buffer
    std::vector<String> lines_; // Sorok listája
    String currentLine_;        // Aktuális sor buffer

    int maxLines_;        // Maximum sorok száma a boxban
    int maxCharsPerLine_; // Maximum karakterek száma soronként

    bool needsRedraw_; // Teljes újrarajzolás szükséges
    bool borderDrawn_; // Border már rajzolva van

    // Kurzor
    bool cursorVisible_;                        // Kurzor láthatóság (villogás)
    unsigned long lastCursorBlink_;             // Utolsó villogás időpontja
    static constexpr int CURSOR_BLINK_MS = 500; // Villogási periódus (ms)
    static constexpr uint16_t CURSOR_COLOR = TFT_YELLOW;
    static constexpr int CURSOR_WIDTH = 5;
    static constexpr int CURSOR_HEIGHT = 8;

    // Touch kezelés
    uint32_t lastTouchTime_;

    /**
     * @brief Számítja a maximum sorok és karakterek számát
     */
    void calculateDimensions();

    /**
     * @brief Sor hozzáadása a bufferhez (scroll kezeléssel)
     */
    void addLine(const String &line);

    /**
     * @brief Szöveg terület rajzolása
     */
    void drawText();

    /**
     * @brief Új sor rajzolása alulra
     */
    void drawNewLine();

    /**
     * @brief Scroll felfelé (összes sor újrarajzolása)
     */
    void scrollUp();

    /**
     * @brief Kurzor rajzolása vagy törlése
     */
    void drawCursor(bool show);
};
