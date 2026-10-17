# Troubleshooting

## Služba se nespustí

```text
sudo journalctl -u sx1255-multimode -n 100
```

Nejčastější příčiny:

- chybí `/dev/spidev0.0` — povol SPI v `raspi-config` a načti device tree,
- chybí `/dev/gpiochip0` — ověř `ls /dev/gpiochip*`,
- ALSA zařízení neexistuje — `aplay -l`, `arecord -l`,
- špatná cesta ke konfiguraci v unit souboru,
- `stats.json` nebo `*.iq` v nepřístupném adresáři.

## Frontend se restartuje v cyklu

Podívej se na `/api/health` a log:

```text
curl http://127.0.0.1:8080/api/health
sudo journalctl -u sx1255-multimode -f | grep HealthMonitor
```

Pokud se restartuje `frontend`, zkontroluj SPI/GPIO/ALSA. Pokud `spectrum`,
zvětš `spectrum.fft_size` není potřeba; spíš ověř CPU zátěž (`/api/system`).

## Vysoká zátěž CPU

```text
curl http://127.0.0.1:8080/api/system
top -H -p $(pidof sx1255-multimode)
```

Největší zátěž má obvykle spectrum FFT a M17 RX řetězec. Na Zero 2 W se drží
pod real-time (viz `docs/PERFORMANCE.md`).

## Spektrum/waterfall neběží

- `spectrum.enabled: true` v konfiguraci,
- zkontroluj `GET /api/spectrum` — pole `frame` se musí zvyšovat,
- při přetížení roste `dropped_samples`; to je neškodné (diagnostika zahazuje
  nejstarší nepoužité vzorky, RX cesta běží dál).

## Žádné detekce

- Ověř frekvence RX/TX a práh `frontend.squelch` v konfiguraci,
- `GET /api/status` ukazuje RSSI a čítače detekcí,
- nahraj provoz přes `--capture` a přehraj ho na PC, kde můžeš krok po kroku
  ladit bez rádia,
- spusť regresi datasetů: `./build/sx1255-multimode --dataset datasets`.

## Automatický capture se nespouští

- `automatic_capture.enabled: true`,
- trigger musí být povolený v `trigger:` sekci,
- adresář `directory` musí existovat a být zapisovatelný,
- zkontroluj `GET /api/iq` a `ls directory/capture_*.iq`.

## Únik paměti

Soak test změří špičku RSS a počet alokací:

```text
./build/sx1255-soak --config /etc/sx1255-multimode/sx1255-multimode.yaml \
  --duration 3600 --out soak_report.json
```

Stabilní proces má po úvodním startu zhruba plochou křivku RSS. Rostoucí
`peak_rss_kb` mezi dvěma soak běhy stejné délky indikuje únik.

## Proces spadl

systemd ho restartuje (`Restart=always`). Poslední IQ před pádem najdeš v
`automatic_capture.directory/capture_*.iq`, pokud je zapnutý `crash` trigger.
Analyzuj ho:

```text
./build/sx1255-multimode --replay directory/capture_*.iq
```
