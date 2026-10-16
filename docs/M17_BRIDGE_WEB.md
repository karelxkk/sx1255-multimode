# Webové rozhraní pro M17 zprávy (m17-bridge)

Webový dashboard v `m17-bridge`, který zobrazuje odeslané i přijaté textové
zprávy a umí z formuláře odeslat SMS na RF. Rozhraní je součástí balíčku
`m17-bridge` verze **0.9.0** a běží na serveru (Pi 5, `192.168.0.184`).

## 1. Co rozhraní umí

- **Seznam zpráv** – všechny zprávy, které bridge zpracoval, s časem, směrem,
  zdrojovým a cílovým znakem, textem, transportem a stavem:
  - `RF → APRS-IS` – SMS z rádia převedená na APRS zprávu,
  - `APRS-IS → RF` – APRS zpráva odeslaná rádiím na RF,
  - `WEB → RF` – zpráva odeslaná z dashboardu.
- **Formulář pro odeslání SMS** – komu + text; zpráva se vyšle jako M17 SMS
  na RF. Odesílá se **jen na RF**, na APRS-IS se nekopíruje.
- **Stav** – připojení APRS-IS, zaregistrovaní APRS uživatelé (mapování
  M17 znak ↔ APRS znak), připojení klienti modulu (tj. je-li rádio/hotspot
  na příjmu) a počty zpráv podle směru.

Zdrojový znak zprávy je vždy **stanice, která ji odeslala** (`LSF.Src`), ne
gateway znak `OK1LBC`. `OK1LBC` se objevuje jen jako login APRS-IS a jako
znak zpráv odeslaných z dashboardu.

## 2. Ovládání

```text
http://192.168.0.184:17080/
```

Formulář má jen dvě pole (komu a text). Tlačítko odešle zprávu do éteru;
panel nad tabulkou ukáže, zda je nějaké rádio připojené – když není, API
vrátí chybu a nikam se nevysílá.

Limit: `MaxTextLength` znaků (výchozí 67 podle limitu APRS zprávy) a
`MinSendInterval` sekund mezi dvěma zprávami (výchozí 2 s), aby se sdílený
kanál nezaplavil.

## 3. Konfigurace

Sekce `[Web]` v `/etc/m17-bridge.ini`:

```ini
[Web]
# Prázdné ListenAddress = dashboard je vypnutý.
ListenAddress=0.0.0.0
ListenPort=17080
# Zdrojový znak zpráv odeslaných z webu.
DefaultSource=OK1LBC
# Historie zpráv.
MaxMessages=500
StorePath=/var/lib/m17-bridge/messages.jsonl
# Maximální délka textu a odstup mezi zprávami z webu.
MaxTextLength=67
MinSendInterval=2
```

Bez `[Web]` (nebo s prázdným `ListenAddress`) se bridge chová jako dřív.
Adresář `/var/lib/m17-bridge` vytváří systemd (`StateDirectory=m17-bridge`)
vlastněný uživatelem `m17-bridge`.

## 4. HTTP API

| metoda | cesta | popis |
| --- | --- | --- |
| GET | `/` | HTML dashboard s formulářem |
| GET | `/api/status` | stav APRS-IS, klienti modulu, čítače zpráv |
| GET | `/api/messages?limit=&since=&direction=&q=` | seznam zpráv (nejnovější první) |
| POST | `/api/messages/send` | odeslání SMS na RF |
| GET | `/healthz` | `ok` pro monitoring |

`GET /api/status`:

```json
{
  "web": { "version": "0.9.0", "uptime_s": 3600, "module": "A",
           "default_source": "OK1LBC", "max_text_length": 67 },
  "aprs": { "server": "euro.aprs2.net", "callsign": "OK1LBC", "connected": true,
            "filter": "g/OK1ABC*/OK1XYZ*",
            "users": [ { "m17": "OK1ABC", "aprs": "OK1ABC-1",
                         "last_heard": "2026-10-16T21:10:39Z" } ] },
  "clients": [ { "callsign": "OK1LBC", "module": "A",
                 "addr": "192.168.1.207:34940", "listen_only": false } ],
  "counters": { "rf2aprs": 1, "web2rf": 2 }
}
```

`POST /api/messages/send`:

```json
{ "to": "OK1ABC-7", "text": "Ahoj z webu" }
```

Znak se normalizuje (`OK1ABC-7` → `OK1ABC 7`) a ověří. Odpovědi: `201`
(zpráva odeslána, vrací uloženou zprávu), `400` (neplatný znak, prázdný nebo
příliš dlouhý text), `409` (na modulu není žádný klient, není kam vysílat),
`429` (příliš rychle po sobě).

## 5. Uložení zpráv

Kruhová fronta v paměti (`MaxMessages`) + append-only JSONL
`/var/lib/m17-bridge/messages.jsonl` s rotací po 1 MiB (`messages.jsonl.1`).
Po restartu se historie načte zpět, takže dashboard neztratí přehled.

```json
{"id":12,"time":"2026-10-16T21:13:59Z","direction":"rf2aprs","module":"A",
 "src":"OK1ABC","dst":"OK1XYZ","type":"SMS","text":"Ahoj z M17",
 "transport":"APRS-IS","status":"sent",
 "detail":"OK1ABC-1>APZ001::OK1XYZ   :Ahoj z M17"}
```

## 6. Jak to funguje

```text
 rádio --RF--> Pi Zero (multimode) --M17P--> m17-bridge --> APRS-IS
                                                |
                                        [ úložiště zpráv ]
                                                |
                                    dashboard :17080 (GET / POST)
                                                |
                            SendPacketToModule('A') --> multimode --> RF
```

- Zpráva z RF projde `APRSModule.HandlePacket`, pošle se na APRS-IS a zapíše
  se jako `rf2aprs`.
- Zpráva z APRS-IS pro registrovaný znak se přeloží na M17 packet, pošle
  klientům modulu (na RF) a zapíše se jako `aprs2rf`; pokud znak nemá M17
  protějšek, zapíše se jako `failed`, aby bylo vidět, co se nedoručilo.
- Zpráva z dashboardu jde přímo přes `inet.Server.SendPacketToModule()`,
  tedy **mimo APRS modul** – na APRS-IS se nedostane a nevzniká duplicitní
  zpráva.

## 7. Bezpečnost

Podle rozhodnutí **bez hesla**: rozhraní je jen v LAN, neposlouchá se na
veřejné adrese a nikdo ho neforwarduje do internetu. Kdokoli v LAN ale může
vysílat do éteru, proto:

- `MinSendInterval` a `MaxTextLength` jako základní pojistka,
- `DefaultSource` musí být licencovaný znak operátora.

Kdyby se mělo rozhraní vystavit dál, patří před něj reverzní proxy s TLS
a HTTP Basic auth (případně `[Web] Username/Password`, což je malé rozšíření
`web.handler()`).

## 8. Logování

Zprávy se logují na úrovni **INFO**, takže jsou vidět v journalu i s běžným
`Level=INFO` (dřív byly všechny packet řádky na `DEBUG` a provoz byl neviditelný):

```text
[INFO] RF2APRS OK1ABC>OK1XYZ: "Ahoj z M17" (sent)
[INFO] APRS2RF OK1XYZ>OK1ABC: "Test" (sent)
[INFO] WEB2RF OK1LBC>OK1ABC: "Ahoj z webu" (sent)
```

`Level=DEBUG` navíc vypisuje APRS-IS provoz a M17 pakety; u plného APRS-IS
feedu to může být hodně řádků, proto zůstává výchozí INFO.

## 9. Testy

- Unit testy `m17/web` (`go test ./web/...`): ukládání a filtry zpráv,
  perzistence, validace znaku a textu, rate limit, chybějící rádio (`409`),
  stavový endpoint.
- Lokální end-to-end test: bridge + falešný hotspot (`CONN`/`ACKN`/`PONG`)
  na loopbacku ověří, že zpráva z formuláře dorazí jako M17 packet
  `src=OK1LBC dst=OK1ABC 7 type=0x05 payload="Ahoj z webu\0"`.
- Na cíli: `curl http://192.168.0.184:17080/api/status` musí vidět multimode
  jako klienta modulu A; po odeslání z webu se v logu multimode objeví
  `M17 NET TX packet: dst=… src=…` a čítač `bridge_rx_packets` se zvýší.

Ověřeno na cíli 16. 10. 2026 (balíček 0.9.0 na `192.168.0.184`, multimode na
`192.168.1.207`): dashboard vidí klienta `OK1LBC  R` (multimode), zpráva
z formuláře se zapsala do historie a multimode ji vyslal –
`M17 NET TX packet: dst=OK1LBC src=OK1LBC payload=21 bytes`, `bridge_rx_packets`
se zvýšil z 0 na 1.

## 10. Rozdíl proti upstreamu

Změny jsou lokální commit v klonu `github.com/jancona/m17`
(`/home/xkk/projects/repeater/m17`) nad commitem `83b77c6`; jako patch jsou
uložené v `deploy/m17-bridge/patches/0001-m17-bridge-web-dashboard.patch`.
Jsou aditivní a dají se poslat upstreamu:

| soubor | změna |
| --- | --- |
| `web/` (nový balíček) | úložiště zpráv, HTTP API, HTML dashboard, testy |
| `server/aprs_module.go` | `Recorder`, `Status()`, INFO logování zpráv, start bez APRS-IS |
| `inet/server.go` | `Clients()`, `ClientsByModule()`, `Close()` bez paniky |
| `cmd/m17-bridge/bridge.go` | sekce `[Web]`, odeslání SMS, nenulový exit při pádu |

## 11. Další možné kroky

- Volitelná HTTP Basic auth, pokud se dashboard vystaví i mimo LAN.
- Zobrazení GNSS polohových reportů (teď se ukládají jen textové zprávy).
- Potvrzování zpráv (`ack`) a stav „doručeno“ u obou směrů.
