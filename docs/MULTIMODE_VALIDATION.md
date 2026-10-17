# Validace multimódové arbitráže

Tento dokument popisuje, jak `sx1255-multimode` rozpoznává více režimů nad
jediným společným IQ streamem, jak se rozhoduje o vlastnictví relace a proč je
pořadí priorit právě **M17 > D-Star > FM**.

## Princip

Frontend (`RadioFrontend` → `IQDispatcher`) doručuje každý IQ blok **všem**
registrovaným pluginům najednou. Každý plugin je nezávislý detektor:

```text
                IQBlock (125 kSa/s)
                        |
                        v
               +----------------+
               |  IQDispatcher  |
               +----------------+
                |      |      |
                v      v      v
             +----+ +------+ +-----+
             | FM | |D-Star| | M17 |
             +----+ +------+ +-----+
                 \     |     /
                  v    v    v
               +----------------+
               | SessionManager |
               +----------------+
```

V režimu monitoringu dostane každý plugin stejná data a vrátí
`Detection{detected, confidence}`. Dispatcher předá všechny výsledky
`SessionManager::evaluateDetections()`, který určí vítěze.

## Arbitráž

Pravidla jsou jednoduchá a deterministická:

1. Detekce se seřadí podle `priority()` sestupně.
2. První plugin s `detected == true` získá exkluzivní vlastnictví.
3. Po dobu relace dostává IQ bloky **pouze** vítěz (`process()`), ostatní
   pluginy jsou odstaveny.
4. Po `stopSession()` (normální konec, timeout, EOT) přechází systém přes
   `RETURN_TO_IDLE` zpět do `IDLE` a monitoring začíná znovu.

Priorita je deklarovaná přímo v pluginu:

| Plugin | `priority()` | Stav relace |
|---|---|---|
| M17    | 30 | `ACTIVE_M17` |
| D-Star | 20 | `ACTIVE_DSTAR` |
| FM     | 10 | `ACTIVE_FM` |

## Proč M17 > D-Star > FM

Pořadí odráží **specifičnost a robustnost detekce**.

### M17 má nejvyšší prioritu

Detekce M17 je založená na synchronizačních slovech (`LSF_SYNC`, `STREAM_SYNC`,
`PACKET_SYNC`), CRC LSF rámce a Viterbi dekódování. Falešný záchyt na šumu je
velmi nepravděpodobný, protože vyžaduje přesnou korelaci se sync vzorem a
následnou validaci. Proto má M17 přednost, když se objeví digitální provoz.

### D-Star je uprostřed

D-Star detekce používá 24bitový frame sync (`0x557650`), který se hledá
s přesným porovnáním, a teprve po nalezení synchronizace se dekóduje 660bitová
FEC hlavička (descramble → deinterleave → Viterbi → CCITT CRC). CRC hlavičky
funguje jako silná ochrana proti falešným záchytům. Detekce je tedy také
specifická, ale D-Star má nižší prioritu než M17, aby byl M17 referenční
digitální implementací s předností.

### FM je poslední

FM detekce je z principu „nejměkčí“: aktivuje se na jakoukoli nosnou, která
zvedne RSSI nad práh (`frontend.squelch`). Protože digitální provoz je také
nosná, FM nesmí přebíjet digitální detekce. FM je záchytná síť, která se
uplatní, jen když žádný digitální režim nedetekuje; pokud FM kanál získá
dřív, vyšší priorita digitálního režimu ho po potvrzení převezme.

## Současná aktivita

Pokud dva režimy detekují ve **stejném IQ bloku**, vyhrává vždy ten s vyšší
prioritou:

```text
M17 sync nalezen          → ACTIVE_M17
M17 nenalezen, D-Star ano → ACTIVE_DSTAR
nic digitálního, nosná utiší šum → ACTIVE_FM
```

Jakmile relace začne, ostatní pluginy dál dostávají IQ (aby se neztratil
packet) a `IQDispatcher` sleduje jejich detekci. Když mód s vyšší prioritou
hlásí nosnou dostatečně dlouho (5 bloků), aktuální vlastník relaci uvolní a
příští blok ji předá vítězi.

## Ověření

Unit testy pokrývají každou vrstvu zvlášť:

- `test_dstar_detect` — validní/nevalidní D-Star sync, GMSK RF smyčka
  (modulace → diskriminátor → detektor), FM vstup a šum musí být odmítnuty.
- `test_dstar_loopback` — D-Star TX modulátor (hlavička + DV rámce + EOT) →
  RX detektor dekóduje identické události.
- `test_dstar_plugin` — D-Star plugin přes falešný `ITransmit`: PTT, TX IQ,
  drain a ukončení relace po dojezdu.
- `test_dstar_gateway` — DSRP UDP klient: header/data round-trip proti
  falešnému gateway procesu.
- `test_dstar_netplugin` — síť → RF: DSRP gateway → plugin → falešný
  `ITransmit`/arbiter.
- `test_fm_detect` — RSSI squelch FM nosné.
- `test_m17_loopback` — M17 TX → RX dekóduje identický LSF.
- `test_session_manager` — priority a přechody stavů.
- `test_runtime_stats` — čítače detekcí/relací a JSON výstupy HTTP serveru.

Společný scénář „jeden stream, tři detektory“ ověřuje, že architektura je
skutečně rozšiřitelná beze změny frontend vrstvy.

## Runtime diagnostika

Statistiky agreguje `SystemStatistics`, HTTP server vystavuje:

```text
GET /api/status
GET /api/plugins
```

Příklad `/api/status`:

```json
{
  "state": "ACTIVE_M17",
  "mode": "M17",
  "rssi": -82,
  "uptime": 15324,
  "stats": {
    "fm_detected": 244,
    "dstar_detected": 17,
    "m17_detected": 83,
    "false_positives": 0
  }
}
```

`/api/plugins` vrací seznam registrovaných pluginů s jejich stavem
`enabled`.
