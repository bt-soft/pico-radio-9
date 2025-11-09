/**
 * @file AudioProcessor_AGC_Pelda.cpp
 * @brief Példakód az AudioProcessorC1 AGC és zajszűrés funkcióinak használatához
 * @author BT-Soft
 */

#include "AudioProcessor-c1.h"
#include "decoder_api.h"

// ============================================================================
// PÉLDA 1: Alapvető AGC használat
// ============================================================================

void pelda1_AlapvetoAgcHasznalat() {
    AudioProcessorC1 audioProcessor;

    // Konfiguráció előkészítése
    AdcDmaC1::CONFIG config;
    config.audioPin = A0;        // GPIO26
    config.sampleCount = 256;    // FFT méret
    config.samplingRate = 12000; // 12kHz mintavételezés

    // Audio processor inicializálása FFT-vel
    audioProcessor.initialize(config, true, false);

    // AGC bekapcsolása (alapértelmezett beállítások)
    audioProcessor.setAgcEnabled(true);

    // Zajszűrés bekapcsolása 3-pontos simítással
    audioProcessor.setNoiseReductionEnabled(true);
    audioProcessor.setSmoothingPoints(3);

    // Indítás
    audioProcessor.start();

    // Feldolgozás...
    SharedData sharedData;
    while (true) {
        if (audioProcessor.processAndFillSharedData(sharedData)) {
            // Feldolgozott adatok elérhetők a sharedData-ban
            // - sharedData.rawSampleData: AGC-vel erősített minták
            // - sharedData.fftSpectrumData: FFT spektrum

            // Debug: Aktuális AGC erősítés kiírása
            float currentGain = audioProcessor.getCurrentAgcGain();
            DEBUG("AGC Gain: %.2f\n", currentGain);
        }
        delay(10);
    }
}

// ============================================================================
// PÉLDA 2: Manuális gain használat
// ============================================================================

void pelda2_ManualisGainHasznalat() {
    AudioProcessorC1 audioProcessor;

    AdcDmaC1::CONFIG config;
    config.audioPin = A0;
    config.sampleCount = 128;
    config.samplingRate = 8000;

    audioProcessor.initialize(config, true, false);

    // AGC kikapcsolása, manuális gain beállítása
    audioProcessor.setAgcEnabled(false);
    audioProcessor.setManualGain(3.0f); // 3x erősítés

    // Zajszűrés bekapcsolva
    audioProcessor.setNoiseReductionEnabled(true);
    audioProcessor.setSmoothingPoints(3);

    audioProcessor.start();

    // Feldolgozás fix 3x erősítéssel...
}

// ============================================================================
// PÉLDA 3: CW Dekóderhez optimalizált beállítások
// ============================================================================

void pelda3_CwDekoderBeallitasok() {
    AudioProcessorC1 audioProcessor;

    AdcDmaC1::CONFIG config;
    config.audioPin = A0;
    config.sampleCount = CW_RAW_SAMPLES_SIZE; // 128
    config.samplingRate = 2000;               // CW sávszélességből számolva

    // FFT NEM kell a CW dekódernek (csak nyers minták)
    audioProcessor.initialize(config, false, false);

    // CW-hez optimális beállítások:
    audioProcessor.setAgcEnabled(true); // AGC be - gyenge CW jelek detektálása
    audioProcessor.setNoiseReductionEnabled(true);
    audioProcessor.setSmoothingPoints(3); // Enyhe simítás - gyors válasz a pontokra

    audioProcessor.start();

    SharedData sharedData;
    while (true) {
        if (audioProcessor.processAndFillSharedData(sharedData)) {
            // A sharedData.rawSampleData tartalmazza az AGC-vel erősített mintákat
            // Ezt továbbítjuk a CW dekódernek
            // cwDecoder.processSamples(sharedData.rawSampleData, sharedData.rawSampleCount);
        }
    }
}

// ============================================================================
// PÉLDA 4: SSTV Dekóderhez optimalizált beállítások
// ============================================================================

void pelda4_SstvDekoderBeallitasok() {
    AudioProcessorC1 audioProcessor;

    AdcDmaC1::CONFIG config;
    config.audioPin = A0;
    config.sampleCount = SSTV_RAW_SAMPLES_SIZE;          // 1024
    config.samplingRate = C_SSTV_DECODER_SAMPLE_RATE_HZ; // 15000 Hz

    // SSTV-nek NEM kell FFT (blokkoló DMA mód)
    audioProcessor.initialize(config, false, true);

    // SSTV-hez optimális beállítások:
    audioProcessor.setAgcEnabled(true); // AGC be - stabil képminőség
    audioProcessor.setNoiseReductionEnabled(true);
    audioProcessor.setSmoothingPoints(5); // Erősebb simítás - zajmentes kép

    audioProcessor.start();

    SharedData sharedData;
    while (true) {
        if (audioProcessor.processAndFillSharedData(sharedData)) {
            // SSTV dekóder feldolgozása
            // sstvDecoder.processSamples(sharedData.rawSampleData, sharedData.rawSampleCount);
        }
    }
}

// ============================================================================
// PÉLDA 5: Dinamikus AGC paraméter változtatás
// ============================================================================

void pelda5_DinamikusAgcBeallitas() {
    AudioProcessorC1 audioProcessor;

    AdcDmaC1::CONFIG config;
    config.audioPin = A0;
    config.sampleCount = 256;
    config.samplingRate = 12000;

    audioProcessor.initialize(config, true, false);
    audioProcessor.start();

    // Kezdeti beállítás: Auto AGC
    audioProcessor.setAgcEnabled(true);
    audioProcessor.setNoiseReductionEnabled(true);
    audioProcessor.setSmoothingPoints(3);

    SharedData sharedData;
    uint32_t modeChangeTimer = 0;
    bool useAutoMode = true;

    while (true) {
        if (audioProcessor.processAndFillSharedData(sharedData)) {

            // Példa: Váltás AGC és manuális mód között 10 másodpercenként
            if (millis() - modeChangeTimer > 10000) {
                modeChangeTimer = millis();
                useAutoMode = !useAutoMode;

                if (useAutoMode) {
                    DEBUG("Váltás AUTO AGC módra\n");
                    audioProcessor.setAgcEnabled(true);
                } else {
                    DEBUG("Váltás MANUAL GAIN módra (2.0x)\n");
                    audioProcessor.setAgcEnabled(false);
                    audioProcessor.setManualGain(2.0f);
                }
            }

            // Aktuális állapot monitorozása
            if (audioProcessor.isAgcEnabled()) {
                float gain = audioProcessor.getCurrentAgcGain();
                DEBUG("AUTO mód - aktuális gain: %.2f\n", gain);
            } else {
                float gain = audioProcessor.getManualGain();
                DEBUG("MANUAL mód - beállított gain: %.2f\n", gain);
            }
        }
        delay(100);
    }
}

// ============================================================================
// PÉLDA 6: UI integráció - felhasználói beállítások
// ============================================================================

class AudioSettingsUI {
  private:
    AudioProcessorC1 *audioProcessor_;

    bool agcEnabled_;
    float manualGain_;
    bool noiseReductionEnabled_;
    uint8_t smoothingPoints_;

  public:
    AudioSettingsUI(AudioProcessorC1 *processor) : audioProcessor_(processor), agcEnabled_(true), manualGain_(1.0f), noiseReductionEnabled_(true), smoothingPoints_(3) { applySettings(); }

    void setAgc(bool enabled) {
        agcEnabled_ = enabled;
        audioProcessor_->setAgcEnabled(enabled);
        DEBUG("AGC: %s\n", enabled ? "BE" : "KI");
    }

    void setManualGain(float gain) {
        manualGain_ = constrain(gain, 0.1f, 20.0f);
        audioProcessor_->setManualGain(manualGain_);
        DEBUG("Manuális Gain: %.1fx\n", manualGain_);
    }

    void increaseGain() {
        if (!agcEnabled_) {
            manualGain_ = constrain(manualGain_ + 0.5f, 0.1f, 20.0f);
            setManualGain(manualGain_);
        }
    }

    void decreaseGain() {
        if (!agcEnabled_) {
            manualGain_ = constrain(manualGain_ - 0.5f, 0.1f, 20.0f);
            setManualGain(manualGain_);
        }
    }

    void setNoiseReduction(bool enabled) {
        noiseReductionEnabled_ = enabled;
        audioProcessor_->setNoiseReductionEnabled(enabled);
        DEBUG("Zajszűrés: %s\n", enabled ? "BE" : "KI");
    }

    void toggleSmoothingLevel() {
        smoothingPoints_ = (smoothingPoints_ == 3) ? 5 : 3;
        audioProcessor_->setSmoothingPoints(smoothingPoints_);
        DEBUG("Simítás: %d-pontos\n", smoothingPoints_);
    }

    void applySettings() {
        audioProcessor_->setAgcEnabled(agcEnabled_);
        audioProcessor_->setManualGain(manualGain_);
        audioProcessor_->setNoiseReductionEnabled(noiseReductionEnabled_);
        audioProcessor_->setSmoothingPoints(smoothingPoints_);
    }

    void printStatus() {
        DEBUG("=== Audio Beállítások ===\n");
        DEBUG("AGC: %s\n", agcEnabled_ ? "BE" : "KI");
        if (!agcEnabled_) {
            DEBUG("Manuális Gain: %.1fx\n", manualGain_);
        } else {
            float currentGain = audioProcessor_->getCurrentAgcGain();
            DEBUG("Aktuális AGC Gain: %.2fx\n", currentGain);
        }
        DEBUG("Zajszűrés: %s\n", noiseReductionEnabled_ ? "BE" : "KI");
        if (noiseReductionEnabled_) {
            DEBUG("Simítás: %d-pontos\n", smoothingPoints_);
        }
        DEBUG("========================\n");
    }
};

// Használat:
void pelda6_UiIntegracio() {
    AudioProcessorC1 audioProcessor;

    AdcDmaC1::CONFIG config;
    config.audioPin = A0;
    config.sampleCount = 256;
    config.samplingRate = 12000;

    audioProcessor.initialize(config, true, false);
    audioProcessor.start();

    // UI objektum létrehozása
    AudioSettingsUI settings(&audioProcessor);

    // Kezdeti állapot kiírása
    settings.printStatus();

    // Felhasználói interakció szimulálása
    delay(5000);

    // AGC kikapcsolása
    settings.setAgc(false);
    settings.setManualGain(2.5f);

    delay(5000);

    // Zajszűrés erősítése
    settings.toggleSmoothingLevel(); // 3 -> 5 pontos

    delay(5000);

    // Vissza auto módba
    settings.setAgc(true);

    // Állapot kiírása
    settings.printStatus();
}

// ============================================================================
// PÉLDA 7: Adaptív beállítások jelerősség alapján
// ============================================================================

void pelda7_AdaptivBeallitasok() {
    AudioProcessorC1 audioProcessor;

    AdcDmaC1::CONFIG config;
    config.audioPin = A0;
    config.sampleCount = 256;
    config.samplingRate = 12000;

    audioProcessor.initialize(config, true, false);
    audioProcessor.start();

    // Kezdeti beállítás
    audioProcessor.setAgcEnabled(true);
    audioProcessor.setNoiseReductionEnabled(true);
    audioProcessor.setSmoothingPoints(3);

    SharedData sharedData;
    const int SIGNAL_CHECK_SAMPLES = 10;
    int weakSignalCount = 0;

    while (true) {
        if (audioProcessor.processAndFillSharedData(sharedData)) {

            // Jelerősség ellenőrzése
            int32_t maxAbs = 0;
            for (int i = 0; i < sharedData.rawSampleCount; i++) {
                int32_t abs_val = abs(sharedData.rawSampleData[i]);
                if (abs_val > maxAbs)
                    maxAbs = abs_val;
            }

            // Gyenge jel detektálása
            if (maxAbs < 500) {
                weakSignalCount++;
            } else {
                weakSignalCount = 0;
            }

            // Ha folyamatosan gyenge a jel, váltás erősebb zajszűrésre
            if (weakSignalCount >= SIGNAL_CHECK_SAMPLES) {
                if (audioProcessor.isNoiseReductionEnabled()) {
                    audioProcessor.setSmoothingPoints(5); // Erősebb simítás
                    DEBUG("Gyenge jel detektálva - váltás 5-pontos simításra\n");
                }
                weakSignalCount = 0; // Reset
            }
        }
        delay(50);
    }
}

// ============================================================================
// MEGJEGYZÉSEK ÉS TIPPEK
// ============================================================================

/*
 * TELJESÍTMÉNY OPTIMALIZÁLÁS:
 * ---------------------------
 * - Az AGC és zajszűrés CPU overhead-je elhanyagolható (~30-35 µs / blokk)
 * - Ha mégsem kell, kapcsold ki mindkettőt:
 *   audioProcessor.setAgcEnabled(false);
 *   audioProcessor.setManualGain(1.0f);
 *   audioProcessor.setNoiseReductionEnabled(false);
 *
 * ZAJSZŰRÉS FINOMHANGOLÁS:
 * ------------------------
 * - 3-pontos: Gyors válasz, enyhe simítás - CW, RTTY-hez ajánlott
 * - 5-pontos: Lassabb válasz, erősebb simítás - SSTV, WEFAX-hoz ajánlott
 *
 * AGC FINOMHANGOLÁS:
 * ------------------
 * - Ha túl agresszív: Csökkentsd az ATTACK_COEFF értéket (0.3 -> 0.1)
 * - Ha túl lassú: Növeld az ATTACK_COEFF értéket (0.3 -> 0.5)
 * - Ha pumping hallható: Csökkentsd a RELEASE_COEFF értéket (0.01 -> 0.005)
 *
 * MINTAVÉTELEZÉSI FREKVENCIA:
 * ---------------------------
 * - AGC és zajszűrés NEM módosítja a mintavételezési frekvenciát
 * - A dekóderek számára minden változatlan marad
 * - Biztonságosan használható minden dekóderrel
 */
