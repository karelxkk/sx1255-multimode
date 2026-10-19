# sx1255-multimode v1.0

Multimode radioamatérský hotspot/repeater framework pro Raspberry Pi Zero 2 W
a SX1255 HAT. Jeden RF frontend, společný IQ stream, režimy jako nezávislé
pluginy.

> Autor: Karel Kadlec (OK1LBC) <xkk@post.cz>

## Základní myšlenka

- SX1255 frontend běží trvale a je **jediným** zdrojem IQ vzorků.
- Všechny demodulátory a protokoly jsou pluginy.
- Všechny pluginy dostávají **stejný** IQ stream.
- První plugin, který detekuje platný rámec/synchronizaci, získá exkluzivní
  vlastnictví relace.
- Po skončení relace se systém vrátí do režimu monitoringu.

Klíčový princip: *SX1255 je pouze RF frontend. Přidání nového režimu nesmí
vyžadovat změnu frontend vrstvy.*

## Režimy

| Režim | Stav | Detekce |
|---|---|---|
| FM (NFM) | plný RX→TX převaděč (bridge) | RSSI squelch + hang timer + volitelná regenerace CTCSS na TX |
| D-Star | lokální RX→TX převaděč + DSRP gateway | GMSK demodulace + FEC hlavička + re-modulace DV rámců/EOT + DSRP síť |
| M17 | 1:1 port z m17-gateway | 4FSK demod + sync + LSF/stream/packet dekódování |

Plánované: DMR, C4FM, POCSAG, APRS, AX.25, FreeDV, Codec2 experimenty.

### D-Star gateway

D-Star umí kromě lokálního převaděče i DSRP protokol pro
ircDDBGateway/DStarGateway. Povol a nastav v konfiguraci:

```yaml
modes:
  dstar:
    enabled: true
    callsign: OK1LBC
    gateway:
      enabled: true
      address: 127.0.0.1
      port: 20010
      local_address: 127.0.0.1
      local_port: 20011
      name: sx1255-multimode

network:
  mode: dstar    # auto | fm | dstar | m17
```

`network.mode: dstar` povolí síť → RF pro D-Star i bez lokální D-STAR
stanice; výchozí `auto` používá naposledy použitý lokální mód.

## Požadavky na build

- CMake >= 3.20
- GCC 12+ / Clang 15+ (C++20)
- Linux (Raspberry Pi OS, arm64)

Žádné externí knihovny. YAML je parsován vestavěným minimalistickým parserem
(podmnožina YAML), ALSA/SPI vrstva přijde později přes `ISX1255` rozhraní.

## Požadavky na hardware

- Raspberry Pi Zero 2 W + SX1255 HAT (arm64),
- kernel s podporou I2S vzorkování **125 000 Hz** (patch
  `0001-ASoC-allow-125000-Hz-CONTINUOUS-for-sx1255_sound.patch`, balíček
  `rpi-zero2-kernel-6.18.54-myzero2+.deb`),
- device-tree overlay `genericstereoaudiocodec` (zkompilovaný
  `genericstereoaudiocodec.dtbo`) a v `config.txt`:

  ```ini
  dtparam=spi=on
  dtparam=i2s=on
  dtoverlay=genericstereoaudiocodec
  ```

- pro `fm.backend: svxlink` navíc modul `snd-aloop`.

Oba soubory (`rpi-zero2-kernel-6.18.54-myzero2+.deb` a
`genericstereoaudiocodec.dtbo`) jsou přiložené jako assety GitHub release
[v1.0.0-rc4](https://github.com/karelxkk/sx1255-multimode/releases/tag/v1.0.0-rc4).
Odkazy pro stažení jsou vypsané v [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md).

Postup instalace kernelu a overlaye viz [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md).

## Build a testy

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Spuštění

```
cp sx1255-multimode.yaml.example sx1255-multimode.yaml
./build/sx1255-multimode -config sx1255-multimode.yaml
```

Bez hardwaru se použije `NullSX1255` (tichý zdroj), takže se dá spustit a
ladit i na běžném PC.

Po spuštění běží na portu `8080` diagnostické HTTP endpointy:

```
GET /               web dashboard (spektrum, waterfall, RSSI, aktivní plugin)
GET /api/spectrum   aktuální spektrum (PSD + peak-hold + noise floor)
GET /api/waterfall  historie spektra
GET /api/status     stav, režim, RSSI, uptime a čítače detekcí
GET /api/plugins    seznam pluginů s telemetrií
GET /api/m17/diagnostics  AFC, SyncDistance a posledních 100 M17 pokusů
POST /api/m17/diagnostics/reset  reset M17 decode čítačů
GET /api/iq         stav IQ záznamu/přehrávání
GET /api/health     stav subsystémů (watchdog)
GET /api/system     paměť, CPU, vlákna, stav frontendu
```

## Nástroje

```
sx1255-multimode   démon
sx1255-soak        soak test (24h/48h/7d, soak_report.json)
sx1255-bench       výkonnostní profil (FFT, waterfall, RX chain)
m17_rx_dump        offline rozbor sync burstů v IQ souboru
m17_iq_audit       offline M17 telemetrie a kandidáti do JSON
m17-frame-audit    per-frame audit softbitů, Viterbi, Golay, CRC a LSF
m17_afc_audit      AFC trace pro srovnání s původním m17-gateway
```

Deb balíček pro nasazení:

```text
scripts/build_deb.sh        # -> sx1255-multimode_1.0.0-rc4_arm64.deb
```

CLI démona:

```
--capture FILE     záznam živého IQ
--replay FILE      přehrání IQ bez rádia
--dataset DIR      replay všech datasetů + regression_report.json
--log-level LEVEL  debug | info | warn | error
```

## Adresářová struktura

```
include/sx1255/          veřejná rozhraní (frontend, core, plugin, config, dsp)
src/core/                dispatcher, session manager, registr pluginů
src/frontend/            RadioFrontend, ISX1255, NullSX1255
src/plugins/             FM, D-Star, M17
src/config/              YAML parser
src/monitor/             HealthMonitor (watchdog)
src/soak/                SoakTester
src/dataset/             dataset replay / regresní report
src/util/                Logger, SystemInfo
deploy/systemd/          systemd service
datasets/                IQ dataset workflow
tests/                   unit testy (ctest)
docs/                    architektura, plugin API, UML diagramy
```

## Dokumentace

- [Architektura](docs/architecture.md)
- [Plugin API](docs/plugin-api.md)
- [D-Star gateway (DSRP)](docs/DSTAR_GATEWAY.md)
- [Připojení FlexNetu](docs/FLEXNET_CONNECTION.md)
- [Session Manager](docs/session-manager.md)
- [M17 migrace z m17-gateway](docs/M17_MIGRATION.md)
- [M17 bridge: zprávy a poloha](docs/M17_BRIDGE.md)
- [M17 porting report](docs/M17_PORTING_REPORT.md)
- [M17 / DM-1701 AFC a SyncDistance analýza](docs/M17_DM1701_ANALYSIS.md)
- [M17 frame quality audit](docs/M17_FRAME_AUDIT.md)
- [M17 reference comparison](docs/M17_REFERENCE_COMPARISON.md)
- [SX1255 hardware frontend](docs/SX1255_HARDWARE.md)
- [Validace multimódové arbitráže](docs/MULTIMODE_VALIDATION.md)
- [IQ capture & replay](docs/IQ_CAPTURE.md)
- [Spectrum monitor a dashboard](docs/SPECTRUM_MONITOR.md)
- [Automatický IQ capture](docs/AUTOMATIC_CAPTURE.md)
- [Nasazení na RPi](docs/DEPLOYMENT.md)
- [Služba a běh](docs/SERVICE.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [Deb balíček a nasazení](docs/DEB_PACKAGE.md)
- [Výkonnostní profil](docs/PERFORMANCE.md)
- [UML diagramy](docs/uml/)

## Release

- [Release notes](RELEASE_NOTES.md)
- [Changelog](CHANGELOG.md)
- [Release checklist](RELEASE_CHECKLIST.md)
- [RC1 soak report](rc1-soak-report.md)
- [False positive audit](false_positive_report.md)
- [Performance RC1](performance_rc1.md)
- [Failure injection](failure_injection.md)

Validace na reálném hardwaru se spouští na cíli:

```text
scripts/rc1_validate.sh 604800    # 7 dní
scripts/record_dataset.sh m17 datasets/m17/qso.iq 30
```
