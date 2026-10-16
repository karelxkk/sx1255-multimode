# M17 bridge: zprávy a poloha

M17 plugin umí přenášet packet mode oběma směry. Díky tomu se dá napojit na
`m17-bridge` s APRS modulem a přenášet:

- M17 SMS zprávy mezi RF a APRS-IS,
- GNSS polohu z LSF rámců do APRS,
- případně další M17 packet typy (`AX25`, `APRS`, …).

## Zapojení

V multimode se použije stávající M17 inet klient a nastaví se na `m17-bridge`
místo hlasového reflektoru:

```yaml
m17:
  enabled: true
  callsign: OK1LBC
  reflector:
    enabled: true
    host: 127.0.0.1     # kde běží m17-bridge
    port: 17000
    module: A           # modul, na kterém je v m17-bridge APRS module
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

- M17 plugin má jednu konfiguraci `m17.reflector`; nelze současně poslouchat
  hlasový reflektor a APRS bridge na dvou různých serverech. Pro souběh by
  bylo potřeba přidat druhý M17 inet klient.
- `m17-bridge` APRS modul zpracovává M17 SMS a GNSS, ne obecné AX.25/FlexNet
  rámce.

## Testy

- `m17_packet_loopback` — M17 packet TX → RX dekóduje stejný typ, payload
  i CRC.
