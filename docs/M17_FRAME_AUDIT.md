# M17 frame quality audit

Diagnostika rozlišuje skutečné vrstvy příjmu bez změny AFC, synchronizace,
RRC, Viterbiho ani Golay algoritmu.

## Význam čítačů

| Čítač | Význam |
|---|---|
| `sync_detected` | každý sync burst předaný do `M17FrameDecoder` |
| `lsf_ok` / `lsf_fail` | LSF z přímého LSF syncu nebo rekonstruovaný z LICH |
| `crc_ok` / `crc_fail` | LSF CRC a CRC složené packet zprávy |
| `golay_ok` / `golay_fail` | jednotlivé Golay(24,12) bloky LICH (4 na stream rámec) |
| `viterbi_ok` / `viterbi_fail` | Viterbi doběhl a vrátil očekávanou délku výstupu |
| `frame_ok` / `frame_fail` | výsledek zpracování celého sync rámce |

Důležité: LSF cesta je **Viterbi → CRC**, Golay se jí netýká. Golay patří
do LICH u stream rámců. Packet CRC vzniká až po složení packet frame.

Čítače jsou v `/api/plugins` a `/api/m17/diagnostics`. Reset:

```bash
curl -X POST http://127.0.0.1:8080/api/m17/diagnostics/reset
```

## Logy a artefakty

`m17_frame_audit.log` obsahuje pro každý sync:

```text
timestamp RSSI AFC SyncDistance SoftbitRMS BER ViterbiMetric Viterbi
Golay CRC LSF Frame Type Result
```

Log se rotuje po 1 MB, tři zálohy.

Dashboard zobrazuje `BER last / avg [%]`. `last` je BER posledního
dekódovaného rámce, `avg` je průměr z posledních 100 rámců. Pro LSF
a packet se počítá z 368 kanálových bitů, pro stream z 272 bitů.

Na neúspěšných framech se do `audit_directory` ukládá
`softbit_stats.json` s histogramem 16 binů, saturací a podbuzením.
Je-li `CRC=FAIL`, `SyncDistance < 4.0` a RSSI je nad diagnostickým
prahem (`near_miss_rssi_dbfs`, aby se nezahlcovaly false syncy ze šumu),
ukládá se také JSON artefakt rámce s raw softbity, deinterleaved bity
a Viterbi výstupem a současně se vyžádá IQ capture.

## Offline nástroje

```bash
m17-frame-audit --dataset datasets/m17/dm1701 \
  --output dm1701_audit.json

scripts/m17_reference_compare.sh INPUT.iq reference_compare.json
```

`m17-frame-audit` vypíše AFC, RSSI, SyncDistance, Softbit RMS, Viterbi
metric/status, Golay, CRC a LSF pro každý rámec. Referenční skript
porovná softbity, deinterleave, Viterbi výstup, Golay a CRC s původním
Go `m17-gateway` a vygeneruje
[M17_REFERENCE_COMPARISON.md](M17_REFERENCE_COMPARISON.md).

Offline nástroje aplikují stejný 25kHz low-IF NCO jako živý
`RadioFrontend`. Bez něj se výsledky na SX1255 IQ záznamech neshodují
s live dekodérem.
