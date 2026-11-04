# RC1 Soak Report

> Stav: **PODMÍNEČNÝ — první běh dokončen, čistý PASS ještě není potvrzen.**
> Monitor běžel přesně 168 hodin, ale zaznamenal 46 změn PID služby
> `sx1255-multimode`. Podle potvrzení operátora byly tyto restarty způsobené
> testováním, nikoli samovolným pádem aplikace. Monitor sám plánovaný restart
> od pádu nerozliší, proto tento běh nelze použít jako důkaz sedmidenní
> bezrestartové stability.

## První běh

| položka | hodnota |
|---|---|
| start | 2026-10-24 00:24:03 +02:00 |
| konec | 2026-10-30 23:24:16 +01:00 |
| skutečné trvání | 168,00 h |
| vzorků po 30 s | 20 069 |
| změny PID aplikace | 46 |
| změny boot ID | 0 |
| nejdelší souvislý běh PID | 94,55 h (2026-10-26 15:41–2026-10-30 14:14) |
| restarty health komponent | 0 |
| API timeout vzorky | 14 z 20 069 (0,07 %) |
| max. RSS | 79 804 kB |
| RSS první / poslední | 57 132 / 79 024 kB |
| max. vláken | 17 |
| max. teplota | 48,85 °C |
| session FM / D-Star / M17 | 81 / 0 / 156 |

Restarty aplikace se objevily v několika shlucích, především 24. 10. dopoledne
a večer, 26. 10. odpoledne a 30. 10. odpoledne až večer. V těchto obdobích
monitor viděl i krátké přechody přes `MainPID=0` a `ActiveState=failed`, což
odpovídá testovacím stop/start zásahům. Bez operátorského markeru je nelze
automaticky odlišit od skutečného selhání.

## Vyhodnocení

- [x] monitor dokončil celé 168hodinové okno
- [ ] 7 dní bez restartu aplikace — 46 změn PID, podle operátora testovacích
- [x] bez rebootu operačního systému — 0 změn boot ID
- [x] bez restartu health komponent — 0 pro frontend, dispatcher, session
  manager, HTTP i spectrum
- [!] trend RSS nelze z tohoto běhu hodnotit jako čistý; data jsou přerušená
  testovacími restarty a změnami buildu
- [ ] **PASS** — chybí čistý běh bez plánovaných restartů

Závěr: první běh potvrdil, že monitor a služba vydrží celý týden provozu, ale
nejde z něj uzavřít, že aplikace běžela celých sedm dní bez restartu. Nejdelší
souvislý běh byl 94,55 h.

## Navazující čistý běh

Předchozí navazující běh byl zneplatněn nasazením opravy D-Star. Nový čistý
běh byl spuštěn 2026-11-04 18:29:10 +01:00 a končí
2026-11-11 18:29:10 +01:00. V okamžiku startu měla služba
`sx1255-multimode` aktivní opravenou D-Star detekci, `NRestarts=0` a proběhly
tři úspěšné lokální D-Star relace. Během tohoto okna se nesmí služba ani
Raspberry Pi záměrně restartovat; jinak bude nutné test znovu spustit od
začátku.

## Co je ověřeno bez hardwaru (v tomto workspace)

- build v1.0.0-rc1 bez varování
- 23/23 unit testů PASS (2 HTTP testy jsou v sandboxu SKIP, mimo sandbox PASS)
- `sx1255-bench` na Raspberry Pi 500: FFT 4096 ~0,4 ms, waterfall ~1,4 µs/řádek,
  M17 RX ~58× real-time
- false positive audit na syntetických FM/DStar/M17/unknown datasetech: 0 FP
- failure injection unit test (přetečení capture bufferu, prázdný IQ blok): PASS
