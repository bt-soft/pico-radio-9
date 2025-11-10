/**
 * @file AdcDma-c1.cpp
 * @brief ADC-DMA osztály implementációja a Core-1 számára
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 */
#include <pico/stdlib.h>

#include "AdcDma-c1.h"
#include "defines.h"

// ADC-DMA működés debug engedélyezése de csak DEBUG módban
#if defined(__DEBUG) && defined(__ADCDMA_DEBUG)
#define ADCDMA_DEBUG(fmt, ...) DEBUG(fmt __VA_OPT__(, ) __VA_ARGS__)
#else
#define ADCDMA_DEBUG(fmt, ...) // Üres makró, ha __DEBUG nincs definiálva
#endif

/**
 * @brief Beállítja és elindítja a DMA átvitelt a megadott pufferbe.
 * @param buffer Pointer a célpufferre, ahova a DMA írni fog.
 */
void AdcDmaC1::configureDmaTransfer(uint16_t *buffer) {
    dma_channel_configure(dmaChannel,    // DMA csatorna
                          &dmaConfig,    // konfiguráció
                          buffer,        // cél cím
                          &adc_hw->fifo, // forrás cím
                          sampleCount,   // átvitel száma (minták száma)
                          true);         // DMA azonnali engedélyezése
}

/**
 * @brief Inicializálja és elindítja az ADC-t és a DMA-t a megadott konfigurációval.
 * @param config A használni kívánt konfigurációs beállítások.
 */
void AdcDmaC1::initialize(const CONFIG &config) {
    ADCDMA_DEBUG("AdcDmaC1::initialize - KEZDÉS - dmaChannel=%d\n", dmaChannel);

    // Konfiguráció mentése
    // Robosztus pin->ADC konverzió:
    // Ha config.audioPin >= 26, akkor GPIO számot várunk (26..28)
    // Ha config.audioPin <= 2, akkor közvetlen ADC csatorna (0..2)
    if (config.audioPin >= 26U) {
        captureChannel = static_cast<uint8_t>(config.audioPin - 26);
    } else if (config.audioPin <= 2) {
        captureChannel = static_cast<uint8_t>(config.audioPin);
    } else {
        ADCDMA_DEBUG("AdcDmaC1::initialize - Figyelmeztetés: érvénytelen audioPin=%d, alapértelmezett csatorna 0 lesz használva\n", config.audioPin);
        captureChannel = 0;
    }

    // Ellenőrizzük, hogy a kért méret nem nagyobb-e a maximálisnál
    // Validáljuk a sampleCount-et és samplingRate-et
    if (config.sampleCount == 0) {
        ADCDMA_DEBUG("AdcDmaC1::initialize - HIBA: sampleCount nem lehet 0. Alapértelmezett %d lesz használva\n", MAX_CAPTURE_DEPTH);
        sampleCount = MAX_CAPTURE_DEPTH;
    } else if (config.sampleCount > MAX_CAPTURE_DEPTH) {
        sampleCount = MAX_CAPTURE_DEPTH;
        ADCDMA_DEBUG("AdcDmaC1::initialize - A kért sampleCount (%d) nagyobb a maximálisnál (%d). A maximális érték lesz használva.\n", config.sampleCount, MAX_CAPTURE_DEPTH);
    } else {
        sampleCount = config.sampleCount;
    }

    if (config.samplingRate == 0) {
        ADCDMA_DEBUG("AdcDmaC1::initialize - HIBA: samplingRate=0 érvénytelen. Alapértelmezett 44100 lesz használva\n");
        samplingRate = 44100;
    } else {
        samplingRate = config.samplingRate;
    }
    ADCDMA_DEBUG("AdcDmaC1::initialize - CPU core: %d, Channel: %d, Depth: %d, Rate: %d\n", get_core_num(), captureChannel, sampleCount, samplingRate);

    // ADC inicializálása
    ADCDMA_DEBUG("AdcDmaC1::initialize - ADC hardver inicializálása\n");
    adc_init();

    uint32_t gpio_pin = 26 + captureChannel;
    ADCDMA_DEBUG("AdcDmaC1::initialize - GPIO %d beállítása az ADC %d csatornához\n", gpio_pin, captureChannel);
    adc_gpio_init(gpio_pin);

    float clkdiv = ((float)ADC_CLOCK / samplingRate) - 1.0f;
    ADCDMA_DEBUG("AdcDmaC1::initialize - ADC clock divider beállítása: %.2f (ADC_CLOCK=%d, SampleRate=%d)\n", clkdiv, ADC_CLOCK, samplingRate);
    adc_set_clkdiv(clkdiv);

    // Additional debug: print computed clkdiv and expected sample rate to help diagnose
    // issues where the actual sampling rate differs from configuration (e.g. factor of 2).
    ADCDMA_DEBUG("AdcDmaC1::initialize - DEBUG: számított clkdiv=%.6f, beállított samplingRate=%d\n", clkdiv, samplingRate);

    // DMA inicializálása
    ADCDMA_DEBUG("AdcDmaC1::initialize - DMA csatorna lefoglalása\n");
    dmaChannel = dma_claim_unused_channel(true);
    ADCDMA_DEBUG("AdcDmaC1::initialize - Lefoglalt DMA csatorna: %d\n", dmaChannel);
    dmaConfig = dma_channel_get_default_config(dmaChannel);

    channel_config_set_transfer_data_size(&dmaConfig, DMA_SIZE_16); // 16-bit adatátvitel
    channel_config_set_read_increment(&dmaConfig, false);           // ADC FIFO cím nem változik
    channel_config_set_write_increment(&dmaConfig, true);           // Puffer cím növekszik
    channel_config_set_dreq(&dmaConfig, DREQ_ADC);                  // ADC DREQ használata

    // DMA interrupt prioritás beállítása - magasabb prioritás az audio feldolgozáshoz!
    // Az RP2040-en a DMA IRQ0 és IRQ1 külön prioritást kaphat.
    // Alacsonyabb szám = magasabb prioritás (0-255, default 128)
    // Így az audio DMA nem blokkolódik a TFT SPI műveletektől
    irq_set_priority(DMA_IRQ_0, 0x40); // Magasabb prioritás mint az SPI (default 0x80)

    // ADC beállítása a mintavételezéshez
    ADCDMA_DEBUG("AdcDmaC1::initialize - ADC bemenet konfigurálása\n");
    adc_select_input(captureChannel);
    adc_fifo_setup(true, true, 1, false, false); // FIFO engedélyezése, DMA kérések engedélyezése

    // FIFO kiürítése mielőtt elindítanánk az ADC-t és DMA-t
    adc_fifo_drain();
    ADCDMA_DEBUG("AdcDmaC1::initialize - ADC FIFO kiürítve, készen áll az indításra\n");

    adc_run(true); // ADC elindítása

    // Az első DMA transzfer elindítása a ping pufferbe
    configureDmaTransfer(pingBuffer.data());
    isPingActive = true;

    ADCDMA_DEBUG("AdcDmaC1::initialize - === AdcDmaC1::initialize OK ===\n");
}

/**
 * @brief Leállítja az ADC-t és a DMA-t, és felszabadítja az erőforrásokat.
 *
 * Ezt a függvényt hívja a destruktor, de manuálisan is hívható, ha ideiglenesen
 * le szeretnénk állítani a mintavételezést.
 */
void AdcDmaC1::finalize() {
    // KRITIKUS: ADC-t ELŐSZÖR leállítjuk, hogy ne generáljon több DREQ-et a DMA-nak
    adc_run(false);
    adc_fifo_drain(); // ADC FIFO kiürítése
    ADCDMA_DEBUG("AdcDmaC1::finalize - ADC leállítva és FIFO kiürítve.\n");

    // Ellenőrizzük, hogy van-e érvényes DMA csatorna
    if (dmaChannel < NUM_DMA_CHANNELS && dma_channel_is_claimed(dmaChannel)) {
        ADCDMA_DEBUG("AdcDmaC1::finalize - DMA csatorna %d leállítása...\n", dmaChannel);

        dma_channel_abort(dmaChannel);

        // KRITIKUS: Várunk, amíg a DMA tényleg leáll
        // A dma_channel_abort() nem blokkoló, így explicit várakozás kell
        uint32_t timeout = 0;
        while (dma_channel_is_busy(dmaChannel) && timeout < 10000) {
            tight_loop_contents();
            timeout++;
        }

        dma_channel_unclaim(dmaChannel);
        ADCDMA_DEBUG("AdcDmaC1::finalize - DMA csatorna (%d) leállítva és felszabadítva (timeout=%d).\n", dmaChannel, timeout);

        // Jelezzük, hogy nincs érvényes DMA csatorna
        dmaChannel = 255;
    } else {
        ADCDMA_DEBUG("AdcDmaC1::finalize - Nincs érvényes DMA csatorna (dmaChannel=%d).\n", dmaChannel);
    }

    // A statikus pufferek memóriáját nem kell felszabadítani,
    // az az objektum életciklusához van kötve.
}

/**
 * @brief Leállítja, majd újraindítja a mintavételezést egy új konfigurációval.
 * @param config Az új konfigurációs beállítások.
 */
void AdcDmaC1::reconfigure(const CONFIG &config) {
    ADCDMA_DEBUG("AdcDmaC1::reconfigure - Mintavételezés újrakonfigurálása...\n");
    finalize();
    initialize(config);
}

/**
 * @brief Visszaadja a megtelt ping/pong puffert blokkoló vagy nem-blokkoló módban.
 *
 * @param blocking Ha true, blokkoló várakozás; ha false, azonnali visszatérés nullptr-rel, ha nincs kész adat.
 * @return Pointer a teli puffer adatainak kezdetére, vagy nullptr (ha blocking=false és az átvitel még folyamatban van).
 */
uint16_t *AdcDmaC1::getCompletePingPongBufferPtr(bool blocking) {

    // Ellenőrizzük, hogy a DMA átvitel befejeződött-e
    if (!blocking && dma_channel_is_busy(dmaChannel)) {
        // Nem-blokkoló mód: a DMA még dolgozik, nincs új adat
        return nullptr;
    }

    if (blocking) {
        // Blokkoló mód: várjuk meg a DMA befejezését
        dma_channel_wait_for_finish_blocking(dmaChannel);
    }

    // A DMA végzett, a legutóbb írt puffer most már a "teli" puffer.
    // Visszaadjuk a pointerét, és elindítjuk a DMA-t a másik, "üres" pufferre.
    uint16_t *completed_buffer_ptr;
    if (isPingActive) {
        // A Ping puffer telt meg. Visszaadjuk a pointerét.
        completed_buffer_ptr = pingBuffer.data();
        // A következő DMA a Pong pufferbe fog írni.
        configureDmaTransfer(pongBuffer.data());
    } else {
        // A Pong puffer telt meg. Visszaadjuk a pointerét.
        completed_buffer_ptr = pongBuffer.data();
        // A következő DMA a Ping pufferbe fog írni.
        configureDmaTransfer(pingBuffer.data());
    }

    // Váltunk a két puffer között.
    isPingActive = !isPingActive;

    return completed_buffer_ptr;
}
