# SX1255 hardware frontend

Dokumentace reálného frontendu `SX1255Hardware`, který nahrazuje `NullSX1255`
a implementuje rozhraní `ISX1255` (SPI + libgpiod + ALSA).

> **Kernel a overlay:** připravené `rpi-zero2-kernel-6.18.54-myzero2+.deb` a
> `genericstereoaudiocodec.dtbo` jsou assety release v1.0.0-rc6. Instalace je
> popsána v [DEPLOYMENT.md](DEPLOYMENT.md).
>
> **Raspberry Pi 5 / Pi 500:** použij stock kernel a overlay
> `sx1255-pi5-duplex`; úplný postup je v
> [RPI5_SX1255_HAT.md](RPI5_SX1255_HAT.md).

## Architektura

```text
RadioFrontend (vlastní ISX1255)
        |
        v
SX1255Hardware : ISX1255
   |-- ISX1255SPI  (SX1255SpiDev, linux spidev)
   |-- ISX1255GPIO (SX1255Gpiod, libgpiod v2 přes dlopen)
   `-- ALSA capture + SPSCQueue RX + přímý ALSA playback
```

Frontend **nezná** FM, D-Star, M17, SessionManager ani PluginRegistry. Pouze
přesouvá IQ vzorky a řídí RF. Data tečou takto:

```text
ALSA capture (S32_LE stereo, 125 kSa/s)
  -> int32/2^31 -> ComplexFloat
  -> SPSCQueue RX (lock-free)
  -> readIQ()  (RadioFrontend -> IQDispatcher -> M17Plugin)
```

TX jde přímo z volajícího vlákna pluginu:

```text
plugin (FM/D-Star/M17)
  -> writeIQ()
  -> blocking snd_pcm_writei()
  -> ALSA playback ring
  -> RP1 I2S consumer
  -> SX1255 (BCLK/LRCLK master)
```

V TX cestě není mezilehlá fronta. `writeIQ()` zapisuje do ALSA kruhového
bufferu blokujícím voláním, takže tempo producenta přímo určují hodiny SX1255.
RX capture vlákno běží i během TX; SX1255 zůstává v RX režimu a současně
zapíná TX cestu, takže zařízení pracuje plně duplexně.

## Vrstvy

### SX1255SPI

- `open()` / `close()` / `readRegister()` / `writeRegister()` /
  `burstRead()` / `burstWrite()`.
- Linux `spidev` přes `ioctl(SPI_IOC_MESSAGE)`, mode 0, 8 bitů.
- Žádná závislost na WiringPi.

### SX1255GPIO

- `reset`, `tx_enable`, `rx_enable`, `ptt` jako volitelné linky (záporné číslo
  = nezapojeno).
- `libgpiod` v2 načítané dynamicky (`dlopen`), takže build nepotřebuje
  `libgpiod-dev`; deprecated sysfs GPIO se nepoužívá.

### ALSA / IQ vrstva

- Capture: S32_LE stereo, skutečný I2S clock 125 kSa/s, perioda 4096 rámců,
  buffer 65536 rámců.
- Playback: stejné parametry. Na Pi 5 může ALSA kvůli stub codecu hlásit
  nominálních 96 kHz; fyzické tempo určuje externí BCLK z SX1255.
- Normalizace převzatá z m17-gateway: `sample / 2147483648.0` (MSB-aligned
  S32 → float).
- RX používá jedno capture vlákno a jednu lock-free `SPSCQueue`. Prázdná
  fronta vede ke krátkému spánku; plná fronta zahodí další RX vzorek a zvýší
  `rxOverruns`.
- TX nemá samostatné playback vlákno ani druhou frontu. Plugin volá
  `writeIQ()`; při plném ALSA bufferu se zápis zablokuje a pokračuje, až
  hardware odešle vzorky. Částečné zápisy jsou dokončeny ve smyčce.
- Při skutečných 125 kSa/s odpovídá buffer 65536 rámců přibližně 524 ms a
  perioda 4096 rámců přibližně 33 ms.
- Při zapnutí PTT se playback znovu otevře, SX1255 se přepne do TX a čeká se
  na TX PLL lock. Před první IQ data se zapíše prefill 8192 nulových rámců
  (přibližně 65 ms nosné), který překlene start TX DSP.
- Při `-EPIPE`, `-ESTRPIPE`, `-EBADFD` nebo `-ENODEV` se playback znovu
  připraví a zápis se omezeně opakuje. Krátké podtečení může způsobit
  diskontinuitu vzorků a tím poškodit právě vysílaný rámec; obnova zajistí
  pokračování dalších vzorků, ale ne doplnění již vynechaného času.
- M17 a D-Star před vypnutím TX volají `drainTx()`, takže poslední rámce
  a EOT se odešlou. FM lokální smyčka při ukončení stream rovnou zahodí.

## Převzaté části

### Z m17-gateway

- Registrová mapa a mode bity (`sx1255_modem.go`): `0x00` control, `0x01-0x06`
  frekvence, `0x07` version, `0x08` DAC/mixer gain, `0x0B` DAC taps, `0x0C`
  LNA/PGA, `0x0D` RX BW, `0x12/0x13` I2S rate.
- `sx1255FreqToReg` (`freq * 2^20 / 32 MHz`).
- Inicializační sekvence: reset (high→low), `0x0D=0x37`, `0x0B=5`, verze
  `0x11`, `0x13=0x2C`, `0x12=0x22`, gainy, RX enable / TX disabled.
- Gain tabulky LNA (`48/42/36/24/12/0 dB`) a DAC (`0/-3/-6/-9 dB`) ze
  `sx1255_gain.go`.
- ALSA parametry a normalizace IQ (`sx1255_alsa.go`).

### Ze sx1255-fm-repeater

- Ověření hodnot registrů pro reset/režim (stejné hodnoty `0x2C`/`0x22`).

### Z DStarRepeater

- Dynamické načítání `libgpiod` v2 přes `dlopen` (fallback bez build závislosti).

## Refaktorizace a důvody

- Go `chan`/gorutiny → C++ vlákna + `SPSCQueue`.
- `go-gpiocdev` → vlastní `SX1255Gpiod` (dlopen libgpiod).
- `github.com/yobert/alsa` → přímé ALSA `snd_pcm_*`.
- `math/rand` / logovací frameworky nejsou portované (C++ protějšky).
- TX výkon **je** v registrech: `0x08` (TXFE1) má `tx_dac_gain` [6:4] a
  `tx_mix_gain` [3:0]. Absolutní dBm registr neexistuje a PA driver je jen
  on/off (bit 3 v `0x00`), takže výkon se skládá z DAC gain + mixer gain
  (v konfiguraci `dac_gain` / `mixer_gain`). `setPower(dBm)` za běhu
  programuje `tx_dac_gain`: 10 dBm → 0 dB, 7 → −3, 4 → −6, jinak −9.
- RSSI **není** v registrech SX1255 (RSSI má až doprovodný čip SX1257).
  Na SX1255 se RSSI počítá z výkonu I/Q vzorků — `10*log10(mean power)`,
  přesně jako to dělá m17-gateway ve FMDemodAFC. Absolutní kalibrace v dBm
  vyžaduje měření. Na tomto HW je naměřené minimum přibližně **-60**
  (relativní dBFS); zvýšení LNA z 24 na 42 dB v testu hodnotu nezměnilo.
  RSSI squelch zatím není implementovaný, proto se `squelch` v ukázkové
  konfiguraci neuvádí; FM plugin otevírá squelch šumovou detekcí nosné, ne
  podle RSSI.

## Diagnostika

`getStats()` / `stats()` vrací: RX vzorky, TX vzorky, RSSI, RX overruny, TX
underruny, SPI chyby, ALSA chyby.

Tyto frontendové čítače nejsou v současnosti součástí HTTP API. Trvalé TX
podtečení je vidět v systémovém logu jako `SX1255 playback write failed`;
přechodná obnova se loguje jako `SX1255 playback recover`.

## Testy (bez hardwaru)

- `test_sx1255_spi` — registrové programování přes mock SPI.
- `test_sx1255_gpio` — reset, TX/RX enable, PTT přes mock GPIO.
- `test_sx1255_buffering` — RX SPSC fronta a započtení TX vzorků při
  nedostupném playback streamu.

## Kritérium úspěchu

`SX1255Hardware -> RadioFrontend -> IQDispatcher -> M17Plugin` přijímá reálná
IQ data beze změn v M17 pluginu, SessionManageru a IQDispatcheru.
