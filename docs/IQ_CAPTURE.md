# IQ Capture & Replay

Deterministický framework pro záznam a přehrávání surových IQ dat ze SX1255.
Slouží k ladění DSP, regresním testům, vývoji pluginů a reprodukci chyb bez
připojeného rádia.

## Architektura

```text
SX1255Hardware ─┐
                ├─> IQRecorder ──> capture.iq
                │
                └─> RadioFrontend ──> IQDispatcher ──> pluginy

capture.iq ──> IQReplay ──> RadioFrontend ──> IQDispatcher ──> pluginy
```

`IQRecorder` a `IQReplay` jsou obě implementace `ISX1255`. Režim frontendu se
tedy volí **výměnou zařízení**, nikoliv změnou `RadioFrontend`, `IQDispatcher`
ani pluginů:

- `MODE_HARDWARE` — `frontend.type: sx1255` (volitelně s `recording.enabled`).
- `MODE_REPLAY` — `frontend.type: replay` (nebo `replay.enabled: true`),
  data se čtou pouze z `replay.file`, žádný SX1255 hardware není potřeba.

Rozhraní `ISX1255` má novou metodu `eof()`; `IQReplay` ji po vyčerpání souboru
vrátí `true` a `RadioFrontend` čistě ukončí capture smyčku. Živý hardware a
`NullSX1255` vrací `false`, takže jejich smyčka běží dál.

## Formát souboru

Jednoduchý binární, streamovatelný a endian-safe kontejner:

```text
[64B header][float32 I][float32 Q][float32 I][float32 Q] ...
```

Header (vše little-endian, bez ohledu na endianitu hostitele):

| offset | pole | velikost | popis |
|---|---|---|---|
| 0 | `magic` | 8 B | `"SX1255IQ"` |
| 8 | `version` | 4 B | `1` |
| 12 | `header_size` | 4 B | `64` (rozšiřitelnost) |
| 16 | `flags` | 4 B | rezervováno |
| 20 | `sample_rate` | 4 B | Hz (125000) |
| 24 | `frequency` | 4 B | RX frekvence Hz, 0 = neznámá |
| 28 | `timestamp` | 8 B | Unix sekundy |
| 36 | `sample_count` | 8 B | celkový počet vzorků (0 při streamování) |
| 44 | `reserved` | 20 B | budoucí rozšíření |

Při záznamu se `sample_count` píše jako 0 a po `close()` se zpětně doplní na
offset 36. Čtečka si poradí i se souborem, kde je 0 — dopočítá délku z velikosti
souboru.

## Konfigurace

```yaml
recording:
  enabled: true
  file: capture.iq

replay:
  enabled: false
  file: capture.iq
```

Režim se volí v `frontend.type`:

```yaml
frontend:
  type: replay        # null | sx1255 | replay
```

`recording.enabled: true` obalí reálný `SX1255Hardware` zapisovačem, takže
všechny vzorky, které projdou dispatcherem, skončí 1:1 v souboru. Replay a
recording se navzájem vylučují; replay má přednost.

## Workflow vývoje

1. Zachyť reálný provoz na SX1255:

   ```yaml
   frontend:
     type: sx1255
   recording:
     enabled: true
     file: capture.iq
   ```

2. Zkopíruj záznam na vývojový stroj a spusť bez rádia:

   ```yaml
   frontend:
     type: replay
   replay:
     file: capture.iq
   ```

3. Iteruj na DSP/pluginu; detekce běží nad identickými vzorky, takže výsledky
   jsou reprodukovatelné.

## Workflow ladění

Stejný záznam lze pustit pod debuggerem, s profilerem nebo opakovaně s
upravenými konstantami. Protože replay nezávisí na wall-clock čase, výsledky
detekce se nemění mezi běhy.

## Workflow reprodukce chyb

Záznam ukládá přesně ten IQ stream, který detektor viděl. Chybu stačí
zreprodukovat příkazem:

```text
./build/sx1255-multimode -config replay.yaml
```

nebo jako regresní test nad stejným souborem.

## HTTP diagnostika

```text
GET /api/iq
```

```json
{
  "recording": true,
  "replay": false,
  "captured_samples": 12345678,
  "file": "capture.iq"
}
```

V replay režimu je `replay: true` a `captured_samples` značí celkový počet
vzorků v souboru.

## Testy

- `test_iq_recording` — záznam deterministického zdroje, integrita hlavičky a
  bitová shoda vzorků po zpětném načtení.
- `test_iq_replay` — čtení ve více dávkách, shoda vzorků, `eof()`, chování po
  konci souboru.
- `test_multimode_replay` — plná smyčka `IQReplay → RadioFrontend →
  IQDispatcher → pluginy` pro FM+CTCSS, D-Star a M17; ověřuje, že vznikne
  `ACTIVE_FM`, `ACTIVE_DSTAR` a `ACTIVE_M17`.
- `test_runtime_stats` — JSON výstup `GET /api/iq`.
