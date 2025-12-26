# pico-radio-9


## A kütyü
![A kísérleti kütyü](doc/pictures/A%20kísérleti%20kütyü.jpg)
<br/>
<em>A kísérleti kütyü (egy V7-es prototípus hangfrekis műveleti erősítővel kiegészítve)</em>



### PCB 3D View

<table style="border:none; width:100%;">
  <tr>
    <td style="border:none; text-align:center; vertical-align:middle;">
      <img src="doc/pictures/PCB%203D%20View%20-%20kétoldalas.png" alt="PCB 3D View - kétoldalas" width="50%"/>
    </td>
    <td style="border:none; text-align:center; vertical-align:middle;">
      <img src="doc/pictures/PCB%203D%20View.png" alt="PCB 3D nézet - egyoldalas" width="50%"/>
    </td>
  </tr>
  <tr>
    <td style="border:none; text-align:center;">
      <em>Kétoldalas NYÁK terv</em>
    </td>
    <td style="border:none; text-align:center;">
      <em>Egyoldalas NYÁK terv</em>
    </td>
  </tr>
</table>

**A projekt még fejlesztés alatt áll!**

Egy Raspberry Pi Pico alapú rádióprojekt (Si4735 vezérléssel) — fejlett dekóderekkel (RTTY, CW, SSTV, WeFax), spektrum megjelenítéssel és interaktív érintőképernyős felülettel.

**Fő funkciók**
- Si4735 vezérlés (AM/FM/SDR jellegű demodok és RDS)
- Dekóderek: RTTY, CW, SSTV, WeFax, RTTY feldolgozó
- Valós idejű spektrum- és vízesés (low/high resolution bar, oscilloscope, envelope, waterfall, valamint CW/RTTY hangolássegéd waterfall és SNR burkoló) megjelenítők
- Hangfeldolgozó pipeline (AGC, FFT alapú vizualizáció manuális vagy automatikus erősítéssel)
- Rétegzett dialógus rendszer: többgombos dialógusok, értékmódosítók (rotary/touch)
- Konfigurációs tárolás EEPROM-ban (station/setting store)

## Architektúra
 - RP2040 / multicore: A Raspberry Pi Pico kétmagos RP2040 mikrovezérlője lehetővé teszi a feladatok elkülönítését (audio-feldolgozás a második magon, míg az első mag a UI/Si4735 vezérlést végzi). Ezt a projekt kihasználja a valós idejű audio dekódolás és UI műveletek párhuzamosítása érdekében.


## Technológiák

- Platform: Raspberry Pi Pico (RP2040)
- Fejlesztési környezet: PlatformIO 
- Framework: Arduino (RP2040 / Arduino core) 
- Nyelv: C++17 
- TFT vezérlés: `TFT_eSPI` — ILI9844 3.5" TFT grafikus megjelenítés és touch kezelése.
- Si4735 vezérlés: `PU2CLR SI4735` wrapper és a projekt saját `Si4735*` moduljai (I2C/SPI integráció és rádiófunkciók).
- Audio feldolgozás: FFT alapú spektrum- és vízesés (waterfall) megjelenítés, AGC és egyedi AudioProcessor pipeline.
- Dekóderek: RTTY, CW, SSTV, WeFax implementációk (saját `Decoder*` modulok).
- UI komponensek: egyedi komponenskeretrendszer (`UI*` osztályok) — dialógusok, többgombos menük, virtuális billentyűzet és grafikus komponensek.
- Időzítés és megszakítások: `RPI_PICO_TimerInterrupt` (PlatformIO lib) a pontos időzítéshez.
- FFT könyvtár: `arduinoFFT` a spektrum-számításokhoz.
- Helyi segédkönyvtárak: `lib/pico_sstv` az SSTV dekódoláshoz és segédfüggvényekhez.


## Felhasznált könyvtárak
- `Arduino-Pico`: https://github.com/earlephilhower/arduino-pico
- `TFT eSPI`: https://github.com/Bodmer/TFT_eSPI
- `RPI PICO TimerInterrupt`: https://github.com/khoih-prog/RPI_PICO_TimerInterrupt
- `PU2CLR SI4735`: https://github.com/pu2clr/SI4735
- `Arduino CMSIS-DSP`: https://www.arduinolibraries.info/libraries/arduino_cmsis-dsp
- `lib/pico_sstv`: https://github.com/dawsonjon/PicoSSTV


## Fájlok és modulok (összefoglaló)
- `src/` - forráskód (képernyők, dekóderek, UI komponensek, vezérlők)
  - `main.cpp`, `main-c1.cpp` - indító fájlok
  - `Screen*` - különböző képernyők (RTTY, CW, FM, AM, SSTV, WeFax, Setup, stb.)
  - `Decoder*` - dekóderek (RTTY, CW, SSTV, WeFax, stb.)
  - `UI*` - UI keretrensdszer komponensei (gombok, dialógusok, spektrum, textbox, stb.)
  - `Si4735*` - Si4735 kezelő és runtime logika
  - `AudioController.*`, `AudioProcessor-*` - audio pipeline és feldolgozás
- `include/` - header fájlok (összes osztály, UI, konfigurációs struktúrák)
  - `Config.h`, `ConfigData.h` - konfigurációs struktúrák és default értékek
  - `UIDialogBase.h`, `UIMultiButtonDialog.h`, `UIValueChangeDialog.h` - dialógusok és vezérlők
  - `UIHorizontalButtonBar.h` - alsó gombsor kezelése
- `lib/` - külső vagy saját könyvtárak (pl. `pico_sstv`, `CMSIS-DSP`)
- `doc/` - dokumentációs anyagok 
- `test/` - teszt erőforrások és példa adatállományok (pl. cw, rtty tesztek)


## Konfiguráció és mentés
- Konfigurációk a `Config` struktúrában találhatók (`include/Config.h`, `include/ConfigData.h`).
- Station / memory store beállítások EEPROM-ban tárolódnak (lásd `StationStore*` és `Eeprom*` fájlok).


## UI képernyők, dialógusok
- Rétegzett dialógus rendszer: a `UIScreen` tartalmaz egy dialógusstack-et, a `showDialog()` és `onDialogClosed()` mechanizmusokkal.
- Többgombos dialógusok: `UIMultiButtonDialog` + `UIValueChangeDialog` az értékek szerkesztéséhez.



## Szoftverarchitektúra
- Dekóderek: `src/Decoder*.cpp` — új dekóderek hozzáadása egyszerű az `IDecoder` interfész implementálásával.
- UI komponensek: `include/UI*`, `src/UI*` — gombok, dialógusok, új vizualizációk hozzáadása.
- Audio pipeline: `AudioController.cpp` és `AudioProcessor-*.cpp` fájlok tartalmazzák a feldolgozás logikáját.


## Hibakeresés / debug
- A forrásban `DEBUG(...)` makrók és logolási segédek találhatók. A debug build beállítása és a `__DEBUG` makró használata segíthet a problémakeresésben.


## Kapcsolási rajz / nyomtatott áramkör
- Kicad formátumban a `kicad` mappában (egy- és kétoldalas NYÁK tervvel).

---

## Harmadik féltől származó források
Az alábbi forrás nyújtottak inspirációkat a projekt fejlesztése és megvalósítása során.

 - **SI4735 + TFT példa (IU4ALH)**: https://github.com/IU4ALH/IU4ALH
  - Használat: Elsősorban a repó  `SI4735_2.8_TFT_SI5351_V.5.2b_Dark.zip` állományában található forrásokat használtam fel, amelyek inspirációt adtak a szolgáltatások, a képernyők felépítéséhez valamint az Si4735 beállítások kezeléséhez.

- **Rotary encoder / forgókapcsoló referencia**: https://www.mikrocontroller.net/articles/Drehgeber
  - Használat: A forgató encoder kezelésének elméleti háttere és debouncing/step logika inspirálására; segített a `RotaryEncoder` osztály és a forgóvezérlők finomhangolásában.

- **Pico SSTV implementáció**: https://github.com/dawsonjon/PicoSSTV
  - Használat: SSTV dekóder és mintafeldolgozó technikák inspirációjára használtam a projekt `DecoderSSTV` moduljának kialakításához.

- **fldigi**: https://github.com/w1hkj/fldigi
  - Használat: HF WeFax dekóder kiakalítása, technikai megoldások, szoftveres szűrők
---

## License
Projekt licenc: MIT 

