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
| FM (NFM) | plný RX→TX převaděč (bridge) | Goertzel CTCSS + hang timer + regenerace CTCSS na TX |
| D-Star | detekční plugin | GMSK + frame sync + validace FEC hlavičky (CRC) |
| M17 | 1:1 port z m17-gateway | 4FSK demod + sync + LSF/stream/packet dekódování |

Plánované: DMR, C4FM, POCSAG, APRS, AX.25, FreeDV, Codec2 experimenty.

## Požadavky na build

- CMake >= 3.20
- GCC 12+ / Clang 15+ (C++20)
- Linux (Raspberry Pi OS, arm64)

Žádné externí knihovny. YAML je parsován vestavěným minimalistickým parserem
(podmnožina YAML), ALSA/SPI vrstva přijde později přes `ISX1255` rozhraní.

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
GET /api/iq         stav IQ záznamu/přehrávání
GET /api/health     stav subsystémů (watchdog)
GET /api/system     paměť, CPU, vlákna, stav frontendu
```

## Nástroje

```
sx1255-multimode   démon
sx1255-soak        soak test (24h/48h/7d, soak_report.json)
sx1255-bench       výkonnostní profil (FFT, waterfall, RX chain)
```

Deb balíček pro nasazení:

```text
scripts/build_deb.sh        # -> sx1255-multimode_1.0.0-rc1_arm64.deb
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
- [Session Manager](docs/session-manager.md)
- [M17 migrace z m17-gateway](docs/M17_MIGRATION.md)
- [M17 porting report](docs/M17_PORTING_REPORT.md)
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
