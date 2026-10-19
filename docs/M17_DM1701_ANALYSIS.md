# M17 / DM-1701 diagnostics

Automaticky generováno z offline analýz IQ. Detekční prahy, AFC
konstanty ani M17 DSP nebyly změněny.

## Stav datové sady

- záznamů: **16** (cíl 20-50)
- úspěšná detekce: **0**
- neúspěšná detekce: **16**
- accepted sync burstů: **0**
- rejected sync burstů: **404**

> Poznámka: existující capture z 14. 10. 2026 nemají původní metadata.
> Zdroj `DM-1701 / OpenRTX` je u nich odvozen z probíhající testovací
> session, nikoli z uloženého identifikátoru rádia. Nové záznamy přes
> `scripts/m17_dm1701_record.sh` mají metadata vytvořená přímo při capture.

## Přehled záznamů

| soubor | LSF | rámce | rejected | RSSI max | symbol level | AFC min/max |
|---|---:|---:|---:|---:|---:|---:|
| capture_20261014_005444.iq | 0 | 0 | 15 | -23.9 | 0.33 | -6259 / 10112 Hz |
| capture_20261014_005457.iq | 0 | 0 | 15 | -23.9 | 0.37 | -6259 / 10112 Hz |
| capture_20261014_005507.iq | 0 | 0 | 46 | -24.5 | 1.61 | -5781 / 2469 Hz |
| capture_20261014_005511.iq | 0 | 0 | 14 | -24.4 | 0.38 | -6563 / 5710 Hz |
| capture_20261014_005520.iq | 0 | 0 | 15 | -23.9 | 0.33 | -6259 / 10112 Hz |
| capture_20261014_005534.iq | 0 | 0 | 15 | -23.9 | 0.35 | -6259 / 10112 Hz |
| capture_20261014_005543.iq | 0 | 0 | 55 | -24.5 | 2.47 | -5328 / 11137 Hz |
| capture_20261014_005547.iq | 0 | 0 | 24 | -24.4 | 0.33 | -11702 / 9916 Hz |
| capture_20261014_005557.iq | 0 | 0 | 15 | -23.9 | 0.34 | -6259 / 10112 Hz |
| capture_20261014_005611.iq | 0 | 0 | 15 | -23.9 | 0.34 | -6259 / 10112 Hz |
| capture_20261014_005621.iq | 0 | 0 | 50 | -24.5 | 1.68 | -6382 / 1722 Hz |
| capture_20261014_005624.iq | 0 | 0 | 20 | -24.4 | 0.34 | -6563 / 5701 Hz |
| capture_20261014_005634.iq | 0 | 0 | 15 | -23.9 | 0.34 | -6259 / 10112 Hz |
| capture_20261014_005648.iq | 0 | 0 | 15 | -23.9 | 0.41 | -6259 / 10112 Hz |
| capture_20261014_005657.iq | 0 | 0 | 54 | -24.5 | 1.89 | -6105 / 1722 Hz |
| capture_20261014_005701.iq | 0 | 0 | 21 | -24.4 | 0.39 | -10051 / 5451 Hz |

## Histogram AFC korekcí

### Accepted / M17

_Žádná data._

### Rejected

```text
    <-1500 Hz    ################                         88
    <-1250 Hz    ######                                   35
     <-500 Hz    #                                        8
       500 Hz    #                                        1
       750 Hz    #                                        3
      1000 Hz    #                                        3
      1250 Hz    ########                                 46
      1500 Hz    ######################################## 220
```

## Histogram SyncDistance

### Accepted / M17

_Žádná data._

### Rejected

```text
      3.25       #                                        4
      3.50       #                                        2
      3.75       ####                                     23
      4.00       ####################                     109
      4.25       ######################################## 213
      6.75       ##                                       9
      7.00       ###                                      17
      7.25       #####                                    27
```

## Frame quality audit

Offline nástroj `m17-frame-audit` se správným low-IF NCO zpracoval všech
16 starších capture a našel **3401 rámců, žádný platný M17**:

- `M17`: **0/3401**,
- `Viterbi=OK`: většina rámců měla očekávanou délku výstupu,
- `CRC=FAIL`: všechny validované CRC,
- `LSF=FAIL`: všechny přímé LSF,
- medián Viterbi metric: **55**,
- medián Softbit RMS: **0.848**,
- medián SyncDistance: **4.356**,
- medián RSSI při syncu: **−58.2 dBFS**.

Historická datová sada tedy neobsahuje použitelný M17 stream, i po opravě
offline NCO.

Referenční srovnání na nejsilnějším capture (`005543`) dalo:

- softbit neshody: pouze **max. 1 LSB**, bez vlivu na Viterbi,
- Viterbi výstup: **55/55 shodný** s Go m17-gateway,
- Viterbi metric: **55/55 shodný**,
- Golay se u LSF neuplatňuje,
- CRC a LSF výsledek: **55/55 shodně FAIL**.

První skutečně selhávající kontrola je tedy **CRC**, ale již na vstupu
Viterbi jsou data poškozená; nová i původní implementace produkují stejný
Viterbi výstup. Nejde o chybu implementace C++ dekodéru.

Novější řízené capture s firmware `preamble160` naopak obsahuje
**1808 M17 rámců a 299 LSF OK**. Tím je potvrzeno, že DM-1701 je schopná
vyslat dekódovatelný M17 stream a problém starší datové sady nebyl
v dekodéru SX1255.

## Předběžné závěry

- AFC C++ implementace byla porovnána s původním Go `FMDemodAFC`;
  maximální rozdíl na reálném capture je **0.005 Hz**.
- Samotná AFC tedy nevysvětluje neúspěšnou detekci.
- Nejnižší SyncDistance rejected burstu je **3.33**.
- SyncDistance je pod prahem, ale LSF neprojde CRC. To ukazuje na poškození symbolů/payloadu, nízkou úroveň nebo chybu vysílače, nikoli na příliš přísný sync threshold.
- Část záznamů má `average_symbol_level < 1.0`; takový signál je příliš slabý nebo má malou deviaci a vzdálenosti jsou nafouknuté.

## Doporučené další kroky

1. Doplnit alespoň 20 nových záznamů z DM-1701 s ověřenou úrovní,
   vzdáleností a nastavením OpenRTX.
2. U každého záznamu porovnat validní LSF, BER a `average_symbol_level`.
3. Teprve po dosažení datové sady rozhodnout, zda upravit AFC restart,
   RSSI gate nebo sync threshold.

## Nástroje

```bash
scripts/m17_afc_audit.sh INPUT.iq afc_audit.json 100
./build/m17_iq_audit INPUT.iq --output analysis.json
scripts/m17_dataset_report.py \
  --analysis-dir datasets/m17/afc_audit/analysis \
  --dataset-dir datasets/m17/dm1701 \
  --output docs/M17_DM1701_ANALYSIS.md
```
