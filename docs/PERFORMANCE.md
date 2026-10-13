# Výkonnostní profil

Měřeno nástrojem `sx1255-bench` v release buildu (`-O2`, bez
`-march=native`). Cílem je ověřit, že diagnostika nezablokuje RX cestu.

## Metodika

```text
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/sx1255-bench
```

Profiluje tři hot-spoty:

- FFT 4096 s Hann oknem (spektrum),
- přidání řádku do waterfallu (2049 binů, 500 řádků),
- kompletní M17 RX řetězec (125 kSa/s → matched filter, 2 s IQ).

## Naměřené hodnoty

Referenční běh na Raspberry Pi 5 (BCM2712, 4× Cortex-A76, aarch64):

| Operace | Výsledek |
|---|---|
| FFT 4096 + Hann | ~0.4 ms/op, ~2800 op/s |
| Waterfall addRow | ~1.3 µs/řádek, ~780 000 řádků/s |
| M17 RX chain | ~34 ms na 2 s IQ → ~58× real-time |

## Interpretace

- Spectrum monitor dělá 5 FFT/s → ~2 ms CPU za sekundu, zanedbatelné.
- Waterfall 5 řádků/s → zanedbatelné.
- M17 RX běží ~58× rychleji než real-time na Pi 5, takže na Pi Zero 2 W
  (odhadem 4–6× pomalejší) stále zůstává zhruba 10–15× rezerva nad real-time.
- Úzké hrdlo je M17 RX řetězec, ne diagnostika. Přidání spektra/waterfallu/
  dashboardu nemění zátěž RX cesty o víc než jednotky procent.

## Měření na Raspberry Pi Zero 2 W

Spusť stejný příkaz přímo na cíli. Očekávané hodnoty jsou řádově:

| Operace | Pi Zero 2 W (odhad) |
|---|---|
| FFT 4096 | 2–3 ms/op |
| Waterfall addRow | ~5 µs/řádek |
| M17 RX chain | 10–15× real-time |

Pro přesná čísla:

```text
./build/sx1255-bench
```

## Dlouhodobá stabilita

Soak test (`sx1255-soak`) opakovaně generuje FM/D-Star/M17/šum a měří RSS,
CPU, alokace, relace a chyby. Po úvodním nárůstu musí být křivka RSS plochá:

```text
./build/sx1255-soak --config sx1255-multimode.yaml --duration 86400
```

Kritérium 7 dní: bez nárůstu RSS mezi 24h a 48h během, bez restartů procesu,
s aktivním spektrem a dashboardem.
