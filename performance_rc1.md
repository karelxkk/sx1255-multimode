# Performance RC1

> Měřeno na **Raspberry Pi 500** (BCM2712, 4× Cortex-A76, aarch64) — toto
> zařízení je v workspace k dispozici. Měření na **Raspberry Pi Zero 2 W** je
> **PENDING** (musí běžet na cíli).

## Naměřeno (`sx1255-bench`, release build)

| operace | hodnota |
|---|---|
| FFT 4096 + Hann | ~0.4 ms/op, ~2800 op/s |
| Waterfall addRow (2049 binů) | ~1.4 µs/řádek, ~720 000 řádků/s |
| M17 RX chain (125 kSa/s → MF) | ~35 ms na 2 s IQ → **~58× real-time** |

## Očekávání na Pi Zero 2 W

Pi Zero 2 W (4× Cortex-A53) je zhruba 4–6× pomalejší než Pi 500:

| operace | Pi Zero 2 W (odhad) |
|---|---|
| FFT 4096 | 2–3 ms/op |
| Waterfall addRow | ~5–8 µs/řádek |
| M17 RX chain | 10–15× real-time |

Pro přesná čísla spusť na cíli:

```text
./build/sx1255-bench
```

nebo celou validaci:

```text
scripts/rc1_validate.sh
```

## Interpretace pro kritérium úspěchu

- Spectrum monitor dělá 5 FFT/s → i na Zero 2 W ~10–15 ms CPU/s, zanedbatelné.
- Waterfall 5 řádků/s → zanedbatelné.
- Dashboard přidává jen periodické HTTP odpovědi, nezatěžuje RX vlákno.
- Úzké hrdlo je M17 RX; pokud zůstává >1× real-time, systém nepadá do
  přetečení bufferu.

## Stav

- [x] měřeno na dostupném aarch64 zařízení (Pi 500)
- [ ] měřeno na Raspberry Pi Zero 2 W (PENDING)
