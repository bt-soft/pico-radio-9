#pragma once

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
    static inline bool was_audio_active = false;

  public:
    /**
     * @brief EEPROM biztonságos írás indítása
     */
    static void begin() {
        was_audio_active = isAudioSamplingRunningC1();
        if (was_audio_active) {
            stopAudioSamplingC1();
        }
    }

    /**
     * @brief EEPROM biztonságos írás befejezése
     */
    static void end() {
        if (was_audio_active) {
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
