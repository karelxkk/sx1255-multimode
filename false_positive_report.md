# False Positive Audit

> Stav: syntetická verifikace **PASS (0 FP)**; audit na reálném provozu
> **PENDING** (vyžaduje nasbírané reálné datasety).

Audit záměn módů spouští každý dataset proti **všem** detektorům (FM, D-Star,
M17), ne jen proti vítězi arbitráže:

```text
./build/sx1255-fp-audit --dataset datasets --out false_positive_report.md
```

## Syntetické datasety (generované v CI)

| soubor | očekáván | FM | D-Star | M17 | výsledek |
|---|---|---|---|---|---|
| datasets/dstar | dstar | ne | ano | ne | ok |
| datasets/fm | fm | ano | ne | ne | ok |
| datasets/m17 | m17 | ne | ne | ano | ok |
| datasets/unknown | unknown | ne | ne | ne | ok |

## Shrnutí

- datasetů: 4 (syntetické)
- false positives: **0**
- cíl: 0

## Reálný provoz

Po nasbírání reálných záznamů (`scripts/record_dataset.sh`) se tabulka
automaticky přepíše spuštěním `sx1255-fp-audit`.

Kontrolované záměny:

- FM jako D-Star
- FM jako M17
- D-Star jako M17
- M17 jako D-Star

Výsledek: **PASS (0 false positives) na syntetických datech; reálná data
PENDING.**
