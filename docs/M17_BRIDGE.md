# M17 bridge: zprávy a poloha

M17 plugin umí přenášet packet mode oběma směry. Díky tomu se dá napojit na
`m17-bridge` s APRS modulem a přenášet:

- M17 SMS zprávy mezi RF a APRS-IS,
- GNSS polohu z LSF rámců do APRS,
- případně další M17 packet typy (`AX25`, `APRS`, …).

## Zapojení

V multimode se přidá druhé M17 inet připojení pro `m17-bridge`; hlasový
reflektor (`m17.reflector`) může zůstat zapnutý současně:

```yaml
m17:
  enabled: true
  callsign: OK1LBC
  bridge:
    enabled: true
    host: 127.0.0.1     # kde běží m17-bridge
    port: 17001         # mimo port mrefd (17000)
    module: A           # modul, na kterém je v m17-bridge APRS module
  reflector:
    enabled: true
    host: mrefd.example.org
    port: 17000
    module: B
```

Na straně `m17-bridge` musí být zapnutý APRS modul a připojení na APRS-IS
(`Server`, `Symbol`, případně přihlašovací údaje podle konfigurace).

## Tok dat

```text
M17 rádio -- SMS/GNSS --> multimode -- M17 packet/stream --> m17-bridge --> APRS-IS
APRS-IS --> m17-bridge -- M17 packet/stream --> multimode --> RF
```

- Packet z RF se po ověření CRC pošle jako `M17P` datagram do `m17-bridge`.
- Packet z `m17-bridge` se zrekonstruuje a vyšle na RF.
- LSF se GNSS metadaty se přeposílá ve stream datagramu; APRS modul z něj
  vytvoří polohový report.

## Omezení

- `m17-bridge` APRS modul zpracovává M17 SMS a GNSS, ne obecné AX.25/FlexNet
  rámce.

## Testy

- `m17_packet_loopback` — M17 packet TX → RX dekóduje stejný typ, payload
  i CRC.

## Příjem packetů

Jak je zaručené, že se neztratí žádný packet (co bylo opravené, jaké jsou
čítače v telemetrii a jak ladit prahy), popisuje
[M17_PACKET_RX.md](M17_PACKET_RX.md).

## Webové rozhraní

`m17-bridge` umí webový dashboard se seznamem odeslaných i přijatých zpráv a
formulářem pro odeslání SMS na RF (sekce `[Web]` v konfiguraci, výchozí port
17080). Popis, API a konfigurace jsou v [M17_BRIDGE_WEB.md](M17_BRIDGE_WEB.md).
