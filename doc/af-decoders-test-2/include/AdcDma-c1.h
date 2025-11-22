#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <hardware/adc.h>
#include <hardware/dma.h>
#include <hardware/irq.h>

// Előre definiált maximális puffer méret.
#define MAX_CAPTURE_DEPTH 1024

/**
 * @class AdcDma
 * @brief ADC mintavételezést kezelő osztály DMA segítségével, ping-pong puffereléssel.
 *
 * Ez az osztály lehetővé teszi a folyamatos, megszakításmentes ADC mintavételezést
 * a háttérben, DMA (Direct Memory Access) és egy dupla pufferelési (ping-pong)
 * mechanizmus segítségével. A pufferek statikusan vannak allokálva a maximális
 * teljesítmény és a determinisztikus memóriahasználat érdekében.
 */
class AdcDmaC1 {
  public:
    /**
     * @struct CONFIG
     * @brief Az AdcDmaC1 osztály konfigurációs struktúrája.
     */
    typedef struct CONFIG_ {
        uint16_t audioPin;     ///< Audio bemeneti pin (GPIO szám, pl. 26, 27, 28).
        uint16_t sampleCount;  ///< A használni kívánt puffer méret (mintákban). Nem lehet nagyobb, mint a MAX_CAPTURE_DEPTH.
        uint16_t samplingRate; ///< Mintavételezési frekvencia (Hz).
    } CONFIG;

    // Segéd: elfogadunk GPIO számot (pl. 26) vagy ADC csatornát (0..2).
    // Ha Arduino A0/A1 makrókat használsz, azok platformtól függően különbözhetnek,
    // ezért a hívó code-nak érdemes a dokumentáció szerint megadni a fizikai GPIO-t.

  private:
    /// @brief Az elsődleges (ping) puffer a DMA átvitelhez.
    std::array<uint16_t, MAX_CAPTURE_DEPTH> pingBuffer;
    /// @brief A másodlagos (pong) puffer a DMA átvitelhez.
    std::array<uint16_t, MAX_CAPTURE_DEPTH> pongBuffer;
    /// @brief Jelzi, hogy a 'ping' puffer-e az aktív (amelyikbe a DMA ír).
    bool isPingActive;

  public:
    /// @brief Az ADC hardver órajele (48MHz).
    static constexpr uint32_t ADC_CLOCK = (48 * 1000 * 1000);

    /**
     * @brief Az AdcDmaC1 osztály konstruktora.
     */
    AdcDmaC1() : isPingActive(true) {};

    /**
     * @brief Az AdcDmaC1 osztály destruktora.
     *
     * Gondoskodik a lefoglalt hardveres erőforrások (ADC, DMA) felszabadításáról.
     */
    ~AdcDmaC1() { finalize(); };

    /**
     * @brief Inicializálja és elindítja az ADC-t és a DMA-t a megadott konfigurációval.
     * @param config A használni kívánt konfigurációs beállítások.
     */
    void initialize(const CONFIG &config);

    /**
     * @brief Leállítja az ADC-t és a DMA-t, és felszabadítja az erőforrásokat.
     *
     * Ezt a függvényt hívja a destruktor, de manuálisan is hívható, ha ideiglenesen
     * le szeretnénk állítani a mintavételezést.
     */
    void finalize(void);

    /**
     * @brief Leállítja, majd újraindítja a mintavételezést egy új konfigurációval.
     * @param config Az új konfigurációs beállítások.
     */
    void reconfigure(const CONFIG &config);

    /**
     * @brief Visszaadja a megtelt ping/pong puffert blokkoló vagy nem-blokkoló módban.
     *
     * Ha `blocking=true`: Megvárja, amíg a DMA átvitel befejeződik, majd visszaadja
     * a teli puffer pointerét. SSTV és WEFAX dekóderekhez ajánlott, ahol garantáltan
     * teljes blokkokra van szükség.
     *
     * Ha `blocking=false`: Azonnal visszatér. Ha a DMA átvitel még folyamatban van,
     * `nullptr`-t ad vissza. CW és RTTY dekóderekhez ajánlott, ahol kisebb késleltetés
     * szükséges.
     *
     * @param blocking Ha true, blokkoló várakozás; ha false, azonnali visszatérés nullptr-rel, ha nincs kész adat.
     * @return Pointer a teli puffer adatainak kezdetére, vagy `nullptr` (ha blocking=false és nincs új adat).
     */
    uint16_t *getCompletePingPongBufferPtr(bool blocking = true);

    /**
     * @brief Visszaadja az aktuálisan használt ADC csatornát.
     * @return Az ADC csatorna száma (0, 1, vagy 2).
     */
    uint8_t getCaptureChannel() { return captureChannel; }

    /**
     * @brief Visszaadja az aktuális mintavételezési frekvenciát.
     * @return A mintavételezési frekvencia Hz-ben.
     */
    uint32_t getSamplingRate() { return samplingRate; }

    /**
     * @brief Visszaadja az aktuális puffer mélységet.
     * @return A puffer mélysége (minták száma).
     */
    uint16_t getSampleCount() { return sampleCount; }

  private:
    uint8_t dmaChannel;           ///< A használt DMA csatorna azonosítója.
    dma_channel_config dmaConfig; ///< A DMA csatorna konfigurációja.

    uint8_t captureChannel; ///< A használt ADC csatorna (0-2).
    uint16_t sampleCount;   ///< Az aktív puffer mélység (mintákban).
    uint16_t samplingRate;  ///< Az aktív mintavételezési frekvencia (Hz).

    /**
     * @brief Beállítja és elindítja a DMA átvitelt a megadott pufferbe.
     * @param buffer Pointer a célpufferre, ahova a DMA írni fog.
     */
    void configureDmaTransfer(uint16_t *buffer);
};