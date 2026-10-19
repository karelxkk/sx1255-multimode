# M17 DM-1701 LNA dataset — 2026-10-19

Proběhly tři řízené skupiny po 10 klíčních. Síťové M17/DSRP cesty byly
během capture vypnuté, aby RF obsahovalo pouze lokální vysílání DM-1701.

| LNA | IQ záznam | sync rámců | CRC OK rámců | silné klíče |
|---:|---|---:|---:|---:|
| 42 dB | `lna42_clean_20261019_211507.iq` | 2394 | 1 | 6/10 |
| 36 dB | `lna36_clean_20261019_212138.iq` | 1865 | 0 | 9/10 |
| 24 dB | `lna24_clean_20261019_212728.iq` | 316 | 0 | 2/10 |

IQ soubory zůstávají na cíli v
`/home/xkk/sx1255-diagnostics/datasets/lna{42,36,24}/`.

## Klíčové pozorování

- Snížení LNA na 36 dB počet nalezených sync burstů nesnížilo, ale CRC
  zůstal 0/1865.
- Při 24 dB výrazně klesl počet silných slotů; úspěšnost zůstala nulová.
- Silné bursty měly SyncDistance typicky 2.6–3.1 a Viterbi metric 40–48.
- Jediný CRC OK rámec při 42 dB měl nesmyslný LSF
  `F/.Z52NNR>B1HR30YVL`; jde pravděpodobně o náhodný CRC-pass false
  positive, nikoli o použitelný stream OK1LBC.
- Při 42, 36 i 24 dB zůstávala špička silného RF burstu přibližně
  −23.6 dBFS, což ukazuje na saturaci společnou pro všechny tři LNA režimy
  nebo na omezení jiné části RX řetězce.

## Závěr

Snižování LNA samo o sobě problém nevyřešilo. Data nepodporují teorii, že
hlavní příčinou je přebuzení LNA. Další smysluplný krok je ověřit
modulační výstup OpenRTX/DM-1701 proti referenčnímu M17 vysílači nebo
snížit výkon stanice / zvětšit odstup od antény a zopakovat stejnou
sekvenci.

## Test firmware gain_m12

OpenRTX v0.4.4 byl přebudován se sníženým digitálním vstupem HR_C6000
z `-6 dB` na `-12 dB`; výkon PA se nezměnil. Při LNA 42 dB:

- 490 sync rámců, z toho 326 silných,
- 0 CRC OK,
- medián symbolové úrovně klesl z **2.257** na **1.361**,
- medián Viterbi metricu se zhoršil z **45** na **57**.

Snížení úrovně C6000 tedy funguje, ale kvalitu CRC zhoršuje. Přebuzení
vstupu C6000 není příčinou. Firmware `-18 dB` nemá smysl zkoušet bez další
změny jiného parametru; baseline `-6 dB` je vhodnější pro další testy.
