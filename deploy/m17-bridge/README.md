# Samostatný m17-bridge (APRS)

Balíček obsahuje arm64 binárku `m17-bridge`, APRS konfiguraci a systemd unit.

## Build

```text
./build-deb.sh
```

Výsledek: `build/m17-bridge_0.8.0-1_arm64.deb`.

Ve výchozím stavu se používá binárka z repozitáře m17:

```text
/home/xkk/projects/repeater/m17/cmd/m17-bridge/m17-bridge
```

Jiná binárka se předá druhým argumentem:

```text
./build-deb.sh /tmp/m17-bridge.deb /cesta/k/m17-bridge
```

## Instalace

```text
sudo apt install ./m17-bridge_0.8.0-1_arm64.deb
sudo nano /etc/m17-bridge.ini
sudo systemctl start m17-bridge
```

`[General] Name` je APRS volací znak a musí odpovídat licenci. `ListenAddress`
musí být adresa, na kterou se připojí multimode (typicky `0.0.0.0` nebo IP
Pi 5 v LAN).

## Použití s multimode

V multimode nastav druhé M17 připojení:

```yaml
m17:
  bridge:
    enabled: true
    host: <IP Pi 5>
    port: 17000
    module: A
```

Hlasový reflektor `m17.reflector` může zůstat zapnutý současně.
