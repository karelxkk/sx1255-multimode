# M17 DM-1701 LNA dataset — 2026-10-19

Proběhly tři řízené skupiny po 10 klíčních. Síťové M17/DSRP cesty byly
během capture vypnuté, aby RF obsahovalo pouze lokální vysílání DM-1701.

| LNA | IQ záznam | M17 rámců | LSF OK | EOT | packet |
|---:|---|---:|---:|---:|---:|
| 42 dB | `lna42_clean_20261019_211507.iq` | 1634 | 267 | 43 | 16 |
| 36 dB | `lna36_clean_20261019_212138.iq` | 1736 | 287 | — | — |
| 24 dB | `lna24_clean_20261019_212728.iq` | 288 | 49 | — | — |

IQ soubory zůstávají na cíli v
`/home/xkk/sx1255-diagnostics/datasets/lna{42,36,24}/`.

## Klíčové pozorování

- LNA 36 dB bylo v offline auditu mírně lepší než 42 dB.
- Při 24 dB výrazně klesl počet zachycených rámců; výrazně ubylo
  použitelného signálu.
- Silné bursty měly SyncDistance typicky 2.6–3.1 a Viterbi metric 40–48.
- Při 42, 36 i 24 dB zůstávala špička silného RF burstu přibližně
  −23.6 dBFS, což ukazuje na saturaci společnou pro všechny tři LNA režimy
  nebo na omezení jiné části RX řetězce.

## Oprava offline auditu

Původní offline výsledky byly zkreslené: nástroje neaplikovaly 25kHz
low-IF NCO, který používá živý `RadioFrontend`. Po opravě offline chainu
se data z těchto capture dekódují. Živý dekodér touto chybou netrpěl.

## Závěr LNA

Snížení LNA nevedlo k jednoznačnému zlepšení. LNA 36 dB je použitelné,
24 dB už připravuje příliš mnoho rámců. Hlavní problém tedy není přebuzení
LNA.

## Test firmware gain_m12

OpenRTX v0.4.4 byl přebudován se sníženým digitálním vstupem HR_C6000
z `-6 dB` na `-12 dB`; výkon PA se nezměnil. Při LNA 42 dB:

- 4541 rámců, 1 M17 rámec, 0 LSF OK,
- medián symbolové úrovně klesl z **2.257** na **1.361**,
- medián Viterbi metricu se zhoršil z **45** na **57**.

Snížení úrovně C6000 tedy funguje, ale kvalitu CRC zhoršuje. Přebuzení
vstupu C6000 není příčinou. Firmware `-18 dB` nemá smysl zkoušet bez další
změny jiného parametru; baseline `-6 dB` je vhodnější pro další testy.

## Firmware preamble160

OpenRTX v0.4.4 s původním gainem `-6 dB` a preamble prodlouženou
z 80 na 160 ms:

- 6570 rámců,
- 1808 M17 rámců,
- 299 LSF OK,
- 82 EOT rámců.

Live frame log během závěrečné části záznamu potvrdil 501 M17 rámců
a 75 LSF OK. Preamble160 je z testovaných variant nejlepší a je vhodné
u něj zatím zůstat.
