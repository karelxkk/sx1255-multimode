# Changelog

Všechny podstatné změny projektu `sx1255-multimode` jsou zaznamenány zde.

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
