# Samostatný m17-bridge (APRS)

Balíček obsahuje arm64 binárku `m17-bridge`, APRS konfiguraci, systemd unit
a webový dashboard se seznamem zpráv a formulářem pro odeslání SMS.

## Build

```text
./build-deb.sh
```

Výsledek: `build/m17-bridge_0.9.0_arm64.deb`.

Pokud je dostupné Go a zdrojový strom m17, balíček se přebuilduje z aktuálního
zdroje:

```text
/home/xkk/projects/repeater/m17
```

Zdrojový strom je klon `github.com/jancona/m17` s lokálním commitem, který
přidává dashboard; jako patch je v
`patches/0001-m17-bridge-web-dashboard.patch`. Jinak se použije předpřipravená
binárka, nebo se předá druhým argumentem:

```text
./build-deb.sh /tmp/m17-bridge.deb /cesta/k/m17-bridge
```

## Instalace

```text
sudo apt install ./m17-bridge_0.9.0_arm64.deb
sudo nano /etc/m17-bridge.ini
sudo systemctl start m17-bridge
```

`[General] Name` je APRS volací znak a musí odpovídat licenci. `ListenAddress`
musí být adresa, na kterou se připojí multimode (typicky `0.0.0.0` nebo IP
Pi 5 v LAN).

## Webový dashboard

```text
http://<IP serveru>:17080/
```

Sekce `[Web]` v `/etc/m17-bridge.ini` zapíná seznam zpráv a formulář pro
odeslání SMS na RF (prázdné `ListenAddress` = vypnuto). Historie zpráv se
ukládá do `/var/lib/m17-bridge/messages.jsonl` (adresář vytváří systemd přes
`StateDirectory`). Rozhraní není chráněné heslem, proto poslouchej jen v LAN.

Test:

```text
curl http://127.0.0.1:17080/api/status
curl -X POST -H 'Content-Type: application/json' \
     -d '{"to":"OK1ABC","text":"Ahoj z webu"}' \
     http://127.0.0.1:17080/api/messages/send
```

Detaily (API, datový model, bezpečnost) jsou v `../../docs/M17_BRIDGE_WEB.md`.

## Použití s multimode

V multimode nastav druhé M17 připojení:

```yaml
m17:
  bridge:
    enabled: true
    host: <IP Pi 5>
    port: 17001
    module: A
```

Hlasový reflektor `m17.reflector` může zůstat zapnutý současně.
