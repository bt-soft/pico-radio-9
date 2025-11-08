/**
 * @file PicoSensorUtils.cpp
 * @brief Pico érzékelő funkciók implementációja - CORE1 ALAPÚ MÉRÉSEK
 * @author BT-Soft (https://github.com/bt-soft, https://electrodiy.blog.hu/)
 * @project Pico Radio
 *
 * Változások a korábbi verziókhoz képest:
 *  Az ADC méréseket TELJES EGÉSZÉBEN a Core1 végzi (main-c1.cpp / readSensorsOnCore1()).
 *  A core0-core1 nem képes osztozni az ADC-ken, így elkerüljük a csatornaváltás problémáit.
 */

#include "PicoSensorUtils.h"
#include "AudioController.h" // SharedData eléréshez (Core1 szenzor adatok)

// CORE1 szenzor adatok (Core1 írja az ADC-t, Core0 csak olvassa + kijelzi!)
extern volatile float core1_VbusVoltage;    // VBUS feszültség (Volt) - Core1 méri ADC1-ről
extern volatile float core1_CpuTemperature; // CPU hőmérséklet (Celsius) - Core1 méri ADC4-ről

namespace PicoSensorUtils {

/**
 * AD inicializálása
 */
void init() {
    // MEGJEGYZÉS: Az ADC méréseket TELJES EGÉSZÉBEN a Core1 végzi!
    // Core0 CSAK OLVASSA a SharedData-ból az értékeket, NEM mér!
    // Ez a függvény most üres, de megtartjuk a kompatibilitás kedvéért.
}

/**
 * ADC olvasás és VBUS feszültség kiszámítása KÜLSŐ osztóval
 * @return A VBUS mért feszültsége Voltban.
 *
 * ÚJ IMPLEMENTÁCIÓ - CORE1 ALAPÚ:
 * A Core1 30 másodpercenként méri a VBUS feszültséget az ADC1-ről (GPIO27).
 * Az értéket a SharedData struktúrában tárolja.
 * Core0 CSAK OLVASSA az értéket, NEM mér!
 */
float readVBusExternal() {
    // Core1 által mért értéket visszaadjuk
    // DEBUG("PicoSensorUtils: readVBusExternal() = %.2f V\n", ::core1_VbusVoltage);
    return ::core1_VbusVoltage;
}

/**
 * @brief Kiolvassa a processzor hőmérsékletét
 * @details A hőmérsékletet az ADC4 bemeneten keresztül olvassa, és cache-eli az értéket.
 * @return A processzor hőmérséklete Celsius fokban
 *
 * ÚJ IMPLEMENTÁCIÓ - CORE1 ALAPÚ:
 * A Core1 30 másodpercenként méri a CPU hőmérsékletet az ADC4-ről (beépített szenzor).
 * Az értéket a SharedData struktúrában tárolja.
 * Core0 CSAK OLVASSA az értéket, NEM mér!
 */
float readCoreTemperature() {

    // Core1 által mért értéket visszaadjuk
    // DEBUG("PicoSensorUtils: readCoreTemperature() = %.2f °C\n", ::core1_CpuTemperature);
    return ::core1_CpuTemperature;
}

} // namespace PicoSensorUtils
