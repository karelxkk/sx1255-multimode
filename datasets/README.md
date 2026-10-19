# IQ dataset workflow

Adresáře pro reálné IQ záznamy používané regresními testy:

```
datasets/
  fm/       FM + CTCSS záznamy
  dstar/    D-Star záznamy
  m17/      M17 záznamy
    dm1701/ reálná měření DM-1701 / OpenRTX + metadata AFC/SyncDistance
  unknown/  šum / neznámé signály
```

Každý soubor `*.iq` se přehraje celým řetězcem
`IQReplay -> RadioFrontend -> IQDispatcher -> pluginy -> SessionManager`.
Očekávaný režim se odvodí z názvu nadřazeného adresáře a výsledek se zapíše do
`regression_report.json`.

Ke každému záznamu patří `metadata.json` (schéma v
`metadata.schema.json`): datum, čas, RX frekvence, RSSI a detekovaný mód.

Záznam vytvoříš na SX1255:

```text
scripts/record_dataset.sh m17 datasets/m17/qso.iq 30
```

Skript spustí démona s `--capture`, po ukončení přečte `/api/status` a vygeneruje
`metadata.json` vedle `.iq` souboru.

Přehrání jednoho záznamu bez rádia:

```text
./build/sx1255-multimode --replay datasets/m17/qso.iq
```

Automatický replay všech datasetů:

```text
./build/sx1255-multimode --dataset datasets
```
