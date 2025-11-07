#pragma once

// Core1 audio mintavételezés indítása, leállítása és állapot lekérdezése
void startAudioSamplingC1();
void stopAudioSamplingC1();
bool isAudioSamplingRunningC1();

/**
 * @brief EEPROM biztonságos írás Core1 audio szüneteltetésével
 *
 * Ez a wrapper biztosítja, hogy EEPROM írás közben a Core1 audio
 * feldolgozás szünetelve legyen.
 */
class EepromSafeWrite {
  private:
    static inline bool wasAudioActive = false;

  public:
    /**
     * @brief EEPROM biztonságos írás indítása
     * Ha fut az audio feldolgozás, leállítja azt.
     */
    static void begin() {
        wasAudioActive = isAudioSamplingRunningC1();
        if (wasAudioActive) {
            stopAudioSamplingC1();
        }
    }

    /**
     * @brief EEPROM biztonságos írás befejezése
     * Ha futott az audio feldolgozás, újraindítja azt.
     */
    static void end() {
        if (wasAudioActive) {
            startAudioSamplingC1();
        }
    }

    /**
     * @brief RAII-stílusú EEPROM védelem automatikus destruktorral
     */
    class Guard {
      public:
        Guard() { begin(); }
        ~Guard() { end(); }
    };
};
