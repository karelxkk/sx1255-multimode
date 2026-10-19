# Changelog

Všechny podstatné změny projektu `sx1255-multimode` jsou zaznamenány zde.

## [Nepublikováno]

### Přidáno

- M17 diagnostics: `/api/plugins` a `/api/m17/diagnostics` nyní zobrazují
  AFC korekci, SyncDistance, sílu symbolů, RSSI při syncu a historii
  posledních 100 kandidátů. Dashboard má samostatný panel M17 Diagnostics.
- Volitelný rotovaný `m17_candidates.log` s RSSI, AFC, SyncDistance a
  výsledkem každého zachyceného M17 pokusu.
- Trigger `m17_near_miss` pro automatický IQ capture při SyncDistance
  4.5-7.0 bez validního LSF/rámce.
- Offline nástroje `m17_iq_audit`, `m17_afc_audit` a `m17_dataset_report.py`;
  AFC audit porovnává C++ trasu s původním Go `m17-gateway`.
- Dataset `datasets/m17/dm1701/` a skript pro sběr reálných vysílání
  z Baofeng DM-1701 / OpenRTX.
- Analýza `docs/M17_DM1701_ANALYSIS.md` s histogramy AFC a SyncDistance.

- M17 packet mode TX (`generatePacketSymbols`) a přenos packet datagramů
  přes `M17ReflectorClient::sendPacket`.
- M17 plugin přeposílá packet rámce z RF do M17 inet klienta a naopak je
  vysílá na RF; tím funguje bridge na `m17-bridge` APRS modul pro SMS zprávy
  a GNSS polohu.
- M17 plugin má druhé nezávislé inet připojení `m17.bridge`, takže hlasový
  `m17.reflector` (mrefd) a APRS/messaging `m17-bridge` mohou běžet současně.
- Samostatný APRS balíček `m17-bridge` (`deploy/m17-bridge/`) včetně systemd
  unit a konfigurace; výchozí port je 17001, aby nekolidoval s mrefd na 17000.
- Unit test `m17_packet_loopback` (packet TX → RX dekóduje stejný payload).

### Opraveno

- M17 packet decoder: první (a případně jediný) packet frame po LSF se
  špatně skládal kvůli přetečení `uint8_t` při výpočtu offsetu; nyní se
  `0xff + 1` správně zabalí na 0.
- FM/SVXLink CTCSS na TX: v backendu `svxlink` se `ctcss_tx` z multimode
  nepoužívá. Šablona SVXLinku teď zapíná `TX_CTCSS=ALWAYS` v
  `[RepeaterLogic]`/`[ReflectorLogic]` a nastavuje `CTCSS_FQ=88.5` a
  `CTCSS_LEVEL=-24` v `[Tx1]`.
- `ctcss_tx` je v konfiguraci zpět a je vysvětlené, že se použije jen pro
  `backend: local`; v backendu `svxlink` tón generuje SVXLink.
- Ukázková i cílová konfigurace jsou komentované po jednotlivých sekcích a
  klíčích.
- RSSI na SX1255 je relativní (počítané z I/Q výkonu); naměřené minimum je
  cca -60 a zvýšení LNA na 42 dB ho nezměnilo. RSSI squelch zatím není
  implementovaný, takže se nesmyslný `squelch` klíč přestal uvádět v ukázkové
  konfiguraci (parser ho kvůli zpětné kompatibilitě pořád přijme).
- FM/SVXLink TX: `tx_dev_hz` se už skutečně uplatní jako referenční deviation;
  2500 Hz zachovává původní NFM 12.5 kHz ladění, 5000 Hz zdvojnásobí deviation
  pro WFM 25 kHz. `tx_gain` zůstává jemná korekce.

## [1.0.0-rc4] — 2026-10-16

### Přidáno

- D-Star lokální RX→TX převaděč. Po validní FEC hlavičce je relace přidělena
  D-Star pluginu, který re-moduluje přijatou hlavičku (RPT1/RPT2 nahradí
  vlastním callsignem), přeposílá dekódované DV rámce a na konci vysílá
  D-Star end-of-transmission sekvenci.
- Nová D-Star TX DSP vrstva (`dstar::DStarTXModulator`): GMSK Gaussian
  shaping BT=0.5, resampling 48 kSa/s → 125 kSa/s a FM modulace s vrcholovou
  odchylkou 1200 Hz.
- Konfigurace `modes.dstar.callsign`, `tx_preamble_bytes` a `tx_tail_ms`.
- Unit testy `dstar_loopback` (TX IQ → RX detektor) a `dstar_plugin`
  (plugin + falešný frontend, PTT a dojezd).
- D-Star DSRP gateway (`dstar::DStarGatewayClient`) pro ircDDBGateway /
  DStarGateway: síť → RF i RF → síť, keepalive registrace, watchdog a
  telemetrie. Konfigurace `modes.dstar.gateway` a `network.mode`.
- Unit testy `dstar_gateway` a `dstar_netplugin` (loopback UDP, gateway TX
  cesta přes falešný frontend).
- D-Star hotspot režim pro DCS/XLX reflektory: konfigurace
  `modes.dstar.gateway`, volba skupiny přes `reflector=DCS019 B` a statická
  poloha (`latitude`/`longitude`, `positionSource=fixed`).
- Dashboard: `confidence` a `sync_confidence` se zobrazují pevným formátem
  (6 desetinných míst), aby layout neposkakoval při exponenciálních hodnotách.
- Dokumentace: odkazy na stažení kernelu a overlaye z GitHub release jsou
  přímo v README a v `docs/DEPLOYMENT.md`.

## [1.0.0-rc3] — 2026-10-14

### Opraveno

- FM/SVXLink TX: při přepínání RX↔TX už do I2S netečou žádná data. Při
  klíčování se ALSA playback zavře a znovu otevře prázdný ještě před zapnutím
  TX hodin, PLL se nechá zamknout a teprve pak se předplní; při odklíčení se
  stream nejdřív zastaví a teprve pak se vypne TX. PTT přepíná jen TX vlákno
  (mezi zápisy) a arbiter se uvolní až po fyzickém vypnutí TX, takže M17/D-Star
  nemůžou klíčovat do streamu, který se právě bourá.

## [1.0.0-rc2] — 2026-10-13

### Opraveno

- Digitální šum na začátku vysílání/dojezdu: playback ring byl při klíčování
  prázdný a než přišel první IQ blok, I2S ho vyprázdnil (XRUN), což se na
  vzduchu projevilo jako pravidelný digitální šum. `setPtt(true)` teď před
  zapnutím TX předplní ring ~65 ms tiché nosné.
- Klíčování během dojezdu: když SVXLink drží TX (`ACTIVE_SVXLINK`, přehrává
  dojezd), místní stanice teď může klíčovat. SQL se nově řídí místní nosnou i
  mimo aktivní FM session (v `detect()`), takže se otevře okamžitě, jakmile
  místní stanice zaklíčuje, a SVXLink dojezd přeruší.
- FM TX přestal vysílat (`SX1255 playback write failed: File descriptor in bad
  state (-77)`): `drainTx` uvedl playback do SETUP a souběžný zápis TX vlákna
  skončil chybou, kterou `writeIQ` neznal. TX vlákno se teď zastaví před
  unkey a `writeIQ` se umí zotavit z `-EPIPE`/`-ESTRPIPE`/`-EBADFD`/`-ENODEV`
  (prepare + omezený retry), takže jediný glitch nezabije FM vysílač.
- RX cesta (RF -> SVXLink) byla mrtvá, kdykoli arbiter držel TX bez plugin
  session (`ACTIVE_SVXLINK` / `ACTIVE_M17_NETWORK`): dispatcher zahodil celý IQ
  blok a `owner` byl prázdný. FM plugin teď krmí RX do SVXLink bridge průběžně
  i v těchto stavech, takže místní RF signál jde do SVXLinku vždy.
- FM/SVXLink převaděč: ALSA playback na SX1255 podtékal (XRUN) na startu relace
  i během dojezdu. DAC pak opakoval stará data, což na vzduchu znělo jako
  pravidelný digitální šum. Bridge nyní drží TX stream živý tichou nosnou, když
  SVXLink nemá připravené audio, a před vypnutím PTT vyprázdní buffer
  (`drainTx`).
- FM squelch se zavíral po jediném slabém bloku Goertzela, což sekalo audio;
  nyní vyžaduje několik potvrzených bloků plus krátkou prodlevu.
- FM příjem: horní propust audio (2,6 kHz) a rozladěná pre/de-emfáze
  (100 µs RX vs 75 µs TX) zkreslovaly zvuk.

### Změněno

- Volba síťového zdroje pro směr síť -> RF podle naposledy použitého lokálního
  módu: default je FM/SVXLink (M17 a D-Star ze sítě blokované); po lokálním
  klíčování se zdroj přepne na odpovídající mód (M17 -> M17, FM -> SVXLink,
  D-Star -> D-Star).  Arbiter povolí TX ze sítě jen tomu zdroji, který odpovídá
  naposledy použitému lokálnímu módu.  Preferovaný síťový mód je vidět v
  `/api/status` (`network_mode`) a na webovém dashboardu.
- FM zvukové tvarování je nově konfigurovatelné v YAML
  (`rx_deemph_us`, `tx_preemph_us`, `rx_audio_lpf_hz`, `tx_audio_lpf_hz`).
  Defaulty jsou laděné pro NFM 12,5 kHz: 75 µs pre/de-emfáze a 3 kHz audio
  low-pass. Pro široké FM (25 kHz) zvedni audio filtry na ~4–5 kHz.

## [1.0.0-rc1] — 2026-10-11

První release candidate. Obsahuje kompletní multimódový framework a produkční
robustnost, ale čeká na 7denní validaci na reálném SX1255 hardwaru.

### Přidáno

- SX1255 frontend (SPI + libgpiod + ALSA, 125 kSa/s duplex).
- Plugin systém s arbitráží M17 > D-Star > FM a exkluzivním vlastnictvím relace.
- M17: 1:1 port DSP z m17-gateway (DC removal, decimace, FM demod, AFC,
  resampler, RRC, synchronizace, Viterbi, Golay, LSF/stream/packet).
- D-Star: GMSK detekce, frame sync a validace FEC hlavičky (CRC).
- FM: NFM diskriminátor a CTCSS detekce (Goertzel).
- IQ capture/replay (endian-safe binární formát) a automatický IQ capture
  s pre/post trigger okny.
- Spectrum monitor (FFT/PSD/peak-hold/noise floor) a waterfall.
- Web dashboard bez frameworků (`/`, `/api/status`, `/api/plugins`,
  `/api/iq`, `/api/spectrum`, `/api/waterfall`, `/api/health`, `/api/system`).
- Watchdog (HealthMonitor) s restartem subsystémů a IQ dumpem při poruše.
- Logování (DEBUG/INFO/WARN/ERROR, konzole + rotující soubory, `--log-level`).
- Persistované statistiky (`stats.json`).
- Soak tester (`sx1255-soak`), benchmark (`sx1255-bench`), false positive audit
  (`sx1255-fp-audit`), dataset replay (`--dataset`).
- systemd služba s auto-startem a auto-restartem.
- Dokumentace: architektura, nasazení, služba, troubleshooting, výkon.

### Změněno

- Verze CMake projektu nastavena na 1.0.0.

### Známá omezení

- Real-hardware validace (7 dní, reálný provoz, reálné poruchy) je PENDING —
  viz `RELEASE_CHECKLIST.md` a `rc1-soak-report.md`.
