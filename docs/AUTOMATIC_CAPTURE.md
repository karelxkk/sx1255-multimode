# Automatický IQ capture

Automatické ukládání IQ záznamů při podezřelých událostech. Po triggeru se
uloží posledních N sekund IQ (pre-trigger) a dalších N sekund (post-trigger)
do souboru `capture_YYYYMMDD_HHMMSS.iq`.

## Motivace

Při provozu se občas objeví jev, který se obtížně reprodukuje ručně:

- falešná detekce pluginu,
- signál, který žádný plugin nerozpoznal,
- pád procesu.

Díky kruhovému bufferu v paměti je pre-historie k dispozici okamžitě, takže
událost nemusí být předem naplánovaná.

## Trigger události

| Trigger | Zdroje |
|---|---|
| `false_positive` | pozitivní detekce v okamžiku, kdy už jiný plugin vlastní kanál |
| `unknown_signal` | RSSI nad -90 dBm po dobu 2 s, ale žádný plugin nezískal relaci |
| `crash` | výjimka zachycená v hlavní smyčce (best-effort, uloží pre-trigger) |
| `m17_near_miss` | RSSI nad prahem a SyncDistance 4.5-7.0, ale LSF/rámec neprošel validací |

Pro `crash` se post-trigger nečeká (proces končí), uloží se dostupná
pre-historie.

## Konfigurace

```yaml
automatic_capture:
  enabled: true
  directory: /var/lib/sx1255-multimode
  pre_seconds: 10
  post_seconds: 10
  trigger:
    false_positive: true
    unknown_signal: true
    crash: true
    m17_near_miss: true
```

Meze near-miss triggeru se nastavují v `m17.diagnostics`:

```yaml
modes:
  m17:
    diagnostics:
      near_miss_min_distance: 4.5
      near_miss_max_distance: 7.0
      near_miss_rssi_dbfs: -55.0
      near_miss_interval_ms: 100
```

Výsledek: `capture_YYYYMMDD_HHMMSS.iq` v zadaném adresáři, kompatibilní s
`IQReplay` (viz [IQ Capture & Replay](IQ_CAPTURE.md)).

## Implementace

- Kruhový buffer `pre + post + okraj` vzorků, velikost zarovnaná na mocninu
  dvou.
- Producent (`IQDispatcher` sink) jen krátce zapíše blok vzorků; veškeré I/O
  dělá samostatné writer vlákno.
- Po triggeru writer počká na dokončení post-okna a pak v jednom zámku
  zkopíruje souvislé okno `[trigger - pre, trigger + post)` v chronologickém
  pořadí.

## Ověření

`test_capture_trigger` ověřuje shodu pre+post vzorků a integritu hlavičky.
Výsledný soubor lze okamžitě přehrát přes `frontend.type: replay` a
analyzovat v dashboardu.
