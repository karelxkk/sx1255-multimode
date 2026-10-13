# RC1 Soak Report

> Stav: **PENDING** — vyžaduje 7denní běh na reálném hardwaru
> (Raspberry Pi Zero 2 W + SX1255 HAT + antény + reálný provoz).

Tento report nelze vyplnit mimo cílové zařízení. Spusť na Pi Zero 2 W:

```text
scripts/rc1_validate.sh 604800   # 7 dní
```

Skript průběžně měří CPU/RAM, restarty subsystémů, watchdog události,
detekce módů a IQ dumpy, a výsledky zapíše sem.

## Sledované položky

- [ ] CPU (využití procesoru, špičky)
- [ ] RAM (RSS, peak RSS, trend — nesmí růst)
- [ ] restarty subsystémů (HealthMonitor `restarts`)
- [ ] watchdog události (log `HealthMonitor`)
- [ ] detekce módů (FM / D-Star / M17, false positives)
- [ ] zachycené IQ dumpy (`automatic_capture.directory/capture_*.iq`)

## Naměřené hodnoty (na cíli)

| metrika | začátek | konec | poznámka |
|---|---|---|---|
| uptime | | | |
| RSS (kB) | | | plochý trend = bez úniku |
| CPU % | | | |
| restarty subsystémů | | | cíl 0 |
| watchdog událostí | | | |
| relací FM/DStar/M17 | | | |
| IQ dumpů | | | |

## Závěr

- [ ] 7 dní bez pádu procesu
- [ ] bez úniku paměti
- [ ] bez restartu subsystémů
- [ ] spectrum monitor + dashboard aktivní po celou dobu
- [ ] **PASS** / **FAIL**

## Co je ověřeno bez hardwaru (v tomto workspace)

- build v1.0.0-rc1 bez varování
- 23/23 unit testů PASS (2 HTTP testy jsou v sandboxu SKIP, mimo sandbox PASS)
- `sx1255-bench` na Raspberry Pi 500: FFT 4096 ~0.4 ms, waterfall ~1.4 µs/řádek,
  M17 RX ~58× real-time
- false positive audit na syntetických FM/DStar/M17/unknown datasetech: 0 FP
- failure injection unit test (přetečení capture bufferu, prázdný IQ blok): PASS
