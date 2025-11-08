#pragma once

/**
 * @brief Képernyőkezelő interfész
 */
class IScreenManager {

  public:
    /**
     * @brief Képernyő váltás név alapján
     * @param screenName A cél képernyő neve
     */
    virtual bool switchToScreen(const char *screenName, void *params = nullptr) = 0;

    /**
     * @brief Vissza az előző képernyőre
     */
    virtual bool goBack() = 0;

    /**
     * @brief Van aktív dialógus az aktuális képernyőn?
     */
    virtual bool isCurrentScreenDialogActive() = 0;
};
